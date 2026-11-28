// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/pinyin.h"
#include "common/dictcodec.h"
#include "ime/engine/engine.h"

#include <new>
#include <utility>

namespace qiqi {
namespace {

constexpr uint32_t kMagic = 0x49595051u; // QPYI
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderBytes = 16;
constexpr size_t kRecordBytes = 8;
constexpr size_t kMaxBytes = 1u * 1024u * 1024u;
constexpr uint64_t kPayloadHash = 0x8AB99B4A47398B83ull;

constexpr uint32_t PackSyllable(std::wstring_view syllable) noexcept {
    if (syllable.size() > PinyinIndex::kMaxSyllableLength) return 0;
    uint32_t key = 0;
    for (wchar_t ch : syllable) {
        if (ch >= L'A' && ch <= L'Z') ch = gsl::narrow_cast<wchar_t>(ch - L'A' + L'a');
        if (ch < L'a' || ch > L'z') return 0;
        key = (key << 5) | gsl::narrow_cast<uint32_t>(ch - L'a' + 1);
    }
    return key;
}

std::wstring ScalarText(uint32_t value) {
    if (value <= 0xFFFFu) return std::wstring(1, gsl::narrow_cast<wchar_t>(value));
    value -= 0x10000u;
    return {gsl::narrow_cast<wchar_t>(0xD800u + (value >> 10)),
        gsl::narrow_cast<wchar_t>(0xDC00u + (value & 0x3FFu))};
}

} // namespace

HRESULT PinyinIndex::Open(const std::filesystem::path& path) noexcept {
    ReadOnlyFileMapping mapped;
    const HRESULT opened = mapped.Open(path, kHeaderBytes + kRecordBytes, kMaxBytes);
    if (FAILED(opened)) return opened;
    const auto bytes = mapped.Bytes();
    codec::BinarySpanReader header(bytes.first(kHeaderBytes));
    if (header.ReadLe32() != kMagic) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (header.ReadLe32() != kVersion) return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    const auto payload = bytes.subspan(kHeaderBytes);
    const uint32_t records = header.ReadLe32();
    if (header.ReadLe32() != 0 || payload.size() % kRecordBytes != 0 ||
        records != payload.size() / kRecordBytes) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (codec::ComputeFnv1a64(payload) != kPayloadHash) return HRESULT_FROM_WIN32(ERROR_CRC);
    mapping_.Reset();
    mapping_ = std::move(mapped);
    return S_OK;
}

HRESULT PinyinIndex::OpenBundled() noexcept {
    std::filesystem::path path;
    const HRESULT resolved = ResolveSharedRuntimeDataPath(L"pinyin.idx", path);
    return SUCCEEDED(resolved) ? Open(path) : resolved;
}

std::vector<std::wstring> PinyinIndex::Lookup(std::wstring_view syllable) const {
    const auto bytes = mapping_.Bytes();
    const uint32_t key = PackSyllable(syllable);
    if (bytes.empty() || key == 0) return {};
    const auto records = bytes.subspan(kHeaderBytes);
    const size_t size = records.size() / kRecordBytes;
    const auto read_row = [records](size_t index) noexcept {
        return codec::BinarySpanReader(records.subspan(index * kRecordBytes, kRecordBytes));
    };
    size_t first = 0;
    size_t last = size;
    while (first < last) {
        const size_t middle = first + (last - first) / 2;
        if (read_row(middle).ReadLe32() < key) first = middle + 1;
        else last = middle;
    }
    std::vector<std::wstring> candidates;
    for (; first < size; ++first) {
        auto row = read_row(first);
        if (row.ReadLe32() != key) break;
        candidates.push_back(ScalarText(row.ReadLe32()));
    }
    return candidates;
}

HRESULT WubiEngine::SetZPinyinLookup(bool enabled, const std::filesystem::path& index_path) noexcept {
    if (enabled && (!pinyin_index_ || !index_path.empty())) {
        try {
            auto index = std::make_shared<PinyinIndex>();
            const HRESULT result = index_path.empty() ? index->OpenBundled() : index->Open(index_path);
            if (FAILED(result)) return result;
            pinyin_index_ = std::move(index);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        }
    }
    enable_z_pinyin_lookup_ = enabled;
    return S_OK;
}

void WubiEngine::UpdatePinyinCandidates() {
    std::wstring_view syllable(raw_code_);
    syllable.remove_prefix(1);
    all_candidates_ = pinyin_index_->Lookup(syllable);
    all_display_candidates_.clear();
    all_display_candidates_.reserve(all_candidates_.size());
    for (const auto& candidate : all_candidates_) {
        const auto code = LookupCharacterCode(candidate);
        all_display_candidates_.push_back(candidate + L"(" + (code.empty() ? L"-" : code) + L")");
    }
    current_page_ = 0;
    selected_in_page_ = 0;
}

WubiEngine::TransitionOutcome WubiEngine::HandleStartPinyinLookup(const KeyInputContext& /*ctx*/) {
    raw_code_ = L"z";
    UpdatePinyinCandidates();
    return {EngineState::PinyinLookup, MakeCurrentStateResult(KeyActionType::UpdateState)};
}

WubiEngine::TransitionOutcome WubiEngine::HandleAppendPinyinLetter(const KeyInputContext& ctx) {
    // Retain one excess letter so an overlong input cannot select a truncated syllable.
    if (raw_code_.size() < PinyinIndex::kMaxSyllableLength + 2) {
        raw_code_.push_back(gsl::narrow_cast<wchar_t>(ctx.vk - 'A' + L'a'));
        UpdatePinyinCandidates();
    }
    return {state_, MakeCurrentStateResult(KeyActionType::UpdateState)};
}

WubiEngine::TransitionOutcome WubiEngine::HandlePopPinyinLetter(const KeyInputContext& ctx) {
    if (raw_code_.size() <= 1) return HandleCancelComposition(ctx);
    raw_code_.pop_back();
    UpdatePinyinCandidates();
    return {state_, MakeCurrentStateResult(KeyActionType::UpdateState)};
}

} // namespace qiqi
