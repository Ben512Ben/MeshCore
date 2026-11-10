#!/usr/bin/env python3
"""
wiki_to_sd.py - turn a Kiwix Wikipedia "mini" ZIM file into the three small files the TrailBot SD card reads.

  wiki.idx   sorted index, FIXED size records of 56 bytes so the repeater can binary search it without parsing:
               key    48 bytes  lowercase letters/digits/single spaces, padded with spaces on the right
               offset  4 bytes  little endian, position of the record in wiki.txt
               length  2 bytes  little endian, length of the record in wiki.txt
               flags   2 bytes  little endian, 0 for now
  wiki.txt   the records, one after another (plain ASCII, no separators needed, the index says where each is):
               "<Title>\n<summary>"      the summary is at most MAX_SUMMARY characters, cut at a sentence end
  wiki.meta  plain text: source file, date, counts, licence line. Shown by the bot on request later.

Redirects (for example "USA" -> "United States") become extra index records that point at the target's text.

Usage:
  python3 wiki_to_sd.py wikipedia_en_simple_all_mini_2025-01.zim  outdir/        (needs: pip install libzim beautifulsoup4 lxml)
  python3 wiki_to_sd.py --jsonl sample.jsonl  outdir/                            (no ZIM needed, used by the tests)

The jsonl format is one JSON object per line: {"title": "...", "html": "<p>...</p>"} or {"title": "...", "redirect": "Target title"}.
"""
import argparse, json, os, re, struct, sys, unicodedata

MAX_SUMMARY = 440          # 4 messages of about 110 characters
MIN_SUMMARY = 40           # shorter than this: not worth keeping
KEY_LEN = 48               # index key width; titles that need a longer key are skipped
REC = struct.Struct("<%dsIHH" % KEY_LEN)   # 56 bytes
LICENCE = "Text from Wikipedia (en.wikipedia.org / simple.wikipedia.org), CC BY-SA 4.0. Authors: see each article's history on Wikipedia."

SKIP_PREFIXES = ("category:", "help:", "wikipedia:", "file:", "template:", "portal:", "talk:", "user:", "mediawiki:", "module:", "draft:")
ABBREV = {"mr", "mrs", "ms", "dr", "prof", "st", "mt", "jr", "sr", "vs", "etc", "no", "inc", "ltd", "co", "corp", "gen", "col", "sgt", "capt",
          "lt", "rev", "hon", "fig", "approx", "c", "ca", "b", "d", "e.g", "i.e", "u.s", "u.k", "a.m", "p.m", "jan", "feb", "mar", "apr", "jun",
          "jul", "aug", "sep", "sept", "oct", "nov", "dec", "ave", "blvd", "rd", "dept", "est", "gov", "sen", "rep", "pres"}

FOLD = {0x2013: "-", 0x2014: "-", 0x2212: "-", 0x2018: "'", 0x2019: "'", 0x201C: '"', 0x201D: '"', 0x2026: "...", 0x00A0: " ",
        0x2022: "-", 0x00D7: "x", 0x00B0: " deg ", 0x2032: "'", 0x2033: '"', 0x00E6: "ae", 0x00C6: "AE", 0x0153: "oe", 0x00F8: "o",
        0x00D8: "O", 0x0142: "l", 0x0141: "L", 0x00DF: "ss", 0x0111: "d", 0x0110: "D"}


def norm_key(title):
    """Same idea as the bot's own name cleaning: lowercase, letters and digits only, one space between words."""
    t = ascii_fold(title).lower().replace("'", "")
    t = re.sub(r"[^a-z0-9]+", " ", t)
    return t.strip()


def ascii_fold(s):
    out = []
    for ch in s:
        o = ord(ch)
        if o < 128:
            out.append(ch)
        elif o in FOLD:
            out.append(FOLD[o])
        else:
            d = unicodedata.normalize("NFKD", ch)
            out.append("".join(c for c in d if ord(c) < 128 and not unicodedata.combining(c)))
    return "".join(out)


def clean_parens(text):
    """Clean (...) groups. Pronunciations, native-script names and "German: ..." labels are dropped, but the rest of the
    group stays, so "(/x/ EYE-styne; German: [..]; 14 March 1879 - 18 April 1955)" becomes "(14 March 1879 - 18 April 1955)"."""
    def keep(m):
        good = []
        for seg in m.group(1).split(";"):
            seg = seg.strip()
            if not seg:
                continue
            folded = "".join(FOLD.get(ord(c), c) for c in seg)
            if re.search(r"[^\x00-\x7f]", folded):                 # non-ASCII letters: pronunciation or native script
                continue
            if re.search(r"(?i)pronounced|listen|\bipa\b|/", folded):
                continue
            if re.match(r"^[A-Za-z ]+:\s*$", folded) or re.match(r"^[A-Z][a-z]+( language)?:\s", folded) and re.search(r"[^\x00-\x7f]", seg):
                continue                                              # "German:" label with nothing useful after it
            good.append(seg)
        return "(" + "; ".join(good) + ")" if good else ""
    prev = None
    while prev != text:                                               # repeat for nesting
        prev = text
        text = re.sub(r"\(([^()]*)\)", keep, text)
    return text


def html_to_summary(html):
    """First paragraphs of an article's HTML as clean ASCII text, cut to MAX_SUMMARY at a sentence end. '' if nothing usable."""
    from bs4 import BeautifulSoup
    soup = BeautifulSoup(html, "lxml")
    for tag in soup(["style", "script", "sup", "table", "figure", "sub", "math"]):
        tag.decompose()
    for tag in soup.select(".mw-empty-elt, .hatnote, .noprint, .reference, .thumb, .mw-editsection"):
        tag.decompose()
    chunks = []
    total = 0
    for p in soup.find_all("p"):
        txt = p.get_text(" ", strip=True)
        txt = re.sub(r"\[[^\]]*\]", "", txt)              # [1] [citation needed]
        txt = ascii_fold(clean_parens(txt))
        txt = re.sub(r"\s+([,.;:!?])", r"\1", txt)       # "word ." -> "word."
        txt = re.sub(r"\(\s*\)", "", txt)
        txt = re.sub(r"\s+", " ", txt).strip()
        if len(txt) < 25:                                 # empty or coordinates or a stub line
            continue
        chunks.append(txt)
        total += len(txt)
        if total >= MAX_SUMMARY * 2:
            break
    if not chunks:
        return ""
    return cut_sentences(" ".join(chunks))


def split_sentences(text):
    """Split at . ! ? followed by a space and a capital or digit, but not after abbreviations or single initials."""
    out, start = [], 0
    for m in re.finditer(r"[.!?][\"')]*\s+(?=[A-Z0-9\"'])", text):
        end = m.end()
        before = text[start:m.start()]
        last = re.split(r"[\s(]", before)[-1].lower().rstrip(".")
        if last in ABBREV or re.fullmatch(r"[a-z]", last) or re.fullmatch(r"(?:[a-z]\.)+[a-z]?", last + "."):
            continue
        out.append(text[start:end].strip())
        start = end
    rest = text[start:].strip()
    if rest:
        out.append(rest)
    return out


def cut_sentences(text, limit=MAX_SUMMARY):
    sents = split_sentences(text)
    res = ""
    for s in sents:
        cand = (res + " " + s).strip()
        if len(cand) <= limit:
            res = cand
        else:
            break
    if not res:                                           # first sentence alone is too long: cut at a word
        cut = text[: limit - 3]
        cut = cut[: cut.rfind(" ")] if " " in cut else cut
        res = cut.rstrip(",;: ") + "..."
    return res


def is_junk(title, summary):
    tl = title.lower()
    if tl.startswith(SKIP_PREFIXES):
        return True
    if tl.startswith(("list of ", "lists of ", "index of ", "outline of ", "timeline of ")):
        return True
    low = summary.lower()
    if re.search(r"\b(may|can) (also )?refer to\b|\bmay mean\b|\bcan mean\b|\bis a disambiguation\b", low[:200]):
        return True
    return len(summary) < MIN_SUMMARY


def iter_zim(path):
    """Yield (title, html, redirect_title) for every entry of the ZIM. Needs libzim (pip install libzim)."""
    from libzim.reader import Archive
    zim = Archive(path)
    n = zim.entry_count
    for i in range(n):
        try:
            e = zim._get_entry_by_id(i)
        except Exception:
            continue
        try:
            if e.is_redirect:
                tgt = e.get_redirect_entry()
                yield e.title, None, tgt.title
                continue
            item = e.get_item()
            if not item.mimetype.startswith("text/html"):
                continue
            yield e.title, bytes(item.content).decode("utf-8", "replace"), None
        except Exception:
            continue


def iter_jsonl(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            yield o.get("title", ""), o.get("html"), o.get("redirect")


def convert(entries, outdir, source_name="", progress=True):
    os.makedirs(outdir, exist_ok=True)
    txt_path = os.path.join(outdir, "wiki.txt")
    articles = {}        # title -> (offset, length)
    redirects = []       # (title, target title)
    stats = {"entries": 0, "articles": 0, "redirects": 0, "junk": 0, "empty": 0, "long_key": 0, "dup_key": 0}
    offset = 0
    with open(txt_path, "wb") as tf:
        for title, html, redir in entries:
            stats["entries"] += 1
            if progress and stats["entries"] % 20000 == 0:
                print("  %d entries read, %d articles kept" % (stats["entries"], stats["articles"]), file=sys.stderr)
            if not title:
                continue
            if redir is not None:
                redirects.append((title, redir))
                continue
            if title.lower().startswith(SKIP_PREFIXES):
                stats["junk"] += 1
                continue
            summary = html_to_summary(html) if html else ""
            if not summary:
                stats["empty"] += 1
                continue
            disp = ascii_fold(title).strip()
            if is_junk(disp, summary):
                stats["junk"] += 1
                continue
            rec = (disp + "\n" + summary).encode("ascii", "ignore")
            if len(rec) > 65535:
                continue
            tf.write(rec)
            articles[title] = (offset, len(rec))
            offset += len(rec)
            stats["articles"] += 1

    # build the index: real articles first, then redirects that point at a kept article
    keys = {}            # key -> (offset, length)
    def add(title, off, ln, is_real):
        k = norm_key(title)
        if not k:
            return
        if len(k) > KEY_LEN:
            stats["long_key"] += 1
            return
        if k in keys:
            stats["dup_key"] += 1
            return
        keys[k] = (off, ln)

    plain = sorted(articles.items(), key=lambda kv: (("(" in kv[0]), kv[0]))   # "Mercury" before "Mercury (planet)"
    for title, (off, ln) in plain:
        add(title, off, ln, True)
        if "(" in title:                                                       # "Mercury (planet)" also answers "mercury" if free
            add(re.sub(r"\s*\([^)]*\)\s*$", "", title), off, ln, False)
    for title, target in sorted(redirects):
        if target in articles:
            off, ln = articles[target]
            stats["redirects"] += 1
            add(title, off, ln, False)

    with open(os.path.join(outdir, "wiki.idx"), "wb") as xf:
        for k in sorted(keys):
            off, ln = keys[k]
            xf.write(REC.pack(k.encode("ascii").ljust(KEY_LEN, b" "), off, ln, 0))
    with open(os.path.join(outdir, "wiki.meta"), "w") as mf:
        mf.write("source: %s\nrecords: %d\narticles: %d\nredirect aliases: %d\nsummary max chars: %d\nlicence: %s\n"
                 % (source_name, len(keys), stats["articles"], stats["redirects"], MAX_SUMMARY, LICENCE))
    stats["index_records"] = len(keys)
    stats["txt_bytes"] = offset
    stats["idx_bytes"] = len(keys) * REC.size
    return stats


def lookup(outdir, query):
    """Binary search the way the repeater will do it. Returns the record text or None. Used by the tests."""
    k = norm_key(query)
    if not k or len(k) > KEY_LEN:
        return None
    want = k.encode("ascii").ljust(KEY_LEN, b" ")
    with open(os.path.join(outdir, "wiki.idx"), "rb") as xf, open(os.path.join(outdir, "wiki.txt"), "rb") as tf:
        n = os.path.getsize(xf.name) // REC.size
        lo, hi = 0, n - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            xf.seek(mid * REC.size)
            key, off, ln, _ = REC.unpack(xf.read(REC.size))
            if key == want:
                tf.seek(off)
                return tf.read(ln).decode("ascii")
            if key < want:
                lo = mid + 1
            else:
                hi = mid - 1
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("outdir")
    ap.add_argument("--jsonl", action="store_true", help="input is a jsonl file instead of a ZIM")
    a = ap.parse_args()
    entries = iter_jsonl(a.input) if a.jsonl else iter_zim(a.input)
    st = convert(entries, a.outdir, os.path.basename(a.input))
    print("done:", json.dumps(st))
    for fn in ("wiki.idx", "wiki.txt", "wiki.meta"):
        print("  %-9s %12d bytes" % (fn, os.path.getsize(os.path.join(a.outdir, fn))))
    if st["articles"] == 0:
        sys.exit("ERROR: no articles were kept - the input may not be a Wikipedia ZIM")


if __name__ == "__main__":
    main()
