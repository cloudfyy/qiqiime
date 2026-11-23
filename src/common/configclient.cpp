// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configclient.h"

#include "common/comobj.h"
#include "common/configrpc.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/RAII.h"

#include <wrl/client.h>
#include <gsl/gsl>
#include <memory>
#include <new>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace qiqi::config {
namespace {

using Microsoft::WRL::ComPtr;

static_assert(
    std::is_nothrow_move_assignable_v<
        Snapshot>);

struct CacheState {
    std::shared_mutex mutex;
    Snapshot value;
    bool valid = false;
};

CacheState g_cache;

class ScopedCom final {
public:
    ScopedCom() noexcept
        : result_(::CoInitializeEx(
              nullptr,
              COINIT_MULTITHREADED)),
          owns_initialization_(
              SUCCEEDED(result_)) {
        if (result_ ==
            RPC_E_CHANGED_MODE) {
            result_ = S_OK;
        }
    }

    ~ScopedCom() noexcept {
        if (owns_initialization_) {
            ::CoUninitialize();
        }
    }

    HRESULT Result() const noexcept {
        return result_;
    }

private:
    HRESULT result_ = S_OK;
    bool owns_initialization_ = false;
};

HRESULT SetProxySecurity(
    IUnknown* proxy) noexcept {
    const HRESULT result =
        ::CoSetProxyBlanket(
            proxy,
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_DEFAULT,
            nullptr,
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE);
    std::ignore = log::CheckHResult(
        result,
        "ConfigClient::CoSetProxyBlanket");
    return result;
}

HRESULT CommitSnapshot(
    Snapshot decoded,
    Snapshot& output) noexcept {
    try {
        output = decoded;
        {
            std::unique_lock lock(
                g_cache.mutex);
            if (g_cache.valid &&
                g_cache.value.revision >
                    decoded.revision) {
                Snapshot latest =
                    g_cache.value;
                output = std::move(latest);
                return S_OK;
            }
            g_cache.value =
                std::move(decoded);
            g_cache.valid = true;
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (const std::system_error& error) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Config cache synchronization failed "
            "(error={})",
            error.code().value());
        return SystemErrorToHResult(
            error);
    }
}

HRESULT DecodeAndCommit(
    HRESULT call_result,
    ULONG size,
    byte* raw,
    Snapshot& output) noexcept {
    const ScopedCoTaskMem memory(raw);
    if (call_result == S_FALSE) {
        const HRESULT cached =
            CopyCachedSnapshot(output);
        return cached == S_OK
            ? S_FALSE
            : FAILED(cached)
                ? cached
                : E_UNEXPECTED;
    }
    if (FAILED(call_result)) {
        return call_result;
    }
    if (size == 0 || !raw) {
        return E_UNEXPECTED;
    }
    std::vector<uint8_t> bytes;
    HRESULT result =
        ReadRpcBuffer(size, raw, bytes);
    if (FAILED(result)) {
        return result;
    }
    Snapshot decoded;
    result = DecodeSnapshot(
        bytes,
        decoded);
    if (FAILED(result)) {
        return result;
    }
    return CommitSnapshot(
        std::move(decoded),
        output);
}

enum class Operation {
    Read,
    Save,
    Reset,
    SyncDirectory,
};

struct Request {
    Operation operation = Operation::Read;
    uint64_t known_revision = 0;
    uint64_t expected_revision = 0;
    std::optional<SaveRequest> save;
    std::optional<SyncDirectoryRequest>
        sync_directory;
    AsyncCompletion completion;
};

HRESULT CreateAsyncBroker(
    ComPtr<AsyncIQiQiConfigBroker>& output,
    ComPtr<ISynchronize>& completion)
    noexcept {
    ComPtr<IQiQiConfigBroker> synchronous;
    const HRESULT result =
        ::CoCreateInstance(
            kClsidQiQiConfigBroker,
            nullptr,
            CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(&synchronous));
    if (FAILED(result) ||
        !synchronous) {
        QIQI_LOG_CAT_WARN(
            log::Keyword::Config,
            "ConfigClient::CoCreateInstance "
            "(hr=0x{:08X})",
        gsl::narrow_cast<unsigned long>(
                result));
        return FAILED(result)
            ? result
            : E_UNEXPECTED;
    }
    HRESULT security =
        SetProxySecurity(
            synchronous.Get());
    if (FAILED(security)) {
        return security;
    }
    ComPtr<ICallFactory> call_factory;
    security = synchronous.As(
        &call_factory);
    std::ignore = log::CheckHResult(
        security,
        "ConfigClient::QueryInterface("
        "ICallFactory)");
    if (FAILED(security)) {
        return security;
    }
    ComPtr<IUnknown> call_object;
    IUnknown* raw_call = nullptr;
    security =
        call_factory->CreateCall(
            __uuidof(
                AsyncIQiQiConfigBroker),
            nullptr,
            IID_IUnknown,
            &raw_call);
    std::ignore = log::CheckHResult(
        security,
        "ConfigClient::ICallFactory::"
        "CreateCall");
    if (FAILED(security)) {
        return security;
    }
    call_object.Attach(raw_call);
    security = call_object.As(
        &completion);
    if (FAILED(security)) {
        return security;
    }
    security = call_object.As(&output);
    std::ignore = log::CheckHResult(
        security,
        "ConfigClient::QueryInterface("
        "AsyncIQiQiConfigBroker call)");
    return security;
}

HRESULT EncodeInput(
    const Request& request,
    std::vector<uint8_t>& bytes) noexcept {
    HRESULT result = S_OK;
    switch (request.operation) {
        case Operation::Save:
            result = request.save
                ? EncodeSaveRequest(
                      *request.save,
                      bytes)
                : E_INVALIDARG;
            break;
        case Operation::SyncDirectory:
            result =
                request.sync_directory
                    ? EncodeSyncDirectoryRequest(
                          *request.sync_directory,
                          bytes)
                    : E_INVALIDARG;
            break;
        case Operation::Read:
        case Operation::Reset:
            return S_OK;
    }
    return result;
}

HRESULT ExecuteAsync(
    const Request& request,
    Snapshot& output) noexcept {
    ComPtr<AsyncIQiQiConfigBroker> broker;
    ComPtr<ISynchronize> completion;
    HRESULT result =
        CreateAsyncBroker(
            broker,
            completion);
    if (FAILED(result)) {
        return result;
    }
    std::vector<uint8_t> input;
    result = EncodeInput(
        request,
        input);
    if (FAILED(result)) {
        return result;
    }

    ULONG raw_size = 0;
    switch (request.operation) {
        case Operation::Read:
            result = broker->Begin_Read(
                request.known_revision,
                &raw_size);
            break;
        case Operation::Save:
            result = broker->Begin_Save(
                gsl::narrow_cast<ULONG>(
                    input.size()),
                input.data(),
                &raw_size);
            break;
        case Operation::Reset:
            result = broker->Begin_Reset(
                request.expected_revision,
                &raw_size);
            break;
        case Operation::SyncDirectory:
            result =
                broker->Begin_PublishSyncDirectory(
                    gsl::narrow_cast<ULONG>(
                        input.size()),
                    input.data(),
                    &raw_size);
            break;
    }
    if (FAILED(result)) {
        return result;
    }

    result = completion->Wait(
        0,
        INFINITE);
    if (FAILED(result)) {
        return result;
    }

    byte* raw = nullptr;
    switch (request.operation) {
        case Operation::Read:
            result =
                broker->Finish_Read(
                    &raw_size,
                    &raw);
            break;
        case Operation::Save:
            result =
                broker->Finish_Save(
                    &raw_size,
                    &raw);
            break;
        case Operation::Reset:
            result =
                broker->Finish_Reset(
                    &raw_size,
                    &raw);
            break;
        case Operation::SyncDirectory:
            result =
                broker
                    ->Finish_PublishSyncDirectory(
                        &raw_size,
                        &raw);
            break;
    }
    return DecodeAndCommit(
        result,
        raw_size,
        raw,
        output);
}

void CALLBACK RunRequest(
    PTP_CALLBACK_INSTANCE,
    void* context) noexcept {
    const std::unique_ptr<Request> request(
        static_cast<Request*>(context));
    const ScopedCom com;
    HRESULT result = com.Result();
    Snapshot snapshot;
    if (SUCCEEDED(result)) {
        result = ExecuteAsync(
            *request,
            snapshot);
    }
    if (result == S_FALSE) {
        result = S_OK;
    }
    try {
        request->completion(
            result,
            snapshot);
    } catch (...) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "ConfigClient async "
            "completion failed");
    }
}

bool SubmitAsync(
    Request request) noexcept {
    try {
        auto context =
            std::make_unique<Request>(
                std::move(request));
        if (!::TrySubmitThreadpoolCallback(
                &RunRequest,
                context.get(),
                nullptr)) {
            QIQI_LOG_CAT_ERROR(
                log::Keyword::Config,
                "ConfigClient threadpool "
                "submission failed "
                "(error={})",
                ::GetLastError());
            return false;
        }
        context.release();
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (
        const std::system_error& error) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "ConfigClient worker startup "
            "failed (error={})",
            error.code().value());
        return false;
    }
}

template <typename Invoke>
HRESULT ExecuteSync(
    Invoke&& invoke,
    Snapshot& output) noexcept {
    const ScopedCom com;
    if (FAILED(com.Result())) {
        return com.Result();
    }
    ComPtr<IQiQiConfigBroker> broker;
    const HRESULT create_result =
        ::CoCreateInstance(
            kClsidQiQiConfigBroker,
            nullptr,
            CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(&broker));
    if (FAILED(create_result) ||
        !broker) {
        return FAILED(create_result)
            ? create_result
            : E_UNEXPECTED;
    }
    HRESULT result =
        SetProxySecurity(broker.Get());
    if (FAILED(result)) {
        return result;
    }
    ULONG raw_size = 0;
    byte* raw = nullptr;
    result = invoke(
        broker.Get(),
        raw_size,
        raw);
    return DecodeAndCommit(
        result,
        raw_size,
        raw,
        output);
}

} // namespace

uint64_t CachedRevision() noexcept {
    try {
        const std::shared_lock lock(
            g_cache.mutex);
        return g_cache.valid
            ? g_cache.value.revision
            : 0;
    } catch (const std::system_error& error) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Config cache revision lock failed "
            "(error={})",
            error.code().value());
        return 0;
    }
}

HRESULT CopyCachedSnapshot(
    Snapshot& output) noexcept {
    try {
        Snapshot copy;
        {
            const std::shared_lock lock(
                g_cache.mutex);
            if (!g_cache.valid) {
                return S_FALSE;
            }
            copy = g_cache.value;
        }
        output = std::move(copy);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (const std::system_error& error) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Config cache copy lock failed "
            "(error={})",
            error.code().value());
        return SystemErrorToHResult(
            error);
    }
}

HRESULT ReadSnapshot(
    Snapshot& output) noexcept {
    const uint64_t revision =
        CachedRevision();
    return ExecuteSync(
        [revision](
            IQiQiConfigBroker* broker,
            ULONG& raw_size,
            byte*& raw) {
            return broker->Read(
                revision,
                &raw_size,
                &raw);
        },
        output);
}

HRESULT SaveSnapshot(
    const SaveRequest& request,
    Snapshot& output) noexcept {
    std::vector<uint8_t> bytes;
    const HRESULT result =
        EncodeSaveRequest(
            request,
            bytes);
    if (FAILED(result)) {
        return result;
    }
    return ExecuteSync(
        [bytes = std::move(bytes)](
            IQiQiConfigBroker* broker,
            ULONG& raw_size,
            byte*& raw) mutable {
            return broker->Save(
                gsl::narrow_cast<ULONG>(
                    bytes.size()),
                bytes.data(),
                &raw_size,
                &raw);
        },
        output);
}

HRESULT ResetSnapshot(
    uint64_t expected_revision,
    Snapshot& output) noexcept {
    return ExecuteSync(
        [expected_revision](
            IQiQiConfigBroker* broker,
            ULONG& raw_size,
            byte*& raw) {
            return broker->Reset(
                expected_revision,
                &raw_size,
                &raw);
        },
        output);
}

HRESULT PublishSyncDirectory(
    const SyncDirectoryRequest& request,
    Snapshot& output) noexcept {
    std::vector<uint8_t> bytes;
    const HRESULT result =
        EncodeSyncDirectoryRequest(
            request,
            bytes);
    if (FAILED(result)) {
        return result;
    }
    return ExecuteSync(
        [bytes = std::move(bytes)](
            IQiQiConfigBroker* broker,
            ULONG& raw_size,
            byte*& raw) mutable {
            return broker
                ->PublishSyncDirectory(
                    gsl::narrow_cast<ULONG>(
                        bytes.size()),
                    bytes.data(),
                    &raw_size,
                    &raw);
        },
        output);
}

bool ReadSnapshotAsync(
    AsyncCompletion completion) noexcept {
    return SubmitAsync(Request{
        .operation = Operation::Read,
        .known_revision =
            CachedRevision(),
        .completion =
            std::move(completion),
    });
}

bool SaveSnapshotAsync(
    SaveRequest request,
    AsyncCompletion completion) noexcept {
    return SubmitAsync(Request{
        .operation = Operation::Save,
        .save = std::move(request),
        .completion =
            std::move(completion),
    });
}

bool ResetSnapshotAsync(
    uint64_t expected_revision,
    AsyncCompletion completion) noexcept {
    return SubmitAsync(Request{
        .operation = Operation::Reset,
        .expected_revision =
            expected_revision,
        .completion =
            std::move(completion),
    });
}

bool PublishSyncDirectoryAsync(
    SyncDirectoryRequest request,
    AsyncCompletion completion) noexcept {
    return SubmitAsync(Request{
        .operation =
            Operation::SyncDirectory,
        .sync_directory =
            std::move(request),
        .completion =
            std::move(completion),
    });
}

} // namespace qiqi::config
