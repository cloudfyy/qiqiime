// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/langtest.h"
#include "ime/tsf/trace.h"

namespace qiqi::test {

void RunTsfImeIconTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    langbar_cases::TestImeBrandIcon();
    langbar_cases::TestTaskbarThemeIcons();
    langbar_cases::TestLangBarThemeNotifications();
}

void RunMenuSkinTests() { langbar_cases::TestQuickSettingsMenuSkin(); }

void RunLangBarMenuTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    langbar_cases::TestSharedMenuItems();
    langbar_cases::TestQuickSettingsMenu();
    langbar_cases::TestQuickSettingsMenuResourceLifetime();
    langbar_cases::TestQuickSettingsMenuKeyboardAndDismissal();
    langbar_cases::TestQuickSettingsMenuWorkArea();
    langbar_cases::TestQuickSettingsMenuComfortScrolling();
    langbar_cases::TestQuickSettingsMenuCommandValidation();
    langbar_cases::TestQuickSettingsMenuNavigation();
    langbar_cases::TestQuickSettingsMenuLivePainting();
    langbar_cases::TestQuickSettingsMenuLiteralAmpersand();
    langbar_cases::TestQuickSettingsMenuQuitMessage();
    langbar_cases::TestQuickSettingsMenuFocusRestoration();
    langbar_cases::TestLangBarButton();
    langbar_cases::TestLangBarReentrantNotifications();
}

} // namespace qiqi::test
