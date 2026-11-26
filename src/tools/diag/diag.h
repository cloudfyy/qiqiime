// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

HRESULT BuildReport(const Selection& selection, bool compact, std::string& output) noexcept;

HRESULT WriteReportSet(const std::filesystem::path& directory, std::string_view report) noexcept;

} // namespace qiqi::diag
