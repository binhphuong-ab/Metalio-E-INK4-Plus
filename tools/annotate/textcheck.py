#!/usr/bin/env python3
"""Prove the book text is unchanged: strip ap-gloss/ap-note additions from the annotated EPUB
and compare every XHTML body with the original.

usage: textcheck.py <original.epub> <annotated.epub>
(Only works before --split, i.e. when both EPUBs have the same XHTML file names.)
"""
import sys, zipfile, re
from lxml import etree
def text(z, name, strip):
    doc = etree.fromstring(z.read(name))
    if strip:
        for el in doc.xpath('//*[@class="ap-gloss" or @class="ap-note"]'):
            p = el.getparent(); t = el.tail or ""
            prev = el.getprevious()
            if prev is not None: prev.tail = (prev.tail or "") + t
            else: p.text = (p.text or "") + t
            p.remove(el)
    body = doc.find("{http://www.w3.org/1999/xhtml}body")
    return re.sub(r"\s+", " ", "".join(body.itertext())).strip()
a, b = zipfile.ZipFile(sys.argv[1]), zipfile.ZipFile(sys.argv[2])
bad = 0
for n in a.namelist():
    if n.endswith(".xhtml") and "nav" not in n:
        ta, tb = text(a, n, False), text(b, n, True)
        if ta != tb:
            bad += 1; i = next(i for i,(x,y) in enumerate(zip(ta,tb)) if x!=y) if any(x!=y for x,y in zip(ta,tb)) else min(len(ta),len(tb))
            print("DIFF", n, repr(ta[i-40:i+40]), "|", repr(tb[i-40:i+40]))
print("files differing:", bad, "| total xhtml in new:", sum(1 for n in b.namelist() if n.endswith('.xhtml')), "| mimetype first:", b.namelist()[0]=="mimetype", "| testzip:", b.testzip())
