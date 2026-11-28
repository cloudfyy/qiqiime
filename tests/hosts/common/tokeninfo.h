// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace qiqi::host::token {

struct State {
    DWORD open_error = ERROR_NOT_READY;
    DWORD app_container_error = ERROR_NOT_READY;
    DWORD less_privileged_error = ERROR_NOT_READY;
    DWORD package_sid_error = ERROR_NOT_READY;
    DWORD capabilities_error = ERROR_NOT_READY;
    DWORD groups_error = ERROR_NOT_READY;
    bool is_app_container = false;
    bool is_less_privileged_app_container = false;
    bool inferred_less_privileged_app_container = false;
    bool has_all_application_packages = false;
    bool has_all_restricted_application_packages = false;
    bool has_lpac_com = false;
    bool has_registry_read = false;
    std::wstring package_sid;
    std::vector<std::wstring> capability_sids;
};

State ReadCurrentProcessState() noexcept;
std::wstring FormatState(const State& state);

} // namespace qiqi::host::token
