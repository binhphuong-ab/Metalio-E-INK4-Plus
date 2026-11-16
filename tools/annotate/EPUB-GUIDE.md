# Hướng dẫn soạn / sửa EPUB cho Metalio E-INK4 Plus (firmware en.4)

> Đưa file này cho AI agent trước khi nhờ nó sửa ebook. Mọi điều dưới đây được đối chiếu
> với mã nguồn firmware (`main/reader/html_content.cc`, `epub_document.cc`, `image_stream.cc`,
> `page_text_raster.cc`) của bản `2.0.79-en.4`, không phải chuẩn EPUB chung.

## 1. Tóm tắt một dòng

Máy **không phải trình duyệt**: nó chỉ lấy **chữ, ngắt đoạn, tiêu đề và ảnh** từ EPUB rồi tự
dàn trang bằng font và cài đặt của nó. **Gần như toàn bộ CSS bị bỏ qua.** Muốn "trình bày" thì
dùng cấu trúc HTML (tiêu đề, đoạn, box `ap-note`, ảnh), không dùng CSS.

## 2. Định dạng sách máy đọc được

| Đuôi file | Ghi chú |
|---|---|
| `.epub` | EPUB 2 và EPUB 3. Đọc spine trong `content.opf`, mục lục từ `nav.xhtml` hoặc `toc.ncx`. |
| `.txt` | UTF-8 (có hỗ trợ GB18030). Máy tự nhận chương. |
| `.ebook` | Định dạng riêng của Metalio (chuyển bằng `tools/ebook/epdbook`). |

Chép sách vào `metalio/e-ink/books/` trên thẻ SD (qua USB hoặc Wi-Fi transfer).

## 3. Những gì máy HIỂU

| Thành phần | Máy hiển thị thế nào |
|---|---|
| `<p>`, `<div>`, `<br>`, `<li>`, `<tr>`, `<section>`, `<article>`, `<blockquote>` | Ngắt đoạn / xuống dòng. Mỗi đoạn có thụt dòng đầu, khoảng cách đoạn và căn đều theo **cài đặt của máy**. |
| `<h1>` … `<h6>` | Dòng tiêu đề: **in đậm, căn giữa**, có khoảng cách thêm phía sau. Đây là cách duy nhất để có chữ đậm/căn giữa. |
| `<img src>`, `<svg><image xlink:href>` | Ảnh, thu nhỏ vừa khổ màn hình, hiển thị thang xám. |
| Phần tử có `class` chứa **`ap-note`** (mới ở en.4) | Đoạn đó (hoặc mọi đoạn trong `<div class="ap-note">`) nằm **trong khung viền**, thụt vào hai bên, không thụt dòng đầu. Vắt sang trang sau thì để hở ở chỗ ngắt. |
| Thuộc tính `hidden`, `style="display:none"` (viết inline) | Ẩn hẳn. |
| `<nav epub:type="page-list">`, `landmarks` | Ẩn (không in hàng trăm số trang). |
| Thực thể `&amp; &lt; &gt; &quot; &apos; &nbsp;` và `&#…;` / `&#x…;` | Giải mã đúng. |
| Mục lục `nav.xhtml` / `toc.ncx`, kể cả link có `#fragment` | Dùng cho màn hình Mục lục và nhảy tới đúng vị trí. |
| Bìa: `<item properties="cover-image">` hoặc `<meta name="cover" content="ID">` | Hiện ở thư viện và trang chủ. |

## 4. Những gì máy BỎ QUA (đừng tốn công làm)

- **Toàn bộ CSS**: `<style>`, file `.css`, `class` (trừ `ap-note`), `style=""` (trừ `display:none`).
  Không có màu, cỡ chữ, căn lề, viền, nền, margin, float, cột.
- **Định dạng chữ trong dòng**: `<b> <strong> <i> <em> <u> <small> <sup> <sub> <span>`. Chữ vẫn hiện
  nhưng **không đậm, không nghiêng**. Nếu cần nhấn mạnh, dùng chữ: ngoặc, dấu gạch, VIẾT HOA (ít).
- **Font nhúng** (`@font-face`, `.ttf/.otf/.woff` trong EPUB). Máy dùng font trên thẻ SD
  (`metalio/e-ink/fonts/*.ef`), người đọc chọn trong *Reading settings → Choose font*.
- **Danh sách**: `<ul>/<ol>` không có dấu chấm đầu dòng hay số thứ tự. Muốn có thì **viết thẳng vào
  chữ**: `• Ý thứ nhất`, `1. Bước một`.
- **Bảng**: `<table>` bị dàn phẳng, các ô trong một hàng **dính liền nhau**. Tránh bảng: đổi thành
  danh sách dòng ("Tên: giá trị"), hoặc vẽ bảng thành **ảnh PNG**.
- **Link bấm được, chú thích popup**: `<a href>` không bấm được trong lúc đọc, `epub:type="noteref"`
  không hiện popup. Đặt chú thích **ngay trong dòng** bằng ngoặc, hoặc trong box `ap-note`.
- `<hr>`, audio, video, JavaScript, SVG vẽ bằng vector (chỉ `<image>` bên trong SVG được hiển thị).

## 5. Quy tắc cấu trúc BẮT BUỘC

1. **Mỗi file XHTML trong spine ≤ 100 KB** (sau khi giải nén). Firmware dàn nguyên một file thành
   một chương, và file quá lớn sẽ bị **cắt cụt** (đã gặp: file 536 KB chỉ hiện 3 trang). Sách xuất từ
   Google Docs hay Word thường gộp cả sách vào một file, nên phải **cắt theo `<h1>/<h2>`** và cập nhật
   `manifest`, `spine`, `nav.xhtml`/`toc.ncx`. Xem `tools/annotate/merge.py --split`.
2. XHTML phải **well-formed** (đóng thẻ đầy đủ), mã hoá UTF-8.
3. File zip: `mimetype` là mục **đầu tiên**, lưu **không nén** (stored).
4. **Ảnh**:
   - JPEG **baseline**: JPEG *progressive* có thể không hiển thị, nên luôn lưu baseline.
   - PNG 8 hoặc 16 bit, **không interlace**.
   - Màn hình là thang xám nên màu sẽ thành xám: dùng tương phản cao, chữ trong ảnh to.
   - Ảnh rộng hơn khổ màn hình sẽ bị thu nhỏ; nên làm rộng khoảng 600–700 px cho ảnh trong bài.
5. **Bìa**: dọc, tỉ lệ 9:16 (ví dụ 1125×2000), JPEG baseline, dưới 1 MB. Khai báo cả
   `properties="cover-image"` lẫn `<meta name="cover">` cho chắc.
6. Máy lưu vị trí đọc (`.pos`) và số trang từng chương (`.cpg`) **theo đường dẫn file**. Ghi đè một
   sách đã sửa vào **cùng tên file** thì máy có thể dùng lại số trang cũ nếu số chương không đổi.
   Khi nội dung thay đổi, nên đặt **tên file mới** (hoặc xoá sách cũ qua máy trước rồi mới chép).

## 6. "Bộ công cụ trình bày" khả dụng

| Muốn | Cách làm trên Metalio |
|---|---|
| Tiêu đề chương/mục, chữ đậm căn giữa | `<h1>`/`<h2>`/`<h3>` |
| Dấu phân cách giữa các phần | `<h3>* * *</h3>` (một tiêu đề ngắn sẽ được căn giữa) |
| Hộp giải thích, tóm tắt, mẹo, ví dụ | `<p class="ap-note">Explainer: …</p>` hoặc `<div class="ap-note"><p>…</p><p>…</p></div>` |
| Nghĩa từ vựng | Ngay sau từ, trong ngoặc: `outsized (much larger than usual)` |
| Danh sách | Viết dấu vào chữ: `<p>• …</p>`, `<p>1. …</p>` |
| Bảng, sơ đồ, biểu đồ, công thức | Vẽ thành ảnh PNG thang xám, đặt `<img>` |
| Trích dẫn | `<blockquote>` chỉ ngắt đoạn, không thụt lề; nếu muốn tách hẳn thì dùng box `ap-note` |

Ký tự đặc biệt (đã kiểm tra trong font Literata/Be Vietnam trên thẻ SD):
- **Có**: chữ Việt, Latin mở rộng, — – “ ” ‘ ’ … • · ° ± µ ¹ ² ³ ¼ ½ ¾ × ÷ § ¶ † ‡ € £ ¥.
- **Không có** (hiện ô trống): chữ Hy Lạp (α π Σ), ký hiệu toán (≤ ≠ √ ∑ ∫ ∞ −), mũi tên (→),
  chỉ số trên/dưới ngoài ¹²³ (⁻ ₙ), ★ ✓, emoji.

## 6b. Công thức toán (LaTeX / MathML)

Máy **không** hiển thị công thức:
- **LaTeX** viết thẳng trong chữ (`$\frac{a}{b}$`) hiện nguyên văn ký tự `\frac{a}{b}`.
- **MathML** (`<math>…</math>`) chỉ còn chữ số và chữ cái dính liền nhau (`a b 2` thay cho a/b²).
- **MathJax/KaTeX** cần JavaScript, mà máy không chạy.
- **SVG vector** cũng không được vẽ.

Cách làm cho sách có công thức:
1. **Công thức đứng riêng dòng**: render thành **PNG** (thang xám, nền trắng, không interlace, chữ đủ
   to, rộng ≤ ~650 px), rồi đặt `<p><img src="images/eq-012.png" alt="LaTeX gốc"/></p>`. Giữ LaTeX gốc
   trong `alt` để sau này còn sửa.
2. **Công thức trong dòng**: máy coi mỗi ảnh là một khối riêng, nên ảnh luôn **tách dòng**, không
   nằm giữa câu được. Công thức đơn giản thì viết bằng chữ có sẵn trong font (`x²`, `a × b`, `½`,
   `±`, `a/b`, `sqrt(x)`, `pi`). Công thức phức tạp thì đưa ra thành dòng riêng như cách 1.
3. Công cụ gợi ý: tách công thức bằng script, render bằng `latex` + `dvipng` hoặc matplotlib
   (mathtext), thay vào XHTML rồi chèn ảnh vào manifest. Sách có hàng trăm công thức thì ảnh nặng
   thêm vài MB, nhưng vẫn ổn.

## 7. Quy trình đã dùng cho 2 cuốn có chú giải (tái sử dụng được)

Script nằm trong repo ở `tools/annotate/`. Chạy từ một **thư mục làm việc** riêng nằm ngoài repo
(ví dụ `~/books-annotated/`), nơi chứa sách và các thư mục `src/`, `work/`, `out/`. Đặt biến
`A=<repo>/tools/annotate` cho gọn. Cài đặt một lần: xem `tools/annotate/README.md`.

1. Tải sách về (Wi-Fi transfer): `GET http://<ip>/download?path=/metalio/e-ink/books/<file>`.
2. `python3 $A/chunk.py <key> <unpacked_dir> <xhtml...>`: gắn mã `P0001…` cho từng đoạn, cắt thành
   các phần khoảng 3.000 từ (`work/<key>/chunks/cNN.txt`).
3. Mỗi subagent đọc **một phần mỗi lần** và chỉ viết JSON chú giải `work/<key>/ann/cNN.json`
   (`glosses`: từ, nghĩa, mã đoạn; `notes`: đoạn giải thích, chèn sau mã đoạn). **Agent không viết lại
   văn bản sách**, nhờ vậy không mất chữ và không lệch định dạng.
4. `python3 $A/validate.py <key> cNN`: agent tự kiểm, sửa đến khi `OK`.
5. `python3 $A/merge.py <key> out/<tên>.epub [--cover work/<key>/cover.jpg] [--split <file.xhtml>]`:
   chèn chú giải, thêm trang "About this edition", bìa, cắt file lớn. Bìa có thể vẽ
   bằng script, xem `cover_bench.py` làm mẫu.
6. `python3 $A/textcheck.py original/<sách>.epub out/<sách>.epub`: gỡ chú giải ra thì văn bản phải
   giống hệt bản gốc (chạy trên bản build chưa `--split`).
7. `python3 $A/upload.py <ip> /metalio/e-ink/books out/<tên>.epub [--replace]`: tải lên qua WebSocket.
   Xoá file: `POST http://<ip>/delete` với `path=…`. Lưu ý: xoá sách là **mất luôn vị trí đọc và
   thống kê** của sách đó.
8. Xem log máy để chắc chắn sách mở đủ:
   `GET /download?path=/metalio/e-ink/settings/system/logs/metalio_logs.txt`, tìm dòng
   `EpubDoc: opf ok spine=N` và `BookSession: progress restored chapter=x page=y/z`.

## 8. Mẫu lời nhắn cho AI agent

> Sửa EPUB `<file>` cho máy đọc Metalio E-INK4 Plus. Đọc `tools/annotate/EPUB-GUIDE.md` trong repo
> Metalio-E-INK4-Plus (nhánh `english-default`) trước.
> Ràng buộc: không dựa vào CSS hay in đậm/nghiêng; giữ mỗi XHTML trong spine ≤ 100 KB; ảnh JPEG
> baseline hoặc PNG không interlace; danh sách viết dấu vào chữ; không dùng bảng (đổi thành ảnh
> hoặc dòng chữ); phần giải thích bỏ vào `<p class="ap-note">`. Không viết lại văn bản gốc: chỉ
> thêm phần tử mới, và chứng minh văn bản gốc không đổi sau khi sửa.
