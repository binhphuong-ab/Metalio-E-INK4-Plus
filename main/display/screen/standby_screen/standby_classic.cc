#include "standby_screen/standby_classic_priv.h"

#include "standby_classic_priv.h"
#include "standby_classic_ui.h"
#include "standby_classic_weather_store.h"
#include "standby_classic_weather_fetch.h"
#include "standby_classic_todos.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include <esp_log.h>
#include <lvgl.h>

#include "assets/lang_config.h"
#include "assets/lang_reader.h"
#include "board.h"
#include "fontpack_lvgl.h"
#include "haptic_feedback.h"
#include "home_icon_source.h"
#include "home_screen/pixel/home_pixel.h"
#include "reader/book_home_snapshot.h"
#include "reader/book_progress_cache.h"

// ---------------------------------------------------------------------------
// Standby screen, redesigned: a typographic "reader's calendar".
//
//   FRIDAY                      weekday, letter-spaced caps
//   14                          240px serif day number (shared pixel clock font)
//   NOVEMBER 2026
//   ───────────────────────     rule
//   CONTINUE READING
//   Book title, up to two lines
//   ██████████░░░░░░░░  42%
//
//   [   menu   ]                home button (full-screen mode only)
//   ───────────────────────
//   Battery 82%     This week 2h 15m
//
// No bitmap text: every string comes from the translation table, so the screen
// follows the UI language. Weather and to-dos are gone (cloud-only features).
// ---------------------------------------------------------------------------

namespace {

constexpr lv_coord_t kPad = 48;          // side margin
constexpr lv_coord_t kTopY = 76;         // top of the date block
constexpr lv_coord_t kRuleH = 3;         // section rule thickness
constexpr lv_coord_t kBarH = 10;         // progress bar height
constexpr lv_coord_t kFooterPad = 36;    // footer distance from bottom
constexpr lv_coord_t kHomeBtnHeight = 64;

const char* const kWeekdayEn[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
                                  "THURSDAY", "FRIDAY", "SATURDAY"};
const char* const kMonthEn[] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY",
                                "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

bool IsZh() { return std::strcmp(Lang::CODE, "zh-CN") == 0; }

const lv_font_t* BodyFont() {
    const lv_font_t* f = fontpack_lv_font_get(30, 4);
    return f != nullptr ? f : fontpack_lv_font_ui();
}

const lv_font_t* SmallFont() {
    const lv_font_t* f = fontpack_lv_font_get(25, 2);
    return f != nullptr ? f : BodyFont();
}

lv_obj_t* Box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, const char* text, lv_coord_t letter_space = 0) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_style_text_letter_space(l, letter_space, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
    return l;
}

lv_obj_t* Rule(lv_obj_t* parent, lv_coord_t width, lv_coord_t height) {
    lv_obj_t* r = Box(parent);
    lv_obj_set_size(r, width, height);
    lv_obj_set_style_bg_color(r, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

// "/sdcard/metalio/e-ink/books/The_Great_Gatsby.epub" -> "The Great Gatsby"
std::string TitleFromPath(const std::string& path) {
    std::string name = path.substr(path.find_last_of('/') + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name.resize(dot);
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

void RefreshReading() {
    auto& st = StandbyClassic_State();
    if (st.book_title == nullptr) return;

    const std::string path = reader::book_home_snapshot::LoadLatestAbsPath();
    reader::BookSession::ProgressPeek peek;
    const bool has_progress = !path.empty() && reader::book_progress_cache::TryGet(path.c_str(), peek);
    const bool has_book = !path.empty() && (has_progress || !reader::book_progress_cache::IsReady());

    if (!has_book) {
        lv_label_set_text(st.book_title, Lang::Strings::STANDBY_NO_BOOK);
        lv_obj_add_flag(st.book_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(st.book_pct, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_label_set_text(st.book_title, TitleFromPath(path).c_str());
    if (has_progress && peek.progress_x10 >= 0) {
        const int x10 = std::clamp(peek.progress_x10, 0, 1000);
        lv_bar_set_value(st.book_bar, x10, LV_ANIM_OFF);
        char pct[12];
        std::snprintf(pct, sizeof(pct), "%d%%", x10 / 10);
        lv_label_set_text(st.book_pct, pct);
    } else {
        lv_bar_set_value(st.book_bar, 0, LV_ANIM_OFF);
        lv_label_set_text(st.book_pct, "--");
    }
}

void RefreshFooter(lv_obj_t* week_lbl) {
    auto& st = StandbyClassic_State();
    if (st.batt_lbl != nullptr) {
        int level = 0;
        bool charging = false, discharging = false;
        if (Board::GetInstance().GetBatteryLevel(level, charging, discharging) && level >= 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf),
                          charging ? Lang::Strings::STANDBY_CHARGING_FMT : Lang::Strings::STANDBY_BATTERY_FMT,
                          level);
            lv_label_set_text(st.batt_lbl, buf);
        } else {
            lv_label_set_text(st.batt_lbl, "");
        }
    }
    if (week_lbl != nullptr) {
        const auto week = reader::book_home_snapshot::LoadWeekStats();
        if (week.valid && week.total_seconds > 0) {
            const unsigned minutes = static_cast<unsigned>(week.total_seconds / 60);
            char dur[24];
            std::snprintf(dur, sizeof(dur), Lang::Strings::BOOK_DURATION_HM_FMT, minutes / 60, minutes % 60);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s  %s", Lang::Strings::BOOK_WEEK_READING, dur);
            lv_label_set_text(week_lbl, buf);
        } else {
            lv_label_set_text(week_lbl, "");
        }
    }
}

lv_obj_t* s_week_lbl = nullptr;

} // namespace

void StandbyClassic_RefreshDateOnce() {
    auto& st = StandbyClassic_State();
    if (st.day_lbl == nullptr) {
        return;
    }
    time_t now = time(nullptr);
    struct tm tm_info = {};
    if (localtime_r(&now, &tm_info) == nullptr || tm_info.tm_year < (2020 - 1900)) {
        // Clock not set yet (RTC lost power, no NTP): say so instead of showing a fake date.
        lv_label_set_text(st.wday_lbl, "");
        lv_obj_add_flag(st.day_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(st.month_lbl, Lang::Strings::STANDBY_CLOCK_NOT_SET);
        lv_obj_set_style_text_letter_space(st.month_lbl, 0, 0);
        return;
    }
    char day[4];
    std::snprintf(day, sizeof(day), "%d", tm_info.tm_mday);
    lv_label_set_text(st.day_lbl, day);
    lv_obj_remove_flag(st.day_lbl, LV_OBJ_FLAG_HIDDEN);

    char month[40];
    if (IsZh()) {
        static const char* const kWdayZh[] = {Lang::Strings::HOME_WDAY_SUN, Lang::Strings::HOME_WDAY_MON,
                                              Lang::Strings::HOME_WDAY_TUE, Lang::Strings::HOME_WDAY_WED,
                                              Lang::Strings::HOME_WDAY_THU, Lang::Strings::HOME_WDAY_FRI,
                                              Lang::Strings::HOME_WDAY_SAT};
        lv_label_set_text(st.wday_lbl, kWdayZh[tm_info.tm_wday % 7]);
        std::snprintf(month, sizeof(month), "%d年%d月", tm_info.tm_year + 1900, tm_info.tm_mon + 1);
    } else {
        lv_label_set_text(st.wday_lbl, kWeekdayEn[tm_info.tm_wday % 7]);
        std::snprintf(month, sizeof(month), "%s %d", kMonthEn[tm_info.tm_mon % 12], tm_info.tm_year + 1900);
    }
    lv_label_set_text(st.month_lbl, month);
}

void StandbyClassic_ApplyWeatherUi() {
    // Weather removed from the redesigned standby screen; kept as a no-op for listeners.
}

void StandbyClassic_BuildUi(lv_obj_t* root) {
    lv_obj_set_style_bg_color(root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    StandbyClassic_todos.clear();
    auto& st = StandbyClassic_State();

    const lv_coord_t W = LV_HOR_RES;
    const lv_coord_t H = LV_VER_RES;
    const lv_coord_t cw = W - 2 * kPad;
    const lv_font_t* body = BodyFont();
    const lv_font_t* small = SmallFont();

    // ---- Top block: date + continue reading -------------------------------
    lv_obj_t* top = Box(root);
    lv_obj_set_size(top, cw, LV_SIZE_CONTENT);
    lv_obj_set_pos(top, kPad, kTopY);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(top, 0, 0);

    st.wday_lbl = Label(top, body, "", 6);

    st.day_lbl = Label(top, HomePixel_ClockFont(), "");
    lv_obj_set_style_margin_top(st.day_lbl, 4, 0);
    lv_obj_set_style_translate_x(st.day_lbl, -6, 0);  // optical alignment of the serif digits

    st.month_lbl = Label(top, body, Lang::Strings::STANDBY_DATE_PLACEHOLDER, 4);
    lv_obj_set_width(st.month_lbl, cw);
    lv_label_set_long_mode(st.month_lbl, LV_LABEL_LONG_WRAP);

    lv_obj_t* rule = Rule(top, cw, kRuleH);
    lv_obj_set_style_margin_top(rule, 44, 0);

    lv_obj_t* cont = Label(top, small, Lang::Strings::STANDBY_CONTINUE_READING, 4);
    lv_obj_set_style_margin_top(cont, 40, 0);

    st.book_title = Label(top, body, "");
    lv_obj_set_width(st.book_title, cw);
    lv_obj_set_height(st.book_title, 2 * lv_font_get_line_height(body) + 4);
    lv_label_set_long_mode(st.book_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_margin_top(st.book_title, 14, 0);

    lv_obj_t* prog_row = Box(top);
    lv_obj_set_size(prog_row, cw, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(prog_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(prog_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(prog_row, 20, 0);
    lv_obj_set_style_margin_top(prog_row, 18, 0);

    st.book_bar = lv_bar_create(prog_row);
    lv_obj_remove_style_all(st.book_bar);
    lv_obj_set_height(st.book_bar, kBarH);
    lv_obj_set_flex_grow(st.book_bar, 1);
    lv_obj_set_style_border_width(st.book_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(st.book_bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(st.book_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(st.book_bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(st.book_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(st.book_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(st.book_bar, 0, LV_PART_INDICATOR);
    lv_obj_clear_flag(st.book_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_bar_set_range(st.book_bar, 0, 1000);
    lv_bar_set_value(st.book_bar, 0, LV_ANIM_OFF);

    st.book_pct = Label(prog_row, body, "--");

    // ---- Bottom block: home button + footer -------------------------------
    lv_obj_t* bottom = Box(root);
    lv_obj_set_size(bottom, cw, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bottom, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bottom, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(bottom, 0, 0);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_LEFT, kPad, -kFooterPad);

    st.home_btn = Box(bottom);
    lv_obj_set_size(st.home_btn, cw, kHomeBtnHeight);
    lv_obj_set_style_bg_color(st.home_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(st.home_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(st.home_btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(st.home_btn, 2, 0);
    lv_obj_set_style_radius(st.home_btn, kHomeBtnHeight / 2, 0);
    lv_obj_set_style_margin_bottom(st.home_btn, 40, 0);
    if (!StandbyClassic_as_overlay) {
        lv_obj_add_flag(st.home_btn, LV_OBJ_FLAG_CLICKABLE);
        HapticAttachClick(st.home_btn);
        lv_obj_add_event_cb(st.home_btn, StandbyClassic_OnHomeBtnClicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* menu_icon = lv_image_create(st.home_btn);
        lv_image_set_src(menu_icon, "A:ic_s_standby_menu.spng");
        lv_obj_set_size(menu_icon, kMenuIcon, kMenuIcon);
        lv_obj_center(menu_icon);
        lv_obj_clear_flag(menu_icon, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_flag(st.home_btn, LV_OBJ_FLAG_HIDDEN);
    }

    Rule(bottom, cw, 1);

    lv_obj_t* footer = Box(bottom);
    lv_obj_set_size(footer, cw, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(footer, 14, 0);

    st.batt_lbl = Label(footer, small, "");
    s_week_lbl = Label(footer, small, "");

    (void)H;
}

void StandbyClassic_StartOverlayRuntime() {
    StandbyClassic_RefreshDateOnce();
    RefreshReading();
    RefreshFooter(s_week_lbl);
    StandbyClassic_ApplyTodosFromCache();
}

void StandbyClassic_Build(lv_obj_t* root, bool as_overlay) {
    if (root == nullptr) {
        return;
    }
    StandbyClassic_as_overlay = as_overlay;
    StandbyClassic_alive = true;
    StandbyClassic_State() = StandbyClassicUiState{};
    s_week_lbl = nullptr;
    StandbyClassic_BuildUi(root);
}

void StandbyClassic_StartRuntime() {
    if (!StandbyClassic_alive) {
        return;
    }
    StandbyClassic_StartOverlayRuntime();
}

void StandbyClassic_StopRuntime() {}

void StandbyClassic_Teardown() {
    StandbyClassic_StopRuntime();
    StandbyClassic_alive = false;
    StandbyClassic_as_overlay = false;
    StandbyClassic_todos.clear();
    StandbyClassic_State() = StandbyClassicUiState{};
    s_week_lbl = nullptr;
}
