// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candsearch.h"
#include "common/comobj.h"
#include "common/logger.h"
#include <gsl/gsl>
#include <algorithm>
#include <new>
#include <span>
#include <utility>

namespace qiqi {
namespace {

using CandidateSnapshot =
    std::shared_ptr<const std::vector<std::wstring>>;

class SearchCandidateString final
    : public ComObjectBase<SearchCandidateString, ITfCandidateString> {
public:
    SearchCandidateString(
        CandidateSnapshot candidates, ULONG index) noexcept
        : candidates_(std::move(candidates)), index_(index) {}

    STDMETHODIMP GetString(BSTR* value) noexcept override {
        if (!value) return E_INVALIDARG;
        *value = nullptr;
        if (!candidates_ ||
            index_ >= gsl::narrow_cast<ULONG>(candidates_->size())) {
            return E_UNEXPECTED;
        }
        return CopyToBstr(gsl::at(*candidates_, index_), value);
    }

    STDMETHODIMP GetIndex(ULONG* index) noexcept override {
        if (!index) return E_INVALIDARG;
        *index = index_;
        return S_OK;
    }

private:
    const CandidateSnapshot candidates_;
    const ULONG index_;
};

class SearchCandidateEnumerator final
    : public ComObjectBase<SearchCandidateEnumerator, IEnumTfCandidates> {
public:
    SearchCandidateEnumerator(
        CandidateSnapshot candidates, size_t position = 0) noexcept
        : candidates_(std::move(candidates)), position_(position) {}

    STDMETHODIMP Clone(IEnumTfCandidates** output) noexcept override {
        if (!output) return E_INVALIDARG;
        *output = nullptr;
        return CreateComObjectToOut<SearchCandidateEnumerator>(
            output, candidates_, position_);
    }

    STDMETHODIMP Next(
        ULONG count, ITfCandidateString** output,
        ULONG* fetched) noexcept override {
        if (!fetched) return E_INVALIDARG;
        *fetched = 0;
        if (!output && count != 0) return E_INVALIDARG;
        if (!candidates_ || position_ > candidates_->size()) return E_UNEXPECTED;
        if (count == 0) return S_OK;
        std::span<ITfCandidateString*> results(output, count);
        std::ranges::fill(results, nullptr);
        const size_t available = candidates_->size() - position_;
        const size_t to_fetch = std::min<size_t>(count, available);
        for (size_t i = 0; i < to_fetch; ++i) {
            const ULONG index = gsl::narrow_cast<ULONG>(position_ + i);
            const HRESULT hr = CreateComObjectToOut<SearchCandidateString>(
                &gsl::at(results, i), candidates_, index);
            if (FAILED(hr)) {
                for (size_t released = 0; released < i; ++released) {
                    Microsoft::WRL::ComPtr<ITfCandidateString> cleanup;
                    cleanup.Attach(gsl::at(results, released));
                    gsl::at(results, released) = nullptr;
                }
                return hr;
            }
        }
        position_ += to_fetch;
        *fetched = gsl::narrow_cast<ULONG>(to_fetch);
        return to_fetch == count ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() noexcept override {
        position_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG count) noexcept override {
        if (!candidates_) return E_UNEXPECTED;
        const size_t available = candidates_->size() - position_;
        const size_t to_skip =
            std::min<size_t>(available, gsl::narrow_cast<size_t>(count));
        position_ += to_skip;
        return to_skip == gsl::narrow_cast<size_t>(count) ? S_OK : S_FALSE;
    }

private:
    const CandidateSnapshot candidates_;
    size_t position_;
};

class SearchCandidateList final
    : public ComObjectBase<SearchCandidateList, ITfCandidateList> {
public:
    explicit SearchCandidateList(CandidateSnapshot candidates) noexcept
        : candidates_(std::move(candidates)) {}

    STDMETHODIMP EnumCandidates(IEnumTfCandidates** output) noexcept override {
        if (!output) return E_INVALIDARG;
        *output = nullptr;
        return CreateComObjectToOut<SearchCandidateEnumerator>(
            output, candidates_);
    }

    STDMETHODIMP GetCandidate(
        ULONG index, ITfCandidateString** output) noexcept override {
        if (!output) return E_INVALIDARG;
        *output = nullptr;
        if (!candidates_ ||
            index >= gsl::narrow_cast<ULONG>(candidates_->size())) {
            return E_INVALIDARG;
        }
        return CreateComObjectToOut<SearchCandidateString>(
            output, candidates_, index);
    }

    STDMETHODIMP GetCandidateNum(ULONG* count) noexcept override {
        if (!count) return E_INVALIDARG;
        *count = candidates_
            ? gsl::narrow_cast<ULONG>(candidates_->size())
            : 0;
        return S_OK;
    }

    STDMETHODIMP SetResult(
        ULONG index, TfCandidateResult result) noexcept override {
        if (result == CAND_CANCELED) return S_OK;
        if (result != CAND_SELECTED && result != CAND_FINALIZED) {
            return E_INVALIDARG;
        }
        if (!candidates_ ||
            index >= gsl::narrow_cast<ULONG>(candidates_->size())) {
            return E_INVALIDARG;
        }
        return S_OK;
    }

private:
    const CandidateSnapshot candidates_;
};

} // namespace

HRESULT CreateSearchCandidateList(
    std::vector<std::wstring> candidates,
    ITfCandidateList** output) noexcept {
    if (!output) return E_INVALIDARG;
    *output = nullptr;
    if (candidates.empty()) return S_FALSE;
    try {
        auto snapshot = std::make_shared<const std::vector<std::wstring>>(
            std::move(candidates));
        return CreateComObjectToOut<SearchCandidateList>(
            output, std::move(snapshot));
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CreateSearchCandidateList allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    }
}

} // namespace qiqi
