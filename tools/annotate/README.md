# tools/annotate: study editions of EPUBs for the Metalio reader

Scripts to add vocabulary glosses "word (meaning)" and boxed "Explainer" notes to an EPUB
without rewriting its text, then upload it to the reader over Wi-Fi.
**Read [`EPUB-GUIDE.md`](EPUB-GUIDE.md) first.** It covers what the reader's EPUB renderer
supports, the structure rules, math, and the full workflow (in Vietnamese).

Books are **not** stored in this repository (copyright). Keep them in a workspace folder
outside the repo; every script reads and writes `work/` and `out/` under the current directory
(or under `$ANNOTATE_WORKSPACE`).

## Setup (once per computer)

```bash
git clone -b english-default https://github.com/binhphuong-ab/Metalio-E-INK4-Plus.git
mkdir -p ~/books-annotated && cd ~/books-annotated
python3 -m venv .venv && .venv/bin/pip install lxml pillow websocket-client
A=~/Metalio-E-INK4-Plus/tools/annotate   # adjust to where you cloned
```

## Scripts

| Script | Does |
|---|---|
| `chunk.py <key> <unpacked_dir> <xhtml...>` | Tag every paragraph `P0001…`, cut into ~3000-word chunks `work/<key>/chunks/cNN.txt` |
| `validate.py <key> cNN` | Check an agent's `work/<key>/ann/cNN.json` (terms verbatim, anchors in chunk) |
| `merge.py <key> out/<name>.epub [--cover img] [--split file.xhtml]` | Insert glosses + `<p class="ap-note">` notes, add an "About" page, cover, split huge XHTML |
| `textcheck.py <orig.epub> <new.epub>` | Prove the original text is unchanged (before `--split`) |
| `upload.py <ip> /metalio/e-ink/books <file> [--replace]` | Upload to the reader's Wi-Fi transfer server |
| `cover_bench.py` | Example of a drawn grayscale 9:16 cover (Pillow; uses the repo's Literata and macOS Avenir) |

Boxed notes need firmware **2.0.79-en.4** or later. On older firmware they show as plain paragraphs.
