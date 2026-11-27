// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"
#include "common/support.h"
#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "ime/engine/dict.h"
#include "tools/settings/internal.h"
#include "tools/settings/window.h"
#include "common/dictui.h"

#include <windows.h>
#include <array>
#include <string>
#include <tuple>
#include <utility>

namespace qiqi::test {

namespace {

void TestSettingsDisplayPreview() {
    using namespace settings_detail;
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetHighContrastOverrideForTest(false);
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.page_size = 8;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    const HWND tabs = ::GetDlgItem(dialog.Get(), kIdSettingsTabs);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Display), 0);
    RECT tab_bounds{};
    QIQI_TEST_CHECK(::GetClientRect(tabs, &tab_bounds));
    const ScopedWindowDC tab_dc(tabs);
    const ScopedMemoryDC painted(::CreateCompatibleDC(tab_dc.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(tab_dc.Get(), tab_bounds.right, tab_bounds.bottom));
    QIQI_TEST_CHECK(painted && bitmap);
    const ScopedSelectObject selected_bitmap(painted.Get(), bitmap.Get());
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTheme, CB_GETCOUNT, 0, 0) == 4);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTextScale, CB_GETCOUNT, 0, 0) == 4);
    const auto select = [&](int id, int value) {
        ::SendDlgItemMessageW(dialog.Get(), id, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(value), 0);
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), 0);
    };
    {
        const HWND preview = ::GetDlgItem(dialog.Get(), kIdCandidatePreview);
        RECT preview_bounds{};
        QIQI_TEST_CHECK(::GetClientRect(preview, &preview_bounds));
        const ScopedMemoryDC preview_dc(::CreateCompatibleDC(tab_dc.Get()));
        const ScopedGdiHandle<HBITMAP> preview_bitmap(::CreateCompatibleBitmap(
            tab_dc.Get(), preview_bounds.right, preview_bounds.bottom));
        QIQI_TEST_CHECK(preview_dc && preview_bitmap);
        const ScopedSelectObject select_preview_bitmap(preview_dc.Get(), preview_bitmap.Get());
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_STATIC;
        item.CtlID = kIdCandidatePreview;
        item.hwndItem = preview;
        item.hDC = preview_dc.Get();
        item.rcItem = preview_bounds;
        for (const bool app_dark : {false, true}) {
            for (const bool windows_dark : {false, true}) {
                SetThemeDarkOverrideForTest(app_dark);
                SetTaskbarDarkOverrideForTest(windows_dark);
                for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                    ::SendMessageW(dialog.Get(), message, 0, 0);
                    QIQI_TEST_CHECK(context->d2d.appearance.palette.is_dark == app_dark);
                    ::SendMessageW(tabs, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(painted.Get()), PRF_CLIENT);
                    // TCM_GETITEMRECT writes through LPARAM.
#pragma warning(suppress : 26496)
                    RECT unselected{};
                    QIQI_TEST_CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&unselected)));
                    QIQI_TEST_CHECK(::GetPixel(painted.Get(), unselected.left + 4, unselected.top + 4) ==
                                    GetThemePalette(app_dark).window_bg);
                    SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
                    ::SendMessageW(dialog.Get(), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
                    QIQI_TEST_CHECK(::GetPixel(preview_dc.Get(), 2, 2) == GetThemePalette(app_dark).window_bg);
                    SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
                }
            }
        }
        SetHighContrastOverrideForTest(true);
        ::SendMessageW(dialog.Get(), WM_SETTINGCHANGE, 0, 0);
        QIQI_TEST_CHECK(context->d2d.appearance.system_high_contrast);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
        ::SendMessageW(dialog.Get(), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
        QIQI_TEST_CHECK(::GetPixel(preview_dc.Get(), 2, 2) == ::GetSysColor(COLOR_WINDOW));
        SetHighContrastOverrideForTest(false);
        ::SendMessageW(dialog.Get(), WM_THEMECHANGED, 0, 0);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
    }
    QiQiConfig edited;
    for (const UiTheme theme : {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
        select(kIdComboUiTheme, static_cast<int>(theme));
        ReadSettingsFromWindowControls(dialog.Get(), edited);
        QIQI_TEST_CHECK(edited.ui_theme == theme);
        QIQI_TEST_CHECK(context->d2d.display_preferences.theme == theme);
        QIQI_TEST_CHECK(config.ui_theme == UiTheme::System);
        QIQI_TEST_CHECK(context->active_page == SettingsPage::Display);
        ::SendMessageW(tabs, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(painted.Get()), PRF_CLIENT);
        // TCM_GETITEMRECT writes through LPARAM; CppCoreCheck cannot see that mutation.
#pragma warning(suppress : 26496)
        RECT unselected{}, selected{};
        QIQI_TEST_CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&unselected)));
        QIQI_TEST_CHECK(::SendMessageW(tabs, TCM_GETITEMRECT, static_cast<WPARAM>(SettingsPage::Display),
                                     reinterpret_cast<LPARAM>(&selected)));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), unselected.left + 4, unselected.top + 4) ==
                        context->d2d.appearance.palette.window_bg);
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), selected.left + 4, selected.top + 4) ==
                        context->d2d.appearance.palette.selection_bg);
    }
    const HWND comfort = ::GetDlgItem(dialog.Get(), kIdChkEasyReading);
    std::array<wchar_t, 128> comfort_label{};
    ::GetWindowTextW(comfort, comfort_label.data(), gsl::narrow_cast<int>(comfort_label.size()));
    QIQI_TEST_CHECK(std::wstring(comfort_label.data()) == L"Comfort mode");
    ::SendMessageW(comfort, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(8, 8));
    ::SendMessageW(comfort, WM_LBUTTONUP, 0, MAKELPARAM(8, 8));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkEasyReading) == BST_UNCHECKED);
    RECT comfort_client{};
    QIQI_TEST_CHECK(::GetClientRect(comfort, &comfort_client));
    const RECT switch_bounds = FluentToggleBounds(comfort_client, context->d2d);
    const LPARAM switch_point = MAKELPARAM(
        (switch_bounds.left + switch_bounds.right) / 2, (switch_bounds.top + switch_bounds.bottom) / 2);
    ::SendMessageW(comfort, WM_LBUTTONDOWN, MK_LBUTTON, switch_point);
    ::SendMessageW(comfort, WM_LBUTTONUP, 0, switch_point);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkEasyReading) == BST_CHECKED);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdChkEasyReading, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent150);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent100));
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent150);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(dialog.Get(), kIdComboUiTextScale, CB_SETCURSEL,
        static_cast<WPARAM>(UiTextScale::Percent125), 0) == CB_ERR);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::CheckDlgButton(dialog.Get(), kIdChkEasyReading, BST_UNCHECKED);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdChkEasyReading, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(!edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent200);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent100));
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(!edited.easy_reading && edited.ui_text_scale == UiTextScale::Percent100);
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::SendDlgItemMessageW(dialog.Get(), kIdComboPageSize, CB_SETCURSEL, 5, 0);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Candidates), 0);
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(SettingsPage::Display), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.page_size == 6 && edited.ui_text_scale == UiTextScale::Percent200);
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdBtnRestoreDefaults, BN_CLICKED), 0);
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    const QiQiConfig defaults;
    QIQI_TEST_CHECK(GetDisplayPreferences(edited) == GetDisplayPreferences(defaults));
    QIQI_TEST_CHECK(context->d2d.display_preferences == GetDisplayPreferences(defaults));
    QIQI_TEST_CHECK(edited.page_size == defaults.page_size && edited.ui_language == defaults.ui_language);
    QIQI_TEST_CHECK(edited.primary_dict == defaults.primary_dict);
    select(kIdComboUiTheme, static_cast<int>(UiTheme::Dark));
    select(kIdComboUiTextScale, static_cast<int>(UiTextScale::Percent200));
    ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
    QIQI_TEST_CHECK(GetDisplayPreferences(config) == DisplayPreferences{});
    QIQI_TEST_CHECK(config.page_size == 8);

    const ScopedWindow accepted(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(accepted);
    ::SendDlgItemMessageW(accepted.Get(), kIdComboUiTheme, CB_SETCURSEL,
                         static_cast<WPARAM>(UiTheme::Dark), 0);
    ::SendDlgItemMessageW(accepted.Get(), kIdComboUiTextScale, CB_SETCURSEL,
                         static_cast<WPARAM>(UiTextScale::Percent125), 0);
    ::CheckDlgButton(accepted.Get(), kIdChkEasyReading, BST_CHECKED);
    QIQI_TEST_CHECK(GetDisplayPreferences(config) == DisplayPreferences{});
    ::SendMessageW(accepted.Get(), WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
    QIQI_TEST_CHECK(config.ui_theme == UiTheme::Dark &&
                    config.ui_text_scale == UiTextScale::Percent150 && config.easy_reading);
}

void TestSettingsAccessibleLayout() {
    using namespace settings_detail;
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    for (const UINT dpi : {96u, 144u, 192u}) {
        for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 800, 600}, dpi});
            QiQiConfig config;
            config.ui_language = language;
            config.ui_text_scale = UiTextScale::Percent200;
            config.easy_reading = true;
            const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
            QIQI_TEST_CHECK(dialog);
            auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
            QIQI_TEST_CHECK(context && context->content_height > context->viewport_height);
            RECT client{};
            ::GetClientRect(dialog.Get(), &client);
            const auto bounds = [&](int id) {
                RECT rect{};
                QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), id), &rect));
                ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&rect), 2);
                return rect;
            };
            const RECT ok = bounds(IDOK);
            const RECT cancel = bounds(IDCANCEL);
            const RECT space_enter_label = bounds(kIdLblSpaceEnter);
            const RECT space_enter = bounds(kIdComboSpaceEnter);
            QIQI_TEST_CHECK(space_enter_label.left ==
                bounds(kIdChkShiftToggle).left + FluentToggleLabelInset(context->d2d));
            QIQI_TEST_CHECK(space_enter_label.bottom <= space_enter.top &&
                space_enter.left >= context->page_bounds.left && space_enter.right <= context->page_bounds.right);
            QIQI_TEST_CHECK(ok.left >= 0 && cancel.right <= client.right);
            QIQI_TEST_CHECK(ok.top >= context->page_bounds.bottom && ok.bottom <= client.bottom);
            QIQI_TEST_CHECK(cancel.top >= context->page_bounds.bottom && cancel.bottom <= client.bottom);
            QIQI_TEST_CHECK(ok.right <= cancel.left);
            QIQI_TEST_CHECK(ok.bottom - ok.top >= ScalePxByDpi(44, dpi));
            const int before_footer_focus = context->scroll_offset;
            ::SendDlgItemMessageW(dialog.Get(), kIdBtnRestoreDefaults, WM_SETFOCUS, 0, 0);
            QIQI_TEST_CHECK(context->scroll_offset == before_footer_focus);
            QIQI_TEST_CHECK(bounds(kIdBtnRestoreDefaults).top >= context->page_bounds.bottom);
            ::SendDlgItemMessageW(dialog.Get(), kIdComboEnglishSwitch, WM_SETFOCUS, 0, 0);
            const RECT focused = bounds(kIdComboEnglishSwitch);
            QIQI_TEST_CHECK(context->scroll_offset > 0);
            QIQI_TEST_CHECK(focused.top < context->page_bounds.bottom && focused.bottom > context->page_bounds.top);
            if (focused.bottom - focused.top <= context->viewport_height - ScalePxByDpi(8, dpi)) {
                QIQI_TEST_CHECK(focused.top >= context->page_bounds.top && focused.bottom <= context->page_bounds.bottom);
            }
            QIQI_TEST_CHECK(bounds(IDOK).top == ok.top);
            const int input_scroll = context->scroll_offset;
            ::SendDlgItemMessageW(dialog.Get(), kIdSettingsTabs, TCM_SETCURSEL,
                                 static_cast<WPARAM>(SettingsPage::Display), 0);
            QIQI_TEST_CHECK(context->active_page == SettingsPage::Display);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdChkAutoCommit4), GWL_STYLE) & WS_VISIBLE) == 0);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdChkEasyReading), GWL_STYLE) & WS_VISIBLE) != 0);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdComboImeSkin), GWL_STYLE) & WS_VISIBLE) == 0);
            QIQI_TEST_CHECK(bounds(kIdChkEasyReading).bottom - bounds(kIdChkEasyReading).top >= ScalePxByDpi(44, dpi));
            QIQI_TEST_CHECK(bounds(kIdComboUiTheme).bottom <= bounds(kIdChkEasyReading).top);
            QIQI_TEST_CHECK(bounds(kIdChkEasyReading).bottom <= bounds(kIdLblUiTextScale).top);
            QIQI_TEST_CHECK(bounds(kIdComboUiTextScale).bottom <= bounds(kIdLblUiLang).top);
            QIQI_TEST_CHECK(bounds(kIdSettingsTabs).bottom <= context->page_bounds.top);
            ::SendMessageW(dialog.Get(), kSettingsEnsureFocusVisible,
                           reinterpret_cast<WPARAM>(::GetDlgItem(dialog.Get(), kIdChkStatusBar)), 0);
            QIQI_TEST_CHECK(bounds(kIdChkStatusBar).bottom <= context->page_bounds.bottom);
            QIQI_TEST_CHECK(bounds(IDOK).top == ok.top);
            SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdComboImeSkin),
                GWL_STYLE) & WS_VISIBLE) != 0);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdBtnCandidateFont),
                GWL_STYLE) & WS_VISIBLE) != 0);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdCandidatePreview),
                GWL_STYLE) & WS_VISIBLE) != 0);
            ::SendDlgItemMessageW(dialog.Get(), kIdSettingsTabs, TCM_SETCURSEL,
                                 static_cast<WPARAM>(SettingsPage::Input), 0);
            QIQI_TEST_CHECK(context->scroll_offset == input_scroll);
            QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(
                dialog.Get(), kIdComboImeSkin), GWL_STYLE) & WS_VISIBLE) == 0);
            for (int page = 0; page < static_cast<int>(SettingsPage::Count); ++page) {
                SelectSettingsPage(dialog.Get(), *context, static_cast<SettingsPage>(page));
                for (const int id : SettingsPageControls(static_cast<SettingsPage>(page))) {
                    if (id == kIdDisplayExplanation) continue;
                    QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), id), GWL_STYLE) & WS_VISIBLE) != 0);
                }
                for (const int id : kSettingsFooterControls) {
                    QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), id), GWL_STYLE) & WS_VISIBLE) != 0);
                    QIQI_TEST_CHECK(bounds(id).top >= context->page_bounds.bottom);
                    QIQI_TEST_CHECK(bounds(id).bottom <= client.bottom);
                }
            }
        }
    }
}

} // namespace

void RunSettingsDisplayTests() {
    TestSettingsDisplayPreview();
    TestSettingsAccessibleLayout();
}

} // namespace qiqi::test
