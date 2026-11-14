#include "stardict.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sys/stat.h>

#include <zlib.h>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define SD_LOGI(...) ESP_LOGI("StarDict", __VA_ARGS__)
#define SD_LOGW(...) ESP_LOGW("StarDict", __VA_ARGS__)
static void YieldBriefly() { vTaskDelay(1); }
#else
#define SD_LOGI(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
#define SD_LOGW(...) (std::fprintf(stderr, __VA_ARGS__), std::fputc('\n', stderr))
static void YieldBriefly() {}
#endif

namespace reader {
namespace {

constexpr char kCacheMagic[8] = {'M', 'T', 'D', 'X', '1', 0, 0, 0};
constexpr size_t kMaxWord = 256;

struct File {
    FILE* f = nullptr;
    explicit File(const std::string& path, const char* mode = "rb") : f(std::fopen(path.c_str(), mode)) {}
    ~File() { if (f) std::fclose(f); }
    explicit operator bool() const { return f != nullptr; }
};

bool FileSize(const std::string& path, uint32_t& size) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0 || st.st_size < 0 || static_cast<uint64_t>(st.st_size) > 0xFFFFFFFFull) return false;
    size = static_cast<uint32_t>(st.st_size);
    return true;
}

bool Exists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

int AsciiFold(unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// g_ascii_strcasecmp: the primary key of the StarDict .idx sort order.
int AsciiCaseCmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const int ca = AsciiFold(static_cast<unsigned char>(*a));
        const int cb = AsciiFold(static_cast<unsigned char>(*b));
        if (ca != cb || ca == 0) return ca - cb;
    }
}

uint32_t Be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void PutU32(std::string& s, uint32_t v) { s.append(reinterpret_cast<const char*>(&v), 4); }

// Stream NUL-terminated records (word + tail bytes) starting at offset.
// fn(word, word_len, tail, record_offset) returns false to stop.
template <class Fn>
bool ScanRecords(FILE* f, uint32_t offset, uint32_t end, uint32_t tail, size_t buf_size, Fn&& fn) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    std::vector<char> buf(buf_size);
    size_t have = 0, cur = 0;
    uint32_t base = offset;
    for (;;) {
        const size_t left = have - cur;
        const char* start = buf.data() + cur;
        const char* nul = left ? static_cast<const char*>(std::memchr(start, 0, left)) : nullptr;
        if (!nul || static_cast<size_t>(nul - start) + 1 + tail > left) {
            if (base + cur >= end) return true;
            if (left > kMaxWord + 16 && !nul) return false;  // corrupt: word too long
            std::memmove(buf.data(), start, left);
            base += static_cast<uint32_t>(cur);
            have = left;
            cur = 0;
            const size_t want = std::min<size_t>(buf.size() - have, end - (base + have));
            const size_t n = want ? std::fread(buf.data() + have, 1, want, f) : 0;
            if (n == 0) return left == 0;
            have += n;
            continue;
        }
        const size_t word_len = static_cast<size_t>(nul - start);
        if (!fn(start, word_len, reinterpret_cast<const uint8_t*>(nul + 1), base + static_cast<uint32_t>(cur)))
            return true;
        cur += word_len + 1 + tail;
    }
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

}  // namespace

// ---------------------------------------------------------------- SortedFile

bool StarDict::SortedFile::Build() {
    File f(path);
    if (!f) return false;
    offsets.clear();
    word_pos.clear();
    words.clear();
    count = 0;
    uint32_t chunks = 0;
    const bool ok = ScanRecords(f.f, 0, size, tail, 64 * 1024,
        [&](const char* w, size_t len, const uint8_t*, uint32_t off) {
            if (count % kSampleInterval == 0) {
                offsets.push_back(off);
                word_pos.push_back(static_cast<uint32_t>(words.size()));
                words.append(w, len);
                words.push_back('\0');
                if (++chunks % 512 == 0) YieldBriefly();
            }
            ++count;
            return true;
        });
    if (!ok || count == 0) {
        offsets.clear();
        word_pos.clear();
        words.clear();
        return false;
    }
    return true;
}

bool StarDict::SortedFile::Load(const std::string& cache) {
    File f(cache);
    if (!f) return false;
    char magic[8];
    uint32_t hdr[6];  // src_size, tail, interval, count, samples, words_len
    if (std::fread(magic, 1, 8, f.f) != 8 || std::memcmp(magic, kCacheMagic, 8) != 0) return false;
    if (std::fread(hdr, 4, 6, f.f) != 6) return false;
    if (hdr[0] != size || hdr[1] != tail || hdr[2] != kSampleInterval || hdr[4] == 0 ||
        hdr[4] > hdr[3] || hdr[5] > 64u * 1024 * 1024)
        return false;
    count = hdr[3];
    offsets.resize(hdr[4]);
    word_pos.resize(hdr[4]);
    words.resize(hdr[5]);
    if (std::fread(offsets.data(), 4, hdr[4], f.f) != hdr[4] ||
        std::fread(word_pos.data(), 4, hdr[4], f.f) != hdr[4] ||
        std::fread(&words[0], 1, hdr[5], f.f) != hdr[5]) {
        offsets.clear();
        return false;
    }
    for (uint32_t p : word_pos) {
        if (p >= words.size()) { offsets.clear(); return false; }
    }
    if (words.empty() || words.back() != '\0') { offsets.clear(); return false; }
    return true;
}

bool StarDict::SortedFile::Save(const std::string& cache) const {
    const std::string tmp = cache + ".tmp";
    {
        File f(tmp, "wb");
        if (!f) return false;
        std::string hdr(kCacheMagic, 8);
        for (uint32_t v : {size, tail, kSampleInterval, count, static_cast<uint32_t>(offsets.size()),
                           static_cast<uint32_t>(words.size())})
            PutU32(hdr, v);
        bool ok = std::fwrite(hdr.data(), 1, hdr.size(), f.f) == hdr.size() &&
                  std::fwrite(offsets.data(), 4, offsets.size(), f.f) == offsets.size() &&
                  std::fwrite(word_pos.data(), 4, word_pos.size(), f.f) == word_pos.size() &&
                  std::fwrite(words.data(), 1, words.size(), f.f) == words.size();
        ok = (std::fflush(f.f) == 0) && ok;
        if (!ok) { std::fclose(f.f); f.f = nullptr; std::remove(tmp.c_str()); return false; }
    }
    std::remove(cache.c_str());
    return std::rename(tmp.c_str(), cache.c_str()) == 0;
}

// ---------------------------------------------------------------- StarDict

StarDict::StarDict() = default;

StarDict::~StarDict() {
    if (data_) std::fclose(data_);
}

bool StarDict::Open(const std::string& dir, const std::string& cache_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    std::string ifo;
    // The folder name is usually the stem; otherwise take the first .ifo inside.
    const size_t slash = dir.find_last_of('/');
    const std::string folder = slash == std::string::npos ? dir : dir.substr(slash + 1);
    if (Exists(dir + "/" + folder + ".ifo")) {
        ifo = dir + "/" + folder + ".ifo";
    }
    if (ifo.empty()) return false;
    stem_ = ifo.substr(0, ifo.size() - 4);
    cache_dir_ = cache_dir;

    File f(ifo);
    if (!f) return false;
    char line[1024];
    bool magic = false;
    while (std::fgets(line, sizeof(line), f.f)) {
        std::string s = Trim(line);
        if (!magic) {
            if (s.find("StarDict's dict ifo file") == std::string::npos) return false;
            magic = true;
            continue;
        }
        const size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = s.substr(0, eq), value = s.substr(eq + 1);
        if (key == "bookname") name_ = value;
        else if (key == "sametypesequence") types_ = value;
        else if (key == "idxoffsetbits") offset64_ = value == "64";
    }
    if (!magic) return false;
    if (name_.empty()) name_ = folder;
    html_ = !types_.empty() && types_[0] == 'h';

    idx_.path = stem_ + ".idx";
    idx_.tail = offset64_ ? 12 : 8;
    if (!FileSize(idx_.path, idx_.size) || idx_.size == 0) return false;
    syn_.path = stem_ + ".syn";
    syn_.tail = 4;
    has_syn_ = FileSize(syn_.path, syn_.size) && syn_.size > 0;

    if (Exists(stem_ + ".dict")) {
        data_path_ = stem_ + ".dict";
        data_dz_ = false;
    } else if (Exists(stem_ + ".dict.dz")) {
        data_path_ = stem_ + ".dict.dz";
        data_dz_ = true;
    } else {
        return false;
    }
    open_ = true;
    return true;
}

bool StarDict::PrepareFile(SortedFile& file, const char* ext) {
    std::string cache;
    if (!cache_dir_.empty()) {
        const size_t slash = stem_.find_last_of('/');
        cache = cache_dir_ + "/" + stem_.substr(slash + 1) + ext + ".mtx";
        if (file.Load(cache)) return true;
    }
    SD_LOGI("indexing %s (%u bytes)", file.path.c_str(), static_cast<unsigned>(file.size));
    if (!file.Build()) return false;
    SD_LOGI("indexed %u records, %u samples", static_cast<unsigned>(file.count),
            static_cast<unsigned>(file.offsets.size()));
    if (!cache.empty() && !file.Save(cache)) SD_LOGW("could not save %s", cache.c_str());
    return true;
}

bool StarDict::PrepareIndex() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return false;
    if (index_ready_) return true;
    if (!PrepareFile(idx_, ".idx")) return false;
    if (has_syn_ && !PrepareFile(syn_, ".syn")) has_syn_ = false;
    index_ready_ = true;
    return true;
}

bool StarDict::Find(SortedFile& file, const std::string& target, std::vector<Match>& out) {
    out.clear();
    if (file.offsets.empty()) return false;
    // Last sample strictly below target; equal words may start before a sample.
    size_t lo = 0, hi = file.offsets.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (AsciiCaseCmp(file.SampleWord(mid), target.c_str()) < 0) lo = mid + 1;
        else hi = mid;
    }
    const size_t sample = lo == 0 ? 0 : lo - 1;
    File f(file.path);
    if (!f) return false;
    uint32_t ordinal = static_cast<uint32_t>(sample) * kSampleInterval;
    uint32_t scanned = 0;
    char word[kMaxWord + 1];
    const bool ok = ScanRecords(f.f, file.offsets[sample], file.size, file.tail, 4096,
        [&](const char* w, size_t len, const uint8_t* tail, uint32_t) {
            const size_t n = std::min(len, kMaxWord);
            std::memcpy(word, w, n);
            word[n] = '\0';
            const int cmp = AsciiCaseCmp(word, target.c_str());
            if (cmp > 0) return false;
            if (cmp == 0) {
                Match m;
                m.word.assign(word, n);
                std::memcpy(m.tail, tail, std::min<uint32_t>(file.tail, sizeof(m.tail)));
                m.ordinal = ordinal;
                out.push_back(std::move(m));
            }
            ++ordinal;
            return ++scanned < kSampleInterval * 8;
        });
    return ok;
}

bool StarDict::RecordAt(SortedFile& file, uint32_t ordinal, Match& out) {
    if (ordinal >= file.count || file.offsets.empty()) return false;
    const size_t sample = ordinal / kSampleInterval;
    if (sample >= file.offsets.size()) return false;
    uint32_t skip = ordinal % kSampleInterval;
    File f(file.path);
    if (!f) return false;
    bool found = false;
    ScanRecords(f.f, file.offsets[sample], file.size, file.tail, 4096,
        [&](const char* w, size_t len, const uint8_t* tail, uint32_t) {
            if (skip-- > 0) return true;
            out.word.assign(w, std::min(len, kMaxWord));
            std::memcpy(out.tail, tail, std::min<uint32_t>(file.tail, sizeof(out.tail)));
            out.ordinal = ordinal;
            found = true;
            return false;
        });
    return found;
}

void StarDict::DecodeLocation(const Match& m, uint64_t& off, uint32_t& size) const {
    if (offset64_) {
        off = (uint64_t(Be32(m.tail)) << 32) | Be32(m.tail + 4);
        size = Be32(m.tail + 8);
    } else {
        off = Be32(m.tail);
        size = Be32(m.tail + 4);
    }
}

bool StarDict::LocateWord(const std::string& target, const std::string& prefer, uint64_t& off,
                          uint32_t& size, std::string& headword, bool& io_error) {
    std::vector<Match> matches;
    if (!Find(idx_, target, matches)) io_error = true;
    if (matches.empty() && has_syn_) {
        std::vector<Match> syns;
        if (!Find(syn_, target, syns)) io_error = true;
        for (const auto& s : syns) {
            Match m;
            if (RecordAt(idx_, Be32(s.tail), m)) { matches.push_back(std::move(m)); break; }
        }
    }
    if (matches.empty()) return false;
    // Prefer the exact spelling the reader tapped ("May" vs "may"), then lower case.
    const Match* best = &matches[0];
    for (const auto& m : matches) if (m.word == target) { best = &m; break; }
    for (const auto& m : matches) if (m.word == prefer) { best = &m; break; }
    DecodeLocation(*best, off, size);
    headword = best->word;
    return true;
}

bool StarDict::OpenDictZip() {
    if (!dz_chunk_off_.empty()) return true;
    uint8_t h[10];
    if (std::fseek(data_, 0, SEEK_SET) != 0 || std::fread(h, 1, 10, data_) != 10) return false;
    if (h[0] != 0x1f || h[1] != 0x8b || h[2] != 8 || !(h[3] & 4)) return false;
    const uint8_t flags = h[3];
    uint8_t xl[2];
    if (std::fread(xl, 1, 2, data_) != 2) return false;
    const uint32_t xlen = xl[0] | (xl[1] << 8);
    std::vector<uint8_t> extra(xlen);
    if (std::fread(extra.data(), 1, xlen, data_) != xlen) return false;
    std::vector<uint16_t> sizes;
    for (size_t i = 0; i + 4 <= extra.size();) {
        const uint32_t len = extra[i + 2] | (extra[i + 3] << 8);
        if (extra[i] == 'R' && extra[i + 1] == 'A' && i + 4 + len <= extra.size() && len >= 6) {
            const uint8_t* p = extra.data() + i + 4;
            dz_chunk_len_ = p[2] | (p[3] << 8);
            const uint32_t count = p[4] | (p[5] << 8);
            if (6 + count * 2 > len) return false;
            for (uint32_t c = 0; c < count; ++c) sizes.push_back(p[6 + c * 2] | (p[7 + c * 2] << 8));
        }
        i += 4 + len;
    }
    if (sizes.empty() || dz_chunk_len_ == 0) return false;
    auto skip_string = [this]() {
        int c;
        while ((c = std::fgetc(data_)) != EOF && c != 0) {}
        return c == 0;
    };
    if ((flags & 8) && !skip_string()) return false;
    if ((flags & 16) && !skip_string()) return false;
    if ((flags & 2) && std::fseek(data_, 2, SEEK_CUR) != 0) return false;
    const long start = std::ftell(data_);
    if (start < 0) return false;
    dz_chunk_off_.resize(sizes.size() + 1);
    uint32_t off = static_cast<uint32_t>(start);
    uint32_t largest = 0;
    for (size_t i = 0; i < sizes.size(); ++i) {
        dz_chunk_off_[i] = off;
        off += sizes[i];
        largest = std::max<uint32_t>(largest, sizes[i]);
    }
    dz_chunk_off_.back() = off;
    dz_in_.resize(largest);
    dz_out_.resize(dz_chunk_len_);
    return true;
}

bool StarDict::ReadDzChunk(uint32_t index) {
    if (static_cast<int64_t>(index) == dz_cached_) return true;
    if (index + 1 >= dz_chunk_off_.size()) return false;
    const uint32_t comp = dz_chunk_off_[index + 1] - dz_chunk_off_[index];
    if (std::fseek(data_, static_cast<long>(dz_chunk_off_[index]), SEEK_SET) != 0 ||
        std::fread(dz_in_.data(), 1, comp, data_) != comp)
        return false;
    z_stream z {};
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
    z.next_in = dz_in_.data();
    z.avail_in = comp;
    z.next_out = dz_out_.data();
    z.avail_out = static_cast<uInt>(dz_out_.size());
    const int ret = inflate(&z, Z_SYNC_FLUSH);
    dz_out_len_ = static_cast<uint32_t>(dz_out_.size() - z.avail_out);
    inflateEnd(&z);
    if ((ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) || dz_out_len_ == 0) {
        dz_cached_ = -1;
        return false;
    }
    dz_cached_ = index;
    return true;
}

bool StarDict::ReadData(uint64_t off, uint32_t size, std::string& out) {
    out.clear();
    size = std::min(size, kMaxBodyBytes);
    if (!data_) {
        data_ = std::fopen(data_path_.c_str(), "rb");
        if (!data_) return false;
    }
    if (!data_dz_) {
        out.resize(size);
        if (std::fseek(data_, static_cast<long>(off), SEEK_SET) != 0 ||
            std::fread(&out[0], 1, size, data_) != size) {
            out.clear();
            return false;
        }
        return true;
    }
    if (!OpenDictZip()) return false;
    out.reserve(size);
    uint64_t pos = off;
    const uint64_t end = off + size;
    while (pos < end) {
        const uint32_t chunk = static_cast<uint32_t>(pos / dz_chunk_len_);
        if (!ReadDzChunk(chunk)) { out.clear(); return false; }
        const uint32_t in_chunk = static_cast<uint32_t>(pos % dz_chunk_len_);
        if (in_chunk >= dz_out_len_) { out.clear(); return false; }
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(dz_out_len_ - in_chunk, end - pos));
        out.append(reinterpret_cast<const char*>(dz_out_.data()) + in_chunk, n);
        pos += n;
    }
    return true;
}

std::string StarDict::ExtractBody(const std::string& raw) const {
    if (types_.size() == 1) return raw;  // whole entry is one field
    // No (or multi-field) sametypesequence: take the first field.
    const size_t start = types_.empty() ? 1 : 0;  // without sametypesequence each field has a type byte
    if (start > raw.size()) return std::string();
    const size_t nul = raw.find('\0', start);
    return raw.substr(start, nul == std::string::npos ? std::string::npos : nul - start);
}

StarDict::Result StarDict::Lookup(const std::string& word, std::string& headword, std::string& body) {
    std::lock_guard<std::mutex> lock(mutex_);
    headword.clear();
    body.clear();
    if (!open_ || !index_ready_) return Result::kError;
    const std::string cleaned = CleanWord(word);
    if (cleaned.empty()) return Result::kNotFound;
    const std::string prefer = CleanWord(word, false);  // reader's capitalisation breaks case ties
    std::vector<std::string> candidates{cleaned};
    std::vector<std::string> stems;
    StemVariants(cleaned, stems);
    candidates.insert(candidates.end(), stems.begin(), stems.end());
    // The firmware only lets the SD card switch to USB mode while no file is
    // open, so the data file is closed again after every lookup.
    struct CloseData {
        FILE*& f;
        ~CloseData() { if (f) { std::fclose(f); f = nullptr; } }
    } close_data{data_};
    bool io_error = false;
    for (const auto& candidate : candidates) {
        uint64_t off = 0;
        uint32_t size = 0;
        if (!LocateWord(candidate, prefer, off, size, headword, io_error)) continue;
        std::string raw;
        if (!ReadData(off, size, raw)) return Result::kError;
        body = ExtractBody(raw);
        return Result::kFound;
    }
    return io_error ? Result::kError : Result::kNotFound;
}

std::string StarDict::CleanWord(const std::string& raw, bool fold) {
    // Normalise curly apostrophes so "don’t" matches "don't".
    std::string s;
    s.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        const auto c = static_cast<unsigned char>(raw[i]);
        if (c == 0xE2 && i + 2 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0x80 &&
            (static_cast<unsigned char>(raw[i + 2]) == 0x98 || static_cast<unsigned char>(raw[i + 2]) == 0x99)) {
            s.push_back('\'');
            i += 2;
            continue;
        }
        s.push_back(static_cast<char>(c));
    }
    auto is_word = [](unsigned char c) { return c >= 0x80 || std::isalnum(c); };
    size_t a = 0, b = s.size();
    while (a < b) {
        const auto c = static_cast<unsigned char>(s[a]);
        if (is_word(c) && !(c == 0xE2)) break;
        a += (c == 0xE2 && a + 2 < b) ? 3 : 1;  // other General Punctuation (dashes, quotes)
    }
    while (b > a) {
        const auto c = static_cast<unsigned char>(s[b - 1]);
        if (b - a >= 3 && static_cast<unsigned char>(s[b - 3]) == 0xE2) { b -= 3; continue; }
        if (is_word(c)) break;
        --b;
    }
    std::string out = s.substr(a, b - a);
    if (fold) for (auto& c : out) c = static_cast<char>(AsciiFold(static_cast<unsigned char>(c)));
    return out;
}

void StarDict::StemVariants(const std::string& w, std::vector<std::string>& out) {
    out.clear();
    const size_t n = w.size();
    auto add = [&out](std::string v) {
        if (v.size() >= 2 && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(std::move(v));
    };
    auto ends = [&](const char* suf) {
        const size_t len = std::strlen(suf);
        return n > len + 1 && w.compare(n - len, len, suf) == 0;
    };
    auto cut = [&](size_t k) { return w.substr(0, n - k); };
    if (ends("'s")) add(cut(2));
    if (ends("s'")) add(cut(1));
    if (ends("ies")) add(cut(3) + "y");
    if (ends("ied")) add(cut(3) + "y");
    if (ends("ier")) add(cut(3) + "y");
    if (ends("iest")) add(cut(4) + "y");
    if (ends("ily")) add(cut(3) + "y");
    if (ends("es")) add(cut(2));
    if (ends("s") && !ends("ss")) add(cut(1));
    if (ends("ed")) {
        add(cut(2));
        add(cut(1));
        if (n >= 5 && w[n - 3] == w[n - 4]) add(cut(3));
    }
    if (ends("ing")) {
        add(cut(3));
        add(cut(3) + "e");
        if (n >= 6 && w[n - 4] == w[n - 5]) add(cut(4));
    }
    if (ends("er")) {
        add(cut(2));
        add(cut(1));
        if (n >= 5 && w[n - 3] == w[n - 4]) add(cut(3));
    }
    if (ends("est")) {
        add(cut(3));
        add(cut(2));
        if (n >= 6 && w[n - 4] == w[n - 5]) add(cut(4));
    }
    if (ends("ly")) add(cut(2));
    if (ends("ness")) add(cut(4));
    if (ends("ment")) add(cut(4));
    // Hyphenated or apostrophe compounds: try the first part ("well-known" -> "well").
    const size_t dash = w.find('-');
    if (dash != std::string::npos && dash >= 2) {
        std::string joined;
        for (char c : w) if (c != '-') joined.push_back(c);
        add(joined);
    }
}

}  // namespace reader
