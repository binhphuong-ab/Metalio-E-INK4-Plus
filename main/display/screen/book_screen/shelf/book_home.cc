#include "assets/lang_reader.h"
#pragma GCC optimize("O1")

#include "book_screen/shelf/book_home.h"
#include "book_screen/book_screen_priv.h"
#include "book_screen/cover/book_cover_cell.h"
#include "book_screen/cover/book_cover_style.h"
#include "book_screen/cover/book_cover_workers.h"
#include "book_screen/book_font_runtime.h"
#include "book_screen/book_text_util.h"
#include "book_screen/book_nav.h"
#include "book_screen/reader/book_reader_prefs.h"
#include "book_screen/shelf/book_bookshelf.h"

#include <lvgl.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "assets/lang_config.h"
#include "SdCardManager.hpp"
#include "fontpack_lvgl.h"
#include "haptic_feedback.h"
#include "reader/reader.h"
#include "reader/book_library.h"
#include "reader/book_home_snapshot.h"

// ---------------------------------------------------------------------------
// Books home, redesigned for a 1-bit e-paper panel:
//
//   ┌──────────────────────────────────────┐
//   │ ┌──────┐  CONTINUE READING           │   hero card: big cover + details
//   │ │cover │  Title (up to 3 lines)      │
//   │ │      │  Author                     │
//   │ │      │  ██████░░░░░░░░░  42%       │
//   │ └──────┘  8m read   [ Continue › ]   │
//   └──────────────────────────────────────┘
//    8.4m     │   12m         │   0             stats strip (no empty bar chart)
//    THIS WEEK│ TOTAL READING │ FINISHED
//   UP NEXT
//   [cover] [cover] [cover]                     other books; never empty placeholders
//   [        My bookshelf  ›        ]
//
// No gray fills: everything is black/white so it stays crisp on 1-bit refresh.
// ---------------------------------------------------------------------------

namespace {

constexpr lv_coord_t kCardPad = 18;
constexpr lv_coord_t kCardRadius = 16;
constexpr lv_coord_t kHeroCoverW = 176;
constexpr lv_coord_t kHeroCoverH = 254;
constexpr lv_coord_t kHeroGap = 22;
constexpr lv_coord_t kSectionGap = 26;
constexpr lv_coord_t kShelfBtnH = 72;
constexpr lv_coord_t kContinueBtnH = 58;
constexpr int kUpNextCount = 3;

lv_obj_t* Box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    Book_DisableScroll(o);
    return o;
}

lv_obj_t* Text(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color,
               lv_coord_t letter_space = 0) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t* Rule(lv_obj_t* parent, lv_coord_t w, lv_coord_t h) {
    lv_obj_t* r = Box(parent);
    lv_obj_set_size(r, w, h);
    lv_obj_set_style_bg_color(r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

lv_obj_t* ProgressBar(lv_obj_t* parent, int x10, lv_coord_t h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_height(bar, h);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, std::clamp(x10, 0, 1000), LV_ANIM_OFF);
    return bar;
}

void AttachContinue(lv_obj_t* obj, int index) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    HapticAttachClick(obj);
    lv_obj_add_event_cb(obj, OnContinueReadingClicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(index)));
}

// Recent books first (MRU order), then the rest of the library; skip the hero book.
std::vector<int> PickUpNext(const HomeStats& stats, size_t book_count) {
    std::vector<int> out;
    auto push = [&](int idx) {
        if (idx < 0 || idx >= static_cast<int>(book_count) || idx == stats.continue_index) return;
        if (std::find(out.begin(), out.end(), idx) != out.end()) return;
        if (static_cast<int>(out.size()) < kUpNextCount) out.push_back(idx);
    };
    for (int idx : stats.recent_indices) push(idx);
    for (size_t i = 0; i < book_count && static_cast<int>(out.size()) < kUpNextCount; ++i) {
        push(static_cast<int>(i));
    }
    return out;
}

} // namespace

void RenderReadingHome() {
    auto& st = Book_State();
    if (st.shelf.list_body == nullptr) {
        return;
    }
    CancelListCoverFill();
    lv_obj_set_style_layout(st.shelf.list_body, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(st.shelf.list_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(st.shelf.list_body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(st.shelf.list_body, 0, 0);

    // Remove widgets before releasing covers so no lv_image keeps a dangling source.
    lv_obj_clean(st.shelf.list_body);
    st.shelf.recent_covers.clear();
    Book_DisableScroll(st.shelf.list_body);

    if (st.shelf.list_footer != nullptr) {
        lv_label_set_text(st.shelf.list_footer, "");
        lv_obj_add_flag(st.shelf.list_footer, LV_OBJ_FLAG_HIDDEN);
    }

    if (st.shelf.books.empty()) {
        char tip[160];
        std::snprintf(tip, sizeof(tip), Lang::Strings::BOOK_EMPTY_HOME_FMT,
                      SdUserPath(reader::kDefaultBooksDir));
        Book_ShowMessage(st.shelf.list_body, tip, Book_ListFont());
        return;
    }

    const HomeStats stats = CollectHomeStats(st.shelf.books);
    const lv_coord_t content_w = Book_ContentWidth();
    const lv_color_t black = lv_color_black();
    const lv_color_t white = lv_color_white();

    const lv_font_t* list_f = Book_ListFont();
    const lv_font_t* title_f = fontpack_lv_font_get(30, 4);
    if (title_f == nullptr) title_f = list_f;
    const lv_font_t* small_f = fontpack_lv_font_get(25, 2);
    if (small_f == nullptr) small_f = list_f;
    const lv_coord_t title_lh = lv_font_get_line_height(title_f);
    const lv_coord_t small_lh = lv_font_get_line_height(small_f);

    // ---- Hero: continue reading -----------------------------------------
    lv_obj_t* hero = Box(st.shelf.list_body);
    lv_obj_set_size(hero, content_w, kHeroCoverH + 2 * kCardPad);
    lv_obj_set_style_bg_color(hero, white, 0);
    lv_obj_set_style_bg_opa(hero, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hero, 2, 0);
    lv_obj_set_style_border_color(hero, black, 0);
    lv_obj_set_style_radius(hero, kCardRadius, 0);
    lv_obj_set_style_pad_all(hero, kCardPad, 0);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(hero, kHeroGap, 0);

    const lv_coord_t info_w = content_w - 2 * kCardPad - 4 - kHeroCoverW - kHeroGap;
    const int cont = stats.continue_index;
    const bool has_cont = cont >= 0 && cont < static_cast<int>(st.shelf.books.size());

    if (has_cont) {
        const reader::BookInfo& book = st.shelf.books[static_cast<size_t>(cont)];
        CreateBookCoverCell(hero, book, cont, kHeroCoverW, kHeroCoverH, st.shelf.recent_covers,
                            false, 0, false);

        lv_obj_t* info = Box(hero);
        lv_obj_set_size(info, info_w, kHeroCoverH);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(info, 8, 0);

        Text(info, Lang::Strings::STANDBY_CONTINUE_READING, small_f, black, 3);

        lv_obj_t* title = Text(info, book.title.c_str(), title_f, black);
        lv_obj_set_width(title, info_w);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_height(title, 3 * title_lh, 0);

        if (!book.author.empty()) {
            lv_obj_t* author = Text(info, book.author.c_str(), small_f, black);
            lv_obj_set_width(author, info_w);
            lv_label_set_long_mode(author, LV_LABEL_LONG_DOT);
            lv_obj_set_height(author, small_lh);
        }

        lv_obj_t* spacer = Box(info);
        lv_obj_set_width(spacer, info_w);
        lv_obj_set_flex_grow(spacer, 1);

        // progress bar + percent
        lv_obj_t* prog = Box(info);
        lv_obj_set_size(prog, info_w, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(prog, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(prog, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(prog, 14, 0);
        const int x10 = std::max(0, stats.continue_progress_x10);
        lv_obj_t* bar = ProgressBar(prog, x10, 10);
        lv_obj_set_flex_grow(bar, 1);
        char pct[12];
        std::snprintf(pct, sizeof(pct), "%d%%", x10 / 10);
        Text(prog, pct, small_f, black);

        // time read + continue button
        lv_obj_t* actions = Box(info);
        lv_obj_set_size(actions, info_w, kContinueBtnH);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        char dur[24], read_txt[40];
        FormatHomeDurationShort(dur, sizeof(dur), stats.continue_seconds);
        std::snprintf(read_txt, sizeof(read_txt), Lang::Strings::BOOK_TIME_READ_FMT, dur);
        Text(actions, read_txt, small_f, black);

        lv_obj_t* btn = Box(actions);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, kContinueBtnH);
        lv_obj_set_style_pad_hor(btn, 24, 0);
        lv_obj_set_style_bg_color(btn, black, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, kContinueBtnH / 2, 0);
        AttachContinue(btn, cont);
        char btn_txt[48];
        std::snprintf(btn_txt, sizeof(btn_txt), "%s  >", Lang::Strings::BOOK_CONTINUE);
        lv_obj_t* btn_lbl = Text(btn, btn_txt, list_f, white);
        lv_obj_center(btn_lbl);
    } else {
        lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* msg = Text(hero, Lang::Strings::BOOK_NO_CONTINUE, list_f, black);
        lv_obj_set_width(msg, content_w - 2 * kCardPad - 4);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    }

    // ---- Stats strip -------------------------------------------------------
    const auto week = reader::book_home_snapshot::LoadWeekStats();
    const uint32_t week_seconds = week.valid ? static_cast<uint32_t>(week.total_seconds) : week.undated_seconds;
    char weekly[24], total[24], finished[24];
    FormatHomeDurationShort(weekly, sizeof(weekly), week_seconds);
    FormatHomeDurationShort(total, sizeof(total), stats.total_seconds);
    std::snprintf(finished, sizeof(finished), Lang::Strings::BOOK_COUNT_FMT, stats.finished_count);

    lv_obj_t* stats_box = Box(st.shelf.list_body);
    lv_obj_set_size(stats_box, content_w, LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(stats_box, kSectionGap, 0);
    lv_obj_set_flex_flow(stats_box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_box, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const lv_coord_t col_w = (content_w - 4) / 3;
    const struct { const char* value; const char* label; } cols[] = {
        {weekly, Lang::Strings::BOOK_WEEK_DURATION},
        {total, Lang::Strings::BOOK_TOTAL_DURATION},
        {finished, Lang::Strings::BOOK_FINISHED},
    };
    for (int i = 0; i < 3; ++i) {
        if (i > 0) Rule(stats_box, 2, title_lh + small_lh + 6);
        lv_obj_t* col = Box(stats_box);
        lv_obj_set_size(col, col_w, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 6, 0);
        Text(col, cols[i].value, title_f, black);
        lv_obj_t* lab = Text(col, cols[i].label, small_f, black);
        lv_obj_set_width(lab, col_w - 8);
        lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_DOT);
    }

    // ---- Up next -----------------------------------------------------------
    const std::vector<int> up_next = PickUpNext(stats, st.shelf.books.size());
    if (!up_next.empty()) {
        lv_obj_t* hdr = Text(st.shelf.list_body, Lang::Strings::BOOK_UP_NEXT, small_f, black, 3);
        lv_obj_set_style_margin_top(hdr, kSectionGap + 4, 0);
        lv_obj_set_style_margin_bottom(hdr, 12, 0);

        lv_obj_update_layout(st.shelf.list_body);
        // Height already taken above the cover row (header bottom + its margin).
        const lv_coord_t used = lv_obj_get_y(hdr) + lv_obj_get_height(hdr) + 12;
        const lv_coord_t avail = lv_obj_get_content_height(st.shelf.list_body) - used -
            kRecentTitleGap - lv_font_get_line_height(list_f) - kSectionGap - kShelfBtnH - 8;
        const lv_coord_t cell_w = (content_w - 2 * kHomeGap) / kUpNextCount;
        const lv_coord_t cover_h = std::clamp<lv_coord_t>(avail, UiSy(90), cell_w * 3 / 2);

        lv_obj_t* row = Box(st.shelf.list_body);
        lv_obj_set_size(row, content_w, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, kHomeGap, 0);
        for (int idx : up_next) {
            CreateBookCoverCell(row, st.shelf.books[static_cast<size_t>(idx)], idx, cell_w, cover_h,
                                st.shelf.recent_covers, false, kRecentTitleGap, true);
        }
    }

    // ---- My bookshelf (pinned to the bottom) ------------------------------
    lv_obj_t* fill = Box(st.shelf.list_body);
    lv_obj_set_width(fill, content_w);
    lv_obj_set_flex_grow(fill, 1);

    lv_obj_t* shelf_btn = Box(st.shelf.list_body);
    lv_obj_set_size(shelf_btn, content_w, kShelfBtnH);
    lv_obj_set_style_margin_top(shelf_btn, kSectionGap, 0);
    lv_obj_set_style_bg_color(shelf_btn, white, 0);
    lv_obj_set_style_bg_opa(shelf_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(shelf_btn, 2, 0);
    lv_obj_set_style_border_color(shelf_btn, black, 0);
    lv_obj_set_style_radius(shelf_btn, kShelfBtnH / 2, 0);
    lv_obj_add_flag(shelf_btn, LV_OBJ_FLAG_CLICKABLE);
    HapticAttachClick(shelf_btn);
    lv_obj_add_event_cb(shelf_btn, OnOpenShelfClicked, LV_EVENT_CLICKED, nullptr);
    char shelf_txt[64];
    std::snprintf(shelf_txt, sizeof(shelf_txt), "%s  >", Lang::Strings::BOOK_SHELF_TITLE);
    lv_obj_t* shelf_lbl = Text(shelf_btn, shelf_txt, list_f, black);
    lv_obj_center(shelf_lbl);

    ScheduleListCoverFill();
}
