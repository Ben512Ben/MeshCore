#pragma once
/*
 * Trail info bot logic (item 32). Pure functions: no mesh, radio or Arduino calls, so it can be tested on a PC
 * (tools/trailbot_test.cpp). MyMesh.cpp feeds it the text after the '!' and queues the replies it returns.
 *
 *   !<trail or alias>   -> "<Trail name>: <distance>, <time>, <grade>, <features>"
 *   !<town or alias>    -> the trail names in that town, as numbered messages "(1/2) !Name !Name ..."
 *   !<region or alias>  -> the towns in that region, same format
 *   !trails             -> the regions, same format
 *   !trails <name>      -> same as !<name> but never a trail (a town, else a region)
 *   !test               -> five lines: "Hi @[sender]", "Time 14:32:07", "SNR 6.5dB", "RSSI -91dBm", "Hops 2"
 *                          (what THIS repeater heard)
 *   anything else       -> "Not found, try !trails"
 *
 * Every reply is at most BOT_MSG_MAX (134) characters INCLUDING the sender prefix "<node name>: ".
 * Lookup order: trail, then town, then region. A trail wins over a town with the same name. Replies never begin with '!', so two bot repeaters on one
 * channel can not trigger each other.
 */

#include <stdio.h>
#include <string.h>
#include "trails.h"

#ifndef BOT_MSG_MAX
  #define BOT_MSG_MAX 134      // total message length including "<node name>: "
#endif
#ifndef BOT_Q_MAX
  #define BOT_Q_MAX 4          // most messages one request can produce
#endif

#define TRAILBOT_HDR_LEN   6   // "(n/N) "
#define TRAILBOT_MORE_LEN  9   // " +NN more"
#define TRAILBOT_MAX_ITEMS 24
#define TRAILBOT_QUERY_MAX 48

// lowercase letters and digits only. apostrophes vanish (Moran's -> morans), other characters become one space.
// bytes of 0x80 and above (accents, curly quotes) are dropped.
static void trailbotNorm(const char* in, char* out, int outsz) {
  int n = 0;
  bool space = false;
  for (const unsigned char* p = (const unsigned char*)in; *p; p++) {
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (space && n > 0 && n < outsz - 1) out[n++] = ' ';
      space = false;
      if (n < outsz - 1) out[n++] = (char)c;
    } else if (c == '\'' || c >= 0x80) {
      // dropped: joins the letters on either side
    } else {
      space = true;
    }
  }
  out[n] = 0;
}

// does the normalised query q equal the (normalised) name, or one of the '|' separated aliases?
static bool trailbotMatches(const char* name, const char* aliases, const char* q) {
  char tmp[TRAILBOT_QUERY_MAX + 8];
  trailbotNorm(name, tmp, sizeof(tmp));
  if (strcmp(tmp, q) == 0) return true;
  const char* p = aliases;
  while (p && *p) {
    const char* e = strchr(p, '|');
    int len = e ? (int)(e - p) : (int)strlen(p);
    char seg[TRAILBOT_QUERY_MAX + 8];
    if (len > (int)sizeof(seg) - 1) len = (int)sizeof(seg) - 1;
    memcpy(seg, p, len);
    seg[len] = 0;
    trailbotNorm(seg, tmp, sizeof(tmp));
    if (tmp[0] && strcmp(tmp, q) == 0) return true;
    p = e ? e + 1 : NULL;
  }
  return false;
}

static int trailbotFindTrail(const char* q) {
  for (int i = 0; i < TRAIL_COUNT; i++) {
    if (trailbotMatches(TRAILS[i].name, TRAILS[i].aliases, q)) return i;
  }
  return -1;
}

static int trailbotFindRegion(const char* q) {
  for (int i = 0; i < TRAIL_REGION_COUNT; i++) {
    if (trailbotMatches(TRAIL_REGIONS[i].name, TRAIL_REGIONS[i].aliases, q)) return i;
  }
  return -1;
}

static int trailbotFindTown(const char* q) {
  for (int i = 0; i < TRAIL_TOWN_COUNT; i++) {
    if (trailbotMatches(TRAIL_TOWNS[i].name, TRAIL_TOWNS[i].aliases, q)) return i;
  }
  return -1;
}

// add as many "!name" items as fit in cap characters, starting at item 'from'. returns the next item.
static int trailbotPack(const char* const* names, int n, int from, int cap) {
  int used = 0, i = from;
  while (i < n) {
    int need = (i > from ? 1 : 0) + 1 + (int)strlen(names[i]);
    if (used + need > cap) break;
    used += need;
    i++;
  }
  if (i == from && i < n) i++;   // a single name longer than a page: still make progress (it is cut on output)
  return i;
}

// numbered list of "!name" items, at most BOT_Q_MAX messages. returns the number of messages.
static int trailbotList(const char* const* names, int n, int budget, char out[][BOT_MSG_MAX + 1]) {
  if (n <= 0) return 0;
  int cap = budget - TRAILBOT_HDR_LEN;
  int start[BOT_Q_MAX + 1];
  int np = 0, i = 0;
  while (i < n && np < BOT_Q_MAX) {
    start[np] = i;
    int next = trailbotPack(names, n, i, cap);
    if (np == BOT_Q_MAX - 1 && next < n) {   // last allowed message and items are left over: make room for "+N more"
      next = trailbotPack(names, n, i, cap - TRAILBOT_MORE_LEN);
    }
    i = next;
    np++;
  }
  start[np] = i;
  int left = n - i;
  for (int p = 0; p < np; p++) {
    char* o = out[p];
    int pos = snprintf(o, BOT_MSG_MAX + 1, "(%d/%d)", p + 1, np);
    for (int k = start[p]; k < start[p + 1]; k++) {
      pos += snprintf(o + pos, BOT_MSG_MAX + 1 - pos, " !%s", names[k]);
      if (pos > budget) break;
    }
    if (p == np - 1 && left > 0 && pos < BOT_MSG_MAX) {
      pos += snprintf(o + pos, BOT_MSG_MAX + 1 - pos, " +%d more", left);
    }
    if (pos > budget) pos = budget;   // never over the budget (cut, not wrapped)
    o[pos] = 0;
  }
  return np;
}

static int trailbotRegionTowns(int region, const char** names, int max) {
  int n = 0;
  for (int i = 0; i < TRAIL_TOWN_COUNT && n < max; i++) {
    if (TRAIL_TOWNS[i].region == region) names[n++] = TRAIL_TOWNS[i].name;
  }
  return n;
}

static int trailbotTownTrails(int town, const char** names, int max) {
  int n = 0;
  for (int i = 0; i < TRAIL_COUNT && n < max; i++) {
    if (TRAILS[i].town == town) names[n++] = TRAILS[i].name;
  }
  return n;
}

// 'query' is the message text after the '!'. 'node_name' is this repeater's name (the sender prefix is
// "<node name>: ", cut at 39 characters like postChannel does). Fills out[] with ready-to-send message texts
// (WITHOUT the prefix) and returns how many (0 = send nothing).
// like trailbotReply() but returns -1 (writes nothing) when the query is not a trail, town, region or "trails"
static int trailbotLookup(const char* query, const char* node_name, char out[][BOT_MSG_MAX + 1]) {
  char q[TRAILBOT_QUERY_MAX + 8];
  trailbotNorm(query, q, sizeof(q));
  if (q[0] == 0) return 0;   // a lone "!" : ignore

  int prefix = (int)strlen(node_name) + 2;
  if (prefix > 39) prefix = 39;
  int budget = BOT_MSG_MAX - prefix;   // characters left for the text itself
  const char* names[TRAILBOT_MAX_ITEMS];

  bool towns_only = false;   // "!trails <name>": never a trail
  if (strcmp(q, "trails") == 0) {   // the regions
    int n = 0;
    for (int i = 0; i < TRAIL_REGION_COUNT && n < TRAILBOT_MAX_ITEMS; i++) names[n++] = TRAIL_REGIONS[i].name;
    return trailbotList(names, n, budget, out);
  }
  if (strncmp(q, "trails ", 7) == 0) {
    memmove(q, q + 7, strlen(q + 7) + 1);
    towns_only = true;
  }

  if (!towns_only) {
    int t = trailbotFindTrail(q);
    if (t >= 0) {
      int pos = snprintf(out[0], BOT_MSG_MAX + 1, "%s: %s", TRAILS[t].name, TRAILS[t].info);
      if (pos > budget) pos = budget;   // cut to fit
      out[0][pos] = 0;
      return 1;
    }
  }
  int town = trailbotFindTown(q);
  if (town >= 0) {
    int n = trailbotTownTrails(town, names, TRAILBOT_MAX_ITEMS);
    if (n > 0) return trailbotList(names, n, budget, out);
  }
  int region = trailbotFindRegion(q);
  if (region >= 0) {
    int n = trailbotRegionTowns(region, names, TRAILBOT_MAX_ITEMS);
    if (n > 0) return trailbotList(names, n, budget, out);
  }
  return -1;
}

static int trailbotReply(const char* query, const char* node_name, char out[][BOT_MSG_MAX + 1]) {
  int n = trailbotLookup(query, node_name, out);
  if (n >= 0) return n;
  snprintf(out[0], BOT_MSG_MAX + 1, "Not found, try !trails");
  return 1;
}

// '!test': is the (normalised) query exactly "test"?
static bool trailbotIsTest(const char* query) {
  char q[TRAILBOT_QUERY_MAX + 8];
  trailbotNorm(query, q, sizeof(q));
  return strcmp(q, "test") == 0;
}

// reply to '!test' with what this repeater heard of the message, one item per line:
//   Hi @[<sender>] / Time <t> / SNR <x.y>dB / RSSI <n>dBm / Hops <h>
// sender / slen: the sender name (not 0 terminated), snr10: SNR in tenths of a dB, rssi: dBm, hops: repeaters the
// message passed through (0 = heard directly). The sender name is cut if needed so the reply fits in 'budget'.
// 'out' must hold BOT_MSG_MAX + 1 bytes. returns the length.
static int trailbotTestReply(const char* sender, int slen, int budget, const char* tstr, int snr10, int rssi, int hops, char* out) {
  char tail[64];
  int a = snr10 < 0 ? -snr10 : snr10;
  int tl = snprintf(tail, sizeof(tail), "\nTime %s\nSNR %s%d.%ddB\nRSSI %ddBm\nHops %d", tstr, snr10 < 0 ? "-" : "", a / 10, a % 10,
                    rssi, hops);
  if (tl < 0 || tl >= (int)sizeof(tail)) tl = (int)sizeof(tail) - 1;
  int room = budget - tl - 6;   // "Hi @[" + name + "]"
  if (slen > room) slen = room;
  if (slen < 0) slen = 0;
  int pos = snprintf(out, BOT_MSG_MAX + 1, "Hi @[%.*s]%s", slen, sender, tail);
  if (pos > budget) pos = budget;
  if (pos > BOT_MSG_MAX) pos = BOT_MSG_MAX;
  out[pos] = 0;
  return pos;
}
