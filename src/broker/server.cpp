// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/server.h"

#include "common/comobj.h"
#include "common/configrpc.h"
#include "common/configwire.h"
#include "common/logger.h"

#include <wrl/client.h>
#include <gsl/gsl>
#include <new>
#include <string>
#include <utility>

namespace qiqi::broker {
namespace {

using Microsoft::WRL::ComPtr;

class BrokerObject final
    : public ComObjectBase<
          BrokerObject,
          IQiQiConfigBroker> {
public:
    using Base =
        ComObjectBase<
            BrokerObject,
            IQiQiConfigBroker>;

    explicit BrokerObject(
        Runtime& runtime) noexcept
        : runtime_(runtime) {
        runtime_.AddObject();
    }

    ~BrokerObject() noexcept {
        runtime_.RemoveObject();
    }

    STDMETHODIMP QueryInterface(
        REFIID iid,
        void** object) noexcept override {
        const HRESULT result =
            Base::QueryInterface(
                iid,
                object);
        QIQI_LOG_CAT_DEBUG(
            log::Keyword::Config,
            "BrokerObject::QueryInterface "
            "(sync={}, "
            "hr=0x{:08X})",
            IsEqualIID(
                iid,
                __uuidof(
                    IQiQiConfigBroker)),
            gsl::narrow_cast<unsigned long>(
                result));
        return result;
    }

    STDMETHODIMP Read(
        ULONGLONG known_revision,
        ULONG* snapshot_size,
        byte** snapshot) noexcept override {
        if (!InitializeOutput(
                snapshot_size,
                snapshot)) {
            return E_POINTER;
        }
        HRESULT result =
            ValidateCaller();
        if (FAILED(result)) {
            return result;
        }
        std::vector<uint8_t> payload;
        result = runtime_.Read(
            known_revision,
            payload);
        LogResult(
            "read",
            result,
            known_revision);
        if (result == S_FALSE) {
            return result;
        }
        return SUCCEEDED(result)
            ? config::AllocateRpcBuffer(
                  payload,
                  *snapshot_size,
                  *snapshot)
            : result;
    }

    STDMETHODIMP Save(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size,
        byte** snapshot) noexcept override {
        if (!InitializeOutput(
                snapshot_size,
                snapshot)) {
            return E_POINTER;
        }
        HRESULT result =
            ValidateCaller();
        if (FAILED(result)) {
            return result;
        }
        config::SaveRequest decoded;
        std::vector<uint8_t> bytes;
        result = config::ReadRpcBuffer(
            request_size,
            request,
            bytes);
        if (SUCCEEDED(result)) {
            result = config::DecodeSaveRequest(
                bytes,
                decoded);
        }
        std::vector<uint8_t> payload;
        if (SUCCEEDED(result)) {
            result = runtime_.Save(
                decoded,
                payload);
        }
        LogResult(
            "save",
            result,
            decoded.expected_revision);
        return SUCCEEDED(result)
            ? config::AllocateRpcBuffer(
                  payload,
                  *snapshot_size,
                  *snapshot)
            : result;
    }

    STDMETHODIMP Reset(
        ULONGLONG expected_revision,
        ULONG* snapshot_size,
        byte** snapshot) noexcept override {
        if (!InitializeOutput(
                snapshot_size,
                snapshot)) {
            return E_POINTER;
        }
        HRESULT result =
            ValidateCaller();
        if (FAILED(result)) {
            return result;
        }
        std::vector<uint8_t> payload;
        result = runtime_.Reset(
            expected_revision,
            payload);
        LogResult(
            "reset",
            result,
            expected_revision);
        return SUCCEEDED(result)
            ? config::AllocateRpcBuffer(
                  payload,
                  *snapshot_size,
                  *snapshot)
            : result;
    }

    STDMETHODIMP PublishSyncDirectory(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size,
        byte** snapshot) noexcept override {
        if (!InitializeOutput(
                snapshot_size,
                snapshot)) {
            return E_POINTER;
        }
        HRESULT result =
            ValidateCaller();
        if (FAILED(result)) {
            return result;
        }
        config::SyncDirectoryRequest decoded;
        std::vector<uint8_t> bytes;
        result = config::ReadRpcBuffer(
            request_size,
            request,
            bytes);
        if (SUCCEEDED(result)) {
            result =
                config::DecodeSyncDirectoryRequest(
                    bytes,
                    decoded);
        }
        std::vector<uint8_t> payload;
        if (SUCCEEDED(result)) {
            result =
                runtime_.PublishSyncDirectory(
                    decoded,
                    payload);
        }
        LogResult(
            "publish-sync-directory",
            result,
            decoded.expected_revision);
        return SUCCEEDED(result)
            ? config::AllocateRpcBuffer(
                  payload,
                  *snapshot_size,
                  *snapshot)
            : result;
    }

private:
    static bool InitializeOutput(
        ULONG* size,
        byte** bytes) noexcept {
        if (!size || !bytes) {
            return false;
        }
        *size = 0;
        *bytes = nullptr;
        return true;
    }

    HRESULT ValidateCaller() const noexcept {
        const HRESULT result =
            runtime_.ValidateCaller();
        if (FAILED(result)) {
            QIQI_LOG_CAT_WARN(
                log::Keyword::Config,
                "ConfigBroker rejected caller "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    result));
        }
        return result;
    }

    static void LogResult(
        std::string_view operation,
        HRESULT result,
        uint64_t revision) noexcept {
        const auto level =
            FAILED(result) &&
                    result != S_FALSE
                ? log::Level::err
                : log::Level::debug;
        QIQI_LOG_CTX(
            level,
            (log::LogContext{
                log::Keyword::Config}),
            "ConfigBroker operation={} "
            "hr=0x{:08X} revision={}",
            operation,
            gsl::narrow_cast<unsigned long>(
                result),
            revision);
    }

    Runtime& runtime_;
};

class BrokerFactory final
    : public ComObjectBase<
          BrokerFactory,
          IClassFactory> {
public:
    explicit BrokerFactory(
        Runtime& runtime) noexcept
        : runtime_(runtime) {
    }

    STDMETHODIMP CreateInstance(
        IUnknown* outer,
        REFIID iid,
        void** object) noexcept override {
        if (!object) {
            return E_POINTER;
        }
        *object = nullptr;
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        QIQI_LOG_CAT_DEBUG(
            log::Keyword::Config,
            "ClassFactory::CreateInstance "
            "called");
        ComPtr<BrokerObject> instance;
        instance.Attach(new (
            std::nothrow) BrokerObject(
                runtime_));
        if (!instance) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result =
            instance->QueryInterface(
            iid,
            object);
        QIQI_LOG_CAT_DEBUG(
            log::Keyword::Config,
            "ClassFactory::CreateInstance "
            "completed (hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                result));
        return result;
    }

    STDMETHODIMP LockServer(
        BOOL lock) noexcept override {
        if (lock) {
            runtime_.LockServer();
        } else {
            runtime_.UnlockServer();
        }
        return S_OK;
    }

private:
    Runtime& runtime_;
};

} // namespace

ComPtr<IClassFactory> CreateConfigBrokerFactory(
    Runtime& runtime) noexcept {
    ComPtr<BrokerFactory> factory;
    factory.Attach(new (
        std::nothrow) BrokerFactory(
            runtime));
    if (!factory) {
        return {};
    }
    ComPtr<IClassFactory> output;
    if (FAILED(factory.As(&output))) {
        return {};
    }
    return output;
}

} // namespace qiqi::broker
