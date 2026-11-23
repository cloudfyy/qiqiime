// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <optional>
#include <string>

namespace qiqi::host::fulltrust {

struct PackageState {
    bool has_package_identity = false;
    bool is_app_container = false;
    DWORD integrity_rid = 0;
    DWORD package_name_error = ERROR_SUCCESS;
    DWORD app_container_error = ERROR_SUCCESS;
    DWORD integrity_error = ERROR_SUCCESS;
    std::wstring package_full_name;
};

struct RegistryState {
    HRESULT root_hr = S_OK;
    HRESULT sequence_hr = S_OK;
    HRESULT skin_hr = S_OK;
    HRESULT catalog_hr = S_OK;
    HRESULT sync_directory_hr = S_OK;
    HRESULT marker_hr = S_OK;
    std::optional<uint64_t> config_sequence;
    std::optional<std::wstring> skin_id;
    std::optional<std::wstring> active_catalog;
    bool sync_directory_present = false;
    bool shadow_marker_present = false;
};

struct ShadowSeedResult {
    HRESULT hr = S_OK;
    uint64_t previous_sequence = 0;
    uint64_t shadow_sequence = 0;
    std::wstring previous_skin;
    std::wstring shadow_skin;
};

[[nodiscard]] PackageState ReadPackageState() noexcept;
[[nodiscard]] RegistryState ReadQiQiRegistryState() noexcept;
[[nodiscard]] ShadowSeedResult SeedPackagePrivateQiQiShadow(
    const PackageState& package_state) noexcept;

[[nodiscard]] std::wstring IntegrityLabel(DWORD rid) noexcept;
[[nodiscard]] std::wstring FormatPackageState(const PackageState& state);
[[nodiscard]] std::wstring FormatRegistryState(const RegistryState& state);

} // namespace qiqi::host::fulltrust

