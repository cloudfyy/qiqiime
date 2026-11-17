// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <string>

namespace qiqi::installer {

inline constexpr int kConfigurationControl = 1101;
inline constexpr int kDictionariesControl = 1102;

struct WizardText {
    std::wstring title;
    std::wstring description;
    std::wstring configuration;
    std::wstring dictionaries;
    std::wstring warning;
    std::wstring remove;
    std::wstring cancel;
};

HRESULT ShowDataChoices(const WizardText& text, bool& configuration, bool& dictionaries, bool& accepted);

} // namespace qiqi::installer
