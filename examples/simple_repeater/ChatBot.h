#pragma once
/*
 * ELIZA-style chat engine for the bot channel (item 33). Pure functions: no mesh, radio or Arduino calls, so it can
 * be tested on a PC (tools/chatbot_test.cpp). HikeBot.h calls chatbotReply() for a '!' message it can not answer from the trail or hike knowledge.
 *
 * What it does, in order:
 *   1. cleans the message (lowercase, no punctuation, contractions expanded, a few slang words fixed)
 *   2. finds the matching rule with the highest priority (chatrules.h)
 *   3. cuts out what the person said after the keyword, swaps I/you, my/your, am/are
 *   4. picks the next reply template of that rule (so it does not repeat), and fills in the blanks
 *   5. remembers one line per person (15 people) to bring up later, and notices repeated messages
 *
 * Safety rules (emergencies, being lost, snakes, low water ...) have the top priorities, so they win over chat.
 * A reply never starts with '!' or '*', so another bot repeater on the channel can not mistake it for a request.
 * About 700 bytes of RAM; the rules live in flash.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "chatrules.h"

#ifndef BOT_MSG_MAX
  #define BOT_MSG_MAX 134      // total message length including "<node name>: "
#endif

#define CHAT_TEXT_MAX  168     // longest cleaned message kept (characters)
#define CHAT_CAP_MAX   44      // longest piece of the person's words that is repeated back
#define CHAT_NAME_MAX  18      // longest sender name used in a reply (bytes)
#define CHAT_SENDERS   15      // how many people it remembers
#define CHAT_WORK_MAX  (CHAT_TEXT_MAX + 16)

struct ChatSender {
  uint16_t id;                 // hash of the sender's name (0 = free slot)
  uint16_t last_hash;          // hash of their last message
  uint16_t age;                // use counter, to recycle the oldest slot
  char mem[CHAT_CAP_MAX + 12]; // a line they said earlier ("your feet hurt"), or ""
};

static ChatSender g_chat_senders[CHAT_SENDERS];
static uint8_t g_chat_cyc[CHAT_RULE_COUNT];   // which reply of each rule is next (255 = not used yet)
static uint8_t g_chat_fb, g_chat_mem_cyc, g_chat_rep_cyc, g_chat_empty_cyc;
static bool g_chat_ready = false;
static uint16_t g_chat_tick = 0;

// ---------------------------------------------------------------------------------------------------------------
// cleaning

struct ChatSubst { const char* from; const char* to; };
static const ChatSubst CHAT_SUBSTS[] = {
  {"im", "i am"}, {"ive", "i have"}, {"youre", "you are"}, {"youve", "you have"}, {"youll", "you will"},
  {"youd", "you would"}, {"dont", "do not"}, {"doesnt", "does not"}, {"didnt", "did not"}, {"cant", "cannot"},
  {"wont", "will not"}, {"isnt", "is not"}, {"arent", "are not"}, {"wasnt", "was not"}, {"havent", "have not"},
  {"hasnt", "has not"}, {"couldnt", "could not"}, {"shouldnt", "should not"}, {"wouldnt", "would not"},
  {"thats", "that is"}, {"whats", "what is"}, {"hows", "how is"}, {"theres", "there is"}, {"wanna", "want to"},
  {"gonna", "going to"}, {"gotta", "have to"}, {"mum", "mother"}, {"mom", "mother"}, {"dad", "father"},
  {"mates", "friends"}, {"mate", "friend"}, {"ur", "your"}, {"u", "you"}, {"r", "are"}, {"pls", "please"},
  {"plz", "please"}, {"thx", "thanks"}, {"yeh", "yes"}, {"yea", "yes"}, {"ya", "yes"}, {"nup", "no"},
  {"gday", "hello"},
};
#define CHAT_SUBST_COUNT ((int)(sizeof(CHAT_SUBSTS) / sizeof(CHAT_SUBSTS[0])))

static bool chatWordIs(const char* w, int len, const char* lit) {
  return (int)strlen(lit) == len && memcmp(w, lit, len) == 0;
}

// lowercase words separated by single spaces, apostrophes and other punctuation removed, contractions expanded
static void chatNorm(const char* in, char* out, int outsz) {
  char tmp[CHAT_WORK_MAX];
  int n = 0;
  bool space = false;
  for (const unsigned char* p = (const unsigned char*)in; *p && n < (int)sizeof(tmp) - 2; p++) {
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      if (space && n > 0) tmp[n++] = ' ';
      space = false;
      tmp[n++] = (char)c;
    } else if (c == '\'' || c >= 0x80) {
      // dropped, joins the letters on either side
    } else {
      space = true;
    }
  }
  tmp[n] = 0;
  int o = 0;
  const char* p = tmp;
  while (*p && o < outsz - 1) {
    const char* e = strchr(p, ' ');
    int len = e ? (int)(e - p) : (int)strlen(p);
    const char* rep = NULL;
    for (int i = 0; i < CHAT_SUBST_COUNT; i++) {
      if (chatWordIs(p, len, CHAT_SUBSTS[i].from)) { rep = CHAT_SUBSTS[i].to; break; }
    }
    if (o > 0 && o < outsz - 1) out[o++] = ' ';
    const char* src = rep ? rep : p;
    int sl = rep ? (int)strlen(rep) : len;
    for (int i = 0; i < sl && o < outsz - 1; i++) out[o++] = src[i];
    p = e ? e + 1 : p + len;
  }
  out[o] = 0;
}

static uint16_t chatHash(const char* s, int len) {   // 16 bit FNV-1a
  uint32_t h = 2166136261UL;
  for (int i = 0; i < len; i++) {
    h ^= (uint8_t)s[i];
    h *= 16777619UL;
  }
  uint16_t r = (uint16_t)(h ^ (h >> 16));
  return r ? r : 1;
}

// ---------------------------------------------------------------------------------------------------------------
// swapping I / you

static bool chatIsVerbAfterYou(const char* w, int len) {
  static const char* const v[] = {"are", "were", "have", "will", "can", "cannot", "do", "did", "would", "should", "could", "may",
                                  "must", "had", "was", "shall", "like", "love", "want", "need", "know", "think", "feel", "see"};
  for (unsigned i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
    if (chatWordIs(w, len, v[i])) return true;
  }
  return false;
}

static void chatPut(char* out, int outsz, int* o, const char* s, int len) {
  if (*o > 0 && *o < outsz - 1) out[(*o)++] = ' ';
  for (int i = 0; i < len && *o < outsz - 1; i++) out[(*o)++] = s[i];
  out[*o] = 0;
}

static void chatReflect(const char* in, char* out, int outsz) {
  out[0] = 0;
  int o = 0;
  const char* prev = "";
  int prev_len = 0;
  const char* p = in;
  while (*p) {
    const char* e = strchr(p, ' ');
    int len = e ? (int)(e - p) : (int)strlen(p);
    const char* next = e ? e + 1 : "";
    const char* ne = strchr(next, ' ');
    int next_len = *next ? (ne ? (int)(ne - next) : (int)strlen(next)) : 0;

    const char* r = NULL;
    if (chatWordIs(p, len, "i") || chatWordIs(p, len, "me")) r = "you";
    else if (chatWordIs(p, len, "my")) r = "your";
    else if (chatWordIs(p, len, "mine")) r = "yours";
    else if (chatWordIs(p, len, "myself")) r = "yourself";
    else if (chatWordIs(p, len, "am")) r = "are";
    else if (chatWordIs(p, len, "was") && chatWordIs(prev, prev_len, "i")) r = "were";
    else if (chatWordIs(p, len, "are") && chatWordIs(prev, prev_len, "you")) r = "am";
    else if (chatWordIs(p, len, "were") && chatWordIs(prev, prev_len, "you")) r = "was";
    else if (chatWordIs(p, len, "you")) r = chatIsVerbAfterYou(next, next_len) ? "I" : "me";
    else if (chatWordIs(p, len, "your")) r = "my";
    else if (chatWordIs(p, len, "yours")) r = "mine";
    else if (chatWordIs(p, len, "yourself")) r = "myself";
    if (r) chatPut(out, outsz, &o, r, (int)strlen(r));
    else chatPut(out, outsz, &o, p, len);

    prev = p;
    prev_len = len;
    p = e ? e + 1 : p + len;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// matching

// finds the first of the ';' separated keys of a rule that occurs as whole words in 'padded' (" words "). Returns the
// position just after the matched key (the start of what follows), and the key text in 'matched'. NULL = no match.
static const char* chatMatchKeys(const char* padded, const char* keys, char* matched, int msz) {
  const char* k = keys;
  while (*k) {
    const char* e = strchr(k, ';');
    int len = e ? (int)(e - k) : (int)strlen(k);
    if (len > 0 && len < 40) {
      char pk[48];
      pk[0] = ' ';
      memcpy(&pk[1], k, len);
      pk[len + 1] = ' ';
      pk[len + 2] = 0;
      const char* f = strstr(padded, pk);
      if (f) {
        int mlen = len < msz - 1 ? len : msz - 1;
        memcpy(matched, k, mlen);
        matched[mlen] = 0;
        return f + len + 2;   // after the space that ends the key
      }
    }
    k = e ? e + 1 : k + len;
  }
  return NULL;
}

// what the person said after the keyword, up to a joining word, at most CHAT_CAP_MAX characters
static void chatCapture(const char* after, char* cap, int csz) {
  static const char* const cuts[] = {"but", "because", "so", "then", "though", "although", "however"};
  int o = 0;
  cap[0] = 0;
  const char* p = after;
  while (*p) {
    const char* e = strchr(p, ' ');
    int len = e ? (int)(e - p) : (int)strlen(p);
    if (len == 0) break;
    bool cut = false;
    for (unsigned i = 0; i < sizeof(cuts) / sizeof(cuts[0]); i++) {
      if (chatWordIs(p, len, cuts[i])) cut = true;
    }
    if (cut && o > 0) break;
    if (cut) { p = e ? e + 1 : p + len; continue; }
    if (o + (o ? 1 : 0) + len > CHAT_CAP_MAX || o + (o ? 1 : 0) + len > csz - 1) break;
    if (o) cap[o++] = ' ';
    memcpy(&cap[o], p, len);
    o += len;
    cap[o] = 0;
    p = e ? e + 1 : p + len;
  }
  // a trailing space from the padded input is already skipped (len == 0 above)
  while (o > 0 && cap[o - 1] == ' ') cap[--o] = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// templates

static int chatCountSegs(const char* s) {
  int n = 1;
  for (; *s; s++) if (*s == '|') n++;
  return n;
}

static void chatSeg(const char* s, int idx, const char** start, int* len) {
  const char* p = s;
  while (idx > 0) {
    const char* e = strchr(p, '|');
    if (!e) break;
    p = e + 1;
    idx--;
  }
  const char* e = strchr(p, '|');
  *start = p;
  *len = e ? (int)(e - p) : (int)strlen(p);
}

static bool chatNeeds(const char* t, int len, char c) {
  return memchr(t, c, len) != NULL;
}

// fill in % & $ @ . returns false if a piece is needed but empty.
static bool chatExpand(const char* t, int len, const char* name, const char* cap, const char* amp, const char* mem, char* out, int osz) {
  int o = 0;
  for (int i = 0; i < len && o < osz - 1; i++) {
    char c = t[i];
    const char* ins = NULL;
    if (c == '%') ins = cap;
    else if (c == '&') ins = amp;
    else if (c == '$') ins = mem;
    else if (c == '@') ins = name;
    if (ins) {
      if (!ins[0]) return false;
      for (; *ins && o < osz - 1; ins++) out[o++] = *ins;
    } else {
      out[o++] = c;
    }
  }
  out[o] = 0;
  return true;
}

// the next usable template of a list. 'cyc' is the position to start from; it is advanced. 255 = start at 'seed'.
static bool chatPick(const char* list, uint8_t* cyc, uint32_t seed, const char* name, const char* cap, const char* amp,
                     const char* mem, char* out, int osz) {
  int n = chatCountSegs(list);
  int start = (*cyc == 255) ? (int)(seed % (uint32_t)n) : (int)(*cyc % n);
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % n;
    const char* t;
    int len;
    chatSeg(list, idx, &t, &len);
    if (chatExpand(t, len, name, cap, amp, mem, out, osz)) {
      *cyc = (uint8_t)((idx + 1) % n);
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------------------------------------------
// the people it remembers

static ChatSender* chatSenderFor(uint16_t id) {
  ChatSender* oldest = &g_chat_senders[0];
  for (int i = 0; i < CHAT_SENDERS; i++) {
    if (g_chat_senders[i].id == id) {
      g_chat_senders[i].age = ++g_chat_tick;
      return &g_chat_senders[i];
    }
    if (g_chat_senders[i].id == 0) {
      oldest = &g_chat_senders[i];
      break;
    }
    if ((uint16_t)(g_chat_tick - g_chat_senders[i].age) > (uint16_t)(g_chat_tick - oldest->age)) oldest = &g_chat_senders[i];
  }
  memset(oldest, 0, sizeof(*oldest));
  oldest->id = id;
  oldest->age = ++g_chat_tick;
  return oldest;
}

// ---------------------------------------------------------------------------------------------------------------
// the reply

// cut a UTF-8 name to at most CHAT_NAME_MAX bytes without splitting a character, and drop control characters
static void chatCleanName(const char* s, int slen, char* out) {
  int o = 0;
  for (int i = 0; i < slen && o < CHAT_NAME_MAX; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c == 0x7f) continue;
    out[o++] = (char)c;
  }
  out[o] = 0;
  if (o == CHAT_NAME_MAX || (o > 0 && (unsigned char)out[o - 1] >= 0x80)) {   // may have cut a multi byte character
    int k = o;
    while (k > 0 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;   // continuation bytes
    if (k > 0 && (unsigned char)out[k - 1] >= 0xC0) {
      int need = ((unsigned char)out[k - 1] >= 0xF0) ? 4 : ((unsigned char)out[k - 1] >= 0xE0) ? 3 : 2;
      if (o - (k - 1) < need) o = k - 1;   // the character is incomplete: drop it
    }
    out[o] = 0;
  }
  while (o > 0 && out[o - 1] == ' ') out[--o] = 0;
  if (o == 0) strcpy(out, "friend");
}

// text: the message after the '*'. sender / slen: the name before the ": ". budget: characters left for the reply.
// seed: any random number. out: BOT_MSG_MAX + 1 bytes. returns the length (0 = send nothing).
static int chatbotReply(const char* sender, int slen, const char* text, int budget, uint32_t seed, char* out) {
  if (!g_chat_ready) {
    memset(g_chat_cyc, 255, sizeof(g_chat_cyc));
    g_chat_fb = g_chat_mem_cyc = g_chat_rep_cyc = g_chat_empty_cyc = 255;
    g_chat_ready = true;
  }
  if (budget > BOT_MSG_MAX) budget = BOT_MSG_MAX;
  if (budget < 20) return 0;

  char name[CHAT_NAME_MAX + 8];
  chatCleanName(sender, slen, name);

  char norm[CHAT_WORK_MAX];
  chatNorm(text, norm, sizeof(norm));

  char buf[CHAT_WORK_MAX + 8];
  const char* none = "";
  ChatSender* who = chatSenderFor(chatHash(name, (int)strlen(name)));
  bool ok = false;

  if (norm[0] == 0) {   // just "!"
    ok = chatPick(CHAT_EMPTY, &g_chat_empty_cyc, seed, name, none, none, none, buf, sizeof(buf));
  } else {
    char padded[CHAT_WORK_MAX + 4];
    snprintf(padded, sizeof(padded), " %s ", norm);
    uint16_t h = chatHash(norm, (int)strlen(norm));

    int best = -1;
    const char* best_after = NULL;
    char best_key[44] = "";
    for (int i = 0; i < CHAT_RULE_COUNT; i++) {
      if (best >= 0 && CHAT_RULES[i].prio <= CHAT_RULES[best].prio) continue;
      char mk[44];
      const char* after = chatMatchKeys(padded, CHAT_RULES[i].keys, mk, sizeof(mk));
      if (after) {
        best = i;
        best_after = after;
        memcpy(best_key, mk, sizeof(best_key));
      }
    }
    int prio = best >= 0 ? CHAT_RULES[best].prio : 0;

    if (prio < 90 && who->last_hash == h) {   // said twice in a row (safety words are always answered)
      ok = chatPick(CHAT_REPEAT, &g_chat_rep_cyc, seed, name, none, none, none, buf, sizeof(buf));
    } else if (prio < 20 && who->mem[0] && (seed & 1)) {   // nothing much matched: go back to something they said
      ok = chatPick(CHAT_MEMORY, &g_chat_mem_cyc, seed, name, none, none, who->mem, buf, sizeof(buf));
      if (ok) who->mem[0] = 0;
    }
    if (!ok && best >= 0) {
      char cap_raw[CHAT_CAP_MAX + 8], cap[CHAT_CAP_MAX + 24], amp_raw[CHAT_CAP_MAX + 60], amp[CHAT_CAP_MAX + 80];
      chatCapture(best_after, cap_raw, sizeof(cap_raw));
      chatReflect(cap_raw, cap, sizeof(cap));
      snprintf(amp_raw, sizeof(amp_raw), "%s%s%s", best_key, cap_raw[0] ? " " : "", cap_raw);
      chatReflect(amp_raw, amp, sizeof(amp));
      ok = chatPick(CHAT_RULES[best].replies, &g_chat_cyc[best], seed, name, cap, amp, who->mem, buf, sizeof(buf));
      if (ok && (CHAT_RULES[best].flags & CHAT_REMEMBER) && cap[0]) {   // keep it for later
        snprintf(who->mem, sizeof(who->mem), "%.*s", CHAT_CAP_MAX + 8, amp);
      }
    }
    if (!ok) {
      ok = chatPick(CHAT_FALLBACK, &g_chat_fb, seed, name, none, none, none, buf, sizeof(buf));
    }
    who->last_hash = h;
  }
  if (!ok) return 0;

  // finish: never start with '!' or '*', capital letter first, cut to the budget at a word boundary
  char* s = buf;
  while (*s == '!' || *s == '*' || *s == ' ') s++;
  int len = (int)strlen(s);
  if (len == 0) return 0;
  if (s[0] >= 'a' && s[0] <= 'z') s[0] = (char)(s[0] - 32);
  if (len > budget) {
    int cut = budget;
    while (cut > budget / 2 && s[cut] != ' ') cut--;
    if (s[cut] != ' ') cut = budget;   // no space in the second half: hard cut
    len = cut;
    while (len > 0 && s[len - 1] == ' ') len--;
  }
  memcpy(out, s, len);
  out[len] = 0;
  return len;
}
