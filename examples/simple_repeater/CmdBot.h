#pragma once
/*
 * Command channel bot ("cmdbot"): the trailbot reads what the other repeaters post on the private command channel,
 * remembers the latest values per repeater, and answers "!" questions on that same channel.
 *
 * Pure functions: no mesh, radio or Arduino calls, so it can be tested on a PC. MyMesh.cpp feeds it every command channel
 * message (cbHear) and posts the answers it returns (cbAnswer). All state is in RAM: a reboot clears it, and it fills again
 * as the repeaters post (daily reports, status replies, wake messages ...).
 *
 * What it remembers per repeater (each value has its own "recorded X ago"):
 *   battery (volts and %), board temperature, TX power, neighbour count, asleep / awake and the expected wake time.
 *
 * What it understands (the repeaters run different versions, so every message is matched by its shape, anything it does not
 * recognise is ignored, nothing is ever required):
 *   Bat 4.02V 85%, 24.8C, ...                         status: battery and temperature
 *   Awake for 3 mins, battery 4.02V 85%                wake of a sleep cycle: battery, and the cycle is anchored here
 *   Battery low 3.49V, sleep cycle started             battery, asleep now, wakes in 57 minutes
 *   Battery critical 3.30V / Rebooted... / Sleep cycle ended... / Awake from sleep, battery 3.80V
 *   V/h old>new: 4.05 4.08 ... 4.06                    battlog: newest value = battery
 *   C/h old>new: 23.3 ... 20.0                         templog: highest and lowest of the 24 values (24h high / low)
 *   Battery 85%, ... Chip temp hi 47C lo 15C ...       daily report: battery % (voltage worked back, shown with ~) and the 24h high / low
 *   {"battery_mv":4020,...                             stats-core: battery
 *   OK - sleeping for 120 mins / OK - sleeping until 21:00 (354 mins) / sleep cycle started / sleep cycle stopped
 *   Neighbours 12, ...                                 neighbour count (exact)
 *   New neighbour discovered: 2F1E77 SNR +3.75dB       neighbour count + 1
 *   > 22                                               TX power, only as the answer to a <get tx all> or <get tx Name> seen on the channel
 *
 * Sleep cycle: every repeater runs 57 minutes asleep, 3 minutes awake (CB_CYCLE_*). One wake message is enough to know the
 * phase for the next 24 hours. Any message heard from a repeater that should be asleep proves it is awake: the guess is dropped.
 *
 * Questions (after "!"): lowest / highest battery, below 3.6v / below 30%, hottest / coldest, asleep / wake, tx power
 * (highest / lowest), neighbours, list, and "forget <name>" (drops one repeater from the ledger). Words can be anywhere in the
 * sentence, and a keyword of 5 or more letters may have one typo ("batery", "lowset", "neigbours").
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef CB_MAX
  #define CB_MAX 20            // repeaters remembered (when full, the one heard longest ago is replaced)
#endif
#define CB_NAME_MAX   31
#define CB_BUF        152      // size of one answer message buffer
#define CB_MAX_MSGS   3        // most messages one answer can have

#define CB_CYCLE_AWAKE_S  180UL     // sleep cycle: radio on 3 minutes
#define CB_CYCLE_SLEEP_S  3420UL    // sleep cycle: radio off 57 minutes
#define CB_CYCLE_S        3600UL
#define CB_EXTRAP_MAX_S   86400UL   // a wake message is trusted for 24 hours, after that the state is "unknown"
#define CB_MARGIN_S       60UL      // a message this long after going to sleep (and not near the wake) proves it is awake
#define CB_LAG_S          10UL      // a wake message comes about 10 s after waking, a sleep starts about 10 s after its message
#define CB_TX_ALL_S       95UL      // replies to <get tx all> are accepted for this long
#define CB_TX_ONE_S       25UL      // ... and to <get tx Name>
#define CB_GET_QUIET_S    70UL      // a different <get ...> this recently before makes a 'get tx' answer ambiguous
#define CB_NO_TEMP        (-32768)

#define CBF_BATT   0x01
#define CBF_APPROX 0x02   // the voltage was worked back from a percentage
#define CBF_TEMP   0x04
#define CBF_TX     0x08
#define CBF_NEIGH  0x10
#define CBF_EXACT  0x20   // the neighbour count came from a 'Neighbours N' reply (or from this repeater itself)
#define CBF_SELF   0x40   // this repeater's own row (read live)
#define CBF_TLH    0x80   // the 24 hour temperature high and low (thi, tlo) are known

#define CBS_NONE  0
#define CBS_CYCLE 1       // sleep cycle: 'anchor' is the time a wake starts
#define CBS_ONCE  2       // one sleep: 'anchor' is the time it wakes

struct CbRep {
  char name[CB_NAME_MAX + 1];
  uint32_t heard;                              // anything heard from it (for replacing the oldest)
  uint32_t t_batt, t_temp, t_tx, t_neigh;      // when each value was recorded (seconds)
  uint32_t t_thl;                              // ... and when the temperature high and low were recorded
  uint32_t anchor, sfrom;                      // sleep anchor, and when a single sleep started
  uint16_t mv;
  int16_t temp10;
  int8_t tx;
  int8_t thi, tlo;                             // temperature high and low over 24 hours, whole degrees
  uint8_t neigh;
  uint8_t flags;
  uint8_t sk;
};

static CbRep g_cb[CB_MAX];
static struct {
  bool open, all, poisoned;
  uint32_t until;
  char name[CB_NAME_MAX + 1];
} g_cb_tx;                                     // the <get tx ...> whose answers are being collected
static bool g_cb_other_set = false;
static uint32_t g_cb_other_get = 0;            // when the last other <get ...> was seen

// ---------------------------------------------------------------------------------------------------------------
// small helpers

static char cbLc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool cbDigit(char c) { return c >= '0' && c <= '9'; }

static bool cbNameEq(const char* a, const char* b) {
  while (*a && *b) {
    if (cbLc(*a) != cbLc(*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

static bool cbStarts(const char* s, const char* pre) { return strncmp(s, pre, strlen(pre)) == 0; }

static int cbPct(int mv) {   // the firmware's own straight line: 3.00 V = 0 %, 4.20 V = 100 %
  if (mv <= 3000) return 0;
  if (mv >= 4200) return 100;
  return (int)(((uint32_t)(mv - 3000) * 100) / 1200);
}

// "4.02" -> 4020, "23" -> 23000, "20.0" -> 20000 (value x 1000, up to 3 decimals). Returns the end of the number, NULL if none.
static const char* cbFixed(const char* p, int* milli) {
  if (!cbDigit(*p)) return NULL;
  long ip = 0, fp = 0;
  int digs = 0;
  while (cbDigit(*p)) {
    if (ip < 100000) ip = ip * 10 + (*p - '0');
    p++;
  }
  if (*p == '.' && cbDigit(p[1])) {
    p++;
    while (cbDigit(*p)) {
      if (digs < 3) {
        fp = fp * 10 + (*p - '0');
        digs++;
      }
      p++;
    }
  }
  while (digs < 3) {
    fp *= 10;
    digs++;
  }
  *milli = (int)(ip * 1000 + fp);
  return p;
}

// signed temperature in tenths of a degree: "-3.2" -> -32, "24.8" -> 248
static const char* cbTemp10(const char* p, int* t10) {
  bool neg = false;
  if (*p == '-') {
    neg = true;
    p++;
  }
  int m;
  const char* e = cbFixed(p, &m);
  if (!e) return NULL;
  int v = (m + 50) / 100;
  *t10 = neg ? -v : v;
  return e;
}

static CbRep* cbFind(const char* name) {
  for (int i = 0; i < CB_MAX; i++) {
    if (g_cb[i].name[0] && cbNameEq(g_cb[i].name, name)) return &g_cb[i];
  }
  return NULL;
}

static CbRep* cbGet(const char* name, uint32_t now) {
  CbRep* r = cbFind(name);
  if (r) {
    r->heard = now;
    return r;
  }
  int use = -1;
  for (int i = 0; i < CB_MAX; i++) {
    if (!g_cb[i].name[0]) {
      use = i;
      break;
    }
  }
  if (use < 0) {   // full: replace the repeater heard longest ago (never this repeater's own row)
    for (int i = 0; i < CB_MAX; i++) {
      if (g_cb[i].flags & CBF_SELF) continue;
      if (use < 0 || (int32_t)(g_cb[i].heard - g_cb[use].heard) < 0) use = i;
    }
    if (use < 0) use = 0;
  }
  memset(&g_cb[use], 0, sizeof(CbRep));
  strncpy(g_cb[use].name, name, CB_NAME_MAX);
  g_cb[use].heard = now;
  return &g_cb[use];
}

static void cbBatt(CbRep* r, int mv, bool approx, uint32_t now) {
  if (mv < 2000 || mv > 5000) return;
  r->mv = (uint16_t)mv;
  r->t_batt = now;
  r->flags |= CBF_BATT;
  if (approx) r->flags |= CBF_APPROX;
  else r->flags &= (uint8_t)~CBF_APPROX;
}

static void cbTempSet(CbRep* r, int t10, uint32_t now) {
  if (t10 < -400 || t10 > 1500) return;
  r->temp10 = (int16_t)t10;
  r->t_temp = now;
  r->flags |= CBF_TEMP;
}

static int cbRound10(int t10) {   // tenths of a degree -> nearest whole degree (the firmware report rounds the same way)
  return t10 >= 0 ? (t10 + 5) / 10 : -((-t10 + 5) / 10);
}

// "47" or "-3" -> whole number; returns the end, NULL if none
static const char* cbWhole(const char* p, int* v) {
  bool neg = false;
  if (*p == '-') {
    neg = true;
    p++;
  }
  if (!cbDigit(*p)) return NULL;
  int x = 0;
  while (cbDigit(*p)) {
    if (x < 1000) x = x * 10 + (*p - '0');
    p++;
  }
  *v = neg ? -x : x;
  return p;
}

static void cbSetTHL(CbRep* r, int hi, int lo, uint32_t now) {
  if (hi < -40 || hi > 120 || lo < -40 || lo > 120 || lo > hi) return;
  r->thi = (int8_t)hi;
  r->tlo = (int8_t)lo;
  r->t_thl = now;
  r->flags |= CBF_TLH;
}

// "<key>4.02V" somewhere in the text -> battery
static void cbBattAfter(CbRep* r, const char* body, const char* key, uint32_t now) {
  const char* p = strstr(body, key);
  if (!p) return;
  p += strlen(key);
  int m;
  const char* e = cbFixed(p, &m);
  if (e && (*e == 'V' || *e == 'v')) cbBatt(r, m, false, now);
}

// ---------------------------------------------------------------------------------------------------------------
// sleep state

// 0 = not known to be on a sleep, 1 = awake (inside a cycle's awake window), 2 = asleep.
// wake_in: seconds until it wakes (asleep). since: seconds it has been asleep (so far as known).
static int cbSleepState(const CbRep* r, uint32_t now, uint32_t* wake_in, uint32_t* since) {
  *wake_in = 0;
  *since = 0;
  if (r->sk == CBS_CYCLE) {
    int32_t d = (int32_t)(now - r->anchor);
    if (d < 0) {   // the anchor is the coming wake: asleep until then
      *wake_in = (uint32_t)(-d);
      long s = (long)CB_CYCLE_SLEEP_S + d;
      *since = s > 0 ? (uint32_t)s : 0;
      return 2;
    }
    if ((uint32_t)d > CB_EXTRAP_MAX_S) return 0;
    uint32_t ph = (uint32_t)d % CB_CYCLE_S;
    if (ph < CB_CYCLE_AWAKE_S) return 1;
    *wake_in = CB_CYCLE_S - ph;
    *since = ph - CB_CYCLE_AWAKE_S;
    return 2;
  }
  if (r->sk == CBS_ONCE) {
    int32_t d = (int32_t)(now - r->anchor);
    if (d >= 0) return 0;   // it has woken: nothing more is known
    *wake_in = (uint32_t)(-d);
    *since = now - r->sfrom;
    return 2;
  }
  return 0;
}

// a message from a repeater that is guessed to be asleep (and not about to wake): it is awake, so the guess was wrong
static void cbContradict(CbRep* r, uint32_t now) {
  uint32_t wi, since;
  if (cbSleepState(r, now, &wi, &since) != 2) return;
  if (wi < CB_MARGIN_S) return;        // about to wake anyway (clock drift)
  if (since > CB_MARGIN_S) r->sk = CBS_NONE;
}

// ---------------------------------------------------------------------------------------------------------------
// reading the channel

// <get tx all>, <get tx Tuna>, <get ...>: the channel command (without the angle brackets, as the relay writes it too)
static void cbCommand(const char* inner, uint32_t now) {
  char s[64];
  int n = 0;
  while (inner[n] && n < (int)sizeof(s) - 1) {
    s[n] = cbLc(inner[n]);
    n++;
  }
  s[n] = 0;
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '>')) s[--n] = 0;
  const char* p = s;
  while (*p == ' ' || *p == '<') p++;
  if (strncmp(p, "get ", 4) != 0) return;
  p += 4;
  while (*p == ' ') p++;
  if (p[0] == 't' && p[1] == 'x' && (p[2] == ' ' || p[2] == 0)) {
    p += 2;
    while (*p == ' ') p++;
    if (!*p) return;
    g_cb_tx.open = true;
    g_cb_tx.all = strcmp(p, "all") == 0;
    g_cb_tx.until = now + (g_cb_tx.all ? CB_TX_ALL_S : CB_TX_ONE_S);
    g_cb_tx.poisoned = g_cb_other_set && (uint32_t)(now - g_cb_other_get) < CB_GET_QUIET_S;
    // the name keeps its original capital letters in the ledger, but the comparison ignores case
    strncpy(g_cb_tx.name, p, CB_NAME_MAX);
    g_cb_tx.name[CB_NAME_MAX] = 0;
  } else {   // a different 'get': its answers ("> value") look the same, so any open 'get tx' window can no longer be trusted
    g_cb_other_set = true;
    g_cb_other_get = now;
    if (g_cb_tx.open && (int32_t)(g_cb_tx.until - now) > 0) g_cb_tx.poisoned = true;
  }
}

// one message of a repeater (body without the "name: "). 'sod' = seconds since local midnight, -1 if the clock is not set.
static void cbParse(const char* name, const char* body, uint32_t now, int32_t sod) {
  CbRep* r = NULL;
  bool anchored = false;

  if (cbStarts(body, "Bat ") && cbDigit(body[4])) {                       // status
    int m;
    const char* e = cbFixed(body + 4, &m);
    if (e && *e == 'V') {
      r = cbGet(name, now);
      cbBatt(r, m, false, now);
      const char* p = e;
      while ((p = strchr(p, ',')) != NULL) {   // the field that is a temperature: "24.8C"
        p++;
        while (*p == ' ') p++;
        int t10;
        const char* te = cbTemp10(p, &t10);
        if (te && *te == 'C' && (te[1] == ',' || te[1] == 0 || te[1] == ' ')) {
          cbTempSet(r, t10, now);
          break;
        }
      }
    }
  } else if (cbStarts(body, "Awake for ")) {                              // wake of a sleep cycle
    r = cbGet(name, now);
    cbBattAfter(r, body, "battery ", now);
    r->sk = CBS_CYCLE;
    r->anchor = now >= CB_LAG_S ? now - CB_LAG_S : 0;
    anchored = true;
  } else if (cbStarts(body, "Battery low ")) {                            // low battery: asleep now when a cycle starts
    r = cbGet(name, now);
    cbBattAfter(r, body, "Battery low ", now);
    if (strstr(body, "sleep cycle started")) {
      r->sk = CBS_CYCLE;
      r->anchor = now + CB_CYCLE_SLEEP_S + CB_LAG_S;
      anchored = true;
    }
  } else if (cbStarts(body, "Battery critical ")) {
    r = cbGet(name, now);
    cbBattAfter(r, body, "Battery critical ", now);
  } else if (cbStarts(body, "Sleep cycle ended")) {
    r = cbGet(name, now);
    cbBattAfter(r, body, "battery ", now);
    r->sk = CBS_NONE;
  } else if (cbStarts(body, "Rebooted")) {                                // its tables are empty again
    r = cbGet(name, now);
    cbBattAfter(r, body, "battery ", now);
    r->sk = CBS_NONE;
    r->flags &= (uint8_t) ~(CBF_NEIGH | CBF_EXACT);
    r->neigh = 0;
  } else if (cbStarts(body, "Awake from sleep")) {
    r = cbGet(name, now);
    cbBattAfter(r, body, "battery ", now);
    r->sk = CBS_NONE;
  } else if (cbStarts(body, "V/h old>new:")) {                            // battlog: the newest value is last
    const char* p = body + 12;
    int last = -1, m;
    for (;;) {
      while (*p == ' ') p++;
      const char* e = cbFixed(p, &m);
      if (!e) break;
      last = m;
      p = e;
    }
    if (last > 0) {
      r = cbGet(name, now);
      cbBatt(r, last, false, now);
    }
  } else if (cbStarts(body, "C/h old>new:")) {                            // templog: 24 hourly values, "?" = no reading
    const char* p = body + 12;
    int last = 0, t, tmin = 0, tmax = 0, n = 0;
    for (;;) {
      while (*p == ' ') p++;
      if (*p == '?') {
        p++;
        continue;
      }
      const char* e = cbTemp10(p, &t);
      if (!e) break;
      last = t;
      if (n == 0 || t < tmin) tmin = t;
      if (n == 0 || t > tmax) tmax = t;
      n++;
      p = e;
    }
    if (n > 0) {
      r = cbGet(name, now);
      cbTempSet(r, last, now);                                            // the newest value is also the current temperature
      cbSetTHL(r, cbRound10(tmax), cbRound10(tmin), now);
    }
  } else if (cbStarts(body, "Battery ") && cbDigit(body[8])) {            // daily report: "Battery 85%, ..."
    int pct = atoi(body + 8);
    if (pct >= 0 && pct <= 100) {
      r = cbGet(name, now);
      cbBatt(r, 3000 + pct * 12, true, now);
      const char* ct = strstr(body, "Chip temp hi ");                     // "... Chip temp hi 47C lo 15C. ..."
      int hi, lo;
      const char* e = ct ? cbWhole(ct + 13, &hi) : NULL;
      if (e && *e == 'C') {
        const char* l = strstr(e, " lo ");
        if (l && cbWhole(l + 4, &lo)) cbSetTHL(r, hi, lo, now);
      }
    }
  } else if (body[0] == '{' && strstr(body, "\"battery_mv\":")) {         // stats-core
    r = cbGet(name, now);
    cbBatt(r, atoi(strstr(body, "\"battery_mv\":") + 13), false, now);
  } else if (cbStarts(body, "OK - sleeping for ")) {                      // a sleep command it accepted
    int mins = atoi(body + 18);
    if (mins >= 1 && mins <= 1440) {
      r = cbGet(name, now);
      r->sk = CBS_ONCE;
      r->sfrom = now;
      r->anchor = now + (uint32_t)mins * 60UL + CB_LAG_S;
      anchored = true;
    }
  } else if (cbStarts(body, "OK - sleeping until ")) {                    // "... until 21:00 (354 mins)"
    const char* p = body + 20;
    const char* colon = strchr(p, ':');
    if (cbDigit(p[0]) && colon && cbDigit(colon[1])) {
      int hh = atoi(p), mm = atoi(colon + 1);
      uint32_t diff = 0;
      if (sod >= 0 && hh <= 23 && mm <= 59) {
        diff = ((uint32_t)hh * 3600UL + (uint32_t)mm * 60UL + 86400UL - (uint32_t)sod) % 86400UL;
        if (diff == 0) diff = 86400UL;
      } else {
        const char* par = strchr(p, '(');
        if (par && cbDigit(par[1])) diff = (uint32_t)atoi(par + 1) * 60UL;
      }
      if (diff > 0) {
        r = cbGet(name, now);
        r->sk = CBS_ONCE;
        r->sfrom = now;
        r->anchor = now + diff;
        anchored = true;
      }
    }
  } else if (strcmp(body, "sleep cycle started") == 0) {
    r = cbGet(name, now);
    r->sk = CBS_CYCLE;
    r->anchor = now + CB_CYCLE_SLEEP_S + CB_LAG_S;
    anchored = true;
  } else if (strcmp(body, "sleep cycle stopped") == 0) {
    r = cbGet(name, now);
    r->sk = CBS_NONE;
  } else if (cbStarts(body, "Neighbours ") && cbDigit(body[11])) {        // exact count
    int n = atoi(body + 11);
    if (n >= 0 && n <= 255) {
      r = cbGet(name, now);
      r->neigh = (uint8_t)n;
      r->flags |= (CBF_NEIGH | CBF_EXACT);
      r->t_neigh = now;
    }
  } else if (cbStarts(body, "New neighbour discovered:")) {               // one more than before
    r = cbGet(name, now);
    if (r->flags & CBF_NEIGH) {
      if (r->neigh < 50) r->neigh++;
    } else {
      r->neigh = 1;
      r->flags |= CBF_NEIGH;
      r->flags &= (uint8_t)~CBF_EXACT;
    }
    r->t_neigh = now;
  } else if (body[0] == '>' && body[1] == ' ') {                          // answer to <get tx ...>
    bool neg = body[2] == '-';
    const char* p = body + (neg ? 3 : 2);
    if (cbDigit(*p) && g_cb_tx.open && !g_cb_tx.poisoned && (int32_t)(g_cb_tx.until - now) > 0
        && (g_cb_tx.all || cbNameEq(g_cb_tx.name, name))) {
      int v = atoi(p);
      if (v >= 0 && v <= 30) {
        r = cbGet(name, now);
        r->tx = (int8_t)(neg ? -v : v);
        r->t_tx = now;
        r->flags |= CBF_TX;
      }
    }
  }

  if (r == NULL) r = cbFind(name);
  if (r) {
    r->heard = now;
    if (!anchored) cbContradict(r, now);   // any other message from it: it is awake
  }
}

// What the trailbot reads from the board itself. Called before every answer so its own row is always current.
static void cbSelf(const char* name, unsigned mv, int temp10, int thi, int tlo, int tx, int neigh, uint32_t now) {
  CbRep* r = cbGet(name, now);
  r->flags |= CBF_SELF;
  r->sk = CBS_NONE;
  if (mv) {
    r->mv = (uint16_t)mv;
    r->flags |= CBF_BATT;
    r->flags &= (uint8_t)~CBF_APPROX;
    r->t_batt = now;
  }
  if (temp10 != CB_NO_TEMP) {
    r->temp10 = (int16_t)temp10;
    r->flags |= CBF_TEMP;
    r->t_temp = now;
  }
  if (thi != CB_NO_TEMP && tlo != CB_NO_TEMP) {   // its own hourly log, 24 hours (or since boot)
    r->thi = (int8_t)thi;
    r->tlo = (int8_t)tlo;
    r->t_thl = now;
    r->flags |= CBF_TLH;
  } else {
    r->flags &= (uint8_t)~CBF_TLH;
  }
  r->tx = (int8_t)tx;
  r->flags |= CBF_TX;
  r->t_tx = now;
  if (neigh >= 0) {
    r->neigh = (uint8_t)(neigh > 255 ? 255 : neigh);
    r->flags |= (CBF_NEIGH | CBF_EXACT);
    r->t_neigh = now;
  }
}

// The whole channel message ("<sender>: <text>"). Always updates the ledger. Returns the text after "!" when the message is
// a question to answer, otherwise NULL. Messages from this repeater itself (self_name) are ignored.
static const char* cbHear(const char* text, uint32_t now, const char* self_name, int32_t sod) {
  const char* body = strstr(text, ": ");
  if (!body) return NULL;
  int nl = (int)(body - text);
  if (nl < 1 || nl > CB_NAME_MAX) return NULL;
  char name[CB_NAME_MAX + 1];
  memcpy(name, text, nl);
  name[nl] = 0;
  body += 2;
  while (*body == ' ') body++;
  if (!*body) return NULL;
  if (cbNameEq(name, self_name)) return NULL;
  if (*body == '!') return body + 1;
  if (*body == '<') {   // somebody's command: only 'get' matters (to read TX power answers)
    cbCommand(body, now);
    return NULL;
  }
  cbParse(name, body, now, sod);
  return NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// answers

struct CbOut {
  char (*m)[CB_BUF];
  int n, max, budget, len, items, dropped;
  bool full;
  const char* sep;
};

static void cbOutInit(CbOut* o, char (*m)[CB_BUF], int max, int budget, const char* head, const char* sep) {
  o->m = m;
  o->max = max;
  o->budget = budget;
  o->sep = sep;
  o->n = 1;
  o->items = 0;
  o->dropped = 0;
  o->full = false;
  o->len = snprintf(m[0], CB_BUF, "%s", head);
  if (o->len >= CB_BUF) o->len = CB_BUF - 1;
}

static void cbOutItem(CbOut* o, const char* it) {
  if (o->full) {
    o->dropped++;
    return;
  }
  int il = (int)strlen(it);
  int sl = o->items ? (int)strlen(o->sep) : 0;
  int lim = o->budget - (o->n == o->max ? 10 : 0);   // the last message keeps room for " +N more"
  if (o->len + sl + il > lim && o->items > 0) {
    if (o->n >= o->max) {
      o->full = true;
      o->dropped++;
      return;
    }
    o->n++;
    o->len = 0;
    o->items = 0;
    sl = 0;
    o->m[o->n - 1][0] = 0;
    lim = o->budget - (o->n == o->max ? 10 : 0);
  }
  if (o->len + sl + il > lim) il = lim - o->len - sl;   // one item longer than a message: cut
  if (il < 0) il = 0;
  char* d = o->m[o->n - 1] + o->len;
  if (sl) {
    memcpy(d, o->sep, sl);
    d += sl;
    o->len += sl;
  }
  memcpy(d, it, il);
  d[il] = 0;
  o->len += il;
  o->items++;
}

static int cbOutEnd(CbOut* o) {
  if (o->dropped > 0) snprintf(o->m[o->n - 1] + o->len, CB_BUF - o->len, " +%d more", o->dropped);
  return o->n;
}

static void cbAge(uint32_t now, uint32_t t, bool self, char* o, int n) {
  if (self) {
    snprintf(o, n, "now");
    return;
  }
  uint32_t s = now - t;
  if (s < 60) snprintf(o, n, "<1m ago");
  else if (s < 7200) snprintf(o, n, "%um ago", (unsigned)(s / 60));
  else if (s < 172800UL) snprintf(o, n, "%uh ago", (unsigned)(s / 3600));
  else snprintf(o, n, "%ud ago", (unsigned)(s / 86400UL));
}

static void cbDur(uint32_t s, char* o, int n) {
  if (s < 60) snprintf(o, n, "<1m");
  else if (s < 7200) snprintf(o, n, "%um", (unsigned)(s / 60));
  else snprintf(o, n, "%uh%02um", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
}

// ---- sorting: idx[] holds indexes into g_cb, sorted by a key

#define CBK_MV    1
#define CBK_TEMP  2
#define CBK_TX    3
#define CBK_NEIGH 4
#define CBK_WAKE  5
#define CBK_THI   6
#define CBK_TLO   7

static long cbKey(const CbRep* r, int kind, uint32_t now) {
  switch (kind) {
    case CBK_MV:    return r->mv;
    case CBK_TEMP:  return r->temp10;
    case CBK_TX:    return r->tx;
    case CBK_NEIGH: return r->neigh;
    case CBK_THI:   return r->thi;
    case CBK_TLO:   return r->tlo;
    default: {
      uint32_t wi, since;
      cbSleepState(r, now, &wi, &since);
      return (long)wi;
    }
  }
}

static void cbSort(int* idx, int n, int kind, bool ascending, uint32_t now) {
  for (int i = 1; i < n; i++) {
    int v = idx[i];
    long kv = cbKey(&g_cb[v], kind, now);
    int j = i - 1;
    while (j >= 0) {
      long kj = cbKey(&g_cb[idx[j]], kind, now);
      bool move = ascending ? (kj > kv) : (kj < kv);
      if (!move) break;
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = v;
  }
}

// ---- reading the question

static void cbNorm(const char* in, char* out, int osz) {   // " lowest battery ", keeps '.' and '%' for numbers
  int n = 0;
  bool space = true;
  out[n++] = ' ';
  for (const unsigned char* p = (const unsigned char*)in; *p && n < osz - 3; p++) {
    char c = cbLc((char)*p);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '%') {
      out[n++] = c;
      space = false;
    } else if (!space) {
      out[n++] = ' ';
      space = true;
    }
  }
  if (!space) out[n++] = ' ';
  out[n] = 0;
}

// A word the person typed matches a keyword when it is the same, or (for keywords of 5 or more letters) one typo away:
// one letter wrong, missing, extra, or two neighbouring letters swapped ("batery", "lowset", "neigbours").
static bool cbNear(const char* a, int al, const char* b) {
  int bl = (int)strlen(b);
  if (al == bl && memcmp(a, b, al) == 0) return true;
  if (bl < 5) return false;
  if (al < bl - 1 || al > bl + 1) return false;
  if (al == bl) {
    int i = 0;
    while (i < al && a[i] == b[i]) i++;
    if (memcmp(a + i + 1, b + i + 1, al - i - 1) == 0) return true;                                        // one letter wrong
    return i + 1 < al && a[i] == b[i + 1] && a[i + 1] == b[i] && memcmp(a + i + 2, b + i + 2, al - i - 2) == 0;   // two swapped
  }
  const char* lo = al > bl ? a : b;   // the longer one has one extra letter
  const char* sh = al > bl ? b : a;
  int sl = al > bl ? bl : al;
  int i = 0;
  while (i < sl && lo[i] == sh[i]) i++;
  return memcmp(lo + i + 1, sh + i, sl - i) == 0;
}

static bool cbHasW(const char* n, const char* w) {   // n is the normalised question (words separated by single spaces)
  const char* p = n;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* t = p;
    while (*p && *p != ' ') p++;
    if (cbNear(t, (int)(p - t), w)) return true;
  }
  return false;
}

static bool cbHasAny(const char* n, const char* list) {   // words separated by ';'
  char w[24];
  const char* p = list;
  while (*p) {
    int l = 0;
    while (p[l] && p[l] != ';' && l < (int)sizeof(w) - 1) {
      w[l] = p[l];
      l++;
    }
    w[l] = 0;
    if (l > 0 && cbHasW(n, w)) return true;
    p += l;
    if (*p == ';') p++;
  }
  return false;
}

// "below 3.6v", "under 30%", "less than 40 percent", "below 3600mv": the limit as volts (millivolts) or percent.
static bool cbThreshold(const char* n, int* val, bool* volts) {
  const char* p = n;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char* t = p;
    while (*p && *p != ' ') p++;
    int tl = (int)(p - t);
    const char* np = p;
    while (*np == ' ') np++;
    bool kw = cbNear(t, tl, "below") || cbNear(t, tl, "under") || cbNear(t, tl, "beneath");
    if (!kw && ((tl == 4 && strncmp(t, "less", 4) == 0) || (tl == 5 && strncmp(t, "lower", 5) == 0))
        && strncmp(np, "than", 4) == 0 && (np[4] == ' ' || np[4] == 0)) {
      np += 4;
      while (*np == ' ') np++;
      kw = true;
    }
    if (!kw) continue;
    int m;
    const char* e = cbFixed(np, &m);
    if (!e) continue;
    bool has_dot = false;
    for (const char* q = np; q < e; q++) {
      if (*q == '.') has_dot = true;
    }
    const char* u = e;
    while (*u == ' ') u++;
    bool unit_v = (u[0] == 'v' && !(u[1] >= 'a' && u[1] <= 'z')) || cbStarts(u, "volt");
    bool unit_mv = cbStarts(u, "mv") && !(u[2] >= 'a' && u[2] <= 'z');
    bool unit_pc = u[0] == '%' || cbStarts(u, "percent") || (cbStarts(u, "pc") && !(u[2] >= 'a' && u[2] <= 'z'));
    if (unit_mv) {
      *val = m / 1000;
      *volts = true;
    } else if (unit_v || (!unit_pc && has_dot)) {
      *val = m;
      *volts = true;
    } else {
      *val = m / 1000;
      *volts = false;
    }
    if (*volts) return *val >= 2000 && *val <= 5000;
    return *val >= 0 && *val <= 100;
  }
  return false;
}

// ---- items

static void cbVolts(const CbRep* r, char* o, int n) {
  snprintf(o, n, "%s%u.%02uV", (r->flags & CBF_APPROX) ? "~" : "", (unsigned)(r->mv / 1000), (unsigned)((r->mv % 1000) / 10));
}

static void cbBattItem(const CbRep* r, uint32_t now, char* o, int n) {
  char v[12], age[16];
  cbVolts(r, v, sizeof(v));
  cbAge(now, r->t_batt, (r->flags & CBF_SELF) != 0, age, sizeof(age));
  snprintf(o, n, "%s %s %d%% (%s)", r->name, v, cbPct(r->mv), age);
}

static void cbTempItem(const CbRep* r, uint32_t now, char* o, int n) {
  bool self = (r->flags & CBF_SELF) != 0;
  char age[16];
  if (r->flags & CBF_TLH) {   // the 24 hour high and low
    cbAge(now, r->t_thl, self, age, sizeof(age));
    snprintf(o, n, "%s high %dC low %dC (%s)", r->name, (int)r->thi, (int)r->tlo, age);
    return;
  }
  cbAge(now, r->t_temp, self, age, sizeof(age));   // only a single reading is known: show it with its age
  int t = r->temp10;
  int a = t < 0 ? -t : t;
  snprintf(o, n, "%s %s%d.%dC (%s)", r->name, t < 0 ? "-" : "", a / 10, a % 10, age);
}

static const char* const CB_HELP =
    "Ask: lowest/highest battery, below 3.6v or 30%, hottest, coldest, asleep, tx power, neighbours, list, forget <name>";

// The answer to a question (the text after "!"). Fills out[] with at most maxmsg messages (each at most 'budget' characters,
// WITHOUT the sender prefix) and returns how many. 'sod' = seconds since local midnight (-1 = clock not set).
static int cbAnswer(const char* q, uint32_t now, int32_t sod, int budget, char out[][CB_BUF], int maxmsg) {
  char n[96];
  cbNorm(q, n, sizeof(n));
  char it[112];
  int idx[CB_MAX];
  int cnt = 0;
  CbOut o;

  {   // "forget Tuna": drop one repeater from the ledger (it comes back if it posts again)
    const char* p = q;
    while (*p) {
      while (*p == ' ') p++;
      const char* t = p;
      while (*p && *p != ' ') p++;
      int tl = (int)(p - t);
      char lw[16];
      if (tl <= 0 || tl >= (int)sizeof(lw)) continue;
      for (int i = 0; i < tl; i++) lw[i] = cbLc(t[i]);
      if (!cbNear(lw, tl, "forget")) continue;
      while (*p == ' ' || *p == ':') p++;
      for (;;) {   // "forget about the repeater Tuna": skip those little words
        static const char* const skip[] = {"about ", "the ", "repeater ", "node "};
        bool cut = false;
        for (unsigned k = 0; k < sizeof(skip) / sizeof(skip[0]); k++) {
          size_t l = strlen(skip[k]);
          bool same = true;
          for (size_t i = 0; i < l && same; i++) same = cbLc(p[i]) == skip[k][i];
          if (same) {
            p += l;
            while (*p == ' ') p++;
            cut = true;
          }
        }
        if (!cut) break;
      }
      char nm[CB_NAME_MAX + 1];
      int nl = 0;
      while (p[nl] && nl < CB_NAME_MAX) {
        nm[nl] = p[nl];
        nl++;
      }
      while (nl > 0 && (nm[nl - 1] == ' ' || nm[nl - 1] == '?' || nm[nl - 1] == '!' || nm[nl - 1] == '.')) nl--;
      nm[nl] = 0;
      if (nl == 0) {
        snprintf(out[0], CB_BUF, "Forget which repeater? Say: forget <name>");
        return 1;
      }
      CbRep* r = cbFind(nm);
      if (!r) snprintf(out[0], CB_BUF, "I don't know a repeater called %s", nm);
      else if (r->flags & CBF_SELF) snprintf(out[0], CB_BUF, "That is me: my own values are read live");
      else {
        snprintf(out[0], CB_BUF, "Forgot %s (it is remembered again when it next posts)", r->name);
        memset(r, 0, sizeof(CbRep));
      }
      return 1;
    }
  }

  if (cbHasAny(n, "help;commands;options;menu") || strstr(n, " what can ") != NULL) {
    snprintf(out[0], CB_BUF, "%s", CB_HELP);
    return 1;
  }

  int thr = 0;
  bool volts = true;
  if (cbThreshold(n, &thr, &volts)) {   // below a voltage or a percentage
    int withdata = 0;
    for (int i = 0; i < CB_MAX; i++) {
      if (!g_cb[i].name[0] || !(g_cb[i].flags & CBF_BATT)) continue;
      withdata++;
      bool below = volts ? ((int)g_cb[i].mv < thr) : (cbPct(g_cb[i].mv) < thr);
      if (below) idx[cnt++] = i;
    }
    char lim[16];
    if (volts) snprintf(lim, sizeof(lim), "%d.%02dV", thr / 1000, (thr % 1000) / 10);
    else snprintf(lim, sizeof(lim), "%d%%", thr);
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "None below %s (%d repeaters have battery data)", lim, withdata);
      return 1;
    }
    char head[40];
    snprintf(head, sizeof(head), "Below %s (%d of %d): ", lim, cnt, withdata);
    cbSort(idx, cnt, CBK_MV, true, now);
    cbOutInit(&o, out, maxmsg, budget, head, "; ");
    for (int i = 0; i < cnt; i++) {
      cbBattItem(&g_cb[idx[i]], now, it, sizeof(it));
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "asleep;sleeping;sleep;sleeps;wake;wakes;waking;awake")) {   // who is asleep, and when they wake
    int total = 0;
    for (int i = 0; i < CB_MAX; i++) {
      if (!g_cb[i].name[0]) continue;
      total++;
      uint32_t wi, since;
      if (cbSleepState(&g_cb[i], now, &wi, &since) == 2) idx[cnt++] = i;
    }
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "No repeaters known to be asleep (%d heard)", total);
      return 1;
    }
    cbSort(idx, cnt, CBK_WAKE, true, now);
    char head[40];
    snprintf(head, sizeof(head), "Asleep (%d of %d): ", cnt, total);
    cbOutInit(&o, out, maxmsg, budget, head, "; ");
    for (int i = 0; i < cnt; i++) {
      uint32_t wi, since;
      cbSleepState(&g_cb[idx[i]], now, &wi, &since);
      char d[16], clk[12];
      cbDur(wi, d, sizeof(d));
      clk[0] = 0;
      if (sod >= 0) {
        uint32_t w = ((uint32_t)sod + wi) % 86400UL;
        snprintf(clk, sizeof(clk), " (%02u:%02u)", (unsigned)(w / 3600), (unsigned)((w % 3600) / 60));
      }
      snprintf(it, sizeof(it), "%s wakes in ~%s%s", g_cb[idx[i]].name, d, clk);
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "tx;transmit;txpower")) {   // TX power
    bool asc = cbHasAny(n, "lowest;least;minimum;min;weakest");
    for (int i = 0; i < CB_MAX; i++) {
      if (g_cb[i].name[0] && (g_cb[i].flags & CBF_TX)) idx[cnt++] = i;
    }
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "No TX power known yet. Send <get tx all> on this channel and I will record the answers");
      return 1;
    }
    cbSort(idx, cnt, CBK_TX, asc, now);
    cbOutInit(&o, out, maxmsg, budget, asc ? "TX power, lowest first: " : "TX power, highest first: ", "; ");
    for (int i = 0; i < cnt; i++) {
      char age[16];
      const CbRep* r = &g_cb[idx[i]];
      cbAge(now, r->t_tx, (r->flags & CBF_SELF) != 0, age, sizeof(age));
      snprintf(it, sizeof(it), "%s %ddBm (%s)", r->name, (int)r->tx, age);
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "neighbours;neighbors;neighbour;neighbor")) {   // neighbours (neighbors)
    for (int i = 0; i < CB_MAX; i++) {
      if (g_cb[i].name[0] && (g_cb[i].flags & CBF_NEIGH)) idx[cnt++] = i;
    }
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "No neighbour counts yet. Send <neighbors all> on this channel and I will record the answers");
      return 1;
    }
    cbSort(idx, cnt, CBK_NEIGH, false, now);
    cbOutInit(&o, out, maxmsg, budget, "Neighbours (+ = at least): ", "; ");
    for (int i = 0; i < cnt; i++) {
      char age[16];
      const CbRep* r = &g_cb[idx[i]];
      cbAge(now, r->t_neigh, (r->flags & CBF_SELF) != 0, age, sizeof(age));
      snprintf(it, sizeof(it), "%s %d%s (%s)", r->name, (int)r->neigh, (r->flags & CBF_EXACT) ? "" : "+", age);
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "hottest;coldest;hot;cold;warm;warmest;cool;coolest;temp;temps;temperature;temperatures;celsius")) {
    bool asc = cbHasAny(n, "coldest;cold;cool;coolest;lowest;colder");
    int idx2[CB_MAX];
    int c2 = 0;
    for (int i = 0; i < CB_MAX; i++) {
      if (!g_cb[i].name[0]) continue;
      if (g_cb[i].flags & CBF_TLH) idx[cnt++] = i;           // ranked by the 24 hour high (hottest) or low (coldest)
      else if (g_cb[i].flags & CBF_TEMP) idx2[c2++] = i;     // only a single reading: listed after them
    }
    if (cnt + c2 == 0) {
      snprintf(out[0], CB_BUF, "No temperatures yet");
      return 1;
    }
    cbSort(idx, cnt, asc ? CBK_TLO : CBK_THI, asc, now);
    cbSort(idx2, c2, CBK_TEMP, asc, now);
    for (int k = 0; k < c2; k++) idx[cnt + k] = idx2[k];
    cnt += c2;
    cbOutInit(&o, out, maxmsg, budget, asc ? "Coldest (24h low): " : "Hottest (24h high): ", "; ");
    for (int i = 0; i < cnt; i++) {
      cbTempItem(&g_cb[idx[i]], now, it, sizeof(it));
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "battery;batteries;charge;voltage;volts;flat;lowest;highest;weakest;strongest;fullest;low;best;worst")) {
    bool high = cbHasAny(n, "highest;best;fullest;strongest;most;max;top");
    for (int i = 0; i < CB_MAX; i++) {
      if (g_cb[i].name[0] && (g_cb[i].flags & CBF_BATT)) idx[cnt++] = i;
    }
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "No battery data yet");
      return 1;
    }
    cbSort(idx, cnt, CBK_MV, !high, now);
    cbOutInit(&o, out, maxmsg, budget, high ? "Highest battery: " : "Lowest battery: ", "; ");
    for (int i = 0; i < cnt; i++) {
      cbBattItem(&g_cb[idx[i]], now, it, sizeof(it));
      cbOutItem(&o, it);
    }
    return cbOutEnd(&o);
  }

  if (cbHasAny(n, "list;repeaters;repeater;heard;names;nodes")) {   // who has been heard
    for (int i = 0; i < CB_MAX; i++) {
      if (g_cb[i].name[0]) idx[cnt++] = i;
    }
    if (cnt == 0) {
      snprintf(out[0], CB_BUF, "No repeaters heard yet");
      return 1;
    }
    char head[32];
    snprintf(head, sizeof(head), "%d repeaters heard: ", cnt);
    cbOutInit(&o, out, maxmsg, budget, head, ", ");
    for (int i = 0; i < cnt; i++) cbOutItem(&o, g_cb[idx[i]].name);
    return cbOutEnd(&o);
  }

  snprintf(out[0], CB_BUF, "Not understood. %s", CB_HELP);
  return 1;
}
