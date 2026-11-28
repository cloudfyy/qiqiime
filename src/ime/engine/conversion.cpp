// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/conversion.h"
#include "common/dictcodec.h"
#include "ime/engine/dictdetail.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace qiqi {
namespace {

constexpr uint32_t kMagic = 0x564E4351u; // QCNV
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kRecordBytes = 8;
constexpr size_t kMaxBytes = 4u * 1024u * 1024u;
constexpr size_t kMaxTextUnits = 32;
constexpr uint64_t kContentHash = 0xE40D143B670BAEE3ull;

struct Row {
    size_t offset;
    size_t key_length;
    size_t value_length;
};

Row ReadRow(std::span<const uint8_t> records, size_t index) noexcept {
    codec::BinarySpanReader row(records.subspan(index * kRecordBytes, kRecordBytes));
    return {row.ReadLe32(), gsl::narrow_cast<size_t>(row.ReadLeNumber(2)),
        gsl::narrow_cast<size_t>(row.ReadLeNumber(2))};
}

[[gsl::suppress("type.1")]]
std::wstring_view TextPool(std::span<const uint8_t> bytes) noexcept {
    static_assert(sizeof(wchar_t) == 2 && std::endian::native == std::endian::little);
    // The mapping is page-aligned and the header and records have even sizes.
    return {reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t)};
}

bool ValidRows(std::span<const uint8_t> records, std::wstring_view pool) noexcept {
    std::wstring_view previous;
    for (size_t i = 0; i < records.size() / kRecordBytes; ++i) {
        const Row row = ReadRow(records, i);
        if (row.key_length == 0 || row.key_length > kMaxTextUnits ||
            row.value_length == 0 || row.value_length > kMaxTextUnits ||
            row.offset > pool.size() || row.key_length + row.value_length > pool.size() - row.offset) {
            return false;
        }
        const auto key = pool.substr(row.offset, row.key_length);
        if (key <= previous) return false;
        previous = key;
    }
    return true;
}

std::wstring_view FindReplacement(
    std::span<const uint8_t> records, std::wstring_view pool, std::wstring_view key) noexcept {
    size_t first = 0;
    size_t end = records.size() / kRecordBytes;
    while (first < end) {
        const size_t middle = first + (end - first) / 2;
        const Row row = ReadRow(records, middle);
        const auto entry = pool.substr(row.offset, row.key_length);
        if (entry == key) return pool.substr(row.offset + row.key_length, row.value_length);
        if (entry < key) first = middle + 1;
        else end = middle;
    }
    return {};
}

} // namespace

HRESULT ConversionIndex::Open(const std::filesystem::path& path) noexcept {
    ReadOnlyFileMapping mapped;
    const HRESULT opened = mapped.Open(path, kHeaderBytes + 2 * kRecordBytes, kMaxBytes);
    if (FAILED(opened)) return opened;
    const auto bytes = mapped.Bytes();
    codec::BinarySpanReader header(bytes.first(kHeaderBytes));
    if (header.ReadLe32() != kMagic) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (header.ReadLe32() != kVersion) return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    const size_t s2t = header.ReadLe32();
    const size_t t2s = header.ReadLe32();
    const auto payload = bytes.subspan(kHeaderBytes);
    const size_t max_records = payload.size() / kRecordBytes;
    if (s2t == 0 || s2t > max_records || t2s == 0 || t2s > max_records - s2t)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    const auto text_bytes = payload.subspan((s2t + t2s) * kRecordBytes);
    if (text_bytes.empty() || text_bytes.size() % sizeof(wchar_t) != 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (codec::ComputeFnv1a64(bytes.subspan(8)) != kContentHash) return HRESULT_FROM_WIN32(ERROR_CRC);
    const auto pool = TextPool(text_bytes);
    if (!ValidRows(payload.first(s2t * kRecordBytes), pool) ||
        !ValidRows(payload.subspan(s2t * kRecordBytes, t2s * kRecordBytes), pool))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    mapping_.Reset();
    mapping_ = std::move(mapped);
    s2t_count_ = s2t;
    t2s_count_ = t2s;
    return S_OK;
}

HRESULT ConversionIndex::OpenBundled() noexcept {
    std::filesystem::path path;
    const HRESULT resolved = ResolveSharedRuntimeDataPath(L"conv.idx", path);
    return SUCCEEDED(resolved) ? Open(path) : resolved;
}

std::wstring ConversionIndex::Convert(std::wstring_view text, OutputConversion mode) const {
    if (mode == OutputConversion::Off) return std::wstring(text);
    const auto bytes = mapping_.Bytes();
    Expects(!bytes.empty());
    const auto payload = bytes.subspan(kHeaderBytes);
    const auto pool = TextPool(payload.subspan((s2t_count_ + t2s_count_) * kRecordBytes));
    const auto records = mode == OutputConversion::SimplifiedToTraditional
        ? payload.first(s2t_count_ * kRecordBytes)
        : payload.subspan(s2t_count_ * kRecordBytes, t2s_count_ * kRecordBytes);
    std::wstring output;
    output.reserve(text.size());
    while (!text.empty()) {
        size_t matched = std::min(text.size(), kMaxTextUnits);
        for (; matched != 0; --matched) {
            const auto replacement = FindReplacement(records, pool, text.substr(0, matched));
            if (!replacement.empty()) {
                output.append(replacement);
                break;
            }
        }
        if (matched == 0) {
            matched = dictdetail::LeadingUtf16CodepointUnits(text);
            output.append(text.substr(0, matched));
        }
        text.remove_prefix(matched);
    }
    return output;
}

} // namespace qiqi
