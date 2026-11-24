// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searchprobe.h"

#include "common/guids.h"

#include <ctffunc.h>
#include <msctf.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <format>
#include <memory>
#include <tuple>

namespace qiqi::host::fulltrust {
namespace {

constexpr std::wstring_view kQuery = L"wq";

struct BstrCloser {
    void operator()(BSTR value) const noexcept {
        if (value) {
            ::SysFreeString(value);
        }
    }
};

class ComInitializer final {
public:
    explicit ComInitializer(HRESULT hr) noexcept
        : hr_(hr),
          initialized_(SUCCEEDED(hr)) {}

    ~ComInitializer() noexcept {
        if (initialized_) {
            ::CoUninitialize();
        }
    }

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    HRESULT Get() const noexcept {
        return hr_;
    }

private:
    HRESULT hr_ = S_OK;
    bool initialized_ = false;
};

class ThreadManagerGuard final {
public:
    ThreadManagerGuard(
        ITfThreadMgr* manager,
        TfClientId client_id) noexcept
        : manager_(manager),
          client_id_(client_id) {}

    ~ThreadManagerGuard() noexcept {
        if (manager_ && client_id_ != TF_CLIENTID_NULL) {
            std::ignore = manager_->Deactivate();
        }
    }

    ThreadManagerGuard(const ThreadManagerGuard&) = delete;
    ThreadManagerGuard& operator=(const ThreadManagerGuard&) = delete;

private:
    ITfThreadMgr* manager_ = nullptr;
    TfClientId client_id_ = TF_CLIENTID_NULL;
};

std::wstring DisplayHResult(HRESULT value) {
    return std::format(L"0x{:08X}", static_cast<uint32_t>(value));
}

} // namespace

SearchProbeResult ProbeSearchCandidateProvider() noexcept {
    SearchProbeResult result;
    const ComInitializer com(
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    result.coinitialize_hr = com.Get();
    if (FAILED(result.coinitialize_hr)) {
        return result;
    }

    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_manager;
    const HRESULT create_hr = ::CoCreateInstance(
        CLSID_TF_ThreadMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&thread_manager));
    if (FAILED(create_hr) || !thread_manager) {
        result.activate_hr = FAILED(create_hr) ? create_hr : E_NOINTERFACE;
        return result;
    }

    TfClientId client_id = TF_CLIENTID_NULL;
    result.activate_hr = thread_manager->Activate(&client_id);
    if (FAILED(result.activate_hr) ||
        client_id == TF_CLIENTID_NULL) {
        if (SUCCEEDED(result.activate_hr)) {
            result.activate_hr = E_UNEXPECTED;
        }
        return result;
    }
    const ThreadManagerGuard manager_guard(
        thread_manager.Get(),
        client_id);

    Microsoft::WRL::ComPtr<ITfFunctionProvider> function_provider;
    result.provider_hr = thread_manager->GetFunctionProvider(
        qiqi::kClsidQiQiTextService,
        &function_provider);
    if (FAILED(result.provider_hr) || !function_provider) {
        if (SUCCEEDED(result.provider_hr)) {
            result.provider_hr = E_NOINTERFACE;
        }
        return result;
    }
    result.provider_available = true;

    Microsoft::WRL::ComPtr<IUnknown> search_unknown;
    result.function_hr = function_provider->GetFunction(
        GUID_NULL,
        IID_ITfFnSearchCandidateProvider,
        &search_unknown);
    if (FAILED(result.function_hr) || !search_unknown) {
        if (SUCCEEDED(result.function_hr)) {
            result.function_hr = E_NOINTERFACE;
        }
        return result;
    }

    Microsoft::WRL::ComPtr<ITfFnSearchCandidateProvider> search_provider;
    result.function_hr = search_unknown.As(&search_provider);
    if (FAILED(result.function_hr) || !search_provider) {
        if (SUCCEEDED(result.function_hr)) {
            result.function_hr = E_NOINTERFACE;
        }
        return result;
    }

    std::unique_ptr<OLECHAR, BstrCloser> query(
        ::SysAllocStringLen(kQuery.data(), static_cast<UINT>(kQuery.size())));
    std::unique_ptr<OLECHAR, BstrCloser> application_id(
        ::SysAllocString(L"QiQiImePackagedFullTrustHost"));
    if (!query || !application_id) {
        result.candidates_hr = E_OUTOFMEMORY;
        return result;
    }

    Microsoft::WRL::ComPtr<ITfCandidateList> candidates;
    result.candidates_hr = search_provider->GetSearchCandidates(
        query.get(),
        application_id.get(),
        &candidates);
    if (result.candidates_hr != S_OK || !candidates) {
        return result;
    }
    result.candidates_hr = candidates->GetCandidateNum(
        &result.candidate_count);
    return result;
}

std::wstring FormatSearchProbe(
    const SearchProbeResult& result) {
    return std::format(
        L"COM initialize: {}\r\n"
        L"ITfThreadMgr Activate: {}\r\n"
        L"QiQi function provider: {} (available={})\r\n"
        L"ITfFnSearchCandidateProvider: {}\r\n"
        L"GetSearchCandidates: {} (count={})",
        DisplayHResult(result.coinitialize_hr),
        DisplayHResult(result.activate_hr),
        DisplayHResult(result.provider_hr),
        result.provider_available ? L"true" : L"false",
        DisplayHResult(result.function_hr),
        DisplayHResult(result.candidates_hr),
        result.candidate_count);
}

} // namespace qiqi::host::fulltrust

