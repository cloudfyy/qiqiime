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

    BrokerObject(
        const BrokerObject&) =
        delete;
#pragma warning(suppress : 26456)
    BrokerObject& operator=(
        const BrokerObject&) =
        delete;
    BrokerObject(
        BrokerObject&&) =
        delete;
#pragma warning(suppress : 26456)
    BrokerObject& operator=(
        BrokerObject&&) =
        delete;

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
        QIQI_CONFIG_TRACE_CONTEXT*
            trace_context,
        ULONG* snapshot_size,
        byte** snapshot,
        QIQI_CONFIG_TRACE_RESULT*
            trace_result) noexcept override {
        if (!snapshot_size ||
            !snapshot ||
            !trace_context ||
            !trace_result) {
            return E_POINTER;
        }
        *snapshot_size = 0;
        *snapshot = nullptr;
        trace::OperationSpan operation(
            "ConfigBroker",
            "read",
            trace::Endpoint::Broker,
            known_revision,
            *trace_context,
            *trace_result);
        if (!operation.Valid()) {
            return operation.Finish(
                E_INVALIDARG);
        }
        HRESULT result =
            ValidateCaller(
                operation,
                CallerAccess::Read);
        if (FAILED(result)) {
            return operation.Finish(
                result);
        }
        operation.GetResult().revision =
            known_revision;
        std::vector<uint8_t> payload;
        result = runtime_.Read(
            known_revision,
            payload,
            operation.GetResult());
        if (result == S_FALSE) {
            return operation.Finish(
                result);
        }
        if (SUCCEEDED(result)) {
            result =
                config::AllocateRpcBuffer(
                    payload,
                    *snapshot_size,
                    *snapshot);
        }
        return operation.Finish(
            result);
    }

    STDMETHODIMP Save(
        ULONG request_size,
        byte* request,
        QIQI_CONFIG_TRACE_CONTEXT*
            trace_context,
        ULONG* snapshot_size,
        byte** snapshot,
        QIQI_CONFIG_TRACE_RESULT*
            trace_result) noexcept override {
        if (!snapshot_size ||
            !snapshot ||
            !trace_context ||
            !trace_result) {
            return E_POINTER;
        }
        *snapshot_size = 0;
        *snapshot = nullptr;
        config::SaveRequest decoded;
        trace::OperationSpan operation(
            "ConfigBroker",
            "save",
            trace::Endpoint::Broker,
            decoded.expected_revision,
            *trace_context,
            *trace_result);
        if (!operation.Valid()) {
            return operation.Finish(
                E_INVALIDARG);
        }
        HRESULT result =
            ValidateCaller(
                operation,
                CallerAccess::Mutation);
        if (FAILED(result)) {
            return operation.Finish(
                result);
        }
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
        if (SUCCEEDED(result)) {
            operation.GetResult().revision =
                decoded.expected_revision;
        }
        std::vector<uint8_t> payload;
        if (SUCCEEDED(result)) {
            result = runtime_.Save(
                decoded,
                operation.GetContext(),
                payload,
                operation.GetResult());
        }
        if (SUCCEEDED(result)) {
            result =
                config::AllocateRpcBuffer(
                    payload,
                    *snapshot_size,
                    *snapshot);
        }
        return operation.Finish(
            result);
    }

    STDMETHODIMP Reset(
        ULONGLONG expected_revision,
        QIQI_CONFIG_TRACE_CONTEXT*
            trace_context,
        ULONG* snapshot_size,
        byte** snapshot,
        QIQI_CONFIG_TRACE_RESULT*
            trace_result) noexcept override {
        if (!snapshot_size ||
            !snapshot ||
            !trace_context ||
            !trace_result) {
            return E_POINTER;
        }
        *snapshot_size = 0;
        *snapshot = nullptr;
        trace::OperationSpan operation(
            "ConfigBroker",
            "reset",
            trace::Endpoint::Broker,
            expected_revision,
            *trace_context,
            *trace_result);
        if (!operation.Valid()) {
            return operation.Finish(
                E_INVALIDARG);
        }
        HRESULT result =
            ValidateCaller(
                operation,
                CallerAccess::Mutation);
        if (FAILED(result)) {
            return operation.Finish(
                result);
        }
        operation.GetResult().revision =
            expected_revision;
        std::vector<uint8_t> payload;
        result = runtime_.Reset(
            expected_revision,
            operation.GetContext(),
            payload,
            operation.GetResult());
        if (SUCCEEDED(result)) {
            result =
                config::AllocateRpcBuffer(
                    payload,
                    *snapshot_size,
                    *snapshot);
        }
        return operation.Finish(
            result);
    }

    STDMETHODIMP PublishSyncDirectory(
        ULONG request_size,
        byte* request,
        QIQI_CONFIG_TRACE_CONTEXT*
            trace_context,
        ULONG* snapshot_size,
        byte** snapshot,
        QIQI_CONFIG_TRACE_RESULT*
            trace_result) noexcept override {
        if (!snapshot_size ||
            !snapshot ||
            !trace_context ||
            !trace_result) {
            return E_POINTER;
        }
        *snapshot_size = 0;
        *snapshot = nullptr;
        config::SyncDirectoryRequest
            decoded;
        trace::OperationSpan operation(
            "ConfigBroker",
            "publish-sync-directory",
            trace::Endpoint::Broker,
            decoded.expected_revision,
            *trace_context,
            *trace_result);
        if (!operation.Valid()) {
            return operation.Finish(
                E_INVALIDARG);
        }
        HRESULT result =
            ValidateCaller(
                operation,
                CallerAccess::Mutation);
        if (FAILED(result)) {
            return operation.Finish(
                result);
        }
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
        if (SUCCEEDED(result)) {
            operation.GetResult().revision =
                decoded.expected_revision;
        }
        std::vector<uint8_t> payload;
        if (SUCCEEDED(result)) {
            result =
                runtime_.PublishSyncDirectory(
                    decoded,
                    operation.GetContext(),
                    payload,
                    operation.GetResult());
        }
        if (SUCCEEDED(result)) {
            result =
                config::AllocateRpcBuffer(
                    payload,
                    *snapshot_size,
                    *snapshot);
        }
        return operation.Finish(
            result);
    }

private:
    HRESULT ValidateCaller(
        const trace::OperationSpan&
            operation,
        CallerAccess access)
        const noexcept {
        const HRESULT result =
            runtime_.ValidateCaller(
                access);
        if (FAILED(result)) {
            QIQI_LOG_CTX(
                log::Level::warn,
                (log::LogContext{
                    log::Keyword::Config,
                    log::Opcode::Info,
                    &operation
                         .GetResult()
                         .broker_activity_id,
                    &operation
                         .GetContext()
                         .caller_activity_id,
                    &operation
                         .GetContext()
                         .trace_id}),
                "ConfigBroker rejected caller "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    result));
        }
        return result;
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
