#!/usr/bin/env python3
"""Tag every text block of an unpacked EPUB with a stable id and cut the text
into ~N-word chunks for annotation.

usage: chunk.py <book_key> <unpacked_dir> <content.xhtml>... [--words 3000]

Writes:
  work/<key>/tree/...            copy of the unpacked EPUB, blocks carry id="ap-<key>-NNNN"
  work/<key>/chunks/cNN.txt      "[P0123] paragraph text" lines (only what the agent reads)
  work/<key>/chunks/index.json   chunk -> pids, file of every pid
"""
import json, re, shutil, sys
import os
from pathlib import Path
from lxml import etree

XHTML = "http://www.w3.org/1999/xhtml"
BLOCKS = {"p", "li", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote", "td"}


def text_of(el):
    return re.sub(r"\s+", " ", "".join(el.itertext())).strip()


def main():
    args = sys.argv[1:]
    words = 3000
    if "--words" in args:
        i = args.index("--words"); words = int(args[i + 1]); del args[i:i + 2]
    key, src, files = args[0], Path(args[1]), args[2:]
    root = Path(os.environ.get("ANNOTATE_WORKSPACE", os.getcwd())).resolve() / "work" / key
    tree_dir = root / "tree"
    if tree_dir.exists():
        shutil.rmtree(tree_dir)
    shutil.copytree(src, tree_dir)
    (root / "chunks").mkdir(parents=True, exist_ok=True)
    for old in (root / "chunks").glob("c*.txt"):
        old.unlink()

    paras = []  # (pid, file, kind, text)
    n = 0
    for rel in files:
        path = tree_dir / rel
        doc = etree.parse(str(path))
        body = doc.getroot().find(f"{{{XHTML}}}body")
        for el in body.iter():
            if not isinstance(el.tag, str):
                continue
            tag = etree.QName(el).localname
            if tag not in BLOCKS:
                continue
            # innermost block only (a <li> holding <p> -> tag the <p>)
            if any(etree.QName(d).localname in BLOCKS for d in el.iterdescendants() if isinstance(d.tag, str)):
                continue
            t = text_of(el)
            if not t:
                continue
            n += 1
            pid = f"P{n:04d}"
            el.set("id", el.get("id") or f"ap-{key}-{n:04d}")
            el.set("data-ap", pid)
            paras.append((pid, rel, tag, t))
        doc.write(str(path), xml_declaration=True, encoding="utf-8")

    chunks, cur, cw = [], [], 0
    for p in paras:
        w = len(p[3].split())
        if cur and cw + w > words:
            chunks.append(cur); cur, cw = [], 0
        cur.append(p); cw += w
    if cur:
        if chunks and cw < words // 4:
            chunks[-1].extend(cur)  # fold a short tail into the previous chunk
        else:
            chunks.append(cur)

    index = {"key": key, "chunks": [], "pid_file": {p[0]: p[1] for p in paras}}
    for i, ch in enumerate(chunks, 1):
        name = f"c{i:02d}"
        lines = []
        for pid, rel, tag, t in ch:
            mark = f"[{pid}]" + (f" <{tag}>" if tag.startswith("h") else "")
            lines.append(f"{mark} {t}")
        (root / "chunks" / f"{name}.txt").write_text("\n\n".join(lines) + "\n", encoding="utf-8")
        index["chunks"].append({"name": name, "first": ch[0][0], "last": ch[-1][0],
                                "words": sum(len(p[3].split()) for p in ch)})
    (root / "chunks" / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")
    print(f"{key}: {len(paras)} blocks, {len(chunks)} chunks")
    for c in index["chunks"]:
        print(f"  {c['name']} {c['first']}-{c['last']} {c['words']}w")


if __name__ == "__main__":
    main()
