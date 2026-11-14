Dictionaries (StarDict)
=======================

Put each StarDict dictionary in its own folder here, named like its files:

  dictionaries/<Name>/<Name>.ifo
  dictionaries/<Name>/<Name>.idx      (.idx.gz is not supported: gunzip it first)
  dictionaries/<Name>/<Name>.dict.dz  (or .dict)
  dictionaries/<Name>/<Name>.syn      (optional)

Several dictionaries are searched in folder-name order; the first hit wins.
Folders starting with "_" or "." are ignored. No dictionary is shipped here.

The first lookup builds a small sampled index in
temp/books/cache/dict/ (about a second); later lookups are instant.

Using it while reading:
  - Hold Home: the word in the middle of the page is highlighted.
  - Tap a word: its definition opens full screen.
  - Page keys, or tapping the left third / the rest of the screen: previous / next page.
  - Tap the headword line: back to the book page to pick another word.
  - Short press Home: close the dictionary.

_fonts/ holds the definition fonts (Noto Serif 32 px, SIL Open Font License),
built with tools/epdfont/fontconvert.py. Without them the book font is used.
