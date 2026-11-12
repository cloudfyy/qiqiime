// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <msctf.h>
#include <optional>

namespace qiqi {

bool IsRunningUnderWow64() noexcept;
bool IsNative64BitClsidRegistered() noexcept;

HRESULT EnableCurrentUserProfile(ITfInputProcessorProfiles* custom_profiles = nullptr);
HRESULT DeactivateCurrentUserProfile(ITfInputProcessorProfiles* custom_profiles = nullptr);

HRESULT RegisterServerWithManagers(
    HINSTANCE instance,
    ITfInputProcessorProfiles* custom_profiles = nullptr,
    ITfCategoryMgr* custom_cat_mgr = nullptr,
    std::optional<bool> override_wow64 = std::nullopt);

HRESULT UnregisterServerWithManagers(
    ITfInputProcessorProfiles* custom_profiles = nullptr,
    ITfCategoryMgr* custom_cat_mgr = nullptr,
    std::optional<bool> override_wow64 = std::nullopt);

HRESULT RegisterServer(HINSTANCE instance);
HRESULT UnregisterServer();

} // namespace qiqi
