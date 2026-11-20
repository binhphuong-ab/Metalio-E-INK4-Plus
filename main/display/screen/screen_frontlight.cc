#include "assets/lang_screen.h"
#include "assets/lang_config.h"
#pragma GCC optimize("O1")

#include "screen_frontlight.h"
#include "screen_common.h"
#include "ota_upgrade_screen/ota_upgrade_screen.h"
#include "standby_screen/standby_screen.h"
#include "fontpack_lvgl.h"
#include "frontlight.h"
#include "haptic_feedback.h"
#include "home_icon_source.h"
#include "lv_adapter_epdiy.h"
#include "settings.h"
#include "settings_screen/settings_frontlight_tab.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "QuickControls"

// Pull-down quick panel: front-light brightness only.
//   Portrait:   Front light             [ 5% ] [ 50% ]
//               37%
//               [-] [==========      ] [+]
//   Landscape:  one slim row: [Front light / 37%] [-] [====] [+] [5%] [50%]
// -/+ step 1% (hold to repeat); 5% and 50% are one-tap presets.
namespace {
constexpr int kPresetLow = 5;
constexpr int kPresetHigh = 50;

struct PanelUi {
    lv_obj_t* root = nullptr;                // full-screen touch mask
    EpdStatusBar previous_status;            // status bar binding of the page underneath
    lv_obj_t* panel_status_label = nullptr;  // status bar binding of the panel
    lv_obj_t* value = nullptr;               // "37%"
    lv_obj_t* slider = nullptr;              // horizontal brightness slider
    lv_obj_t* preset[2] = {nullptr, nullptr};
    int shown = -1;                          // last drawn level, avoids needless e-ink redraws
};
PanelUi s_ui;
std::atomic<bool> s_save_running{false};
std::atomic<uint32_t> s_saved_value{0};  // low three bytes: brightness, warmth, cct
bool s_light_dirty = false;              // LVGL context; survives a failed save task

void SaveLightTask(void*) {
    for (;;) {
        const uint32_t value = s_saved_value.load();
        {
            Settings settings("frontlight", true);
            settings.SetInt("brightness", value & 0xff);
            settings.SetInt("warmth", (value >> 8) & 0xff);
            settings.SetInt("cct", (value >> 16) & 0xff);
        }
        s_save_running.store(false);
        if (value == s_saved_value.load() || s_save_running.exchange(true)) break;
    }
    vTaskDelete(nullptr);
}

void SaveLight() {
    if (!s_light_dirty) return;
    auto& light = Frontlight::GetInstance();
    s_saved_value.store(light.target_brightness() | (light.warmth() << 8) |
                        (static_cast<uint32_t>(light.cct()) << 16));
    if (!s_save_running.exchange(true) &&
        xTaskCreate(SaveLightTask, "quick_fl_save", 4096, nullptr, 5, nullptr) != pdPASS) {
        s_save_running.store(false);
        ESP_LOGW(TAG, "frontlight save task failed");
        return;
    }
    s_light_dirty = false;
}

// Level the user sees: 0 while the light is switched off, whatever brightness is stored.
int CurrentLevel() {
    auto& light = Frontlight::GetInstance();
    if (light.cct() == FrontlightCct::kOff) return 0;
    return light.target_brightness();
}

void RefreshUi() {
    const int level = CurrentLevel();
    if (level == s_ui.shown) return;
    s_ui.shown = level;
    char text[8];
    std::snprintf(text, sizeof(text), "%d%%", level);
    lv_label_set_text(s_ui.value, text);
    lv_slider_set_value(s_ui.slider, level, LV_ANIM_OFF);
    const int presets[2] = {kPresetLow, kPresetHigh};
    for (int i = 0; i < 2; ++i) {
        const bool active = level == presets[i];
        lv_obj_set_style_bg_color(s_ui.preset[i], active ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_ui.preset[i], 0),
                                    active ? lv_color_white() : lv_color_black(), 0);
    }
}

void ApplyLevel(int level) {
    level = std::clamp(level, 0, 100);
    auto& light = Frontlight::GetInstance();
    light.SetBrightness(static_cast<uint8_t>(level), false);
    // A light switched off elsewhere keeps cct=kOff; SetWarmth restores the channel mix.
    if (level > 0 && light.cct() == FrontlightCct::kOff) light.SetWarmth(light.warmth());
    s_light_dirty = true;
    RefreshUi();
}

void SliderChanged(lv_event_t* event) {
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        ApplyLevel(lv_slider_get_value(s_ui.slider));
    } else {
        SaveLight();  // RELEASED / PRESS_LOST
    }
}

void StepPressed(lv_event_t* event) {
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED_REPEAT) {
        const int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
        ApplyLevel(CurrentLevel() + step);
    } else {
        SaveLight();  // RELEASED / PRESS_LOST
    }
}

void PresetClicked(lv_event_t* event) {
    ApplyLevel(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
    SaveLight();
}

lv_obj_t* Box(lv_obj_t* parent, int x, int y, int width, int height) {
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* Label(lv_obj_t* parent, const char* text, const lv_font_t* font) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

// Rounded outlined button with a centred label (child 0).
lv_obj_t* Button(lv_obj_t* parent, int x, int y, int width, int height, const char* text,
                 const lv_font_t* font) {
    auto* button = Box(parent, x, y, width, height);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_bg_color(button, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_black(), 0);
    lv_obj_set_style_radius(button, height * 2 / 5, 0);
    lv_obj_center(Label(button, text, font));
    HapticAttachClick(button);
    return button;
}
}  // namespace

bool ScreenFrontlightIsVisible() {
    return s_ui.root != nullptr;
}

bool ScreenFrontlightOwnsDrag(int x, int y) {
    if (!s_ui.slider) return false;
    lv_area_t area;
    lv_obj_get_coords(s_ui.slider, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void ScreenFrontlightHide() {
    if (!s_ui.root) return;
    SaveLight();
    if (auto* display = LvAdapterEpdiy::Instance()) display->RequestFullOnNextCommit();
    lv_obj_delete(s_ui.root);
    SettingsFrontlightTab_Refresh();
}

void ScreenFrontlightShow() {
    if (!HomeIconDecoderReady()) return;
    if (OtaUpgradeScreen::IsActive() || StandbyScreen::IsActive() ||
        s_ui.root || !Frontlight::GetInstance().initialized()) return;
    auto* root = Box(lv_layer_top(), 0, 0, LV_HOR_RES, LV_VER_RES);
    s_ui.root = root;
    lv_obj_add_event_cb(root, [](lv_event_t*) {
        if (auto* display = LvAdapterEpdiy::Instance(); display &&
            display->GetBoundStatusBar().status_label == s_ui.panel_status_label) {
            auto status = s_ui.previous_status;
            if (status.status_label && !lv_obj_is_valid(status.status_label)) status = {};
            display->BindStatusBar(status.status_label, status.notification_label, status.network_label,
                                   status.battery_label, status.battery_pct_label, status.mute_label);
        }
        s_ui = {};
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(root, [](lv_event_t*) { ScreenFrontlightHide(); }, LV_EVENT_CLICKED, nullptr);
    if (auto* display = LvAdapterEpdiy::Instance()) s_ui.previous_status = display->GetBoundStatusBar();
    const auto status = ScreenCreateStatusBar(root);
    s_ui.panel_status_label = status.status_label;
    lv_obj_set_style_bg_color(status.bar, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(status.bar, LV_OPA_COVER, 0);
    if (auto* display = LvAdapterEpdiy::Instance()) display->UpdateStatusBar(true);

    const auto* ui_font = fontpack_lv_font_ui();
    const auto* big_font = fontpack_lv_font_get(30, 4);
    if (big_font == nullptr) big_font = ui_font;

    const int width = LV_HOR_RES;
    // Same compact style in both orientations; sizes follow the short side so rotation does
    // not inflate them. Landscape uses one row, portrait wraps -/slider/+ onto a second row.
    const bool landscape = LV_HOR_RES > LV_VER_RES;
    const int unit = std::min<int>(LV_HOR_RES, LV_VER_RES);
    const int pad = unit / 20;
    const int gap = unit / 40;
    const int step_size = unit * 2 / 15;  // square -/+ buttons; presets share the height
    const int preset_w = unit / 5;
    const int label_w = unit * 3 / 10;
    const int radius = unit / 26;
    const int height = landscape ? pad + step_size + pad : pad + step_size + gap + step_size + pad;
    const int panel_y = ScreenStatusBarHeight();

    auto* clip = Box(root, 0, panel_y, width, height);
    // Top corners sit outside the clip: square top edge, rounded bottom, continuous side borders.
    auto* panel = Box(clip, 0, -radius, width, height + radius);
    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_black(), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_side(panel, static_cast<lv_border_side_t>(
        LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);

    // "Front light" with the level under it, top-left.
    const int y = radius + pad;
    const auto* small_font = fontpack_lv_font_get(25, 2);
    if (small_font == nullptr) small_font = ui_font;
    auto* title = Label(panel, Lang::Strings::SETTINGS_FRONTLIGHT_TITLE, small_font);
    const int text_h = small_font->line_height + big_font->line_height;
    const int text_y = y + (step_size - text_h) / 2;
    lv_obj_set_pos(title, pad, text_y);
    s_ui.value = Label(panel, "", big_font);
    lv_obj_set_pos(s_ui.value, pad, text_y + small_font->line_height);

    // Presets sit at the right end of the first row in both orientations.
    const int preset_y = y;
    const int preset_x0 = width - pad - 2 * preset_w - gap;
    int row_y, minus_x, slider_w;
    if (landscape) {
        // [Front light / 37%]  [-] [=====] [+]   [5%] [50%]
        row_y = y;
        minus_x = pad + label_w + gap;
        slider_w = preset_x0 - 2 * gap - minus_x - 2 * (step_size + gap);
    } else {
        // [Front light / 37%]        [5%] [50%]
        // [-] [======================] [+]
        row_y = y + step_size + gap;
        minus_x = pad;
        slider_w = width - 2 * pad - 2 * (step_size + gap);
    }

    const int slider_x = minus_x + step_size + gap;
    const int plus_x = slider_x + slider_w + gap;
    auto* minus = Button(panel, minus_x, row_y, step_size, step_size, "-", big_font);
    auto* plus = Button(panel, plus_x, row_y, step_size, step_size, "+", big_font);
    for (auto [button, step] : {std::pair{minus, -1}, std::pair{plus, 1}}) {
        for (auto code : {LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED_REPEAT, LV_EVENT_RELEASED,
                          LV_EVENT_PRESS_LOST})
            lv_obj_add_event_cb(button, StepPressed, code, reinterpret_cast<void*>(static_cast<intptr_t>(step)));
    }

    const int slider_h = step_size / 2;
    s_ui.slider = lv_slider_create(panel);
    lv_obj_remove_style_all(s_ui.slider);
    lv_obj_set_pos(s_ui.slider, slider_x, row_y + (step_size - slider_h) / 2);
    lv_obj_set_size(s_ui.slider, slider_w, slider_h);
    lv_slider_set_range(s_ui.slider, 0, 100);
    lv_obj_set_style_bg_color(s_ui.slider, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_ui.slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ui.slider, 2, LV_PART_MAIN);
    lv_obj_set_style_border_post(s_ui.slider, true, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ui.slider, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(s_ui.slider, slider_h / 2, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(s_ui.slider, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.slider, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_ui.slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_ui.slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_opa(s_ui.slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(s_ui.slider, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(s_ui.slider, LV_OBJ_FLAG_SCROLLABLE);
    for (auto code : {LV_EVENT_VALUE_CHANGED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST})
        lv_obj_add_event_cb(s_ui.slider, SliderChanged, code, nullptr);

    const int presets[2] = {kPresetLow, kPresetHigh};
    for (int i = 0; i < 2; ++i) {
        char text[8];
        std::snprintf(text, sizeof(text), "%d%%", presets[i]);
        s_ui.preset[i] = Button(panel, preset_x0 + i * (preset_w + gap), preset_y, preset_w, step_size,
                                text, big_font);
        lv_obj_add_event_cb(s_ui.preset[i], PresetClicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(static_cast<intptr_t>(presets[i])));
    }

    RefreshUi();
    if (auto* display = LvAdapterEpdiy::Instance()) display->RequestFullOnNextCommit();
}
