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

// Persisted values; append new operations without renumbering existing ones.
enum class CleanupOperation : DWORD {
    unknown,
    validate_request,
    open_state,
    open_job,
    read_job,
    persist_job,
    remove_configuration,
    resolve_profile,
    open_source_directory,
    verify_source_directory,
    enumerate_cache,
    open_source_file,
    verify_source_file,
    delete_source_file,
    open_volume_root,
    verify_volume_root,
    verify_volume_security,
    read_volume_name,
    create_quarantine_directory,
    open_quarantine_directory,
    verify_quarantine_directory,
    boot_session,
    persist_pending_target,
    rename_to_quarantine,
    schedule_reboot_delete,
    persist_result,
    generate_quarantine_name,
    open_configuration_key,
    check_configuration_link,
    enumerate_configuration_keys,
    delete_configuration_key,
};

struct CleanupResult {
    HRESULT error = S_OK;
    unsigned int deleted = 0;
    unsigned int scheduled = 0;
    bool restart_required = false;
    CleanupOperation operation = CleanupOperation::unknown;
};

enum class PendingState { clear, restart_required, incomplete };

// Entry points never initialize or delete the user's preferences implicitly.
// Files blocked by mappings are isolated on the same volume in a protected,
// machine-owned quarantine before being queued for reboot deletion.
HRESULT ValidateRequest(const CleanupRequest& request) noexcept;
const wchar_t* CleanupOperationName(CleanupOperation operation) noexcept;
CleanupResult PerformCleanup(const CleanupRequest& request, bool system_restart_required = false);
HRESULT ReadCleanupResult(const CleanupRequest& request, CleanupResult& result);
HRESULT CheckPendingCleanup(PendingState& state);

} // namespace qiqi::installer
