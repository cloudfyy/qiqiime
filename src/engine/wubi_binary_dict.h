#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi {

// Pack 1..4 lowercase Wubi letters ('a'..'z') into a big-endian uint32_t.
// Numeric comparison on the returned uint32_t is identical to lexicographical order.
// Returns 0 if the code is empty, longer than 4 chars, or contains non-'a'..'z' characters.
constexpr uint32_t PackWubiCode(std::wstring_view code) noexcept {
    if (code.empty() || code.size() > 4) {
        return 0;
    }
    uint32_t packed = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        wchar_t ch = code[i];
        if (ch >= L'A' && ch <= L'Z') {
            ch = static_cast<wchar_t>(ch - L'A' + L'a');
        }
        if (ch < L'a' || ch > L'z') {
            return 0;
        }
        const uint32_t byte_val = static_cast<uint32_t>(ch - L'a' + 1);
        packed |= (byte_val << ((3 - i) * 8));
    }
    return packed;
}

// Compute the inclusive upper bound of packed codes that share the given prefix (1..4 chars).
constexpr uint32_t PackWubiPrefixUpperBound(std::wstring_view prefix) noexcept {
    const uint32_t lo = PackWubiCode(prefix);
    if (lo == 0 || prefix.size() >= 4) {
        return lo;
    }
    const uint32_t mask = (1u << ((4 - prefix.size()) * 8)) - 1u;
    return lo | mask;
}

// Check if a packed code matches a wildcard pattern (where 'z' in pattern matches any letter).
constexpr bool MatchesWildcardPattern(uint32_t packed_code, std::wstring_view pattern) noexcept {
    if (pattern.empty() || pattern.size() > 4) {
        return false;
    }
    for (size_t i = 0; i < pattern.size(); ++i) {
        const uint32_t code_byte = (packed_code >> ((3 - i) * 8)) & 0xFFu;
        if (code_byte == 0) {
            return false; // Code is shorter than pattern
        }
        wchar_t pat_ch = pattern[i];
        if (pat_ch >= L'A' && pat_ch <= L'Z') {
            pat_ch = static_cast<wchar_t>(pat_ch - L'A' + L'a');
        }
        if (pat_ch != L'z') {
            const uint32_t pat_byte = static_cast<uint32_t>(pat_ch - L'a' + 1);
            if (code_byte != pat_byte) {
                return false;
            }
        }
    }
    return true;
}

#pragma pack(push, 1)
struct DictBinaryHeader {
    uint32_t magic = 0x49514257u; // "WBQI"
    uint32_t version = 1u;
    uint64_t source_size = 0;
    uint64_t source_mtime = 0;
    uint32_t code_count = 0;
    uint32_t word_count = 0;
    uint32_t char_count = 0;
    uint32_t reserved = 0;
};

struct DictCodeIndex {
    uint32_t packed_code = 0;
    uint32_t first_word_idx = 0;
    uint32_t word_count = 0;
};

struct DictWordSlice {
    uint32_t char_offset = 0;
    uint32_t char_length = 0;
};
#pragma pack(pop)

static_assert(sizeof(DictBinaryHeader) == 40);
static_assert(sizeof(DictCodeIndex) == 12);
static_assert(sizeof(DictWordSlice) == 8);

// Read-only memory-mapped (MapViewOfFile) or flat-buffer Wubi dictionary.
// OS pages in 4KB blocks on demand and shares physical memory across all processes.
class WubiMappedDict final {
public:
    WubiMappedDict() noexcept = default;
    ~WubiMappedDict() noexcept;

    WubiMappedDict(const WubiMappedDict&) = delete;
    WubiMappedDict& operator=(const WubiMappedDict&) = delete;
    WubiMappedDict(WubiMappedDict&&) = delete;
    WubiMappedDict& operator=(WubiMappedDict&&) = delete;

    // Load from a .txt / .yaml dictionary file, using or generating a memory-mapped .qdict cache.
    static std::shared_ptr<const WubiMappedDict> LoadFromTextOrCache(
        const std::filesystem::path& txt_path);

    bool IsValid() const noexcept { return !code_index_.empty(); }
    bool IsMemoryMapped() const noexcept { return mapped_view_ != nullptr; }
    size_t CodeCount() const noexcept { return code_index_.size(); }
    size_t WordCount() const noexcept { return word_slices_.size(); }

    // Append exact matches for packed_code into out_words (deduplicated).
    void LookupExact(uint32_t packed_code, std::vector<std::wstring>& out_words) const;

    // Append prefix matches in [prefix_lo, prefix_hi] (excluding exact_code) up to max_results.
    void LookupPrefix(uint32_t exact_code,
                      uint32_t prefix_lo,
                      uint32_t prefix_hi,
                      std::vector<std::wstring>& out_words,
                      size_t max_results) const;

    // Append wildcard ('z') matches up to max_results.
    void LookupWildcard(std::wstring_view pattern,
                        std::vector<std::wstring>& out_words,
                        size_t max_results) const;

private:
    bool OpenMappedFile(const std::filesystem::path& bin_path,
                        uint64_t expected_src_size,
                        uint64_t expected_src_mtime) noexcept;
    bool BindBufferView(const uint8_t* base,
                        size_t total_bytes,
                        uint64_t expected_src_size,
                        uint64_t expected_src_mtime) noexcept;
    void CloseMapping() noexcept;
    std::wstring_view GetWordView(const DictWordSlice& slice) const noexcept;

    HANDLE file_handle_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_handle_ = nullptr;
    const void* mapped_view_ = nullptr;
    std::vector<uint8_t> fallback_buffer_;

    std::span<const DictCodeIndex> code_index_;
    std::span<const DictWordSlice> word_slices_;
    std::span<const wchar_t> char_pool_;
};

} // namespace qiqi
