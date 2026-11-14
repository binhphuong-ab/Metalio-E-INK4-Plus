# Metalio E-INK4 Plus — Offline English firmware `2.0.79-en.1`

> This folder holds the installer scripts only. Download the ready-to-flash zip from
> [Releases](https://github.com/binhphuong-ab/Metalio-E-INK4-Plus/releases/tag/v2.0.79-en.1).
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

## Requirements

- Metalio E-INK4 Plus (ESP32-S31, 4.7") **running the stock Metalio firmware**.
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
esptool --port COM5 write-flash 0x80000 metalio-e-ink4-plus-2.0.79-en.1-app.bin 0xae4000 metalio-e-ink4-plus-2.0.79-en.1-fontpack.bin
```

## Reading fonts (optional)

Copy `sdcard/metalio` from the source branch to the root of the SD card (enable
**Transfer → USB** on the reader). It adds Literata and Noto Serif with Vietnamese
support (SIL OFL 1.1). Then: open a book → **Reading settings → Choose font**.

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

### Yêu cầu

- Metalio E-INK4 Plus (ESP32-S31, 4,7") **đang chạy firmware gốc của Metalio**.
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

### Lưu ý

- Wi-Fi chỉ **2,4 GHz**. Kết nối một lần để đồng hồ lấy giờ (NTP); không có chỉnh giờ tay.
- Bản này không cập nhật qua mạng (OTA); cài bản mới bằng `flash.sh`.
- Tự chịu rủi ro khi sử dụng. Mã nguồn: nhánh `english-default` của
  https://github.com/binhphuong-ab/Metalio-E-INK4-Plus
