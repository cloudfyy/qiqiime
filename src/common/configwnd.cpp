// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configwnd.h"
#include "common/configclient.h"
#include "common/guids.h"

#include <wrl/implements.h>
#include <atomic>

namespace qiqi {
namespace {

using Microsoft::WRL::ComPtr;

void PostConfigWindowMessage(HWND window, UINT message, WPARAM result = 0, LPARAM request = 0) noexcept {
    if (!::PostMessageW(window, message, result, request)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_INVALID_WINDOW_HANDLE) {
            QIQI_LOG_WARN("Tool config notification post failed (error={})", error);
        }
    }
}

class ConfigWindowSink final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, ITfCompartmentEventSink> {
public:
    explicit ConfigWindowSink(HWND window) noexcept : window_(window) {}

    STDMETHODIMP OnChange(REFGUID guid) noexcept override {
        if (IsEqualGUID(guid, kGuidCompartmentConfigVersion)) {
            PostConfigWindowMessage(window_, kToolConfigChangedMessage);
        }
        return S_OK;
    }

private:
    HWND window_;
};

} // namespace

HRESULT ToolConfigSubscription::Subscribe(HWND window) noexcept {
    compartment_.Reset();
    thread_mgr_.Reset();
    request_ = 0;
    refresh_pending_ = false;
    if (HasConfigRegistrySubKeyOverride()) return S_FALSE;

    ComPtr<ITfThreadMgr> thread_mgr;
    HRESULT result = ::CoCreateInstance(
        CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr));
    if (FAILED(result)) return result;
    if (!thread_mgr) return E_UNEXPECTED;
    ComPtr<ITfCompartmentMgr> manager;
    result = thread_mgr->GetGlobalCompartment(&manager);
    if (FAILED(result)) return result;
    if (!manager) return E_UNEXPECTED;
    const auto sink = Microsoft::WRL::Make<ConfigWindowSink>(window);
    if (!sink) return E_OUTOFMEMORY;
    if (!compartment_.Bind(manager.Get(), kGuidCompartmentConfigVersion, sink.Get())) return E_FAIL;
    thread_mgr_ = std::move(thread_mgr);
    return S_OK;
}

void ToolConfigSubscription::RequestRefresh(HWND window) noexcept {
    if (!thread_mgr_) return;
    if (request_ != 0) {
        refresh_pending_ = true;
        return;
    }
    static std::atomic<LPARAM> last_request{0};
    const LPARAM request = ++last_request;
    try {
        const HRESULT result = config::ReadSnapshotAsync([window, request](HRESULT hr, const config::Snapshot&) {
            PostConfigWindowMessage(window, kToolConfigLoadedMessage, config::EncodeAsyncResult(hr), request);
        });
        if (log::CheckHResult(result, "Tool::ReadSnapshotAsync")) request_ = request;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Tool config refresh callback allocation failed");
    }
}

bool ToolConfigSubscription::CompleteRefresh(HWND window, WPARAM result, LPARAM request) noexcept {
    if (request_ == 0 || request != request_) return false;
    request_ = 0;
    if (std::exchange(refresh_pending_, false)) RequestRefresh(window);
    return log::CheckHResult(config::DecodeAsyncResult(result), "Tool::ConfigRefresh");
}

} // namespace qiqi
