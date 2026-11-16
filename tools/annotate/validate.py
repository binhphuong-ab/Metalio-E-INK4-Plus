#!/usr/bin/env python3
"""Check an annotation file against its chunk.

usage: validate.py <book_key> <cNN>   (reads work/<key>/ann/<cNN>.json)
Exit 0 and prints "OK ..." when every gloss term occurs verbatim in its
paragraph and every note anchors to a paragraph of this chunk.
"""
import json, re, sys
import os
from pathlib import Path

root = Path(os.environ.get("ANNOTATE_WORKSPACE", os.getcwd())).resolve() / "work"


def load_chunk(key, name):
    paras = {}
    for block in (root / key / "chunks" / f"{name}.txt").read_text(encoding="utf-8").split("\n\n"):
        m = re.match(r"\[(P\d{4})\](?: <h\d>)? (.*)", block.strip(), re.S)
        if m:
            paras[m.group(1)] = m.group(2)
    return paras


def main():
    key, name = sys.argv[1], sys.argv[2]
    paras = load_chunk(key, name)
    path = root / key / "ann" / f"{name}.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:
        print(f"FAIL cannot read {path}: {e}"); sys.exit(1)
    errs = []
    gl = data.get("glosses", [])
    notes = data.get("notes", [])
    seen = set()
    for i, g in enumerate(gl):
        pid, term, gloss = g.get("pid"), g.get("term", ""), g.get("gloss", "")
        if pid not in paras:
            errs.append(f"gloss {i}: pid {pid} not in chunk")
        elif term not in paras[pid]:
            errs.append(f"gloss {i}: term {term!r} not found verbatim in {pid}")
        if not gloss or len(gloss.split()) > 12:
            errs.append(f"gloss {i}: gloss for {term!r} empty or longer than 12 words")
        if "(" in gloss or ")" in gloss:
            errs.append(f"gloss {i}: no parentheses inside gloss text")
        k = term.lower()
        if k in seen:
            errs.append(f"gloss {i}: duplicate term {term!r}")
        seen.add(k)
    for i, n in enumerate(notes):
        if n.get("after") not in paras:
            errs.append(f"note {i}: anchor {n.get('after')} not in chunk")
        w = len(n.get("text", "").split())
        if not 25 <= w <= 140:
            errs.append(f"note {i}: {w} words (want 25-140)")
    if errs:
        print("FAIL\n" + "\n".join(errs)); sys.exit(1)
    print(f"OK {key}/{name}: {len(gl)} glosses, {len(notes)} notes")


if __name__ == "__main__":
    main()
