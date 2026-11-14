/**
 * @file book_dict.cc
 * @brief Reader dictionary mode: highlight a word on the page, show its definition in a card.
 */
#include "book_screen/reader/book_dict.h"

#include "assets/lang_config.h"
#include "book_screen/book_font_runtime.h"
#include "book_screen/book_nav.h"
#include "book_screen/book_screen_priv.h"
#include "book_screen/reader/book_reader_overlay.h"
#include "book_screen/reader/book_toc_footer.h"
#include "display/font/epdfont.h"
#include "epd_content_policy.h"
#include "reader/dict_service.h"
#include "reader/page_text_raster.h"

#include <lvgl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <climits>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>

namespace {

constexpr const char* kTag = "BookDict";

// ------------------------------------------------------------------ fonts

struct DictFonts {
    bool tried = false;
    epdfont_t* regular = nullptr;
    epdfont_t* bold = nullptr;
    epdfont_t* italic = nullptr;
} s_fonts;

void LoadFonts() {
    if (s_fonts.tried) return;
    s_fonts.tried = true;
    const std::string dir = reader::dict::FontDir();
    s_fonts.regular = epdfont_open((dir + "/dict_regular.ef").c_str());
    s_fonts.bold = epdfont_open((dir + "/dict_bold.ef").c_str());
    s_fonts.italic = epdfont_open((dir + "/dict_italic.ef").c_str());
    ESP_LOGI(kTag, "fonts regular=%d bold=%d italic=%d", s_fonts.regular != nullptr, s_fonts.bold != nullptr,
             s_fonts.italic != nullptr);
}

const lv_font_t* FontFor(uint8_t flags) {
    const lv_font_t* fallback = BookFont() ? BookFont() : Book_ItemFont();
    const lv_font_t* regular = s_fonts.regular ? epdfont_get_lv_font(s_fonts.regular) : fallback;
    if ((flags & 1) && s_fonts.bold) return epdfont_get_lv_font(s_fonts.bold);
    if ((flags & 2) && s_fonts.italic) return epdfont_get_lv_font(s_fonts.italic);
    return regular;
}

// ------------------------------------------------------------------ HTML -> paragraphs

struct Span {
    std::string text;
    uint8_t flags = 0;  // 1 bold, 2 italic
};
struct Para {
    std::vector<Span> spans;
    bool indent = false;
};

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x110000) {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class ParaBuilder {
public:
    std::vector<Para> paras;
    int bold = 0, italic = 0;

    void Text(const std::string& s) {
        for (char c : s) Char(c);
    }
    void Char(char c) {
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (space) {
            if (cur_.spans.empty() && pending_.empty() && text_len_ == 0) { ++lead_spaces_; return; }
            space_ = true;
            return;
        }
        if (space_ && text_len_ + pending_.size() > 0) pending_.push_back(' ');
        space_ = false;
        const uint8_t flags = (bold > 0 ? 1 : 0) | (italic > 0 ? 2 : 0);
        if (flags != flags_ && !pending_.empty()) FlushSpan();
        flags_ = flags;
        pending_.push_back(c);
    }
    void Style(int dbold, int ditalic) {
        FlushSpan();
        bold = std::max(0, bold + dbold);
        italic = std::max(0, italic + ditalic);
    }
    void Break() {
        FlushSpan();
        if (!cur_.spans.empty()) {
            const std::string& first = cur_.spans.front().text;
            cur_.indent = lead_spaces_ >= 2 || first.rfind("\xE2\x80\x93", 0) == 0 || first.rfind("-", 0) == 0;
            paras.push_back(std::move(cur_));
        }
        cur_ = Para{};
        lead_spaces_ = 0;
        text_len_ = 0;
        space_ = false;
    }

private:
    void FlushSpan() {
        if (pending_.empty()) return;
        // A leading space carried into a new span stays (it separates words across styles).
        if (!cur_.spans.empty() && cur_.spans.back().flags == flags_) cur_.spans.back().text += pending_;
        else cur_.spans.push_back(Span{pending_, flags_});
        text_len_ += pending_.size();
        pending_.clear();
    }
    Para cur_;
    std::string pending_;
    uint8_t flags_ = 0;
    size_t text_len_ = 0;
    int lead_spaces_ = 0;
    bool space_ = false;
};

std::vector<Para> ParseDefinition(const std::string& src, bool html) {
    ParaBuilder b;
    if (!html) {
        for (char c : src) {
            if (c == '\n') b.Break();
            else b.Char(c);
        }
        b.Break();
        return std::move(b.paras);
    }
    for (size_t i = 0; i < src.size();) {
        const char c = src[i];
        if (c == '<') {
            const size_t end = src.find('>', i);
            if (end == std::string::npos) break;
            std::string tag = src.substr(i + 1, end - i - 1);
            i = end + 1;
            const bool closing = !tag.empty() && tag[0] == '/';
            if (closing) tag.erase(0, 1);
            size_t n = 0;
            while (n < tag.size() && std::isalnum(static_cast<unsigned char>(tag[n]))) ++n;
            std::string name = tag.substr(0, n);
            for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            const int d = closing ? -1 : 1;
            if (name == "b" || name == "strong" || name == "h1" || name == "h2" || name == "h3" || name == "h4") {
                if (name[0] == 'h') b.Break();
                b.Style(d, 0);
                if (name[0] == 'h' && closing) b.Break();
            } else if (name == "i" || name == "em" || name == "cite") {
                b.Style(0, d);
            } else if (name == "p" || name == "br" || name == "div" || name == "li" || name == "tr" ||
                       name == "blockquote" || name == "ul" || name == "ol") {
                b.Break();
            }
            continue;
        }
        if (c == '&') {
            const size_t semi = src.find(';', i);
            if (semi != std::string::npos && semi - i <= 10) {
                const std::string ent = src.substr(i + 1, semi - i - 1);
                std::string out;
                if (ent == "amp") out = "&";
                else if (ent == "lt") out = "<";
                else if (ent == "gt") out = ">";
                else if (ent == "quot") out = "\"";
                else if (ent == "apos") out = "'";
                else if (ent == "nbsp") out = " ";
                else if (ent.size() > 1 && ent[0] == '#') {
                    const uint32_t cp = (ent[1] == 'x' || ent[1] == 'X')
                        ? static_cast<uint32_t>(std::strtoul(ent.c_str() + 2, nullptr, 16))
                        : static_cast<uint32_t>(std::strtoul(ent.c_str() + 1, nullptr, 10));
                    AppendUtf8(out, cp);
                }
                if (!out.empty()) {
                    b.Text(out);
                    i = semi + 1;
                    continue;
                }
            }
        }
        b.Char(c);
        ++i;
    }
    b.Break();
    return std::move(b.paras);
}

// ------------------------------------------------------------------ state

struct DictUi {
    std::atomic<bool> active{false};
    lv_obj_t* layer = nullptr;   // full-screen touch catcher on the reader screen
    lv_obj_t* mark = nullptr;    // black box behind the selected word
    lv_obj_t* mark_text = nullptr;
    lv_obj_t* card = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* head = nullptr;    // headword + page counter row
    lv_obj_t* body = nullptr;    // clipping viewport
    lv_obj_t* col = nullptr;     // paragraphs, moved up to page
    lv_obj_t* page_lbl = nullptr;
    std::vector<reader::PageWordBox> words;
    int sel = -1;
    lv_point_t origin{};         // raster top-left on screen
    int letter_gap = 0;
    uint32_t gen = 0;
    int page = 0, pages = 1, step = 1, overflow = 0;
    int screen_w = 0, screen_h = 0;  // from the reader screen: a new object's size is 0 until layout runs
} s;

struct LookupPayload {
    uint32_t gen = 0;
    reader::dict::Lookup result;
};

// ------------------------------------------------------------------ background lookups

std::mutex s_queue_mutex;
std::string s_queue_word;
uint32_t s_queue_gen = 0;
bool s_queue_running = false;

void OnLookupResult(void* data);

void LookupTask(void*) {
    for (;;) {
        std::string word;
        uint32_t gen = 0;
        {
            std::lock_guard<std::mutex> lock(s_queue_mutex);
            if (s_queue_word.empty()) {
                s_queue_running = false;
                break;
            }
            word.swap(s_queue_word);
            gen = s_queue_gen;
        }
        auto* payload = new (std::nothrow) LookupPayload;
        if (!payload) continue;
        payload->gen = gen;
        try {
            payload->result = reader::dict::Find(word);
        } catch (const std::bad_alloc&) {
            payload->result = reader::dict::Lookup{};
            payload->result.status = reader::dict::Lookup::Status::kError;
        }
        if (!BookLvAsync(OnLookupResult, payload)) delete payload;
    }
    vTaskDeleteWithCaps(nullptr);
}

bool RequestLookup(uint32_t gen, const std::string& word) {
    std::lock_guard<std::mutex> lock(s_queue_mutex);
    s_queue_word = word;
    s_queue_gen = gen;
    if (s_queue_running) return true;
    s_queue_running = true;
    // Stack in PSRAM like the reader's page worker: internal RAM is scarce while reading.
    if (xTaskCreateWithCaps(LookupTask, "dict_lookup", 16 * 1024, nullptr, 2, nullptr,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_queue_running = false;
        s_queue_word.clear();
        ESP_LOGE(kTag, "lookup task create failed");
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ card

lv_obj_t* PlainObj(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

void UpdatePage() {
    if (!s.col) return;
    const int y = std::min(s.page * s.step, s.overflow);
    lv_obj_set_y(s.col, -y);
    if (s.page_lbl) {
        if (s.pages > 1) {
            lv_label_set_text_fmt(s.page_lbl, Lang::Strings::DICT_PAGE_FMT, s.page + 1, s.pages);
        } else {
            lv_label_set_text(s.page_lbl, "");
        }
    }
}

void ShowMessage(const char* text) {
    if (!s.col) return;
    lv_obj_clean(s.col);
    lv_obj_t* l = Label(s.col, Book_ItemFont());
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, text);
    s.page = 0;
    s.pages = 1;
    s.overflow = 0;
    UpdatePage();
}

void ShowDefinition(const reader::dict::Lookup& r) {
    if (!s.col) return;
    lv_obj_clean(s.col);
    LoadFonts();
    const auto paras = ParseDefinition(r.body, r.html);
    std::string all;
    for (const auto& p : paras) {
        lv_obj_t* g = lv_spangroup_create(s.col);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_width(g, LV_PCT(100));
        lv_obj_set_height(g, LV_SIZE_CONTENT);
        lv_spangroup_set_mode(g, LV_SPAN_MODE_BREAK);
        lv_obj_set_style_text_color(g, lv_color_black(), 0);
        lv_obj_set_style_text_font(g, FontFor(0), 0);
        if (p.indent) lv_obj_set_style_pad_left(g, UiSx(22), 0);
        for (const auto& sp : p.spans) {
            lv_span_t* span = lv_spangroup_add_span(g);
            lv_spangroup_set_span_text(g, span, sp.text.c_str());
            lv_style_set_text_font(lv_span_get_style(span), FontFor(sp.flags));
            lv_style_set_text_color(lv_span_get_style(span), lv_color_black());
            all += sp.text;
        }
        lv_spangroup_refresh(g);
    }
    for (auto* f : {s_fonts.regular, s_fonts.bold, s_fonts.italic})
        if (f) epdfont_prewarm_utf8(f, all.data(), all.size());
    lv_obj_update_layout(s.card);
    const int view_h = lv_obj_get_height(s.body);
    const int total = lv_obj_get_height(s.col);
    const lv_font_t* font = FontFor(0);
    s.step = std::max<int>(40, view_h - static_cast<int>(font->line_height) - static_cast<int>(UiSy(4)));
    s.overflow = std::max(0, total - view_h);
    s.pages = s.overflow == 0 ? 1 : 1 + (s.overflow + s.step - 1) / s.step;
    s.page = 0;
    UpdatePage();
}

bool CardShown() { return s.card && !lv_obj_has_flag(s.card, LV_OBJ_FLAG_HIDDEN); }

// The definition covers the whole screen (like a reader page); hiding it goes
// back to the book page to pick another word.
void ShowCard(bool show) {
    if (!s.card) return;
    if (show) {
        lv_obj_remove_flag(s.card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s.card);
    } else {
        lv_obj_add_flag(s.card, LV_OBJ_FLAG_HIDDEN);
        ++s.gen;  // drop a lookup still in flight
    }
}

void BuildCard(lv_obj_t* parent) {
    s.card = lv_obj_create(parent);
    lv_obj_remove_style_all(s.card);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s.card, s.screen_w, s.screen_h);
    lv_obj_set_pos(s.card, 0, 0);
    lv_obj_set_style_bg_color(s.card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s.card, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s.card, UiSx(26), 0);
    lv_obj_set_style_pad_top(s.card, UiSy(16), 0);
    lv_obj_set_style_pad_bottom(s.card, UiSy(10), 0);
    lv_obj_set_style_pad_row(s.card, UiSy(8), 0);
    lv_obj_set_flex_flow(s.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s.card, LV_OBJ_FLAG_HIDDEN);

    s.head = PlainObj(s.card);
    lv_obj_set_size(s.head, LV_PCT(100), LV_SIZE_CONTENT);
    s.title = Label(s.head, Book_ListFont());
    lv_label_set_long_mode(s.title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s.title, LV_PCT(78));
    lv_obj_align(s.title, LV_ALIGN_LEFT_MID, 0, 0);
    s.page_lbl = Label(s.head, Book_ItemFont());
    lv_label_set_text(s.page_lbl, "");
    lv_obj_align(s.page_lbl, LV_ALIGN_RIGHT_MID, 0, 0);

    lv_obj_t* rule = PlainObj(s.card);
    lv_obj_set_size(rule, LV_PCT(100), UiSy(2));
    lv_obj_set_style_bg_color(rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);

    s.body = PlainObj(s.card);
    lv_obj_set_width(s.body, LV_PCT(100));
    lv_obj_set_flex_grow(s.body, 1);
    s.col = PlainObj(s.body);
    lv_obj_set_size(s.col, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s.col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s.col, UiSy(6), 0);
    lv_obj_set_pos(s.col, 0, 0);
}

// ------------------------------------------------------------------ selection

void Select(int index, bool lookup) {
    if (index < 0 || index >= static_cast<int>(s.words.size()) || !s.layer) return;
    s.sel = index;
    const auto& w = s.words[static_cast<size_t>(index)];
    const lv_font_t* font = BookFont();
    const int pad = UiSx(3);
    const int line_h = font ? static_cast<int>(font->line_height) : (w.cell_bottom - w.cell_top);
    if (!s.mark) {
        s.mark = PlainObj(s.layer);
        lv_obj_set_style_bg_color(s.mark, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s.mark, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s.mark, UiSx(3), 0);
        EpdSetContentRole(s.mark, EpdContentRole::Monochrome);
        s.mark_text = Label(s.mark, font ? font : Book_ItemFont());
        lv_obj_set_style_text_color(s.mark_text, lv_color_white(), 0);
        lv_obj_set_style_text_letter_space(s.mark_text, s.letter_gap, 0);
    }
    lv_obj_set_pos(s.mark, s.origin.x + w.x0 - pad, s.origin.y + w.line_top);
    lv_obj_set_size(s.mark, w.x1 - w.x0 + pad * 2, line_h);
    lv_label_set_text(s.mark_text, w.text.c_str());
    lv_obj_set_pos(s.mark_text, pad, 0);

    if (!lookup) return;
    ShowCard(true);
    lv_label_set_text(s.title, w.text.c_str());
    ShowMessage(reader::dict::NeedsPreparation() ? Lang::Strings::DICT_PREPARING : Lang::Strings::DICT_LOOKING_UP);
    if (!RequestLookup(++s.gen, w.text)) ShowMessage(Lang::Strings::DICT_ERROR);
}

void OnLookupResult(void* data) {
    std::unique_ptr<LookupPayload> payload(static_cast<LookupPayload*>(data));
    if (!s.active.load() || payload->gen != s.gen || !CardShown()) return;
    const auto& r = payload->result;
    using Status = reader::dict::Lookup::Status;
    switch (r.status) {
        case Status::kFound:
            lv_label_set_text(s.title, r.headword.c_str());
            ShowDefinition(r);
            break;
        case Status::kNotFound: ShowMessage(Lang::Strings::DICT_NOT_FOUND); break;
        case Status::kNoDictionary: ShowMessage(Lang::Strings::DICT_NO_DICTIONARY); break;
        case Status::kBusy: ShowMessage(Lang::Strings::DICT_SD_BUSY); break;
        case Status::kError: ShowMessage(Lang::Strings::DICT_ERROR); break;
    }
}

int WordAt(int sx, int sy) {
    const int x = sx - s.origin.x, y = sy - s.origin.y;
    const int slack = UiSx(14);
    int best = -1, best_d = INT_MAX;
    for (size_t i = 0; i < s.words.size(); ++i) {
        const auto& w = s.words[i];
        if (y < w.cell_top || y >= w.cell_bottom) continue;
        const int d = x < w.x0 ? w.x0 - x : (x >= w.x1 ? x - w.x1 + 1 : 0);
        if (d <= slack && d < best_d) { best = static_cast<int>(i); best_d = d; }
    }
    return best;
}

void OnLayerClicked(lv_event_t* e) {
    if (!s.active.load()) return;
    lv_point_t pt{};
    if (!ReadEventPoint(e, &pt)) return;
    if (CardShown()) {
        // Header (headword row) = back to the page; left third = previous page; rest = next.
        lv_area_t h;
        lv_obj_get_coords(s.head, &h);
        if (pt.y <= h.y2 + UiSy(12)) {
            ShowCard(false);
            return;
        }
        if (s.pages > 1) {
            const bool back = pt.x < s.screen_w / 3;
            s.page = std::clamp(s.page + (back ? -1 : 1), 0, s.pages - 1);
            UpdatePage();
        }
        return;
    }
    const int index = WordAt(pt.x, pt.y);
    if (index >= 0) Select(index, true);
}

void ResetState() {
    s.active.store(false);
    s.layer = s.mark = s.mark_text = s.card = s.head = s.title = s.body = s.col = s.page_lbl = nullptr;
    s.words.clear();
    s.sel = -1;
    ++s.gen;
}

// Page keys turn the definition's pages; words are picked by touch.
void StepPageAsync(void* user_data) {
    if (!s.active.load() || !CardShown() || s.pages <= 1) return;
    const int delta = static_cast<int>(reinterpret_cast<intptr_t>(user_data));
    const int next = std::clamp(s.page + delta, 0, s.pages - 1);
    if (next == s.page) return;
    s.page = next;
    UpdatePage();
}

// Font files stay open while in use; release them so the SD card can go to USB mode.
void CloseFonts() {
    for (auto** f : {&s_fonts.regular, &s_fonts.bold, &s_fonts.italic}) {
        if (*f) epdfont_close(*f);
        *f = nullptr;
    }
    s_fonts.tried = false;
}

void CloseAsync(void*) { BookDict_Close(); }

}  // namespace

bool BookDict_Active() { return s.active.load(); }

void BookDict_Close() {
    if (!s.layer) {
        s.active.store(false);
        return;
    }
    lv_obj_t* layer = s.layer;
    ResetState();
    if (lv_obj_is_valid(layer)) lv_obj_delete(layer);
    CloseFonts();
}

void BookDict_EnterAsync(void*) {
    auto& st = Book_State();
    if (s.active.load()) return;
    if (!st.reader.read_scr || !lv_obj_is_valid(st.reader.read_scr) || !st.reader.session ||
        !st.reader.session->IsOpen() || st.reader.opening.load() ||
        st.reader.read_chrome != BookUiState::ReadChrome::kReading)
        return;

    s.words.clear();
    lv_obj_t* image = st.reader.text_raster_image;
    const reader::Page* page = st.reader.session->CurrentPageData();
    if (image && lv_obj_is_valid(image) && page && BookFont() && !st.reader.session->VerticalWriting() &&
        !st.reader.session->IsPageImagesMode()) {
        lv_area_t a;
        lv_obj_get_coords(image, &a);
        s.origin = {a.x1, a.y1};
        const auto style = st.reader.session->Style();
        s.letter_gap = style.letter_gap;
        reader::CollectPageWords(*page, BookFont(), st.reader.session->LineSpacingPercent(),
                                 st.reader.session->ParaGap(), lv_obj_get_width(image), st.reader.viewport_h,
                                 style, s.words);
    }

    lv_obj_update_layout(st.reader.read_scr);
    s.screen_w = lv_obj_get_width(st.reader.read_scr);
    s.screen_h = lv_obj_get_height(st.reader.read_scr);
    if (s.screen_w <= 0 || s.screen_h <= 0) {
        s.screen_w = lv_display_get_horizontal_resolution(nullptr);
        s.screen_h = lv_display_get_vertical_resolution(nullptr);
    }
    ESP_LOGI(kTag, "enter: screen %dx%d, %u words", s.screen_w, s.screen_h, static_cast<unsigned>(s.words.size()));
    s.layer = lv_obj_create(st.reader.read_scr);
    lv_obj_remove_style_all(s.layer);
    lv_obj_set_size(s.layer, s.screen_w, s.screen_h);
    lv_obj_set_pos(s.layer, 0, 0);
    lv_obj_add_flag(s.layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s.layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s.layer, OnLayerClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s.layer, [](lv_event_t* e) {
        if (lv_event_get_target(e) != s.layer) return;
        ResetState();
        CloseFonts();
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_move_foreground(s.layer);
    BuildCard(s.layer);
    lv_obj_update_layout(s.layer);
    s.active.store(true);
    ResetPointerLongPress();

    if (s.words.empty()) {
        ShowCard(true);
        lv_label_set_text(s.title, "");
        ShowMessage(Lang::Strings::DICT_NO_WORDS);
        return;
    }
    // Start on the word nearest the middle of the page.
    const int cx = lv_obj_get_width(image) / 2;
    const int cy = lv_obj_get_height(image) / 2;
    int best = 0;
    long best_d = LONG_MAX;
    for (size_t i = 0; i < s.words.size(); ++i) {
        const auto& w = s.words[i];
        const long dx = (w.x0 + w.x1) / 2 - cx, dy = ((w.cell_top + w.cell_bottom) / 2 - cy) * 3;
        const long d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = static_cast<int>(i); }
    }
    Select(best, false);  // highlight only; a tap opens the definition
}

bool BookDict_OnKey(const char* key_name) {
    if (!s.active.load() || key_name == nullptr) return false;
    if (std::strcmp(key_name, "vk_home") == 0) {
        BookLvAsync(CloseAsync);
    } else if (std::strcmp(key_name, "vk_prev") == 0) {
        BookLvAsync(StepPageAsync, reinterpret_cast<void*>(static_cast<intptr_t>(-1)));
    } else if (std::strcmp(key_name, "vk_next") == 0) {
        BookLvAsync(StepPageAsync, reinterpret_cast<void*>(static_cast<intptr_t>(1)));
    }
    return true;
}
