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
#include "common/version.h"
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

void TestSettingsTextAlignment() {
    using namespace settings_detail;
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    constexpr std::array aligned_rows{
        std::pair{kIdLblPageSize, kIdComboPageSize},
        std::pair{kIdLblImeSkin, kIdComboImeSkin},
        std::pair{kIdLblUiLang, kIdComboUiLang},
        std::pair{kIdLblUnderlineStyle, kIdComboUnderlineStyle},
        std::pair{kIdLblEnglishSwitch, kIdComboEnglishSwitch}};
    constexpr RECT roomy_work_area{0, 0, 3840, 2160};
    for (const auto language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{roomy_work_area, 96});
        QiQiConfig config;
        config.ui_language = language;
        const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
        QIQI_TEST_CHECK(dialog);
        auto* const context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
        QIQI_TEST_CHECK(context);
        const auto bounds = [&](int id) {
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), id), &rect));
            ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&rect), 2);
            return rect;
        };
        const auto window_size = [&] {
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(dialog.Get(), &rect));
            return SIZE{rect.right - rect.left, rect.bottom - rect.top};
        };
        const auto resize = [&](int width, UINT dpi, RECT work_area) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{work_area, dpi});
            const RECT suggested{0, 0, ScalePxByDpi(width, dpi), ScalePxByDpi(494, dpi)};
            ::SendMessageW(dialog.Get(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
            QIQI_TEST_CHECK(context->d2d.dpi == dpi);
            return SIZE{suggested.right, suggested.bottom};
        };
        const auto check_space_enter_layout = [&] {
            const LONG control_right = context->page_bounds.right - context->d2d.ScaleDipPx(16);
            const LONG toggle_text_left = bounds(kIdChkShiftToggle).left + FluentToggleLabelInset(context->d2d);
            const RECT label = bounds(kIdLblSpaceEnter);
            const RECT combo = bounds(kIdComboSpaceEnter);
            QIQI_TEST_CHECK(label.left == toggle_text_left);
            QIQI_TEST_CHECK(combo.right == control_right && combo.left < combo.right &&
                            combo.right - combo.left <= context->d2d.ScalePx(260));
            if (language == UiLanguage::EnUs) {
                const auto& theme = context->d2d;
                QIQI_TEST_CHECK(theme.dwrite_factory && theme.fmt_body);
                // Leave room for the closed combo's padding and drop-down arrow.
                const float text_width = static_cast<float>(combo.right - combo.left) - theme.ScaleF(40);
                for (const std::wstring_view choice : GetUiStrings(language).dlg_space_enter_modes) {
                    ScopedDWriteTextLayout layout;
                    QIQI_TEST_CHECK(SUCCEEDED(theme.dwrite_factory->CreateTextLayout(
                        choice.data(), gsl::narrow_cast<UINT32>(choice.size()), theme.fmt_body.Get(),
                        text_width, static_cast<float>(combo.bottom - combo.top), layout.Put())));
                    DWRITE_TEXT_METRICS metrics{};
                    QIQI_TEST_CHECK(SUCCEEDED(layout->GetMetrics(&metrics)));
                    QIQI_TEST_CHECK(metrics.lineCount == 1 && metrics.width <= text_width);
                }
            }
            const bool inline_pair = label.top == combo.top;
            if (inline_pair) {
                QIQI_TEST_CHECK(label.right < combo.left);
            } else {
                QIQI_TEST_CHECK(label.right == control_right && label.bottom < combo.top);
            }
            return inline_pair;
        };
        for (const UINT dpi : {96u, 120u, 144u, 192u}) {
            for (const int width : {820, 620}) {
                const SIZE requested = resize(width, dpi, roomy_work_area);
                // Keep the synthetic sizes independent of the runner's native window tracking limits.
                QIQI_TEST_CHECK(::SetWindowPos(dialog.Get(), nullptr, 0, 0, requested.cx, requested.cy,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING));
                const SIZE actual = window_size();
                QIQI_TEST_CHECK(actual.cx == requested.cx && actual.cy == requested.cy);
                for (const auto &[label_id, toggle_id] : aligned_rows) {
                    const SettingsPage page = label_id == kIdLblUiLang ? SettingsPage::Display
                                              : label_id == kIdLblEnglishSwitch ? SettingsPage::Input
                                                                                : SettingsPage::Candidates;
                    SelectSettingsPage(dialog.Get(), *context, page);
                    RECT label{}, toggle{};
                    QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), label_id), &label));
                    QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), toggle_id), &toggle));
                    if (label.right <= toggle.left) {
                        QIQI_TEST_CHECK(label.top == toggle.top);
                    } else {
                        QIQI_TEST_CHECK(label.left == toggle.left);
                        QIQI_TEST_CHECK(label.bottom <= toggle.top);
                    }
                }
                const int control_right = context->page_bounds.right - context->d2d.ScaleDipPx(16);
                SelectSettingsPage(dialog.Get(), *context, SettingsPage::Input);
                const RECT restore = bounds(kIdBtnRestoreDefaults);
                const RECT ok = bounds(IDOK);
                const RECT cancel = bounds(IDCANCEL);
                QIQI_TEST_CHECK(ok.top == cancel.top && ok.right <= cancel.left);
                QIQI_TEST_CHECK(restore.top == ok.top);
                QIQI_TEST_CHECK(restore.top <= ok.top);
                if (restore.top == ok.top)
                    QIQI_TEST_CHECK(restore.right < ok.left);
                else
                    QIQI_TEST_CHECK(restore.bottom <= ok.top);
                RECT client{};
                QIQI_TEST_CHECK(::GetClientRect(dialog.Get(), &client));
                QIQI_TEST_CHECK(
                    restore.left >= client.left &&
                    cancel.right <= client.right);
                QIQI_TEST_CHECK(ok.bottom <= client.bottom && cancel.bottom <= client.bottom);
                const LONG toggle_text_left = bounds(kIdChkShiftToggle).left + FluentToggleLabelInset(context->d2d);
                QIQI_TEST_CHECK(bounds(kIdLblEnglishSwitch).left == toggle_text_left);
                QIQI_TEST_CHECK(bounds(kIdComboEnglishSwitch).right == control_right);
                const bool inline_space_enter = check_space_enter_layout();
                QIQI_TEST_CHECK(width != 820 || inline_space_enter);
                QIQI_TEST_CHECK(restore.right - restore.left < (control_right - context->page_bounds.left) / 2);
                SelectSettingsPage(dialog.Get(), *context, SettingsPage::Dictionary);
                const RECT browse = bounds(kIdBtnBrowsePrimaryDict);
                const RECT manager = bounds(kIdBtnDictManager);
                QIQI_TEST_CHECK(bounds(kIdChkExtraDict).left > bounds(kIdRadioJidian).left);
                QIQI_TEST_CHECK(bounds(kIdChkExtraDict).top >= bounds(kIdRadioJidian).bottom);
                QIQI_TEST_CHECK(browse.top == manager.top && browse.right <= manager.left);
                const RECT custom = bounds(kIdRadioCustom);
                const RECT custom_path = bounds(kIdLblCustomPrimaryPath);
                QIQI_TEST_CHECK(custom_path.left == custom.left + context->d2d.ScalePx(kFluentRadioTextInset) &&
                                custom_path.right <= control_right);
                if (custom.top == browse.top)
                    QIQI_TEST_CHECK(custom.right <= browse.left);
                else
                    QIQI_TEST_CHECK(custom.bottom <= browse.top);
                QIQI_TEST_CHECK(manager.right == control_right);
                SelectSettingsPage(dialog.Get(), *context, SettingsPage::Candidates);
                const RECT size = bounds(kIdComboPageSize);
                const RECT layout = bounds(kIdComboCandidateLayout);
                QIQI_TEST_CHECK(size.top == layout.top && size.right < layout.left);
                QIQI_TEST_CHECK(layout.right == control_right);
                QIQI_TEST_CHECK(bounds(kIdComboUnderlineStyle).right == control_right);
                const LONG candidate_text_left = bounds(kIdChkBoldUnderline).left +
                    FluentToggleLabelInset(context->d2d);
                QIQI_TEST_CHECK(bounds(kIdLblUnderlineStyle).left == candidate_text_left);
                QIQI_TEST_CHECK(bounds(kIdLblImeSkin).left == candidate_text_left);
                const RECT skin = bounds(kIdComboImeSkin);
                const RECT font = bounds(kIdBtnCandidateFont);
                QIQI_TEST_CHECK(skin.top == font.top && skin.right < font.left);
                QIQI_TEST_CHECK(font.right == control_right);
                const RECT preview = bounds(kIdCandidatePreview);
                QIQI_TEST_CHECK(preview.left == candidate_text_left && preview.right == control_right);
                QIQI_TEST_CHECK(preview.bottom - preview.top < ScalePxByDpi(150, dpi));
                SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
                const LONG display_text_left = bounds(kIdChkEasyReading).left +
                    FluentToggleLabelInset(context->d2d);
                QIQI_TEST_CHECK(bounds(kIdLblUiTextScale).left == display_text_left);
                QIQI_TEST_CHECK(bounds(kIdLblUiLang).left == display_text_left);
                QIQI_TEST_CHECK(bounds(kIdComboUiTheme).right == control_right);
                QIQI_TEST_CHECK(bounds(kIdComboUiTextScale).right == control_right);
                QIQI_TEST_CHECK(bounds(kIdComboUiLang).right == control_right);
                QIQI_TEST_CHECK((::GetWindowLongPtrW(::GetDlgItem(dialog.Get(), kIdComboImeSkin),
                    GWL_STYLE) & WS_VISIBLE) == 0);
                SelectSettingsPage(
                    dialog.Get(), *context,
                    SettingsPage::Shortcuts);
                QIQI_TEST_CHECK(
                    ControlText(::GetDlgItem(
                        dialog.Get(), kIdLblShortcutsTitle)) ==
                    GetUiStrings(language).help_header_title);
                QIQI_TEST_CHECK(
                    ControlText(::GetDlgItem(
                        dialog.Get(), kIdLblShortcutsBody)) ==
                    GetUiStrings(language).help_shortcuts_info);
                QIQI_TEST_CHECK(
                    (::GetWindowLongPtrW(
                        ::GetDlgItem(
                            dialog.Get(), kIdBtnOpenGitHub),
                        GWL_STYLE) & WS_VISIBLE) == 0);
                SelectSettingsPage(
                    dialog.Get(), *context, SettingsPage::About);
                QIQI_TEST_CHECK(
                    ControlText(::GetDlgItem(
                        dialog.Get(), kIdLblAboutTitle)) ==
                    GetUiStrings(language).about_app_name);
                QIQI_TEST_CHECK(ControlText(::GetDlgItem(
                    dialog.Get(), kIdLblAboutVersion))
                    .starts_with(
                        GetUiStrings(
                            language).about_version_prefix));
                QIQI_TEST_CHECK(
                    ControlText(::GetDlgItem(
                        dialog.Get(), kIdLblAboutLicense)) ==
                    GetUiStrings(language).about_license_info);
                QIQI_TEST_CHECK(
                    ControlText(::GetDlgItem(
                        dialog.Get(), kIdBtnOpenGitHub)) ==
                    GetUiStrings(language).about_btn_github);
                QIQI_TEST_CHECK(
                    (::GetWindowLongPtrW(
                        ::GetDlgItem(
                            dialog.Get(), kIdLblShortcutsBody),
                        GWL_STYLE) & WS_VISIBLE) == 0);
                const HWND tabs =
                    ::GetDlgItem(dialog.Get(), kIdSettingsTabs);
                QIQI_TEST_CHECK(
                    ::SendMessageW(
                        tabs, TCM_GETITEMCOUNT, 0, 0) ==
                    static_cast<LRESULT>(
                        SettingsPage::Count));
                std::array<wchar_t, 64> tab_text{};
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = tab_text.data();
                item.cchTextMax =
                    gsl::narrow_cast<int>(tab_text.size());
                QIQI_TEST_CHECK(::SendMessageW(
                    tabs, TCM_GETITEMW,
                    static_cast<WPARAM>(SettingsPage::Shortcuts),
                    reinterpret_cast<LPARAM>(&item)));
                QIQI_TEST_CHECK(std::wstring_view(
                    tab_text.data()) ==
                    (language == UiLanguage::ZhCn
                        ? std::wstring_view(L"快捷键")
                        : std::wstring_view(L"Shortcuts")));
                tab_text.fill(0);
                QIQI_TEST_CHECK(::SendMessageW(
                    tabs, TCM_GETITEMW,
                    static_cast<WPARAM>(SettingsPage::About),
                    reinterpret_cast<LPARAM>(&item)));
                QIQI_TEST_CHECK(std::wstring_view(
                    tab_text.data()) ==
                    (language == UiLanguage::ZhCn
                        ? std::wstring_view(L"关于")
                        : std::wstring_view(L"About")));
            }
        }
        constexpr RECT small_work_area{0, 0, 1024, 768};
        const SIZE requested = resize(820, 192, small_work_area);
        const SIZE clamped = window_size();
        QIQI_TEST_CHECK(clamped.cx < requested.cx && clamped.cx <= small_work_area.right);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::Input);
        QIQI_TEST_CHECK(!check_space_enter_layout());
    }
}

void CheckAboutLabelStartsAtLeft(HWND dialog, HDC screen, int id, HFONT font) {
    const HWND label = ::GetDlgItem(dialog, id);
    RECT client{};
    QIQI_TEST_CHECK(::GetClientRect(label, &client));
    const ScopedMemoryDC painted(::CreateCompatibleDC(screen));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen, client.right, client.bottom));
    const ScopedSelectObject selected_bitmap(painted.Get(), bitmap.Get());
    QIQI_TEST_CHECK(painted && bitmap && selected_bitmap);

    constexpr LONG kLeftProbeWidth = 12;
    {
        const ScopedSelectObject selected_font(painted.Get(), font);
        const auto text = ControlText(label);
        SIZE extent{};
        QIQI_TEST_CHECK(selected_font && !text.empty() && ::GetTextExtentPoint32W(
            painted.Get(), text.c_str(), gsl::narrow_cast<int>(text.size()), &extent));
        // With this much spare width, centered text would start outside the probe.
        QIQI_TEST_CHECK(client.right - extent.cx > 2 * kLeftProbeWidth);
    }
    QIQI_TEST_CHECK(::PatBlt(painted.Get(), 0, 0, client.right, client.bottom, BLACKNESS));
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_STATIC;
    item.CtlID = gsl::narrow_cast<UINT>(id);
    item.hwndItem = label;
    item.hDC = painted.Get();
    item.rcItem = client;
    QIQI_TEST_CHECK(::SendMessageW(dialog, WM_DRAWITEM, item.CtlID, reinterpret_cast<LPARAM>(&item)) == TRUE);
    const COLORREF background = ::GetPixel(painted.Get(), client.right - 1, 0);
    QIQI_TEST_CHECK(background != CLR_INVALID);
    bool has_left_text = false;
    for (LONG y = 0; y < client.bottom && !has_left_text; ++y) {
        for (LONG x = 0; x < kLeftProbeWidth && !has_left_text; ++x) {
            const COLORREF pixel = ::GetPixel(painted.Get(), x, y);
            QIQI_TEST_CHECK(pixel != CLR_INVALID);
            has_left_text = pixel != background;
        }
    }
    QIQI_TEST_CHECK(has_left_text);
}

void TestSettingsAboutHeader() {
    using namespace settings_detail;
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&original_monitor]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    struct LayoutCase {
        MonitorMetrics monitor;
        UiLanguage language;
        UiTextScale scale;
    };
    for (const auto& test : std::array{
             LayoutCase{{{0, 0, 1920, 1080}, 96}, UiLanguage::EnUs, UiTextScale::Percent100},
             LayoutCase{{{0, 0, 640, 600}, 192}, UiLanguage::ZhCn, UiTextScale::Percent200}}) {
        SetMonitorMetricsOverrideForTest(test.monitor);
        QiQiConfig config;
        config.ui_language = test.language;
        config.ui_text_scale = test.scale;
        const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
        QIQI_TEST_CHECK(dialog);
        auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
        QIQI_TEST_CHECK(context);
        SelectSettingsPage(dialog.Get(), *context, SettingsPage::About);
        const auto bounds = [&](int id) {
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(dialog.Get(), id), &rect));
            ::MapWindowPoints(nullptr, dialog.Get(), reinterpret_cast<POINT*>(&rect), 2);
            return rect;
        };
        const RECT icon = bounds(kIdAboutIcon);
        const RECT title = bounds(kIdLblAboutTitle);
        const RECT version = bounds(kIdLblAboutVersion);
        const RECT license = bounds(kIdLblAboutLicense);
        QIQI_TEST_CHECK(icon.left == license.left && icon.right < title.left);
        QIQI_TEST_CHECK(title.left == version.left && title.right == version.right);
        QIQI_TEST_CHECK(title.bottom <= version.top);
        QIQI_TEST_CHECK(title.top < icon.bottom && version.bottom > icon.top);
        QIQI_TEST_CHECK(version.right <= context->page_bounds.right &&
                        license.top > std::max(icon.bottom, version.bottom));

        const ScopedWindowDC dc(dialog.Get());
        const ScopedSelectObject selected_font(dc.Get(), context->font.Get());
        QIQI_TEST_CHECK(dc && selected_font);
        const auto text = ControlText(::GetDlgItem(dialog.Get(), kIdLblAboutVersion));
        QIQI_TEST_CHECK(text == std::wstring(GetUiStrings(test.language).about_version_prefix) +
            std::wstring(version::kFullVersionW) + L" (" + std::wstring(version::kArchW) + L")");
        const LONG available = version.right - version.left;
        RECT measured{0, 0, available, 0};
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), text.c_str(), -1, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX) > 0);
        QIQI_TEST_CHECK(measured.right <= available && measured.bottom <= version.bottom - version.top);

        if (test.scale == UiTextScale::Percent100) {
            CheckAboutLabelStartsAtLeft(dialog.Get(), dc.Get(), kIdLblAboutTitle, context->bold_font.Get());
            CheckAboutLabelStartsAtLeft(dialog.Get(), dc.Get(), kIdLblAboutVersion, context->font.Get());
        }
    }
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
    config.ui_language = UiLanguage::EnUs;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog.IsValid());

    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboPageSize, CB_SETCURSEL, 8, 0);
    ::CheckDlgButton(dialog.Get(), settings_detail::kIdChkAutoCommit4, BST_UNCHECKED);
    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboEnglishSwitch, CB_SETCURSEL, 1, 0);
    ::SendDlgItemMessageW(dialog.Get(), settings_detail::kIdComboUiLang, CB_SETCURSEL, 1, 0);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-2560, -1440, 0, 0}, 192});
    const RECT suggested_rect{-2100, -1250, -400, -200};
    ::SendMessageW(
        dialog.Get(),
        WM_DPICHANGED,
        MAKEWPARAM(192, 192),
        reinterpret_cast<LPARAM>(&suggested_rect));

    const HWND page_size_combo = ::GetDlgItem(dialog.Get(), settings_detail::kIdComboPageSize);
    QIQI_TEST_CHECK(page_size_combo != nullptr);
    auto* context = reinterpret_cast<settings_detail::DialogContext*>(
        ::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    settings_detail::SelectSettingsPage(dialog.Get(), *context, settings_detail::SettingsPage::Candidates);
    POINT combo_origin{};
    ::ClientToScreen(dialog.Get(), &combo_origin);
    RECT combo_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(page_size_combo, &combo_rect));
    RECT client{};
    ::GetClientRect(dialog.Get(), &client);
    QIQI_TEST_CHECK(combo_rect.left >= combo_origin.x &&
                    combo_rect.right <= combo_origin.x + client.right);

    const HFONT combo_font = reinterpret_cast<HFONT>(::SendMessageW(page_size_combo, WM_GETFONT, 0, 0));
    LOGFONTW font_info{};
    QIQI_TEST_CHECK(combo_font != nullptr);
    QIQI_TEST_CHECK(::GetObjectW(combo_font, sizeof(font_info), &font_info) == sizeof(font_info));
    QIQI_TEST_CHECK(font_info.lfHeight == -ScalePxByDpi(15, 192));
    if (CandidateFontFamilyAvailable(L"Microsoft YaHei UI")) {
        QIQI_TEST_CHECK(LogFontFaceNameView(font_info) == L"Microsoft YaHei UI");
    }

    QiQiConfig read_back{};
    ReadSettingsFromWindowControls(dialog.Get(), read_back);
    QIQI_TEST_CHECK(read_back.page_size == 9);
    QIQI_TEST_CHECK(!read_back.auto_commit_4code);
    QIQI_TEST_CHECK(!read_back.commit_on_english_switch);
    QIQI_TEST_CHECK(read_back.ui_language == UiLanguage::ZhCn && config.ui_language == UiLanguage::EnUs);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1900, -1200, 0, 0}, 192});
    ::SendMessageW(dialog.Get(), WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    RECT dialog_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(dialog.Get(), &dialog_rect));
    QIQI_TEST_CHECK(dialog_rect.left == -1900 && dialog_rect.top == -1200);
    QIQI_TEST_CHECK(dialog_rect.right <= 0 && dialog_rect.bottom <= 0);
}

void TestSettingsLabelViewportClipping() {
    using namespace settings_detail;
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.easy_reading = true;
    config.ui_text_scale = UiTextScale::Percent200;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context);
    SelectSettingsPage(dialog.Get(), *context, SettingsPage::Display);
    const HWND label = ::GetDlgItem(dialog.Get(), kIdLblUiTheme);
    RECT client{};
    QIQI_TEST_CHECK(::GetClientRect(label, &client));
    const ScopedWindowDC screen(dialog.Get());
    const ScopedMemoryDC painted(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(
        ::CreateCompatibleBitmap(screen.Get(), client.right, client.bottom));
    const ScopedGdiHandle<HBRUSH> untouched(::CreateSolidBrush(RGB(255, 0, 255)));
    QIQI_TEST_CHECK(screen && painted && bitmap && untouched);
    const ScopedSelectObject selected_bitmap(painted.Get(), bitmap.Get());
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_STATIC;
    item.CtlID = kIdLblUiTheme;
    item.hwndItem = label;
    item.hDC = painted.Get();
    item.rcItem = client;
    for (const bool fully_clipped : {true, false}) {
        const int hidden_height = fully_clipped ? client.bottom : client.bottom / 2;
        QIQI_TEST_CHECK(::SetWindowPos(label, nullptr,
            context->page_bounds.left, context->page_bounds.top - hidden_height, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW));
        ::FillRect(painted.Get(), &client, untouched.Get());
        ::SendMessageW(dialog.Get(), WM_DRAWITEM, kIdLblUiTheme, reinterpret_cast<LPARAM>(&item));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), client.right - 2, 1) == RGB(255, 0, 255));
        QIQI_TEST_CHECK(::GetPixel(painted.Get(), client.right - 2, client.bottom - 2) ==
            (fully_clipped ? RGB(255, 0, 255) : context->d2d.appearance.palette.surface_bg));
    }
}

} // namespace

void RunSettingsLayoutTests() {
    TestSettingsTextAlignment();
    TestSettingsAboutHeader();
    TestSettingsWindowMonitorChanges();
    TestSettingsLabelViewportClipping();
}

} // namespace qiqi::test
