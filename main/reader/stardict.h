#pragma once

// Minimal StarDict reader for the on-device dictionary.
//
// A dictionary is a folder holding <stem>.ifo, <stem>.idx (uncompressed),
// <stem>.dict or <stem>.dict.dz (dictzip) and an optional <stem>.syn.
// Nothing large is held in RAM: the .idx/.syn files are sampled every
// kSampleInterval records (offset + word), the samples are cached on SD, and a
// lookup bisects the samples and then scans at most a few hundred bytes.
// Portable C++ (stdio + zlib) so it can be tested on a desktop.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace reader {

class StarDict {
public:
    enum class Result : uint8_t { kFound, kNotFound, kError };

    StarDict();
    ~StarDict();
    StarDict(const StarDict&) = delete;
    StarDict& operator=(const StarDict&) = delete;

    /** Parse the .ifo inside dir and locate the data files. cache_dir may be empty. */
    bool Open(const std::string& dir, const std::string& cache_dir);
    bool IsOpen() const { return open_; }
    const std::string& Name() const { return name_; }
    bool HtmlDefinitions() const { return html_; }

    /** True once the sampled indexes are in memory (lookups are then fast). */
    bool IndexReady() const { return index_ready_; }
    /** Load the cached samples, or build them by streaming the .idx/.syn (slow, once). */
    bool PrepareIndex();

    /**
     * Look a word up: exact (ASCII case-insensitive) match, then the .syn
     * synonyms, then simple English stems (-s, -ed, -ing, ...).
     */
    Result Lookup(const std::string& word, std::string& headword, std::string& body);

    /** Trim punctuation around a selected word and fold ASCII to lower case. */
    static std::string CleanWord(const std::string& raw, bool fold = true);
    static void StemVariants(const std::string& word, std::vector<std::string>& out);

    static constexpr uint32_t kSampleInterval = 32;
    static constexpr uint32_t kMaxBodyBytes = 64 * 1024;

private:
    struct SortedFile {
        std::string path;
        uint32_t size = 0;
        uint32_t tail = 8;   // fixed bytes after each NUL-terminated word
        uint32_t count = 0;  // records
        std::vector<uint32_t> offsets;   // byte offset of every kSampleInterval-th record
        std::vector<uint32_t> word_pos;  // start of that record's word inside words
        std::string words;               // sample words, NUL separated
        bool Build();
        bool Load(const std::string& cache);
        bool Save(const std::string& cache) const;
        const char* SampleWord(size_t i) const { return words.data() + word_pos[i]; }
    };
    struct Match {
        std::string word;
        uint8_t tail[12] = {};
        uint32_t ordinal = 0;
    };

    bool PrepareFile(SortedFile& file, const char* ext);
    // All records whose word equals target ignoring ASCII case.
    bool Find(SortedFile& file, const std::string& target, std::vector<Match>& out);
    bool RecordAt(SortedFile& file, uint32_t ordinal, Match& out);
    bool LocateWord(const std::string& target, const std::string& prefer, uint64_t& off,
                    uint32_t& size, std::string& headword, bool& io_error);
    void DecodeLocation(const Match& m, uint64_t& off, uint32_t& size) const;
    bool ReadData(uint64_t off, uint32_t size, std::string& out);
    bool OpenDictZip();
    bool ReadDzChunk(uint32_t index);
    std::string ExtractBody(const std::string& raw) const;

    std::mutex mutex_;
    bool open_ = false;
    bool index_ready_ = false;
    bool html_ = false;
    bool offset64_ = false;
    std::string name_;
    std::string stem_;      // dir + "/" + stem
    std::string cache_dir_;
    std::string types_;     // sametypesequence
    std::string data_path_;
    bool data_dz_ = false;
    bool has_syn_ = false;
    SortedFile idx_;
    SortedFile syn_;
    // dictzip
    FILE* data_ = nullptr;
    uint32_t dz_chunk_len_ = 0;
    std::vector<uint32_t> dz_chunk_off_;  // compressed offsets, size chunk_count + 1
    std::vector<uint8_t> dz_in_;
    std::vector<uint8_t> dz_out_;
    uint32_t dz_out_len_ = 0;
    int64_t dz_cached_ = -1;
};

}  // namespace reader
