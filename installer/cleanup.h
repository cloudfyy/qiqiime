// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <filesystem>
#include <string>

namespace qiqi::installer {

struct CleanupRequest {
    std::wstring sid;
    std::wstring transaction;
    bool configuration = false;
    bool dictionaries = false;
};

struct CleanupResult {
    HRESULT error = S_OK;
    unsigned int deleted = 0;
    unsigned int scheduled = 0;
    bool restart_required = false;
};

enum class PendingState { clear, restart_required, incomplete };

// Entry points never initialize or delete the user's preferences implicitly.
// Files blocked by mappings are isolated on the same volume in a protected,
// machine-owned quarantine before being queued for reboot deletion.
HRESULT ValidateRequest(const CleanupRequest& request) noexcept;
CleanupResult PerformCleanup(const CleanupRequest& request, bool system_restart_required = false);
HRESULT ReadCleanupResult(const CleanupRequest& request, CleanupResult& result);
HRESULT CheckPendingCleanup(PendingState& state);

} // namespace qiqi::installer
