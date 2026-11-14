#include "dict_service.h"

#include "stardict.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <dirent.h>
#include <memory>
#include <mutex>
#include <sys/stat.h>
#include <vector>

#include <esp_log.h>

#include "sd_access.h"
#include "sd_paths.h"

namespace reader::dict {
namespace {

constexpr const char* kTag = "Dict";
constexpr const char* kDictRoot = SD_APP_ROOT "/dictionaries";
constexpr const char* kCacheDir = SD_PATH_BOOK_CACHE "/dict";
constexpr const char* kFontDir = SD_APP_ROOT "/dictionaries/_fonts";

std::mutex s_mutex;
bool s_scanned = false;
std::atomic<bool> s_ready{false};
std::vector<std::unique_ptr<StarDict>> s_dicts;

void MakeDirs(const std::string& path) {
    for (size_t pos = 1; pos != std::string::npos;) {
        pos = path.find('/', pos + 1);
        const std::string part = path.substr(0, pos);
        if (mkdir(part.c_str(), 0775) != 0 && errno != EEXIST) {
            // Parent may be the mount point itself; keep going.
        }
    }
}

void ScanLocked() {
    // Keep rescanning until a dictionary turns up: the folder may be copied (or
    // the card handed back from USB mode) while the reader is running.
    if (s_scanned) return;
    DIR* dir = opendir(kDictRoot);
    if (!dir) {
        ESP_LOGI(kTag, "no dictionary folder %s", kDictRoot);
        return;
    }
    std::vector<std::string> names;
    while (dirent* e = readdir(dir)) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        names.emplace_back(e->d_name);
    }
    closedir(dir);
    std::sort(names.begin(), names.end());
    MakeDirs(kCacheDir);
    for (const auto& name : names) {
        auto d = std::make_unique<StarDict>();
        if (d->Open(std::string(kDictRoot) + "/" + name, kCacheDir)) {
            ESP_LOGI(kTag, "dictionary %s: %s", name.c_str(), d->Name().c_str());
            s_dicts.push_back(std::move(d));
        }
    }
    s_scanned = !s_dicts.empty();
}

}  // namespace

bool NeedsPreparation() { return !s_ready.load(); }

const char* FontDir() { return kFontDir; }

Lookup Find(const std::string& word) {
    Lookup result;
    std::lock_guard<std::mutex> lock(s_mutex);
    sd_access::Transaction transaction;
    if (!transaction) {
        ESP_LOGW(kTag, "SD card not available (USB mode or unmounted)");
        result.status = Lookup::Status::kBusy;
        return result;
    }
    ScanLocked();
    if (s_dicts.empty()) {
        result.status = Lookup::Status::kNoDictionary;
        return result;
    }
    bool any_error = false;
    bool any_ready = false;
    for (auto& d : s_dicts) {
        if (!d->PrepareIndex()) {
            ESP_LOGE(kTag, "index failed for %s", d->Name().c_str());
            any_error = true;
            continue;
        }
        any_ready = true;
        s_ready.store(true);
        const auto r = d->Lookup(word, result.headword, result.body);
        ESP_LOGI(kTag, "lookup '%s' in %s -> %d", word.c_str(), d->Name().c_str(), static_cast<int>(r));
        if (r == StarDict::Result::kFound) {
            result.status = Lookup::Status::kFound;
            result.dictionary = d->Name();
            result.html = d->HtmlDefinitions();
            return result;
        }
        if (r == StarDict::Result::kError) any_error = true;
    }
    // A miss in a readable dictionary is a genuine "not found"; only report an
    // error when nothing could be searched.
    result.status = (!any_ready || (any_error && s_dicts.size() == 1)) ? Lookup::Status::kError
                                                                         : Lookup::Status::kNotFound;
    return result;
}

}  // namespace reader::dict
