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

namespace qiqi::settings_detail {
namespace {

namespace layout = settings_layout;

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct DisplayLabels {
    const wchar_t* theme;
    const wchar_t* scale;
    const wchar_t* easy;
    const wchar_t* explanation;
    const wchar_t* preview;
    std::array<const wchar_t*, 4> themes;
};
constexpr DisplayLabels kDisplayZh{
    L"主题", L"文字大小", L"关怀模式",
    L"系统高对比度已启用，将优先使用 Windows 配色。",
    L"候选窗预览\nwubi\n1 五笔\n2 输入\n3 你好\n上一页 / 下一页",
    {L"跟随系统", L"明亮", L"深色", L"高对比度"}};
constexpr DisplayLabels kDisplayEn{
    L"Theme", L"Text size", L"Comfort mode",
    L"Windows high contrast is active and overrides the selected theme.",
    L"Candidate preview\nwubi\n1 五笔\n2 输入\n3 你好\nPrevious / Next",
    {L"Follow Windows", L"Light", L"Dark", L"High contrast"}};
constexpr std::array<const wchar_t*, 4> kTextScaleLabels{L"100%", L"125%", L"150%", L"200%"};

const DisplayLabels& DisplayText(HWND hwnd, const DialogContext& ctx) noexcept {
    return ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, ctx)) == UiLanguage::EnUs
        ? kDisplayEn : kDisplayZh;
}

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
        return CB_ERR;
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

struct SettingsRadioBinding {
    int control_id;
    PrimaryDict dict_value;
    const wchar_t* UiStrings::* label_field;
    int y_offset_px;
    int width_px;
    DWORD extra_style;
};

constexpr std::array<SettingsRadioBinding, 4> kSettingsDictRadios = {{
    {kIdRadioJidian, PrimaryDict::Jidian, &UiStrings::dlg_radio_jidian,
     layout::kDictionaryFirstRow, layout::kOptionWidth, WS_GROUP},
    {kIdRadioHaifeng, PrimaryDict::Haifeng, &UiStrings::dlg_radio_haifeng,
     layout::kHaifengRow, layout::kOptionWidth, 0},
    {kIdRadioBuiltin, PrimaryDict::BuiltinOnly, &UiStrings::dlg_radio_builtin,
     layout::kBuiltinRow, layout::kOptionWidth, 0},
    {kIdRadioCustom, PrimaryDict::Custom, &UiStrings::dlg_radio_custom,
     layout::kCustomRow, layout::kCustomRadioWidth, 0},
}};

struct SettingsCheckboxBinding {
    int control_id;
    bool QiQiConfig::* config_field;
    const wchar_t* UiStrings::* label_field;
    int x_offset_px;
    int y_offset_px;
    int width_px;
};

constexpr std::array<SettingsCheckboxBinding, 15> kSettingsCheckboxes = {{
    {kIdChkExtraDict, &QiQiConfig::enable_jidian_extra, &UiStrings::dlg_chk_extra,
     layout::kLeftColumn, layout::kDictionaryFirstRow + layout::kDictionaryRowStep, layout::kOptionWidth},
    {kIdChkAutoCommit4, &QiQiConfig::auto_commit_4code, &UiStrings::dlg_chk_auto_commit4,
     layout::kLeftColumn, layout::kInputFirstRow, layout::kOptionWidth},
    {kIdChkPushCommit5, &QiQiConfig::push_commit_5th, &UiStrings::dlg_chk_push_commit5,
     layout::kLeftColumn, layout::kInputFirstRow + layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkQuickSelect, &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,
     layout::kLeftColumn, layout::kInputFirstRow + 2 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkCodeHint, &QiQiConfig::show_code_hint, &UiStrings::dlg_chk_code_hint,
     layout::kLeftColumn, layout::kInputFirstRow + 3 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkCodeCompletion, &QiQiConfig::enable_code_completion, &UiStrings::dlg_chk_code_completion,
     layout::kLeftColumn, layout::kInputFirstRow + 7 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkDynamicFreq, &QiQiConfig::dynamic_phrase_freq, &UiStrings::dlg_chk_dynamic_freq,
     layout::kLeftColumn, layout::kInputFirstRow + 4 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkShiftToggle, &QiQiConfig::shift_toggle_mode, &UiStrings::dlg_chk_shift_toggle,
     layout::kLeftColumn, layout::kInputFirstRow + 5 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkUpDownPageTurn, &QiQiConfig::up_down_page_turn, &UiStrings::dlg_chk_up_down_page_turn,
     layout::kLeftColumn, layout::kInputFirstRow + 6 * layout::kInputRowStep, layout::kOptionWidth},
    {kIdChkStatusBar, &QiQiConfig::show_status_bar, &UiStrings::dlg_chk_status_bar,
     layout::kRightColumn, layout::kDictionaryFirstRow, layout::kOptionWidth},
    {kIdChkBoldUnderline, &QiQiConfig::bold_underline, &UiStrings::dlg_chk_bold_underline,
     layout::kRightColumn, layout::kBoldUnderlineRow, layout::kOptionWidth},
    {kIdChkBeepEmpty4, &QiQiConfig::beep_on_empty_4code, &UiStrings::dlg_chk_beep_empty4,
     layout::kRightColumn, layout::kSoundFirstRow, layout::kOptionWidth},
    {kIdChkBeepDup4, &QiQiConfig::beep_on_dup_4code, &UiStrings::dlg_chk_beep_dup4,
     layout::kRightColumn, layout::kSoundFirstRow + layout::kSoundRowStep, layout::kOptionWidth},
    {kIdChkClearEmpty4, &QiQiConfig::clear_on_empty_4code, &UiStrings::dlg_chk_clear_empty4,
     layout::kRightColumn, layout::kSoundFirstRow + 2 * layout::kSoundRowStep, layout::kOptionWidth},
    {kIdChkKeySound, &QiQiConfig::enable_key_sound, &UiStrings::dlg_chk_key_sound,
     layout::kRightColumn, layout::kSoundFirstRow + 3 * layout::kSoundRowStep, layout::kOptionWidth},
}};

} // namespace

const wchar_t* SettingsConfigFieldLabel(std::string_view key, UiLanguage language) noexcept {
    const auto& str = GetUiStrings(language);
    const wchar_t* label = nullptr;
    ForEachConfigField([&](const auto& field) noexcept {
        using T = std::remove_cvref_t<decltype(QiQiConfig{}.*(field.member))>;
        if constexpr (std::is_same_v<T, bool>) {
            if (key == field.backup_key) {
                for (const auto& binding : kSettingsCheckboxes) {
                    if (binding.config_field == field.member) label = str.*(binding.label_field);
                }
            }
        }
    });
    if (label) return label;
    if (key == "primary_dict")
        return LocalizedUiText(language, L"主词库", L"Primary dictionary");
    if (key == "page_size" || key == "candidate_layout") return str.dlg_lbl_page_size;
    if (key == "ui_language") return str.dlg_lbl_ui_lang;
    if (key == "underline_style") return str.dlg_lbl_underline_style;
    if (key == "ime_skin_id") return LocalizedUiText(language, L"皮肤", L"Skin");
    if (key == "commit_on_english_switch") return str.dlg_lbl_english_switch;
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    if (key == "ui_theme") return english ? kDisplayEn.theme : kDisplayZh.theme;
    if (key == "ui_text_scale") return english ? kDisplayEn.scale : kDisplayZh.scale;
    if (key == "easy_reading") return english ? kDisplayEn.easy : kDisplayZh.easy;
    if (key == "enable_z_temp_english") return english ? L"Temporary English input" : L"临时英文输入";
    if (key == "default_chinese_mode") return english ? L"Default Chinese mode" : L"默认中文模式";
    if (key == "default_full_width") return english ? L"Default full width" : L"默认全角模式";
    if (key == "default_chinese_punct") return english ? L"Default Chinese punctuation" : L"默认中文标点";
    return nullptr;
}

void UpdateSettingsDisplayLabels(HWND hwnd, const DialogContext& ctx) noexcept {
    const auto& text = DisplayText(hwnd, ctx);
    ::SetDlgItemTextW(hwnd, kIdLblUiTheme, text.theme);
    ::SetDlgItemTextW(hwnd, kIdLblUiTextScale, text.scale);
    ::SetDlgItemTextW(hwnd, kIdChkEasyReading, text.easy);
    ::SetDlgItemTextW(hwnd, kIdDisplayExplanation, text.explanation);
    ::SetDlgItemTextW(hwnd, kIdCandidatePreview, text.preview);
    ::SetDlgItemTextW(hwnd, kIdSkinCandidatePreview, text.preview);
    std::ignore = log::CheckHResult(
        ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboUiTheme), text.themes),
        "Settings::ReplaceComboBoxItems(theme)");
    UpdateSettingsTabLabels(hwnd, ctx);
}

void UpdateSettingsTabLabels(HWND hwnd, const DialogContext& ctx) noexcept {
    const HWND tabs = ::GetDlgItem(hwnd, kIdSettingsTabs);
    if (!tabs) return;
    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, ctx)) == UiLanguage::EnUs;
    const LRESULT count = ::SendMessageW(tabs, TCM_GETITEMCOUNT, 0, 0);
    for (size_t index = 0; index < kSettingsPageLabels.size(); ++index) {
        const auto& label = gsl::at(kSettingsPageLabels, index);
        std::array<wchar_t, 64> text{};
        wcscpy_s(text.data(), text.size(), english ? label.en : label.zh);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        const bool insert = gsl::narrow_cast<LRESULT>(index) >= count;
        const LRESULT result = ::SendMessageW(tabs, insert ? TCM_INSERTITEMW : TCM_SETITEMW,
            index, reinterpret_cast<LPARAM>(&item));
        if ((insert && result == -1) || (!insert && !result)) {
            QIQI_LOG_WARN("Settings tab {} label update failed", index);
        }
    }
    ::SendMessageW(tabs, TCM_SETCURSEL, static_cast<WPARAM>(ctx.active_page), 0);
}

namespace {

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

void ApplySettingsDisplayPreview(HWND hwnd, DialogContext& ctx) noexcept {
    QiQiConfig edited;
    ReadSettingsFromWindowControls(hwnd, edited);
    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL,
                         static_cast<WPARAM>(edited.ui_text_scale), 0);
    ctx.preview_theme = edited.ui_theme;
    ctx.preview_scale = edited.ui_text_scale;
    ctx.preview_easy_reading = edited.easy_reading;
    const auto preferences = GetDisplayPreferences(edited);
    ctx.d2d.SetDisplayPreferences(preferences);
    UpdateSettingsSkinPreview(hwnd, ctx);
    ApplyDisplayThemeToWindow(hwnd, ctx.d2d.appearance);
    auto font = CreateUiFont(ctx.d2d.ScalePx(settings_layout::kFontSize),
                             FW_NORMAL, CLEARTYPE_QUALITY);
    if (font) {
        ::EnumChildWindows(hwnd, [](HWND child, LPARAM handle) noexcept -> BOOL {
            ::SendMessageW(child, WM_SETFONT, gsl::narrow_cast<WPARAM>(handle), FALSE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font.Get()));
        ctx.font = std::move(font);
    } else {
        QIQI_LOG_ERROR("Settings preview font creation failed (error={})", ::GetLastError());
    }
    UpdateSettingsDisplayLabels(hwnd, ctx);
    LayoutSettingsControls(hwnd, ctx);
    EnsureSettingsControlVisible(hwnd, ::GetFocus(), ctx);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

int MeasureSettingsCandidatePreviewHeight(HWND preview, int width, const DialogContext& ctx) noexcept {
    const ScopedWindowDC dc(preview);
    if (!dc) {
        QIQI_LOG_WARN("Settings preview measurement DC acquisition failed");
        return 0;
    }
    return MeasureCandidateSample(dc.Get(), ctx.font.Get(), width, ctx.d2d.dpi,
        GetSelectedUiLanguageFromCombo(::GetParent(preview), ctx), ctx.d2d.display_preferences);
}

void DrawSettingsCandidatePreview(const DRAWITEMSTRUCT& item, const DialogContext& ctx) noexcept {
    std::ignore = PaintCandidateSample(item.hDC, item.rcItem, ctx.font.Get(), ctx.d2d.dpi,
        GetSelectedUiLanguageFromCombo(::GetParent(item.hwndItem), ctx),
        ctx.d2d.display_preferences, ctx.candidate_skin_colors);
}

UiLanguage GetSelectedUiLanguageFromCombo(HWND hwnd, const DialogContext& ctx) noexcept {
    const LRESULT lang_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (lang_sel == 1) {
        return UiLanguage::ZhCn;
    }
    if (lang_sel == 2) {
        return UiLanguage::EnUs;
    }
    if (lang_sel == 0) {
        return UiLanguage::Auto;
    }
    return ctx.config ? ctx.config->ui_language : UiLanguage::Auto;
}

void UpdateSettingsWindowTitle(HWND hwnd, const DialogContext& ctx) noexcept {
    if (!ctx.controls_initialized) {
        return;
    }
    QiQiConfig current;
    ForEachConfigField([&](const auto& field) noexcept {
        current.*(field.member) = ctx.baseline_config.*(field.member);
    });
    ReadSettingsFromWindowControls(hwnd, current);
    bool dirty = ctx.custom_primary_dict_path != ctx.baseline_config.primary_dict_path;
    ForEachConfigField([&](const auto& field) noexcept {
        if (field.HasSettingsChange(ctx.baseline_config, current) ||
            (ctx.imported_config && field.Value(ctx.baseline_config) != field.Value(current))) {
            dirty = true;
        }
    });
    const auto& str = GetUiStrings(current.ui_language);
    std::array<wchar_t, 256> title{};
    if (swprintf_s(title.data(), title.size(), L"%ls%ls", str.dlg_title, dirty ? L" *" : L"") < 0) {
        QIQI_LOG_ERROR("Settings title formatting failed");
        return;
    }
    if (!::SetWindowTextW(hwnd, title.data())) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("Settings title update failed (error={})", error);
    }
}

void RenderSettingsLanguage(HWND hwnd, DialogContext& ctx) noexcept {
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    const auto& str = GetUiStrings(lang);
    PopulateSettingsSkinCombo(hwnd, ctx);

    for (const auto& radio : kSettingsDictRadios) {
        ::SetDlgItemTextW(hwnd, radio.control_id, str.*(radio.label_field));
    }
    for (const auto& binding : kSettingsCheckboxes) {
        ::SetDlgItemTextW(hwnd, binding.control_id, str.*(binding.label_field));
    }
    ::SetDlgItemTextW(hwnd, kIdLblPageSize, str.dlg_lbl_page_size);
    ::SetDlgItemTextW(hwnd, kIdLblUiLang, str.dlg_lbl_ui_lang);
    ::SetDlgItemTextW(hwnd, kIdLblUnderlineStyle, str.dlg_lbl_underline_style);
    ::SetDlgItemTextW(hwnd, kIdLblEnglishSwitch, str.dlg_lbl_english_switch);
    ::SetDlgItemTextW(hwnd, kIdBtnBrowsePrimaryDict, str.dlg_btn_browse_dict);
    ::SetDlgItemTextW(
        hwnd,
        kIdLblCustomPrimaryPath,
        ResolveDictionaryPathDisplayText(ctx.custom_primary_dict_path, str.dlg_lbl_custom_dict_none));
    ::SetDlgItemTextW(hwnd, kIdBtnAbout, str.dlg_btn_about);
    ::SetDlgItemTextW(hwnd, kIdBtnDictManager, str.dlg_btn_dict_mgr);
    ::SetDlgItemTextW(hwnd, kIdBtnHelp, str.dlg_btn_help);
    ::SetDlgItemTextW(hwnd, kIdBtnRestoreDefaults, str.dlg_btn_restore_defaults);
    ::SetDlgItemTextW(hwnd, kIdBtnImportConfig, str.dlg_btn_import_config);
    ::SetDlgItemTextW(hwnd, kIdBtnExportConfig, str.dlg_btn_export_config);
    ::SetDlgItemTextW(hwnd, kIdBtnOk, str.dlg_btn_ok);
    ::SetDlgItemTextW(hwnd, kIdBtnCancel, str.dlg_btn_cancel);

    const auto replace_combo_items = [hwnd](int combo_id, std::span<const wchar_t* const> items) noexcept {
        std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, combo_id), items),
            "Settings::ReplaceComboBoxItems");
    };
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    replace_combo_items(kIdComboUiLang, lang_items);

    const std::array<const wchar_t*, 5> underline_items = {
        str.dlg_underline_dot,
        str.dlg_underline_solid,
        str.dlg_underline_dash,
        str.dlg_underline_squiggle,
        str.dlg_underline_none,
    };
    replace_combo_items(kIdComboUnderlineStyle, underline_items);
    const std::array<const wchar_t*, 2> english_switch_items = {
        str.dlg_english_switch_commit, str.dlg_english_switch_clear
    };
    replace_combo_items(kIdComboEnglishSwitch, english_switch_items);
    const std::array<const wchar_t*, 2> candidate_layout_items = {
        str.dlg_candidate_horizontal, str.dlg_candidate_vertical
    };
    replace_combo_items(kIdComboCandidateLayout, candidate_layout_items);
    UpdateSettingsDisplayLabels(hwnd, ctx);

    struct LanguageUpdate {
        HWND owner;
        UiLanguage language;
    } update{hwnd, lang};
    ::EnumThreadWindows(
        ::GetCurrentThreadId(),
        [](HWND owned, LPARAM param) noexcept -> BOOL {
            const auto* change = reinterpret_cast<const LanguageUpdate*>(param);
            if (::GetWindow(owned, GW_OWNER) == change->owner) {
                std::array<wchar_t, 128> name{};
                ::GetClassNameW(owned, name.data(), gsl::narrow_cast<int>(name.size()));
                if (std::wcscmp(name.data(), kInfoWndClass) == 0 ||
                    std::wcscmp(name.data(), kSettingsMessageWndClass) == 0) {
                    ::SendMessageW(owned, kSettingsLanguageChangedMessage,
                                   static_cast<WPARAM>(change->language), 0);
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&update));
    UpdateSettingsWindowTitle(hwnd, ctx);
    LayoutSettingsControls(hwnd, ctx);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SyncSettingsWindowFromExternalConfig(HWND hwnd, DialogContext& ctx) noexcept {
    if (ctx.transfer_in_progress) return;
    const uint64_t reg_seq = QueryRegistryConfigSequence();
    if (reg_seq == 0 || reg_seq == ctx.config_registry_sequence) {
        return;
    }

    QiQiConfig external_cfg{};
    uint64_t loaded_seq = 0;
    if (!LoadConfigFromRegistry(external_cfg, &loaded_seq)) {
        return;
    }
    if (ctx.save_on_accept) ctx.skin_catalog_result = LoadImeSkinCatalog(ctx.skin_catalog);

    QiQiConfig current_ui_cfg = ctx.baseline_config;
    ReadSettingsFromWindowControls(hwnd, current_ui_cfg);

    QiQiConfig merged_ui_cfg = external_cfg;
    MergeSettingsConfig(merged_ui_cfg, ctx.baseline_config, current_ui_cfg);
    if (ctx.imported_config) {
        ForEachConfigField([&](const auto& field) noexcept {
            if (!field.settings_editable && field.Value(ctx.baseline_config) != field.Value(current_ui_cfg))
                merged_ui_cfg.*(field.member) = current_ui_cfg.*(field.member);
            ctx.imported_config.value().*(field.member) = merged_ui_cfg.*(field.member);
        });
    }
    ctx.config_registry_sequence = loaded_seq;
    if (ctx.config) {
        *ctx.config = external_cfg;
    }

    if (!log::CheckHResult(RenderSettingsDraft(hwnd, ctx, merged_ui_cfg), "SyncSettingsWindowFromExternalConfig")) return;
    ctx.baseline_config = external_cfg;
    ctx.baseline_config.enable_jidian_extra =
        external_cfg.primary_dict == PrimaryDict::Jidian && external_cfg.enable_jidian_extra;
    if (external_cfg.easy_reading) {
        ctx.baseline_config.ui_text_scale = std::max(external_cfg.ui_text_scale, UiTextScale::Percent150);
    }
    UpdateSettingsWindowTitle(hwnd, ctx);
}

void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept {
    std::ignore = hwnd;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(ctx.d2d.appearance.palette.window_bg));
    if (!background) {
        QIQI_LOG_WARN("Settings background brush creation failed (error={})", ::GetLastError());
        return;
    }
    ::FillRect(hdc, &client_rc, background.Get());
    if (ctx.d2d.surface_brush) ::FillRect(hdc, &ctx.page_bounds, ctx.d2d.surface_brush.Get());
}

void UpdateExtraDictCheckboxState(HWND hwnd) noexcept {
    const bool is_jidian = (::IsDlgButtonChecked(hwnd, kIdRadioJidian) == BST_CHECKED);
    if (const HWND chk = ::GetDlgItem(hwnd, kIdChkExtraDict)) {
        if (!is_jidian) {
            ::CheckDlgButton(hwnd, kIdChkExtraDict, BST_UNCHECKED);
        }
        ::EnableWindow(chk, is_jidian ? TRUE : FALSE);
    }
    if (const HWND browse = ::GetDlgItem(hwnd, kIdBtnBrowsePrimaryDict)) {
        ::EnableWindow(browse, ::IsDlgButtonChecked(hwnd, kIdRadioCustom) == BST_CHECKED);
    }
    const LRESULT underline_sel = ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_GETCURSEL, 0, 0);
    const bool has_underline =
        underline_sel != static_cast<LRESULT>(CompositionUnderlineStyle::None);
    if (const HWND bold_chk = ::GetDlgItem(hwnd, kIdChkBoldUnderline)) {
        ::EnableWindow(bold_chk, has_underline ? TRUE : FALSE);
    }
}

void CreateSettingsControls(HWND hwnd, HINSTANCE instance, const DialogContext& ctx) noexcept {
    const auto S = [&ctx](int px) noexcept { return ctx.d2d.ScalePx(px); };
    const HFONT f = ctx.font.Get();
    const auto& str = GetUiStrings(ctx.config ? ctx.config->ui_language : UiLanguage::Auto);
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
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETMINVISIBLE, kPageSizeLabels.size(), 0);
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboPageSize), kPageSizeLabels),
        "Settings::ReplaceComboBoxItems(page size)");
    const HWND candidate_layout_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kCandidateLayoutCombo, kIdComboCandidateLayout);
    const std::array<const wchar_t*, 2> candidate_layout_items = {
        str.dlg_candidate_horizontal, str.dlg_candidate_vertical
    };
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(candidate_layout_combo, candidate_layout_items),
        "Settings::ReplaceComboBoxItems(candidate layout)");

    create_control(L"STATIC", str.dlg_lbl_ui_lang, 0, layout::kLanguageLabel, kIdLblUiLang);
    create_control(L"COMBOBOX", L"", combo_style, layout::kLanguageCombo, kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboUiLang), lang_items),
        "Settings::ReplaceComboBoxItems(language)");

    create_control(L"STATIC", str.dlg_lbl_underline_style, 0, layout::kUnderlineLabel, kIdLblUnderlineStyle);
    const HWND underline_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kUnderlineCombo, kIdComboUnderlineStyle);
    EnableFluentUnderlineComboPreview(underline_combo, kIdChkBoldUnderline);
    const std::array<const wchar_t*, 5> underline_items = {
        str.dlg_underline_dot,
        str.dlg_underline_solid,
        str.dlg_underline_dash,
        str.dlg_underline_squiggle,
        str.dlg_underline_none,
    };
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(underline_combo, underline_items),
        "Settings::ReplaceComboBoxItems(underline)");

    create_control(L"STATIC", str.dlg_lbl_english_switch, 0, layout::kEnglishSwitchLabel, kIdLblEnglishSwitch);
    const HWND english_switch_combo = create_control(
        L"COMBOBOX", L"", combo_style, layout::kEnglishSwitchCombo, kIdComboEnglishSwitch);
    const std::array<const wchar_t*, 2> english_switch_items = {
        str.dlg_english_switch_commit, str.dlg_english_switch_clear
    };
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(english_switch_combo, english_switch_items),
        "Settings::ReplaceComboBoxItems(English switch)");

    constexpr DWORD button_style = BS_OWNERDRAW | WS_TABSTOP;
    create_control(L"BUTTON", str.dlg_btn_about, button_style, layout::kAboutButton, kIdBtnAbout);
    create_control(L"BUTTON", str.dlg_btn_dict_mgr, button_style, layout::kDictionaryManagerButton, kIdBtnDictManager);
    create_control(L"BUTTON", str.dlg_btn_help, button_style, layout::kHelpButton, kIdBtnHelp);
    create_control(L"BUTTON", str.dlg_btn_restore_defaults, button_style,
                   layout::kRestoreDefaultsButton, kIdBtnRestoreDefaults);
    create_control(L"BUTTON", str.dlg_btn_ok, button_style, layout::kOkButton, kIdBtnOk);
    create_control(L"BUTTON", str.dlg_btn_cancel, button_style, layout::kCancelButton, kIdBtnCancel);
    const RECT initial{0, 0, 100, 40};
    create_control(L"BUTTON", str.dlg_btn_import_config, button_style, initial, kIdBtnImportConfig);
    create_control(L"BUTTON", str.dlg_btn_export_config, button_style, initial, kIdBtnExportConfig);
    create_control(L"STATIC", L"", 0, initial, kIdLblUiTheme);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboUiTheme);
    create_control(L"STATIC", L"", 0, initial, kIdLblUiTextScale);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboUiTextScale);
    create_control(L"BUTTON", L"", BS_AUTOCHECKBOX, initial, kIdChkEasyReading);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdCandidatePreview);
    create_control(L"STATIC", L"", 0, initial, kIdLblImeSkin);
    create_control(L"COMBOBOX", L"", combo_style, initial, kIdComboImeSkin);
    create_control(L"STATIC", L"", SS_OWNERDRAW, initial, kIdSkinCandidatePreview);
    create_control(L"STATIC", L"", 0, initial, kIdDisplayExplanation);
    std::ignore = log::CheckHResult(
        ReplaceComboBoxItems(::GetDlgItem(hwnd, kIdComboUiTextScale), kTextScaleLabels),
        "Settings::ReplaceComboBoxItems(text scale)");
    UpdateSettingsDisplayLabels(hwnd, ctx);
    ::EnumChildWindows(hwnd, [](HWND child, LPARAM) noexcept -> BOOL {
        if (!::SetWindowSubclass(child, ScrollFocusSubclass, 1, 0)) {
            QIQI_LOG_WARN("Settings focus subclass failed (error={})", ::GetLastError());
        }
        return TRUE;
    }, 0);
}

} // namespace qiqi::settings_detail

namespace qiqi {

using namespace settings_detail;

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    std::ignore = log::CheckHResult(
        WriteSettingsToWindowControlsResult(hwnd, config), "WriteSettingsToWindowControls");
}

static HRESULT PopulateSettingsControls(HWND hwnd, const QiQiConfig& config, bool load_baseline) noexcept {
    if (!::IsWindow(hwnd)) {
        QIQI_LOG_WARN("WriteSettingsToWindowControls requires a valid window");
        return E_INVALIDARG;
    }
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        try {
            std::wstring custom_path = config.primary_dict_path;
            if (load_baseline) {
                QiQiConfig baseline = config;
                baseline.enable_jidian_extra =
                    config.primary_dict == PrimaryDict::Jidian && config.enable_jidian_extra;
                if (config.easy_reading) {
                    baseline.ui_text_scale = std::max(config.ui_text_scale, UiTextScale::Percent150);
                }
                ctx->baseline_config = std::move(baseline);
            }
            ctx->custom_primary_dict_path = std::move(custom_path);
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("WriteSettingsToWindowControls: failed to copy configuration strings");
            return E_OUTOFMEMORY;
        }
        ctx->last_valid_primary_dict = config.primary_dict;
    }
    int checked_radio_id = kIdRadioJidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (config.primary_dict == radio.dict_value) {
            checked_radio_id = radio.control_id;
            break;
        }
    }
    for (const auto& radio : kSettingsDictRadios) {
        ::CheckDlgButton(
            hwnd,
            radio.control_id,
            (radio.control_id == checked_radio_id) ? BST_CHECKED : BST_UNCHECKED);
    }
    const auto& str = GetUiStrings(config.ui_language);
    ::SetDlgItemTextW(
        hwnd,
        kIdLblCustomPrimaryPath,
        ResolveDictionaryPathDisplayText(config.primary_dict_path, str.dlg_lbl_custom_dict_none));

    for (const auto& binding : kSettingsCheckboxes) {
        ::CheckDlgButton(
            hwnd,
            binding.control_id,
            (config.*(binding.config_field)) ? BST_CHECKED : BST_UNCHECKED);
    }

    const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(idx), 0);
    ::SendDlgItemMessageW(hwnd, kIdComboCandidateLayout, CB_SETCURSEL,
        config.candidate_layout == CandidateLayout::Vertical ? 1 : 0, 0);

    const int lang_idx = (config.ui_language == UiLanguage::ZhCn)
        ? 1
        : (config.ui_language == UiLanguage::EnUs ? 2 : 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(lang_idx), 0);

    const int underline_idx = std::clamp(
        static_cast<int>(config.underline_style),
        0,
        static_cast<int>(CompositionUnderlineStyle::None));
    ::SendDlgItemMessageW(
        hwnd, kIdComboUnderlineStyle, CB_SETCURSEL, gsl::narrow_cast<WPARAM>(underline_idx), 0);
    ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_SETCURSEL,
        config.commit_on_english_switch ? 0 : 1, 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_SETCURSEL, static_cast<WPARAM>(config.ui_theme), 0);
    if (auto* context = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        context->preview_skin_id = config.ime_skin_id;
        PopulateSettingsSkinCombo(hwnd, *context);
    }
    ::CheckDlgButton(hwnd, kIdChkEasyReading, config.easy_reading ? BST_CHECKED : BST_UNCHECKED);
    const UiTextScale scale = config.easy_reading
        ? std::max(config.ui_text_scale, UiTextScale::Percent150) : config.ui_text_scale;
    ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_SETCURSEL, static_cast<WPARAM>(scale), 0);
    UpdateExtraDictCheckboxState(hwnd);
    if (auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        ApplySettingsDisplayPreview(hwnd, *ctx);
        UpdateSettingsWindowTitle(hwnd, *ctx);
    }
    return S_OK;
}

HRESULT WriteSettingsToWindowControlsResult(HWND hwnd, const QiQiConfig& config) noexcept {
    return PopulateSettingsControls(hwnd, config, true);
}

HRESULT settings_detail::RenderSettingsDraft(HWND hwnd, DialogContext& ctx, const QiQiConfig& config) noexcept {
    const HRESULT hr = PopulateSettingsControls(hwnd, config, false);
    if (SUCCEEDED(hr)) RenderSettingsLanguage(hwnd, ctx);
    return hr;
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    config.primary_dict = PrimaryDict::Jidian;
    for (const auto& radio : kSettingsDictRadios) {
        if (::IsDlgButtonChecked(hwnd, radio.control_id) == BST_CHECKED) {
            config.primary_dict = radio.dict_value;
            break;
        }
    }
    if (const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        config.ime_skin_id = ctx->preview_skin_id;
        if (ctx->imported_config) {
            ForEachConfigField([&](const auto& field) noexcept {
                if (!field.settings_editable)
                    config.*(field.member) = ctx->imported_config.value().*(field.member);
            });
        }
        try {
            config.primary_dict_path = ctx->custom_primary_dict_path;
        } catch (const std::bad_alloc&) {
            QIQI_LOG_WARN("ReadSettingsFromWindowControls: custom_primary_dict_path copy failed");
        }
    }

    for (const auto& binding : kSettingsCheckboxes) {
        config.*(binding.config_field) = (::IsDlgButtonChecked(hwnd, binding.control_id) == BST_CHECKED);
    }
    if (const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        ctx && ctx->imported_config && config.primary_dict == ctx->imported_config->primary_dict &&
        config.primary_dict != PrimaryDict::Jidian) {
        config.enable_jidian_extra = ctx->imported_config->enable_jidian_extra;
    }

    const LRESULT sel = ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 9) {
        config.page_size = gsl::narrow_cast<size_t>(sel + 1);
    }
    const LRESULT layout_sel = ::SendDlgItemMessageW(hwnd, kIdComboCandidateLayout, CB_GETCURSEL, 0, 0);
    if (layout_sel == 0 || layout_sel == 1) {
        config.candidate_layout = static_cast<CandidateLayout>(layout_sel);
    }

    const LRESULT lang_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (lang_sel == 1) {
        config.ui_language = UiLanguage::ZhCn;
    } else if (lang_sel == 2) {
        config.ui_language = UiLanguage::EnUs;
    } else if (lang_sel == 0) {
        config.ui_language = UiLanguage::Auto;
    }

    const LRESULT underline_sel = ::SendDlgItemMessageW(hwnd, kIdComboUnderlineStyle, CB_GETCURSEL, 0, 0);
    if (underline_sel >= 0 &&
        underline_sel <= static_cast<LRESULT>(CompositionUnderlineStyle::None)) {
        config.underline_style = static_cast<CompositionUnderlineStyle>(underline_sel);
    }
    const LRESULT english_switch_sel = ::SendDlgItemMessageW(hwnd, kIdComboEnglishSwitch, CB_GETCURSEL, 0, 0);
    if (english_switch_sel == 0 || english_switch_sel == 1) {
        config.commit_on_english_switch = (english_switch_sel == 0);
    }
    const LRESULT theme = ::SendDlgItemMessageW(hwnd, kIdComboUiTheme, CB_GETCURSEL, 0, 0);
    const LRESULT scale = ::SendDlgItemMessageW(hwnd, kIdComboUiTextScale, CB_GETCURSEL, 0, 0);
    if (theme >= 0 && theme < 4) config.ui_theme = static_cast<UiTheme>(theme);
    if (scale >= 0 && scale < 4) config.ui_text_scale = static_cast<UiTextScale>(scale);
    config.easy_reading = ::IsDlgButtonChecked(hwnd, kIdChkEasyReading) == BST_CHECKED;
    if (config.easy_reading && config.ui_text_scale < UiTextScale::Percent150) {
        config.ui_text_scale = UiTextScale::Percent150;
    }
}

} // namespace qiqi
