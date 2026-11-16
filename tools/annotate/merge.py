#!/usr/bin/env python3
"""Insert agent annotations into the tagged EPUB tree and zip the annotated book.

usage: merge.py <book_key> <out.epub> [--title-suffix " (Annotated)"] [--cover work/<key>/cover.jpg]

- glosses: " (meaning)" right after the first occurrence of the term in its paragraph,
  once per book (later repeats of the same term are skipped);
- notes: a separate "Explainer" paragraph after the anchor paragraph (after the whole
  list/table when the anchor sits inside one);
- an "About this edition" page before the first chapter; optional cover image.
The book text itself is never rewritten: only spans/paragraphs are added.
"""
import json, re, shutil, sys, uuid, zipfile
import os
from pathlib import Path
from lxml import etree

XHTML = "http://www.w3.org/1999/xhtml"
OPF = "http://www.idpf.org/2007/opf"
DC = "http://purl.org/dc/elements/1.1/"
ROOT = Path(os.environ.get("ANNOTATE_WORKSPACE", os.getcwd())).resolve()  # workspace: work/, out/
CONTAINERS = {"li", "ul", "ol", "td", "tr", "tbody", "thead", "table", "blockquote"}
CSS = (".ap-gloss{font-style:italic;font-size:0.88em}"
       ".ap-note{margin:0.9em 0 0.9em 1.2em;padding-left:0.6em;border-left:3px solid #777;"
       "font-size:0.92em;text-indent:0;font-style:normal}"
       ".ap-note b{font-weight:bold}")


def q(tag):
    return f"{{{XHTML}}}{tag}"


def local(el):
    return etree.QName(el).localname if isinstance(el.tag, str) else ""


def term_regex(term):
    pat = re.escape(term)
    if re.match(r"\w", term):
        pat = r"(?<!\w)" + pat
    if re.search(r"\w$", term):
        pat += r"(?!\w)"
    return re.compile(pat)


def text_slots(el):
    """(owner, attr) pairs holding el's own text in document order, skipping gloss spans."""
    if el.text:
        yield el, "text"
    for child in el:
        if not isinstance(child.tag, str):
            if child.tail:
                yield child, "tail"
            continue
        if child.get("class") != "ap-gloss":
            yield from text_slots(child)
        if child.tail:
            yield child, "tail"


def insert_gloss(el, term, gloss):
    rx = term_regex(term)
    for owner, attr in list(text_slots(el)):
        s = getattr(owner, attr)
        m = rx.search(s)
        if not m:
            continue
        span = etree.Element(q("span"))
        span.set("class", "ap-gloss")
        span.text = f" ({gloss})"
        span.tail = s[m.end():]
        setattr(owner, attr, s[:m.end()])
        if attr == "text":
            owner.insert(0, span)
        else:
            owner.addnext(span)
        return True
    return False


def make_note(text):
    p = etree.Element(q("p"))
    p.set("class", "ap-note")
    b = etree.SubElement(p, q("b"))
    b.text = "Explainer: "
    b.tail = text.strip()
    return p


def add_style(doc):
    head = doc.getroot().find(q("head"))
    st = etree.SubElement(head, q("style"))
    st.set("type", "text/css")
    st.text = CSS


ABOUT = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en"><head><title>About this edition</title>
<style type="text/css">{css} h2{{text-align:center}} p{{text-indent:0;margin:0.7em 0}}</style></head>
<body><h2>About this annotated edition</h2>
<p>This edition keeps the full original text and adds two kinds of help for readers of English as a second language.</p>
<p><b>Vocabulary.</b> Words and phrases at CEFR level C1 and above, idioms, slang and specialist terms are followed by a short meaning in brackets, in italics, <i>the first time they appear</i>{example}.</p>
<p class="ap-note"><b>Explainer: </b>Paragraphs like this one are added after key passages. They give background on people, companies and events, unpack difficult ideas, and explain jokes or cultural references.</p>
<p>The notes were written with the help of AI and may contain mistakes; the original text is unchanged.</p>
</body></html>
"""


def split_large(build, opf_dir, manifest, spine, href, max_bytes):
    """Cut one huge spine document into several files: a new file at every h1/h2, and
    another whenever a part grows past max_bytes. The reader paginates a whole spine
    item at once, so a 500 KB document is cut short on the device."""
    path = opf_dir / href
    doc = etree.parse(str(path))
    root = doc.getroot()
    head, body = root.find(q("head")), root.find(q("body"))
    parts, cur, size = [], [], 0
    for child in list(body):
        n = len(etree.tostring(child))
        heading = local(child) in ("h1", "h2")
        if cur and (heading or size + n > max_bytes):
            parts.append(cur); cur, size = [], 0
        cur.append(child); size += n
    if cur:
        parts.append(cur)
    stem = Path(href).stem
    id_file, names = {}, []
    for i, children in enumerate(parts, 1):
        name = f"{stem}_{i:02d}.xhtml"
        names.append(name)
        html = etree.Element(root.tag, nsmap=root.nsmap)
        for k, v in root.attrib.items():
            html.set(k, v)
        html.append(etree.fromstring(etree.tostring(head)))
        nb = etree.SubElement(html, q("body"))
        for k, v in body.attrib.items():
            nb.set(k, v)
        for c in children:
            nb.append(c)
            for el in c.iter():
                if isinstance(el.tag, str) and el.get("id"):
                    id_file[el.get("id")] = name
        etree.ElementTree(html).write(str(path.parent / name), xml_declaration=True, encoding="utf-8")
    path.unlink()
    rel_dir = Path(href).parent.as_posix()
    rel = (lambda n: n if rel_dir in ("", ".") else f"{rel_dir}/{n}")
    item = next(i for i in manifest if i.get("href") == href)
    old_id = item.get("id")
    ref = next(r for r in spine if r.get("idref") == old_id)
    for i, name in enumerate(names, 1):
        it = etree.Element(f"{{{OPF}}}item", href=rel(name), id=f"{old_id}-{i:02d}")
        it.set("media-type", "application/xhtml+xml")
        item.addprevious(it)
        ref.addprevious(etree.Element(f"{{{OPF}}}itemref", idref=f"{old_id}-{i:02d}"))
    manifest.remove(item); spine.remove(ref)
    # fix links in every other xhtml (nav included)
    base = Path(href).name
    for f in opf_dir.rglob("*.xhtml"):
        s = f.read_text(encoding="utf-8")
        if base not in s:
            continue
        s = re.sub(re.escape(base) + r"#([^\"']+)", lambda m: f"{id_file.get(m.group(1), names[0])}#{m.group(1)}", s)
        s = re.sub(re.escape(base) + r"(?=[\"'])", names[0], s)
        f.write_text(s, encoding="utf-8")
    sizes = [(path.parent / n).stat().st_size // 1024 for n in names]
    print(f"  split {href} into {len(names)} files, KB: {sizes}")


def main():
    args = sys.argv[1:]
    opts = {"--title-suffix": " (Annotated)", "--cover": None, "--split": None, "--split-bytes": "90000"}
    for k in list(opts):
        if k in args:
            i = args.index(k); opts[k] = args[i + 1]; del args[i:i + 2]
    key, out = args[0], Path(args[1])
    work = ROOT / "work" / key
    build = work / "build"
    if build.exists():
        shutil.rmtree(build)
    shutil.copytree(work / "tree", build)
    index = json.loads((work / "chunks" / "index.json").read_text(encoding="utf-8"))

    glosses, notes, missing = [], [], []
    for c in index["chunks"]:
        f = work / "ann" / f"{c['name']}.json"
        if not f.exists():
            missing.append(c["name"]); continue
        d = json.loads(f.read_text(encoding="utf-8"))
        glosses += d.get("glosses", [])
        notes += d.get("notes", [])
    if missing:
        sys.exit(f"missing annotations: {missing}")

    docs, by_pid = {}, {}
    for rel in sorted(set(index["pid_file"].values())):
        doc = etree.parse(str(build / rel))
        docs[rel] = doc
        for el in doc.getroot().iter():
            if isinstance(el.tag, str) and el.get("data-ap"):
                by_pid[el.get("data-ap")] = el

    glosses.sort(key=lambda g: g["pid"])
    seen, g_ok, g_dup, g_miss = set(), 0, 0, []
    for g in glosses:
        k = g["term"].lower()
        if k in seen:
            g_dup += 1; continue
        el = by_pid.get(g["pid"])
        if el is not None and insert_gloss(el, g["term"], g["gloss"].strip().rstrip(".")):
            seen.add(k); g_ok += 1
        else:
            g_miss.append(f"{g['pid']}:{g['term']}")

    n_ok = 0
    last_after = {}
    for n in sorted(notes, key=lambda n: n["after"]):
        el = by_pid.get(n["after"])
        if el is None:
            continue
        target = el
        while target.getparent() is not None and local(target.getparent()) in CONTAINERS | {"p"} \
                and local(target.getparent()) != "body":
            target = target.getparent()
        if local(target) in {"li", "td", "tr"}:
            continue
        anchor = last_after.get(id(target), target)
        note = make_note(n["text"])
        note.tail = anchor.tail
        anchor.tail = None
        anchor.addnext(note)
        last_after[id(target)] = note
        n_ok += 1

    for rel, doc in docs.items():
        for el in doc.getroot().iter():
            if isinstance(el.tag, str) and el.get("data-ap"):
                del el.attrib["data-ap"]
                if (el.get("id") or "").startswith(f"ap-{key}-"):
                    del el.attrib["id"]
        add_style(doc)
        doc.write(str(build / rel), xml_declaration=True, encoding="utf-8")

    # --- package: title, about page, cover
    container = etree.parse(str(build / "META-INF" / "container.xml"))
    opf_rel = container.getroot().find(".//{*}rootfile").get("full-path")
    opf_path = build / opf_rel
    opf_dir = opf_path.parent
    opf = etree.parse(str(opf_path))
    meta = opf.getroot().find(f"{{{OPF}}}metadata")
    manifest = opf.getroot().find(f"{{{OPF}}}manifest")
    spine = opf.getroot().find(f"{{{OPF}}}spine")
    title = meta.find(f"{{{DC}}}title")
    title.text = title.text.replace(" | ", " — ") + opts["--title-suffix"]
    uid_id = opf.getroot().get("unique-identifier")
    for ident in meta.findall(f"{{{DC}}}identifier"):
        if ident.get("id") == uid_id:
            ident.text = f"urn:uuid:{uuid.uuid4()}"

    first_rel = index["pid_file"][min(index["pid_file"])]
    first_href = Path(first_rel).relative_to(Path(opf_rel).parent).as_posix() \
        if first_rel.startswith(str(Path(opf_rel).parent)) else first_rel
    about_href = str(Path(first_href).parent / "ap_about.xhtml").lstrip("./") if "/" in first_href else "ap_about.xhtml"
    example = " — for example, <i>ubiquitous</i><span class=\"ap-gloss\"> (found everywhere)</span>"
    (opf_dir / about_href).write_text(ABOUT.format(css=CSS, example=example), encoding="utf-8")
    it = etree.SubElement(manifest, f"{{{OPF}}}item", href=about_href, id="ap-about")
    it.set("media-type", "application/xhtml+xml")
    first_item = next(i for i in manifest if i.get("href") == first_href)
    first_ref = next(r for r in spine if r.get("idref") == first_item.get("id"))
    ref = etree.Element(f"{{{OPF}}}itemref", idref="ap-about")
    first_ref.addprevious(ref)

    if opts["--cover"]:
        cov = ROOT / opts["--cover"]
        (opf_dir / "images").mkdir(exist_ok=True)
        shutil.copy(cov, opf_dir / "images" / "cover.jpg")
        ci = etree.SubElement(manifest, f"{{{OPF}}}item", href="images/cover.jpg", id="cover-image")
        ci.set("media-type", "image/jpeg"); ci.set("properties", "cover-image")
        m = etree.SubElement(meta, f"{{{OPF}}}meta", name="cover", content="cover-image")
        (opf_dir / "ap_cover.xhtml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Cover</title>'
            '<style type="text/css">body{margin:0;padding:0;text-align:center}img{max-width:100%;max-height:100%}</style>'
            '</head><body><div><img src="images/cover.jpg" alt="Cover"/></div></body></html>\n', encoding="utf-8")
        cx = etree.SubElement(manifest, f"{{{OPF}}}item", href="ap_cover.xhtml", id="ap-cover")
        cx.set("media-type", "application/xhtml+xml")
        spine.insert(0, etree.Element(f"{{{OPF}}}itemref", idref="ap-cover"))
    if opts["--split"]:
        split_large(build, opf_dir, manifest, spine, opts["--split"], int(opts["--split-bytes"]))
    opf.write(str(opf_path), xml_declaration=True, encoding="utf-8")

    # --- zip (mimetype first, stored)
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w") as z:
        z.write(build / "mimetype", "mimetype", compress_type=zipfile.ZIP_STORED)
        for p in sorted(build.rglob("*")):
            if p.is_file() and p.name != "mimetype" and p.name != ".DS_Store":
                z.write(p, p.relative_to(build).as_posix(), compress_type=zipfile.ZIP_DEFLATED)
    print(f"{key}: glosses {g_ok} inserted, {g_dup} repeats skipped, {len(g_miss)} not found; notes {n_ok}/{len(notes)}")
    if g_miss:
        print("  not found:", ", ".join(g_miss[:15]), "..." if len(g_miss) > 15 else "")
    print(f"  -> {out} ({out.stat().st_size:,} B)")


if __name__ == "__main__":
    main()
