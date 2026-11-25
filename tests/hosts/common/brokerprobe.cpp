// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "brokerprobe.h"

#include "common/configrpc.h"
#include "common/guids.h"

#include <wrl/client.h>

namespace qiqi::host::broker {
namespace {

using Microsoft::WRL::ComPtr;

class ComInitializer final {
public:
    explicit ComInitializer(
        HRESULT result) noexcept
        : result_(result) {
    }

    ~ComInitializer() noexcept {
        if (SUCCEEDED(result_)) {
            ::CoUninitialize();
        }
    }

    ComInitializer(
        const ComInitializer&) = delete;
    ComInitializer& operator=(
        const ComInitializer&) = delete;

    HRESULT Result() const noexcept {
        return result_;
    }

    bool Available() const noexcept {
        return SUCCEEDED(result_) ||
            result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_;
};

class RpcBuffer final {
public:
    explicit RpcBuffer(
        byte* value) noexcept
        : value_(value) {
    }

    ~RpcBuffer() noexcept {
        ::CoTaskMemFree(value_);
    }

    RpcBuffer(const RpcBuffer&) = delete;
    RpcBuffer& operator=(
        const RpcBuffer&) = delete;

private:
    byte* value_ = nullptr;
};

} // namespace

ProbeResult ProbeConfiguration() noexcept {
    ProbeResult result;
    const ComInitializer com(
        ::CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED));
    result.coinitialize = com.Result();
    if (!com.Available()) {
        return result;
    }

    ComPtr<IQiQiConfigBroker> proxy;
    result.create =
        ::CoCreateInstance(
            qiqi::kClsidQiQiConfigBroker,
            nullptr,
            CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(&proxy));
    if (FAILED(result.create) ||
        !proxy) {
        if (SUCCEEDED(result.create)) {
            result.create = E_UNEXPECTED;
        }
        return result;
    }

#if WINAPI_FAMILY_PARTITION( \
    WINAPI_PARTITION_DESKTOP | \
    WINAPI_PARTITION_SYSTEM)
    result.blanket =
        ::CoSetProxyBlanket(
            proxy.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_DEFAULT,
            nullptr,
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE);
    if (FAILED(result.blanket)) {
        return result;
    }
#else
    result.blanket = E_NOTIMPL;
#endif

    QIQI_CONFIG_TRACE_CONTEXT trace_context{};
    QIQI_CONFIG_TRACE_RESULT trace_result{};
    ULONG snapshot_size = 0;
    byte* snapshot = nullptr;
    result.read =
        proxy->Read(
            0,
            &trace_context,
            &snapshot_size,
            &snapshot,
            &trace_result);
    const RpcBuffer snapshot_owner(snapshot);
    result.snapshot_size = snapshot_size;
    result.revision = trace_result.revision;
    return result;
}

} // namespace qiqi::host::broker
