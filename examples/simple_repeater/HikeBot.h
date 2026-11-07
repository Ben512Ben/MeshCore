#pragma once
/*
 * Hiking bot: the one place every '!' message goes. Pure functions: no mesh, radio or Arduino calls, so it can be
 * tested on a PC (tools/hikebot_test.cpp). MyMesh.cpp passes it the text after the '!' and queues the replies.
 *
 * In order:
 *   1. "!test" and "!ping": the signal report, plus a plain verdict, a tip when weak and a comparison with the
 *      person's last test. "!radio check", "!can you hear me" ... give the same reply.
 *   2. exact trail, town, region or "!trails" lookups (TrailBot.h), exactly as before.
 *   3. everything else is natural conversation:
 *        safety rules in chatrules.h always win (emergencies, lost, snakes, storms, low water ...)
 *        then: hike timer, time/date/sunrise/sunset/moon/glow worms (today, or another day: "sunrise tomorrow", "moon on Friday"), "when should I start", compare trails,
 *        what to take, how long/far/hard, walk suggestions, hiking knowledge (hikeknowledge.h),
 *        a trail, town or region named anywhere in the sentence (typos tolerated),
 *        and finally the ELIZA chat (ChatBot.h).
 *
 * It remembers 50 people (oldest slot recycled, no expiry): their last trail, hike timer, pace, last signal test
 * and last walk search. About 32 bytes each. A person with a running hike timer is never recycled (see
 * hikePersonFor). Nothing here changes trails.h.
 *
 * Test-only mode (HikeCtx.test_only, used for the test channels): only '!test', '!ping' and the radio-check
 * phrases are answered; everything else is ignored. A signal test uses no memory slot at all.
 *
 * Hike timer: the hike can be one of my trails, an unlisted hike (HT_UNKNOWN) or an off-trail hike (HT_OFFTRAIL). A start
 * with no trail named reuses the person's last hike and says so; a short reply naming the right trail (or "off trail")
 * corrects it. Off-trail and unknown hikes are only timed: no listed time, no pace learning.
 *
 * Sky: moon phase, moonrise and moonset, and the next meteor shower are worked out from the clock (hbDoMoon, hbDoMeteors).
 *
 * Every message is at most the budget (134 minus the "<node name>: " prefix) and never starts with '!'.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "trails.h"
#include "TrailBot.h"
#include "ChatBot.h"
#include "hikeknowledge.h"
#include "WildBot.h"

#ifndef BOT_MSG_MAX
  #define BOT_MSG_MAX 134
#endif
#ifndef BOT_Q_MAX
  #define BOT_Q_MAX 4
#endif
#ifdef BRISBANE_UTC_OFFSET_SECS
  #define HIKE_TZ_SECS ((long)BRISBANE_UTC_OFFSET_SECS)
#else
  #define HIKE_TZ_SECS 36000L      // Queensland: UTC+10, no daylight saving
#endif
#define HIKE_LAT  (-27.6f)         // middle of South East Queensland (Brisbane to Gold Coast to Scenic Rim)
#define HIKE_LON  (153.0f)
#define HIKE_PEOPLE 50
#define HIKE_MAXW 40

// Stack is small on the repeater. Handlers are never inlined into each other, and every large work buffer is static
// (the bot answers one request at a time and is never re-entered), so at most one small frame is live at a time.
#if defined(__GNUC__)
  #define HB_NOINLINE __attribute__((noinline))
#else
  #define HB_NOINLINE
#endif

// ---------------------------------------------------------------------------------------------------------------
// request context

struct HikeCtx {
  const char* sender;   // sender name, not 0 terminated
  int slen;
  const char* node_name;
  uint32_t now_utc;     // 0 = clock not set
  int snr10;            // SNR of the request in tenths of a dB
  int rssi;
  int hops;
  uint32_t seed;        // random number, for variety
  bool test_only;       // #test channel: answer only the signal-test commands, ignore everything else
};

// ---------------------------------------------------------------------------------------------------------------
// memory of people

#define HP_OFFTRAIL  0x01   // said they are going off trail; used by their next hike timer
#define HP_AWAIT_DUR 0x02
#define HP_HINT_SEEN 0x04
#define HP_ASSUMED   0x08   // the running timer's trail was assumed (same as last time): a trail name or "off trail" corrects it

// what last_trail / t_trail can hold besides a trail number (0 or more)
#define HT_NONE     (-1)    // nothing known
#define HT_UNKNOWN  (-2)    // a hike that is not in the trail library
#define HT_OFFTRAIL (-3)    // an off-trail hike
#define HI_NONE    0
#define HI_SUGGEST 1

struct HikePerson {
  uint16_t id;          // hash of the sender's name, 0 = free slot
  uint16_t age;
  int16_t last_trail;   // the last hike: a trail number, or HT_NONE / HT_UNKNOWN / HT_OFFTRAIL
  int16_t t_trail;      // the running hike timer's hike: a trail number, or HT_NONE / HT_UNKNOWN / HT_OFFTRAIL
  uint32_t t_start;     // UTC seconds when the timer started, 0 = no timer
  uint16_t pace_pct;    // this person's time as a percent of the listed time, 0 = unknown
  uint8_t pace_n;
  uint8_t flags;
  uint8_t last_intent;
  uint8_t sg_glo, sg_ghi;     // grade range of the last walk search
  uint8_t sg_tmin, sg_tmax;   // minutes / 10, 0 = no limit
  uint16_t sg_feat;           // feature bits
  int8_t sg_town, sg_region;  // place filter, -1 none
  uint8_t sg_off;             // how many results were shown already
};

static HikePerson g_hike_people[HIKE_PEOPLE];
static uint16_t g_hike_tick = 0;
static bool g_hike_ready = false;
static uint8_t g_hike_cyc[HIKE_TOPIC_COUNT];

// a running hike timer older than this is treated as left on (the same limit hbDoFinish uses), so it no longer
// protects its slot
#define HIKE_TIMER_LIVE_SECS (16UL * 3600UL)

// when every slot holds a live timer, a new person gets this scratch slot: it works for one request and is not remembered
static HikePerson g_hike_temp;

static bool hikeTimerLive(const HikePerson* p, uint32_t now_utc) {
  if (p->t_start == 0) return false;
  if (now_utc == 0 || now_utc < p->t_start) return true;   // clock not set / went backwards: keep the timer
  return (now_utc - p->t_start) <= HIKE_TIMER_LIVE_SECS;
}

// the person's slot. Order: their own slot, a free slot, the oldest slot WITHOUT a live hike timer. If every slot has a
// live timer (50 hikers walking at once) the scratch slot g_hike_temp is returned, so no running timer is ever lost.
static HikePerson* hikePersonFor(uint16_t id, uint32_t now_utc) {
  if (id == 0) id = 1;
  g_hike_tick++;
  HikePerson* free_slot = NULL;
  HikePerson* oldest = NULL;
  for (int i = 0; i < HIKE_PEOPLE; i++) {
    HikePerson* p = &g_hike_people[i];
    if (p->id == id) { p->age = g_hike_tick; return p; }
    if (p->id == 0) {
      if (!free_slot) free_slot = p;
      continue;
    }
    if (hikeTimerLive(p, now_utc)) continue;   // protected
    if (!oldest || (uint16_t)(g_hike_tick - p->age) > (uint16_t)(g_hike_tick - oldest->age)) oldest = p;
  }
  HikePerson* p = free_slot ? free_slot : oldest;
  if (!p) p = &g_hike_temp;   // every slot has a live timer
  memset(p, 0, sizeof(*p));
  p->id = id;
  p->age = g_hike_tick;
  p->last_trail = HT_NONE;
  p->t_trail = HT_NONE;
  p->sg_town = -1;
  p->sg_region = -1;
  return p;
}

// ---------------------------------------------------------------------------------------------------------------
// small helpers

static bool hbHas(const char* padded, const char* keys) {
  char mk[48];
  return chatMatchKeys(padded, keys, mk, sizeof(mk)) != NULL;
}

static int hbWordCount(const char* s) {
  int n = 0;
  bool in = false;
  for (; *s; s++) {
    if (*s == ' ') in = false;
    else if (!in) { in = true; n++; }
  }
  return n;
}

// append a message (printf style). A text longer than the budget (a long node name) is split at a sentence end,
// else at a space, and goes on in the next message; if the queue is full the rest is dropped.
struct HbOut {
  char (*out)[BOT_MSG_MAX + 1];
  int n;
  int budget;
};

static HB_NOINLINE bool hbMsg(HbOut* o, const char* fmt, ...) {
  static char tmp[BOT_MSG_MAX * 2 + 80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  int tl = (int)strlen(tmp);
  while (tl > 0 && tmp[tl - 1] == ' ') tmp[--tl] = 0;
  const char* s = tmp;
  while (*s) {
    if (o->n >= BOT_Q_MAX) return false;
    int len = (int)strlen(s);
    int cut = len;
    if (len > o->budget) {
      cut = -1;
      for (int i = o->budget; i > o->budget / 3; i--) if (s[i] == ' ' && s[i - 1] == '.') { cut = i; break; }
      if (cut < 0) for (int i = o->budget; i > o->budget / 3; i--) if (s[i] == ' ') { cut = i; break; }
      if (cut < 0) cut = o->budget;
    }
    int pl = cut;
    while (pl > 0 && (s[pl - 1] == ' ' || (cut < len && (s[pl - 1] == ',' || s[pl - 1] == ';')))) pl--;
    if (pl > 0) {
      memcpy(o->out[o->n], s, pl);
      o->out[o->n][pl] = 0;
      if (o->out[o->n][0] == '!') o->out[o->n][0] = '.';   // never start with '!'
      o->n++;
    }
    s += cut;
    while (*s == ' ') s++;
  }
  return true;
}

// a text of up to 4 parts separated by '~' becomes messages
static HB_NOINLINE void hbParts(HbOut* o, const char* text) {
  while (text && *text) {
    const char* e = strchr(text, '~');
    int len = e ? (int)(e - text) : (int)strlen(text);
    char seg[BOT_MSG_MAX + 1];
    if (len > BOT_MSG_MAX) len = BOT_MSG_MAX;
    memcpy(seg, text, len);
    seg[len] = 0;
    hbMsg(o, "%s", seg);
    text = e ? e + 1 : NULL;
  }
}

static void hbFmtMin(int m, char* out, int osz) {
  if (m < 0) m = 0;
  int h = m / 60, mm = m % 60;
  if (h == 0) snprintf(out, osz, "%d min", mm);
  else if (mm == 0) snprintf(out, osz, "%d hr", h);
  else snprintf(out, osz, "%d hr %d min", h, mm);
}

static void hbFmtClock(int mins, char* out, int osz) {   // "5:07 pm"
  mins = ((mins % 1440) + 1440) % 1440;
  int h = mins / 60, m = mins % 60;
  int h12 = h % 12;
  if (h12 == 0) h12 = 12;
  snprintf(out, osz, "%d:%02d %s", h12, m, h < 12 ? "am" : "pm");
}

static void hbFmtKm(int d100, char* out, int osz) {
  if (d100 % 100 == 0) snprintf(out, osz, "%dkm", d100 / 100);
  else if (d100 % 10 == 0) snprintf(out, osz, "%d.%dkm", d100 / 100, (d100 % 100) / 10);
  else snprintf(out, osz, "%d.%02dkm", d100 / 100, d100 % 100);
}

// ---------------------------------------------------------------------------------------------------------------
// reading the trail info text ("4km return, 2h, Gr 3, waterfalls, rainforest")

struct HikeInfo {
  int dist100;       // km * 100, -1 unknown
  int tlo, thi;      // minutes, -1 unknown
  bool tapprox;
  int grade;         // 1..5, 0 none listed
  bool offtrack;
  char f0[30];       // distance text as listed ("4km return"), or a label, "" if none
  char ft[14];       // time text as listed
  char feat[100];    // the rest
};

static bool hbReadNum100(const char** pp, int* v) {
  const char* p = *pp;
  if (*p < '0' || *p > '9') return false;
  int ip = 0, fp = 0, fd = 0;
  while (*p >= '0' && *p <= '9') { ip = ip * 10 + (*p - '0'); p++; }
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') {
      if (fd < 2) { fp = fp * 10 + (*p - '0'); fd++; }
      p++;
    }
  }
  while (fd < 2) { fp *= 10; fd++; }
  *v = ip * 100 + fp;
  *pp = p;
  return true;
}

// "2h", "15-30min", "~7h": the whole field must be a time
static bool hbParseTimeField(const char* f, int* lo, int* hi, bool* approx) {
  const char* p = f;
  *approx = false;
  if (*p == '~') { *approx = true; p++; }
  int a, b;
  if (!hbReadNum100(&p, &a)) return false;
  b = a;
  if (*p == '-') { p++; if (!hbReadNum100(&p, &b)) return false; }
  int mul;
  if (strcmp(p, "h") == 0) mul = 60;
  else if (strcmp(p, "min") == 0) mul = 1;
  else return false;
  *lo = a * mul / 100;
  *hi = b * mul / 100;
  return true;
}

// "4km return", "4-5.6km return", "0.6km one way": a number (or range) straight followed by km
static bool hbParseDistField(const char* f, int* d100) {
  const char* p = f;
  if (*p == '~') p++;
  int a, b;
  if (!hbReadNum100(&p, &a)) return false;
  b = a;
  if (*p == '-') { p++; if (!hbReadNum100(&p, &b)) return false; }
  if (p[0] != 'k' || p[1] != 'm') return false;
  *d100 = (a + b) / 2;
  return true;
}

static HB_NOINLINE void hbParseInfo(const char* info, HikeInfo* r) {
  r->dist100 = -1; r->tlo = r->thi = -1; r->tapprox = false; r->grade = 0; r->offtrack = false;
  r->f0[0] = r->ft[0] = r->feat[0] = 0;
  char buf[160];
  strncpy(buf, info, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char* p = buf;
  int idx = 0;
  while (p && *p) {
    char* e = strchr(p, ',');
    if (e) *e = 0;
    while (*p == ' ') p++;
    int len = (int)strlen(p);
    while (len > 0 && p[len - 1] == ' ') p[--len] = 0;
    int lo, hi, d;
    bool ap;
    if (len > 0) {
      if (r->tlo < 0 && hbParseTimeField(p, &lo, &hi, &ap)) {
        r->tlo = lo; r->thi = hi; r->tapprox = ap;
        snprintf(r->ft, sizeof(r->ft), "%s", p);
      } else if (r->grade == 0 && strncmp(p, "Gr ", 3) == 0 && p[3] >= '1' && p[3] <= '5') {
        r->grade = p[3] - '0';
      } else if (r->dist100 < 0 && hbParseDistField(p, &d)) {
        r->dist100 = d;
        snprintf(r->f0, sizeof(r->f0), "%s", p);
      } else if (idx == 0 && r->f0[0] == 0) {
        snprintf(r->f0, sizeof(r->f0), "%s", p);   // a label such as "short walk"
      } else {
        int fl = (int)strlen(r->feat);
        snprintf(r->feat + fl, sizeof(r->feat) - fl, "%s%s", fl ? ", " : "", p);
      }
    }
    idx++;
    p = e ? e + 1 : NULL;
  }
  if (strstr(info, "off-track") || strstr(info, "off track")) r->offtrack = true;
}

static bool hbInfoHas(const char* info, const char* word) {
  return strstr(info, word) != NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// clock, date and sun times (South East Queensland, approximate)

struct HikeClock {
  bool ok;
  int year, mon, day, wday, mins;   // mins = minutes since local midnight
  int rise, set;                    // local minutes since midnight
};

static const char* const HB_DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const HB_MONTHS[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                        "September", "October", "November", "December"};

static void hbCivil(long z, int* y, int* m, int* d) {   // days since 1970-01-01 to a calendar date
  z += 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long yy = yoe + era * 400;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yy + (*m <= 2));
}

static void hbSun(int year, int mon, int day, int* rise, int* set) {
  static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int doy = cum[mon - 1] + day;
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  if (leap && mon > 2) doy++;
  float g = 6.2831853f / (leap ? 366.0f : 365.0f) * (float)(doy - 1);
  float eq = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) - 0.014615f * cosf(2 * g) -
                        0.040849f * sinf(2 * g));
  float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g) +
               0.000907f * sinf(2 * g) - 0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  float lat = HIKE_LAT * 0.017453293f;
  float c = (cosf(90.833f * 0.017453293f) - sinf(lat) * sinf(decl)) / (cosf(lat) * cosf(decl));
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  float ha = acosf(c) * 57.29578f;   // degrees
  float tz = (float)HIKE_TZ_SECS / 60.0f;
  float rs = 720.0f - 4.0f * (HIKE_LON + ha) - eq + tz;
  float st = 720.0f - 4.0f * (HIKE_LON - ha) - eq + tz;
  *rise = (int)(rs + 0.5f);
  *set = (int)(st + 0.5f);
}

static void hbClockInit(uint32_t now_utc, HikeClock* c) {
  memset(c, 0, sizeof(*c));
  if (now_utc == 0) return;
  long local = (long)now_utc + HIKE_TZ_SECS;
  long days = local / 86400L;
  long secs = local % 86400L;
  hbCivil(days, &c->year, &c->mon, &c->day);
  c->wday = (int)((days + 4) % 7);
  c->mins = (int)(secs / 60);
  hbSun(c->year, c->mon, c->day, &c->rise, &c->set);
  c->ok = true;
}

// ---------------------------------------------------------------------------------------------------------------
// a day named in a sentence: tomorrow, Friday, in 3 days, 25 December (for sunrise, sunset, moon and glow worm times)

static long hbDaysFromCivil(int y, int m, int d) {   // calendar date to days since 1970-01-01 (the inverse of hbCivil)
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153L * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

static int hbDaysInMonth(int y, int m) {
  static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return dm[m - 1] + (m == 2 && leap ? 1 : 0);
}

static bool hbWordIs(const char* w, int n, const char* lit) {
  return (int)strlen(lit) == n && strncmp(w, lit, n) == 0;
}

static int hbMonthWord(const char* w, int n) {   // 1 to 12, 0 if not a month
  static const char* const nm[] = {"january", "february", "march", "april", "may", "june", "july", "august", "september",
                                   "october", "november", "december"};
  for (int i = 0; i < 12; i++) {
    if (hbWordIs(w, n, nm[i])) return i + 1;
    if (n == 3 && strncmp(w, nm[i], 3) == 0) return i + 1;   // jan, feb, mar ...
  }
  if (hbWordIs(w, n, "sept")) return 9;
  return 0;
}

static int hbWeekdayWord(const char* w, int n) {   // 0 Sunday to 6 Saturday, -1 if not a weekday
  static const char* const wd[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
  for (int i = 0; i < 7; i++) if (hbWordIs(w, n, wd[i])) return i;
  if (hbWordIs(w, n, "tues")) return 2;
  if (hbWordIs(w, n, "weds")) return 3;
  if (hbWordIs(w, n, "thur") || hbWordIs(w, n, "thurs")) return 4;
  return -1;
}

// "25", "25th", "3rd" or a number word one to ten
static bool hbNumberWord(const char* w, int n, int* v) {
  int i = 0, x = 0;
  while (i < n && w[i] >= '0' && w[i] <= '9') { x = x * 10 + (w[i] - '0'); i++; if (x > 9999) return false; }
  if (i > 0) {
    bool suffix = (n - i == 2) && ((w[i] == 's' && w[i + 1] == 't') || (w[i] == 'n' && w[i + 1] == 'd') ||
                                   (w[i] == 'r' && w[i + 1] == 'd') || (w[i] == 't' && w[i + 1] == 'h'));
    if (i == n || suffix) { *v = x; return true; }
    return false;
  }
  static const char* const nw[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"};
  for (int k = 0; k < 10; k++) if (hbWordIs(w, n, nw[k])) { *v = k + 1; return true; }
  return false;
}

// The first day the sentence names, as days since 1970-01-01 (local). false if none.
// tomorrow, yesterday, today, tonight, the day after tomorrow, in 3 days, in a week, Friday (the next one),
// 25 December, 25th of December, December 25, with an optional year.
static bool hbFindDay(const char* norm, long today_ld, long* out_ld) {
  const char* tp[32];
  int tl[32];
  int nt = 0;
  const char* s = norm;
  while (*s && nt < 32) {
    while (*s == ' ') s++;
    if (!*s) break;
    const char* e = strchr(s, ' ');
    int len = e ? (int)(e - s) : (int)strlen(s);
    tp[nt] = s;
    tl[nt] = len;
    nt++;
    s += len;
  }
  int ty, tm, td;
  hbCivil(today_ld, &ty, &tm, &td);
  int today_wd = (int)((today_ld + 4) % 7);
  for (int i = 0; i < nt; i++) {
    const char* w = tp[i];
    int n = tl[i];
    long ld = 0;
    bool found = false;
    if (hbWordIs(w, n, "day") && i + 2 < nt && hbWordIs(tp[i + 1], tl[i + 1], "after") &&
        (hbWordIs(tp[i + 2], tl[i + 2], "tomorrow") || hbWordIs(tp[i + 2], tl[i + 2], "tmrw"))) {
      ld = today_ld + 2; found = true;
    } else if (hbWordIs(w, n, "tomorrow") || hbWordIs(w, n, "tomorow") || hbWordIs(w, n, "tommorow") || hbWordIs(w, n, "tmrw") ||
               hbWordIs(w, n, "tmr")) {
      ld = today_ld + 1; found = true;
    } else if (hbWordIs(w, n, "yesterday")) {
      ld = today_ld - 1; found = true;
    } else if (hbWordIs(w, n, "today") || hbWordIs(w, n, "tonight")) {
      ld = today_ld; found = true;
    } else if (hbWordIs(w, n, "in") && i + 2 < nt) {
      int v = 0;
      if (hbWordIs(tp[i + 1], tl[i + 1], "a") && hbWordIs(tp[i + 2], tl[i + 2], "week")) { ld = today_ld + 7; found = true; }
      else if (hbNumberWord(tp[i + 1], tl[i + 1], &v) && v >= 1 && v <= 366) {
        if (hbWordIs(tp[i + 2], tl[i + 2], "day") || hbWordIs(tp[i + 2], tl[i + 2], "days")) { ld = today_ld + v; found = true; }
        else if (hbWordIs(tp[i + 2], tl[i + 2], "week") || hbWordIs(tp[i + 2], tl[i + 2], "weeks")) { ld = today_ld + 7L * v; found = true; }
      }
    }
    if (!found) {
      int wd = hbWeekdayWord(w, n);
      if (wd >= 0) {
        int off = (wd - today_wd + 7) % 7;
        if (off == 0) off = 7;   // today's weekday name means a week away
        ld = today_ld + off;
        found = true;
      }
    }
    if (!found) {
      int mo = hbMonthWord(w, n);
      if (mo > 0) {
        int d = 0, yr = 0, yi = i + 1;
        bool have = false;
        if (i >= 1 && hbNumberWord(tp[i - 1], tl[i - 1], &d)) have = true;
        else if (i >= 2 && hbWordIs(tp[i - 1], tl[i - 1], "of") && hbNumberWord(tp[i - 2], tl[i - 2], &d)) have = true;
        if (!have) {
          int j = i + 1;
          if (j < nt && hbWordIs(tp[j], tl[j], "the")) j++;
          if (j < nt && hbNumberWord(tp[j], tl[j], &d)) { have = true; yi = j + 1; }
        }
        if (have && d >= 1 && d <= 31) {
          int v = 0;
          if (yi < nt && hbNumberWord(tp[yi], tl[yi], &v) && v >= 2000 && v <= 2100 && tl[yi] == 4) yr = v;
          int y = yr ? yr : ty;
          if (d <= hbDaysInMonth(y, mo)) {
            ld = hbDaysFromCivil(y, mo, d);
            if (!yr && ld < today_ld) {
              y++;
              if (d <= hbDaysInMonth(y, mo)) ld = hbDaysFromCivil(y, mo, d);
              else have = false;
            }
            found = have;
          }
        }
      }
    }
    if (found) {
      if (ld - today_ld > 3660L || today_ld - ld > 3660L) return false;
      *out_ld = ld;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// finding trails, towns and regions named in a sentence (exact, or a close misspelling)

struct HbWords {
  int n;
  const char* p[HIKE_MAXW];
  int l[HIKE_MAXW];
  bool used[HIKE_MAXW];
};

static void hbSplit(const char* norm, HbWords* w) {
  w->n = 0;
  const char* s = norm;
  while (*s && w->n < HIKE_MAXW) {
    const char* e = strchr(s, ' ');
    int len = e ? (int)(e - s) : (int)strlen(s);
    w->p[w->n] = s;
    w->l[w->n] = len;
    w->used[w->n] = false;
    w->n++;
    s = e ? e + 1 : s + len;
  }
}

struct HbHit {
  bool ok;
  int kind;    // 0 trail, 1 town, 2 region
  int idx;
  int dist;    // 0 = exact
  int plen;    // length of the phrase matched
  int w0, nw;  // words covered
  bool tie;    // another, different, equally good fuzzy match exists
  int alt_kind, alt_idx;
};

// edit distance of a and b (insert, delete, change, swap two letters) if it is at most maxd, else maxd + 1
static HB_NOINLINE int hbEdit(const char* a, int la, const char* b, int lb, int maxd) {
  if (la - lb > maxd || lb - la > maxd) return maxd + 1;
  if (la > 48 || lb > 48) return maxd + 1;
  static int r0[50], r1[50], r2[50];
  int *p2 = r0, *p1 = r1, *cur = r2;
  for (int j = 0; j <= lb; j++) p1[j] = j;
  for (int i = 1; i <= la; i++) {
    cur[0] = i;
    int rowmin = i;
    for (int j = 1; j <= lb; j++) {
      int v = p1[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
      if (p1[j] + 1 < v) v = p1[j] + 1;
      if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] && p2[j - 2] + 1 < v) v = p2[j - 2] + 1;
      cur[j] = v;
      if (v < rowmin) rowmin = v;
    }
    if (rowmin > maxd) return maxd + 1;
    int* tmp = p2;
    p2 = p1;
    p1 = cur;
    cur = tmp;
  }
  return p1[lb] <= maxd ? p1[lb] : maxd + 1;
}

static bool hbBetter(const HbHit* a, const HbHit* b) {   // is a better than b?
  if (!b->ok) return true;
  bool ae = a->dist == 0, be = b->dist == 0;
  if (ae != be) return ae;
  if (ae) {
    if (a->plen != b->plen) return a->plen > b->plen;
    return a->kind < b->kind;
  }
  if (a->dist != b->dist) return a->dist < b->dist;
  if (a->plen != b->plen) return a->plen > b->plen;
  return a->kind < b->kind;
}

static void hbTryPhrase(const HbWords* w, const char* phrase, int kind, int idx, HbHit* best) {
  int pl = (int)strlen(phrase);
  if (pl == 0) return;
  int pw = 1;
  for (const char* c = phrase; *c; c++) if (*c == ' ') pw++;
  int tol = pl < 6 ? 0 : (pl < 10 ? 1 : 2);
  for (int s = 0; s + pw <= w->n; s++) {
    bool bad = false;
    int wl = pw - 1;
    for (int k = 0; k < pw; k++) { if (w->used[s + k]) bad = true; wl += w->l[s + k]; }
    if (bad) continue;
    if (wl - pl > tol || pl - wl > tol) continue;
    char win[56];
    if (wl > 54) continue;
    int o = 0;
    for (int k = 0; k < pw; k++) {
      if (k) win[o++] = ' ';
      memcpy(win + o, w->p[s + k], w->l[s + k]);
      o += w->l[s + k];
    }
    win[o] = 0;
    int d;
    if (wl == pl && memcmp(win, phrase, pl) == 0) d = 0;
    else if (tol == 0 || wl < 5) continue;
    else {
      d = hbEdit(win, wl, phrase, pl, tol);
      if (d > tol) continue;
    }
    HbHit h;
    h.ok = true; h.kind = kind; h.idx = idx; h.dist = d; h.plen = pl; h.w0 = s; h.nw = pw; h.tie = false;
    h.alt_kind = h.alt_idx = -1;
    if (best->ok && best->kind == kind && best->idx == idx) {
      if (hbBetter(&h, best)) *best = h;
      continue;
    }
    if (hbBetter(&h, best)) {
      HbHit old = *best;
      *best = h;
      if (old.ok && old.dist > 0 && h.dist > 0 && old.dist == h.dist && old.plen == h.plen && old.kind == h.kind) {
        best->tie = true; best->alt_kind = old.kind; best->alt_idx = old.idx;
      }
    } else if (best->ok && best->dist > 0 && h.dist == best->dist && h.plen == best->plen && h.kind == best->kind) {
      best->tie = true; best->alt_kind = kind; best->alt_idx = idx;
    }
  }
}

static void hbTryEntity(const HbWords* w, const char* name, const char* aliases, int kind, int idx, HbHit* best) {
  char tmp[TRAILBOT_QUERY_MAX + 8];
  trailbotNorm(name, tmp, sizeof(tmp));
  hbTryPhrase(w, tmp, kind, idx, best);
  const char* p = aliases;
  while (p && *p) {
    const char* e = strchr(p, '|');
    int len = e ? (int)(e - p) : (int)strlen(p);
    char seg[TRAILBOT_QUERY_MAX + 8];
    if (len > (int)sizeof(seg) - 1) len = (int)sizeof(seg) - 1;
    memcpy(seg, p, len);
    seg[len] = 0;
    trailbotNorm(seg, tmp, sizeof(tmp));
    if (tmp[0]) hbTryPhrase(w, tmp, kind, idx, best);
    p = e ? e + 1 : NULL;
  }
}

// best trail / town / region named in the words not yet used (kinds: bit 0 trails, 1 towns, 2 regions)
static HbHit hbFind(HbWords* w, int kinds) {
  HbHit best;
  memset(&best, 0, sizeof(best));
  if (kinds & 1) for (int i = 0; i < TRAIL_COUNT; i++) hbTryEntity(w, TRAILS[i].name, TRAILS[i].aliases, 0, i, &best);
  if (kinds & 2) for (int i = 0; i < TRAIL_TOWN_COUNT; i++) hbTryEntity(w, TRAIL_TOWNS[i].name, TRAIL_TOWNS[i].aliases, 1, i, &best);
  if (kinds & 4) for (int i = 0; i < TRAIL_REGION_COUNT; i++) hbTryEntity(w, TRAIL_REGIONS[i].name, TRAIL_REGIONS[i].aliases, 2, i, &best);
  return best;
}

static void hbUse(HbWords* w, const HbHit* h) {
  for (int k = 0; k < h->nw; k++) w->used[h->w0 + k] = true;
}

static const char* hbEntityName(int kind, int idx) {
  return kind == 0 ? TRAILS[idx].name : (kind == 1 ? TRAIL_TOWNS[idx].name : TRAIL_REGIONS[idx].name);
}

static int hbTrailByWords(const char* phrase) {   // "natural bridge" -> trail index, or -1
  char norm[60];
  trailbotNorm(phrase, norm, sizeof(norm));
  static HbWords w;
  hbSplit(norm, &w);
  HbHit h = hbFind(&w, 1);
  return (h.ok && h.dist == 0) ? h.idx : -1;
}

// ---------------------------------------------------------------------------------------------------------------
// durations in a message: "2 hours", "90 minutes", "1h 20m", "an hour"

static HB_NOINLINE int hbParseDuration(const char* norm) {
  int total = -1;
  static HbWords w;
  hbSplit(norm, &w);
  for (int i = 0; i < w.n; i++) {
    // forms: "2" "hours" | "2h" | "2hours" | "an" "hour" | "half" "an" "hour"
    int val = -1;
    const char* unit = NULL;
    int ul = 0;
    const char* s = w.p[i];
    int l = w.l[i];
    int k = 0;
    int num = 0;
    while (k < l && s[k] >= '0' && s[k] <= '9') { num = num * 10 + (s[k] - '0'); k++; if (num > 5000) break; }
    if (k > 0) {
      val = num;
      if (k < l) { unit = s + k; ul = l - k; }
      else if (i + 1 < w.n) { unit = w.p[i + 1]; ul = w.l[i + 1]; }
    } else if (l == 2 && memcmp(s, "an", 2) == 0 && i + 1 < w.n) {
      val = 1; unit = w.p[i + 1]; ul = w.l[i + 1];
    }
    if (val < 0 || !unit) continue;
    int mins = -1;
    if ((ul == 1 && unit[0] == 'h') || (ul == 2 && memcmp(unit, "hr", 2) == 0) || (ul == 3 && memcmp(unit, "hrs", 3) == 0) ||
        (ul == 4 && memcmp(unit, "hour", 4) == 0) || (ul == 5 && memcmp(unit, "hours", 5) == 0))
      mins = val * 60;
    else if ((ul == 1 && unit[0] == 'm') || (ul == 3 && memcmp(unit, "min", 3) == 0) || (ul == 4 && memcmp(unit, "mins", 4) == 0) ||
             (ul == 6 && memcmp(unit, "minute", 6) == 0) || (ul == 7 && memcmp(unit, "minutes", 7) == 0))
      mins = val;
    if (mins >= 0) total = (total < 0 ? 0 : total) + mins;
  }
  return total;
}

// ---------------------------------------------------------------------------------------------------------------
// the sentence being answered

struct HbCtx {
  const HikeCtx* c;
  HbOut o;
  HikePerson* who;
  HikeClock clk;
  char norm[CHAT_WORK_MAX];
  char padded[CHAT_WORK_MAX + 4];
  HbWords w;
  bool question;     // the raw text had a '?'
};

// may a short follow-up use the person's last trail? ("how long is it", "what should i take") but not a long sentence
static bool hbLastOk(HbCtx* H) {
  return hbWordCount(H->norm) <= 4 ||
         hbHas(H->padded, "it;that;there;this;the walk;the trail;the track;the hike;that walk;this walk;that trail;this trail");
}

// copies of the sentence's words for searches that mark words as used (static: keeps the stack small)
static HbWords g_hb_scratch[3];
static HbWords* hbCopyWords(const HbWords* src, int k) {
  g_hb_scratch[k] = *src;
  return &g_hb_scratch[k];
}

// trail named in the sentence; else (if allowed) the person's last trail. -1 if none.
static int hbTrailIn(HbCtx* H, bool allow_last, bool mark = true) {
  HbHit h = hbFind(&H->w, 1);
  if (h.ok) {
    if (mark) hbUse(&H->w, &h);
    return h.idx;
  }
  if (allow_last && H->who->last_trail >= 0) return H->who->last_trail;
  return -1;
}

// words that say "off trail" (an off-trail hike has no listed time)
#define HB_OFFTRAIL_PHRASES "off trail;off track;off the trail;off the track;offtrail;offtrack;bush bash;bush bashing;bushbash;bushbashing;" \
                            "cross country;my own route;own route;no track;no trail;without a track;without a trail;unmarked;" \
                            "not on a trail;not on a track;pathless;off path;off the path"

static bool hbWordIn(const char* w, int l, const char* list) {
  const char* s = list;
  while (*s) {
    const char* e = strchr(s, ';');
    int len = e ? (int)(e - s) : (int)strlen(s);
    if (len == l && memcmp(s, w, l) == 0) return true;
    if (!e) break;
    s = e + 1;
  }
  return false;
}

// "starting the Barney Peak walk": a name in front of a trail word, or after Mt / Mount, that is not one of my trails.
// Only asked when no trail of mine was named in the sentence.
static bool hbUnlistedName(HbCtx* H) {
  static const char* const nouns = "track;trail;walk;hike;circuit;loop;climb;summit;peak;falls;lookout;ridge;hikes;walks;trails;tracks";
  static const char* const plain = "the;my;a;an;our;this;that;new;first;next;another;same;last;your;their;whole;full;quick;short;long;little;big;"
                                   "easy;hard;hilly;day;morning;afternoon;evening;night;sunrise;sunset;solo;group;family;dog;bush;nature;"
                                   "coastal;rainforest;local;nice;good;great;steep;scenic;off;own;now;again;today;am;are;to;on;in;of;and;"
                                   "for;with;it;is;i;we;us;start;starting;started;begin;beginning;hit;hitting;head;heading;set;setting;"
                                   "done;finish;finished;completed;stop;timer;out;at;trailhead;im;going;doing;about;gonna;will;were;just;"
                                   "back;from;mountain;half;overnight;weekend;sunny;cool;hot;cold;km;kms;k;kilometre;kilometres;hour;hours;hr;hrs;min;mins;"
                                   "minute;minutes;mile;miles;lunch;birthday";
  const HbWords* w = &H->w;
  for (int i = 0; i < w->n; i++) {
    bool is_noun = hbWordIn(w->p[i], w->l[i], nouns);
    if (is_noun && i >= 1) {
      const char* pw = w->p[i - 1];
      int pl = w->l[i - 1];
      if (pw[0] >= '0' && pw[0] <= '9') continue;   // "a 5 km walk"
      if (!hbWordIn(pw, pl, plain) && !hbWordIn(pw, pl, nouns)) return true;
    }
    if ((w->l[i] == 2 && memcmp(w->p[i], "mt", 2) == 0) || (w->l[i] == 5 && memcmp(w->p[i], "mount", 5) == 0)) {
      if (i + 1 < w->n) return true;
    }
  }
  return false;
}

// which hike does this message talk about? A trail of mine (its number), HT_OFFTRAIL, HT_UNKNOWN, or (if allowed) the
// person's last hike, whatever that was (*assumed is then true), else HT_NONE.
static HB_NOINLINE int hbPickHike(HbCtx* H, bool allow_last, bool* assumed) {
  HikePerson* P = H->who;
  if (assumed) *assumed = false;
  int t = hbTrailIn(H, false);
  if (t >= 0) { P->flags &= ~HP_OFFTRAIL; return t; }
  if (hbHas(H->padded, HB_OFFTRAIL_PHRASES)) { P->flags &= ~HP_OFFTRAIL; return HT_OFFTRAIL; }
  if (hbUnlistedName(H)) { P->flags &= ~HP_OFFTRAIL; return HT_UNKNOWN; }
  if (P->flags & HP_OFFTRAIL) { P->flags &= ~HP_OFFTRAIL; return HT_OFFTRAIL; }
  if (allow_last && P->last_trail != HT_NONE) {
    if (assumed) *assumed = true;
    return P->last_trail;
  }
  return HT_NONE;
}

static bool hbIsGlowTrail(int t) {
  return t >= 0 && strstr(TRAILS[t].name, "Natural Bridge") != NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// signal test
// What this repeater heard from the LAST hop (the hiker's radio only when Hops is 0, else the previous repeater), so it
// is reported as it is, with no verdict, no tip and no comparison with an earlier test.

static HB_NOINLINE int hbDoTest(HbCtx* H) {
  const HikeCtx* c = H->c;
  char tstr[12];
  if (!H->clk.ok) strcpy(tstr, "n/a");
  else {
    long secs = ((long)c->now_utc + HIKE_TZ_SECS) % 86400L;
    snprintf(tstr, sizeof(tstr), "%02d:%02d:%02d", (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
  }
  static char rep[BOT_MSG_MAX + 1];
  trailbotTestReply(c->sender, c->slen, H->o.budget, tstr, c->snr10, c->rssi, c->hops, rep);
  hbMsg(&H->o, "%s", rep);
  return H->o.n;
}

// ---------------------------------------------------------------------------------------------------------------
// another day than today, named in the sentence ("!sunrise tomorrow", "sunset on Friday", "moon on 25 December")

struct HbDay {
  long ld;            // local days since 1970-01-01
  int off;            // days from today (negative = past)
  int year, mon, day, wday;
  int rise, set;      // sun, local minutes since midnight
};

// true (and the day filled in) only if the sentence names a day other than today
static bool hbDayInfo(const HbCtx* H, HbDay* d) {
  if (!H->clk.ok) return false;
  long today_ld = ((long)H->c->now_utc + HIKE_TZ_SECS) / 86400L;
  long ld = 0;
  if (!hbFindDay(H->norm, today_ld, &ld) || ld == today_ld) return false;
  d->ld = ld;
  d->off = (int)(ld - today_ld);
  hbCivil(ld, &d->year, &d->mon, &d->day);
  d->wday = (int)((ld + 4) % 7);
  hbSun(d->year, d->mon, d->day, &d->rise, &d->set);
  return true;
}

// "Tomorrow (Sun 8 Nov)", "Fri 13 Nov (in 6 days)", "Sun 1 Nov (3 days ago)"
static void hbDayLabel(const HbDay* d, char* out, int osz) {
  char dt[24];
  if (d->off > 300 || d->off < -300) snprintf(dt, sizeof(dt), "%.3s %d %.3s %d", HB_DAYS[d->wday], d->day, HB_MONTHS[d->mon - 1], d->year);
  else snprintf(dt, sizeof(dt), "%.3s %d %.3s", HB_DAYS[d->wday], d->day, HB_MONTHS[d->mon - 1]);
  if (d->off == 1) snprintf(out, osz, "Tomorrow (%s)", dt);
  else if (d->off == -1) snprintf(out, osz, "Yesterday (%s)", dt);
  else if (d->off > 0) snprintf(out, osz, "%s (in %d days)", dt, d->off);
  else snprintf(out, osz, "%s (%d days ago)", dt, -d->off);
}

// ---------------------------------------------------------------------------------------------------------------
// time, date and daylight

static HB_NOINLINE void hbNoClock(HbCtx* H) {
  hbMsg(&H->o, "My clock isn't set, so I can't tell the time.");
  hbMsg(&H->o, "In SE Qld sunrise is about 4:40 to 6:30 am and sunset about 4:55 to 6:35 pm, depending on the season.");
}

static HB_NOINLINE int hbDoTimeDate(HbCtx* H) {
  if (!H->clk.ok) { hbNoClock(H); return H->o.n; }
  HbDay td;
  if (hbDayInfo(H, &td)) {   // "what is the date tomorrow", "what day is it on 25 December"
    char r[14], s[14];
    hbFmtClock(td.rise, r, sizeof(r));
    hbFmtClock(td.set, s, sizeof(s));
    if (td.off == 1) hbMsg(&H->o, "Tomorrow is %s %d %s %d.", HB_DAYS[td.wday], td.day, HB_MONTHS[td.mon - 1], td.year);
    else if (td.off == -1) hbMsg(&H->o, "Yesterday was %s %d %s %d.", HB_DAYS[td.wday], td.day, HB_MONTHS[td.mon - 1], td.year);
    else if (td.off > 0) hbMsg(&H->o, "In %d days it's %s %d %s %d.", td.off, HB_DAYS[td.wday], td.day, HB_MONTHS[td.mon - 1], td.year);
    else hbMsg(&H->o, "%d days ago it was %s %d %s %d.", -td.off, HB_DAYS[td.wday], td.day, HB_MONTHS[td.mon - 1], td.year);
    hbMsg(&H->o, "Sunrise is about %s and sunset about %s that day (SE Qld).", r, s);
    return H->o.n;
  }
  char t[14], r[14], s[14];
  hbFmtClock(H->clk.mins, t, sizeof(t));
  hbFmtClock(H->clk.rise, r, sizeof(r));
  hbFmtClock(H->clk.set, s, sizeof(s));
  hbMsg(&H->o, "It's %s %d %s %d, %s (my clock).", HB_DAYS[H->clk.wday], H->clk.day, HB_MONTHS[H->clk.mon - 1], H->clk.year, t);
  hbMsg(&H->o, "Sunrise is about %s and sunset about %s today (SE Qld).", r, s);
  return H->o.n;
}

static HB_NOINLINE int hbDoSun(HbCtx* H) {
  if (!H->clk.ok) { hbNoClock(H); return H->o.n; }
  HbDay td;
  if (hbDayInfo(H, &td)) {   // "!sunrise tomorrow", "sunset on Friday", "sunrise 25 December"
    char r[14], s[14], fl[14], ll[14], dl[20], lab[44];
    hbFmtClock(td.rise, r, sizeof(r));
    hbFmtClock(td.set, s, sizeof(s));
    hbFmtClock(td.rise - 25, fl, sizeof(fl));
    hbFmtClock(td.set + 25, ll, sizeof(ll));
    hbFmtMin(td.set - td.rise, dl, sizeof(dl));
    hbDayLabel(&td, lab, sizeof(lab));
    hbMsg(&H->o, "%s: sunrise about %s, sunset about %s.", lab, r, s);
    hbMsg(&H->o, "First light about %s, last light about %s. About %s of daylight.", fl, ll, dl);
    return H->o.n;
  }
  char r[14], s[14], x[14], d[20];
  hbFmtClock(H->clk.rise, r, sizeof(r));
  hbFmtClock(H->clk.set, s, sizeof(s));
  hbMsg(&H->o, "Today sunrise is about %s and sunset about %s.", r, s);
  int m = H->clk.mins;
  if (m < H->clk.rise - 25) {
    hbFmtClock(H->clk.rise - 25, x, sizeof(x));
    hbMsg(&H->o, "It's still dark. First light is about %s.", x);
  } else if (m < H->clk.set) {
    hbFmtMin(H->clk.set - m, d, sizeof(d));
    hbFmtClock(H->clk.set - 30, x, sizeof(x));
    hbMsg(&H->o, "About %s of daylight left. Aim to be off the track by %s.", d, x);
  } else {
    hbMsg(&H->o, "It's after sunset. Use a head torch and stay on the track.");
  }
  return H->o.n;
}

// minutes a walk is expected to take for this person: the listed upper time, scaled by their pace
static int hbNeedMin(const HikePerson* p, const HikeInfo* inf) {
  if (inf->thi < 0) return -1;
  int m = inf->thi;
  if (p->pace_pct) m = m * p->pace_pct / 100;
  return m;
}

// one message about the daylight for a walk of need minutes starting at 'start' (local minutes)
static HB_NOINLINE void hbDaylightVerdict(HbCtx* H, int start, int need) {
  const HikeClock* k = &H->clk;
  char s[14], l[14], r[14], f[14];
  hbFmtClock(k->set, s, sizeof(s));
  if (start < k->rise - 25) {
    hbFmtClock(k->rise, r, sizeof(r));
    hbMsg(&H->o, "It's still dark: sunrise is about %s. Wait for light unless you have a head torch and the experience.", r);
    return;
  }
  if (start >= k->set) {
    hbMsg(&H->o, "That's after sunset (about %s). Only go with a head torch and a spare, and tell someone.", s);
    return;
  }
  if (need < 0) {
    hbFmtClock(k->set - 30, l, sizeof(l));
    hbMsg(&H->o, "Sunset is about %s, so aim to be back by %s.", s, l);
    return;
  }
  int fin = start + need;
  hbFmtClock(fin, f, sizeof(f));
  if (fin <= k->set - 30) {
    hbMsg(&H->o, "Back by about %s. Sunset is about %s, so you have plenty of daylight.", f, s);
  } else if (fin <= k->set) {
    hbMsg(&H->o, "Back by about %s, close to sunset (%s). Carry a head torch just in case.", f, s);
  } else {
    int latest = k->set - need - 30;
    if (latest > start) {
      hbFmtClock(latest, l, sizeof(l));
      hbMsg(&H->o, "You'd be back about %s, after sunset (%s). Start by %s, or carry a head torch.", f, s, l);
    } else {
      hbMsg(&H->o, "You'd be back about %s, after sunset (%s). Bring a head torch and a spare, or save it for earlier.", f, s);
    }
  }
}

static HB_NOINLINE int hbDoStartAdvice(HbCtx* H, int t, int dur_min) {
  if (!H->clk.ok) {
    hbMsg(&H->o, "I can't see the time (my clock isn't set). Start early so you're back well before sunset.");
    hbMsg(&H->o, "SE Qld sunset is about 4:55 pm in winter to 6:35 pm in summer. Allow the listed time plus rests.");
    return H->o.n;
  }
  HbDay td;
  if (hbDayInfo(H, &td)) {   // "when should I start tomorrow", "what time should I leave on Saturday"
    int need = dur_min;
    char lab[44], r[14], s[14], d[20], x[14];
    if (t >= 0) {
      HikeInfo inf;
      hbParseInfo(TRAILS[t].info, &inf);
      need = hbNeedMin(H->who, &inf);
      if (need >= 0) {
        hbFmtMin(need, d, sizeof(d));
        hbMsg(&H->o, "%s takes %s%s.", TRAILS[t].name, inf.tapprox ? "about " : "up to ", d);
      } else {
        hbMsg(&H->o, "I don't have a time for %s.", TRAILS[t].name);
      }
    } else if (need >= 0) {
      hbFmtMin(need, d, sizeof(d));
      hbMsg(&H->o, "For a %s walk:", d);
    }
    hbDayLabel(&td, lab, sizeof(lab));
    hbFmtClock(td.rise, r, sizeof(r));
    hbFmtClock(td.set, s, sizeof(s));
    hbMsg(&H->o, "%s: sunrise about %s, sunset about %s.", lab, r, s);
    if (need < 0) {
      hbFmtClock(td.set - 30, x, sizeof(x));
      hbMsg(&H->o, "Aim to be off the track by %s. Tell me the trail or how long you'll walk and I'll work out a start time.", x);
    } else {
      int latest = td.set - need - 30;
      if (latest <= td.rise - 25) {
        hbFmtClock(td.rise - 25, x, sizeof(x));
        hbMsg(&H->o, "That's a long day: even starting at first light (%s) you'd finish near or after dark. Carry a head torch.", x);
      } else {
        hbFmtClock(latest, x, sizeof(x));
        hbFmtClock(td.set - 30, r, sizeof(r));
        hbMsg(&H->o, "Latest start is %s, to be back by about %s (half an hour before sunset).", x, r);
      }
    }
    return H->o.n;
  }
  char tn[14];
  hbFmtClock(H->clk.mins, tn, sizeof(tn));
  int need = dur_min;
  if (t >= 0) {
    HikeInfo inf;
    hbParseInfo(TRAILS[t].info, &inf);
    need = hbNeedMin(H->who, &inf);
    char d[20];
    if (need >= 0) {
      hbFmtMin(need, d, sizeof(d));
      hbMsg(&H->o, "%s takes %s%s. It's %s now.", TRAILS[t].name, inf.tapprox ? "about " : "up to ", d, tn);
    } else {
      hbMsg(&H->o, "I don't have a time for %s. It's %s now.", TRAILS[t].name, tn);
    }
  } else if (need >= 0) {
    char d[20];
    hbFmtMin(need, d, sizeof(d));
    hbMsg(&H->o, "For a %s walk, starting now (%s):", d, tn);
  } else {
    hbMsg(&H->o, "It's %s now. Tell me the trail, or how long you'll walk, and I'll work out a finish time.", tn);
  }
  hbDaylightVerdict(H, H->clk.mins, need);
  return H->o.n;
}

// glow worms: they only show in full darkness, about half an hour after sunset
static HB_NOINLINE int hbDoGlow(HbCtx* H) {
  if (!H->clk.ok) return -1;
  HbDay td;
  if (hbDayInfo(H, &td)) {   // glow worms on another day
    char s[14], g[14], x[14], lab[44];
    hbFmtClock(td.set, s, sizeof(s));
    hbFmtClock(td.set + 35, g, sizeof(g));
    hbFmtClock(td.set + 25, x, sizeof(x));
    hbDayLabel(&td, lab, sizeof(lab));
    hbMsg(&H->o, "%s: sunset about %s, so glow worms show in full dark from about %s.", lab, s, g);
    hbMsg(&H->o, "Natural Bridge Circuit takes about 1 hr. Arrive about %s with a head torch, and check parks.qld.gov.au.", x);
    return H->o.n;
  }
  int from = H->clk.set + 35;
  char s[14], g[14], x[14];
  hbFmtClock(H->clk.set, s, sizeof(s));
  hbFmtClock(from, g, sizeof(g));
  int m = H->clk.mins;
  if (m < H->clk.set - 20) {
    hbFmtClock(from - 10, x, sizeof(x));
    hbMsg(&H->o, "Too early for glow worms: they show in full dark, from about %s. Sunset is about %s.", g, s);
    hbMsg(&H->o, "Natural Bridge Circuit takes about 1 hr. Arrive about %s with a head torch, and check parks.qld.gov.au.", x);
  } else if (m < from) {
    hbMsg(&H->o, "Nearly time. Sunset was about %s and glow worms show from about %s.", s, g);
    hbMsg(&H->o, "Take a head torch for the track, then use a dim or red light at the cave. No flash.");
  } else {
    hbMsg(&H->o, "It's dark, so glow worms should be showing. Use a dim or red light, no flash photos.");
    hbMsg(&H->o, "Stay on the track, don't touch the worms or the walls, go with a friend and allow about 1 hr.");
  }
  return H->o.n;
}

// ---------------------------------------------------------------------------------------------------------------
// hike timer

static HB_NOINLINE void hbTimerReminder(HbCtx* H) {
  static const char* const v[] = {
    "Tell someone your plan and carry a PLB. I can't raise an alarm if you're overdue.",
    "Leave your plan with someone and carry a PLB. I can't alert anyone if you don't finish.",
  };
  hbMsg(&H->o, "%s", v[(H->c->seed >> 3) & 1]);
}

static HB_NOINLINE void hbOffTrailReminder(HbCtx* H) {
  hbMsg(&H->o, "Off track is easy to get lost on: carry a map or GPS and a PLB. I can't raise an alarm if you're overdue.");
}

// t_in: a trail number, HT_OFFTRAIL, HT_UNKNOWN or HT_NONE. assumed: it is the person's last hike, not named this time.
static HB_NOINLINE int hbDoStart(HbCtx* H, int t_in, bool assumed) {
  if (!H->clk.ok) {
    hbMsg(&H->o, "My clock isn't set, so I can't time your hike. Note your start time yourself.");
    hbTimerReminder(H);
    return H->o.n;
  }
  HikePerson* p = H->who;
  if (p == &g_hike_temp) {   // every slot holds a running timer: this one could not be kept
    hbMsg(&H->o, "My timers are full, so I can't time your hike. Note your start time yourself.");
    hbTimerReminder(H);
    return H->o.n;
  }
  int t = t_in;
  char tn[14];
  hbFmtClock(H->clk.mins, tn, sizeof(tn));
  char prev[40] = "";
  if (p->t_start) {
    char pc[14];
    long ps = ((long)p->t_start + HIKE_TZ_SECS) % 86400L;
    hbFmtClock((int)(ps / 60), pc, sizeof(pc));
    snprintf(prev, sizeof(prev), " (restarted, was %s)", pc);
  }
  p->t_start = H->c->now_utc;
  p->t_trail = (int16_t)t;
  if (t != HT_NONE) p->last_trail = (int16_t)t;
  p->flags &= ~HP_AWAIT_DUR;
  bool guess = (assumed && t != HT_NONE) || t == HT_UNKNOWN;   // an assumed hike, or a name I don't know: a correction is welcome
  if (guess) p->flags |= HP_ASSUMED;
  else p->flags &= ~HP_ASSUMED;
  const char* same = (assumed && t != HT_NONE) ? " (same as last time)" : "";
  int need = -1;
  if (t >= 0) {
    HikeInfo inf;
    hbParseInfo(TRAILS[t].info, &inf);
    need = hbNeedMin(p, &inf);
    char d[20];
    if (need >= 0) {
      hbFmtMin(need, d, sizeof(d));
      hbMsg(&H->o, "Timer started %s on %s%s%s. Allow %s%s.", tn, TRAILS[t].name, prev, same, inf.tapprox ? "about " : "up to ", d);
    } else {
      hbMsg(&H->o, "Timer started %s on %s%s%s. I don't have a listed time for it.", tn, TRAILS[t].name, prev, same);
    }
  } else if (t == HT_OFFTRAIL) {
    hbMsg(&H->o, "Timer started %s%s for your off-trail hike%s. I have no listed time, so I'll just time it.", tn, prev, same);
  } else if (t == HT_UNKNOWN) {
    hbMsg(&H->o, "Timer started %s%s%s. I don't have that hike listed, so I'll just time it.", tn, prev, same);
  } else {
    hbMsg(&H->o, "Timer started %s%s. Which trail are you on? Tell me and I'll compare your time later.", tn, prev);
  }
  if (guess && H->o.n <= 1) hbMsg(&H->o, "Not right? Name the trail, or say off trail.");
  bool glow = hbHas(H->padded, "glow worm;glow worms;glowworm;glowworms");
  if (glow && (t < 0 || hbIsGlowTrail(t))) {
    hbDoGlow(H);
  } else {
    hbDaylightVerdict(H, H->clk.mins, need);
  }
  if (t == HT_OFFTRAIL) hbOffTrailReminder(H);
  else hbTimerReminder(H);
  return H->o.n;
}

static HB_NOINLINE void hbExplainElapsed(HbCtx* H, int elapsed, int t, bool from_clock) {
  char e[20];
  hbFmtMin(elapsed, e, sizeof(e));
  HikePerson* p = H->who;
  static const char* const praise[] = {"Great work", "Nice one", "Well done"};
  const char* pr = praise[(H->c->seed >> 5) % 3];
  if (t == HT_OFFTRAIL) {
    hbMsg(&H->o, "%s, that took %s on your off-trail hike%s. I have no listed time to compare it with.", pr, e,
          from_clock ? " (breaks included)" : "");
    return;
  }
  if (t == HT_UNKNOWN) {
    hbMsg(&H->o, "%s, that took %s%s. I don't have that hike listed, so there's no time to compare it with.", pr, e,
          from_clock ? " (breaks included)" : "");
    return;
  }
  if (t < 0) {
    hbMsg(&H->o, "%s, that took %s%s. Tell me which trail it was and I'll compare it with the listed time.", pr, e,
          from_clock ? " (breaks included)" : "");
    return;
  }
  HikeInfo inf;
  hbParseInfo(TRAILS[t].info, &inf);
  char cmp[80] = "";
  if (inf.tlo >= 0) {
    char lt[20];
    if (elapsed < inf.tlo * 95 / 100) {
      hbFmtMin(inf.tlo - elapsed, lt, sizeof(lt));
      snprintf(cmp, sizeof(cmp), "about %s quicker than the %s listed", lt, inf.ft);
    } else if (elapsed > inf.thi * 105 / 100) {
      hbFmtMin(elapsed - inf.thi, lt, sizeof(lt));
      snprintf(cmp, sizeof(cmp), "about %s longer than the %s listed", lt, inf.ft);
    } else {
      snprintf(cmp, sizeof(cmp), "in line with the %s listed", inf.ft);
    }
  }
  if (cmp[0]) hbMsg(&H->o, "%s, that took %s on %s: %s.", pr, e, TRAILS[t].name, cmp);
  else hbMsg(&H->o, "%s, that took %s on %s.", pr, e, TRAILS[t].name);
  // speed
  if (inf.dist100 >= 50 && elapsed >= 5) {
    int kmh100 = inf.dist100 * 60 / elapsed;
    char dk[14];
    hbFmtKm(inf.dist100, dk, sizeof(dk));
    hbMsg(&H->o, "That's about %d.%d km/h over the listed %s%s.", kmh100 / 100, (kmh100 % 100) / 10, dk,
          from_clock ? ", breaks included" : "");
  }
  // learn the pace
  if (inf.thi > 0 && elapsed >= 5 && elapsed <= 16 * 60) {
    int mid = (inf.tlo + inf.thi) / 2;
    if (mid > 0) {
      int pct = elapsed * 100 / (inf.thi);   // against the upper listed time, which is what is predicted from
      if (pct > 400) pct = 400;
      if (pct < 20) pct = 20;
      p->pace_pct = p->pace_pct ? (uint16_t)((p->pace_pct + pct) / 2) : (uint16_t)pct;
      if (p->pace_n < 255) p->pace_n++;
    }
  }
}

// t_in: the hike named in this message (a trail number, HT_OFFTRAIL, HT_UNKNOWN) or HT_NONE.
// A running timer is finished as the hike it was started on, never as "the last trail they talked about".
static HB_NOINLINE int hbDoFinish(HbCtx* H, int t_in) {
  HikePerson* p = H->who;
  int dur = hbParseDuration(H->norm);
  if (p->t_start && H->clk.ok && dur < 0) {
    long el = (long)H->c->now_utc - (long)p->t_start;
    int elapsed = el < 0 ? 0 : (int)(el / 60);
    int t = t_in != HT_NONE ? t_in : p->t_trail;
    p->flags &= ~(HP_ASSUMED | HP_OFFTRAIL);
    if (elapsed > 16 * 60) {
      char e[20];
      hbFmtMin(elapsed, e, sizeof(e));
      hbMsg(&H->o, "That timer has run %s, so it was probably left on. Say I am starting now next time.", e);
      p->t_start = 0;
      p->t_trail = HT_NONE;
      return H->o.n;
    }
    p->t_start = 0;
    p->t_trail = HT_NONE;
    if (t != HT_NONE) p->last_trail = (int16_t)t;
    hbExplainElapsed(H, elapsed, t, true);
    return H->o.n;
  }
  if (dur >= 0 && dur <= 16 * 60) {   // they told us how long they were out
    int t = t_in != HT_NONE ? t_in : (p->t_trail != HT_NONE ? p->t_trail : p->last_trail);
    p->t_start = 0;
    p->t_trail = HT_NONE;
    p->flags &= ~(HP_AWAIT_DUR | HP_ASSUMED | HP_OFFTRAIL);
    if (t != HT_NONE) p->last_trail = (int16_t)t;
    hbExplainElapsed(H, dur, t, false);
    return H->o.n;
  }
  p->flags |= HP_AWAIT_DUR;
  hbMsg(&H->o, "I don't have a start time for you. How long were you out? Say something like 2 hours.");
  return H->o.n;
}

// ---------------------------------------------------------------------------------------------------------------
// moon and meteor showers (worked out from the clock, for South East Queensland; times are approximate)

#define HB_RAD 0.017453292519943295

static double hbRev(double x) {   // an angle in degrees, brought into 0 to 360
  long k = (long)(x / 360.0);
  x -= (double)k * 360.0;
  if (x < 0) x += 360.0;
  return x;
}
static float hbSin(double deg) { return sinf((float)(hbRev(deg) * HB_RAD)); }
static float hbCos(double deg) { return cosf((float)(hbRev(deg) * HB_RAD)); }

struct HbMoon {
  float elong;      // how far the moon is ahead of the sun, 0 to 360 degrees (0 new, 180 full)
  float ra, dec;    // the moon's position in degrees
};

// the moon's position and phase at a UTC time (low-precision lunar theory: about 0.1 degree, plenty for phase and
// rise/set to within a few minutes)
static HB_NOINLINE void hbMoonAt(long utc, HbMoon* m) {
  double d = (double)(utc - 946598400L) / 86400.0;   // days since 1999-12-31 00:00 UTC
  double N = 125.1228 - 0.0529538083 * d;
  double w = 318.0634 + 0.1643573223 * d;
  double Mm = 115.3654 + 13.0649929509 * d;
  double ws = 282.9404 + 4.70935e-5 * d;
  double Ms = 356.0470 + 0.9856002585 * d;
  const double e = 0.0549;
  // eccentric anomaly
  double E = Mm + (e / HB_RAD) * hbSin(Mm) * (1.0 + e * hbCos(Mm));
  for (int i = 0; i < 2; i++) E = E - (E - (e / HB_RAD) * hbSin(E) - Mm) / (1.0 - e * hbCos(E));
  double xv = 60.2666 * (hbCos(E) - e);
  double yv = 60.2666 * sqrtf(1.0f - (float)(e * e)) * hbSin(E);
  double v = atan2f((float)yv, (float)xv) / HB_RAD;
  double r = sqrtf((float)(xv * xv + yv * yv));
  double vw = v + w;
  double ci = 0.99597, si = 0.08968;   // cos and sin of the 5.1454 degree inclination
  double xh = r * (hbCos(N) * hbCos(vw) - hbSin(N) * hbSin(vw) * ci);
  double yh = r * (hbSin(N) * hbCos(vw) + hbCos(N) * hbSin(vw) * ci);
  double zh = r * hbSin(vw) * si;
  double lon = atan2f((float)yh, (float)xh) / HB_RAD;
  double lat = atan2f((float)zh, sqrtf((float)(xh * xh + yh * yh))) / HB_RAD;
  double Ls = ws + Ms, Lm = N + w + Mm;
  double D = Lm - Ls, F = Lm - N;
  lon += -1.274 * hbSin(Mm - 2 * D) + 0.658 * hbSin(2 * D) - 0.186 * hbSin(Ms) - 0.059 * hbSin(2 * Mm - 2 * D) -
         0.057 * hbSin(Mm - 2 * D + Ms) + 0.053 * hbSin(Mm + 2 * D) + 0.046 * hbSin(2 * D - Ms) + 0.041 * hbSin(Mm - Ms) -
         0.035 * hbSin(D) - 0.031 * hbSin(Mm + Ms) - 0.015 * hbSin(2 * F - 2 * D) + 0.011 * hbSin(Mm - 4 * D);
  lat += -0.173 * hbSin(F - 2 * D) - 0.055 * hbSin(Mm - F - 2 * D) - 0.046 * hbSin(Mm + F - 2 * D) + 0.033 * hbSin(F + 2 * D) +
         0.017 * hbSin(2 * Mm + F);
  double sun = Ls + 1.915 * hbSin(Ms) + 0.020 * hbSin(2 * Ms);   // the sun's longitude
  m->elong = (float)hbRev(lon - sun);
  // ecliptic to equatorial
  double ecl = 23.4393 - 3.563e-7 * d;
  double x = hbCos(lon) * hbCos(lat), y = hbSin(lon) * hbCos(lat), z = hbSin(lat);
  double ye = y * hbCos(ecl) - z * hbSin(ecl), ze = y * hbSin(ecl) + z * hbCos(ecl);
  m->ra = (float)hbRev(atan2f((float)ye, (float)x) / HB_RAD);
  m->dec = asinf((float)ze) / (float)HB_RAD;
}

static int hbMoonLitPct(float elong) {   // percent of the moon's face that is lit
  return (int)((1.0f - hbCos(elong)) * 50.0f + 0.5f);
}

static const char* hbMoonPhaseName(float e) {
  if (e < 12 || e >= 348) return "new moon";
  if (e < 84) return "waxing crescent";
  if (e <= 96) return "first quarter";
  if (e < 168) return "waxing gibbous";
  if (e <= 192) return "full moon";
  if (e < 264) return "waning gibbous";
  if (e <= 276) return "last quarter";
  return "waning crescent";
}

// the moon's height above the horizon (degrees) from the middle of SE Qld
#define HB_LAT (-27.6)
#define HB_LON (153.0)
static float hbMoonAlt(long utc) {
  HbMoon m;
  hbMoonAt(utc, &m);
  double gmst = 280.46061837 + 360.98564736629 * ((double)(utc - 946728000L) / 86400.0);
  double H = gmst + HB_LON - m.ra;
  float sa = hbSin(HB_LAT) * hbSin(m.dec) + hbCos(HB_LAT) * hbCos(m.dec) * hbCos(H);
  if (sa > 1) sa = 1;
  if (sa < -1) sa = -1;
  return asinf(sa) / (float)HB_RAD;
}

// local minutes since midnight of the moon's rise and set on the local day starting at midnight_utc (-1 = none that day)
static HB_NOINLINE void hbMoonRiseSet(long midnight_utc, int* rise, int* set) {
  *rise = *set = -1;
  const float h0 = 0.125f;   // the moon's centre is on the horizon when it is a little above it (parallax and refraction)
  float prev = hbMoonAlt(midnight_utc) - h0;
  for (int k = 1; k <= 72; k++) {
    long t = midnight_utc + (long)k * 1200L;
    float f = hbMoonAlt(t) - h0;
    if ((prev < 0) != (f < 0)) {
      float frac = prev / (prev - f);
      int mins = (int)(((long)(k - 1) * 1200L + (long)(frac * 1200.0f) + 30L) / 60L);
      if (mins > 1439) mins = 1439;
      if (prev < 0 && *rise < 0) *rise = mins;
      if (prev >= 0 && *set < 0) *set = mins;
    }
    prev = f;
  }
}

// the next time (UTC) the moon's elongation is `target` degrees (180 full, 0 new); a few hours in the past still counts
static HB_NOINLINE long hbMoonNext(long utc, float target) {
  long t = utc;
  for (int i = 0; i < 6; i++) {
    HbMoon m;
    hbMoonAt(t, &m);
    float diff = (float)hbRev((double)target - (double)m.elong);
    if (diff > 350.0f) diff -= 360.0f;
    t += (long)((double)diff / 12.19 * 86400.0);
  }
  return t;
}

// "Fri 30 Oct" for a UTC time, and how many local days away it is
static void hbFmtDay(long utc, long today_ld, char* out, int osz, int* days_away) {
  long ld = (utc + HIKE_TZ_SECS) / 86400L;
  int y, mo, dd;
  hbCivil(ld, &y, &mo, &dd);
  int wd = (int)((ld + 4) % 7);
  snprintf(out, osz, "%.3s %d %.3s", HB_DAYS[wd], dd, HB_MONTHS[mo - 1]);
  if (days_away) *days_away = (int)(ld - today_ld);
}

static HB_NOINLINE int hbDoMoon(HbCtx* H) {
  if (!H->clk.ok) {
    hbMsg(&H->o, "My clock isn't set, so I can't work out the moon.");
    return H->o.n;
  }
  long now = (long)H->c->now_utc;
  long today_ld = (now + HIKE_TZ_SECS) / 86400L;
  HbDay td;
  bool fut = hbDayInfo(H, &td);   // "moon tomorrow", "moonrise on Friday"
  if (fut) { today_ld = td.ld; now = td.ld * 86400L - HIKE_TZ_SECS + 20L * 3600L; }   // the evening of that day
  long midnight = today_ld * 86400L - HIKE_TZ_SECS;
  const char* when = fut ? "that day" : "today";
  HbMoon m;
  hbMoonAt(now, &m);
  if (fut) {
    char lab[44];
    hbDayLabel(&td, lab, sizeof(lab));
    hbMsg(&H->o, "%s: moon is %s, about %d%% lit.", lab, hbMoonPhaseName(m.elong), hbMoonLitPct(m.elong));
  } else {
    hbMsg(&H->o, "Moon: %s, about %d%% lit.", hbMoonPhaseName(m.elong), hbMoonLitPct(m.elong));
  }

  int rise, set;
  hbMoonRiseSet(midnight, &rise, &set);
  char rs[14], ss[14];
  char tl[100];
  if (rise >= 0) hbFmtClock(rise, rs, sizeof(rs));
  if (set >= 0) hbFmtClock(set, ss, sizeof(ss));
  if (rise >= 0 && set >= 0) {
    if (set < rise) snprintf(tl, sizeof(tl), "Moonset %s, moonrise %s %s.", ss, rs, when);
    else snprintf(tl, sizeof(tl), "Moonrise %s, moonset %s %s.", rs, ss, when);
  } else if (rise >= 0) {
    snprintf(tl, sizeof(tl), "Moonrise %s %s, no moonset until the next day.", rs, when);
  } else if (set >= 0) {
    snprintf(tl, sizeof(tl), "Moonset %s %s, no moonrise until the next day.", ss, when);
  } else {
    snprintf(tl, sizeof(tl), "No moonrise or moonset %s.", when);
  }
  long tf = hbMoonNext(now, 180.0f), tn = hbMoonNext(now, 0.0f);
  if (tn < now - 3600L * 12) tn = hbMoonNext(now + 86400L * 3, 0.0f);   // never report a new moon from yesterday
  char fs[20], ns[20];
  hbFmtDay(tf, today_ld, fs, sizeof(fs), NULL);
  hbFmtDay(tn, today_ld, ns, sizeof(ns), NULL);
  hbMsg(&H->o, "%s Full moon %s, new moon %s.", tl, fs, ns);
  return H->o.n;
}

struct HbShower {
  uint8_t mon, day;   // usual peak date (it moves by a day or so from year to year)
  uint8_t zhr;        // meteors an hour under perfect dark skies; far fewer are seen in practice
  const char* name;
  const char* note;
};

// showers worth watching from SE Qld
static const HbShower HB_SHOWERS[] = {
  {2, 8, 6, "Alpha Centaurids", "A few bright fireballs; best before dawn."},
  {4, 22, 18, "Lyrids", "The radiant is low in the north-east before dawn."},
  {5, 6, 50, "Eta Aquariids", "One of the best from here; watch in the 2 hours before dawn."},
  {7, 30, 25, "Southern Delta Aquariids", "Best after midnight."},
  {8, 12, 100, "Perseids", "The radiant stays very low from SE Qld, so you'll see far fewer."},
  {10, 21, 20, "Orionids", "Best after midnight to dawn."},
  {11, 5, 5, "Southern Taurids", "Few meteors, but some bright fireballs."},
  {11, 17, 15, "Leonids", "Best after midnight to dawn."},
  {12, 14, 120, "Geminids", "The best of the year: good from about 9 pm, best near midnight to 2 am."},
};
#define HB_NSHOWERS ((int)(sizeof(HB_SHOWERS) / sizeof(HB_SHOWERS[0])))

static int hbDayOfYear(int mon, int day) {   // 1 to 365 (leap days ignored)
  static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return cum[mon - 1] + day;
}

static HB_NOINLINE int hbDoMeteors(HbCtx* H) {
  if (!H->clk.ok) {
    hbMsg(&H->o, "My clock isn't set, so I can't tell which meteor shower is next.");
    return H->o.n;
  }
  long now = (long)H->c->now_utc;
  long today_ld = (now + HIKE_TZ_SECS) / 86400L;
  int y = H->clk.year, mo = H->clk.mon, dd = H->clk.day;
  (void)y;
  int today = hbDayOfYear(mo, dd);
  // days from today to each shower's peak (wrapping past new year); a peak up to 2 days ago still counts as "now"
  int best = -1, best_delta = 9999;
  int delta[HB_NSHOWERS];
  for (int i = 0; i < HB_NSHOWERS; i++) {
    int dlt = hbDayOfYear(HB_SHOWERS[i].mon, HB_SHOWERS[i].day) - today;
    if (dlt < -2) dlt += 365;
    delta[i] = dlt;
    if (dlt < best_delta) { best_delta = dlt; best = i; }
  }
  const HbShower& sh = HB_SHOWERS[best];
  char when[24];
  snprintf(when, sizeof(when), "%d %.3s", sh.day, HB_MONTHS[sh.mon - 1]);
  if (best_delta <= 0 && best_delta >= -2) {
    hbMsg(&H->o, "The %s are at their peak around now (usual peak %s). Up to about %d an hour under perfect dark skies.", sh.name, when, sh.zhr);
  } else if (best_delta == 1) {
    hbMsg(&H->o, "Next meteor shower: the %s, peaking about tomorrow (%s). Up to about %d an hour under perfect dark skies.", sh.name, when, sh.zhr);
  } else {
    hbMsg(&H->o, "Next meteor shower: the %s, peaking about %s (in %d days). Up to about %d an hour under perfect dark skies.", sh.name,
          when, best_delta, sh.zhr);
  }
  // the moon that night (11 pm)
  long peak_ld = today_ld + (best_delta < 0 ? 0 : best_delta);
  long night = peak_ld * 86400L + 23L * 3600L - HIKE_TZ_SECS;
  HbMoon m;
  hbMoonAt(night, &m);
  int lit = hbMoonLitPct(m.elong);
  const char* mv = lit < 15 ? "dark skies, ideal" : (lit < 50 ? "some moonlight, face away from it" : "a bright moon will hide the fainter ones");
  hbMsg(&H->o, "Moon that night: about %d%% lit (%s). %s Get away from town lights.", lit, mv, sh.note);
  // the one after
  int nxt = -1, nxt_delta = 9999;
  for (int i = 0; i < HB_NSHOWERS; i++) {
    if (i == best) continue;
    if (delta[i] >= 0 && delta[i] < nxt_delta && delta[i] > best_delta) { nxt_delta = delta[i]; nxt = i; }
  }
  if (nxt < 0) {   // wrapped: the first one of the new year
    for (int i = 0; i < HB_NSHOWERS; i++) {
      if (i != best && (nxt < 0 || delta[i] < nxt_delta)) { nxt = i; nxt_delta = delta[i]; }
    }
  }
  if (nxt >= 0) hbMsg(&H->o, "After that: the %s, about %d %.3s.", HB_SHOWERS[nxt].name, HB_SHOWERS[nxt].day, HB_MONTHS[HB_SHOWERS[nxt].mon - 1]);
  return H->o.n;
}

// ---------------------------------------------------------------------------------------------------------------
// what to take

#define GB_SWIM   0x001
#define GB_NAV    0x002
#define GB_BIG    0x004
#define GB_SCRAM  0x008
#define GB_CREEK  0x010
#define GB_HOT    0x020
#define GB_COLD   0x040
#define GB_RAIN   0x080
#define GB_KIDS   0x100
#define GB_NIGHT  0x200

static HB_NOINLINE int hbDoGear(HbCtx* H, int t) {
  uint16_t b = 0;
  if (t >= 0) {
    const char* inf = TRAILS[t].info;
    HikeInfo hi;
    hbParseInfo(inf, &hi);
    if (hbInfoHas(inf, "swim") || hbInfoHas(inf, "pool") || hbInfoHas(inf, "waterfall") || hbInfoHas(inf, "beach") ||
        hbInfoHas(inf, "lagoon") || hbInfoHas(inf, "cascade"))
      b |= GB_SWIM;
    if (hi.offtrack || hbInfoHas(inf, "hard navigation") || hbInfoHas(inf, "easy to lose")) b |= GB_NAV;
    if (hi.dist100 >= 1000 || hi.thi >= 240 || hi.grade >= 4) b |= GB_BIG;
    if (hbInfoHas(inf, "scrambl") || hbInfoHas(inf, "rock scramble")) b |= GB_SCRAM;
    if (hbInfoHas(inf, "creek")) b |= GB_CREEK;
  }
  if (hbHas(H->padded, "hot;summer;warm;heat;sunny")) b |= GB_HOT;
  if (hbHas(H->padded, "cold;winter;chilly;freezing;frost;misty;mist")) b |= GB_COLD;
  if (hbHas(H->padded, "rain;raining;wet;drizzle;storm;showers")) b |= GB_RAIN;
  if (hbHas(H->padded, "kids;kid;children;child;toddler;toddlers;baby;family")) b |= GB_KIDS;
  if (hbHas(H->padded, "night;after dark;evening;glow worm;glow worms;glowworm;glowworms;dusk")) b |= GB_NIGHT;
  if (hbHas(H->padded, "off track;off trail;offtrack;bushbash;bush bash;cross country")) b |= GB_NAV;

  if (hbHas(H->padded, "overnight;camp;camping;multi day;multiday;two day;2 day;tent")) {
    hbMsg(&H->o, "This is for day walks. Overnight needs shelter, sleeping gear, stove, and extra food and water.");
    hbMsg(&H->o, "Plan with parks.qld.gov.au, book campsites, tell someone your route, and carry a PLB.");
    return H->o.n;
  }

  hbParts(&H->o, HIKE_GEAR_BASE);
  const char* list[10];
  int nl = 0;
  if (b & GB_BIG) list[nl++] = "3 L or more of water, extra food, a warm layer";
  if (b & GB_NAV) list[nl++] = "map, compass, long pants, gloves";
  if (b & GB_SCRAM) list[nl++] = "sturdy boots and gloves for the scramble";
  if (b & GB_KIDS) list[nl++] = "snacks, sun hats, a carrier for little ones";
  if (b & GB_HOT) list[nl++] = (b & GB_BIG) ? "electrolytes, a wide hat" : "extra water, electrolytes, a wide hat";
  if (b & GB_COLD) list[nl++] = "warm layer, beanie, gloves";
  if (b & GB_RAIN) list[nl++] = "rain jacket, grippy shoes";
  if (b & GB_NIGHT) list[nl++] = "spare head torch and batteries, a red light";
  if (b & GB_SWIM) list[nl++] = "swimwear and a towel";
  if (b & GB_CREEK) list[nl++] = "check recent rain first";
  if (nl > 0) {
    static char cur[BOT_MSG_MAX + 1];
    snprintf(cur, sizeof(cur), "%s", t >= 0 ? "Extras for this walk: " : "Extras: ");
    bool first = true;
    for (int i = 0; i < nl; i++) {
      int need = (int)strlen(cur) + (first ? 0 : 2) + (int)strlen(list[i]) + 1;
      if (need > H->o.budget) {
        if (H->o.n + 1 >= BOT_Q_MAX && !first) break;   // no room for another message: the rest is dropped
        if (!first) {
          hbMsg(&H->o, "%s.", cur);
          snprintf(cur, sizeof(cur), "Also: ");
          first = true;
        }
      }
      if (!first) strcat(cur, "; ");
      strncat(cur, list[i], sizeof(cur) - strlen(cur) - 1);
      first = false;
    }
    hbMsg(&H->o, "%s.", cur);
  } else if (t < 0) {
    hbMsg(&H->o, "Tell me the trail, or the conditions (hot, cold, rain, kids), and I'll add extras.");
  }
  if ((b & GB_NAV) && H->o.n < BOT_Q_MAX) hbMsg(&H->o, "Ask me about off-track walking for more tips.");
  return H->o.n;
}

// ---------------------------------------------------------------------------------------------------------------
// trail facts, compare, suggestions

static const char* hbGradeWords(int g) {
  switch (g) {
    case 1: return "flat, even and easy";
    case 2: return "gentle, with a few steps";
    case 3: return "hills, steps and uneven ground";
    case 4: return "steep and rough, sometimes unmarked";
    case 5: return "very rough, experts only";
  }
  return "";
}

static HB_NOINLINE int hbDoTrailInfo(HbCtx* H, int t) {
  hbMsg(&H->o, "%s: %s", TRAILS[t].name, TRAILS[t].info);
  if (!(H->who->flags & HP_HINT_SEEN)) {
    H->who->flags |= HP_HINT_SEEN;
    hbMsg(&H->o, "Ask me what to take, how long it takes you, or when to start. Say I am starting now to time it.");
  }
  return H->o.n;
}

static HB_NOINLINE int hbDoFacet(HbCtx* H, int t, bool want_time, bool want_dist, bool want_grade, bool want_kids, bool want_feat) {
  HikeInfo inf;
  hbParseInfo(TRAILS[t].info, &inf);
  const char* nm = TRAILS[t].name;
  char d[20];
  if (want_kids) {
    bool easy = inf.grade > 0 && inf.grade <= 2 && (inf.dist100 < 0 || inf.dist100 <= 400);
    if (easy) hbMsg(&H->o, "%s is Gr %d (%s), %s. A good choice for kids; take water and snacks.", nm, inf.grade,
                    hbGradeWords(inf.grade), inf.f0[0] ? inf.f0 : "short");
    else if (inf.grade == 0) hbMsg(&H->o, "%s has no grade listed: %s. Check it's suitable before you take kids.", nm, TRAILS[t].info);
    else hbMsg(&H->o, "%s is Gr %d (%s)%s%s. Better for older kids; keep them close.", nm, inf.grade, hbGradeWords(inf.grade),
               inf.f0[0] ? ", " : "", inf.f0);
    return H->o.n;
  }
  if (want_grade) {
    if (inf.grade > 0) hbMsg(&H->o, "%s is Gr %d: %s.", nm, inf.grade, hbGradeWords(inf.grade));
    else hbMsg(&H->o, "No grade is listed for %s. %s", nm, TRAILS[t].info);
  }
  if (want_time || want_dist) {
    char me[48] = "";
    if (want_time && H->who->pace_pct && inf.thi > 0) {
      hbFmtMin(inf.thi * H->who->pace_pct / 100, d, sizeof(d));
      snprintf(me, sizeof(me), " At your pace, about %s.", d);
    }
    if (want_time && want_dist && inf.tlo >= 0 && inf.dist100 >= 0) {
      hbMsg(&H->o, "%s is %s and takes %s%s.%s", nm, inf.f0, inf.tapprox ? "about " : "", inf.ft, me);
    } else if (want_time && inf.tlo >= 0) {
      hbMsg(&H->o, "%s takes %s%s%s%s.%s", nm, inf.tapprox ? "about " : "", inf.ft, inf.f0[0] ? " for " : "", inf.f0, me);
    } else if (want_time) {
      hbMsg(&H->o, "No time is listed for %s%s%s.", nm, inf.f0[0] ? ": " : "", inf.f0);
    } else if (inf.dist100 >= 0) {
      hbMsg(&H->o, "%s is %s%s%s.", nm, inf.f0, inf.ft[0] ? ", about " : "", inf.ft);
    } else {
      hbMsg(&H->o, "No distance is listed for %s: %s", nm, TRAILS[t].info);
    }
  }
  if (want_feat) {
    if (inf.feat[0]) hbMsg(&H->o, "%s has: %s.", nm, inf.feat);
    else hbMsg(&H->o, "%s: %s", nm, TRAILS[t].info);
  }
  return H->o.n;
}

static HB_NOINLINE int hbDoCompare(HbCtx* H, int a, int b) {
  HikeInfo ia, ib;
  hbParseInfo(TRAILS[a].info, &ia);
  hbParseInfo(TRAILS[b].info, &ib);
  bool fh = hbHas(H->padded, "harder;easier;hardest;easiest;difficult;steeper;steepest;tougher;grade");
  bool fl = hbHas(H->padded, "longer;shorter;longest;shortest;further;farther;distance");
  bool fq = hbHas(H->padded, "quicker;faster;slower;quickest;fastest;time;takes longer;less time;more time");
  if (!fh && !fl && !fq) fh = fl = fq = true;
  static char sen[3][110];
  int ns = 0;
  if (fh) {
    if (ia.grade && ib.grade && ia.grade != ib.grade) {
      bool aH = ia.grade > ib.grade;
      snprintf(sen[ns++], sizeof(sen[0]), "%s is harder (Gr %d v %d).", aH ? TRAILS[a].name : TRAILS[b].name, aH ? ia.grade : ib.grade, aH ? ib.grade : ia.grade);
    } else if (ia.grade && ib.grade) snprintf(sen[ns++], sizeof(sen[0]), "Both are Gr %d.", ia.grade);
    else snprintf(sen[ns++], sizeof(sen[0]), "I can't compare grades: one isn't graded.");
  }
  if (fl) {
    if (ia.dist100 >= 0 && ib.dist100 >= 0 && ia.dist100 != ib.dist100) {
      char x[14], y[14];
      bool aL = ia.dist100 > ib.dist100;
      hbFmtKm(aL ? ia.dist100 : ib.dist100, x, sizeof(x));
      hbFmtKm(aL ? ib.dist100 : ia.dist100, y, sizeof(y));
      snprintf(sen[ns++], sizeof(sen[0]), "%s is longer (%s v %s).", aL ? TRAILS[a].name : TRAILS[b].name, x, y);
    } else if (ia.dist100 >= 0 && ib.dist100 >= 0) snprintf(sen[ns++], sizeof(sen[0]), "Same distance.");
    else snprintf(sen[ns++], sizeof(sen[0]), "A distance is missing for one of them.");
  }
  if (fq) {
    int ma = ia.thi < 0 ? -1 : (ia.tlo + ia.thi) / 2, mb = ib.thi < 0 ? -1 : (ib.tlo + ib.thi) / 2;
    if (ma >= 0 && mb >= 0 && ma != mb)
      snprintf(sen[ns++], sizeof(sen[0]), "%s takes longer (%s v %s).", ma > mb ? TRAILS[a].name : TRAILS[b].name, ma > mb ? ia.ft : ib.ft,
               ma > mb ? ib.ft : ia.ft);
    else if (ma >= 0 && mb >= 0) snprintf(sen[ns++], sizeof(sen[0]), "Similar times.");
  }
  static char cur[BOT_MSG_MAX + 1];
  cur[0] = 0;
  for (int i = 0; i < ns; i++) {
    if (cur[0] && (int)strlen(cur) + 1 + (int)strlen(sen[i]) > H->o.budget) { hbMsg(&H->o, "%s", cur); cur[0] = 0; }
    if (cur[0]) strcat(cur, " ");
    strncat(cur, sen[i], sizeof(cur) - strlen(cur) - 1);
  }
  if (cur[0]) hbMsg(&H->o, "%s", cur);
  if (H->o.n + 2 <= BOT_Q_MAX) {
    hbMsg(&H->o, "%s: %s", TRAILS[a].name, TRAILS[a].info);
    hbMsg(&H->o, "%s: %s", TRAILS[b].name, TRAILS[b].info);
  }
  return H->o.n;
}

// feature bits for walk searches
struct HbFeat { const char* keys; const char* match; };   // match: '|' separated words looked for in the info text
static const HbFeat HB_FEATS[] = {
  {"waterfall;waterfalls;falls;cascade;cascades", "fall|cascade"},
  {"swim;swimming;rock pool;rock pools;rockpool;pool;pools;waterhole;lagoon;lagoons", "swim|pool|lagoon"},
  {"lookout;lookouts;views;view;summit;scenic;panorama;panoramic", "view|lookout|summit"},
  {"rainforest;jungle", "rainforest"},
  {"beach;beaches;coast;coastal;ocean;headland;dunes", "beach|coast|headland"},
  {"cave;caves;glow worm;glow worms", "cave"},
  {"boardwalk;accessible;paved;sealed;wheelchair;pram;stroller", "boardwalk|accessible|sealed"},
  {"birds;birdlife;birdwatching;bird watching", "bird"},
  {"gorge;gorges", "gorge"},
  {"creek;creeks", "creek"},
  {"off track;off trail;offtrack", "off-track"},
};
#define HB_FEAT_COUNT ((int)(sizeof(HB_FEATS) / sizeof(HB_FEATS[0])))

static bool hbInfoAny(const char* info, const char* alts) {   // any of the '|' words in info
  const char* p = alts;
  while (*p) {
    const char* e = strchr(p, '|');
    int len = e ? (int)(e - p) : (int)strlen(p);
    char w[24];
    if (len > 23) len = 23;
    memcpy(w, p, len);
    w[len] = 0;
    if (strstr(info, w)) return true;
    p = e ? e + 1 : p + len;
  }
  return false;
}

// how well a trail fits the filters: -1 = no, else a score (higher is better)
static int hbFit(const HikePerson* p, const Trail* tr, bool strict) {
  HikeInfo inf;
  hbParseInfo(tr->info, &inf);
  if (p->sg_town >= 0 && tr->town != p->sg_town) return -1;
  if (p->sg_region >= 0 && TRAIL_TOWNS[tr->town].region != p->sg_region) return -1;
  int score = 10;
  for (int i = 0; i < HB_FEAT_COUNT; i++) {
    if (p->sg_feat & (1u << i)) {
      if (!hbInfoAny(tr->info, HB_FEATS[i].match)) return -1;
      score += 3;
    }
  }
  if (p->sg_ghi) {
    if (inf.grade == 0) { if (strict) return -1; }
    else if (inf.grade < p->sg_glo || inf.grade > p->sg_ghi) { if (strict) return -1; score -= 2 * (inf.grade < p->sg_glo ? p->sg_glo - inf.grade : inf.grade - p->sg_ghi); }
  }
  int tm = inf.thi < 0 ? -1 : inf.thi;
  if (p->sg_tmax) {
    if (tm < 0) { if (strict) return -1; }
    else if (tm > p->sg_tmax * 10) { if (strict) return -1; score -= 2; }
  }
  if (p->sg_tmin) {
    if (tm < 0) { if (strict) return -1; }
    else if (tm < p->sg_tmin * 10) { if (strict) return -1; score -= 2; }
  }
  return score;
}

static void hbFmtItem(const Trail* tr, char* out, int osz) {
  HikeInfo inf;
  hbParseInfo(tr->info, &inf);
  char g[8] = "";
  if (inf.grade) snprintf(g, sizeof(g), "Gr %d", inf.grade);
  const char* parts[3] = {inf.f0, inf.ft, g};
  char det[64];
  int n = 0;
  det[0] = 0;
  for (int i = 0; i < 3; i++) if (parts[i][0]) n += snprintf(det + n, sizeof(det) - n, "%s%s", n ? ", " : "", parts[i]);
  if (n) snprintf(out, osz, "%s (%s)", tr->name, det);
  else snprintf(out, osz, "%s", tr->name);
}

static HB_NOINLINE int hbSuggestList(HbCtx* H, bool more) {
  HikePerson* p = H->who;
  static int idx[TRAIL_COUNT];
  static int sc[TRAIL_COUNT];
  int cnt = 0;
  bool relaxed = false;
  for (int pass = 0; pass < 2 && cnt == 0; pass++) {
    for (int i = 0; i < TRAIL_COUNT; i++) {
      int s = hbFit(p, &TRAILS[i], pass == 0);
      if (s >= 0) { idx[cnt] = i; sc[cnt] = s; cnt++; }
    }
    if (cnt == 0) relaxed = true;
  }
  if (cnt == 0) {
    hbMsg(&H->o, "I couldn't find a walk like that. Try fewer requirements, or browse with !trails.");
    return H->o.n;
  }
  // best score first, library order for ties
  for (int i = 1; i < cnt; i++) {
    int ti = idx[i], ts = sc[i], j = i - 1;
    while (j >= 0 && sc[j] < ts) { idx[j + 1] = idx[j]; sc[j + 1] = sc[j]; j--; }
    idx[j + 1] = ti; sc[j + 1] = ts;
  }
  int off = p->sg_off;
  if (more) {
    if (off >= cnt) {
      hbMsg(&H->o, "That's all I have for that. Try different words, or browse with !trails.");
      return H->o.n;
    }
  } else {
    off = 0;
  }
  int shown = 0;
  static char cur[BOT_MSG_MAX + 1];
  cur[0] = 0;
  const char* head = relaxed ? "Closest matches: " : "Try: ";
  snprintf(cur, sizeof(cur), "%s", head);
  bool first_in_msg = true;
  int msgs_started = 1;
  for (int k = off; k < cnt && shown < 5; k++) {
    static char item[100];
    hbFmtItem(&TRAILS[idx[k]], item, sizeof(item));
    int need = (int)strlen(cur) + (first_in_msg ? 0 : 2) + (int)strlen(item);
    if (need > H->o.budget) {
      if (first_in_msg) {   // a single item is too long: cut it
        hbMsg(&H->o, "%s%s", cur, item);
        cur[0] = 0;
        shown++;
        continue;
      }
      hbMsg(&H->o, "%s", cur);
      msgs_started++;
      if (msgs_started > 3) { cur[0] = 0; break; }
      cur[0] = 0;
      first_in_msg = true;
      need = (int)strlen(item);
      if (need > H->o.budget) { hbMsg(&H->o, "%s", item); shown++; continue; }
    }
    if (!first_in_msg) strcat(cur, "; ");
    strcat(cur, item);
    first_in_msg = false;
    shown++;
  }
  if (cur[0]) hbMsg(&H->o, "%s", cur);
  p->sg_off = (uint8_t)(off + shown > 250 ? 0 : off + shown);
  p->last_intent = HI_SUGGEST;
  if (off + shown < cnt && H->o.n < BOT_Q_MAX) hbMsg(&H->o, "Say more for other options, or ask about one.");
  return H->o.n;
}

// read the walk preferences out of the sentence into the person's search
static HB_NOINLINE void hbReadFilters(HbCtx* H, HbWords* w) {
  HikePerson* p = H->who;
  p->sg_glo = p->sg_ghi = 0;
  p->sg_tmin = p->sg_tmax = 0;
  p->sg_feat = 0;
  p->sg_town = p->sg_region = -1;
  p->sg_off = 0;
  HbHit pl = hbFind(w, 2 | 4);   // the place first, so its own words ("gold coast") are not read as wishes
  if (pl.ok) {
    hbUse(w, &pl);
    if (pl.kind == 1) p->sg_town = (int8_t)pl.idx; else p->sg_region = (int8_t)pl.idx;
  }
  char pdbuf[CHAT_WORK_MAX + 4];
  int po = 0;
  pdbuf[po++] = ' ';
  for (int i = 0; i < w->n; i++) {
    if (w->used[i] || po + w->l[i] + 2 >= (int)sizeof(pdbuf)) continue;
    memcpy(pdbuf + po, w->p[i], w->l[i]);
    po += w->l[i];
    pdbuf[po++] = ' ';
  }
  pdbuf[po] = 0;
  const char* pd = pdbuf;
  if (hbHas(pd, "easy;beginner;beginners;gentle;flat;relaxed;simple;pram;stroller;wheelchair;toddler;toddlers;kids;kid;children;child;baby;family;nothing hard;not hard")) { p->sg_glo = 1; p->sg_ghi = 2; }
  else if (hbHas(pd, "moderate;medium;intermediate")) { p->sg_glo = 3; p->sg_ghi = 3; }
  else if (hbHas(pd, "hard;harder;challenge;challenging;difficult;tough;steep;advanced;experienced;serious;strenuous;fit")) { p->sg_glo = 4; p->sg_ghi = 5; }
  if (hbHas(pd, "short;quick;little;brief;lunch;half hour;under an hour;an hour or less;toddler;toddlers;kids;kid;baby;pram;stroller")) p->sg_tmax = 9;   // 90 min
  else if (hbHas(pd, "half day;few hours;a few hours")) { p->sg_tmin = 12; p->sg_tmax = 30; }
  else if (hbHas(pd, "long;full day;all day;big;epic")) p->sg_tmin = 24;
  int d = hbParseDuration(H->norm);
  if (d > 0 && hbHas(pd, "under;less than;within;max;maximum;up to;no more than")) p->sg_tmax = (uint8_t)(d / 10 > 250 ? 250 : d / 10);
  for (int i = 0; i < HB_FEAT_COUNT; i++) if (hbHas(pd, HB_FEATS[i].keys)) p->sg_feat |= (uint16_t)(1u << i);
}

static bool hbHasFilters(const HikePerson* p) {
  return p->sg_ghi || p->sg_tmin || p->sg_tmax || p->sg_feat || p->sg_town >= 0 || p->sg_region >= 0;
}

// ---------------------------------------------------------------------------------------------------------------
// safety: the high priority chat rules always win

static bool hbSafetyWins(const char* padded, bool gearish) {
  for (int i = 0; i < CHAT_RULE_COUNT; i++) {
    if (CHAT_RULES[i].prio < 90) continue;
    char mk[44];
    if (!chatMatchKeys(padded, CHAT_RULES[i].keys, mk, sizeof(mk))) continue;
    if (gearish && (CHAT_RULES[i].prio == 90 || strncmp(mk, "snake", 5) == 0 || strncmp(mk, "bitten", 6) == 0)) continue;
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// the front door

static HB_NOINLINE int hbChat(HbCtx* H, const char* text) {
  static char rep[BOT_MSG_MAX + 1];
  rep[0] = 0;
  int n = chatbotReply(H->c->sender, H->c->slen, text, H->o.budget, H->c->seed, rep);
  if (n <= 0 || rep[0] == 0) return H->o.n;
  hbMsg(&H->o, "%s", rep);
  return H->o.n;
}

static bool hbTopicExists(HbCtx* H, int min_prio) {
  for (int i = 0; i < HIKE_TOPIC_COUNT; i++)
    if (HIKE_TOPICS[i].prio >= min_prio && hbHas(H->padded, HIKE_TOPICS[i].keys)) return true;
  return false;
}

static HB_NOINLINE bool hbTopic(HbCtx* H, int min_prio, int max_prio) {
  int best = -1;
  for (int i = 0; i < HIKE_TOPIC_COUNT; i++) {
    int pr = HIKE_TOPICS[i].prio;
    if (pr < min_prio || pr > max_prio) continue;
    if (best >= 0 && pr <= HIKE_TOPICS[best].prio) continue;
    if (hbHas(H->padded, HIKE_TOPICS[i].keys)) best = i;
  }
  if (best < 0) return false;
  // variants separated by '|', used in turn
  const char* txt = HIKE_TOPICS[best].text;
  int nv = 1;
  for (const char* c = txt; *c; c++) if (*c == '|') nv++;
  uint8_t* cyc = &g_hike_cyc[best];
  int v = (*cyc == 255) ? (int)(H->c->seed % nv) : (*cyc + 1) % nv;
  *cyc = (uint8_t)v;
  const char* s = txt;
  for (int k = 0; k < v; k++) { s = strchr(s, '|'); s++; }
  const char* e = strchr(s, '|');
  static char seg[4 * (HIKE_PART_MAX + 2) + 8];
  int len = e ? (int)(e - s) : (int)strlen(s);
  if (len > (int)sizeof(seg) - 1) len = (int)sizeof(seg) - 1;
  memcpy(seg, s, len);
  seg[len] = 0;
  hbParts(&H->o, seg);
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// local wildlife (wildlife.h, WildBot.h)

// one species: "<name>: <first part>", then any further parts (separated by '~')
static HB_NOINLINE void hbWildSay(HbOut* o, int s) {
  const char* info = WILDLIFE[s].info;
  const char* e = strchr(info, '~');
  char seg[BOT_MSG_MAX + 1];
  int len = e ? (int)(e - info) : (int)strlen(info);
  if (len > BOT_MSG_MAX) len = BOT_MSG_MAX;
  memcpy(seg, info, len);
  seg[len] = 0;
  hbMsg(o, "%s: %s", WILDLIFE[s].name, seg);
  if (e) hbParts(o, e + 1);
}

// kind 1 = the species of category idx, 2 = the items of group idx, 3 = the top level groups
static HB_NOINLINE int hbWildList(HbOut* o, int kind, int idx) {
  const char* names[TRAILBOT_MAX_ITEMS];
  int n = 0;
  if (kind == 1) n = wildCatSpecies(idx, names, TRAILBOT_MAX_ITEMS);
  else if (kind == 2) n = wildGroupItems(idx, names, TRAILBOT_MAX_ITEMS);
  else n = wildTopGroups(names, TRAILBOT_MAX_ITEMS);
  if (n <= 0) return 0;
  o->n = trailbotList(names, n, o->budget, o->out);
  return o->n;
}

// an exact "!koala", "!frogs", "!birds", "!animals", "!wildlife". q is the normalised text. 0 = not wildlife.
static HB_NOINLINE int hbWildExact(HbOut* o, const char* q) {
  int s = wildFindSpecies(q);
  if (s >= 0) { hbWildSay(o, s); return o->n; }
  int c = wildFindCat(q);
  if (c >= 0) return hbWildList(o, 1, c);
  int g = wildFindGroup(q);
  if (g >= 0) return hbWildList(o, 2, g);
  if (wildIsTop(q)) return hbWildList(o, 3, 0);
  return 0;
}

// a found injured, sick or orphaned animal: the wildlife care entry (the general first aid topics are for people)
static HB_NOINLINE bool hbWildInjured(HbCtx* H) {
  if (!hbHas(H->padded, "injured animal;injured wildlife;injured bird;injured koala;injured joey;injured possum;injured kangaroo;"
                         "injured wallaby;injured echidna;injured turtle;injured bat;injured lizard;injured platypus;injured owl;"
                         "sick animal;sick bird;sick koala;hurt animal;hurt bird;orphaned joey;orphaned animal;orphaned wildlife;"
                         "abandoned joey;baby bird on the ground;found a joey;found a baby bird;dead animal;hit a kangaroo;"
                         "hit a wallaby;hit a koala;hit a possum;hit an animal"))
    return false;
  int s = wildFindSpecies("injured wildlife");
  if (s < 0) return false;
  hbWildSay(&H->o, s);
  return true;
}

// "what birds can I see?" : a category or group named in a question gives its list (a named species is answered later)
static HB_NOINLINE bool hbWildAsk(HbCtx* H) {
  bool ask = H->question || hbHas(H->padded, "what;which;list;show me;types of;kinds of;names of;any;tell me about;where can i see;can i see;do you know");
  if (!ask) return false;
  if (hbHas(H->padded, "allowed;permitted;banned;dog;dogs;pet;pets;leash;gear;pack;bring;take;wear;carry;bite;bitten;walk;walks;trail;trails;track;hike;hikes")) return false;
  if (wildMentionSpecies(H->padded) >= 0) return false;
  int kind = 0, idx = -1;
  if (!wildMentionList(H->padded, &kind, &idx)) {
    if (!hbHas(H->padded, "wildlife;nature")) return false;
    kind = 3;   // "what wildlife can I see?": the top level groups
  }
  return hbWildList(&H->o, kind, idx) > 0;
}

// the radio-check phrases that give the same reply as '!test' (used by the normal flow and by the #test channel)
#define HB_RADIO_PHRASES "radio check;signal check;can you hear me;do you copy;how is my signal;how is my reception;am i getting through;" \
                         "test message;radio test;signal test;do you read me;are you receiving me;can you read me"

static HB_NOINLINE int hikebotReply(const HikeCtx* c, const char* text, char out[][BOT_MSG_MAX + 1]) {
  if (!g_hike_ready) {
    memset(g_hike_cyc, 255, sizeof(g_hike_cyc));
    memset(g_hike_people, 0, sizeof(g_hike_people));
    g_hike_ready = true;
  }
  static HbCtx H;   // static: keeps the stack small
  memset(&H, 0, sizeof(H));
  H.c = c;
  H.o.out = out;
  H.o.n = 0;
  int prefix = (int)strlen(c->node_name) + 2;
  if (prefix > 39) prefix = 39;
  H.o.budget = BOT_MSG_MAX - prefix;
  if (H.o.budget > BOT_MSG_MAX) H.o.budget = BOT_MSG_MAX;
  if (H.o.budget < 30) return 0;

  chatNorm(text, H.norm, sizeof(H.norm));
  snprintf(H.padded, sizeof(H.padded), " %s ", H.norm);
  {   // a signal test needs nothing remembered: no memory slot is used for it (and none for anything else on a test channel)
    char tq[TRAILBOT_QUERY_MAX + 8];
    trailbotNorm(text, tq, sizeof(tq));
    bool exact_test = strcmp(tq, "test") == 0 || strcmp(tq, "ping") == 0;
    if (c->test_only) {   // test channel: only the signal-test commands, nothing else
      if (!exact_test && !hbHas(H.padded, HB_RADIO_PHRASES)) return 0;
      hbClockInit(c->now_utc, &H.clk);
      return hbDoTest(&H);
    }
    if (exact_test) {
      hbClockInit(c->now_utc, &H.clk);
      return hbDoTest(&H);
    }
  }

  char name[CHAT_NAME_MAX + 8];
  chatCleanName(c->sender, c->slen, name);
  H.who = hikePersonFor(chatHash(name, (int)strlen(name)), c->now_utc);
  hbClockInit(c->now_utc, &H.clk);
  for (const char* q = text; *q; q++) if (*q == '?') H.question = true;
  hbSplit(H.norm, &H.w);
  HikePerson* P = H.who;

  if (H.norm[0] == 0) return hbChat(&H, text);   // just "!"

  bool q_words = H.question || hbHas(H.padded, "should i;what time;when should;if i;can i;is it;how long;will i");

  // 4. hike timer
  bool has_finish = hbHas(H.padded, "finished;finish hike;finished hike;finished the hike;finished my hike;finished the walk;finished my walk;"
                                    "done hiking;done the hike;done the walk;back at the car;back at the carpark;made it back;got back;"
                                    "i am back;completed the hike;completed the walk;completed;stop the timer;stop timer;end timer;"
                                    "hike done;walk done;hike complete;walk complete;all done");
  bool has_start = hbHas(H.padded, "starting now;starting my hike;starting the hike;starting my walk;starting the walk;starting out;"
                                   "setting off;set off now;heading out;head out;off we go;start the timer;start timer;start hike;"
                                   "begin hike;beginning my hike;hit the trail;hitting the trail;at the trailhead;leaving now;"
                                   "starting a hike;starting a walk;starting off;i am starting;we are starting");
  if (!has_start && hbHas(H.padded, "starting the;starting my;starting our;starting a;beginning the;beginning my;beginning a") &&
      hbHas(H.padded, "track;trail;walk;hike;circuit;loop;climb;summit;peak;falls;lookout;ridge"))
    has_start = true;   // "starting the Barney Peak walk"
  // "I'm starting the Twin Falls walk tomorrow" is a plan for another day: work out the daylight, don't start a timer now
  bool plan_day = false;
  if (has_start && !q_words) {
    HbDay pd;
    if (hbDayInfo(&H, &pd)) { plan_day = true; has_start = false; }
  }

  // 1. exact commands
  {
    char q[TRAILBOT_QUERY_MAX + 8];
    trailbotNorm(text, q, sizeof(q));
    bool timer_msg = !q_words && (has_start || has_finish);   // "starting the Mt Barney track now" is a timer, not a lookup
    bool fix_reply = P->t_start && (P->t_trail == HT_NONE || (P->flags & HP_ASSUMED)) && strncmp(q, "trails", 6) != 0 &&
                     trailbotFindTrail(q) >= 0;   // a trail name answering "Not right? Name the trail": handled below
    int n = (timer_msg || fix_reply) ? -1 : trailbotLookup(text, c->node_name, out);
    if (n >= 0) {
      if (strncmp(q, "trails", 6) != 0) {
        int t = trailbotFindTrail(q);
        if (t >= 0) P->last_trail = (int16_t)t;
      }
      return n;
    }
    if (!timer_msg && !fix_reply) {
      int w = hbWildExact(&H.o, q);   // !koala, !frogs, !birds, !plants, !wildlife
      if (w > 0) return w;
    }
  }

  // 2. safety first
  bool gearish = hbHas(H.padded, "kit;bandage;bandages;pack;packing;carry;gear;what to take;should i take;to take;what to bring;"
                                 "to bring;should i bring;to pack;what should i wear;checklist;equipment");
  if (hbSafetyWins(H.padded, gearish)) return hbChat(&H, text);

  // 3. radio check
  if (hbHas(H.padded, HB_RADIO_PHRASES)) {
    return hbDoTest(&H);
  }

  bool offtrail_say = hbHas(H.padded, HB_OFFTRAIL_PHRASES);
  if (P->flags & HP_AWAIT_DUR) {
    int d = hbParseDuration(H.norm);
    if (d >= 0 && !has_start) {
      int t = hbPickHike(&H, false, NULL);
      return hbDoFinish(&H, t);
    }
  }
  if (has_finish && !q_words) {
    int t = hbPickHike(&H, false, NULL);
    return hbDoFinish(&H, t);
  }
  if (has_start && !q_words) {
    bool assumed = false;
    int t = hbPickHike(&H, true, &assumed);
    return hbDoStart(&H, t, assumed);
  }
  // a running timer whose hike was assumed (or not known): a short message naming the right trail, or saying off trail, corrects it
  if (P->t_start && !q_words && (P->t_trail == HT_NONE || (P->flags & HP_ASSUMED)) && hbWordCount(H.norm) <= 7) {
    int nt = HT_NONE;
    HbWords& w3 = *hbCopyWords(&H.w, 1);
    HbHit h3 = hbFind(&w3, 1);
    if (h3.ok && h3.dist == 0) nt = h3.idx;
    else if (offtrail_say) nt = HT_OFFTRAIL;
    else if ((P->flags & HP_ASSUMED) && hbUnlistedName(&H)) nt = HT_UNKNOWN;
    if (nt != HT_NONE && nt != P->t_trail) {
      P->t_trail = (int16_t)nt;
      P->last_trail = (int16_t)nt;
      P->flags &= ~(HP_ASSUMED | HP_OFFTRAIL);
      {   // reply and stop: this message was the correction
        if (nt >= 0) {
          HikeInfo inf;
          hbParseInfo(TRAILS[nt].info, &inf);
          int need = hbNeedMin(P, &inf);
          char d[20];
          if (need >= 0) {
            hbFmtMin(need, d, sizeof(d));
            hbMsg(&H.o, "OK, timing you on %s. Allow %s%s.", TRAILS[nt].name, inf.tapprox ? "about " : "up to ", d);
          } else {
            hbMsg(&H.o, "OK, timing you on %s. I don't have a listed time for it.", TRAILS[nt].name);
          }
        } else if (nt == HT_OFFTRAIL) {
          hbMsg(&H.o, "OK, timing your off-trail hike. I have no listed time, so I'll just time it.");
          hbOffTrailReminder(&H);
        } else {
          hbMsg(&H.o, "OK, I'll just time it. I don't have that hike listed.");
        }
        return H.o.n;
      }
    }
  }
  // a trail named while a timer without a trail is running: attach it
  if (P->t_start && P->t_trail == HT_NONE) {
    HbWords& w2 = *hbCopyWords(&H.w, 0);
    HbHit h = hbFind(&w2, 1);
    if (h.ok && h.dist == 0) { P->t_trail = (int16_t)h.idx; }
  }
  // "I'm going off trail": remembered for their next hike timer
  if (offtrail_say && !q_words && hbHas(H.padded, "i am;im;we are;were;going;heading;doing;about to;gonna;i will;we will;planning"))
    P->flags |= HP_OFFTRAIL;

  // 5. more of the last walk search
  if (P->last_intent == HI_SUGGEST && hbWordCount(H.norm) <= 4 &&
      hbHas(H.padded, "more;another;what else;anything else;others;next;more options;other options;show me more;give me more")) {
    return hbSuggestList(&H, true);
  }

  // 5b. a walk planned for another day
  if (plan_day) {
    int t = hbTrailIn(&H, hbLastOk(&H));
    int dur = hbParseDuration(H.norm);
    if (t >= 0) P->last_trail = (int16_t)t;
    return hbDoStartAdvice(&H, t, dur);
  }

  // 6. glow worms, time, date, daylight
  if (hbHas(H.padded, "glow worm;glow worms;glowworm;glowworms") && H.clk.ok && !hbHas(H.padded, "gear;pack;bring;take;wear")) {
    int g = hbDoGlow(&H);
    if (g > 0) return g;
  }
  if (hbHas(H.padded, "moon;moons;moonrise;moonset;moon rise;moon set;moonlight;moonlit;full moon;new moon;half moon;quarter moon;crescent;"
                      "lunar;moon phase;phase of the moon;moon phases") && !hbHas(H.padded, "gear;pack;bring;take;wear")) {
    return hbDoMoon(&H);
  }
  if (hbHas(H.padded, "meteor;meteors;meteor shower;meteor showers;meteorite;meteorites;shooting star;shooting stars;falling star;"
                      "falling stars;perseids;geminids;leonids;orionids;lyrids;taurids;aquariids;stargazing;star gazing;stargaze") &&
      !hbHas(H.padded, "gear;pack;bring;take;wear")) {
    return hbDoMeteors(&H);
  }
  if (hbHas(H.padded, "what time is it;what is the time;time now;current time;what day is it;what is the date;todays date;today date;"
                      "what date;what is today;what month;what year;what is the day;what is todays date;the date;what day is tomorrow;tomorrows date;"
                      "date tomorrow;what day is it tomorrow;what day is it on")) {
    return hbDoTimeDate(&H);
  }
  if (hbHas(H.padded, "when should i start;when should i leave;what time should i start;what time should i leave;when should i set off;"
                      "what time should i set off;what time should i go;when should i go;when should i head;latest start;too late to start;"
                      "is it too late;too late;too early;is it too early;should i start now;should i go now;if i start now;"
                      "if i leave now;if i go now;what time will i finish;what time will i be back;when will i be back;when will i finish;"
                      "return time;get back by;start now;leave now;go now")) {
    int t = hbTrailIn(&H, hbLastOk(&H));
    int dur = hbParseDuration(H.norm);
    if (t >= 0) P->last_trail = (int16_t)t;
    return hbDoStartAdvice(&H, t, dur);
  }
  if (hbHas(H.padded, "sunset;sunrise;when does it get dark;how long until dark;before dark;daylight;when is sunset;when is sunrise;"
                      "how much daylight;first light;last light;dusk;dawn;get dark;gets dark")) {
    return hbDoSun(&H);
  }

  // 7. compare two trails
  if (hbHas(H.padded, "harder;easier;longer;shorter;quicker;faster;slower;steeper;tougher;versus;vs;compared to;compare;which is better;"
                      "which one is;which is longer;which is harder;difference between")) {
    HbWords& w2 = *hbCopyWords(&H.w, 0);
    HbHit h1 = hbFind(&w2, 1);
    if (h1.ok) {
      hbUse(&w2, &h1);
      HbHit h2 = hbFind(&w2, 1);
      int a = -1, b = -1;
      if (h2.ok) {
        // keep the order they were said in
        if (h1.w0 <= h2.w0) { a = h1.idx; b = h2.idx; } else { a = h2.idx; b = h1.idx; }
      } else if (P->last_trail >= 0 && P->last_trail != h1.idx) {
        a = h1.idx; b = P->last_trail;
      }
      if (a >= 0 && b >= 0 && a != b) {
        P->last_trail = (int16_t)b;
        return hbDoCompare(&H, a, b);
      }
    }
  }

  // 8. what to take
  bool gear_q = hbHas(H.padded, "what should i take;what should i bring;what should i pack;what to take;what to bring;what to pack;"
                                "what do i need;what gear;gear;packing list;pack list;checklist;equipment;what to wear;what should i wear;"
                                "what do i wear;should i take;should i bring;should i pack;do i need to bring;do i need to take;"
                                "do i need to pack;items to take;stuff to take;stuff to bring;essentials;what to carry;what should i carry;"
                                "what do i take;what do i bring;what do i pack;what do i carry;what do we need;what should we take;"
                                "what should we bring;what do we take;what do we bring");
  bool water_q = hbHas(H.padded, "how much water;how many litres;how many liters;litres of water;water per hour");
  if (gear_q && !water_q) {
    int t = hbTrailIn(&H, hbLastOk(&H));
    if (t >= 0) P->last_trail = (int16_t)t;
    return hbDoGear(&H, t);
  }

  // 8b. an injured animal, or a wildlife group or category asked about ("what birds can I see?")
  if (hbWildInjured(&H)) return H.o.n;
  if (hbWildAsk(&H)) return H.o.n;

  // 9. how long / how far / how hard / what is there to see (a trail named, or the last one)
  {
    bool f_time = hbHas(H.padded, "how long;how much time;how many hours;how many minutes;how many mins;duration;time does it take;"
                                  "how quick;how fast;how long does;how long will;how long would;time it takes;take me");
    bool f_dist = hbHas(H.padded, "how far;distance;how many km;how many kilometres;how many kilometers;kilometres;kilometers;kms;how long is the walk;"
                                  "how long is the trail;how long is the track;how long is it;length");
    bool f_grade = hbHas(H.padded, "how hard;how difficult;how steep;is it hard;is it easy;is it steep;difficulty;grade;how tough;"
                                   "how strenuous;is it difficult;hard is;difficult is;steep is;what grade");
    bool f_kids = hbHas(H.padded, "good for kids;suitable for kids;ok for kids;okay for kids;with kids;with a toddler;with toddlers;"
                                  "with a pram;with a stroller;kid friendly;family friendly;for children;with children;with a baby;with babies");
    bool f_feat = hbHas(H.padded, "what is there to see;what can i see;what is on it;highlights;what does it have;features;what is along");
    if (f_time || f_dist || f_grade || f_kids || f_feat) {
      HbWords& w2 = *hbCopyWords(&H.w, 0);
      HbHit h = hbFind(&w2, 1);
      int t = -1;
      if (h.ok) t = h.idx;
      else {
        HbWords& w3 = *hbCopyWords(&H.w, 1);
        HbHit place = hbFind(&w3, 6);   // a town or region named: that is a search, not a question about the last trail
        if (!place.ok && P->last_trail >= 0 && (hbLastOk(&H) || hbWordCount(H.norm) <= 5)) t = P->last_trail;
      }
      if (t >= 0) {
        P->last_trail = (int16_t)t;
        bool both = f_time && f_dist;
        if (f_time && hbHas(H.padded, "how long is the walk;how long is the trail;how long is the track;how long is it")) both = true;
        return hbDoFacet(&H, t, f_time || both, f_dist || both, f_grade, f_kids, f_feat);
      }
      if (hbWordCount(H.norm) <= 5 && !(f_time && hbHas(H.padded, "hike;walk"))) {
        HbWords& w3 = *hbCopyWords(&H.w, 1);
        HbHit place = hbFind(&w3, 6);
        if (!place.ok) {
          hbMsg(&H.o, "Which trail do you mean? Tell me the name, like Twin Falls, and I'll look it up.");
          return H.o.n;
        }
      }
    }
  }

  // 10. walk suggestions
  {
    bool sug_key = hbHas(H.padded, "suggest;recommend;recommendation;recommendations;ideas;idea;where should i;where can i;what is there to walk;what walks;"
                                   "which walk;which trail;which track;looking for;find me;something;anything;i want to do a;i want a;i would like;"
                                   "easy walk;short walk;best walk;best hike;good walk;good hike;nice walk;nice hike;any walks;any trails;any hikes;"
                                   "what is a good;what is an easy;what is a short;a walk;a hike");
    bool sug_loose = !sug_key && !hbTopicExists(&H, 60) &&
                     hbHas(H.padded, "walk;walks;hike;hikes;trail;trails;track;tracks;route;routes;bushwalk;bushwalks");
    bool named_trail = false;
    {
      HbWords& w4 = *hbCopyWords(&H.w, 2);
      HbHit th = hbFind(&w4, 1);
      named_trail = th.ok && th.dist == 0;
      if (named_trail) {   // a trail is named, but a place too ("easy coastal walk near the gold coast"): still a search
        hbUse(&w4, &th);
        HbHit place = hbFind(&w4, 6);
        if (place.ok) named_trail = false;
      }
    }
    if ((sug_key || sug_loose) && !named_trail) {
      HbWords& w2 = *hbCopyWords(&H.w, 0);
      HikePerson save = *P;
      hbReadFilters(&H, &w2);
      if (hbHasFilters(P)) {
        bool place_only = P->sg_glo == 0 && P->sg_ghi == 0 && P->sg_tmin == 0 && P->sg_tmax == 0 && P->sg_feat == 0 &&
                          (P->sg_town >= 0 || P->sg_region >= 0);
        if (place_only) {   // just a place: the same list as the !trails tree
          const char* nm = P->sg_town >= 0 ? TRAIL_TOWNS[P->sg_town].name : TRAIL_REGIONS[P->sg_region].name;
          char q[TRAILBOT_QUERY_MAX + 8];
          snprintf(q, sizeof(q), "%s", nm);
          int n = trailbotLookup(q, c->node_name, out);
          P->sg_town = P->sg_region = -1;
          if (n > 0) return n;
        } else {
          return hbSuggestList(&H, false);
        }
      } else {
        *P = save;
        if (sug_key && hbHas(H.padded, "walk;walks;hike;hikes;trail;trails;track;tracks;bushwalk;suggest;recommend;recommendation;ideas;idea;looking for")) {
          hbMsg(&H.o, "What kind of walk? Say easy or hard, short or long, something to see (waterfall, lookout, swim) and a place.");
          return H.o.n;
        }
      }
    }
  }

  // 11. specific hiking knowledge
  if (hbTopic(&H, 60, 255)) return H.o.n;

  // 11b. a local animal, plant or field guide topic named anywhere in the sentence
  {
    int ws = wildMentionSpecies(H.padded);
    if (ws >= 0) {
      hbWildSay(&H.o, ws);
      return H.o.n;
    }
  }

  // 12. a trail, town or region named anywhere in the sentence
  {
    HbWords& w2 = *hbCopyWords(&H.w, 0);
    HbHit h = hbFind(&w2, 7);
    if (h.ok) {
      if (h.tie && h.alt_kind >= 0) {
        hbMsg(&H.o, "Did you mean %s or %s?", hbEntityName(h.kind, h.idx), hbEntityName(h.alt_kind, h.alt_idx));
        return H.o.n;
      }
      if (h.kind == 0) {
        P->last_trail = (int16_t)h.idx;
        return hbDoTrailInfo(&H, h.idx);
      }
      char q[TRAILBOT_QUERY_MAX + 8];
      snprintf(q, sizeof(q), "%s", hbEntityName(h.kind, h.idx));
      int n = trailbotLookup(q, c->node_name, out);
      if (n > 0) return n;
    }
  }

  // 13. lighter topics (what can you do, help)
  if (hbTopic(&H, 0, 59)) return H.o.n;

  // 14. chat
  return hbChat(&H, text);
}
