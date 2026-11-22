// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <cstdint>
#include <span>
#include <vector>

namespace qiqi {
class LangBarButton;
}

namespace qiqi::test::langbar_cases {

HWND FindQuickSettingsMenu() noexcept;
void CloseQuickSettingsMenu();
void CheckImeModeIconPixels(std::span<const uint32_t> pixels, int size);
std::vector<uint32_t> ReadTaskbarIconPixels(qiqi::LangBarButton& button);

void TestLangBarButton();
void TestLangBarReentrantNotifications();
void TestLangBarThemeNotifications();
void TestSharedMenuItems();
void TestImeBrandIcon();
void TestTaskbarThemeIcons();
void TestQuickSettingsMenu();
void TestQuickSettingsMenuResourceLifetime();
void TestQuickSettingsMenuKeyboardAndDismissal();
void TestQuickSettingsMenuWorkArea();
void TestQuickSettingsMenuCommandValidation();
void TestQuickSettingsMenuComfortScrolling();
void TestQuickSettingsMenuNavigation();
void TestQuickSettingsMenuLivePainting();
void TestQuickSettingsMenuSkin();
void TestQuickSettingsMenuLiteralAmpersand();
void TestQuickSettingsMenuQuitMessage();
void TestQuickSettingsMenuFocusRestoration();

} // namespace qiqi::test::langbar_cases
