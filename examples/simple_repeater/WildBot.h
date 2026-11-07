#pragma once
/*
 * Wildlife lookups for the hike bot: local animals, plants and a short field guide (see wildlife.h).
 *
 *   !koala              one species (or any alias), exactly like !<trail>
 *   !mammals, !frogs    the species in a category
 *   !birds              the categories in a group
 *   !animals, !plants   the categories (and child groups) of a top level group
 *   !wildlife           the top level groups
 *
 * Nothing here uses RAM beyond a few small stack buffers: all the text is const in flash.
 * Lookup order for an exact "!" command is: trail, town or region (TrailBot.h), species, category, group.
 */

#include <stdio.h>
#include <string.h>
#include "wildlife.h"
#include "TrailBot.h"

// ---------------------------------------------------------------------------------------------------------------
// exact lookups (the query is already normalised with trailbotNorm)

static int wildFindSpecies(const char* q) {
  for (int i = 0; i < WILD_COUNT; i++) {
    if (trailbotMatches(WILDLIFE[i].name, WILDLIFE[i].aliases, q)) return i;
  }
  return -1;
}

static int wildFindCat(const char* q) {
  for (int i = 0; i < WILD_CAT_COUNT; i++) {
    if (trailbotMatches(WILD_CATS[i].name, WILD_CATS[i].aliases, q)) return i;
  }
  return -1;
}

static int wildFindGroup(const char* q) {
  for (int i = 0; i < WILD_GROUP_COUNT; i++) {
    if (trailbotMatches(WILD_GROUPS[i].name, WILD_GROUPS[i].aliases, q)) return i;
  }
  return -1;
}

// the species of a category
static int wildCatSpecies(int cat, const char** names, int max) {
  int n = 0;
  for (int i = 0; i < WILD_COUNT && n < max; i++) {
    if (WILDLIFE[i].cat == cat) names[n++] = WILDLIFE[i].name;
  }
  return n;
}

// the categories of a group, then the child groups of that group (so !animals ends with !Birds)
static int wildGroupItems(int group, const char** names, int max) {
  int n = 0;
  for (int i = 0; i < WILD_CAT_COUNT && n < max; i++) {
    if (WILD_CATS[i].group == group) names[n++] = WILD_CATS[i].name;
  }
  for (int i = 0; i < WILD_GROUP_COUNT && n < max; i++) {
    if (WILD_GROUPS[i].parent == group) names[n++] = WILD_GROUPS[i].name;
  }
  return n;
}

// "!wildlife" and friends: the top level groups
static bool wildIsTop(const char* q) {
  return strcmp(q, "wildlife") == 0 || strcmp(q, "nature") == 0 || strcmp(q, "wildlife guide") == 0 ||
         strcmp(q, "wildlife library") == 0 || strcmp(q, "nature guide") == 0;
}

// the top level groups (!wildlife)
static int wildTopGroups(const char** names, int max) {
  int n = 0;
  for (int i = 0; i < WILD_GROUP_COUNT && n < max; i++) {
    if (WILD_GROUPS[i].parent < 0) names[n++] = WILD_GROUPS[i].name;
  }
  return n;
}

// ---------------------------------------------------------------------------------------------------------------
// a name said anywhere in a sentence

// is the normalised key present in the padded sentence as whole words (also with an added "s": koala -> koalas)?
static bool wildHasKey(const char* padded, const char* key) {
  int len = (int)strlen(key);
  if (len < 3 || len > 44) return false;
  char pk[48];
  pk[0] = ' ';
  memcpy(&pk[1], key, len);
  pk[len + 1] = ' ';
  pk[len + 2] = 0;
  if (strstr(padded, pk)) return true;
  if (key[len - 1] != 's') {
    pk[len + 1] = 's';
    pk[len + 2] = ' ';
    pk[len + 3] = 0;
    if (strstr(padded, pk)) return true;
  }
  return false;
}

// the longest name or alias (normalised) of this entry found in the sentence, 0 if none
static int wildBestKey(const char* padded, const char* name, const char* aliases) {
  char tmp[TRAILBOT_QUERY_MAX + 8];
  int best = 0;
  trailbotNorm(name, tmp, sizeof(tmp));
  if (wildHasKey(padded, tmp)) best = (int)strlen(tmp);
  const char* p = aliases;
  while (p && *p) {
    const char* e = strchr(p, '|');
    int len = e ? (int)(e - p) : (int)strlen(p);
    if (*p != '=') {   // '=alias' is for the exact "!alias" command only, never matched inside a sentence
      char seg[TRAILBOT_QUERY_MAX + 8];
      if (len > (int)sizeof(seg) - 1) len = (int)sizeof(seg) - 1;
      memcpy(seg, p, len);
      seg[len] = 0;
      trailbotNorm(seg, tmp, sizeof(tmp));
      int tl = (int)strlen(tmp);
      if (tl > best && wildHasKey(padded, tmp)) best = tl;
    }
    p = e ? e + 1 : NULL;
  }
  return best;
}

// the species named in the sentence (the longest name wins), -1 if none
static int wildMentionSpecies(const char* padded) {
  int best = -1, best_len = 0;
  for (int i = 0; i < WILD_COUNT; i++) {
    int l = wildBestKey(padded, WILDLIFE[i].name, WILDLIFE[i].aliases);
    if (l > best_len) { best_len = l; best = i; }
  }
  return best;
}

// a category or group named in the sentence. kind 1 = category, 2 = group. false if none.
static bool wildMentionList(const char* padded, int* kind, int* idx) {
  int best_len = 0;
  *kind = 0;
  *idx = -1;
  for (int i = 0; i < WILD_CAT_COUNT; i++) {
    int l = wildBestKey(padded, WILD_CATS[i].name, WILD_CATS[i].aliases);
    if (l > best_len) { best_len = l; *kind = 1; *idx = i; }
  }
  for (int i = 0; i < WILD_GROUP_COUNT; i++) {
    int l = wildBestKey(padded, WILD_GROUPS[i].name, WILD_GROUPS[i].aliases);
    if (l > best_len) { best_len = l; *kind = 2; *idx = i; }
  }
  return *idx >= 0;
}
