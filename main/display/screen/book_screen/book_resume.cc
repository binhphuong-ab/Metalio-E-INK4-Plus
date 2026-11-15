#include "book_screen/book_resume.h"

#include "book_screen/book_screen.h"
#include "book_screen/book_screen_priv.h"

#include <esp_log.h>
#include <lvgl.h>

#include "SdCardManager.hpp"
#include "reader/book_home_snapshot.h"
#include "reader/reader.h"
#include "screen_common.h"
#include "settings.h"

namespace {

constexpr const char* kTag = "BookResume";
constexpr const char* kNs = "ui";
constexpr const char* kKey = "resume_book";

void OpenLatestAsync(void*) {
    if (!SdCardManager::GetInstance().IsMounted()) return;
    if (reader::book_home_snapshot::LoadLatestAbsPath().empty()) return;
    ESP_LOGI(kTag, "reopening the book that was open at power-off");
    ScreenNavigateTo(BookScreen::CreateLatest);
}

}  // namespace

void BookResume_RecordAtPowerOff() {
    // Runs on the power-off task; only pointer reads of the reader state.
    const auto& reader = Book_State().reader;
    const bool reading = reader.read_scr != nullptr && reader.session != nullptr;
    Settings settings(kNs, true);
    settings.SetBool(kKey, reading);
    ESP_LOGI(kTag, "power-off while reading=%d", reading ? 1 : 0);
}

void BookResume_OnSystemReady() {
    bool pending = false;
    {
        Settings settings(kNs, true);
        pending = settings.GetBool(kKey, false);
        // One shot: a crash while reopening must not loop back into the book.
        if (pending) settings.SetBool(kKey, false);
    }
    if (pending) lv_async_call(OpenLatestAsync, nullptr);
}
