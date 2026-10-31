#include "common.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "ui/settings_dialog.h"

#include <windows.h>
#include <string>

namespace qiqi::test {

namespace {

void TestSettingsDialogControls() {
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
    cfg.log_keystrokes = true;
    cfg.page_size = 7;

    const ScopedWindow hwnd(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
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
    QIQI_TEST_CHECK(read_back.log_keystrokes);
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

    // Test privacy notice WM_CTLCOLORSTATIC highlighting and confirmation MessageBox on kIdChkLogKeystrokes (120)
    {
        const HWND privacy_lbl = ::GetDlgItem(hwnd.Get(), 121);
        QIQI_TEST_CHECK(privacy_lbl != nullptr);
        const ScopedWindowDC lbl_dc(privacy_lbl);
        QIQI_TEST_CHECK(::SendMessageW(
                            hwnd.Get(),
                            WM_CTLCOLORSTATIC,
                            reinterpret_cast<WPARAM>(lbl_dc.Get()),
                            reinterpret_cast<LPARAM>(privacy_lbl)) != 0);
        QIQI_TEST_CHECK(::GetTextColor(lbl_dc.Get()) == RGB(196, 43, 28));

        // 1) User clicks Cancel/No on privacy confirmation -> checkbox is automatically unchecked
        ::CheckDlgButton(hwnd.Get(), 120, BST_CHECKED);
        ::SetTimer(nullptr, 8, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            if (const HWND msg_box = ::FindWindowW(L"#32770", nullptr)) {
                ::SendMessageW(msg_box, WM_COMMAND, MAKEWPARAM(IDNO, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(120, BN_CLICKED), 0);
        QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), 120) == BST_UNCHECKED);

        // 2) User clicks Yes on privacy confirmation -> checkbox stays checked
        ::CheckDlgButton(hwnd.Get(), 120, BST_CHECKED);
        ::SetTimer(nullptr, 9, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            if (const HWND msg_box = ::FindWindowW(L"#32770", nullptr)) {
                ::SendMessageW(msg_box, WM_COMMAND, MAKEWPARAM(IDYES, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(120, BN_CLICKED), 0);
        QIQI_TEST_CHECK(::IsDlgButtonChecked(hwnd.Get(), 120) == BST_CHECKED);
    }

    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    ::SendMessageW(hwnd.Get(), WM_CLOSE, 0, 0);
    ::SendMessageW(hwnd.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

    wchar_t title_buf[128]{};
    {
        // Verify English settings dialog title when created with UiLanguage::EnUs
        cfg.ui_language = UiLanguage::EnUs;
        const ScopedWindow hwnd_en(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_en.IsValid());
        ::GetWindowTextW(hwnd_en.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi Wubi IME - Settings");

        // Verify Chinese settings dialog title when created with UiLanguage::ZhCn
        cfg.ui_language = UiLanguage::ZhCn;
        const ScopedWindow hwnd_zh(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &cfg));
        QIQI_TEST_CHECK(hwnd_zh.IsValid());
        ::GetWindowTextW(hwnd_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi 五笔输入法 - 属性设置");

        // Exercise Direct2D Fluent UI card and toggle-switch painting in both Light and Dark themes
        for (const bool dark : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(hwnd_zh.Get(), WM_THEMECHANGED, 0, 0);
            ::ShowWindow(hwnd_zh.Get(), SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_zh.Get(), nullptr, TRUE);
            ::UpdateWindow(hwnd_zh.Get());
        }
        SetThemeDarkOverrideForTest(std::nullopt);

        // Verify live UI language switching via kIdComboUiLang (111) CBN_SELCHANGE
        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 2, 0); // EnUs
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi Wubi IME - Settings");

        ::SendDlgItemMessageW(hwnd_zh.Get(), 111, CB_SETCURSEL, 1, 0); // ZhCn
        ::SendMessageW(hwnd_zh.Get(), WM_COMMAND, MAKEWPARAM(111, CBN_SELCHANGE), 0);
        ::GetWindowTextW(hwnd_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"QiQi 五笔输入法 - 属性设置");

        // Test clicking the "About..." button (id=115) inside SettingsDialog (both with bound_config and nullptr)
        ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(nullptr, id);
            const HWND about_dlg = ::FindWindowW(kAboutWndClass, nullptr);
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
            const HWND about_dlg = ::FindWindowW(kAboutWndClass, nullptr);
            if (about_dlg) {
                ::SendMessageW(about_dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            }
        });
        ::SendMessageW(hwnd_null_cfg.Get(), WM_COMMAND, MAKEWPARAM(115, BN_CLICKED), 0);
    }

    // Destroy `hwnd` before testing modal ShowSettingsDialog so FindWindowW finds the active modal window
    {
        const ScopedWindow destroy_first = std::move(const_cast<ScopedWindow&>(hwnd));
    }

    // Test modal ShowSettingsDialog with auto-closing timer (IDOK -> returns true, IDCANCEL -> returns false)
    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

    ::SetTimer(nullptr, 1, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(L"QiQiIMESettingsDialogClass", nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(!ShowSettingsDialog(::GetModuleHandleW(nullptr), nullptr, cfg));

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
        ::GetWindowTextW(about_zh.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"关于 奇奇五笔");
        wchar_t btn_buf[64]{};
        ::GetDlgItemTextW(about_zh.Get(), 201, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"访问 GitHub");
        ::GetDlgItemTextW(about_zh.Get(), IDOK, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"确定");
        ::ShowWindow(about_zh.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_zh.Get(), nullptr, TRUE);
        ::UpdateWindow(about_zh.Get());
        ::SendMessageW(about_zh.Get(), WM_CLOSE, 0, 0);

        const ScopedWindow about_en(CreateAboutWindow(::GetModuleHandleW(nullptr), about_zh.Get(), UiLanguage::EnUs));
        QIQI_TEST_CHECK(about_en.IsValid());
        ::GetWindowTextW(about_en.Get(), title_buf, 128);
        QIQI_TEST_CHECK(std::wstring(title_buf) == L"About QiQi Wubi IME");
        ::GetDlgItemTextW(about_en.Get(), 201, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"Visit GitHub");
        ::GetDlgItemTextW(about_en.Get(), IDOK, btn_buf, 64);
        QIQI_TEST_CHECK(std::wstring(btn_buf) == L"OK");
        ::ShowWindow(about_en.Get(), SW_SHOWNOACTIVATE);
        ::InvalidateRect(about_en.Get(), nullptr, TRUE);
        ::UpdateWindow(about_en.Get());
        ::SendMessageW(about_en.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    }

    ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        const HWND dlg = ::FindWindowW(kAboutWndClass, nullptr);
        if (dlg) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(::GetModuleHandleW(nullptr), nullptr, UiLanguage::ZhCn));
}

} // namespace

void RunSettingsDialogTests() {
    TestSettingsDialogControls();
}

} // namespace qiqi::test
