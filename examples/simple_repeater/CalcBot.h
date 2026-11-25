// CalcBot.h - calculator, unit conversions and hiking maths for TrailBot.
//
// Pure functions with no mesh, radio or Arduino calls (it only needs stdio, string and math), so it can be tested on a computer.
// Used by HikeBot.h (the bot channel, in normal conversation) and by the calcchan channel (calculator only).
//
//   calcAnswer()      one call for both channels: unit conversion first, then the expression calculator
//   calcHikeParse()   "8km and climbs 300m" -> distance, climb, descent, one way / return
//   calcNaismithMin() minutes for that walk (Naismith's rule, rough)
//
// Safety: every buffer is fixed size and static, the input is capped at CALC_BUF characters, the parser is recursive but its
// depth is capped at CALC_DEPTH_MAX, and nothing is allocated. Numbers are printed by our own code (no %f / %g), because the
// small printf of the nRF52 build may not support floating point.
//
// Trig functions work in DEGREES: sin(30) = 0.5. Use rad(x) / deg(x) to convert.
#ifndef CALCBOT_H
#define CALCBOT_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define CALC_BUF        152     // longest input looked at (longer is ignored on the bot channel, "Err - too long" on calcchan)
#define CALC_DEPTH_MAX  8       // deepest nesting of brackets / unary signs / functions
#define CALC_TOK_MAX    28      // most words and numbers looked at in a conversion
#define CALC_ANS_MAX    8       // people whose last answer ("ans") is remembered
#define CALC_RAD        0.017453292519943295

#if defined(__GNUC__)
  #define CALC_NOINLINE __attribute__((noinline))
#else
  #define CALC_NOINLINE
#endif

static inline bool calcIsDig(char c) { return c >= '0' && c <= '9'; }
static inline bool calcIsAlpha(char c) { return c >= 'a' && c <= 'z'; }

// ---------------------------------------------------------------------------------------------------------------
// printing a number without printf floating point

// v printed with at most `sig` significant digits, no trailing zeros, no exponent unless very big or very small
static CALC_NOINLINE void calcFmt(double v, int sig, char* out, int osz) {
  if (osz < 2) return;
  if (v != v) { snprintf(out, osz, "nan"); return; }
  bool neg = v < 0;
  if (neg) v = -v;
  if (v > 1e300) { snprintf(out, osz, "%sinf", neg ? "-" : ""); return; }
  if (v == 0) { snprintf(out, osz, "0"); return; }
  if (sig < 1) sig = 1;
  if (sig > 15) sig = 15;
  char buf[48];
  int p = 0;
  if (neg) buf[p++] = '-';
  if (v < 9e15 && v == (double)(unsigned long long)v) {   // a whole number: print every digit
    unsigned long long w = (unsigned long long)v;
    char d[24];
    int dn = 0;
    while (w > 0) { d[dn++] = (char)('0' + (int)(w % 10ULL)); w /= 10ULL; }
    while (dn > 0) buf[p++] = d[--dn];
    buf[p] = 0;
    snprintf(out, osz, "%s", buf);
    return;
  }
  int e10 = 0;
  double t = v;
  while (t >= 10.0) { t /= 10.0; e10++; }
  while (t < 1.0) { t *= 10.0; e10--; }
  unsigned long long scale = 1ULL;
  for (int i = 1; i < sig; i++) scale *= 10ULL;
  unsigned long long m = (unsigned long long)(t * (double)scale + 0.5);
  if (m >= scale * 10ULL) { m /= 10ULL; e10++; }
  char digs[20];
  for (int i = sig - 1; i >= 0; i--) { digs[i] = (char)('0' + (int)(m % 10ULL)); m /= 10ULL; }
  int len = sig;
  while (len > 1 && digs[len - 1] == '0') len--;
  if (e10 >= 15 || e10 < -5) {   // scientific: d.ddde+X
    buf[p++] = digs[0];
    if (len > 1) { buf[p++] = '.'; for (int i = 1; i < len; i++) buf[p++] = digs[i]; }
    buf[p++] = 'e';
    int ae = e10;
    if (ae < 0) { buf[p++] = '-'; ae = -ae; } else buf[p++] = '+';
    if (ae >= 100) buf[p++] = (char)('0' + ae / 100);
    buf[p++] = (char)('0' + (ae / 10) % 10);
    buf[p++] = (char)('0' + ae % 10);
  } else if (e10 >= 0) {
    int intd = e10 + 1;
    for (int i = 0; i < intd; i++) buf[p++] = i < len ? digs[i] : '0';
    if (len > intd) { buf[p++] = '.'; for (int i = intd; i < len; i++) buf[p++] = digs[i]; }
  } else {
    buf[p++] = '0';
    buf[p++] = '.';
    for (int i = 0; i < -e10 - 1; i++) buf[p++] = '0';
    for (int i = 0; i < len; i++) buf[p++] = digs[i];
  }
  buf[p] = 0;
  snprintf(out, osz, "%s", buf);
}

// ---------------------------------------------------------------------------------------------------------------
// "ans": the last answer of each person (separate from the hike memory, so calcchan needs no memory slot either)

static uint16_t g_calc_ans_id[CALC_ANS_MAX];
static double g_calc_ans_v[CALC_ANS_MAX];
static uint8_t g_calc_ans_next = 0;

static bool calcAnsGet(uint16_t who, double* v) {
  if (who == 0) return false;
  for (int i = 0; i < CALC_ANS_MAX; i++) {
    if (g_calc_ans_id[i] == who) { *v = g_calc_ans_v[i]; return true; }
  }
  return false;
}

static void calcAnsSet(uint16_t who, double v) {
  if (who == 0) return;
  for (int i = 0; i < CALC_ANS_MAX; i++) {
    if (g_calc_ans_id[i] == who) { g_calc_ans_v[i] = v; return; }
  }
  g_calc_ans_id[g_calc_ans_next] = who;
  g_calc_ans_v[g_calc_ans_next] = v;
  g_calc_ans_next = (uint8_t)((g_calc_ans_next + 1) % CALC_ANS_MAX);
}

// ---------------------------------------------------------------------------------------------------------------
// the expression parser: recursive descent over a lower case string, floating point (double)
//   expr  = term { (+|-) term }
//   term  = unary { (*|/|%) unary }          % followed by an operand is modulo, otherwise (50%) it is "divide by 100"
//   unary = (-|+) unary | power
//   power = postfix [ ^ unary ]              right to left, so 2^3^2 = 2^9 and -2^2 = -4
//   post  = primary [ % ]
//   prim  = number | ( expr ) | constant | function ( args ) | function operand

struct CalcP {
  const char* s;
  int pos;
  int depth;
  const char* err;       // first error (short text), NULL = none
  bool syntax;           // that error is a syntax error (not a maths error like divide by 0)
  bool has_ans;
  double ans;
};
static CalcP g_cp;

static void calcFail(const char* e, bool syntax) {
  if (!g_cp.err) { g_cp.err = e; g_cp.syntax = syntax; }
}

static void calcSkip() { while (g_cp.s[g_cp.pos] == ' ') g_cp.pos++; }
static char calcPeek() { calcSkip(); return g_cp.s[g_cp.pos]; }
static bool calcOkNum(double v) { return v == v && v < 1e300 && v > -1e300; }

static double calcExpr();

static double calcSnap(double r) { return (r < 1e-12 && r > -1e-12) ? 0.0 : r; }
static double calcSinD(double d) {
  d = fmod(d, 360.0);
  if (d < 0) d += 360.0;
  if (d == 0 || d == 180.0) return 0.0;
  if (d == 90.0) return 1.0;
  if (d == 270.0) return -1.0;
  return calcSnap(sin(d * CALC_RAD));
}
static double calcCosD(double d) {
  d = fmod(d, 360.0);
  if (d < 0) d += 360.0;
  if (d == 90.0 || d == 270.0) return 0.0;
  if (d == 0) return 1.0;
  if (d == 180.0) return -1.0;
  return calcSnap(cos(d * CALC_RAD));
}

// a number: 12, 3.5, .5, 2e3, 1.5e-3
static double calcNumber() {
  const char* s = g_cp.s;
  int i = g_cp.pos;
  double v = 0;
  int nd = 0;
  while (calcIsDig(s[i])) { v = v * 10.0 + (s[i] - '0'); i++; nd++; }
  if (s[i] == '.') {
    i++;
    double div = 1.0, f = 0;
    int fd = 0;
    while (calcIsDig(s[i])) {
      if (fd < 17) { f = f * 10.0 + (s[i] - '0'); div *= 10.0; fd++; }
      i++; nd++;
    }
    v += f / div;
  }
  if (nd == 0) { calcFail("bad number", true); return 0; }
  if (s[i] == 'e' && (calcIsDig(s[i + 1]) || ((s[i + 1] == '-' || s[i + 1] == '+') && calcIsDig(s[i + 2])))) {
    i++;
    bool eneg = false;
    if (s[i] == '-') { eneg = true; i++; } else if (s[i] == '+') i++;
    int ex = 0;
    while (calcIsDig(s[i])) { if (ex < 400) ex = ex * 10 + (s[i] - '0'); i++; }
    if (ex > 300) { calcFail("too big", false); g_cp.pos = i; return 0; }
    double sc = 1.0;
    for (int k = 0; k < ex; k++) sc *= 10.0;
    v = eneg ? v / sc : v * sc;
  }
  g_cp.pos = i;
  if (!calcOkNum(v)) { calcFail("too big", false); return 0; }
  return v;
}

struct CalcFn { const char* name; uint8_t args; };
static const CalcFn CALC_FNS[] = {
  {"sqrt", 1}, {"cbrt", 1}, {"abs", 1}, {"sin", 1}, {"cos", 1}, {"tan", 1}, {"asin", 1}, {"acos", 1}, {"atan", 1},
  {"ln", 1}, {"log", 1}, {"log10", 1}, {"log2", 1}, {"exp", 1}, {"floor", 1}, {"ceil", 1}, {"round", 1}, {"fact", 1},
  {"rad", 1}, {"deg", 1}, {"pow", 2}, {"min", 2}, {"max", 2}, {"mod", 2},
};
#define CALC_NFNS ((int)(sizeof(CALC_FNS) / sizeof(CALC_FNS[0])))

static int calcFnFind(const char* w, int l) {
  for (int i = 0; i < CALC_NFNS; i++) {
    if ((int)strlen(CALC_FNS[i].name) == l && memcmp(CALC_FNS[i].name, w, l) == 0) return i;
  }
  return -1;
}

static bool calcIsConst(const char* w, int l) {
  return (l == 2 && memcmp(w, "pi", 2) == 0) || (l == 1 && w[0] == 'e') || (l == 3 && memcmp(w, "tau", 3) == 0) ||
         (l == 3 && memcmp(w, "ans", 3) == 0);
}

static double calcApply(int fi, double a, double b) {
  const char* n = CALC_FNS[fi].name;
  double r = 0;
  if (strcmp(n, "sqrt") == 0) { if (a < 0) { calcFail("no result", false); return 0; } r = sqrt(a); }
  else if (strcmp(n, "cbrt") == 0) r = cbrt(a);
  else if (strcmp(n, "abs") == 0) r = a < 0 ? -a : a;
  else if (strcmp(n, "sin") == 0) r = calcSinD(a);
  else if (strcmp(n, "cos") == 0) r = calcCosD(a);
  else if (strcmp(n, "tan") == 0) {
    double c = calcCosD(a);
    if (c == 0) { calcFail("no result", false); return 0; }
    r = calcSinD(a) / c;
  }
  else if (strcmp(n, "asin") == 0) { if (a < -1 || a > 1) { calcFail("no result", false); return 0; } r = asin(a) / CALC_RAD; }
  else if (strcmp(n, "acos") == 0) { if (a < -1 || a > 1) { calcFail("no result", false); return 0; } r = acos(a) / CALC_RAD; }
  else if (strcmp(n, "atan") == 0) r = atan(a) / CALC_RAD;
  else if (strcmp(n, "ln") == 0) { if (a <= 0) { calcFail("no result", false); return 0; } r = log(a); }
  else if (strcmp(n, "log") == 0 || strcmp(n, "log10") == 0) { if (a <= 0) { calcFail("no result", false); return 0; } r = log10(a); }
  else if (strcmp(n, "log2") == 0) { if (a <= 0) { calcFail("no result", false); return 0; } r = log(a) / log(2.0); }
  else if (strcmp(n, "exp") == 0) { if (a > 700) { calcFail("too big", false); return 0; } r = exp(a); }
  else if (strcmp(n, "floor") == 0) r = floor(a);
  else if (strcmp(n, "ceil") == 0) r = ceil(a);
  else if (strcmp(n, "round") == 0) r = floor(a + 0.5);
  else if (strcmp(n, "fact") == 0) {
    if (a < 0 || a > 170 || a != floor(a)) { calcFail("no result", false); return 0; }
    r = 1;
    for (int k = 2; k <= (int)a; k++) r *= k;
  }
  else if (strcmp(n, "rad") == 0) r = a * CALC_RAD;
  else if (strcmp(n, "deg") == 0) r = a / CALC_RAD;
  else if (strcmp(n, "pow") == 0) {
    if (a == 0 && b < 0) { calcFail("divide by 0", false); return 0; }
    if (a < 0 && b != floor(b)) { calcFail("no result", false); return 0; }
    r = pow(a, b);
  }
  else if (strcmp(n, "min") == 0) r = a < b ? a : b;
  else if (strcmp(n, "max") == 0) r = a > b ? a : b;
  else if (strcmp(n, "mod") == 0) { if (b == 0) { calcFail("divide by 0", false); return 0; } r = fmod(a, b); }
  if (!calcOkNum(r)) { calcFail("too big", false); return 0; }
  return r;
}

static double calcUnary();

static double calcPrimary() {
  if (g_cp.err) return 0;
  char c = calcPeek();
  if (calcIsDig(c) || c == '.') return calcNumber();
  if (c == '(') {
    g_cp.pos++;
    if (++g_cp.depth > CALC_DEPTH_MAX) { calcFail("too deep", true); return 0; }
    double v = calcExpr();
    g_cp.depth--;
    if (g_cp.err) return 0;
    if (calcPeek() != ')') { calcFail("missing )", true); return 0; }
    g_cp.pos++;
    return v;
  }
  if (calcIsAlpha(c)) {
    const char* s = g_cp.s;
    int i = g_cp.pos;
    int st = i;
    while (calcIsAlpha(s[i]) || (i > st && calcIsDig(s[i]))) i++;
    int l = i - st;
    const char* w = s + st;
    int fi = calcFnFind(w, l);
    if (fi >= 0) {
      g_cp.pos = i;
      if (++g_cp.depth > CALC_DEPTH_MAX) { calcFail("too deep", true); return 0; }
      double a = 0, b = 0;
      if (calcPeek() == '(') {
        g_cp.pos++;
        a = calcExpr();
        if (CALC_FNS[fi].args == 2) {
          if (calcPeek() != ',') { if (!g_cp.err) calcFail("needs 2 values", true); return 0; }
          g_cp.pos++;
          b = calcExpr();
        }
        if (g_cp.err) return 0;
        if (calcPeek() != ')') { calcFail("missing )", true); return 0; }
        g_cp.pos++;
      } else {
        if (CALC_FNS[fi].args == 2) { calcFail("needs ( )", true); return 0; }
        a = calcPrimary();   // "sin 30", "sqrt 16"
      }
      g_cp.depth--;
      if (g_cp.err) return 0;
      return calcApply(fi, a, b);
    }
    if (calcIsConst(w, l)) {
      g_cp.pos = i;
      if (l == 2) return 3.14159265358979323846;
      if (l == 1) return 2.71828182845904523536;
      if (w[0] == 't') return 6.28318530717958647692;
      if (!g_cp.has_ans) { calcFail("no previous answer", false); return 0; }
      return g_cp.ans;
    }
    calcFail("unknown word", true);
    return 0;
  }
  calcFail("bad expression", true);
  return 0;
}

// is the character after a '%' (skipping spaces) the start of an operand? then % is modulo
static bool calcOperandNext(int pos) {
  const char* s = g_cp.s;
  while (s[pos] == ' ') pos++;
  char c = s[pos];
  return calcIsDig(c) || c == '.' || c == '(' || calcIsAlpha(c);
}

static double calcPostfix() {
  double v = calcPrimary();
  if (g_cp.err) return 0;
  if (calcPeek() == '%' && !calcOperandNext(g_cp.pos + 1)) {
    g_cp.pos++;
    v /= 100.0;
  }
  return v;
}

static double calcPower() {
  double a = calcPostfix();
  if (g_cp.err) return 0;
  char c = calcPeek();
  bool dbl = (c == '*' && g_cp.s[g_cp.pos + 1] == '*');
  if (c == '^' || dbl) {
    g_cp.pos += dbl ? 2 : 1;
    double b = calcUnary();
    if (g_cp.err) return 0;
    if (a == 0 && b < 0) { calcFail("divide by 0", false); return 0; }
    if (a < 0 && b != floor(b)) { calcFail("no result", false); return 0; }
    double r = pow(a, b);
    if (!calcOkNum(r)) { calcFail("too big", false); return 0; }
    return r;
  }
  return a;
}

static double calcUnary() {
  if (g_cp.err) return 0;
  char c = calcPeek();
  if (c == '-' || c == '+') {
    g_cp.pos++;
    if (++g_cp.depth > CALC_DEPTH_MAX) { calcFail("too deep", true); return 0; }
    double v = calcUnary();
    g_cp.depth--;
    return c == '-' ? -v : v;
  }
  return calcPower();
}

static double calcTerm() {
  double v = calcUnary();
  while (!g_cp.err) {
    char c = calcPeek();
    if (c == '*' && g_cp.s[g_cp.pos + 1] != '*') {
      g_cp.pos++;
      double b = calcUnary();
      v *= b;
    } else if (c == '/') {
      g_cp.pos++;
      double b = calcUnary();
      if (g_cp.err) break;
      if (b == 0) { calcFail("divide by 0", false); break; }
      v /= b;
    } else if (c == '%') {   // here it is modulo (a % that is followed by nothing is taken in calcPostfix)
      g_cp.pos++;
      double b = calcUnary();
      if (g_cp.err) break;
      if (b == 0) { calcFail("divide by 0", false); break; }
      v = fmod(v, b);
    } else break;
    if (!g_cp.err && !calcOkNum(v)) calcFail("too big", false);
  }
  return g_cp.err ? 0 : v;
}

static double calcExpr() {
  double v = calcTerm();
  while (!g_cp.err) {
    char c = calcPeek();
    if (c == '+') { g_cp.pos++; v += calcTerm(); }
    else if (c == '-') { g_cp.pos++; v -= calcTerm(); }
    else break;
    if (!g_cp.err && !calcOkNum(v)) calcFail("too big", false);
  }
  return g_cp.err ? 0 : v;
}

// evaluate a cleaned lower case expression. Returns true and *out, or false and *err (and *syntax)
static CALC_NOINLINE bool calcEval(const char* s, uint16_t who, double* out, const char** err, bool* syntax) {
  g_cp.s = s;
  g_cp.pos = 0;
  g_cp.depth = 0;
  g_cp.err = NULL;
  g_cp.syntax = false;
  g_cp.has_ans = calcAnsGet(who, &g_cp.ans);
  double v = calcExpr();
  if (!g_cp.err) {
    if (calcPeek() != 0) calcFail("bad expression", true);
  }
  if (g_cp.err) { *err = g_cp.err; *syntax = g_cp.syntax; return false; }
  *out = v;
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// text preparation: lower case copy, rewriting of words like "plus", lead-ins like "what is"

// lower case copy of raw into out. Apostrophes and non-ASCII are dropped (or an apostrophe becomes a space if apos_space).
// Returns false if the text is longer than the buffer.
static bool calcLower(const char* raw, char* out, int osz, bool apos_space) {
  int n = 0;
  for (const unsigned char* p = (const unsigned char*)raw; *p; p++) {
    if (n >= osz - 2) return false;
    unsigned char c = *p;
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    if (c == '\'') { if (apos_space) out[n++] = ' '; continue; }
    if (c >= 0x80) continue;
    if (c < 32) c = ' ';
    out[n++] = (char)c;
  }
  out[n] = 0;
  return true;
}

static void calcTrim(char* s) {
  int n = (int)strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
  int st = 0;
  while (s[st] == ' ') st++;
  if (st > 0) memmove(s, s + st, strlen(s + st) + 1);
}

static bool calcStartsWord(const char* s, const char* phrase) {   // s starts with phrase, followed by a space or the end
  int l = (int)strlen(phrase);
  return strncmp(s, phrase, l) == 0 && (s[l] == 0 || s[l] == ' ');
}

// words that become operators (longest first). A phrase must start and end at a word edge.
struct CalcRw { const char* from; const char* to; };
static const CalcRw CALC_RW[] = {
  {"square root of", "sqrt "}, {"cube root of", "cbrt "}, {"to the power of", "^"}, {"multiplied by", "*"}, {"divided by", "/"}, {"raised to", "^"}, {"percent", "%"},
  {"modulo", "%"}, {"squared", "^2"}, {"times", "*"}, {"cubed", "^3"}, {"minus", "-"}, {"plus", "+"}, {"over", "/"},
  {"mod", "%"}, {"and", "+"}, {"of", "*"},
};
#define CALC_NRW ((int)(sizeof(CALC_RW) / sizeof(CALC_RW[0])))

static bool calcLastNonSpace(const char* out, int n, char* c) {
  while (n > 0 && out[n - 1] == ' ') n--;
  if (n == 0) return false;
  *c = out[n - 1];
  return true;
}

// copy in to out, turning operator words into symbols. "of" only after a '%' ("15% of 80"), "and" is not an operator at all
// (it is kept as a word, so a sentence with "and" is not taken for maths). 'x' between two operands is multiplication.
static CALC_NOINLINE bool calcRewrite(const char* in, char* out, int osz) {
  int n = 0, i = 0;
  while (in[i] && n < osz - 4) {
    bool edge = (i == 0) || !calcIsAlpha(in[i - 1]);
    bool done = false;
    if (edge && calcIsAlpha(in[i])) {
      for (int r = 0; r < CALC_NRW && !done; r++) {
        const char* f = CALC_RW[r].from;
        if (strcmp(f, "and") == 0) continue;
        int fl = (int)strlen(f);
        if (strncmp(in + i, f, fl) != 0 || calcIsAlpha(in[i + fl])) continue;
        if (strcmp(f, "of") == 0) {
          char lc;
          if (!calcLastNonSpace(out, n, &lc) || lc != '%') continue;
        }
        if (strcmp(f, "mod") == 0) {   // mod(7,3) is the function
          int q = i + fl;
          while (in[q] == ' ') q++;
          if (in[q] == '(') continue;
        }
        if (strcmp(f, "percent") == 0 && !(n > 0 && (calcIsDig(out[n - 1]) || out[n - 1] == ' ' || out[n - 1] == ')'))) continue;
        if (CALC_RW[r].to[0] == '^') { while (n > 0 && out[n - 1] == ' ') n--; }   // 10 squared -> 10^2
        else if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
        for (const char* t = CALC_RW[r].to; *t && n < osz - 3; t++) out[n++] = *t;
        if (CALC_RW[r].to[0] != '^' && out[n - 1] != ' ') out[n++] = ' ';
        i += fl;
        if (CALC_RW[r].to[0] == '^') while (in[i] == ' ') i++;   // 2 to the power of 8 -> 2^8
        done = true;
      }
      if (!done && in[i] == 'x' && !calcIsAlpha(in[i + 1])) {   // 3 x 4, 3x4
        char lc = 0;
        int j = i + 1;
        while (in[j] == ' ') j++;
        if (calcLastNonSpace(out, n, &lc) && (calcIsDig(lc) || lc == ')') && (calcIsDig(in[j]) || in[j] == '.' || in[j] == '(')) {
          if (n > 0 && out[n - 1] != ' ') out[n++] = ' ';
          out[n++] = '*';
          out[n++] = ' ';
          i++;
          done = true;
        }
      }
    }
    if (!done) {
      if (in[i] == ' ' && n > 0 && out[n - 1] == ' ') { i++; continue; }   // no double spaces
      out[n++] = in[i++];
    }
  }
  out[n] = 0;
  return in[i] == 0;   // false = it did not fit, the expression would have been cut short
}

// ---------------------------------------------------------------------------------------------------------------
// calculator entry point

static const char* const CALC_LEADS[] = {
  "what is the answer to", "what is the result of", "whats the answer to", "what is the", "whats the", "what is", "what are", "whats", "whatis", "how much is",
  "calculate", "calculator", "calc", "compute", "work out", "solve", "evaluate", "eval", "result of", "answer to", "tell me", "please",
};
#define CALC_NLEADS ((int)(sizeof(CALC_LEADS) / sizeof(CALC_LEADS[0])))

static void calcErr(char* out, int osz, const char* why) { snprintf(out, osz, "Err - %s", why); }

// returns 0 = not a calculation (out untouched), 1 = answer in out, 2 = error text in out
static CALC_NOINLINE int calcMaths(const char* raw, bool strict, uint16_t who, char* out, int osz) {
  static char a[CALC_BUF], b[CALC_BUF * 2];
  if (!calcLower(raw, a, sizeof(a), false)) {
    if (!strict) return 0;
    calcErr(out, osz, "too long");
    return 2;
  }
  calcTrim(a);
  char* s = a;
  bool lead = false;
  for (int pass = 0; pass < 3; pass++) {
    bool hit = false;
    for (int i = 0; i < CALC_NLEADS; i++) {
      if (calcStartsWord(s, CALC_LEADS[i])) {
        s += strlen(CALC_LEADS[i]);
        while (*s == ' ') s++;
        if (strcmp(CALC_LEADS[i], "please") != 0) lead = true;
        hit = true;
        break;
      }
    }
    if (!hit) break;
  }
  int sl = (int)strlen(s);
  while (sl > 0 && (s[sl - 1] == '?' || s[sl - 1] == '.' || s[sl - 1] == '=' || s[sl - 1] == '!' || s[sl - 1] == ' ')) s[--sl] = 0;
  if (sl == 0) return 0;
  if (!calcRewrite(s, b, sizeof(b))) {
    if (!strict) return 0;
    calcErr(out, osz, "too long");
    return 2;
  }
  calcTrim(b);

  // does it look like maths? only digits, operators, brackets, commas, spaces and known words
  int ops = 0, funcs = 0, consts = 0, digits = 0;
  bool shape = true;
  int dslash = 0;
  for (int i = 0; b[i] && shape; i++) {
    char c = b[i];
    if (calcIsDig(c)) { digits++; continue; }
    if (c == ' ' || c == '.' || c == '(' || c == ')' || c == ',') continue;
    if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^' || c == '%') {
      char pc = 0;
      int k = i - 1;
      while (k >= 0 && b[k] == ' ') k--;
      if (k >= 0) pc = b[k];
      if (c == '%' || (pc && (calcIsDig(pc) || pc == ')' || calcIsAlpha(pc) || pc == '%'))) ops++;
      if (c == '/') dslash++;
      continue;
    }
    if (c == 'e' && i > 0 && (calcIsDig(b[i - 1]) || b[i - 1] == '.') &&
        (calcIsDig(b[i + 1]) || ((b[i + 1] == '-' || b[i + 1] == '+') && calcIsDig(b[i + 2])))) {
      if (b[i + 1] == '-' || b[i + 1] == '+') i++;   // 2e3, 1.5e-3: the exponent
      continue;
    }
    if (calcIsAlpha(c)) {
      int st = i;
      while (calcIsAlpha(b[i]) || (i > st && calcIsDig(b[i]))) i++;
      int l = i - st;
      i--;
      if (calcFnFind(b + st, l) >= 0) funcs++;
      else if (calcIsConst(b + st, l)) consts++;
      else shape = false;
      continue;
    }
    shape = false;
  }
  if (shape && dslash >= 2 && funcs == 0 && consts == 0) {   // 25/12/2026 is a date, not a division
    bool only = true;
    for (int i = 0; b[i]; i++) if (!(calcIsDig(b[i]) || b[i] == '/' || b[i] == ' ')) only = false;
    if (only) shape = false;
  }
  bool accept = shape && (strict ? (ops > 0 || funcs > 0 || consts > 0) : (ops > 0 || funcs > 0));
  if (!accept) {
    if (strict && digits > 0) {
      calcErr(out, osz, "try 12*(3+4), sqrt(16), 10km in miles");
      return 2;
    }
    return 0;
  }
  double v = 0;
  const char* err = NULL;
  bool syn = false;
  if (!calcEval(b, who, &v, &err, &syn)) {
    if (!strict && !lead && syn) return 0;   // chat that only looked like maths: let the normal bot answer
    calcErr(out, osz, err);
    return 2;
  }
  calcAnsSet(who, v);
  char num[40];
  calcFmt(v, 9, num, sizeof(num));
  if ((int)strlen(b) <= 56) snprintf(out, osz, "%s = %s", b, num);
  else snprintf(out, osz, "= %s", num);
  return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// unit conversion

#define CALCU_LEN   0
#define CALCU_MASS  1
#define CALCU_SPEED 2
#define CALCU_TEMP  3
#define CALCU_AREA  4
#define CALCU_VOL   5
#define CALCU_TIME  6

struct CalcUnit { const char* names; const char* label; uint8_t dim; double f; };   // f = size in the base unit of its kind
static const CalcUnit CALC_UNITS[] = {
  {"m|metre|metres|meter|meters", "m", CALCU_LEN, 1.0},
  {"km|kms|kilometre|kilometres|kilometer|kilometers", "km", CALCU_LEN, 1000.0},
  {"cm|centimetre|centimetres|centimeter|centimeters", "cm", CALCU_LEN, 0.01},
  {"mm|millimetre|millimetres|millimeter|millimeters", "mm", CALCU_LEN, 0.001},
  {"mi|mile|miles", "miles", CALCU_LEN, 1609.344},
  {"ft|foot|feet", "ft", CALCU_LEN, 0.3048},
  {"in|inch|inches", "in", CALCU_LEN, 0.0254},
  {"yd|yard|yards", "yd", CALCU_LEN, 0.9144},
  {"kg|kgs|kilo|kilos|kilogram|kilograms", "kg", CALCU_MASS, 1.0},
  {"g|gram|grams", "g", CALCU_MASS, 0.001},
  {"lb|lbs|pound|pounds", "lb", CALCU_MASS, 0.45359237},
  {"oz|ounce|ounces", "oz", CALCU_MASS, 0.028349523125},
  {"stone|stones", "stone", CALCU_MASS, 6.35029318},
  {"kmh|kph|kmph|km/h", "km/h", CALCU_SPEED, 1000.0 / 3600.0},
  {"mph|mi/h", "mph", CALCU_SPEED, 0.44704},
  {"m/s|ms|mps", "m/s", CALCU_SPEED, 1.0},
  {"knot|knots|kt|kts", "knots", CALCU_SPEED, 1852.0 / 3600.0},
  {"c|celsius|centigrade", "C", CALCU_TEMP, 1.0},
  {"f|fahrenheit", "F", CALCU_TEMP, 2.0},
  {"k|kelvin", "K", CALCU_TEMP, 3.0},
  {"m2|sqm", "m2", CALCU_AREA, 1.0},
  {"km2|sqkm", "km2", CALCU_AREA, 1000000.0},
  {"ha|hectare|hectares", "ha", CALCU_AREA, 10000.0},
  {"acre|acres", "acres", CALCU_AREA, 4046.8564224},
  {"ft2|sqft", "sq ft", CALCU_AREA, 0.09290304},
  {"l|lt|litre|litres|liter|liters", "L", CALCU_VOL, 1.0},
  {"ml|millilitre|millilitres|milliliter|milliliters", "mL", CALCU_VOL, 0.001},
  {"gal|gallon|gallons", "US gal", CALCU_VOL, 3.785411784},
  {"floz", "US fl oz", CALCU_VOL, 0.0295735295625},
  {"s|sec|secs|second|seconds", "s", CALCU_TIME, 1.0},
  {"min|mins|minute|minutes", "min", CALCU_TIME, 60.0},
  {"h|hr|hrs|hour|hours", "h", CALCU_TIME, 3600.0},
  {"day|days", "days", CALCU_TIME, 86400.0},
};
#define CALC_NUNITS ((int)(sizeof(CALC_UNITS) / sizeof(CALC_UNITS[0])))

// the label of unit u for an amount: "1 mile", "2 miles"
static const char* calcLabel(int u, double amount) {
  const char* l = CALC_UNITS[u].label;
  if (amount == 1.0) {
    if (strcmp(l, "miles") == 0) return "mile";
    if (strcmp(l, "acres") == 0) return "acre";
    if (strcmp(l, "days") == 0) return "day";
    if (strcmp(l, "knots") == 0) return "knot";
  }
  return l;
}

static int calcUnitFind(const char* w, int l) {
  for (int u = 0; u < CALC_NUNITS; u++) {
    const char* p = CALC_UNITS[u].names;
    while (*p) {
      const char* e = strchr(p, '|');
      int len = e ? (int)(e - p) : (int)strlen(p);
      if (len == l && memcmp(p, w, l) == 0) return u;
      if (!e) break;
      p = e + 1;
    }
  }
  return -1;
}

// short words that are easily something else in a sentence: only a unit when stuck to its number ("30c", "10in")
static bool calcUnitRestricted(const char* w, int l) {
  return l == 1 || (l == 2 && (memcmp(w, "in", 2) == 0 || memcmp(w, "ms", 2) == 0 || memcmp(w, "kt", 2) == 0 || memcmp(w, "lt", 2) == 0));
}

struct CalcTok { uint8_t type; bool att; uint8_t wl; double v; char w[14]; };   // type 1 = number, 2 = word; att = word stuck to the number before it
static CalcTok g_ct[CALC_TOK_MAX];
static int g_ctn;

static CALC_NOINLINE bool calcTokenize(const char* s) {
  g_ctn = 0;
  int n = (int)strlen(s);
  int i = 0;
  int last_num_end = -1;
  while (i < n) {
    char c = s[i];
    bool neg = false;
    if (c == '-' && i + 1 < n && calcIsDig(s[i + 1]) && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '(')) { neg = true; i++; c = s[i]; }
    if (calcIsDig(c) || (c == '.' && i + 1 < n && calcIsDig(s[i + 1]))) {
      if (g_ctn >= CALC_TOK_MAX) return false;
      double v = 0;
      while (i < n && (calcIsDig(s[i]) || (s[i] == ',' && i > 0 && calcIsDig(s[i - 1]) && i + 3 < n && calcIsDig(s[i + 1]) &&
                                          calcIsDig(s[i + 2]) && calcIsDig(s[i + 3]) && !(i + 4 < n && calcIsDig(s[i + 4]))))) {
        if (s[i] != ',') v = v * 10.0 + (s[i] - '0');
        i++;
      }
      if (i < n && s[i] == '.') {
        i++;
        double f = 0, div = 1;
        int fd = 0;
        while (i < n && calcIsDig(s[i])) { if (fd < 15) { f = f * 10.0 + (s[i] - '0'); div *= 10.0; fd++; } i++; }
        v += f / div;
      }
      g_ct[g_ctn].type = 1;
      g_ct[g_ctn].v = neg ? -v : v;
      g_ct[g_ctn].att = false;
      g_ct[g_ctn].wl = 0;
      g_ct[g_ctn].w[0] = 0;
      g_ctn++;
      last_num_end = i;
      continue;
    }
    if (calcIsAlpha(c)) {
      if (g_ctn >= CALC_TOK_MAX) return false;
      int st = i;
      while (i < n && calcIsAlpha(s[i])) i++;
      int dg = 0;
      while (i < n && calcIsDig(s[i]) && dg < 2) { i++; dg++; }
      if (i + 1 < n && s[i] == '/' && calcIsAlpha(s[i + 1])) {
        i++;
        while (i < n && calcIsAlpha(s[i])) i++;
      }
      int l = i - st;
      if (l > 13) l = 13;
      memcpy(g_ct[g_ctn].w, s + st, l);
      g_ct[g_ctn].w[l] = 0;
      g_ct[g_ctn].wl = (uint8_t)l;
      g_ct[g_ctn].type = 2;
      g_ct[g_ctn].att = (st == last_num_end) && g_ctn > 0 && g_ct[g_ctn - 1].type == 1;
      g_ct[g_ctn].v = 0;
      g_ctn++;
      continue;
    }
    i++;
  }
  return true;
}

static bool calcWordIs(int t, const char* lit) {
  return t >= 0 && t < g_ctn && g_ct[t].type == 2 && strcmp(g_ct[t].w, lit) == 0;
}
static bool calcSkipWord(int t) {
  return calcWordIs(t, "degrees") || calcWordIs(t, "degree") || calcWordIs(t, "deg") || calcWordIs(t, "degs");
}

// tokens s .. e-1 as a run of "number unit" pairs of one kind ("5 ft 10 in", "30 C"). On success: kind, the total in the base
// unit (or the value, for a temperature), the unit, and the text "5 ft 10 in".
struct CalcSrc { int dim; double base; double val; int unit; char echo[48]; };
static CalcSrc g_csrc;

static CALC_NOINLINE bool calcPairs(int s, int e, bool strict) {
  int i = s, pairs = 0;
  g_csrc.dim = -1;
  g_csrc.base = 0;
  g_csrc.val = 0;
  g_csrc.unit = -1;
  g_csrc.echo[0] = 0;
  while (i < e) {
    if (pairs > 0 && calcWordIs(i, "and") && i + 1 < e) { i++; continue; }
    double val;
    bool skipped = false;
    if (g_ct[i].type == 1) {
      val = g_ct[i].v;
      i++;
      if (i < e && calcSkipWord(i)) { i++; skipped = true; }
    } else if (calcWordIs(i, "a") || calcWordIs(i, "an") || calcWordIs(i, "one")) {
      val = 1;
      i++;
    } else return false;
    if (i >= e || g_ct[i].type != 2) return false;
    int u = calcUnitFind(g_ct[i].w, g_ct[i].wl);
    if (u < 0) return false;
    bool restricted = calcUnitRestricted(g_ct[i].w, g_ct[i].wl);
    if (restricted && !strict && !(g_ct[i].att || skipped)) return false;
    int dim = CALC_UNITS[u].dim;
    if (pairs == 0) g_csrc.dim = dim;
    else if (dim != g_csrc.dim || dim == CALCU_TEMP) return false;
    g_csrc.base += val * CALC_UNITS[u].f;
    g_csrc.val = val;
    g_csrc.unit = u;
    char nb[32];
    calcFmt(val, 9, nb, sizeof(nb));
    int el = (int)strlen(g_csrc.echo);
    snprintf(g_csrc.echo + el, sizeof(g_csrc.echo) - el, "%s%s %s", el ? " " : "", nb, calcLabel(u, val));
    i++;
    if (++pairs > 4) return false;
  }
  return pairs > 0;
}

// returns 0 = not a conversion, 1 = answer, 2 = error text
static CALC_NOINLINE int calcConvert(const char* raw, bool strict, char* out, int osz) {
  static char lc[CALC_BUF];
  if (!calcLower(raw, lc, sizeof(lc), true)) return 0;
  if (!calcTokenize(lc)) return 0;
  int n = g_ctn;
  if (n < 3) return 0;
  int tu = -1;
  bool found = false;
  // "<amount> in|to|into|as <unit>"
  for (int k = n - 2; k >= 1 && !found; k--) {
    if (!(calcWordIs(k, "in") || calcWordIs(k, "to") || calcWordIs(k, "into") || calcWordIs(k, "as"))) continue;
    int t = k + 1;
    if (calcSkipWord(t)) t++;
    if (t != n - 1 || g_ct[t].type != 2) continue;
    int u = calcUnitFind(g_ct[t].w, g_ct[t].wl);
    if (u < 0) continue;
    for (int j = (k - 9 < 0 ? 0 : k - 9); j < k; j++) {
      if (calcPairs(j, k, strict)) { found = true; tu = u; break; }
    }
  }
  // "how many <unit> in|is|are <amount>"
  if (!found) {
    for (int i = 0; i + 4 < n && !found; i++) {
      if (!calcWordIs(i, "how") || !calcWordIs(i + 1, "many") || g_ct[i + 2].type != 2) continue;
      int u = calcUnitFind(g_ct[i + 2].w, g_ct[i + 2].wl);
      if (u < 0) continue;
      if (!(calcWordIs(i + 3, "in") || calcWordIs(i + 3, "is") || calcWordIs(i + 3, "are"))) continue;
      if (calcPairs(i + 4, n, strict)) { found = true; tu = u; }
    }
  }
  if (!found) return 0;
  const CalcUnit* T = &CALC_UNITS[tu];
  const CalcUnit* S = &CALC_UNITS[g_csrc.unit];
  if (T->dim != g_csrc.dim) {
    snprintf(out, osz, "Err - cannot convert %s to %s", S->label, T->label);
    return 2;
  }
  double res;
  if (T->dim == CALCU_TEMP) {
    double c;
    if (S->f == 1.0) c = g_csrc.val;
    else if (S->f == 2.0) c = (g_csrc.val - 32.0) * 5.0 / 9.0;
    else c = g_csrc.val - 273.15;
    if (c < -273.15 - 1e-9) { snprintf(out, osz, "Err - below absolute zero"); return 2; }
    if (T->f == 1.0) res = c;
    else if (T->f == 2.0) res = c * 9.0 / 5.0 + 32.0;
    else res = c + 273.15;
  } else {
    res = g_csrc.base / T->f;
  }
  if (!calcOkNum(res)) { snprintf(out, osz, "Err - too big"); return 2; }
  char nb[40];
  calcFmt(res, 6, nb, sizeof(nb));
  snprintf(out, osz, "%s = %s %s", g_csrc.echo, nb, calcLabel(tu, res));
  return 1;
}

// ---------------------------------------------------------------------------------------------------------------
// the one entry point used by the bot channel and by calcchan
// strict = calcchan: everything is meant for the calculator, so odd input gets "Err - ..." (but text with no digit is ignored)
// returns 0 = not for the calculator, 1 = answer in out, 2 = error text in out
static int calcAnswer(const char* raw, bool strict, uint16_t who, char* out, int osz) {
  int r = calcConvert(raw, strict, out, osz);
  if (r != 0) return r;
  return calcMaths(raw, strict, who, out, osz);
}

// ---------------------------------------------------------------------------------------------------------------
// hiking maths: "it's 8km and climbs 300m. How long will it take?"

struct CalcHike {
  bool has_dist, has_climb, has_desc, one_way, full;
  double dist_km, climb_m, desc_m;
};

static bool calcClimbWord(const char* w) {
  static const char* const L[] = {"climb", "climbs", "climbing", "climbed", "ascent", "ascend", "ascends", "ascending", "gain", "gains",
                                  "gaining", "elevation", "uphill", "up", "vertical", "height", "elev", "rise", "rises", "incline"};
  for (size_t i = 0; i < sizeof(L) / sizeof(L[0]); i++) if (strcmp(w, L[i]) == 0) return true;
  return false;
}
static bool calcDescWord(const char* w) {
  static const char* const L[] = {"descent", "descend", "descends", "descending", "drop", "drops", "dropping", "downhill", "down", "decline",
                                  "loss", "losing"};
  for (size_t i = 0; i < sizeof(L) / sizeof(L[0]); i++) if (strcmp(w, L[i]) == 0) return true;
  return false;
}

// fills h from the sentence. Returns true if a distance (km or miles) was found.
static CALC_NOINLINE bool calcHikeParse(const char* raw, CalcHike* h) {
  static char lc[CALC_BUF];
  memset(h, 0, sizeof(*h));
  if (!calcLower(raw, lc, sizeof(lc), false)) return false;
  if (!calcTokenize(lc)) return false;
  int n = g_ctn;
  for (int i = 0; i + 1 < n; i++) {
    if (g_ct[i].type != 1 || g_ct[i + 1].type != 2) continue;
    int u = calcUnitFind(g_ct[i + 1].w, g_ct[i + 1].wl);
    if (u < 0 || CALC_UNITS[u].dim != CALCU_LEN) continue;
    double metres = g_ct[i].v * CALC_UNITS[u].f;
    const char* lab = CALC_UNITS[u].label;
    bool far_unit = strcmp(lab, "km") == 0 || strcmp(lab, "miles") == 0;
    bool high_unit = strcmp(lab, "m") == 0 || strcmp(lab, "ft") == 0;
    if (far_unit) {
      if (!h->has_dist && metres > 0) { h->has_dist = true; h->dist_km = metres / 1000.0; }
    } else if (high_unit) {
      // climb or descent? look at the words just before and after
      int best = 0, bestd = 99;   // 1 climb, 2 descent
      for (int d = 1; d <= 3; d++) {
        for (int side = -1; side <= 1; side += 2) {
          int t = side < 0 ? i - d : i + 1 + d;
          if (t < 0 || t >= n || g_ct[t].type != 2) continue;
          int dd = d;
          if (dd < bestd) {
            if (calcDescWord(g_ct[t].w)) { best = 2; bestd = dd; }
            else if (calcClimbWord(g_ct[t].w)) { best = 1; bestd = dd; }
          }
        }
      }
      if (best == 0 && !h->has_climb) best = 1;   // a bare "300m" after a distance is taken as the climb
      if (best == 1 && !h->has_climb) { h->has_climb = true; h->climb_m = metres; }
      else if (best == 2 && !h->has_desc) { h->has_desc = true; h->desc_m = metres; }
    }
  }
  for (int i = 0; i < n; i++) {
    if ((calcWordIs(i, "one") || calcWordIs(i, "each") || calcWordIs(i, "single")) && (calcWordIs(i + 1, "way") || calcWordIs(i + 1, "direction")))
      h->one_way = true;
    if (calcWordIs(i, "return") || calcWordIs(i, "loop") || calcWordIs(i, "circuit") || (calcWordIs(i, "round") && calcWordIs(i + 1, "trip")) ||
        ((calcWordIs(i, "there") || calcWordIs(i, "out")) && calcWordIs(i + 1, "and") && calcWordIs(i + 2, "back")))
      h->full = true;
  }
  if (h->one_way) h->full = false;
  return h->has_dist;
}

// minutes for the walk by Naismith's rule: 5 km/h plus 1 hour per 600 m climbed. A steep descent (steeper than about 12 degrees,
// worked out on half the distance) adds 10 minutes per 300 m, a gentle one is left out (it is a rough, cautious figure).
// "one way" doubles the distance (the climb is the total ascent, so it is not doubled). Returns -1 if the numbers are not sane.
static int calcNaismithMin(const CalcHike* h) {
  double km = h->one_way ? h->dist_km * 2.0 : h->dist_km;
  if (!(km >= 0.05 && km <= 250.0)) return -1;
  if (h->has_climb && !(h->climb_m >= 0 && h->climb_m <= 9000.0)) return -1;
  if (h->has_desc && !(h->desc_m >= 0 && h->desc_m <= 9000.0)) return -1;
  double m = km * 12.0;                         // 5 km/h = 12 min per km
  if (h->has_climb) m += h->climb_m / 10.0;     // 600 m per hour = 10 m per minute
  if (h->has_desc) {
    double run_m = km * 500.0;                  // half the distance, in metres
    if (run_m > 0 && h->desc_m / run_m > 0.21) m += h->desc_m / 300.0 * 10.0;
  }
  return (int)(m + 0.5);
}

// average slope in tenths of a percent for a rise over a run (km), and a rough 1 to 5 grade
static int calcSlopeTenths(double rise_m, double run_km) {
  if (!(run_km > 0)) return 0;
  return (int)(rise_m / (run_km * 1000.0) * 1000.0 + 0.5);
}
static int calcSlopeGrade(int tenths) {
  int pct = tenths / 10;
  if (pct < 3) return 1;
  if (pct < 6) return 2;
  if (pct < 10) return 3;
  if (pct < 15) return 4;
  return 5;
}

#endif
