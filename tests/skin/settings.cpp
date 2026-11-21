// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/uiutil.h"

namespace qiqi::test {
namespace {

thread_local bool picker_seen = false;
thread_local bool accept_skin = false;

bool CandidatePreviewContainsColor(HWND settings, int control_id, COLORREF expected) {
    const HWND preview = ::GetDlgItem(settings, control_id);
    RECT bounds{};
    QIQI_TEST_CHECK(preview && ::GetClientRect(preview, &bounds));
    const ScopedWindowDC screen(preview);
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(
        screen.Get(), bounds.right, bounds.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject selected_bitmap(dc.Get(), bitmap.Get());
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_STATIC;
    item.CtlID = gsl::narrow_cast<UINT>(control_id);
    item.hwndItem = preview;
    item.hDC = dc.Get();
    item.rcItem = bounds;
    ::SendMessageW(settings, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item));
    for (int y = bounds.top; y < bounds.bottom; ++y) {
        for (int x = bounds.left; x < bounds.right; ++x) {
            if (::GetPixel(dc.Get(), x, y) == expected) return true;
        }
    }
    return false;
}

void ChooseFromMore(HWND settings, bool accept) {
    using namespace settings_detail;
    picker_seen = false;
    accept_skin = accept;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND picker = FindThreadWindowByClass(kSkinPickerWndClass);
        if (!picker) return;
        ::KillTimer(nullptr, id);
        picker_seen = true;
        QIQI_TEST_CHECK(::GetWindow(picker, GW_OWNER) != nullptr);
        QIQI_TEST_CHECK(!::IsWindowEnabled(::GetWindow(picker, GW_OWNER)));
        const HWND list = ::GetDlgItem(picker, kIdSkinPickerList);
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 22);
        constexpr int selected = gsl::narrow_cast<int>(ImeSkinId::VioletPath);
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
        std::array<wchar_t, 128> name{};
        ListView_GetItemText(list, selected, 0, name.data(), gsl::narrow_cast<int>(name.size()));
        QIQI_TEST_CHECK(std::wstring_view(name.data()) == L"Violet Path");
        RECT area{};
        RECT row{};
        QIQI_TEST_CHECK(::GetClientRect(list, &area) &&
            GetListViewItemBounds(list, selected, row));
        const ScopedWindowDC screen(list);
        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
        const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), area.right, area.bottom));
        QIQI_TEST_CHECK(screen && dc && bitmap);
        const ScopedSelectObject selected_bitmap(dc.Get(), bitmap.Get());
        ::SendMessageW(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(list, WM_GETFONT, 0, 0));
        const ScopedSelectObject selected_font(dc.Get(), font);
        TEXTMETRICW metrics{};
        QIQI_TEST_CHECK(::GetTextMetricsW(dc.Get(), &metrics));
        const int gap = ScalePxByDpi(8, QueryActiveMonitor(picker).dpi);
        ImeSkinCatalog catalog;
        QIQI_TEST_CHECK(LoadImeSkinCatalog(catalog) == S_OK);
        const auto* skin = FindImeSkin(catalog, ImeSkinId::VioletPath);
        QIQI_TEST_CHECK(skin && ::GetPixel(dc.Get(), row.left + gap + 2,
            row.top + metrics.tmHeight + 2 * gap + 2) == skin->palettes.light.Get(SkinColor::Background));
        ::SendMessageW(picker, WM_COMMAND, accept_skin ? IDOK : IDCANCEL, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto stop = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    const auto* const context =
        reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(settings, GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    ::SendDlgItemMessageW(settings, kIdComboImeSkin, CB_SETCURSEL, context->quick_skin_count, 0);
    ::SendMessageW(settings, WM_COMMAND, MAKEWPARAM(kIdComboImeSkin, CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(picker_seen && !FindThreadWindowByClass(kSkinPickerWndClass));
    QIQI_TEST_CHECK(::IsWindowEnabled(settings));
}

} // namespace

void RunSettingsSkinTests() {
    using namespace settings_detail;
    const auto old_app_dark = detail::ThemeDarkOverrideStorage();
    const auto old_windows_dark = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(old_app_dark);
        SetTaskbarDarkOverrideForTest(old_windows_dark);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    SetHighContrastOverrideForTest(false);
    const std::wstring subkey = L"Software\\QiQiIME_SkinUiTest_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(::GetTickCount64());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    QiQiConfig config;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.ui_language = UiLanguage::EnUs;
    config.ui_theme = UiTheme::Light;
    QIQI_TEST_CHECK(SaveConfig(config));
    const auto sequence = QueryRegistryConfigSequence();
    auto window = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true);
    QIQI_TEST_CHECK(window);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(window.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context && context->quick_skin_count == 6);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(window.Get(), kIdComboImeSkin, CB_GETCOUNT, 0, 0) == 7);
    SelectSettingsPage(window.Get(), *context, SettingsPage::Display);
    ::SendDlgItemMessageW(window.Get(), kIdComboImeSkin, CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(kIdComboImeSkin, CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(context->preview_skin_id == ImeSkinId::Firefly && config.ime_skin_id == ImeSkinId::Default);
    QIQI_TEST_CHECK(context->d2d.appearance.palette.window_bg == kLightThemePalette.window_bg);
    const auto& firefly = GetBuiltinImeSkins().front().palettes.light;
    QIQI_TEST_CHECK(context->candidate_skin_colors == firefly);
    for (const auto role : {SkinColor::Background, SkinColor::CodeText, SkinColor::CandidateText,
            SkinColor::SelectedBackground, SkinColor::SelectedText, SkinColor::SelectedBorder,
            SkinColor::PageEnabledText}) {
        QIQI_TEST_CHECK(CandidatePreviewContainsColor(
            window.Get(), kIdCandidatePreview, firefly.Get(role)));
    }
    const auto select_theme = [&](UiTheme theme) {
        ::SendDlgItemMessageW(window.Get(), kIdComboUiTheme, CB_SETCURSEL,
            gsl::narrow_cast<WPARAM>(theme), 0);
        ::SendMessageW(window.Get(), WM_COMMAND,
            MAKEWPARAM(kIdComboUiTheme, CBN_SELCHANGE), 0);
    };
    select_theme(UiTheme::System);
    for (const bool app_dark : {false, true}) {
        for (const bool windows_dark : {false, true}) {
            SetThemeDarkOverrideForTest(app_dark);
            SetTaskbarDarkOverrideForTest(windows_dark);
            for (const UINT message : {WM_THEMECHANGED, WM_SETTINGCHANGE}) {
                ::SendMessageW(window.Get(), message, 0, 0);
                const SkinPalette& expected = app_dark
                    ? GetBuiltinImeSkins().front().palettes.dark
                    : GetBuiltinImeSkins().front().palettes.light;
                QIQI_TEST_CHECK(context->candidate_skin_colors == expected);
                QIQI_TEST_CHECK(CandidatePreviewContainsColor(
                    window.Get(), kIdCandidatePreview,
                    expected.Get(SkinColor::Background)));
            }
        }
    }
    for (const auto theme : {UiTheme::Light, UiTheme::Dark}) {
        select_theme(theme);
        const SkinPalette& expected = theme == UiTheme::Dark
            ? GetBuiltinImeSkins().front().palettes.dark
            : GetBuiltinImeSkins().front().palettes.light;
        QIQI_TEST_CHECK(context->candidate_skin_colors == expected);
    }
    select_theme(UiTheme::Light);
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    SetHighContrastOverrideForTest(true);
    ::SendMessageW(window.Get(), WM_SETTINGCHANGE, 0, 0);
    QIQI_TEST_CHECK(context->candidate_skin_colors.Get(SkinColor::Background) ==
        ::GetSysColor(COLOR_WINDOW));
    SetHighContrastOverrideForTest(false);
    ::SendMessageW(window.Get(), WM_THEMECHANGED, 0, 0);
    QIQI_TEST_CHECK(context->candidate_skin_colors == firefly);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence);
    ChooseFromMore(window.Get(), false);
    QIQI_TEST_CHECK(context->preview_skin_id == ImeSkinId::Firefly && QueryRegistryConfigSequence() == sequence);
    ChooseFromMore(window.Get(), true);
    QIQI_TEST_CHECK(context->preview_skin_id == ImeSkinId::VioletPath && context->quick_skin_count == 7);
    QIQI_TEST_CHECK(::SendDlgItemMessageW(window.Get(), kIdComboImeSkin, CB_GETCOUNT, 0, 0) == 8);
    QIQI_TEST_CHECK(config.ime_skin_id == ImeSkinId::Default && QueryRegistryConfigSequence() == sequence);
    ::SendMessageW(window.Get(), WM_COMMAND, IDCANCEL, 0);
    QIQI_TEST_CHECK(config.ime_skin_id == ImeSkinId::Default && QueryRegistryConfigSequence() == sequence);
    window.Reset();
    window = CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true);
    QIQI_TEST_CHECK(window);
    ::SendDlgItemMessageW(window.Get(), kIdComboImeSkin, CB_SETCURSEL, 5, 0);
    ::SendMessageW(window.Get(), WM_COMMAND, MAKEWPARAM(kIdComboImeSkin, CBN_SELCHANGE), 0);
    ::SendMessageW(window.Get(), WM_COMMAND, IDOK, 0);
    QIQI_TEST_CHECK(config.ime_skin_id == ImeSkinId::PeachBlossom && config.ui_theme == UiTheme::Light);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence + 1);
    QiQiConfig stored;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(stored) && stored.ime_skin_id == config.ime_skin_id);
}

} // namespace qiqi::test
