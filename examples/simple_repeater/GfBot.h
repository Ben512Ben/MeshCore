#pragma once
// GfBot: a flirty, escalating ELIZA-style chat engine for the repeater's bot channel.
// No mesh calls and no heap, so it can be tested on a PC (tools/gfbot_test.cpp).
//
// What it does (all rules live in gfrules.h):
//  - cleans the message (lowercase, no punctuation, contractions and slang fixed)
//  - finds the best keyword rule (priority, then longest key), echoes back what was said with I/you swapped
//  - HEAT: every person has a heat score. Each message heats her up, more for naughty topics. The hotter she is,
//    the dirtier her reply tier (0 teasing, 1 flirty, 2 explicit, 3 filthy). It cools off 20 minutes after the
//    last message and slowly while you are away.
//  - remembers one thing per person (what they wanted, felt, needed, ...) and comes back to it later
//  - spots repeated messages, uses the person's name and pet names, time-of-day greetings
//  - never lets a reply start with ! or * (so one bot repeater can never trigger another)

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "gfrules.h"

#define GF_MAX_USERS   8
#define GF_NAME_LEN    18      // sender names are cut to this many bytes
#define GF_SESSION_S   1200    // 20 minutes without a message: she cools down
#define GF_CLEAN_MAX   120
#define GF_ECHO_MAX    36
#define GF_TIER1_AT    10      // heat needed for each tier
#define GF_TIER2_AT    24
#define GF_TIER3_AT    44

struct GfUser {
  char name[GF_NAME_LEN + 1];
  uint8_t score;
  uint32_t last_s;
  uint8_t turns;
  char last_clean[48];
  const char* last_reply;
  char mem[GF_ECHO_MAX + 1];
  char mem_kind;
};

class GfBot {
  uint32_t rs;
  uint8_t rot[GF_NRULES];
  uint8_t rot_fb, rot_rep, rot_mem, rot_gr;
  GfUser users[GF_MAX_USERS];

  uint32_t rnd(uint32_t n) {   // xorshift32
    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
    return n ? rs % n : 0;
  }

  static bool isWordCh(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

  // one word -> its cleaned form (contractions without the apostrophe, slang)
  static const char* fixWord(const char* w) {
    static const char* const MAP[][2] = {
      {"im", "i am"}, {"ive", "i have"}, {"youre", "you are"}, {"theyre", "they are"}, {"cant", "cannot"},
      {"wont", "will not"}, {"dont", "do not"}, {"didnt", "did not"}, {"isnt", "is not"}, {"arent", "are not"},
      {"wasnt", "was not"}, {"whats", "what is"}, {"thats", "that is"}, {"hows", "how is"}, {"whos", "who is"},
      {"lets", "let us"}, {"gonna", "going to"}, {"wanna", "want to"}, {"gotta", "got to"}, {"u", "you"},
      {"ur", "your"}, {"r", "are"}, {"pls", "please"}, {"plz", "please"}, {"thx", "thanks"}, {"ty", "thanks"},
      {"yeh", "yeah"}, {"cuz", "because"}, {"gnight", "good night"}, {"goodnite", "good night"}, {"nite", "night"},
      {"mornin", "morning"}, {"hiya", "hiya"}, {"whatsup", "what is up"}, {"sup", "sup"}, {"xx", "kiss"},
    };
    for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++) {
      if (strcmp(w, MAP[i][0]) == 0) return MAP[i][1];
    }
    return w;
  }

  // lowercase, drop punctuation and non-ASCII (emoji), fix contractions. Apostrophes vanish, hyphens become spaces.
  static void clean(const char* in, char* out, int max) {
    char w[20]; int wl = 0; int ol = 0;
    for (const char* p = in;; p++) {
      char c = *p;
      if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
      bool ch = isWordCh(c);
      if (ch) {
        if (wl < 18) w[wl++] = c;
        continue;
      }
      if (c == '\'') continue;   // apostrophes vanish: I'm -> im
      if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 && ((unsigned char)p[2] == 0x99 || (unsigned char)p[2] == 0x98)) {
        p += 2;   // curly apostrophe (3 bytes) vanishes too
        continue;
      }
      if (wl > 0) {
        w[wl] = 0;
        const char* f = fixWord(w);
        int fl = (int)strlen(f);
        if (ol > 0 && ol < max - 1) out[ol++] = ' ';
        for (int i = 0; i < fl && ol < max - 1; i++) out[ol++] = f[i];
        wl = 0;
      }
      if (c == 0) break;
    }
    out[ol] = 0;
  }

  // find key in padded text (" word word "); returns pointer just after the match (a space or the end), or NULL
  static const char* findKey(const char* padded, const char* key, int klen, bool prefix) {
    char pat[48];
    if (klen > 40) return NULL;
    pat[0] = ' ';
    memcpy(&pat[1], key, klen);
    int pl = 1 + klen;
    if (!prefix) pat[pl++] = ' ';
    pat[pl] = 0;
    const char* m = strstr(padded, pat);
    if (m == NULL) return NULL;
    const char* e = m + pl;
    if (prefix) { while (*e && *e != ' ') e++; }   // finish the word
    else e--;                                      // leave the closing space for the echo
    return e;
  }

  // does a rule match? returns the length of the longest matching key (0 = no) and where the echo starts
  static int matchRule(const GfRule& r, const char* padded, const char** echo_at) {
    int best = 0;
    const char* k = r.keys;
    while (*k) {
      const char* bar = strchr(k, '|');
      int kl = bar ? (int)(bar - k) : (int)strlen(k);
      bool prefix = (kl > 0 && k[kl - 1] == '~');
      int core = prefix ? kl - 1 : kl;
      if (core > 0) {
        const char* e = findKey(padded, k, core, prefix);
        if (e != NULL && core > best) { best = core; *echo_at = e; }
      }
      if (!bar) break;
      k = bar + 1;
    }
    return best;
  }

  // what was said after the keyword, with I/you swapped
  static void swapEcho(const char* in, char* out, int max) {
    while (*in == ' ') in++;
    char words[14][16]; int n = 0;
    while (*in && n < 14) {
      int l = 0;
      while (*in && *in != ' ' && l < 15) words[n][l++] = *in++;
      words[n][l] = 0;
      while (*in && *in != ' ') in++;
      while (*in == ' ') in++;
      n++;
    }
    int ol = 0; out[0] = 0;
    bool prev_i = false;
    for (int i = 0; i < n; i++) {
      const char* w = words[i];
      const char* nx = (i + 1 < n) ? words[i + 1] : "";
      const char* o = w; bool this_i = false;
      if (!strcmp(w, "i")) { o = "you"; this_i = true; }
      else if (!strcmp(w, "me")) o = "you";
      else if (!strcmp(w, "my")) o = "your";
      else if (!strcmp(w, "mine")) o = "yours";
      else if (!strcmp(w, "myself")) o = "yourself";
      else if (!strcmp(w, "your")) o = "my";
      else if (!strcmp(w, "yours")) o = "mine";
      else if (!strcmp(w, "yourself")) o = "myself";
      else if (!strcmp(w, "am") && prev_i) o = "are";
      else if (!strcmp(w, "was") && prev_i) o = "were";
      else if (!strcmp(w, "you")) {
        bool subj = !strcmp(nx, "are") || !strcmp(nx, "will") || !strcmp(nx, "can") || !strcmp(nx, "do") ||
                    !strcmp(nx, "did") || !strcmp(nx, "have") || !strcmp(nx, "would") || !strcmp(nx, "could") ||
                    !strcmp(nx, "want") || !strcmp(nx, "need") || !strcmp(nx, "like") || !strcmp(nx, "love");
        o = subj ? "I" : "me";
      }
      else if (!strcmp(w, "are") && i > 0 && !strcmp(words[i - 1], "you")) o = "am";
      prev_i = this_i;
      int l = (int)strlen(o);
      if (ol + l + (ol ? 1 : 0) > max) break;
      if (ol) out[ol++] = ' ';
      memcpy(&out[ol], o, l); ol += l;
    }
    out[ol] = 0;
  }

  GfUser* findUser(const char* name, uint32_t now_s) {
    GfUser* oldest = &users[0];
    for (int i = 0; i < GF_MAX_USERS; i++) {
      if (users[i].name[0] && strcmp(users[i].name, name) == 0) return &users[i];
      if (users[i].name[0] == 0) { oldest = &users[i]; break; }
      if (users[i].last_s < oldest->last_s) oldest = &users[i];
    }
    GfUser* u = oldest;
    memset(u, 0, sizeof(*u));
    strncpy(u->name, name, GF_NAME_LEN);
    u->last_s = now_s;
    return u;
  }

  static int tierFor(int score) {
    return score >= GF_TIER3_AT ? 3 : score >= GF_TIER2_AT ? 2 : score >= GF_TIER1_AT ? 1 : 0;
  }

  const char* pet(int tier) { return GF_PETS[tier][rnd(6)]; }

  // fill a template: @ name, ^ pet name, $ echo
  void fill(const char* t, const char* name, const char* echo, int tier, char* out, int cap) {
    int ol = 0;
    for (; *t && ol < cap - 1; t++) {
      const char* ins = NULL;
      if (*t == '@') ins = name[0] ? name : "you";
      else if (*t == '^') ins = pet(tier);
      else if (*t == '$') ins = echo;
      if (ins) {
        for (; *ins && ol < cap - 1; ins++) out[ol++] = *ins;
      } else out[ol++] = *t;
    }
    out[ol] = 0;
  }

  static bool hasEcho(const char* t) { return strchr(t, '$') != NULL; }

  // choose a reply from a list for this tier (falling back to lower tiers); NULL if none usable
  const GfReply* choose(const GfReply* list, int n, int tier, bool have_echo, uint8_t* counter, const char* avoid) {
    for (int t = tier; t >= 0; t--) {
      int idx[48]; int c = 0;
      for (int i = 0; i < n && c < 48; i++) {
        if (list[i].tier != t) continue;
        if (!have_echo && hasEcho(list[i].text)) continue;
        idx[c++] = i;
      }
      if (c == 0) continue;
      int k = (*counter)++ % c;
      if (c > 1 && avoid && list[idx[k]].text == avoid) k = (k + 1) % c;
      return &list[idx[k]];
    }
    return NULL;
  }

public:
  void seed(uint32_t s) {
    rs = s ? s : 0x9E3779B9u;
    memset(users, 0, sizeof(users));
    for (int i = 0; i < GF_NRULES; i++) rot[i] = (uint8_t)rnd(250);
    rot_fb = (uint8_t)rnd(250); rot_rep = rot_mem = rot_gr = 0;
  }

  // Returns the reply length written to out (at most max bytes, no terminator counted), or 0 for no reply.
  //   sender: the name before the ':'     msg: the text after the '*'     now_s: seconds (any steady clock)
  //   hour: local hour 0..23, or -1 when the clock is not set
  int reply(const char* sender, const char* msg, uint32_t now_s, int hour, char* out, int max) {
    if (max < 16) return 0;
    char cl[GF_CLEAN_MAX + 2];
    char tmp[GF_CLEAN_MAX + 1];
    strncpy(tmp, msg, GF_CLEAN_MAX); tmp[GF_CLEAN_MAX] = 0;
    clean(tmp, cl, GF_CLEAN_MAX);
    if (cl[0] == 0) return 0;   // nothing to answer (just '*' or only emoji)

    // sender name: cut to GF_NAME_LEN bytes without splitting a UTF-8 character; never start with ! * @
    char name[GF_NAME_LEN + 1]; int nl = 0;
    const char* s = sender;
    while (*s == '!' || *s == '*' || *s == '@' || *s == ' ') s++;
    while (*s && nl < GF_NAME_LEN) name[nl++] = *s++;
    if (*s && ((unsigned char)*s & 0xC0) == 0x80) {   // the cut would split a UTF-8 character: drop the partial one
      while (nl > 0 && ((unsigned char)name[nl - 1] & 0xC0) == 0x80) nl--;
      if (nl > 0 && ((unsigned char)name[nl - 1] & 0xC0) == 0xC0) nl--;
    }
    while (nl > 0 && name[nl - 1] == ' ') nl--;
    name[nl] = 0;

    GfUser* u = findUser(name[0] ? name : "?", now_s);
    // cool down: a new session after 20 minutes, a little less heat for every 2 minutes away
    if (u->turns > 0) {
      uint32_t away = now_s - u->last_s;
      if (away >= GF_SESSION_S) u->score = (uint8_t)(u->score / 3);
      else { uint32_t d = away / 120; u->score = (uint8_t)(u->score > d ? u->score - d : 0); }
    }
    u->last_s = now_s;
    if (u->turns < 255) u->turns++;

    char padded[GF_CLEAN_MAX + 4];
    snprintf(padded, sizeof(padded), " %s ", cl);

    // same message twice in a row
    bool repeat = (strncmp(u->last_clean, cl, sizeof(u->last_clean) - 1) == 0);
    strncpy(u->last_clean, cl, sizeof(u->last_clean) - 1);
    u->last_clean[sizeof(u->last_clean) - 1] = 0;

    // best rule
    int best_ri = -1, best_score = 0; const char* best_echo = NULL;
    for (int i = 0; i < GF_NRULES; i++) {
      const char* e = NULL;
      int kl = matchRule(GF_RULES[i], padded, &e);
      if (kl == 0) continue;
      int sc = GF_RULES[i].pri * 100 + kl;
      if (sc > best_score) { best_score = sc; best_ri = i; best_echo = e; }
    }

    // heat
    int add = 6 + (best_ri >= 0 ? GF_RULES[best_ri].heat : 0);
    if (repeat) add = 1;
    int sc2 = (int)u->score + add;
    u->score = (uint8_t)(sc2 > 255 ? 255 : sc2);
    int tier = tierFor(u->score);

    char echo[GF_ECHO_MAX + 1]; echo[0] = 0;
    if (best_ri >= 0 && best_echo) swapEcho(best_echo, echo, GF_ECHO_MAX);
    bool have_echo = echo[0] != 0;

    char line[GF_CLEAN_MAX + 80]; line[0] = 0;
    const GfReply* pick = NULL;

    if (repeat) {
      pick = choose(GR_repeat, sizeof(GR_repeat) / sizeof(GR_repeat[0]), tier, false, &rot_rep, u->last_reply);
    } else if (best_ri >= 0) {
      const GfRule& r = GF_RULES[best_ri];
      if ((r.flags & GF_F_GREET) && hour >= 0 && tier <= 1 && rnd(2) == 0) {   // a time-of-day greeting
        const char* const* set = NULL;
        if (hour >= 22 || hour < 5) set = GF_GREET_LATE;
        else if (hour < 11) set = GF_GREET_MORNING;
        else if (hour >= 17) set = GF_GREET_EVENING;
        if (set) { fill(set[rnd(GF_N_GREET)], name, echo, tier, line, sizeof(line)); u->last_reply = NULL; }
      }
      if (line[0] == 0) pick = choose(r.r, r.n, tier, have_echo, &rot[best_ri], u->last_reply);
      if ((r.flags & GF_F_MEM) && have_echo) {   // remember what they said
        strncpy(u->mem, echo, GF_ECHO_MAX); u->mem[GF_ECHO_MAX] = 0;
        u->mem_kind = memKind(best_ri);
      }
    }

    if (line[0] == 0 && pick == NULL) {   // nothing matched: sometimes go back to something they said earlier
      if (u->mem[0] && u->mem_kind && rnd(3) != 0) {
        const char* const* set = memSet(u->mem_kind);
        if (set) { fill(set[rot_mem++ % 2], name, u->mem, tier, line, sizeof(line)); u->mem[0] = 0; u->last_reply = NULL; }
      }
      if (line[0] == 0) pick = choose(GR_fallback, sizeof(GR_fallback) / sizeof(GR_fallback[0]), tier, false, &rot_fb, u->last_reply);
    }
    if (line[0] == 0 && pick) { fill(pick->text, name, echo, tier, line, sizeof(line)); u->last_reply = pick->text; }
    if (line[0] == 0) return 0;

    // make it fit; never start with ! or *
    int len = (int)strlen(line);
    if (len > max) {
      int cut = max;
      while (cut > 0 && ((unsigned char)line[cut] & 0xC0) == 0x80) cut--;
      int sp = cut;
      while (sp > cut - 25 && sp > 10 && line[sp] != ' ') sp--;
      if (line[sp] == ' ') cut = sp;
      len = cut;
    }
    while (len > 0 && line[len - 1] == ' ') len--;
    int st = 0;
    while (st < len && (line[st] == '!' || line[st] == '*' || line[st] == ' ')) st++;
    len -= st;
    if (len <= 0) return 0;
    memcpy(out, line + st, len);
    out[len] = 0;
    return len;
  }

  // which kind of memory a rule creates
  static char memKind(int ri) {
    const GfReply* r = GF_RULES[ri].r;
    if (r == GR_iam) return GF_MEM_ME;
    if (r == GR_ifeel) return GF_MEM_FEEL;
    if (r == GR_iwant) return GF_MEM_WANT;
    if (r == GR_ineed) return GF_MEM_NEED;
    if (r == GR_my) return GF_MEM_MY;
    return 0;
  }
  static const char* const* memSet(char kind) {
    switch (kind) {
      case GF_MEM_ME: return GF_MEM_ME_R;
      case GF_MEM_FEEL: return GF_MEM_FEEL_R;
      case GF_MEM_WANT: return GF_MEM_WANT_R;
      case GF_MEM_NEED: return GF_MEM_NEED_R;
      case GF_MEM_MY: return GF_MEM_MY_R;
    }
    return NULL;
  }

  // for the PC test
  int heatOf(const char* name) {
    for (int i = 0; i < GF_MAX_USERS; i++) if (users[i].name[0] && strcmp(users[i].name, name) == 0) return users[i].score;
    return -1;
  }
};
