// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "cleanup.h"
#include <functional>

namespace qiqi::installer::detail {

// All registry handles must belong to a private, disposable HKCU test subtree.
// ProfileList\<SID>\ProfileImagePath and Users\<SID>\Software\QiQiIME model
// the corresponding machine/profile hives. No production global is replaced.
// quarantine_root is a disposable same-volume folder; scheduling is always fake.
struct CleanupSandbox {
    HKEY machine = nullptr;
    HKEY users = nullptr;
    std::filesystem::path quarantine_root;
    std::wstring boot_session;
    std::function<HRESULT(const std::filesystem::path&)> schedule;
    std::function<HRESULT()> before_pending_persist;
};

CleanupResult PerformCleanup(const CleanupRequest& request, const CleanupSandbox& sandbox,
    bool system_restart_required = false);
HRESULT ReadCleanupResult(const CleanupRequest& request, CleanupResult& result,
    const CleanupSandbox& sandbox);
HRESULT CheckPendingCleanup(PendingState& state, const CleanupSandbox& sandbox);

} // namespace qiqi::installer::detail
