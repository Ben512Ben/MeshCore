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

// ---------------------------------------------------------------------------------------------------------------
// Lists. A list reads its names straight from the tables (or from an array the caller gives), so a town can hold
// any number of trails: there is no item limit and no array on the stack. A list that does not fit in one
// answer (BOT_Q_MAX messages) is split into pages: "!springbrook 2" shows the next page.
struct TbSrc {
  int kind;                    // 0 = the regions, 1 = the towns of region 'id', 2 = the trails of town 'id', 3 = the array 'arr'
  int id;
  const char* const* arr;
  int n;
};

static int tbCount(const TbSrc* s) {
  if (s->kind == 3) return s->n;
  if (s->kind == 0) return TRAIL_REGION_COUNT;
  int n = 0;
  if (s->kind == 1) { for (int i = 0; i < TRAIL_TOWN_COUNT; i++) if (TRAIL_TOWNS[i].region == s->id) n++; }
  else { for (int i = 0; i < TRAIL_COUNT; i++) if (TRAILS[i].town == s->id) n++; }
  return n;
}

// the k-th name of the list (k counts from 0)
static const char* tbName(const TbSrc* s, int k) {
  if (s->kind == 3) return s->arr[k];
  if (s->kind == 0) return TRAIL_REGIONS[k].name;
  if (s->kind == 1) {
    for (int i = 0; i < TRAIL_TOWN_COUNT; i++) if (TRAIL_TOWNS[i].region == s->id && k-- == 0) return TRAIL_TOWNS[i].name;
  } else {
    for (int i = 0; i < TRAIL_COUNT; i++) if (TRAILS[i].town == s->id && k-- == 0) return TRAILS[i].name;
  }
  return "";
}

// add as many "!name" items as fit in cap characters, starting at item 'from'. returns the next item.
static int tbPack(const TbSrc* s, int n, int from, int cap) {
  int used = 0, i = from;
  while (i < n) {
    int need = (i > from ? 1 : 0) + 1 + (int)strlen(tbName(s, i));
    if (used + need > cap) break;
    used += need;
    i++;
  }
  if (i == from && i < n) i++;   // a single name longer than a message: still make progress (it is cut on output)
  return i;
}

// The messages of one page of a list, at most BOT_Q_MAX. 'hint' is what to send for the next page ("springbrook"):
// when items are left, the last message ends with " +N more: !springbrook 2". With hint NULL there is only one page
// and the last message ends with " +N more". returns the number of messages (0 = the list is empty).
static int trailbotListPage(const TbSrc* s, int budget, int page, const char* hint, char out[][BOT_MSG_MAX + 1]) {
  int n = tbCount(s);
  if (n <= 0) return 0;
  int cap = budget - TRAILBOT_HDR_LEN;
  int reserve = hint ? 16 + (int)strlen(hint) : TRAILBOT_MORE_LEN;   // room kept at the end of a page that has a next page
  int start[BOT_Q_MAX + 1];
  int i = 0;
  for (int pg = 1; pg <= page && i < n; pg++) {   // pages before this one are skipped, laid out the same way
    int np = 0;
    while (i < n && np < BOT_Q_MAX) {
      start[np] = i;
      int next = tbPack(s, n, i, cap);
      if (np == BOT_Q_MAX - 1 && next < n) next = tbPack(s, n, i, cap - reserve);
      i = next;
      np++;
    }
    start[np] = i;
    if (pg == page) {
      int left = n - i;
      for (int p = 0; p < np; p++) {
        char* o = out[p];
        int pos = snprintf(o, BOT_MSG_MAX + 1, "(%d/%d)", p + 1, np);
        for (int k = start[p]; k < start[p + 1]; k++) {
          pos += snprintf(o + pos, BOT_MSG_MAX + 1 - pos, " !%s", tbName(s, k));
          if (pos > budget) break;
        }
        if (p == np - 1 && left > 0 && pos < BOT_MSG_MAX) {
          if (hint) pos += snprintf(o + pos, BOT_MSG_MAX + 1 - pos, " +%d more: !%s %d", left, hint, page + 1);
          else pos += snprintf(o + pos, BOT_MSG_MAX + 1 - pos, " +%d more", left);
        }
        if (pos > budget) pos = budget;   // never over the budget (cut, not wrapped)
        o[pos] = 0;
      }
      return np;
    }
  }
  snprintf(out[0], BOT_MSG_MAX + 1, "That was the last page");
  return 1;
}

// a list of names the caller already has (the wildlife lists): one page, "+N more" if it does not all fit
static int trailbotList(const char* const* names, int n, int budget, char out[][BOT_MSG_MAX + 1]) {
  TbSrc s = {3, 0, names, n};
  return trailbotListPage(&s, budget, 1, NULL, out);
}

// "springbrook 2": a page number (1 or 2 digits) at the end of the query. Cuts it off q and returns it, else returns 0.
static int trailbotPageNo(char* q) {
  char* sp = strrchr(q, ' ');
  if (!sp || sp == q || !sp[1]) return 0;
  int len = (int)strlen(sp + 1);
  if (len > 2) return 0;
  int v = 0;
  for (int i = 0; i < len; i++) {
    if (sp[1 + i] < '0' || sp[1 + i] > '9') return 0;
    v = v * 10 + (sp[1 + i] - '0');
  }
  if (v < 1) return 0;
  *sp = 0;
  return v;
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

  bool towns_only = false;   // "!trails <name>": never a trail
  if (strcmp(q, "trails") == 0) {   // the regions
    TbSrc s = {0, 0, NULL, 0};
    return trailbotListPage(&s, budget, 1, "trails", out);
  }
  if (strncmp(q, "trails ", 7) == 0) {
    memmove(q, q + 7, strlen(q + 7) + 1);
    towns_only = true;
    char rest[TRAILBOT_QUERY_MAX + 16];
    snprintf(rest, sizeof(rest), "x %s", q);   // "!trails 2": the next page of the regions
    int pg = trailbotPageNo(rest);
    if (pg > 0 && strcmp(rest, "x") == 0) {
      TbSrc s = {0, 0, NULL, 0};
      return trailbotListPage(&s, budget, pg, "trails", out);
    }
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
  int page = 1;
  {   // "!springbrook 2": a page number after a town or region name
    char qb[TRAILBOT_QUERY_MAX + 8];
    snprintf(qb, sizeof(qb), "%s", q);
    int pg = trailbotPageNo(qb);
    if (pg > 0 && (trailbotFindTown(qb) >= 0 || trailbotFindRegion(qb) >= 0)) {
      page = pg;
      snprintf(q, sizeof(q), "%s", qb);
    }
  }
  int town = trailbotFindTown(q);
  if (town >= 0) {
    TbSrc s = {2, town, NULL, 0};
    if (tbCount(&s) > 0) return trailbotListPage(&s, budget, page, TRAIL_TOWNS[town].name, out);
  }
  int region = trailbotFindRegion(q);
  if (region >= 0) {
    TbSrc s = {1, region, NULL, 0};
    if (tbCount(&s) > 0) return trailbotListPage(&s, budget, page, TRAIL_REGIONS[region].name, out);
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
