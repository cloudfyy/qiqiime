// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace qiqi::diag {

struct Selection {
    bool machine = false;
    bool processes = false;
    bool ime = false;
    bool broker = false;

    constexpr bool Any() const noexcept {
        return machine || processes || ime || broker;
    }
};

std::filesystem::path MakeDefaultReportDirectory();
std::wstring RedactPathForReport(std::wstring_view path);
std::wstring ExtractLocalServerExecutable(std::wstring_view command);
HRESULT ReadConfiguredUiLanguage(UiLanguage& language) noexcept;

HRESULT BuildReport(const Selection& selection, bool compact, std::string& output);

HRESULT WriteReportSet(const std::filesystem::path& directory, std::string_view report);

} // namespace qiqi::diag
