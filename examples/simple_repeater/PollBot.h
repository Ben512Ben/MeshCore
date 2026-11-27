#pragma once
// ---------------------------------------------------------------------------------------------------------------
// PollBot.h - the "poll" feature of the trail bot: logs in to repeaters that YOU select, as a GUEST (blank guest password),
// asks for their whole neighbour list and keeps the answers, separate from the command channel ledger (CmdBot.h).
//
// This file holds only plain functions and data (no Arduino, no radio), so it can be tested on a PC.
// The radio side (sending the login / request, receiving the answers, the commands) is in MyMesh.cpp ("poll bot").
//
// What is kept for each selected repeater: its public key, name, hops, the route to it, and up to POLL_MAX_NEIGH
// neighbours (4 byte key prefix, when it was heard, SNR). Everything is in RAM only, nothing is saved to flash.
// ---------------------------------------------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#ifndef POLL_MAX_REPS
  #define POLL_MAX_REPS   32      // selected repeaters (when full, 'poll add' refuses until one is removed)
#endif
#ifndef POLL_MAX_NEIGH
  #define POLL_MAX_NEIGH  50      // neighbours kept per polled repeater (a repeater never holds more than 50)
#endif
#ifndef POLL_SEEN_MAX
  #define POLL_SEEN_MAX   32      // adverts remembered, so 'poll <name>' can find a repeater that has been heard
#endif
#define POLL_NAME_MAX     31
#define POLL_KEY_LEN      4       // bytes of each neighbour's public key asked for
#define POLL_PAGE_COUNT   13      // neighbours asked for per request (the repeater's reply holds 14 of this size)
#define POLL_MAX_PAGES    4       // requests per poll: POLL_PAGE_COUNT * POLL_MAX_PAGES = 52, enough for a full list of 50
#define POLL_PATH_MAX     16      // longest stored route in bytes (routes longer than this are not used: flood instead)
#define POLL_ZERO_MIN_S   (24UL * 3600UL)   // a zero hop repeater is polled again on its first advert after 24 .. 36 hours
#define POLL_ZERO_MAX_S   (36UL * 3600UL)   //   (the wait is picked at random each time)
#define POLL_REPOLL_S     (24UL * 3600UL)   // any other repeater: every advert, but at least 24 hours between polls
#define POLL_GAP_MS       60000UL // at least this long between the end of one poll and the start of the next automatic one
#define POLL_STALE_S      (14UL * 86400UL)  // 'poll stale': no good poll for this long
#define POLL_DELAY_MIN_MS 5000UL  // a poll starts 5..30 s after the advert that triggered it
#define POLL_DELAY_MAX_MS 30000UL
#define POLL_TO_DIRECT_MS 12000UL // wait for an answer: sent along a known route
#define POLL_TO_FLOOD_MS  25000UL // ... or flooded
#define POLL_OUT_UNKNOWN  0xFF    // no route known

#define POLL_PEER_FLAG    0x1000  // in matching_peer_indexes[]: the peer is a polled repeater (the low byte is its slot)

enum { PS_IDLE = 0, PS_WAIT_LOGIN = 1, PS_WAIT_REQ = 2 };

struct PollNb {            // one neighbour of a polled repeater
  uint32_t heard;          // when the polled repeater heard it (our clock, seconds)
  uint8_t key[POLL_KEY_LEN];
  int8_t snr;              // dB x 4, as in the repeater's own table
  uint8_t fresh;           // seen in the poll that is running (entries not seen in a complete poll are dropped)
};

struct PollRep {           // one selected repeater
  uint8_t used;            // slot in use
  uint8_t ok;              // at least one poll has worked: the data below is real
  uint8_t state;           // PS_*
  uint8_t attempt;         // 0 = first try, 1 = second try (flood)
  uint8_t page;            // requests already answered in this poll
  uint8_t pretry;          // a page was asked for again (flooded): the rest of this poll is flooded too
  uint8_t session;         // a login worked: the next poll tries the request first
  uint8_t pend;            // a poll is waiting for due_ms: 1 = after an advert, 2 = told to by a command
  uint8_t hops;            // hops of its last advert
  uint8_t out_len;         // route to it: encoded path length (hash size in the top 2 bits, count below), or POLL_OUT_UNKNOWN
  uint8_t nn;              // neighbours stored
  uint8_t pub[32];
  uint8_t secret[32];      // shared secret with it
  uint8_t out_path[POLL_PATH_MAX];
  char name[POLL_NAME_MAX + 1];
  uint16_t total;          // neighbours the repeater said it has, at the last good poll
  uint32_t added;          // clock (seconds) when it was selected
  uint32_t last_adv;       // clock (seconds) of its last advert, 0 = none heard since it was selected
  uint32_t last_try;       // clock (seconds) of the last attempt, 0 = never
  uint32_t next_ok;        // clock (seconds): an advert before this does not start a poll
  uint32_t last_ok;        // clock (seconds) of the last good poll
  uint32_t req_ts;         // timestamp of the request in flight (the repeater echoes it back)
  unsigned long due_ms;      // millis(): start the poll then
  unsigned long deadline_ms; // millis(): give up waiting for the answer then
  PollNb nb[POLL_MAX_NEIGH];
};

struct PollSeen {          // an advert heard from a repeater
  uint8_t used;
  uint8_t hops;
  uint8_t out_len;         // encoded path length of the route TO that repeater (reversed from the advert), or POLL_OUT_UNKNOWN
  uint8_t pub[32];
  uint8_t out_path[POLL_PATH_MAX];
  char name[POLL_NAME_MAX + 1];
  uint32_t heard;          // millis() / 1000 when heard (only used to find the oldest)
};

// ---------------------------------------------------------------------------------------------------------------
// small helpers

static inline char pollLc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static inline bool pollNameEq(const char* a, const char* b) {
  while (*a && *b) {
    if (pollLc(*a) != pollLc(*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

static inline int pollHexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// 2 to 64 hex characters (even count) -> bytes. Returns the number of bytes, 0 if it is not hex.
static inline int pollParseHex(const char* s, uint8_t* out, int max_bytes) {
  size_t n = strlen(s);
  if (n < 2 || (n & 1) || (int)(n / 2) > max_bytes) return 0;
  for (size_t i = 0; i < n; i += 2) {
    int hi = pollHexVal(s[i]), lo = pollHexVal(s[i + 1]);
    if (hi < 0 || lo < 0) return 0;
    out[i / 2] = (uint8_t)((hi << 4) | lo);
  }
  return (int)(n / 2);
}

// The path of a flood packet lists the repeaters it went through, the one nearest its sender first. A packet sent back
// the other way needs that list reversed (a hash is 1 to 4 bytes: whole hashes are swapped, not single bytes).
// src_len is the packet's encoded path length. Returns the encoded length of the reversed route (0 = zero hops, the
// repeater is a direct neighbour), or POLL_OUT_UNKNOWN when the route does not fit.
static inline uint8_t pollReversePath(const uint8_t* src, uint8_t src_len, uint8_t* dst, size_t dst_cap) {
  uint8_t count = src_len & 63;
  uint8_t hsz = (uint8_t)((src_len >> 6) + 1);
  if (count == 0) return 0;
  if ((size_t)count * hsz > dst_cap) return POLL_OUT_UNKNOWN;
  for (uint8_t i = 0; i < count; i++) {
    memcpy(&dst[(size_t)i * hsz], &src[(size_t)(count - 1 - i) * hsz], hsz);
  }
  return src_len;
}

// ---------------------------------------------------------------------------------------------------------------
// the tables

static inline int pollFindRep(const PollRep* t, int n, const uint8_t* pub) {
  for (int i = 0; i < n; i++) {
    if (t[i].used && memcmp(t[i].pub, pub, 32) == 0) return i;
  }
  return -1;
}

static inline int pollFindRepByName(const PollRep* t, int n, const char* name) {
  for (int i = 0; i < n; i++) {
    if (t[i].used && pollNameEq(t[i].name, name)) return i;
  }
  return -1;
}

// a free slot, or -1 when all are in use (a selected repeater is never replaced: it has to be removed first)
static inline int pollFreeSlot(const PollRep* t, int n) {
  for (int i = 0; i < n; i++) {
    if (!t[i].used) return i;
  }
  return -1;
}

static inline int pollSeenFind(const PollSeen* t, int n, const uint8_t* pub) {
  for (int i = 0; i < n; i++) {
    if (t[i].used && memcmp(t[i].pub, pub, 32) == 0) return i;
  }
  return -1;
}

// remember (or refresh) an advert. Returns the slot.
static inline int pollSeenPut(PollSeen* t, int n, const uint8_t* pub, const char* name, uint8_t hops, uint8_t out_len,
                              const uint8_t* out_path, uint32_t stamp) {
  int i = pollSeenFind(t, n, pub);
  if (i < 0) {
    for (int k = 0; k < n && i < 0; k++) {
      if (!t[k].used) i = k;
    }
    if (i < 0) {   // full: replace the advert heard longest ago
      i = 0;
      for (int k = 1; k < n; k++) {
        if ((int32_t)(t[k].heard - t[i].heard) < 0) i = k;
      }
    }
    memset(&t[i], 0, sizeof(PollSeen));
    t[i].used = 1;
    memcpy(t[i].pub, pub, 32);
  }
  strncpy(t[i].name, name, POLL_NAME_MAX);
  t[i].name[POLL_NAME_MAX] = 0;
  t[i].hops = hops;
  t[i].out_len = out_len;
  if (out_len != POLL_OUT_UNKNOWN && out_len != 0) {
    size_t bytes = (size_t)(out_len & 63) * ((out_len >> 6) + 1);
    if (bytes > POLL_PATH_MAX) {
      t[i].out_len = POLL_OUT_UNKNOWN;
    } else {
      memcpy(t[i].out_path, out_path, bytes);
    }
  }
  t[i].heard = stamp;
  return i;
}

// ---------------------------------------------------------------------------------------------------------------
// neighbour lists

// merge one neighbour into a polled repeater's list: a known key is updated (the newer 'heard' wins), a new key is added;
// when the list is full the neighbour heard longest ago is replaced
static inline void pollMergeNb(PollRep* r, const uint8_t* key, uint32_t heard, int8_t snr) {
  for (int i = 0; i < r->nn; i++) {
    if (memcmp(r->nb[i].key, key, POLL_KEY_LEN) == 0) {
      r->nb[i].fresh = 1;
      if (heard >= r->nb[i].heard) {
        r->nb[i].heard = heard;
        r->nb[i].snr = snr;
      }
      return;
    }
  }
  int use;
  if (r->nn < POLL_MAX_NEIGH) {
    use = r->nn++;
  } else {
    use = 0;
    for (int i = 1; i < r->nn; i++) {
      if (r->nb[i].heard < r->nb[use].heard) use = i;
    }
  }
  memcpy(r->nb[use].key, key, POLL_KEY_LEN);
  r->nb[use].heard = heard;
  r->nb[use].snr = snr;
  r->nb[use].fresh = 1;
}

// A poll starts: nothing has been seen in it yet.
static inline void pollBeginPoll(PollRep* r) {
  for (int i = 0; i < r->nn; i++) r->nb[i].fresh = 0;
}

// A poll ends. When it fetched the repeater's whole list (as many entries as the total it reported), the neighbours it did
// not list any more are dropped, so the stored list is exactly the repeater's. A poll that missed pages keeps the older entries.
// Returns true when the list is complete.
static inline bool pollEndPoll(PollRep* r) {
  int fresh = 0;
  for (int i = 0; i < r->nn; i++) {
    if (r->nb[i].fresh) fresh++;
  }
  if (fresh < r->total) return false;
  int w = 0;
  for (int i = 0; i < r->nn; i++) {
    if (r->nb[i].fresh) {
      if (w != i) r->nb[w] = r->nb[i];
      w++;
    }
  }
  r->nn = (uint8_t)w;
  return true;
}

// 'poll stale': no good poll for POLL_STALE_S (counted from when it was selected if it never had one). Needs a real clock.
static inline bool pollIsStale(const PollRep* r, uint32_t now, uint32_t valid_after) {
  if (now < valid_after) return false;
  uint32_t ref = r->last_ok ? r->last_ok : r->added;
  if (ref < valid_after || ref > now) return false;
  return now - ref >= POLL_STALE_S;
}

// The reply to a neighbours request, after the repeater's own checks: [tag 4][total 2][count 2] then count entries of
// [key POLL_KEY_LEN][heard seconds ago 4][snr 1]. The reply may be padded with zero bytes. Merges the entries into r.
// Returns the number of entries in this reply, or -1 if the reply is not valid.
static inline int pollParseNeighbours(PollRep* r, const uint8_t* data, size_t len, uint32_t now, uint16_t* total_out) {
  if (len < 8) return -1;
  uint16_t total, count;
  memcpy(&total, &data[4], 2);
  memcpy(&count, &data[6], 2);
  const size_t esz = POLL_KEY_LEN + 4 + 1;
  if (count > 64 || 8 + (size_t)count * esz > len) return -1;
  for (uint16_t i = 0; i < count; i++) {
    const uint8_t* e = &data[8 + (size_t)i * esz];
    uint32_t ago;
    memcpy(&ago, &e[POLL_KEY_LEN], 4);
    uint32_t heard = now >= ago ? now - ago : 0;
    pollMergeNb(r, e, heard, (int8_t)e[POLL_KEY_LEN + 4]);
  }
  *total_out = total;
  return (int)count;
}

// ---------------------------------------------------------------------------------------------------------------
// text

static inline void pollFmtSnr(char* out, size_t n, int8_t snr4) {   // "+8.25" (dB x 4 -> text, same as 'snr' replies)
  int a = snr4 < 0 ? -(int)snr4 : (int)snr4;
  snprintf(out, n, "%c%d.%02d", snr4 < 0 ? '-' : '+', a / 4, (a % 4) * 25);
}

static inline void pollFmtAge(char* out, size_t n, uint32_t s) {   // "45s", "5m", "2h", "3d"
  if (s < 60) snprintf(out, n, "%us", (unsigned)s);
  else if (s < 3600) snprintf(out, n, "%um", (unsigned)(s / 60));
  else if (s < 86400) snprintf(out, n, "%uh", (unsigned)(s / 3600));
  else snprintf(out, n, "%ud", (unsigned)(s / 86400));
}

// empty when the age cannot be trusted (the clock is not set, or the entry is from before it was set)
static inline void pollAgeStr(char* out, size_t n, uint32_t now, uint32_t heard, uint32_t valid_after) {
  out[0] = 0;
  if (now < valid_after || heard < valid_after || heard > now) return;
  pollFmtAge(out, n, now - heard);
}

#define POLL_ANS_FOUND    1
#define POLL_ANS_NOTHEARD 0   // the repeater's whole list is known and the id is not in it
#define POLL_ANS_NOTINLIST 2  // the stored list may be incomplete (fewer neighbours stored than the repeater has)

// The answer to 'snr <id>' from one polled repeater, without the repeater's name in front. It looks like the answer a
// repeater gives for itself: "2F1E SNR +8.25dB | Last heard 5m ago" or "2F1E not heard". A short id can match several
// neighbours: the strongest is shown, "(+N more)". Returns POLL_ANS_*.
static inline int pollSnrText(const PollRep* r, const uint8_t* key, int klen, uint32_t now, uint32_t valid_after,
                              char* out, size_t outn) {
  char id[8];
  for (int j = 0; j < klen && j < 3; j++) sprintf(&id[j * 2], "%02X", key[j]);
  int found = 0, best = -1;
  for (int i = 0; i < r->nn; i++) {
    if (memcmp(r->nb[i].key, key, klen) != 0) continue;
    found++;
    if (best < 0 || r->nb[i].snr > r->nb[best].snr
        || (r->nb[i].snr == r->nb[best].snr && r->nb[i].heard > r->nb[best].heard)) best = i;
  }
  if (found) {
    char snr[10], age[12], more[24];
    pollFmtSnr(snr, sizeof(snr), r->nb[best].snr);
    pollAgeStr(age, sizeof(age), now, r->nb[best].heard, valid_after);
    more[0] = 0;
    if (found > 1) snprintf(more, sizeof(more), " (+%d more)", found - 1);
    snprintf(out, outn, "%s SNR %sdB%s%s%s%s", id, snr, age[0] ? " | Last heard " : "", age, age[0] ? " ago" : "", more);
    return POLL_ANS_FOUND;
  }
  if (r->nn >= r->total) {
    snprintf(out, outn, "%s not heard", id);
    return POLL_ANS_NOTHEARD;
  }
  snprintf(out, outn, "%s not in list", id);
  return POLL_ANS_NOTINLIST;
}

// "Neighbours 12, best A3F122 +8.50dB, 7BC011 +6.00dB, 1F22A0 +4.50dB": what 'neighbors summary' gives for a repeater itself.
// The number is the total the polled repeater reported; the three strongest come from what is stored. limit = most characters.
static inline void pollSummaryText(const PollRep* r, char* out, size_t outn, int limit) {
  int top[3] = {-1, -1, -1};
  for (int i = 0; i < r->nn; i++) {
    int cur = i;   // insert into the top 3 (strongest first, newest first on a tie)
    for (int k = 0; k < 3 && cur >= 0; k++) {
      if (top[k] < 0 || r->nb[cur].snr > r->nb[top[k]].snr
          || (r->nb[cur].snr == r->nb[top[k]].snr && r->nb[cur].heard > r->nb[top[k]].heard)) {
        int t = top[k];
        top[k] = cur;
        cur = t;
      }
    }
  }
  int count = r->total > r->nn ? r->total : r->nn;
  snprintf(out, outn, "Neighbours %d", count);
  size_t pos = strlen(out);
  for (int k = 0; k < 3 && top[k] >= 0; k++) {
    char id[8], snr[10], item[40];
    for (int j = 0; j < 3; j++) sprintf(&id[j * 2], "%02X", r->nb[top[k]].key[j]);
    pollFmtSnr(snr, sizeof(snr), r->nb[top[k]].snr);
    int il = snprintf(item, sizeof(item), "%s%s %sdB", k == 0 ? ", best " : ", ", id, snr);
    if ((int)pos + il > limit || pos + (size_t)il >= outn) break;
    memcpy(&out[pos], item, (size_t)il + 1);
    pos += (size_t)il;
  }
}
