# Metalio E-INK4 Plus — Offline English firmware `2.0.79-en.3`

> This folder holds the installer scripts only. Download the ready-to-flash zip from
> [Releases](https://github.com/binhphuong-ab/Metalio-E-INK4-Plus/releases/tag/v2.0.79-en.3).
> Thư mục này chỉ chứa script; tải gói zip có sẵn file nạp ở trang Releases.

**English** · [Tiếng Việt](#tiếng-việt)

Community build of the official open-source Metalio E-INK4 Plus firmware (MIT, v2.0.79),
made for reading in **English and Vietnamese**. Not an official Metalio/CloudZao release.

## What's different from stock

- **English-only UI** — no Chinese anywhere in the interface; language locked to English.
- **Offline** — Ask AI, WorkBuddy, Tasks, weather and the cloud settings are removed
  (the open-source code has no cloud endpoints, so they could not work anyway).
- **Better reading of English/Vietnamese books**
  - words are never split across lines;
  - justified lines stretch at spaces only (no more `t h e  t i m e`);
  - full Vietnamese letters in the UI font (titles, library, footer, TOC);
  - hidden EPUB sections (print page lists) are no longer shown as pages of numbers.
- **Redesigned** standby screen, home date line and Books home.
- **Offline dictionary** (new in en.2) — hold **Home** while reading, tap any word, and its
  definition opens full screen. See [Dictionary](#dictionary).
- **Back to your page** (new in en.3) — switch the reader off while reading and it reopens
  the same book at the same page when switched on. The standby screen can show the page
  you are reading, too. See [Standby on the reading page](#standby-on-the-reading-page).

## Requirements

- Metalio E-INK4 Plus (ESP32-S31, 4.7") **running the stock Metalio firmware** (or an earlier `en` build — upgrading is the same `./flash.sh`).
  If you installed kiikoread / mogo / dpReader, restore the stock firmware first.
- macOS or Linux, Python 3, and esptool 5.x: `pip install --upgrade esptool`
- A USB-C **data** cable.

## Install

1. Plug the reader into the computer and press the power button so it is awake.
   Close any browser tab with a web flasher.
2. In this folder run:
   ```bash
   ./flash.sh
   ```
   The script checks the device, **backs up the whole 16 MB flash** into `backups/`
   (2–3 minutes, verified against the chip), then installs the firmware.
   Your settings and Wi-Fi are kept. Use `./flash.sh --dry-run` to only check and back up.
3. **Keep the file in `backups/`.** It is your way back to stock.

**Undo:** `./restore.sh backups/<your backup>.bin`

**Windows:** install esptool, find the COM port in Device Manager, back up first, then:
```
esptool --port COM5 read-flash 0 ALL backup.bin
esptool --port COM5 write-flash 0x80000 metalio-e-ink4-plus-2.0.79-en.3-app.bin 0xae4000 metalio-e-ink4-plus-2.0.79-en.3-fontpack.bin
```

## Reading fonts (optional)

Copy `sdcard/metalio` from the source branch to the root of the SD card (enable
**Transfer → USB** on the reader). It adds Literata and Noto Serif with Vietnamese
support (SIL OFL 1.1). Then: open a book → **Reading settings → Choose font**.

## Dictionary

1. Turn on **Transfer → USB** on the reader so the SD card shows up on the computer.
2. Copy the `metalio` folder from the zip's `sdcard/` to the root of the SD card
   (definition fonts).
3. Copy a StarDict dictionary to `metalio/e-ink/dictionaries/<Name>/<Name>.ifo|.idx|.dict.dz|.syn`.
   The release has an English–Vietnamese one ready: unzip
   `AnhVietAnh-StarDict.zip` to the root of the SD card.
4. Eject the disk, turn USB transfer off.

Use: open a book, **hold Home** (the middle word is highlighted), **tap a word** →
its definition fills the screen. Page keys or tapping the left third / the rest of the
screen turn definition pages; tap the headword line to go back to the page; a
**short Home press** closes the dictionary. The first lookup builds a small index
(about a second); after that lookups are instant.

## Standby on the reading page

The zip's `sdcard/metalio/e-ink/wallpaper/` has three transparent overlays (copy them to
the SD card like the fonts above):

- `standby-reading-page-moon-center.png` — the page with a large faint moon in the middle;
- `standby-reading-page-moon.png` — the page with a small moon in the top-right corner;
- `standby-reading-page.png` — the page exactly as it is.

Open **Wallpaper**, pick one, tap **Set as standby** → **Transparent wallpaper**. Standby
then keeps the page you were reading under the overlay. **Clear selection** brings the
classic standby screen back.

Power-on resume needs no setup: if a book is open when the reader is switched off (also
from standby), the next power-on goes straight back to that page (Home shows for about a
second first). Switched off anywhere else, it starts on Home as usual.

## Notes

- Wi-Fi is **2.4 GHz only**. Connect once so the clock can sync (NTP); there is no
  manual clock setting.
- No over-the-air updates in this build; install new versions with `flash.sh`.
- Use at your own risk. Source: branch `english-default` of
  https://github.com/binhphuong-ab/Metalio-E-INK4-Plus

---

## Tiếng Việt

Bản firmware cộng đồng dựa trên mã nguồn mở chính hãng của Metalio E-INK4 Plus (MIT, v2.0.79),
dành cho người đọc sách **tiếng Anh và tiếng Việt**. Không phải bản chính thức của Metalio/CloudZao.

### Khác gì bản gốc

- **Giao diện chỉ tiếng Anh**: không còn chữ Trung, ngôn ngữ khoá ở tiếng Anh.
- **Offline**: bỏ Ask AI, WorkBuddy, Tasks, thời tiết và phần cài đặt đám mây
  (mã nguồn mở không có địa chỉ máy chủ nên các tính năng này vốn không chạy được).
- **Đọc sách tiếng Anh/Việt tốt hơn**
  - không cắt đôi từ khi xuống dòng;
  - căn đều chỉ giãn ở dấu cách (hết lỗi `t h e  t i m e`);
  - font giao diện đủ chữ Việt (tên sách, thư viện, chân trang, mục lục);
  - không còn hiện các trang toàn số trang của bản in trong mục lục EPUB.
- **Thiết kế lại** màn hình chờ, dòng ngày trên trang chủ và trang Books.
- **Từ điển offline** (mới ở en.2): khi đọc, **đè giữ Home**, chạm vào từ, nghĩa hiện toàn màn hình.
- **Trở lại trang đang đọc** (mới ở en.3): tắt máy khi đang đọc, bật lên máy mở lại đúng sách,
  đúng trang. Màn hình chờ cũng có thể giữ nguyên trang đang đọc.

### Yêu cầu

- Metalio E-INK4 Plus (ESP32-S31, 4,7") **đang chạy firmware gốc của Metalio** (hoặc bản `en` cũ — nâng cấp cũng chạy `./flash.sh`).
  Nếu đã cài kiikoread / mogo / dpReader, hãy cài lại firmware gốc trước.
- macOS hoặc Linux, Python 3 và esptool 5.x: `pip install --upgrade esptool`
- Cáp USB-C **có truyền dữ liệu**.

### Cài đặt

1. Cắm máy đọc vào máy tính, bấm nút nguồn để máy thức. Đóng mọi tab nạp firmware trên trình duyệt.
2. Trong thư mục này chạy:
   ```bash
   ./flash.sh
   ```
   Script kiểm tra máy, **sao lưu toàn bộ 16 MB flash** vào `backups/` (2–3 phút, có đối chiếu
   với chip), rồi mới cài firmware. Cài đặt và Wi-Fi được giữ nguyên.
   Chạy `./flash.sh --dry-run` nếu chỉ muốn kiểm tra và sao lưu.
3. **Giữ lại file trong `backups/`.** Đó là đường quay về bản gốc.

**Quay về bản gốc:** `./restore.sh backups/<file sao lưu>.bin`

**Windows:** cài esptool, xem cổng COM trong Device Manager, sao lưu trước rồi nạp
(lệnh như phần tiếng Anh ở trên).

### Font đọc sách (tuỳ chọn)

Chép thư mục `sdcard/metalio` trong nhánh mã nguồn vào gốc thẻ SD (bật **Transfer → USB**
trên máy). Gồm Literata và Noto Serif hỗ trợ tiếng Việt (SIL OFL 1.1).
Sau đó: mở sách → **Reading settings → Choose font**.

### Từ điển

1. Bật **Transfer → USB** trên máy để thẻ SD hiện lên máy tính.
2. Chép thư mục `metalio` trong `sdcard/` của gói zip vào gốc thẻ SD (font hiển thị nghĩa).
3. Chép từ điển StarDict vào `metalio/e-ink/dictionaries/<Tên>/<Tên>.ifo|.idx|.dict.dz|.syn`.
   Release có sẵn từ điển Anh–Việt–Anh: giải nén `AnhVietAnh-StarDict.zip` vào gốc thẻ SD.
4. Tháo ổ an toàn, tắt USB trên máy.

Dùng: mở sách, **đè giữ Home** (từ giữa trang bị bôi đen), **chạm vào từ** → nghĩa hiện toàn
màn hình. Phím bấm, hoặc chạm 1/3 bên trái / phần còn lại để lật trang nghĩa; chạm dòng tên từ
để quay lại trang sách; **nhấn nhanh Home** để thoát. Lần tra đầu tiên mất khoảng 1 giây để
tạo chỉ mục, sau đó tra tức thì.

### Màn hình chờ là trang đang đọc

Trong `sdcard/metalio/e-ink/wallpaper/` của gói zip có 3 ảnh trong suốt (chép vào thẻ SD như font):
`standby-reading-page-moon-center.png` (trăng to mờ ở giữa), `standby-reading-page-moon.png`
(trăng nhỏ góc trên phải), `standby-reading-page.png` (chỉ trang sách).

Mở **Wallpaper**, chọn ảnh, chạm **Set as standby** → **Transparent wallpaper**. Màn hình chờ sẽ
giữ trang đang đọc bên dưới. Chọn **Clear selection** để quay về màn hình chờ cũ.

Mở lại sách khi bật máy không cần cài gì: tắt máy lúc đang mở sách (kể cả từ màn hình chờ), lần
bật sau máy về thẳng trang đó (Home hiện thoáng khoảng 1 giây). Tắt ở chỗ khác thì vẫn vào Home.

### Lưu ý

- Wi-Fi chỉ **2,4 GHz**. Kết nối một lần để đồng hồ lấy giờ (NTP); không có chỉnh giờ tay.
- Bản này không cập nhật qua mạng (OTA); cài bản mới bằng `flash.sh`.
- Tự chịu rủi ro khi sử dụng. Mã nguồn: nhánh `english-default` của
  https://github.com/binhphuong-ab/Metalio-E-INK4-Plus
