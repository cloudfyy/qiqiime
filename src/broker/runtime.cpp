// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/runtime.h"

#include "common/comobj.h"
#include "common/configwire.h"
#include "common/logger.h"
#include "common/RAII.h"

#include <sddl.h>
#include <gsl/gsl>
#include <new>
#include <span>
#include <system_error>
#include <utility>

namespace qiqi::broker {
namespace {

constexpr uint64_t kIdleTimeoutMs =
    5ull * 60ull * 1000ull;

class ImpersonationScope final {
public:
    ImpersonationScope() noexcept
        : result_(::CoImpersonateClient()) {
    }

    ~ImpersonationScope() noexcept {
        if (SUCCEEDED(result_)) {
            std::ignore =
                ::CoRevertToSelf();
        }
    }

    ImpersonationScope(
        const ImpersonationScope&) =
        delete;
    ImpersonationScope& operator=(
        const ImpersonationScope&) =
        delete;
    ImpersonationScope(
        ImpersonationScope&&) =
        delete;
    ImpersonationScope& operator=(
        ImpersonationScope&&) =
        delete;

    HRESULT Result() const noexcept {
        return result_;
    }

private:
    HRESULT result_ = E_UNEXPECTED;
};

std::vector<uint8_t> TokenUserSid(
    HANDLE token,
    HRESULT& result) noexcept {
    result = S_OK;
    DWORD length = 0;
    if (::GetTokenInformation(
            token,
            TokenUser,
            nullptr,
            0,
            &length)) {
        result = E_UNEXPECTED;
        return {};
    }
    const DWORD size_error =
        ::GetLastError();
    if (size_error != ERROR_INSUFFICIENT_BUFFER ||
        length == 0) {
        result =
            HRESULT_FROM_WIN32(size_error);
        return {};
    }
    try {
        std::vector<uint8_t> bytes(length);
        if (!::GetTokenInformation(
                token,
                TokenUser,
                bytes.data(),
                length,
                &length)) {
            result = HRESULT_FROM_WIN32(
                ::GetLastError());
            return {};
        }
        const auto* user =
            reinterpret_cast<
                const TOKEN_USER*>(
                    bytes.data());
        if (!::IsValidSid(user->User.Sid)) {
            result =
                HRESULT_FROM_WIN32(
                    ERROR_INVALID_SID);
            return {};
        }
        const DWORD sid_length =
            ::GetLengthSid(user->User.Sid);
        const std::span<
            const uint8_t> sid_bytes(
                static_cast<
                    const uint8_t*>(
                        user->User.Sid),
                sid_length);
        return {
            sid_bytes.begin(),
            sid_bytes.end(),
        };
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
        return {};
    }
}

HRESULT ReadTokenIdentity(
    HANDLE token,
    std::vector<uint8_t>& sid,
    DWORD& session_id,
    bool reject_app_container) noexcept {
    HRESULT result = S_OK;
    sid = TokenUserSid(token, result);
    if (FAILED(result)) {
        return result;
    }
    DWORD length = 0;
    if (!::GetTokenInformation(
            token,
            TokenSessionId,
            &session_id,
            sizeof(session_id),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    DWORD app_container = 0;
    if (!::GetTokenInformation(
            token,
            TokenIsAppContainer,
            &app_container,
            sizeof(app_container),
            &length)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    return reject_app_container && app_container
        ? E_ACCESSDENIED
        : S_OK;
}

HRESULT PublishSnapshot(
    config::Snapshot& snapshot,
    bool notify,
    const trace::Context& trace,
    std::vector<uint8_t>& payload,
    trace::Result& trace_result) noexcept {
    snapshot.notification_result = S_OK;
    if (notify) {
        snapshot.notification_result =
            BroadcastStandaloneGlobalCompartmentIncrementResult(
                kGuidCompartmentConfigVersion);
        if (FAILED(
                snapshot.notification_result)) {
            QIQI_LOG_CTX(
                log::Level::warn,
                (log::LogContext{
                    log::Keyword::Config,
                    log::Opcode::Info,
                    &trace_result
                         .broker_activity_id,
                    &trace
                         .caller_activity_id,
                    &trace.trace_id}),
                "TSF notification failed "
                "(hr=0x{:08X})",
                gsl::narrow_cast<unsigned long>(
                    snapshot.notification_result));
        } else {
            QIQI_LOG_CTX(
                log::Level::debug,
                (log::LogContext{
                    log::Keyword::Config,
                    log::Opcode::Info,
                    &trace_result
                         .broker_activity_id,
                    &trace
                         .caller_activity_id,
                    &trace.trace_id}),
                "TSF notification published");
        }
    }
    trace_result.commit_trace_id =
        snapshot.commit_trace_id;
    trace_result.commit_activity_id =
        snapshot.commit_activity_id;
    trace_result.commit_revision =
        snapshot.commit_revision;
    trace_result.revision =
        snapshot.revision;
    return config::EncodeSnapshot(
        snapshot,
        payload);
}

} // namespace

Runtime::Runtime(std::wstring subkey)
    : store_(std::move(subkey)) {
    ScopedKernelHandle process_token;
    if (!::OpenProcessToken(
            ::GetCurrentProcess(),
            TOKEN_QUERY,
            process_token.Put())) {
        throw std::system_error(
            gsl::narrow_cast<int>(::GetLastError()),
            std::system_category(),
            "OpenProcessToken");
    }
    DWORD current_session = 0;
    const HRESULT identity =
        ReadTokenIdentity(
            process_token.Get(),
            user_sid_,
            current_session,
            true);
    if (FAILED(identity) ||
        current_session == 0xffffffffu) {
        throw std::system_error(
            identity,
            std::generic_category(),
            "ReadTokenIdentity");
    }
    session_id_ = current_session;
    Touch();
}

HRESULT Runtime::ValidateCaller() const noexcept {
    const ImpersonationScope impersonation;
    if (FAILED(impersonation.Result())) {
        return impersonation.Result();
    }
    HANDLE raw = nullptr;
    if (!::OpenThreadToken(
            ::GetCurrentThread(),
            TOKEN_QUERY,
            TRUE,
            &raw)) {
        return HRESULT_FROM_WIN32(
            ::GetLastError());
    }
    const ScopedKernelHandle token(raw);
    std::vector<uint8_t> caller_sid;
    DWORD caller_session = 0;
    const HRESULT result = ReadTokenIdentity(
        token.Get(),
        caller_sid,
        caller_session,
        false);
    if (FAILED(result)) {
        return result;
    }
    if (result != S_OK ||
        caller_session != session_id_ ||
        caller_sid != user_sid_) {
        return E_ACCESSDENIED;
    }
    return S_OK;
}

HRESULT Runtime::Read(
    uint64_t known_revision,
    std::vector<uint8_t>& payload,
    trace::Result& trace_result) noexcept {
    config::Snapshot snapshot;
    const HRESULT result = store_.Read(snapshot);
    if (FAILED(result)) {
        return result;
    }
    trace_result.commit_trace_id =
        snapshot.commit_trace_id;
    trace_result.commit_activity_id =
        snapshot.commit_activity_id;
    trace_result.commit_revision =
        snapshot.commit_revision;
    trace_result.revision =
        snapshot.revision;
    Touch();
    if (known_revision ==
        snapshot.revision) {
        payload.clear();
        return S_FALSE;
    }
    return PublishSnapshot(
        snapshot,
        false,
        trace::Context{},
        payload,
        trace_result);
}

HRESULT Runtime::Save(
    const config::SaveRequest& request,
    const trace::Context& trace,
    std::vector<uint8_t>& payload,
    trace::Result& trace_result) noexcept {
    config::Snapshot snapshot;
    const HRESULT result = store_.Save(
        request.expected_revision,
        request.value,
        trace,
        trace_result
            .broker_activity_id,
        snapshot);
    if (FAILED(result)) {
        return result;
    }
    Touch();
    return PublishSnapshot(
        snapshot,
        true,
        trace,
        payload,
        trace_result);
}

HRESULT Runtime::Reset(
    uint64_t expected_revision,
    const trace::Context& trace,
    std::vector<uint8_t>& payload,
    trace::Result& trace_result) noexcept {
    config::Snapshot snapshot;
    const HRESULT result = store_.Reset(
        expected_revision,
        trace,
        trace_result
            .broker_activity_id,
        snapshot);
    if (FAILED(result)) {
        return result;
    }
    Touch();
    return PublishSnapshot(
        snapshot,
        true,
        trace,
        payload,
        trace_result);
}

HRESULT Runtime::PublishSyncDirectory(
    const config::SyncDirectoryRequest& request,
    const trace::Context& trace,
    std::vector<uint8_t>& payload,
    trace::Result& trace_result) noexcept {
    config::Snapshot snapshot;
    const HRESULT result =
        store_.PublishSyncDirectory(
            request.expected_revision,
            request.directory,
            trace,
            trace_result
                .broker_activity_id,
            snapshot);
    if (FAILED(result)) {
        return result;
    }
    Touch();
    return PublishSnapshot(
        snapshot,
        snapshot.revision !=
            request.expected_revision,
        trace,
        payload,
        trace_result);
}

void Runtime::AddObject() noexcept {
    ++objects_;
    Touch();
}

void Runtime::RemoveObject() noexcept {
    --objects_;
    Touch();
}

void Runtime::LockServer() noexcept {
    ++server_locks_;
    Touch();
}

void Runtime::UnlockServer() noexcept {
    if (server_locks_ > 0) {
        --server_locks_;
    }
    Touch();
}

void Runtime::Touch() noexcept {
    last_activity_.store(
        ::GetTickCount64(),
        std::memory_order_relaxed);
}

bool Runtime::ShouldExit() const noexcept {
    return objects_.load(
               std::memory_order_relaxed) ==
               0 &&
        server_locks_.load(
            std::memory_order_relaxed) ==
            0 &&
        ::GetTickCount64() -
                last_activity_.load(
                    std::memory_order_relaxed) >=
            kIdleTimeoutMs;
}

} // namespace qiqi::broker
