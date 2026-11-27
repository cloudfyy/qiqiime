// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/i18n.h"
#include "common/guids.h"
#include "common/theme.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cwchar>
#include <string_view>
#include "tools/settings/layout.h"
#include "tools/settings/binding.h"

namespace qiqi::settings_detail {
namespace {

namespace layout = settings_layout;

LRESULT CALLBACK ScrollFocusSubclass(
    HWND child, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR) noexcept {
    if (::GetDlgCtrlID(child) == kIdChkEasyReading &&
        (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_LBUTTONUP)) {
        const auto* ctx = reinterpret_cast<const DialogContext*>(
            ::GetWindowLongPtrW(::GetParent(child), GWLP_USERDATA));
        RECT client{};
        if (!ctx || !::GetClientRect(child, &client)) {
            QIQI_LOG_WARN("Settings comfort-mode hit bounds unavailable");
            return 0;
        }
        const RECT toggle = FluentToggleBounds(client, ctx->d2d);
        const POINT point = PointFromLParam(lparam);
        if (!::PtInRect(&toggle, point)) {
            if (message == WM_LBUTTONUP) ::SendMessageW(child, WM_CANCELMODE, 0, 0);
            return 0;
        }
    }
    if (::GetDlgCtrlID(child) == kIdComboUiTextScale && message == CB_SETCURSEL &&
        wparam < static_cast<WPARAM>(UiTextScale::Percent150) &&
        ::IsDlgButtonChecked(::GetParent(child), kIdChkEasyReading) == BST_CHECKED) {
        const auto* ctx = reinterpret_cast<const DialogContext*>(
            ::GetWindowLongPtrW(::GetParent(child), GWLP_USERDATA));
        // Rebuilding the list can temporarily select its first item; a full draft sets a normalized scale.
        if (!ctx || !ctx->applying_draft) return CB_ERR;
    }
    if (message == WM_KEYDOWN && wparam == VK_TAB) {
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            return ::SendMessageW(::GetParent(child), kSettingsCyclePage,
                (::GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 1 : 0, 0);
        }
        ::SetFocus(::GetNextDlgTabItem(::GetParent(child), child, (::GetKeyState(VK_SHIFT) & 0x8000) != 0));
        return 0;
    }
    if (message == WM_KEYDOWN && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
        std::array<wchar_t, 32> name{};
        ::GetClassNameW(child, name.data(), gsl::narrow_cast<int>(name.size()));
        if (_wcsicmp(name.data(), L"COMBOBOX") != 0 || !::SendMessageW(child, CB_GETDROPPEDSTATE, 0, 0)) {
            const int command = wparam == VK_ESCAPE ? IDCANCEL :
                (_wcsicmp(name.data(), L"BUTTON") == 0 ? ::GetDlgCtrlID(child) : IDOK);
            return ::SendMessageW(::GetParent(child), WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
        }
    }
    if (message == WM_SETFOCUS) {
        ::SendMessageW(::GetParent(child), kSettingsEnsureFocusVisible, reinterpret_cast<WPARAM>(child), 0);
    } else if (message == WM_MOUSEWHEEL) {
        return ::SendMessageW(::GetParent(child), message, wparam, lparam);
    } else if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(child, ScrollFocusSubclass, id);
    }
    return ::DefSubclassProc(child, message, wparam, lparam);
}

void PaintSettingsTabs(HWND hwnd, HDC dc, const DialogContext& ctx) noexcept {
    RECT client{};
    if (!dc || !::GetClientRect(hwnd, &client)) return;
    const ScopedSaveDC saved(dc);
    const ScopedSelectObject font(dc, ctx.font.Get());
    const auto& palette = ctx.d2d.appearance.palette;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(palette.window_bg));
    const ScopedGdiHandle<HBRUSH> selected(::CreateSolidBrush(palette.selection_bg));
    const ScopedGdiHandle<HBRUSH> border(::CreateSolidBrush(palette.border_color));
    if (!background || !selected || !border) {
        QIQI_LOG_WARN("Settings tab brush creation failed (error={})", ::GetLastError());
        return;
    }
    ::FillRect(dc, &client, background.Get());
    ::SetBkMode(dc, TRANSPARENT);
    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(::GetParent(hwnd), ctx)) == UiLanguage::EnUs;
    for (size_t index = 0; index < kSettingsPageLabels.size(); ++index) {
        RECT bounds{};
        if (!::SendMessageW(hwnd, TCM_GETITEMRECT, index, reinterpret_cast<LPARAM>(&bounds))) {
            QIQI_LOG_WARN("Settings tab {} bounds query failed", index);
            continue;
        }
        const bool active = index == static_cast<size_t>(ctx.active_page);
        ::FillRect(dc, &bounds, active ? selected.Get() : background.Get());
        ::FrameRect(dc, &bounds, border.Get());
        ::SetTextColor(dc, active ? palette.selection_text : palette.text_primary);
        const auto& label = gsl::at(kSettingsPageLabels, index);
        ::DrawTextW(dc, english ? label.en : label.zh, -1, &bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (active && ::GetFocus() == hwnd) {
            ::InflateRect(&bounds, -ctx.d2d.ScaleDipPx(3), -ctx.d2d.ScaleDipPx(3));
            ::DrawFocusRect(dc, &bounds);
        }
    }
}

void PaintSettingsTabScroll(HWND hwnd, HDC dc, const DialogContext& ctx) noexcept {
    RECT bounds{};
    if (!dc || !::GetClientRect(hwnd, &bounds)) return;
    const auto& palette = ctx.d2d.appearance.palette;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(palette.window_bg));
    const ScopedGdiHandle<HPEN> pen(::CreatePen(PS_SOLID, ctx.d2d.ScaleDipPx(2), palette.text_primary));
    if (!background || !pen) {
        QIQI_LOG_WARN("Settings tab scroll resources unavailable (error={})", ::GetLastError());
        return;
    }
    const ScopedSelectObject selected(dc, pen.Get());
    ::FillRect(dc, &bounds, background.Get());
    const int half = bounds.right / 2;
    const int y = bounds.bottom / 2;
    const int offset = std::max(2, std::min(half, gsl::narrow_cast<int>(bounds.bottom)) / 5);
    for (const int direction : {-1, 1}) {
        const int x = direction < 0 ? half / 2 : half + half / 2;
        ::MoveToEx(dc, x - direction * offset, y - offset, nullptr);
        ::LineTo(dc, x + direction * offset, y);
        ::LineTo(dc, x - direction * offset, y + offset);
    }
}

} // namespace

LRESULT CALLBACK SettingsTabSubclass(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) noexcept {
    const auto* ctx = reinterpret_cast<const DialogContext*>(data);
    switch (message) {
        case TCM_SETCURSEL: {
            const LRESULT result = ::DefSubclassProc(hwnd, message, wparam, lparam);
            // Native accessibility selection changes the index without TCN_SELCHANGE.
            if (id == 3 && wparam < static_cast<WPARAM>(SettingsPage::Count) &&
                wparam != static_cast<WPARAM>(ctx->active_page)) {
                NMHDR notification{hwnd, kIdSettingsTabs, TCN_SELCHANGE};
                ::SendMessageW(::GetParent(hwnd), WM_NOTIFY, kIdSettingsTabs,
                               reinterpret_cast<LPARAM>(&notification));
            }
            return result;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            const ScopedPaintDC paint(hwnd);
            if (id == 4) PaintSettingsTabScroll(hwnd, paint.Get(), *ctx);
            else PaintSettingsTabs(hwnd, paint.Get(), *ctx);
            return 0;
        }
        case WM_PRINTCLIENT:
            if (id == 4) PaintSettingsTabScroll(hwnd, reinterpret_cast<HDC>(wparam), *ctx);
            else PaintSettingsTabs(hwnd, reinterpret_cast<HDC>(wparam), *ctx);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, SettingsTabSubclass, id);
            break;
        default: break;
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
}

void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept {
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScalePx(px); };
    const HFONT f = ctx.font.Get();
    const auto& str = GetUiStrings(ctx.baseline_config.ui_language);
    const auto create_control = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                                    const RECT& rect, int id, DWORD_PTR group = 0) noexcept {
        // Route page labels through the same viewport clipping as other owner-drawn controls.
        if (_wcsicmp(cls, L"STATIC") == 0) {
            style = (style & ~SS_TYPEMASK) | SS_OWNERDRAW;
        }
        return CreateFluentChildControl(
            hwnd, instance, f, &ctx.d2d, cls, text, style,
            S(rect.left), S(rect.top), S(rect.right - rect.left), S(rect.bottom - rect.top), id, group);
    };

    const INITCOMMONCONTROLSEX common_controls{sizeof(INITCOMMONCONTROLSEX), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    if (!::InitCommonControlsEx(&common_controls)) {
        QIQI_LOG_ERROR("Settings tab/scrollbar class initialization failed");
    }
    const HWND tabs = create_control(WC_TABCONTROLW, L"", TCS_FIXEDWIDTH | WS_TABSTOP,
                                    RECT{0, 0, 100, 40}, kIdSettingsTabs);
    if (tabs && !::SetWindowSubclass(tabs, SettingsTabSubclass, 3, reinterpret_cast<DWORD_PTR>(&ctx))) {
        QIQI_LOG_ERROR("Settings tab subclass installation failed (error={})", ::GetLastError());
    }
    create_control(L"SCROLLBAR", L"", SBS_VERT, RECT{0, 0, 20, 100}, kIdPageScroll);

    constexpr DWORD_PTR radio_group = MAKELONG(kIdRadioJidian, kIdRadioCustom);
    for (const auto& radio : kSettingsDictRadios) {
        create_control(L"BUTTON", str.*(radio.label_field), BS_AUTORADIOBUTTON | radio.extra_style,
            RECT{layout::kLeftColumn, radio.y_offset_px, layout::kLeftColumn + radio.width_px,
                 radio.y_offset_px + layout::kRadioHeight}, radio.control_id, radio_group);
    }
    create_control(L"BUTTON", str.dlg_btn_browse_dict, BS_OWNERDRAW | WS_TABSTOP,
                   layout::kBrowseDictionary, kIdBtnBrowsePrimaryDict);
    create_control(L"STATIC",
        ResolveDictionaryPathDisplayText(ctx.custom_primary_dict_path, str.dlg_lbl_custom_dict_none),
        SS_LEFT, layout::kDictionaryPath, kIdLblCustomPrimaryPath);

    for (const auto& binding : kSettingsCheckboxes) {
        create_control(L"BUTTON", str.*(binding.label_field), BS_AUTOCHECKBOX,
            RECT{binding.x_offset_px, binding.y_offset_px, binding.x_offset_px + binding.width_px,
                 binding.y_offset_px + layout::kCheckboxHeight}, binding.control_id);
    }

    constexpr DWORD combo_style = CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP;
    create_control(L"STATIC", str.dlg_lbl_page_size, 0, layout::kPageSizeLabel, kIdLblPageSize);
    create_control(L"COMBOBOX", L"", combo_style, layout::kPageSizeCombo, kIdComboPageSize);
    create_control(
        L"COMBOBOX", L"", combo_style, layout::kCandidateLayoutCombo, kIdComboCandidateLayout);

    create_control(L"STATIC", str.dlg_lbl_ui_lang, 0, layout::kLanguageLabel, kIdLblUiLang);
    create_control(L"COMBOBOX", L"", combo_style, layout::kLanguageCombo, kIdComboUiLang);

    create_control(L"STATIC", str.dlg_lbl_underline_style, 0, layout::kUnderlineLabel, kIdLblUnderlineStyle);
    const HWND underline_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kUnderlineCombo, kIdComboUnderlineStyle);
    EnableFluentUnderlineComboPreview(underline_combo, kIdChkBoldUnderline);

    create_control(L"STATIC", str.dlg_lbl_english_switch, 0, layout::kEnglishSwitchLabel, kIdLblEnglishSwitch);
    create_control(
        L"COMBOBOX", L"", combo_style, layout::kEnglishSwitchCombo, kIdComboEnglishSwitch);

    constexpr DWORD button_style = BS_OWNERDRAW | WS_TABSTOP;
    create_control(L"BUTTON", str.dlg_btn_dict_mgr, button_style, layout::kDictionaryManagerButton, kIdBtnDictManager);
    create_control(L"BUTTON", str.dlg_btn_restore_defaults, button_style,
                   layout::kRestoreDefaultsButton, kIdBtnRestoreDefaults);
    create_control(L"BUTTON", str.dlg_btn_ok, button_style, layout::kOkButton, kIdBtnOk);
    create_control(L"BUTTON", str.dlg_btn_cancel, button_style, layout::kCancelButton, kIdBtnCancel);
    const RECT initial{0, 0, 100, 40};
    create_control(L"STATIC", str.dlg_lbl_space_enter, 0, initial, kIdLblSpaceEnter);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboSpaceEnter);
    create_control(L"BUTTON", str.dlg_btn_restore_backup, button_style, initial, kIdBtnImportConfig);
    create_control(L"BUTTON", str.dlg_btn_create_backup, button_style, initial, kIdBtnExportConfig);
    create_control(L"STATIC", L"", 0, initial, kIdLblUiTheme);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboUiTheme);
    create_control(L"STATIC", L"", 0, initial, kIdLblUiTextScale);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboUiTextScale);
    create_control(L"BUTTON", L"", BS_AUTOCHECKBOX, initial, kIdChkEasyReading);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdCandidatePreview);
    create_control(L"STATIC", L"", 0, initial, kIdLblImeSkin);
    create_control(L"COMBOBOX", L"", combo_style | WS_VSCROLL, initial, kIdComboImeSkin);
    create_control(L"BUTTON", L"", button_style, initial, kIdBtnCandidateFont);
    create_control(L"STATIC", L"", 0, initial, kIdDisplayExplanation);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdAboutIcon);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdLblAboutTitle);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdLblAboutVersion);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdLblAboutLicense);
    create_control(
        L"BUTTON", str.about_btn_github, button_style, initial,
        kIdBtnOpenGitHub);
    create_control(
        L"STATIC", L"", SS_OWNERDRAW, initial, kIdLblShortcutsTitle);
    create_control(
        L"STATIC", L"", SS_OWNERDRAW, initial,
        kIdLblShortcutsSubtitle);
    create_control(
        L"STATIC", L"", SS_OWNERDRAW, initial, kIdLblShortcutsBody);
    ::EnumChildWindows(hwnd, [](HWND child, LPARAM) noexcept -> BOOL {
        if (!::SetWindowSubclass(child, ScrollFocusSubclass, 1, 0)) {
            QIQI_LOG_WARN("Settings focus subclass failed (error={})", ::GetLastError());
        }
        return TRUE;
    }, 0);
}

} // namespace qiqi::settings_detail
