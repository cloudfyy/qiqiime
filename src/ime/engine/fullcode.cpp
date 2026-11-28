// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/fullcode.h"
#include "ime/engine/dictdetail.h"
#include "common/dictcodec.h"

#include <utility>

namespace qiqi {
namespace {

constexpr std::wstring_view kIndexName = L"full.idx";
constexpr uint32_t kMagic = 0x58444951u; // QIDX
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kRecordSize = 8;
constexpr size_t kMaxIndexSize = 2u * 1024u * 1024u;
// Pins the authoritative data, not a checksum that an arbitrary replacement supplies.
constexpr uint64_t kPayloadHash = 0xC0696CC5E7CAD601ull;

} // namespace

HRESULT FullCodeIndex::Open(const std::filesystem::path& path) noexcept {
    ReadOnlyFileMapping mapped;
    const HRESULT opened = mapped.Open(path, kHeaderSize + kRecordSize, kMaxIndexSize);
    if (FAILED(opened)) return opened;

    const auto bytes = mapped.Bytes();
    codec::BinarySpanReader header(bytes.first(kHeaderSize));
    if (header.ReadLe32() != kMagic) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (header.ReadLe32() != kVersion) return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    const auto payload = bytes.subspan(kHeaderSize);
    const uint32_t count = header.ReadLe32();
    if (header.ReadLe32() != 0 || payload.size() % kRecordSize != 0 ||
        count != payload.size() / kRecordSize) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (codec::ComputeFnv1a64(payload) != kPayloadHash) return HRESULT_FROM_WIN32(ERROR_CRC);
    mapping_.Reset();
    mapping_ = std::move(mapped);
    return S_OK;
}

HRESULT FullCodeIndex::OpenBundled() noexcept {
    std::filesystem::path path;
    const HRESULT resolved = ResolveSharedRuntimeDataPath(kIndexName, path);
    return SUCCEEDED(resolved) ? Open(path) : resolved;
}

uint32_t FullCodeIndex::CharacterCode(std::wstring_view character) const noexcept {
    const uint32_t scalar = dictdetail::SingleUtf16Scalar(character);
    const auto bytes = mapping_.Bytes();
    if (scalar == 0 || bytes.empty()) return 0;
    const auto records = bytes.subspan(kHeaderSize);
    size_t first = 0;
    size_t count = records.size() / kRecordSize;
    while (count != 0) {
        const size_t half = count / 2;
        const size_t middle = first + half;
        codec::BinarySpanReader row(records.subspan(middle * kRecordSize, kRecordSize));
        const uint32_t code = row.ReadLe32();
        const uint32_t current = row.ReadLe32();
        if (current == scalar) return code;
        if (current < scalar) {
            first = middle + 1;
            count -= half + 1;
        } else {
            count = half;
        }
    }
    return 0;
}

} // namespace qiqi
