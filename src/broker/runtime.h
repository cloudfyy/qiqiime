// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "broker/configstore.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace qiqi::broker {

class Runtime final {
public:
    explicit Runtime(
        std::wstring subkey);
    ~Runtime() = default;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    HRESULT ValidateCaller() const noexcept;

    HRESULT Read(
        uint64_t known_revision,
        std::vector<uint8_t>& payload) noexcept;
    HRESULT Save(
        const config::SaveRequest& request,
        std::vector<uint8_t>& payload) noexcept;
    HRESULT Reset(
        uint64_t expected_revision,
        std::vector<uint8_t>& payload) noexcept;
    HRESULT PublishSyncDirectory(
        const config::SyncDirectoryRequest& request,
        std::vector<uint8_t>& payload) noexcept;

    void AddObject() noexcept;
    void RemoveObject() noexcept;
    void LockServer() noexcept;
    void UnlockServer() noexcept;
    void Touch() noexcept;
    bool ShouldExit() const noexcept;

private:
    ConfigStore store_;
    std::atomic<long> objects_{0};
    std::atomic<long> server_locks_{0};
    std::atomic<uint64_t> last_activity_{
        0};
    std::vector<uint8_t> user_sid_;
    DWORD session_id_ = 0;
};

} // namespace qiqi::broker
