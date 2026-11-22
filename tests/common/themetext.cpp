// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test::controltest {
namespace {

struct FailedSelectObjectTraits : SelectObjectGuardTraits {
    inline static unsigned int releases = 0;
    static HGDIOBJ Acquire(HDC, HGDIOBJ) noexcept {
        ::SetLastError(ERROR_INVALID_HANDLE);
        return HGDI_ERROR;
    }
    static void Release(HDC, HGDIOBJ) noexcept { ++releases; }
};

void TestFailedGdiSelection() {
    const ScopedLastErrorRestore preserve_error;
    FailedSelectObjectTraits::releases = 0;
    {
        const ScopedContextGuard<FailedSelectObjectTraits> selection(nullptr, nullptr);
        QIQI_TEST_CHECK(!selection.IsValid() && selection.Error() == ERROR_INVALID_HANDLE);
        ::SetLastError(ERROR_ACCESS_DENIED);
    }
    QIQI_TEST_CHECK(FailedSelectObjectTraits::releases == 0);
    QIQI_TEST_CHECK(::GetLastError() == ERROR_ACCESS_DENIED);
}

void TestDisplayPreferences() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore = gsl::finally([original_theme, original_taskbar]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetHighContrastOverrideForTest(false);
    const auto luminance = [](COLORREF color) {
        const auto channel = [](BYTE value) {
            const double component = static_cast<double>(value) / 255.0;
            return component <= 0.04045 ? component / 12.92 : std::pow((component + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(GetRValue(color)) + 0.7152 * channel(GetGValue(color)) +
               0.0722 * channel(GetBValue(color));
    };
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        SetTaskbarDarkOverrideForTest(!dark);
        QIQI_TEST_CHECK(GetTaskbarIconColor() == (!dark ? kDarkTaskbarIconColor : kLightTaskbarIconColor));
        QIQI_TEST_CHECK(ResolveDisplayAppearance({}).palette.is_dark == dark);
        QIQI_TEST_CHECK(ResolveDisplayAppearance({}, SystemThemeSource::Windows).palette.is_dark == !dark);
        for (const auto source : {SystemThemeSource::App, SystemThemeSource::Windows}) {
            QIQI_TEST_CHECK(!ResolveDisplayAppearance({UiTheme::Light}, source).palette.is_dark);
            QIQI_TEST_CHECK(ResolveDisplayAppearance({UiTheme::Dark}, source).palette.is_dark);
            const auto explicit_contrast = ResolveDisplayAppearance({UiTheme::HighContrast}, source);
            QIQI_TEST_CHECK(explicit_contrast.high_contrast && !explicit_contrast.system_high_contrast);
        }
        const auto contrast = ResolveDisplayAppearance({UiTheme::HighContrast});
        QIQI_TEST_CHECK(contrast.high_contrast && !contrast.system_high_contrast);
        const auto& palette = contrast.palette;
        for (const auto& [text, background] : {
                 std::pair{palette.text_primary, palette.window_bg},
                 std::pair{palette.text_secondary, palette.surface_bg},
                 std::pair{palette.text_accent, palette.window_bg},
                 std::pair{palette.selection_text, palette.selection_bg}}) {
            const double first = luminance(text), second = luminance(background);
            QIQI_TEST_CHECK((std::max(first, second) + 0.05) / (std::min(first, second) + 0.05) >= 7.0);
        }
    }
    FluentD2DThemeContext context;
    context.Init(144);
    context.SetDisplayPreferences({UiTheme::HighContrast, UiTextScale::Percent200, true});
    QIQI_TEST_CHECK(context.dpi == 144 && context.ScalePx(14) == 42 && context.ScaleDipPx(44) == 66);
    QIQI_TEST_CHECK(context.fmt_body && context.fmt_body->GetFontSize() == 42.0f);
    QIQI_TEST_CHECK(context.appearance.high_contrast);
    for (const UiTextScale scale : {UiTextScale::Percent100, UiTextScale::Percent125,
                                    UiTextScale::Percent150, UiTextScale::Percent200}) {
        QIQI_TEST_CHECK(ScaleDisplayPx(100, 192, scale) == TextScalePercent(scale) * 2);
    }
    SetHighContrastOverrideForTest(true);
    QIQI_TEST_CHECK(GetTaskbarIconColor() == ::GetSysColor(COLOR_BTNTEXT));
    for (const UiTheme theme : {UiTheme::System, UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
        for (const auto& appearance : {ResolveDisplayAppearance({theme}),
                                      ResolveDisplayAppearance({theme}, SystemThemeSource::Windows)}) {
            QIQI_TEST_CHECK(appearance.high_contrast && appearance.system_high_contrast);
            QIQI_TEST_CHECK(appearance.palette.text_primary == ::GetSysColor(COLOR_WINDOWTEXT));
            QIQI_TEST_CHECK(appearance.palette.selection_text == ::GetSysColor(COLOR_HIGHLIGHTTEXT));
        }
    }
}

void TestEffectiveDpiSelection() {
    QIQI_TEST_CHECK(ResolveEffectiveDpi(
        DPI_AWARENESS_UNAWARE, 192, 144) == 96);
    QIQI_TEST_CHECK(ResolveEffectiveDpi(
        DPI_AWARENESS_SYSTEM_AWARE, 192, 144) == 144);
    QIQI_TEST_CHECK(ResolveEffectiveDpi(
        DPI_AWARENESS_PER_MONITOR_AWARE, 192, 144) == 192);
    QIQI_TEST_CHECK(ResolveEffectiveDpi(
        DPI_AWARENESS_UNAWARE, 192, 144, 120) == 120);
    QIQI_TEST_CHECK(ResolveEffectiveDpi(
        DPI_AWARENESS_SYSTEM_AWARE, 192, 0) == 96);
}

void TestControlText() {
    const ScopedWindow control(::CreateWindowExW(
        0, L"EDIT", L"", WS_POPUP, 0, 0, 200, 30, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(control.IsValid());
    QIQI_TEST_CHECK(ControlText(control.Get()).empty());
    const std::wstring long_text = std::wstring(2048, L'x') + L"\u4e2d\U0001f600";
    QIQI_TEST_CHECK(::SetWindowTextW(control.Get(), long_text.c_str()));
    QIQI_TEST_CHECK(ControlText(control.Get()) == long_text);
    QIQI_TEST_CHECK(::SetWindowTextW(control.Get(), L"short"));
    QIQI_TEST_CHECK(ControlText(control.Get()) == L"short");
    QIQI_TEST_CHECK(::SetWindowTextW(control.Get(), L""));
    QIQI_TEST_CHECK(ControlText(control.Get()).empty());
}

void TestReusableUiTextAndControls() {
    QIQI_TEST_CHECK(
        std::wstring_view(
            LocalizedUiText(UiLanguage::ZhCn, L"中文", L"English")) ==
        L"中文");
    QIQI_TEST_CHECK(
        std::wstring_view(
            LocalizedUiText(UiLanguage::EnUs, L"中文", L"English")) ==
        L"English");
    QIQI_TEST_CHECK(
        FormatToolWindowFailureMessage(
            L"Create control", 32, UiLanguage::ZhCn) ==
        L"Create control\r\n错误码=32 (0x00000020)");
    QIQI_TEST_CHECK(
        FormatToolWindowFailureMessage(
            L"Create control", 32, UiLanguage::EnUs) ==
        L"Create control\r\nError=32 (0x00000020)");
    const auto compact_english = ResolveCandidatePageLabels(
        UiLanguage::EnUs, {UiTheme::System, UiTextScale::Percent100, false});
    QIQI_TEST_CHECK(compact_english.previous == L"‹" && compact_english.next == L"›");
    const auto readable_english = ResolveCandidatePageLabels(
        UiLanguage::EnUs, {UiTheme::System, UiTextScale::Percent150, true});
    QIQI_TEST_CHECK(readable_english.previous == L"Previous" && readable_english.next == L"Next");
    const auto readable_chinese = ResolveCandidatePageLabels(
        UiLanguage::ZhCn, {UiTheme::System, UiTextScale::Percent150, true});
    QIQI_TEST_CHECK(readable_chinese.previous == L"上一页" && readable_chinese.next == L"下一页");

    enum class TestControlId { Child = 77 };
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    const ScopedWindow parent(::CreateWindowExW(
        0, L"STATIC", L"", WS_POPUP,
        0, 0, 200, 100, nullptr, nullptr, instance, nullptr));
    QIQI_TEST_CHECK(parent);
    const ScopedWindow child(CreateFluentChildControl(
        parent.Get(), instance, nullptr, nullptr, L"EDIT", L"", 0,
        0, 0, 50, 20, std::to_underlying(TestControlId::Child)));
    QIQI_TEST_CHECK(child);
    QIQI_TEST_CHECK(
        GetChildControl(parent.Get(), TestControlId::Child) == child.Get());

    const INITCOMMONCONTROLSEX controls{
        sizeof(controls), ICC_LISTVIEW_CLASSES};
    QIQI_TEST_CHECK(::InitCommonControlsEx(&controls));
    const ScopedWindow list(::CreateWindowExW(
        0, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT | LVS_OWNERDATA,
        0, 30, 180, 50, parent.Get(), nullptr, instance, nullptr));
    QIQI_TEST_CHECK(list);
    QIQI_TEST_CHECK(ListView_SetItemCountEx(
        list.Get(), 3, LVSICF_NOSCROLL));
    ListView_SetItemState(
        list.Get(), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(
        list.Get(), 0, LVIS_SELECTED, LVIS_SELECTED);
    ListView_SetItemState(
        list.Get(), 1,
        LVIS_SELECTED | LVIS_FOCUSED,
        LVIS_SELECTED | LVIS_FOCUSED);
    constexpr std::array<uint64_t, 3> row_ids{101, 202, 303};
    const OwnerDataListSelection selection =
        ReadOwnerDataListSelection(list.Get(), row_ids);
    QIQI_TEST_CHECK((selection.selected_ids ==
                     std::vector<uint64_t>{101, 202}));
    QIQI_TEST_CHECK(selection.active_id == 202);
}
} // namespace

void RunThemeTests() {
    TestFailedGdiSelection();
    TestDisplayPreferences();
    TestEffectiveDpiSelection();
    TestControlText();
    TestReusableUiTextAndControls();
}

} // namespace qiqi::test::controltest
