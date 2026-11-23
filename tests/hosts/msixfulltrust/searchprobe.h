// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

namespace qiqi::host::fulltrust {

struct SearchProbeResult {
    HRESULT coinitialize_hr = S_OK;
    HRESULT activate_hr = S_OK;
    HRESULT provider_hr = S_OK;
    HRESULT function_hr = S_OK;
    HRESULT candidates_hr = S_OK;
    ULONG candidate_count = 0;
    bool provider_available = false;
};

[[nodiscard]] SearchProbeResult ProbeSearchCandidateProvider() noexcept;
[[nodiscard]] std::wstring FormatSearchProbe(
    const SearchProbeResult& result);

} // namespace qiqi::host::fulltrust

