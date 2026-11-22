// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test::controltest {
namespace {

void TestInfoDialogLayout() {
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    for (const UINT dpi : {96u, 144u, 192u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-2560, -1440, 0, 0}, dpi});
        const auto scale = [dpi](int px) { return ScalePxByDpi(px, dpi); };
        for (const bool help : {false, true}) {
            LONG previous_height = 0;
            for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
                const ScopedWindow dialog(help ? CreateHelpWindow(instance, nullptr, language) :
                                                 CreateAboutWindow(instance, nullptr, language));
                QIQI_TEST_CHECK(dialog);
                std::array<wchar_t, 64> class_name{};
                QIQI_TEST_CHECK(::GetClassNameW(
                    dialog.Get(), class_name.data(), gsl::narrow_cast<int>(class_name.size())) > 0);
                QIQI_TEST_CHECK(std::wcscmp(class_name.data(), kInfoWndClass) == 0);
                RECT client{};
                QIQI_TEST_CHECK(::GetClientRect(dialog.Get(), &client));
                QIQI_TEST_CHECK(client.right == scale(help ? 530 : 440));
                if (previous_height != 0) QIQI_TEST_CHECK(client.bottom == previous_height);
                previous_height = client.bottom;
                const auto control_rect = [&dialog](int id) {
                    const HWND control = ::GetDlgItem(dialog.Get(), id);
                    QIQI_TEST_CHECK(control != nullptr);
                    RECT rect{};
                    QIQI_TEST_CHECK(::GetWindowRect(control, &rect));
                    POINT origin{};
                    QIQI_TEST_CHECK(::ClientToScreen(dialog.Get(), &origin));
                    ::OffsetRect(&rect, -origin.x, -origin.y);
                    return rect;
                };
                const RECT ok = control_rect(IDOK);
                QIQI_TEST_CHECK(ok.right == client.right - scale(24));
                QIQI_TEST_CHECK(ok.right - ok.left == scale(98));
                QIQI_TEST_CHECK(ok.bottom - ok.top == scale(32));
                QIQI_TEST_CHECK(client.bottom - ok.bottom == scale(14));
                if (help) {
                    QIQI_TEST_CHECK(::GetDlgItem(dialog.Get(), 201) == nullptr);
                } else {
                    const RECT github = control_rect(201);
                    QIQI_TEST_CHECK(github.left == scale(24));
                    QIQI_TEST_CHECK(github.right - github.left == scale(124));
                    QIQI_TEST_CHECK(github.top == ok.top && github.bottom == ok.bottom);
                    QIQI_TEST_CHECK(github.right < ok.left);
                }
                const auto font = reinterpret_cast<HFONT>(
                    ::SendDlgItemMessageW(dialog.Get(), IDOK, WM_GETFONT, 0, 0));
                QIQI_TEST_CHECK(font != nullptr);
                const int text_width = client.right - 2 * scale(24);
                int body_height = scale(94);
                for (const UiLanguage measured_language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
                    const auto& strings = GetUiStrings(measured_language);
                    const int measured = dialog_detail::MeasureDialogTextHeight(
                        font, help ? strings.help_shortcuts_info : strings.about_license_info, text_width);
                    QIQI_TEST_CHECK(measured > 0);
                    body_height = std::max(body_height, measured);
                }
                const int header_bottom = scale(20) + scale(56);
                const int separator_y = header_bottom + scale(16);
                const int body_top = separator_y + scale(12);
                QIQI_TEST_CHECK(ok.top == body_top + body_height + scale(12));
                const RECT before_switch = ok;
                ::SendMessageW(dialog.Get(), kSettingsLanguageChangedMessage,
                               static_cast<WPARAM>(UiLanguage::EnUs), 0);
                const RECT after_switch = control_rect(IDOK);
                QIQI_TEST_CHECK(::EqualRect(&before_switch, &after_switch));
            }
        }
    }
}

void TestCommonDialogsAndUtils() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore = gsl::finally([original_theme, &original_monitor]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    const ScopedGdiHandle<HFONT> font = CreateUiFont(14, FW_NORMAL);
    QIQI_TEST_CHECK(font.IsValid());
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(nullptr, L"test", 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), nullptr, 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"", 100) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"test", 0) == 0);
    QIQI_TEST_CHECK(dialog_detail::MeasureDialogTextHeight(font.Get(), L"Line 1\nLine 2", 200) > 0);

    const ScopedIcon brand_icon = dialog_detail::LoadOrCreateBrandIcon(32, instance);
    QIQI_TEST_CHECK(brand_icon.IsValid());
    dialog_detail::ApplyDialogIcons(nullptr, brand_icon.Get(), brand_icon.Get());

    constexpr bool done = true;
    QIQI_TEST_CHECK(!dialog_detail::RunModalDialogLoop(nullptr, done));

    // Verify owner-relative centering and work-area clamping on a secondary/negative-coordinate monitor
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1920, -1080, 0, 0}, 96});
    const ScopedWindow owner(::CreateWindowExW(
        0, L"STATIC", L"Owner", WS_POPUP | WS_VISIBLE,
        -1880, -1040, 220, 160, nullptr, nullptr, instance, nullptr));
    QIQI_TEST_CHECK(owner.IsValid());

    {
        const ScopedWindow about_zh(CreateAboutWindow(instance, owner.Get(), UiLanguage::ZhCn, false));
        QIQI_TEST_CHECK(about_zh.IsValid());
        QIQI_TEST_CHECK(::GetWindow(about_zh.Get(), GW_OWNER) == owner.Get());
        RECT about_rc{};
        QIQI_TEST_CHECK(::GetWindowRect(about_zh.Get(), &about_rc));
        // Owner is near (-1880, -1040) so centered dialog would overflow left/top of (-1920, -1080) and must clamp to -1920, -1080
        QIQI_TEST_CHECK(about_rc.left == -1920 && about_rc.top == -1080);
        QIQI_TEST_CHECK(about_rc.right <= 0 && about_rc.bottom <= 0);

        std::array<wchar_t, 128> text{};
        ::GetWindowTextW(about_zh.Get(), text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kZhCnStrings.about_title);

        // Live language update via kSettingsLanguageChangedMessage
        ::SendMessageW(about_zh.Get(), kSettingsLanguageChangedMessage, static_cast<WPARAM>(UiLanguage::EnUs), 0);
        ::GetWindowTextW(about_zh.Get(), text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.about_title);
        ::GetDlgItemTextW(about_zh.Get(), 201, text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.about_btn_github);
        ::GetDlgItemTextW(about_zh.Get(), IDOK, text.data(), gsl::narrow_cast<int>(text.size()));
        QIQI_TEST_CHECK(std::wstring_view(text.data()) == kEnUsStrings.dlg_btn_ok);

        // Theme switching and WM_PRINTCLIENT rendering in both light and dark modes
        const ScopedWindowDC screen_dc;
        const ScopedMemoryDC mem_dc(::CreateCompatibleDC(screen_dc.Get()));
        const ScopedGdiHandle<HBITMAP> bmp(::CreateCompatibleBitmap(screen_dc.Get(), 480, 360));
        QIQI_TEST_CHECK(screen_dc && mem_dc && bmp);
        const ScopedSelectObject sel_bmp(mem_dc.Get(), bmp.Get());
        for (const bool dark : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            ::SendMessageW(about_zh.Get(), WM_THEMECHANGED, 0, 0);
            ::SendMessageW(about_zh.Get(), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(mem_dc.Get()), PRF_CLIENT);
            const COLORREF expected_bg = dark ? kDarkWindowBg : kLightSurfaceBg;
            QIQI_TEST_CHECK(::GetPixel(mem_dc.Get(), 10, 10) == expected_bg);
        }

        // Embedded icon mode fallback
        const ScopedWindow about_embedded(CreateAboutWindow(instance, owner.Get(), UiLanguage::EnUs, true));
        QIQI_TEST_CHECK(about_embedded.IsValid());
        QIQI_TEST_CHECK(::SendMessageW(about_embedded.Get(), WM_GETICON, ICON_BIG, 0) != 0);
    }

    // Modal ShowAboutDialog execution
    ::SetTimer(nullptr, 9, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        if (const HWND dlg = FindThreadWindowByClass(kInfoWndClass)) {
            ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(ShowAboutDialog(instance, owner.Get(), UiLanguage::EnUs, true));
    QIQI_TEST_CHECK(::IsWindowEnabled(owner.Get()) != FALSE);
}
} // namespace

void RunInfoDialogTests() {
    TestInfoDialogLayout();
    TestCommonDialogsAndUtils();
}

} // namespace qiqi::test::controltest
