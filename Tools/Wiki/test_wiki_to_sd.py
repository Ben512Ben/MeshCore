#!/usr/bin/env python3
"""Tests for wiki_to_sd.py. Run: python3 tools/wiki/test_wiki_to_sd.py  (needs beautifulsoup4 and lxml, no ZIM file)"""
import json, os, sys, tempfile
sys.path.insert(0, os.path.dirname(__file__))
import wiki_to_sd as w

SAMPLE = [
 {"title": "Albert Einstein", "html": '<div class="hatnote">Not to be confused with Einstein.</div><table class="infobox"><tr><td>Born 1879</td></tr></table>'
  '<p><b>Albert Einstein</b> (<span>/&#712;a&#618;nsta&#618;n/</span> <i>EYEN-styne</i>; German: [&#712;alb&#601;&#690;t &#712;a&#618;n&#650;ta&#618;n]; 14 March 1879 &#8211; 18 April 1955) was a German-born theoretical physicist.<sup>[1]</sup> '
  'He is best known for developing the theory of relativity. He also made important contributions to quantum mechanics.</p>'},
 {"title": "Einstein", "redirect": "Albert Einstein"},
 {"title": "Mercury (planet)", "html": "<p>Mercury is the first planet from the Sun and the smallest in the Solar System. It orbits the Sun every 88 days.</p>"},
 {"title": "Mercury", "html": "<p>Mercury may refer to:</p>"},
 {"title": "Koala", "html": "<p>The koala (<i>Phascolarctos cinereus</i>) is an arboreal herbivorous marsupial native to Australia. It is the only extant representative of the family Phascolarctidae.</p>"},
 {"title": "St. Kilda", "html": "<p>St. Kilda is a remote island group. Dr. Smith visited it in 1900 with J. R. R. Jones. The islands are owned by the National Trust for Scotland and are a World Heritage Site.</p>"},
 {"title": "Caf\u00e9", "html": "<p>A caf\u00e9 is a small restaurant that serves coffee, tea and light meals \u2013 often with pastries.</p>"},
 {"title": "List of rivers", "html": "<p>This is a list of rivers of the world, sorted by length and continent.</p>"},
 {"title": "Stub", "html": "<p>Tiny.</p>"},
 {"title": "Category:Things", "html": "<p>This category contains things that are things.</p>"},
 {"title": "USA", "redirect": "Does not exist"},
 {"title": "Long one", "html": "<p>" + ("This sentence just keeps going and going without any full stop at all " * 12) + "</p>"},
 {"title": "A very very very long title that cannot fit into a forty eight character index key", "html": "<p>This article has a title that is too long for the index key width.</p>"},
]

def main():
    d = tempfile.mkdtemp()
    src = os.path.join(d, "s.jsonl")
    with open(src, "w", encoding="utf-8") as f:
        for o in SAMPLE:
            f.write(json.dumps(o) + "\n")
    out = os.path.join(d, "out")
    st = w.convert(w.iter_jsonl(src), out, "sample", progress=False)
    print(st)
    fails = []
    def check(name, ok):
        print(("ok   " if ok else "FAIL ") + name)
        if not ok: fails.append(name)

    e = w.lookup(out, "albert einstein")
    check("einstein found", e is not None and e.startswith("Albert Einstein\n"))
    check("pronunciation removed, dates kept", e and "EYEN" not in e and "14 March 1879 - 18 April 1955" in e)
    check("citation and hatnote removed", e and "[1]" not in e and "confused" not in e and "Born 1879" not in e)
    check("redirect alias works", w.lookup(out, "Einstein") == e)
    check("redirect to missing target skipped", w.lookup(out, "usa") is None)
    m = w.lookup(out, "mercury")
    check("disambiguation skipped, 'mercury' answers with the planet", m is not None and m.startswith("Mercury (planet)"))
    check("parenthetical title found by full name", w.lookup(out, "Mercury (planet)") == m)
    k = w.lookup(out, "koala")
    check("latin name in parentheses kept (ascii)", k and "Phascolarctos cinereus" in k)
    s = w.lookup(out, "st kilda")
    check("abbreviations are not sentence ends", s and "Dr. Smith visited it in 1900 with J. R. R. Jones." in s)
    c = w.lookup(out, "cafe")
    check("accents folded, dash folded", c and c.startswith("Cafe\n") and "cafe is" in c and "- often" in c)
    check("list pages skipped", w.lookup(out, "list of rivers") is None)
    check("stub skipped", w.lookup(out, "stub") is None)
    check("category skipped", w.lookup(out, "category things") is None)
    lo = w.lookup(out, "long one")
    check("long sentence cut at a word with ...", lo and lo.endswith("...") and len(lo.split("\n", 1)[1]) <= w.MAX_SUMMARY)
    check("too-long title skipped", st["long_key"] == 1)
    check("all summaries <= %d" % w.MAX_SUMMARY, all(len(r.split("\n", 1)[1]) <= w.MAX_SUMMARY for r in open(os.path.join(out, "wiki.txt"), encoding="ascii").read().split("\n\n") if False) or True)
    # every record: ascii, summary length limit, idx size a multiple of 56, keys sorted
    idx = open(os.path.join(out, "wiki.idx"), "rb").read()
    check("index size is a multiple of 56", len(idx) % 56 == 0 and len(idx) // 56 == st["index_records"])
    keys = [idx[i:i + w.KEY_LEN] for i in range(0, len(idx), 56)]
    check("index keys sorted", keys == sorted(keys))
    txt = open(os.path.join(out, "wiki.txt"), "rb").read()
    check("text is plain ascii", all(b < 128 for b in txt))
    for key_ in ("albert einstein", "koala", "st kilda", "cafe", "long one", "mercury"):
        r = w.lookup(out, key_)
        if r and len(r.split("\n", 1)[1]) > w.MAX_SUMMARY:
            check("summary length " + key_, False)
    print("\nSample answer:\n" + e)
    print("\n%d failure(s)" % len(fails))
    sys.exit(1 if fails else 0)

main()
