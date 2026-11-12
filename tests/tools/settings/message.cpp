// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/i18n.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "tools/settings/internal.h"

#include <commctrl.h>
#include <string>
#include <tuple>

namespace qiqi::test {
namespace {

std::wstring WindowText(HWND hwnd) {
    std::array<wchar_t, 1024> text{};
    QIQI_TEST_CHECK(hwnd && ::GetWindowTextW(hwnd, text.data(), gsl::narrow_cast<int>(text.size())) > 0);
    return text.data();
}

void CheckBackground(HWND hwnd, COLORREF expected) {
    RECT rect{};
    QIQI_TEST_CHECK(::GetClientRect(hwnd, &rect));
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(::PatBlt(dc.Get(), 0, 0, rect.right, rect.bottom, BLACKNESS));
    ::SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 0, 0) == expected);
}

void CheckMessageText(HWND message, SettingsMessageKind kind, UiLanguage language) {
    const auto& str = GetUiStrings(language);
    QIQI_TEST_CHECK(WindowText(message) == str.dlg_title);
    const auto* expected = kind == SettingsMessageKind::SaveFailure ? str.dlg_save_failed :
        (kind == SettingsMessageKind::NotificationFailure ? str.dlg_notify_failed :
            str.dlg_dict_manager_launch_failed);
    QIQI_TEST_CHECK(WindowText(::GetDlgItem(message, 0xFFFF)) == expected);
    QIQI_TEST_CHECK(WindowText(::GetDlgItem(message, IDOK)) == str.dlg_btn_ok);
    RECT client{}, body{};
    QIQI_TEST_CHECK(::GetClientRect(message, &client));
    QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(message, 0xFFFF), &body));
    ::MapWindowPoints(nullptr, message, reinterpret_cast<POINT*>(&body), 2);
    QIQI_TEST_CHECK(body.right <= client.right && body.bottom < client.bottom);
    const ScopedWindowDC dc(message);
    const auto brush = reinterpret_cast<HBRUSH>(::SendMessageW(message, WM_CTLCOLORSTATIC,
        reinterpret_cast<WPARAM>(dc.Get()), reinterpret_cast<LPARAM>(::GetDlgItem(message, 0xFFFF))));
    LOGBRUSH color{};
    QIQI_TEST_CHECK(::GetObjectW(brush, sizeof(color), &color) == sizeof(color));
    QIQI_TEST_CHECK(color.lbColor == (IsSystemDarkTheme() ? RGB(43, 46, 52) : RGB(255, 255, 255)));
    QIQI_TEST_CHECK(::GetTextColor(dc.Get()) ==
                    (IsSystemDarkTheme() ? kDarkTextColor : RGB(28, 32, 40)));
}

void TestLiveThemeAndLanguage() {
    const auto original_theme = qiqi::detail::ThemeDarkOverrideStorage();
    const auto restore_theme = gsl::finally([original_theme]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
    });
    QiQiConfig cfg;
    cfg.ui_language = UiLanguage::ZhCn;
    const ScopedWindow settings(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg, true));
    const ScopedWindow about(CreateAboutWindow(::GetModuleHandleW(nullptr), settings.Get(), UiLanguage::ZhCn));
    QIQI_TEST_CHECK(settings && about);
    for (const auto kind : {SettingsMessageKind::SaveFailure, SettingsMessageKind::NotificationFailure,
                           SettingsMessageKind::DictManagerLaunchFailure}) {
        const ScopedWindow message(CreateSettingsMessageWindow(
            ::GetModuleHandleW(nullptr), settings.Get(), UiLanguage::ZhCn, kind));
        QIQI_TEST_CHECK(message);
        QIQI_TEST_CHECK(::SendMessageW(message.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        QIQI_TEST_CHECK(::SendMessageW(message.Get(), WM_GETICON, ICON_BIG, 0) != 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(message.Get(), GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) == 0);
        for (const bool dark : {false, true, false}) {
            SetThemeDarkOverrideForTest(dark);
            for (const UINT event : {WM_THEMECHANGED, WM_SETTINGCHANGE}) {
                ::SendMessageW(settings.Get(), event, 0, 0);
                ::SendMessageW(about.Get(), event, 0, 0);
                ::SendMessageW(message.Get(), event, 0, 0);
                CheckBackground(settings.Get(), dark ? kDarkWindowBg : RGB(243, 245, 249));
                CheckBackground(about.Get(), dark ? kDarkWindowBg : kLightSurfaceBg);
                CheckBackground(message.Get(), dark ? kDarkWindowBg : RGB(243, 245, 249));
            }
            for (const WPARAM selection : {2u, 1u, 0u}) {
                ::SendDlgItemMessageW(settings.Get(), settings_detail::kIdComboUiLang, CB_SETCURSEL, selection, 0);
                ::SendMessageW(settings.Get(), WM_COMMAND,
                               MAKEWPARAM(settings_detail::kIdComboUiLang, CBN_SELCHANGE), 0);
                const auto language = selection == 2 ? UiLanguage::EnUs
                    : (selection == 1 ? UiLanguage::ZhCn : UiLanguage::Auto);
                CheckMessageText(message.Get(), kind, language);
                const auto& str = GetUiStrings(language);
                QIQI_TEST_CHECK(WindowText(settings.Get()) ==
                    std::wstring(str.dlg_title) + (language == UiLanguage::ZhCn ? L"" : L" *"));
                QIQI_TEST_CHECK(WindowText(about.Get()) == str.about_title);
                QIQI_TEST_CHECK(WindowText(::GetDlgItem(about.Get(), 201)) == str.about_btn_github);
                QIQI_TEST_CHECK(WindowText(::GetDlgItem(about.Get(), IDOK)) == str.dlg_btn_ok);
                {
                    const ScopedWindowDC dc(about.Get());
                    const auto font = reinterpret_cast<HFONT>(::SendDlgItemMessageW(about.Get(), 201, WM_GETFONT, 0, 0));
                    const ScopedSelectObject select(dc.Get(), font);
                    const auto monitor = QueryActiveMonitor(about.Get());
                    const auto scale = [dpi = monitor.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
                    RECT bounds{0, 0, scale(392), 0};
                    QIQI_TEST_CHECK(::DrawTextW(dc.Get(), str.about_license_info, -1, &bounds,
                                               DT_CALCRECT | DT_WORDBREAK) > 0);
                    RECT button{};
                    QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(about.Get(), 201), &button));
                    ::MapWindowPoints(nullptr, about.Get(), reinterpret_cast<POINT*>(&button), 2);
                    QIQI_TEST_CHECK(bounds.bottom <= button.top - scale(114));
                }
                QIQI_TEST_CHECK(cfg.ui_language == UiLanguage::ZhCn);
            }
        }
    }
    QIQI_TEST_CHECK(!CreateSettingsMessageWindow(
        ::GetModuleHandleW(nullptr), nullptr, UiLanguage::Auto, static_cast<SettingsMessageKind>(99)));
}

thread_local DWORD launch_error = ERROR_SUCCESS;
thread_local HANDLE launched_process = nullptr;

BOOL WINAPI ExecuteDictionaryManagerForTest(SHELLEXECUTEINFOW* info) {
    QIQI_TEST_CHECK(info && info->cbSize == sizeof(*info));
    QIQI_TEST_CHECK(std::wstring_view(info->lpVerb) == L"open");
    QIQI_TEST_CHECK(std::wstring_view(info->lpFile) == L"missing-dictionary-manager.exe");
    QIQI_TEST_CHECK((info->fMask & SEE_MASK_FLAG_NO_UI) != 0);
    if (launch_error != ERROR_SUCCESS) {
        ::SetLastError(launch_error);
        return FALSE;
    }
    launched_process = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    QIQI_TEST_CHECK(launched_process);
    info->hProcess = launched_process;
    return TRUE;
}

void TestDictionaryManagerLaunch() {
    const ScopedWindow owner(::CreateWindowExW(0, L"STATIC", L"Launch owner", WS_POPUP,
        0, 0, 200, 200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(owner);
    for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const DWORD error : {ERROR_FILE_NOT_FOUND, ERROR_ACCESS_DENIED, ERROR_CANCELLED}) {
            launch_error = error;
            WithDismissedMessage(GetUiStrings(language).dlg_dict_manager_launch_failed, [&] {
                QIQI_TEST_CHECK(settings_detail::OpenDictionaryManager(owner.Get(),
                    L"missing-dictionary-manager.exe", language, &ExecuteDictionaryManagerForTest) == error);
            });
            QIQI_TEST_CHECK(::IsWindowEnabled(owner.Get()));
        }
    }
    launch_error = ERROR_SUCCESS;
    QIQI_TEST_CHECK(settings_detail::OpenDictionaryManager(owner.Get(),
        L"missing-dictionary-manager.exe", UiLanguage::EnUs, &ExecuteDictionaryManagerForTest) == ERROR_SUCCESS);
    DWORD flags = 0;
    QIQI_TEST_CHECK(!::GetHandleInformation(launched_process, &flags));
    QIQI_TEST_CHECK(::GetLastError() == ERROR_INVALID_HANDLE);
}

void TestPromptModalBehavior() {
    const ScopedWindow owner(::CreateWindowExW(0, L"STATIC", L"Prompt owner", WS_POPUP,
        0, 0, 200, 200, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(owner);
    for (const bool initially_enabled : {false, true}) {
        ::EnableWindow(owner.Get(), initially_enabled);
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND message = FindThreadWindowByClass(kSettingsMessageWndClass);
            if (!message) return;
            ::KillTimer(nullptr, id);
            QIQI_TEST_CHECK(!::IsWindowEnabled(::GetWindow(message, GW_OWNER)));
            QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == IDOK);
            ::PostMessageW(::GetDlgItem(message, IDOK), WM_KEYDOWN, VK_RETURN, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
        QIQI_TEST_CHECK(ShowSettingsMessage(owner.Get(), UiLanguage::EnUs,
                                            SettingsMessageKind::SaveFailure) == IDOK);
        QIQI_TEST_CHECK((::IsWindowEnabled(owner.Get()) != FALSE) == initially_enabled);
    }
    ::EnableWindow(owner.Get(), TRUE);
    for (const UINT key : {VK_TAB, VK_ESCAPE}) {
        const UINT_PTR timer = ::SetTimer(owner.Get(), key, 10, [](HWND timer_owner, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND message = FindThreadWindowByClass(kSettingsMessageWndClass);
            if (!message) return;
            ::KillTimer(timer_owner, id);
            if (id == VK_TAB) {
                ::SendMessageW(message, WM_KEYDOWN, VK_TAB, 0);
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == IDOK);
                ::PostMessageW(::GetDlgItem(message, IDOK), WM_KEYDOWN, VK_RETURN, 0);
            } else {
                ::PostMessageW(message, WM_KEYDOWN, VK_ESCAPE, 0);
            }
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([timer, timer_owner = owner.Get()]() noexcept {
            ::KillTimer(timer_owner, timer);
        });
        QIQI_TEST_CHECK(ShowSettingsMessage(owner.Get(), UiLanguage::ZhCn,
            SettingsMessageKind::NotificationFailure) == IDOK);
    }
    QiQiConfig cfg;
    ::PostQuitMessage(81);
    QIQI_TEST_CHECK(ShowSettingsMessage(owner.Get(), UiLanguage::Auto, SettingsMessageKind::SaveFailure) == 0);
    MSG quit{};
    QIQI_TEST_CHECK(::PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE) && quit.wParam == 81);
    QIQI_TEST_CHECK(::IsWindowEnabled(owner.Get()));
}

void TestNonModalD2DToast() {
    const auto original_theme = qiqi::detail::ThemeDarkOverrideStorage();
    const auto restore_theme = gsl::finally([original_theme]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
    });
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        for (const auto& [language, kind, expected] : {
                 std::tuple{UiLanguage::ZhCn, SettingsToastKind::SaveFailure,
                            GetUiStrings(UiLanguage::ZhCn).quick_settings_save_failed},
                 std::tuple{UiLanguage::EnUs, SettingsToastKind::NotificationFailure,
                            GetUiStrings(UiLanguage::EnUs).quick_settings_notify_failed}}) {
            const auto* foreground_before = ::GetForegroundWindow();
            const long count_before = g_com_object_count.load();
            ShowSettingsToast(language, kind);
            const HWND toast = FindThreadWindowByClass(kSettingsToastWndClass);
            QIQI_TEST_CHECK(toast && ::IsWindowVisible(toast));
            QIQI_TEST_CHECK(WindowText(toast) == expected);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(toast, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
            QIQI_TEST_CHECK(::GetForegroundWindow() == foreground_before);
            QIQI_TEST_CHECK(g_com_object_count.load() == count_before + 1);
            CheckBackground(toast, dark ? kDarkWindowBg : RGB(243, 245, 249));
            const UiLanguage changed_language =
                language == UiLanguage::EnUs ? UiLanguage::ZhCn : UiLanguage::EnUs;
            UpdateSettingsToastLanguage(changed_language);
            const wchar_t* const changed_text = kind == SettingsToastKind::SaveFailure
                ? GetUiStrings(changed_language).quick_settings_save_failed
                : GetUiStrings(changed_language).quick_settings_notify_failed;
            QIQI_TEST_CHECK(WindowText(toast) == changed_text);
            SetThemeDarkOverrideForTest(!dark);
            ::SendMessageW(toast, WM_THEMECHANGED, 0, 0);
            CheckBackground(toast, dark ? RGB(243, 245, 249) : kDarkWindowBg);
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(toast, WM_THEMECHANGED, 0, 0);
            ::SendMessageW(toast, WM_TIMER, 1, 0);
            QIQI_TEST_CHECK(!::IsWindow(toast));
            QIQI_TEST_CHECK(g_com_object_count.load() == count_before);
        }
    }
}

} // namespace

void RunSettingsMessageTests() {
    TestLiveThemeAndLanguage();
    TestDictionaryManagerLaunch();
    TestPromptModalBehavior();
    TestNonModalD2DToast();
}

} // namespace qiqi::test
