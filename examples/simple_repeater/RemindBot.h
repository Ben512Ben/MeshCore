// RemindBot.h - reminders for TrailBot ("remind me at 10am on Saturday to take my hiking boots").
//
// Pure functions with no mesh, radio or Arduino calls, so they can be tested on a computer.
//   - the reminder table: ONE reminder per person, REM_MAX reminders in all, RAM only (lost on reboot, like the hike timers)
//   - remindFormat(): the text posted when a reminder is due:  @[Ann] reminder: take my hiking boots
//   - remindSleepStep(): the decision the repeater's main loop takes about reminders and sleep (post it, wake the radio early,
//     go back to sleep), as a pure function so the logic can be simulated
//
// The sentences are understood in HikeBot.h (it has the clock and day parser). MyMesh.cpp posts the due reminders.
// The table is separate from the hike memory (HikePerson): a hike slot can be recycled, and a reminder must not vanish with it.
#ifndef REMINDBOT_H
#define REMINDBOT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define REM_MAX          20              // reminders in all (one per person)
#define REM_TEXT_MAX     60              // characters of reminder text kept
#define REM_NAME_MAX     32              // characters of the sender's name kept (for the @[name] mention)
#define REM_MAX_AHEAD_S  (7L * 86400L)   // a reminder can be set at most 7 days ahead
#define REM_LATE_TAG_S   300L            // more than 5 minutes late: sent with "(late)"
#define REM_LATE_DROP_S  7200L           // more than 2 hours late: dropped
#define REM_LEAD_S       5L              // the radio wakes this many seconds before a reminder is due
#define REM_NEAR_WAKE_MS 20000L          // no early wake if the normal wake is due within this long anyway

struct RemindItem {
  uint16_t id;                      // hash of the sender's name, 0 = free slot
  uint32_t due;                     // UTC seconds
  char name[REM_NAME_MAX + 1];      // the sender's name exactly as sent (without [ and ])
  char text[REM_TEXT_MAX + 1];
};

static RemindItem g_rem[REM_MAX];

static int remindCount() {
  int n = 0;
  for (int i = 0; i < REM_MAX; i++) if (g_rem[i].id != 0) n++;
  return n;
}

static RemindItem* remindFor(uint16_t id) {
  if (id == 0) id = 1;
  for (int i = 0; i < REM_MAX; i++) if (g_rem[i].id == id) return &g_rem[i];
  return NULL;
}

// copy up to max bytes of s (len bytes) without cutting a UTF-8 character in half. Control characters become spaces. If
// drop_brackets, '[' and ']' are left out (they would break the @[name] mention). Returns true if something was cut.
static bool remindCopy(char* dst, int max, const char* s, int len, bool drop_brackets) {
  int n = 0;
  bool cut = false;
  for (int i = 0; i < len && s[i]; i++) {
    unsigned char c = (unsigned char)s[i];
    if (drop_brackets && (c == '[' || c == ']')) continue;
    if (c < 32) c = ' ';
    // length of this UTF-8 character
    int cl = 1;
    if (c >= 0xF0) cl = 4;
    else if (c >= 0xE0) cl = 3;
    else if (c >= 0xC0) cl = 2;
    if (n + cl > max) {   // too long: end the text at a word, if there is one near the end
      cut = true;
      int sp = n;
      while (sp > 0 && dst[sp - 1] != ' ') sp--;
      if (sp > n / 2) n = sp;
      break;
    }
    dst[n++] = (char)c;
    for (int k = 1; k < cl && i + 1 < len && s[i + 1]; k++) dst[n++] = s[++i];
  }
  while (n > 0 && dst[n - 1] == ' ') n--;
  dst[n] = 0;
  return cut;
}

// 0 = added, 1 = this person already has a reminder, 2 = the table is full. *cut = the text was shortened.
static int remindAdd(uint16_t id, const char* name, int nlen, const char* text, uint32_t due, bool* cut) {
  if (id == 0) id = 1;
  if (remindFor(id)) return 1;
  for (int i = 0; i < REM_MAX; i++) {
    if (g_rem[i].id != 0) continue;
    RemindItem* r = &g_rem[i];
    memset(r, 0, sizeof(*r));
    r->id = id;
    r->due = due;
    remindCopy(r->name, REM_NAME_MAX, name, nlen, true);
    *cut = remindCopy(r->text, REM_TEXT_MAX, text, (int)strlen(text), false);
    return 0;
  }
  return 2;
}

static bool remindCancel(uint16_t id) {
  RemindItem* r = remindFor(id);
  if (!r) return false;
  memset(r, 0, sizeof(*r));
  return true;
}

// reminders more than REM_LATE_DROP_S past their time are removed without being posted
static void remindPurge(uint32_t now) {
  for (int i = 0; i < REM_MAX; i++) {
    if (g_rem[i].id != 0 && now > g_rem[i].due && (long)(now - g_rem[i].due) > REM_LATE_DROP_S) memset(&g_rem[i], 0, sizeof(g_rem[i]));
  }
}

// the earliest due time of any reminder, 0 if there is none
static uint32_t remindNextDue() {
  uint32_t best = 0;
  for (int i = 0; i < REM_MAX; i++) {
    if (g_rem[i].id != 0 && (best == 0 || g_rem[i].due < best)) best = g_rem[i].due;
  }
  return best;
}

// remove the earliest reminder that is due (due <= now) and give it back. false if none is due.
static bool remindTakeDue(uint32_t now, RemindItem* out) {
  remindPurge(now);
  int best = -1;
  for (int i = 0; i < REM_MAX; i++) {
    if (g_rem[i].id != 0 && g_rem[i].due <= now && (best < 0 || g_rem[i].due < g_rem[best].due)) best = i;
  }
  if (best < 0) return false;
  *out = g_rem[best];
  memset(&g_rem[best], 0, sizeof(g_rem[best]));
  return true;
}

// the message to post: "@[Ann] reminder: take my hiking boots" ("reminder (late):" if it is more than 5 minutes late).
// Cut to 'budget' characters (the room left after the repeater name prefix).
static int remindFormat(const RemindItem* r, uint32_t now, int budget, char* out, int osz) {
  bool late = now > r->due && (long)(now - r->due) > REM_LATE_TAG_S;
  char head[REM_NAME_MAX + 40];
  int hl;
  if (r->name[0]) hl = snprintf(head, sizeof(head), "@[%s] reminder%s: ", r->name, late ? " (late)" : "");
  else hl = snprintf(head, sizeof(head), "Reminder%s: ", late ? " (late)" : "");   // no name to mention
  if (hl < 0) hl = 0;
  if (hl >= (int)sizeof(head)) hl = (int)sizeof(head) - 1;
  int room = budget - hl;
  if (room < 0) room = 0;
  char txt[REM_TEXT_MAX + 1];
  remindCopy(txt, room < REM_TEXT_MAX ? room : REM_TEXT_MAX, r->text, (int)strlen(r->text), false);
  int n = snprintf(out, osz, "%s%s", head, txt);
  if (n < 0) n = 0;
  if (n >= osz) n = osz - 1;
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// what the main loop does about reminders and sleep (pure, so it can be simulated)
//
//   RS_POST        a reminder is due and the radio is awake: queue its message
//   RS_WAKE        the radio is asleep in a sleep CYCLE, a reminder is about to be due, the battery is not low and the normal wake
//                  is not about to happen anyway: wake the radio now (remember the normal wake time)
//   RS_SLEEP_AGAIN the radio was woken only for a reminder and everything is sent: sleep until the ORIGINAL wake time
//
// Not woken for: a manual "sleep <mins>" or "sleep until" (the admin switched the radio off on purpose), a sleep cycle with a low
// battery (it exists to save the battery), the bot channel being off, or a clock that is not set.

#define RS_NONE        0
#define RS_POST        1
#define RS_WAKE        2
#define RS_SLEEP_AGAIN 3

struct RemindSleepIn {
  bool bot_ready;        // the bot channel has a key and is on
  bool clock_ok;
  uint32_t now_utc;
  uint32_t next_due;     // remindNextDue(), 0 = no reminder
  bool sleeping;
  bool cycle_active;     // a sleep cycle is running (not a manual sleep)
  bool batt_ok;          // battery at or above the low-battery level
  bool rem_awake;        // the radio is awake only because of a reminder
  bool queue_room;       // the reply queue can take one more answer
  bool queue_empty;      // nothing waiting to be sent
  bool radio_quiet;      // the quiet period after the last send has passed
  long ms_to_wake;       // milliseconds until the normal wake (only meaningful while sleeping or rem_awake)
};

static int remindSleepStep(const RemindSleepIn* in) {
  if (in->rem_awake && !in->sleeping) {
    if (in->bot_ready && in->clock_ok && in->next_due != 0 && in->next_due <= in->now_utc && in->queue_room) return RS_POST;
    bool another_soon = in->bot_ready && in->clock_ok && in->next_due != 0 && in->next_due <= in->now_utc + (uint32_t)REM_LEAD_S + 30;
    if (in->queue_empty && in->radio_quiet && !another_soon) return RS_SLEEP_AGAIN;
    return RS_NONE;
  }
  if (!in->bot_ready || !in->clock_ok || in->next_due == 0) return RS_NONE;
  if (!in->sleeping) {
    if (in->next_due <= in->now_utc && in->queue_room) return RS_POST;
    return RS_NONE;
  }
  if (in->cycle_active && in->batt_ok && in->now_utc + (uint32_t)REM_LEAD_S >= in->next_due && in->ms_to_wake > REM_NEAR_WAKE_MS)
    return RS_WAKE;
  return RS_NONE;
}

#endif
