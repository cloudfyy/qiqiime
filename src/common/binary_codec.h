#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::codec {

// Centralized binary format magic numbers and schema versions across QiQiIME.
inline constexpr uint32_t kDictBinaryMagic        = 0x49514257u; // "WBQI" (.qdict v2 with payload checksum)
inline constexpr uint32_t kDictBinaryVersion      = 2u;
inline constexpr uint32_t kPhraseFreqBinaryMagic  = 0x31465051u; // "QPF1" (phrase_freq binary snapshot)
inline constexpr uint32_t kPhraseFreqBinaryVer    = 1u;
inline constexpr uint32_t kSyncJournalBinaryMagic = 0x53435151u; // "QQCS" (change_sync journal.bin)
inline constexpr uint32_t kSyncJournalBinaryVer   = 1u;
inline constexpr uint64_t kSourceMetaBinaryMagic  = 0x4154454D49514951ull; // "QIQIMETA" (source.meta)
inline constexpr uint32_t kSourceMetaBinaryVer    = 1u;

constexpr uint64_t ComputeFnv1a64(std::span<const uint8_t> bytes) noexcept {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t b : bytes) {
        hash = (hash ^ b) * 1099511628211ull;
    }
    return hash;
}

constexpr uint32_t ComputeFnv1a32(std::span<const uint8_t> bytes) noexcept {
    const uint64_t h64 = ComputeFnv1a64(bytes);
    const uint32_t folded = gsl::narrow_cast<uint32_t>(h64 ^ (h64 >> 32));
    return folded != 0 ? folded : 1u;
}

inline void AppendLeNumber(std::vector<uint8_t>& out, uint64_t value, size_t byte_count) {
    for (size_t i = 0; i < byte_count; ++i) {
        out.push_back(gsl::narrow_cast<uint8_t>(value & 0xFFu));
        value >>= 8;
    }
}

inline void AppendLe32(std::vector<uint8_t>& out, uint32_t value) {
    AppendLeNumber(out, value, 4);
}

inline void AppendLe64(std::vector<uint8_t>& out, uint64_t value) {
    AppendLeNumber(out, value, 8);
}

inline void AppendGuid(std::vector<uint8_t>& out, const GUID& id) {
    AppendLeNumber(out, id.Data1, 4);
    AppendLeNumber(out, id.Data2, 2);
    AppendLeNumber(out, id.Data3, 2);
    for (const uint8_t b : id.Data4) {
        out.push_back(b);
    }
}

inline void AppendWString(std::vector<uint8_t>& out, std::wstring_view str) {
    AppendLe32(out, gsl::narrow_cast<uint32_t>(str.size()));
    for (const wchar_t ch : str) {
        const auto u = gsl::narrow_cast<uint16_t>(ch);
        out.push_back(gsl::narrow_cast<uint8_t>(u & 0xFFu));
        out.push_back(gsl::narrow_cast<uint8_t>((u >> 8) & 0xFFu));
    }
}

// Bounds-checked little-endian binary reader shared by change_sync, dictionary, and phrase-frequency codecs.
class BinarySpanReader final {
public:
    explicit BinarySpanReader(std::span<const uint8_t> bytes) noexcept : bytes_(bytes) {}

    uint64_t ReadLeNumber(size_t byte_count) noexcept {
        if (byte_count > bytes_.size()) {
            valid_ = false;
            return 0;
        }
        uint64_t result = 0;
        for (size_t i = 0; i < byte_count; ++i) {
            result |= uint64_t{gsl::at(bytes_, i)} << (i * 8);
        }
        bytes_ = bytes_.subspan(byte_count);
        return result;
    }

    uint32_t ReadLe32() noexcept {
        return gsl::narrow_cast<uint32_t>(ReadLeNumber(4));
    }

    uint64_t ReadLe64() noexcept {
        return ReadLeNumber(8);
    }

    GUID ReadGuid() noexcept {
        GUID result{};
        result.Data1 = gsl::narrow_cast<ULONG>(ReadLeNumber(4));
        result.Data2 = gsl::narrow_cast<USHORT>(ReadLeNumber(2));
        result.Data3 = gsl::narrow_cast<USHORT>(ReadLeNumber(2));
        for (auto& b : result.Data4) {
            b = gsl::narrow_cast<uint8_t>(ReadLeNumber(1));
        }
        return result;
    }

    std::vector<uint8_t> ReadBytes(size_t count) {
        if (count > bytes_.size()) {
            valid_ = false;
            return {};
        }
        const auto sub = bytes_.first(count);
        bytes_ = bytes_.subspan(count);
        return {sub.begin(), sub.end()};
    }

    std::wstring ReadWString(size_t max_chars = 256) {
        const uint32_t count = ReadLe32();
        const size_t char_count = gsl::narrow_cast<size_t>(count);
        if (!valid_ || char_count > max_chars || bytes_.size() < char_count * 2) {
            valid_ = false;
            return {};
        }
        std::wstring result;
        result.reserve(char_count);
        for (size_t i = 0; i < char_count; ++i) {
            const uint16_t lo = gsl::at(bytes_, i * 2);
            const uint16_t hi = gsl::at(bytes_, i * 2 + 1);
            result.push_back(gsl::narrow_cast<wchar_t>(lo | (hi << 8)));
        }
        bytes_ = bytes_.subspan(char_count * 2);
        return result;
    }

    bool Valid() const noexcept { return valid_; }
    bool Empty() const noexcept { return bytes_.empty(); }

private:
    std::span<const uint8_t> bytes_;
    bool valid_ = true;
};

} // namespace qiqi::codec
