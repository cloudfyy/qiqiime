#include "common/test_support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "tools/settings/internal.h"
#include "tools/settings/window.h"

#include <windows.h>
#include <array>
#include <string>
#include <tuple>
#include <utility>

namespace qiqi::test {

namespace {

void TestSettingsDialogControls() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto restore_theme = gsl::finally([original_theme]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
    });
    QiQiConfig cfg{};
    cfg.primary_dict = PrimaryDict::Haifeng;
    cfg.enable_jidian_extra = false;
    cfg.auto_commit_4code = false;
    cfg.push_commit_5th = false;
    cfg.semicolon_quote_quick_select = false;
    cfg.show_code_hint = false;
    cfg.dynamic_phrase_freq = true;
    cfg.shift_toggle_mode = false;
    cfg.show_status_bar = true;
    cfg.beep_on_empty_4code = false;
    cfg.beep_on_dup_4code = true;
    cfg.clear_on_empty_4code = true;
    cfg.enable_key_sound = true;
    cfg.up_down_page_turn = true;
    cfg.page_size = 7;

    ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
    QIQI_TEST_CHECK(hwnd.IsValid());

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    QIQI_TEST_CHECK(!read_back.enable_jidian_extra);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.push_commit_5th);
    QIQI_TEST_CHECK(!read_back.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!read_back.show_code_hint);
    QIQI_TEST_CHECK(read_back.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!read_back.shift_toggle_mode);
    QIQI_TEST_CHECK(read_back.show_status_bar);
    QIQI_TEST_CHECK(!read_back.beep_on_empty_4code);
    QIQI_TEST_CHECK(read_back.beep_on_dup_4code);
    QIQI_TEST_CHECK(read_back.clear_on_empty_4code);
    QIQI_TEST_CHECK(read_back.enable_key_sound);
    QIQI_TEST_CHECK(read_back.up_down_page_turn);
    QIQI_TEST_CHECK(read_back.page_size == 7);

    // Also test BuiltinOnly and Jidian radio states + UiLanguage combobox + Cancel / WM_CLOSE
    cfg.primary_dict = PrimaryDict::BuiltinOnly;
    cfg.ui_language = UiLanguage::EnUs;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::EnUs);

    cfg.primary_dict = PrimaryDict::Jidian;
    cfg.ui_language = UiLanguage::ZhCn;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    // Simulate clicking Haifeng radio (102) -> disables Extra checkbox; clicking Jidian radio (101) -> enables it
    ::CheckRadioButton(hwnd.Get(), 101, 103, 102);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ::CheckRadioButton(hwnd.Get(), 101, 103, 101);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(101, BN_CLICKED), 0);
    QIQI_TEST_CHECK(::IsWindowEnabled(::GetDlgItem(hwnd.Get(), 104)));
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn);

    cfg.ui_language = UiLanguage::Auto;
    WriteSettingsToWindowControls(hwnd.Get(), cfg);
    ReadSettingsFromWindowControls(hwnd.Get(), read_back);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::Auto);

    // Test WM_CTLCOLORSTATIC brush/text styling on a static control (kIdLblPageSize = 122)
    {
        const HWND lbl = ::GetDlgItem(hwnd.Get(), 122);
        QIQI_TEST_CHECK(lbl != nullptr);
        const ScopedWindowDC lbl_dc(lbl);
        QIQI_TEST_CHECK(::SendMessageW(
                            hwnd.Get(),
                            WM_CTLCOLORSTATIC,
                            reinterpret_cast<WPARAM>(lbl_dc.Get()),
                            reinterpret_cast<LPARAM>(lbl)) != 0);
        const COLORREF tc = ::GetTextColor(lbl_dc.Get());
        QIQI_TEST_CHECK(tc == RGB(28, 32, 40) || tc == RGB(238, 240, 245));
    }

    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    ::SendMessageW(hwnd.Get(), WM_CLOSE, 0, 0);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    std::array<wchar_t, 128> title_buf{};
    {
        // Verify English settings dialog title when created with UiLanguage::EnUs
        cfg.ui_language = UiLanguage::EnUs;
        const ScopedWindow hwnd_en(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_en.IsValid());
        ::GetWindowTextW(hwnd_en.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi Wubi IME - Settings");

        // Verify Chinese settings dialog title when created with UiLanguage::ZhCn
        cfg.ui_language = UiLanguage::ZhCn;
        const ScopedWindow hwnd_zh(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_zh.IsValid());
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi 五笔输入法 - 属性设置");

        // Exercise Direct2D Fluent UI card and toggle-switch painting in both Light and Dark themes
        for (const bool dark : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(hwnd_zh.Get(), WM_THEMECHANGED, 0, 0);
            ::ShowWindow(hwnd_zh.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_zh.Get(), nullptr, TRUE);
            ::UpdateWindow(hwnd_zh.Get());
        }
        SetThemeDarkOverrideForTest(original_theme);

        // Verify live UI language switching via kIdComboUiLang (111) CBN_SELCHANGE
        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 2, 0); // EnUs
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi Wubi IME - Settings");

        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 1, 0); // ZhCn
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"QiQi 五笔输入法 - 属性设置");

        // Test clicking the "About..." button (id=115) inside SettingsDialog (both with bound_config and nullptr)
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = FindThreadWindowByClass(kAboutWndClass);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_CLOSE, 0, 0);
            }
        });
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);

        const ScopedWindow hwnd_null_cfg(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, nullptr));
        QIQI_TEST_CHECK(hwnd_null_cfg.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(hwnd_null_cfg.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = FindThreadWindowByClass(kAboutWndClass);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd_null_cfg.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);
    }

    hwnd.Reset();

    // Test modal ShowSettingsDialog with auto-closing timer (IDOK -> returns true, IDCANCEL -> returns false)
    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    // Verify single-instance Settings behavior:
    // 1) When no Settings window exists, ActivateExistingSettingsWindow returns false.
    // 2) When a Settings window is open (including minimized or with an owned popup),
    //    ActivateExistingSettingsWindow restores/activates it and ShowSettingsDialog returns false
    //    immediately without creating a second Settings window.
    // 3) When the single-instance mutex is already held, ShowSettingsDialog returns false without
    //    opening a duplicate dialog.
    QIQI_TEST_CHECK(!ActivateExistingSettingsWindow());
    {
        const ScopedWindow existing(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(existing.IsValid());
        ::ShowWindow(existing.Get(), SW_MINIMIZE);
        QIQI_TEST_CHECK(::IsIconic(existing.Get()) != FALSE);
        QIQI_TEST_CHECK(ActivateExistingSettingsWindow());
        QIQI_TEST_CHECK(::IsIconic(existing.Get()) == FALSE);

        const ScopedWindow owned_about(
            CreateAboutWindow(::GetModuleHandleW(nullptr), existing.Get(), UiLanguage::ZhCn));
        QIQI_TEST_CHECK(owned_about.IsValid());
        ::ShowWindow(owned_about.Get(), SW_SHOWNOACTIVATE);
        QIQI_TEST_CHECK(ActivateExistingSettingsWindow());
        QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
    }
    {
        const ScopedKernelHandle held_mutex(
            ::CreateMutexW(nullptr, TRUE, kSettingsSingleInstanceMutexName));
        QIQI_TEST_CHECK(held_mutex.IsValid());
        QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));
    }

    // Verify About dialog creation, painting, title localization, child controls, icon, and modal ShowAboutDialog
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_app_name).find(L"奇奇五笔") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_app_name) == L"QiQi Wubi IME");
    QIQI_TEST_CHECK(std::wstring(kGitHubRepoUrl) == L"https://github.com/cloudfyy/qiqiime");
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"cloudfyy") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kEnUsStrings.about_license_info).find(L"GPL License") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"Apache License 2.0") != std::wstring::npos);
    QIQI_TEST_CHECK(std::wstring(kZhCnStrings.about_license_info).find(L"BSD 3-Clause License") != std::wstring::npos);

    {
        const ScopedWindow about_zh(CreateAboutWindow(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
        QIQI_TEST_CHECK(about_zh.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(about_zh.Get(), WM_GETICON, ICON_SMALL, 0) != 0);
        ::GetWindowTextW(about_zh.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"关于 奇奇五笔");
        std::array<wchar_t, 64> btn_buf{};
        ::GetDlgItemTextW(about_zh.Get(), 201, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"访问 GitHub");
        ::GetDlgItemTextW(about_zh.Get(), IDOK, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"确定");
        ::ShowWindow(about_zh.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_zh.Get(), nullptr, TRUE);
        ::UpdateWindow(about_zh.Get());
        ::SendMessageW(about_zh.Get(), WM_CLOSE, 0, 0);

        const ScopedWindow about_en(CreateAboutWindow(::GetModuleHandleW(nullptr), about_zh.Get(), UiLanguage::EnUs));
        QIQI_TEST_CHECK(about_en.IsValid());
        ::GetWindowTextW(about_en.Get(), title_buf.data(), gsl::narrow_cast<int>(title_buf.size()));
        QIQI_TEST_CHECK(std::wstring(title_buf.data()) == L"About QiQi Wubi IME");
        ::GetDlgItemTextW(about_en.Get(), 201, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"Visit GitHub");
        ::GetDlgItemTextW(about_en.Get(), IDOK, btn_buf.data(), gsl::narrow_cast<int>(btn_buf.size()));
        QIQI_TEST_CHECK(std::wstring(btn_buf.data()) == L"OK");
        ::ShowWindow(about_en.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_en.Get(), nullptr, TRUE);
        ::UpdateWindow(about_en.Get());
        ::SendMessageW(about_en.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    }

    ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = FindThreadWindowByClass(kAboutWndClass);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
}

void TestSettingsWindowMonitorChanges() {
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 1920, 1080}, 96});
    QiQiConfig config{};
    config.page_size = 7;
    config.auto_commit_4code = true;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog.IsValid());

    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboPageSize, CB_SETCURSEL, 8, 0);
    ::CheckDlgButton(dialog.Get(), settings_detail::kIdChkAutoCommit4, BST_UNCHECKED);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-2560, -1440, 0, 0}, 192});
    const RECT suggested_rect{-2100, -1250, -400, -200};
    ::SendMessageW(
        dialog.Get(),
        WM_DPICHANGED,
        MAKEWPARAM(192, 192),
        reinterpret_cast<LPARAM>(&suggested_rect));

    const HWND page_size_combo = ::GetDlgItem(dialog.Get(), settings_detail::kIdComboPageSize);
    QIQI_TEST_CHECK(page_size_combo != nullptr);
    POINT combo_origin{};
    ::ClientToScreen(dialog.Get(), &combo_origin);
    RECT combo_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(page_size_combo, &combo_rect));
    QIQI_TEST_CHECK(combo_rect.left - combo_origin.x == ScalePxByDpi(588, 192));

    const HFONT combo_font = reinterpret_cast<HFONT>(::SendMessageW(page_size_combo, WM_GETFONT, 0, 0));
    LOGFONTW font_info{};
    QIQI_TEST_CHECK(combo_font != nullptr);
    QIQI_TEST_CHECK(::GetObjectW(combo_font, sizeof(font_info), &font_info) == sizeof(font_info));
    QIQI_TEST_CHECK(font_info.lfHeight == -ScalePxByDpi(15, 192));

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(dialog.Get(), read_back);
    QIQI_TEST_CHECK(read_back.page_size == 9);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1900, -1200, 0, 0}, 192});
    ::SendMessageW(dialog.Get(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    RECT dialog_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(dialog.Get(), &dialog_rect));
    QIQI_TEST_CHECK(dialog_rect.left == -1900 && dialog_rect.top == -1200);
    QIQI_TEST_CHECK(dialog_rect.right <= 0 && dialog_rect.bottom <= 0);
}

} // namespace

void RunSettingsDialogTests() {
    TestSettingsDialogControls();
    TestSettingsWindowMonitorChanges();
}

} // namespace qiqi::test
