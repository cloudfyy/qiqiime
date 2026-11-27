// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <msi.h>
#include <restartmanager.h>
#include <string>
#include <string_view>

namespace qiqi::installer {
struct CleanupRequest;
HRESULT ParseCleanupChoices(std::wstring_view legacy, std::wstring_view configuration,
                            std::wstring_view dictionaries, CleanupRequest& request) noexcept;
HRESULT SerializeCleanupRequest(const CleanupRequest& request, std::wstring& serialized,
                                std::wstring_view installation_directory = {});
HRESULT ParseCleanupRequest(std::wstring_view serialized, CleanupRequest& request,
                            std::wstring* installation_directory = nullptr);
bool IsPendingProgramFile(std::wstring_view path, std::wstring_view installation_directory);
bool RequiresRestartForProgramLock(
    RM_APP_TYPE application_type,
    bool restartable,
    std::wstring_view executable,
    std::wstring_view windows_directory) noexcept;
constexpr bool ShouldPromptUninstallResult(
    HRESULT error,
    bool restart_required,
    unsigned int scheduled) noexcept {
    return FAILED(error) || (restart_required && scheduled != 0);
}
}

extern "C" UINT __stdcall ChooseUninstallData(MSIHANDLE install) noexcept;
extern "C" UINT __stdcall PrepareUninstall(MSIHANDLE install) noexcept;
extern "C" UINT __stdcall ExecuteCleanup(MSIHANDLE install) noexcept;
extern "C" UINT __stdcall CompleteUninstall(MSIHANDLE install) noexcept;
extern "C" UINT __stdcall CheckInstallPendingCleanup(MSIHANDLE install) noexcept;
