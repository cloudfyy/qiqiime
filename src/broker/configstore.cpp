// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/configstore.h"

#include "common/comobj.h"
#include "common/config.h"
#include "common/logger.h"
#include "common/skin.h"

#include <gsl/gsl>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace qiqi::broker {
namespace {

const wchar_t* OptionalSubkey(
    const std::wstring& subkey) noexcept {
    return subkey.empty() ? nullptr : subkey.c_str();
}

HRESULT CurrentRevision(
    const wchar_t* subkey,
    uint64_t& revision) noexcept {
    revision =
        QueryRegistryConfigSequence(subkey);
    return revision == 0
        ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
        : S_OK;
}

HRESULT CheckExpectedRevision(
    uint64_t expected,
    const wchar_t* subkey) noexcept {
    uint64_t actual = 0;
    const HRESULT result =
        CurrentRevision(subkey, actual);
    if (FAILED(result)) {
        return result;
    }
    return actual == expected
        ? S_OK
        : HRESULT_FROM_WIN32(
              ERROR_REVISION_MISMATCH);
}

template <typename Action>
HRESULT Locked(
    std::mutex& mutex,
    std::string_view operation,
    Action&& action) noexcept {
    try {
        const std::scoped_lock lock(mutex);
        return std::forward<Action>(action)();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "ConfigStore {} allocation failed",
            operation);
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        QIQI_LOG_ERROR(
            "ConfigStore {} size overflowed",
            operation);
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (const std::system_error& error) {
        QIQI_LOG_ERROR(
            "ConfigStore {} synchronization failed "
            "(error={})",
            operation,
            error.code().value());
        return SystemErrorToHResult(
            error);
    }
}

} // namespace

ConfigStore::ConfigStore(std::wstring subkey) noexcept
    : subkey_(std::move(subkey)) {
}

HRESULT ConfigStore::Read(
    config::Snapshot& output) noexcept {
    return Locked(
        mutex_,
        "read",
        [&]() -> HRESULT {
            return ReadLocked(output);
        });
}

HRESULT ConfigStore::ReadLocked(
    config::Snapshot& output) {
    const wchar_t* const subkey =
        OptionalSubkey(subkey_);
    QiQiConfig value;
    if (!LoadConfigFromRegistry(
            value,
            nullptr,
            subkey)) {
        value = {};
        const HRESULT save_result =
            SaveConfigToRegistryResult(
                value,
                subkey);
        if (FAILED(save_result)) {
            return save_result;
        }
        if (!LoadConfigFromRegistry(
                value,
                nullptr,
                subkey)) {
            return E_FAIL;
        }
    }

    HRESULT result =
        EnsureBuiltinImeSkinCatalog(subkey);
    if (FAILED(result)) {
        return result;
    }
    config::Snapshot snapshot;
    snapshot.value = std::move(value);
    result = LoadImeSkinCatalog(
        snapshot.skin_catalog,
        subkey);
    if (FAILED(result)) {
        return result;
    }
    result =
        EnsureConfigAppContainerReadAccess(
            subkey);
    if (FAILED(result)) {
        QIQI_LOG_CAT_WARN(
            log::Keyword::Config,
            "Broker could not refresh "
            "AppContainer registry read access "
            "(hr=0x{:08X})",
            gsl::narrow_cast<
                unsigned long>(
                result));
    }
    result = LoadUserDictSyncDirectoryFromRegistry(
        snapshot.user_dict_sync_directory,
        subkey);
    if (result != S_OK &&
        result !=
            HRESULT_FROM_WIN32(
                ERROR_FILE_NOT_FOUND)) {
        return result;
    }
    result = CurrentRevision(
        subkey,
        snapshot.revision);
    if (FAILED(result)) {
        return result;
    }
    result =
        LoadConfigCommitTraceFromRegistry(
            snapshot.commit_trace_id,
            snapshot.commit_activity_id,
            snapshot.commit_revision,
            subkey);
    if (FAILED(result) &&
        result !=
            HRESULT_FROM_WIN32(
                ERROR_FILE_NOT_FOUND)) {
        return result;
    }
    if (snapshot.commit_revision >
        snapshot.revision) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }
    output = std::move(snapshot);
    return S_OK;
}

HRESULT ConfigStore::Save(
    uint64_t expected_revision,
    const QiQiConfig& value,
    config::Snapshot& output) noexcept {
    return Locked(
        mutex_,
        "save",
        [&]() -> HRESULT {
            return SaveLocked(
                expected_revision,
                value,
                nullptr,
                nullptr,
                output);
        });
}

HRESULT ConfigStore::Save(
    uint64_t expected_revision,
    const QiQiConfig& value,
    const trace::Context& trace,
    const GUID& broker_activity_id,
    config::Snapshot& output) noexcept {
    if (!trace::IsValid(trace) ||
        broker_activity_id == GUID_NULL) {
        return E_INVALIDARG;
    }
    return Locked(
        mutex_,
        "save",
        [&]() -> HRESULT {
            return SaveLocked(
                expected_revision,
                value,
                &trace,
                &broker_activity_id,
                output);
        });
}

HRESULT ConfigStore::SaveLocked(
    uint64_t expected_revision,
    const QiQiConfig& value,
    const trace::Context* trace,
    const GUID* broker_activity_id,
    config::Snapshot& output) {
    const wchar_t* const subkey =
        OptionalSubkey(subkey_);
    HRESULT result =
        CheckExpectedRevision(
            expected_revision,
            subkey);
    if (FAILED(result)) {
        return result;
    }
    result =
        SaveConfigToRegistryResult(
            value,
            subkey,
            trace
                ? &trace->trace_id
                : nullptr,
            broker_activity_id);
    if (FAILED(result)) {
        return result;
    }
    return ReadLocked(output);
}

HRESULT ConfigStore::Reset(
    uint64_t expected_revision,
    config::Snapshot& output) noexcept {
    return Save(
        expected_revision,
        QiQiConfig{},
        output);
}

HRESULT ConfigStore::Reset(
    uint64_t expected_revision,
    const trace::Context& trace,
    const GUID& broker_activity_id,
    config::Snapshot& output) noexcept {
    return Save(
        expected_revision,
        QiQiConfig{},
        trace,
        broker_activity_id,
        output);
}

HRESULT ConfigStore::PublishSyncDirectory(
    uint64_t expected_revision,
    const std::filesystem::path& directory,
    config::Snapshot& output) noexcept {
    return Locked(
        mutex_,
        "publish-sync-directory",
        [&]() -> HRESULT {
            return PublishSyncDirectoryLocked(
                expected_revision,
                directory,
                nullptr,
                nullptr,
                output);
        });
}

HRESULT ConfigStore::PublishSyncDirectory(
    uint64_t expected_revision,
    const std::filesystem::path& directory,
    const trace::Context& trace,
    const GUID& broker_activity_id,
    config::Snapshot& output) noexcept {
    if (!trace::IsValid(trace) ||
        broker_activity_id == GUID_NULL) {
        return E_INVALIDARG;
    }
    return Locked(
        mutex_,
        "publish-sync-directory",
        [&]() -> HRESULT {
            return PublishSyncDirectoryLocked(
                expected_revision,
                directory,
                &trace,
                &broker_activity_id,
                output);
        });
}

HRESULT ConfigStore::PublishSyncDirectoryLocked(
    uint64_t expected_revision,
    const std::filesystem::path& directory,
    const trace::Context* trace,
    const GUID* broker_activity_id,
    config::Snapshot& output) {
    const wchar_t* const subkey =
        OptionalSubkey(subkey_);
    HRESULT result =
        CheckExpectedRevision(
            expected_revision,
            subkey);
    if (FAILED(result)) {
        return result;
    }
    bool changed = false;
    result =
        SaveUserDictSyncDirectoryToRegistry(
            directory,
            subkey,
            trace
                ? &trace->trace_id
                : nullptr,
            broker_activity_id,
            &changed);
    if (FAILED(result)) {
        return result;
    }
    return ReadLocked(output);
}

} // namespace qiqi::broker
