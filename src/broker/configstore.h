// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configwire.h"

#include <filesystem>
#include <mutex>
#include <string>

namespace qiqi::broker {

class ConfigStore final {
public:
    ConfigStore() noexcept = default;
    explicit ConfigStore(std::wstring subkey) noexcept;

    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;
    ConfigStore(ConfigStore&&) = delete;
    ConfigStore& operator=(ConfigStore&&) = delete;

    HRESULT Read(config::Snapshot& output) noexcept;
    HRESULT Save(
        uint64_t expected_revision,
        const QiQiConfig& value,
        config::Snapshot& output) noexcept;
    HRESULT Save(
        uint64_t expected_revision,
        const QiQiConfig& value,
        const trace::Context& trace,
        const GUID& broker_activity_id,
        config::Snapshot& output) noexcept;
    HRESULT Reset(
        uint64_t expected_revision,
        config::Snapshot& output) noexcept;
    HRESULT Reset(
        uint64_t expected_revision,
        const trace::Context& trace,
        const GUID& broker_activity_id,
        config::Snapshot& output) noexcept;
    HRESULT PublishSyncDirectory(
        uint64_t expected_revision,
        const std::filesystem::path& directory,
        config::Snapshot& output) noexcept;
    HRESULT PublishSyncDirectory(
        uint64_t expected_revision,
        const std::filesystem::path& directory,
        const trace::Context& trace,
        const GUID& broker_activity_id,
        config::Snapshot& output) noexcept;

private:
    HRESULT ReadLocked(
        config::Snapshot& output);
    HRESULT SaveLocked(
        uint64_t expected_revision,
        const QiQiConfig& value,
        const trace::Context* trace,
        const GUID* broker_activity_id,
        config::Snapshot& output);
    HRESULT PublishSyncDirectoryLocked(
        uint64_t expected_revision,
        const std::filesystem::path& directory,
        const trace::Context* trace,
        const GUID* broker_activity_id,
        config::Snapshot& output);

    std::wstring subkey_;
    std::mutex mutex_;
};

} // namespace qiqi::broker
