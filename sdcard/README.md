# SD card reading fonts (Vietnamese + English)

Copy the `metalio/` folder in this directory to the **root of the device's SD card**
(merge with the existing `metalio/` folder). With the virtual USB disk enabled
(Transfer → USB) the card shows up as a normal drive.

| File | Copy to (on the SD card) | Notes |
|---|---|---|
| `fonts/literata_vn_25_2.ef` | `metalio/e-ink/fonts/` | Literata, pre-rendered bitmap, 25 px, 2 bpp |
| `fonts/bevietnam_25_2.ef` | `metalio/e-ink/fonts/` | Be Vietnam Pro, pre-rendered bitmap, 25 px, 2 bpp |
| `fonts_ttf/Literata.ttf` | `metalio/e-ink/fonts_ttf/` | Variable TTF, any size; full Vietnamese coverage |
| `fonts_ttf/NotoSerif-Regular.ttf` | `metalio/e-ink/fonts_ttf/` | Static TTF, any size; full Vietnamese coverage |

On the device: open a book → **Reading settings → Choose font**. TTF fonts can be
used directly or converted on the device (**Import TTF**).

`.ef` files are fixed-size bitmaps (name = `<font>_<size px>_<bpp>`): fast, but one
size only. TTF files scale to any size.

## Licenses

All fonts are licensed under the **SIL Open Font License 1.1** — see `licenses/`.
The `.ef` files are bitmap conversions (Modified Versions under the OFL) of Literata
and Be Vietnam Pro and remain under the same license. The fonts may be used,
modified and redistributed, but not sold by themselves. They are not covered by the
MIT license of the firmware source.

- Literata — © 2017 The Literata Project Authors, https://github.com/googlefonts/literata
- Noto Serif — © 2022 The Noto Project Authors, https://github.com/notofonts/latin-greek-cyrillic
- Be Vietnam Pro — © 2021 The Be Vietnam Pro Project Authors, https://github.com/bettergui/BeVietnamPro
