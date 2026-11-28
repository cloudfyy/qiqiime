// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"
#include "common/support.h"
#include "common/dictcodec.h"
#include "common/dlgutil.h"
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
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <tuple>
#include <utility>

namespace qiqi::test {

namespace {

enum class FontMenuChoice { Cancel, FirstFont, More };

struct FontMenuProbe {
    FontMenuChoice choice = FontMenuChoice::Cancel;
    std::wstring_view first_family;
    std::wstring_view first_label;
    unsigned int opens = 0;
    bool chooser_called = false;
};

thread_local FontMenuProbe* font_menu_probe = nullptr;

#pragma warning(suppress : 26461) // The test callback must match the TrackPopupMenuEx signature.
BOOL WINAPI TrackCandidateFontMenuForTest(
    HMENU menu, UINT flags, int x, int y, HWND hwnd, LPTPMPARAMS placement) {
    using namespace settings_detail;
    QIQI_TEST_CHECK(font_menu_probe);
    ++font_menu_probe->opens;
    auto* context = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    QIQI_TEST_CHECK(context && context->active_font_menu && context->transfer_in_progress);
    RECT button{};
    QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(hwnd, kIdBtnCandidateFont), &button));
    QIQI_TEST_CHECK(placement && ::EqualRect(&placement->rcExclude, &button) &&
                    x == button.right && y == button.bottom && (flags & TPM_RETURNCMD));

    std::array<wchar_t, LF_FACESIZE> text{};
    const auto read_item = [&](UINT index) {
        text.fill(L'\0');
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_DATA;
        item.dwTypeData = text.data();
        item.cch = gsl::narrow_cast<UINT>(text.size());
        QIQI_TEST_CHECK(::GetMenuItemInfoW(menu, index, TRUE, &item));
        return item;
    };
    const ScopedWindowDC measure_dc(hwnd);
    QIQI_TEST_CHECK(measure_dc);
    const auto measure_padding = [&](const MENUITEMINFOW& item, HFONT font) {
        const ScopedSelectObject selected(measure_dc.Get(), font);
        QIQI_TEST_CHECK(selected);
        RECT label{};
        QIQI_TEST_CHECK(::DrawTextW(measure_dc.Get(), text.data(), -1, &label, DT_CALCRECT | DT_SINGLELINE));
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemID = item.wID;
        measure.itemData = item.dwItemData;
        QIQI_TEST_CHECK(::SendMessageW(hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)) == TRUE);
        return gsl::narrow_cast<LONG>(measure.itemWidth) - label.right;
    };
    struct FontNames {
        const wchar_t* en;
        const wchar_t* zh;
    };
    constexpr std::array expected{
        FontNames{L"Microsoft YaHei UI", L"微软雅黑 UI"}, FontNames{L"SimSun", L"宋体"},
        FontNames{L"DengXian", L"等线"}, FontNames{L"FangSong", L"仿宋"},
        FontNames{L"KaiTi", L"楷体"}, FontNames{L"SimHei", L"黑体"}};
    const auto language = GetSelectedUiLanguageFromCombo(hwnd, *context);
    UINT position = 0;
    UINT first_command = 0;
    std::optional<LONG> font_item_padding;
    for (const auto& names : expected) {
        if (!CandidateFontFamilyAvailable(names.en)) continue;
        const auto* label = LocalizedUiText(language, names.zh, names.en);
        const auto item = read_item(position++);
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == label && item.wID != 0);
        QIQI_TEST_CHECK(((item.fState & MFS_CHECKED) != 0) ==
                        (context->preview_candidate_font.family == names.en ||
                         context->preview_candidate_font.family == names.zh));
        const auto sample = CreateCandidateFont(
            {.family = std::wstring(names.en)}, settings_layout::kFontSize, context->d2d.dpi,
            context->d2d.display_preferences.text_scale);
        QIQI_TEST_CHECK(sample);
        const LONG padding = measure_padding(item, sample.Get());
        if (font_item_padding) QIQI_TEST_CHECK(padding == *font_item_padding);
        else font_item_padding = padding;
        if (first_command == 0) {
            first_command = item.wID;
            font_menu_probe->first_family = names.en;
            font_menu_probe->first_label = label;
        }
    }
    if (position > 0) {
        QIQI_TEST_CHECK((read_item(position++).fType & MFT_SEPARATOR) != 0);
    }
    const auto more = read_item(position++);
    QIQI_TEST_CHECK(::GetMenuItemCount(menu) == gsl::narrow_cast<int>(position));
    QIQI_TEST_CHECK(std::wstring_view(text.data()) ==
                    LocalizedUiText(language, L"更多…", L"More..."));
    QIQI_TEST_CHECK((more.fType & MFT_OWNERDRAW) && more.dwItemData != 0);
    if (font_item_padding) {
        QIQI_TEST_CHECK(measure_padding(more, context->font.Get()) == *font_item_padding);
    }

    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), 180, 40));
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(screen && dc && bitmap && selected);
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_MENU;
    draw.itemID = more.wID;
    draw.hDC = dc.Get();
    draw.rcItem = {0, 0, 180, 40};
    draw.itemData = more.dwItemData;
    const auto& appearance = context->d2d.appearance;
    QIQI_TEST_CHECK(::SendMessageW(hwnd, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)) == TRUE);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 2, 2) == (appearance.system_high_contrast
        ? ::GetSysColor(COLOR_MENU) : appearance.palette.window_bg));
    draw.itemState = ODS_SELECTED;
    QIQI_TEST_CHECK(::SendMessageW(hwnd, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)) == TRUE);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 2, 2) == (appearance.system_high_contrast
        ? ::GetSysColor(COLOR_HIGHLIGHT) : appearance.palette.selection_bg));

    switch (font_menu_probe->choice) {
        case FontMenuChoice::FirstFont: return gsl::narrow_cast<BOOL>(first_command);
        case FontMenuChoice::More: return gsl::narrow_cast<BOOL>(more.wID);
        case FontMenuChoice::Cancel:
            ::SetLastError(ERROR_SUCCESS);
            return 0;
    }
    QIQI_TEST_CHECK(false);
    return 0;
}

BOOL WINAPI ChooseAdvancedFontForTest(CHOOSEFONTW* dialog) {
    QIQI_TEST_CHECK(dialog && dialog->lpLogFont && font_menu_probe);
    font_menu_probe->chooser_called = true;
    dialog->iPointSize = 170;
    return TRUE;
}

void TestSettingsCandidateFontMenu() {
    using namespace settings_detail;
    const auto restore_contrast = gsl::finally([]() noexcept { SetHighContrastOverrideForTest(std::nullopt); });
    for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        SetHighContrastOverrideForTest(false);
        QiQiConfig config;
        config.ui_language = language;
        config.ui_theme = UiTheme::Light;
        const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
        QIQI_TEST_CHECK(dialog);
        auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
        QIQI_TEST_CHECK(context);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
        QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog.Get(), kIdBtnCandidateFont)) ==
                        LocalizedUiText(language, L"字体", L"Font"));
        FontMenuProbe probe;
        font_menu_probe = &probe;
        const auto reset_probe = gsl::finally([]() noexcept { font_menu_probe = nullptr; });
        const auto open = [&](FontMenuChoice choice) {
            probe.choice = choice;
            const bool applied = ShowSettingsCandidateFontMenu(
                dialog.Get(), *context, ChooseAdvancedFontForTest, TrackCandidateFontMenuForTest);
            QIQI_TEST_CHECK(!context->active_font_menu && !context->transfer_in_progress);
            return applied;
        };
        QIQI_TEST_CHECK(!open(FontMenuChoice::Cancel) && probe.opens == 1 &&
                        context->preview_candidate_font.family.empty());
        ::SendDlgItemMessageW(dialog.Get(), kIdComboUiTheme, CB_SETCURSEL, static_cast<WPARAM>(UiTheme::Dark), 0);
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(kIdComboUiTheme, CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(context->d2d.appearance.palette.is_dark);
        auto& draft = context->preview_candidate_font;
        draft.point_size = 22;
        draft.weight = CandidateFontWeight::Bold;
        draft.italic = true;
        if (!probe.first_family.empty()) {
            QIQI_TEST_CHECK(open(FontMenuChoice::FirstFont));
            QIQI_TEST_CHECK(draft.family == probe.first_family && draft.point_size == 22 &&
                            draft.weight == CandidateFontWeight::Bold && draft.italic);
            QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog.Get(), kIdBtnCandidateFont)).starts_with(probe.first_label));
        } else {
            QIQI_TEST_CHECK(!open(FontMenuChoice::FirstFont));
        }
        QIQI_TEST_CHECK(!probe.chooser_called);
        SetHighContrastOverrideForTest(true);
        ::SendMessageW(dialog.Get(), WM_SETTINGCHANGE, 0, 0);
        QIQI_TEST_CHECK(context->d2d.appearance.system_high_contrast);
        QIQI_TEST_CHECK(open(FontMenuChoice::More) && probe.chooser_called && draft.point_size == 17);
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        QIQI_TEST_CHECK(config.candidate_font_family.empty() && config.candidate_font_size == 0);
    }
}

struct FontDialogPlacementProbe {
    LPCFHOOKPROC position = nullptr;
    HWND owner = nullptr;
    ULONGLONG deadline = 0;
    bool observed = false;
};

thread_local FontDialogPlacementProbe* font_dialog_placement_probe = nullptr;
constexpr UINT_PTR kFontDialogPlacementTimer = 0x514346;

UINT_PTR CALLBACK CheckFontDialogPlacement(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* const probe = font_dialog_placement_probe;
    QIQI_TEST_CHECK(probe && probe->position && probe->owner);
    if (message != WM_TIMER || wparam != kFontDialogPlacementTimer) {
        const auto handled = probe->position(hwnd, message, wparam, lparam);
        if (message == WM_INITDIALOG) {
            QIQI_TEST_CHECK(::SetTimer(hwnd, kFontDialogPlacementTimer, 10, nullptr) != 0);
        }
        return handled;
    }
    if (!::IsWindowVisible(hwnd)) {
        QIQI_TEST_CHECK(::GetTickCount64() < probe->deadline);
        return TRUE;
    }
    QIQI_TEST_CHECK(::KillTimer(hwnd, kFontDialogPlacementTimer));
    RECT parent{}, dialog{};
    QIQI_TEST_CHECK(::GetWindowRect(probe->owner, &parent) && ::GetWindowRect(hwnd, &dialog));
    const RECT work = QueryActiveMonitor(probe->owner).work_area;
    const LONG width = dialog.right - dialog.left;
    const LONG height = dialog.bottom - dialog.top;
    const LONG expected_left = std::clamp(parent.left + (parent.right - parent.left - width) / 2,
        work.left, std::max(work.left, work.right - width));
    const LONG expected_top = std::clamp(parent.top + (parent.bottom - parent.top - height) / 2,
        work.top, std::max(work.top, work.bottom - height));
    QIQI_TEST_CHECK(std::abs(dialog.left - expected_left) <= 1 && std::abs(dialog.top - expected_top) <= 1);
    probe->observed = true;
    QIQI_TEST_CHECK(::PostMessageW(hwnd, WM_COMMAND, IDCANCEL, 0));
    return TRUE;
}

BOOL WINAPI ChooseFontAndCheckPlacement(CHOOSEFONTW* choice) {
    QIQI_TEST_CHECK(choice && (choice->Flags & CF_ENABLEHOOK) != 0 &&
                    choice->lpfnHook && font_dialog_placement_probe);
    font_dialog_placement_probe->position = choice->lpfnHook;
    font_dialog_placement_probe->owner = choice->hwndOwner;
    font_dialog_placement_probe->deadline = ::GetTickCount64() + 5000;
    choice->lpfnHook = CheckFontDialogPlacement;
    return ::ChooseFontW(choice);
}

void TestSettingsCandidateFontDialogPlacement() {
    using namespace settings_detail;
    QiQiConfig config;
    const ScopedWindow owner(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(owner);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(owner.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    ::ShowWindow(owner.Get(), SW_SHOWNOACTIVATE);
    const MonitorMetrics monitor = QueryActiveMonitor(owner.Get());
    FontDialogPlacementProbe probe;
    font_dialog_placement_probe = &probe;
    const auto reset_probe = gsl::finally([]() noexcept { font_dialog_placement_probe = nullptr; });
    for (const bool near_edge : {false, true}) {
        if (near_edge) {
            QIQI_TEST_CHECK(::SetWindowPos(owner.Get(), nullptr, monitor.work_area.left, monitor.work_area.top,
                ScalePxByDpi(320, monitor.dpi), ScalePxByDpi(280, monitor.dpi),
                SWP_NOZORDER | SWP_NOACTIVATE));
        }
        probe.observed = false;
        QIQI_TEST_CHECK(!PromptSettingsCandidateFont(owner.Get(), *context, ChooseFontAndCheckPlacement));
        QIQI_TEST_CHECK(probe.observed && context->preview_candidate_font.family.empty());
    }
}

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
    const auto check_candidate_preview = [&](COLORREF expected) {
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
        const HWND preview = ::GetDlgItem(dialog.Get(), kIdCandidatePreview);
        EnsureSettingsControlVisible(dialog.Get(), preview, *context);
        RECT visible{};
        QIQI_TEST_CHECK(::GetWindowRect(preview, &visible));
        ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&visible), 2);
        QIQI_TEST_CHECK(visible.top >= context->page_bounds.top &&
                        visible.bottom <= context->page_bounds.bottom);
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
        QIQI_TEST_CHECK(::SetPixelV(preview_dc.Get(), 2, 2, expected ^ RGB(255, 255, 255)));
        QIQI_TEST_CHECK(::SendMessageW(dialog.Get(), WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item)) == TRUE);
        QIQI_TEST_CHECK(::GetPixel(preview_dc.Get(), 2, 2) == expected);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
    };
    {
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
                    check_candidate_preview(GetThemePalette(app_dark).window_bg);
                }
            }
        }
        SetHighContrastOverrideForTest(true);
        ::SendMessageW(dialog.Get(), WM_SETTINGCHANGE, 0, 0);
        QIQI_TEST_CHECK(context->d2d.appearance.system_high_contrast);
        check_candidate_preview(::GetSysColor(COLOR_WINDOW));
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

} // namespace

void RunSettingsToggleLabelTests() {
    using namespace settings_detail;
    SetHighContrastOverrideForTest(false);
    const auto restore_contrast = gsl::finally([]() noexcept {
        SetHighContrastOverrideForTest(std::nullopt);
    });
    QiQiConfig config;
    config.ui_language = UiLanguage::ZhCn;
    config.ui_theme = UiTheme::Light;
    config.ui_text_scale = UiTextScale::Percent150;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    auto* const context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context && context->d2d.d2d_target && context->d2d.fmt_body);
    const auto& theme = context->d2d;
    const RECT row{0, 0, theme.ScalePx(280), theme.ScalePx(34)};
    const RECT sample{FluentToggleLabelInset(theme), 0,
                      FluentToggleBounds(row, theme).left - theme.ScalePx(4), row.bottom};
    const RECT reference{sample.left, row.bottom, sample.right, 2 * row.bottom};
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), row.right, reference.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap && theme.surface_brush);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(selected);

    for (const int id : {kIdChkSingleCharacter, kIdChkCodeHint, kIdChkUpDownPageTurn,
                         kIdChkBoldUnderline, kIdChkStatusBar}) {
        const HWND child = ::GetDlgItem(dialog.Get(), id);
        std::array<wchar_t, 128> text{};
        const int length = ::GetWindowTextW(child, text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(length > 0);
        const std::wstring_view label(text.data(), gsl::narrow_cast<size_t>(length));
        if (id == kIdChkStatusBar) QIQI_TEST_CHECK(label == L"显示状态条");
        const DRAWITEMSTRUCT item{.CtlType = ODT_BUTTON, .hwndItem = child, .hDC = dc.Get(), .rcItem = row};
        DrawFluentToggleOrRadioRow(item, theme, false);
        QIQI_TEST_CHECK(::FillRect(dc.Get(), &reference, theme.surface_brush.Get()));
        dialog_detail::DrawDialogText(dc.Get(), context->font.Get(), theme.appearance.palette.text_primary,
            reference, label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        bool text_visible = false;
        for (LONG y = sample.top; y < sample.bottom; ++y) {
            for (LONG x = sample.left; x < sample.right; ++x) {
                const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                QIQI_TEST_CHECK(pixel == ::GetPixel(dc.Get(), x, y + row.bottom));
                text_visible |= pixel != theme.appearance.palette.surface_bg;
            }
        }
        QIQI_TEST_CHECK(text_visible);
    }

    const auto check_switch = [&](int id, bool QiQiConfig::* field) {
        const HWND button = ::GetDlgItem(dialog.Get(), id);
        EnsureSettingsControlVisible(dialog.Get(), button, *context);
        const bool initial = config.*field;
        const auto checked = [&] { return ::IsDlgButtonChecked(dialog.Get(), id) == BST_CHECKED; };
        QIQI_TEST_CHECK(checked() == initial);
        RECT client{};
        QIQI_TEST_CHECK(::GetClientRect(button, &client));
        const RECT toggle = FluentToggleBounds(client, theme);
        const LONG y = (toggle.top + toggle.bottom) / 2;
        const auto label_x = gsl::narrow<short>(FluentToggleLabelInset(theme) + 1);
        const LPARAM label_point = MAKELPARAM(label_x, y);
        const LPARAM switch_point = MAKELPARAM((toggle.left + toggle.right) / 2, y);
        const auto click = [&](LPARAM down, LPARAM up) {
            ::SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, down);
            ::SendMessageW(button, WM_LBUTTONUP, 0, up);
        };
        for (const auto& points : {std::pair{label_point, label_point},
                                   std::pair{switch_point, label_point},
                                   std::pair{label_point, switch_point}}) {
            click(points.first, points.second);
            QIQI_TEST_CHECK(checked() == initial);
        }
        click(switch_point, switch_point);
        QiQiConfig draft;
        ReadSettingsFromWindowControls(dialog.Get(), draft);
        QIQI_TEST_CHECK(draft.*field != initial && config.*field == initial);
        ::SendMessageW(button, WM_KEYDOWN, VK_SPACE, 0);
        ::SendMessageW(button, WM_KEYUP, VK_SPACE, 0);
        QIQI_TEST_CHECK(checked() == initial);
        ::SendMessageW(button, BM_CLICK, 0, 0);
        QIQI_TEST_CHECK(checked() != initial);
        ::SendMessageW(button, BM_CLICK, 0, 0);
        QIQI_TEST_CHECK(checked() == initial);
    };
    for (const bool system_high_contrast : {false, true}) {
        SetHighContrastOverrideForTest(system_high_contrast);
        ::SendMessageW(dialog.Get(), WM_SETTINGCHANGE, 0, 0);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
        check_switch(kIdChkStatusBar, &QiQiConfig::show_status_bar);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Feedback);
        check_switch(kIdChkBeepEmpty4, &QiQiConfig::beep_on_empty_4code);
        check_switch(kIdChkBeepDup4, &QiQiConfig::beep_on_dup_4code);
        check_switch(kIdChkClearEmpty4, &QiQiConfig::clear_on_empty_4code);
        check_switch(kIdChkKeySound, &QiQiConfig::enable_key_sound);
    }
}

namespace {

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
    TestSettingsCandidateFontMenu();
    TestSettingsCandidateFontDialogPlacement();
    TestSettingsDisplayPreview();
    TestSettingsAccessibleLayout();
}

} // namespace qiqi::test
