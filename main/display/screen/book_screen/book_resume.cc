#include "book_screen/book_resume.h"

#include "book_screen/book_screen.h"
#include "book_screen/book_screen_priv.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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

struct RecordJob {
    bool reading;
    TaskHandle_t waiter;
};

// NVS writes disable the flash cache, so the writing task's stack must be in internal
// RAM (ESP-IDF asserts esp_task_stack_is_sane_cache_disabled()). The power-off task's
// stack is in PSRAM, so the write runs on this short-lived internal-stack task.
void RecordTask(void* arg) {
    auto* job = static_cast<RecordJob*>(arg);
    {
        Settings settings(kNs, true);
        settings.SetBool(kKey, job->reading);
    }
    xTaskNotifyGive(job->waiter);
    vTaskDelete(nullptr);
}

}  // namespace

void BookResume_RecordAtPowerOff() {
    // Runs on the power-off task; only pointer reads of the reader state.
    const auto& reader = Book_State().reader;
    static RecordJob job;  // static: outlives a timed-out wait below
    job.reading = reader.read_scr != nullptr && reader.session != nullptr;
    job.waiter = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0);
    // Plain xTaskCreate allocates the stack from internal RAM (same as tap_zones_nvs).
    if (xTaskCreate(RecordTask, "resume_nvs", 4096, &job, tskIDLE_PRIORITY + 5, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "power-off: resume task create failed");
        return;
    }
    const bool done = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) != 0;
    ESP_LOGI(kTag, "power-off while reading=%d saved=%d", job.reading ? 1 : 0, done ? 1 : 0);
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
