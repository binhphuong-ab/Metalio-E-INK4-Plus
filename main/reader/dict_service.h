#pragma once

// On-device dictionaries: every StarDict folder under SD_APP_ROOT/dictionaries
// (folders starting with '_' or '.' are skipped). The first dictionary that
// knows the word wins. Call only from a worker task: it reads the SD card.

#include <string>

namespace reader::dict {

struct Lookup {
    enum class Status : uint8_t { kFound, kNotFound, kNoDictionary, kBusy, kError };
    Status status = Status::kNotFound;
    std::string headword;
    std::string body;
    std::string dictionary;
    bool html = false;
};

/** True while no dictionary index is loaded yet (the first lookup will be slow). */
bool NeedsPreparation();

/** Open the dictionaries (once) and look the word up. */
Lookup Find(const std::string& word);

/** Folder holding the optional dictionary fonts (dict_regular.ef, dict_bold.ef, dict_italic.ef). */
const char* FontDir();

}  // namespace reader::dict
