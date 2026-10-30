#include "ui/settings_dialog.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <d2d1.h>
#include <dwrite.h>
#include <gsl/gsl>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <tuple>

namespace qiqi {

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kIdRadioJidian = 101;
constexpr int kIdRadioHaifeng = 102;
constexpr int kIdRadioBuiltin = 103;
constexpr int kIdChkExtraDict = 104;
constexpr int kIdChkAutoCommit4 = 105;
constexpr int kIdChkPushCommit5 = 106;
constexpr int kIdChkShiftToggle = 107;
constexpr int kIdChkStatusBar = 108;
constexpr int kIdComboPageSize = 109;
constexpr int kIdComboUiLang = 111;
constexpr int kIdChkBeepEmpty4 = 112;
constexpr int kIdChkKeySound = 113;
constexpr int kIdChkClearEmpty4 = 114;
constexpr int kIdBtnAbout = 115;
constexpr int kIdChkQuickSelect = 116;
constexpr int kIdChkBeepDup4 = 117;
constexpr int kIdChkCodeHint = 118;
constexpr int kIdChkDynamicFreq = 119;
constexpr int kIdChkLogKeystrokes = 120;
constexpr int kIdLblPrivacyNotice = 121;
constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr D2D1_COLOR_F MakeD2DColor(uint8_t r, uint8_t g, uint8_t b, float a = 1.0f) noexcept {
    return ColorRefToD2DColor(RGB(r, g, b), a);
}

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct SettingsCheckboxBinding {
    int control_id;
    bool QiQiConfig::* config_field;
    const wchar_t* UiStrings::* label_field;
    int x_offset_px;
    int y_offset_px;
    int width_px;
};

constexpr std::array<SettingsCheckboxBinding, 13> kSettingsCheckboxes = {{
    {kIdChkExtraDict,     &QiQiConfig::enable_jidian_extra,          &UiStrings::dlg_chk_extra,          44,  66, 360},
    {kIdChkAutoCommit4,   &QiQiConfig::auto_commit_4code,            &UiStrings::dlg_chk_auto_commit4,   24, 166, 400},
    {kIdChkPushCommit5,   &QiQiConfig::push_commit_5th,              &UiStrings::dlg_chk_push_commit5,   24, 194, 400},
    {kIdChkQuickSelect,   &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,   24, 222, 400},
    {kIdChkCodeHint,      &QiQiConfig::show_code_hint,               &UiStrings::dlg_chk_code_hint,      24, 250, 400},
    {kIdChkDynamicFreq,   &QiQiConfig::dynamic_phrase_freq,          &UiStrings::dlg_chk_dynamic_freq,   24, 278, 400},
    {kIdChkShiftToggle,   &QiQiConfig::shift_toggle_mode,            &UiStrings::dlg_chk_shift_toggle,   24, 306, 400},
    {kIdChkStatusBar,     &QiQiConfig::show_status_bar,              &UiStrings::dlg_chk_status_bar,     24, 334, 400},
    {kIdChkBeepEmpty4,    &QiQiConfig::beep_on_empty_4code,          &UiStrings::dlg_chk_beep_empty4,    24, 362, 400},
    {kIdChkBeepDup4,      &QiQiConfig::beep_on_dup_4code,            &UiStrings::dlg_chk_beep_dup4,      24, 390, 400},
    {kIdChkClearEmpty4,   &QiQiConfig::clear_on_empty_4code,         &UiStrings::dlg_chk_clear_empty4,   24, 418, 400},
    {kIdChkKeySound,      &QiQiConfig::enable_key_sound,             &UiStrings::dlg_chk_key_sound,      24, 446, 400},
    {kIdChkLogKeystrokes, &QiQiConfig::log_keystrokes,               &UiStrings::dlg_chk_log_keystrokes, 24, 474, 400},
}};

struct FluentRowDef {
    int id;
    bool is_radio;
    const wchar_t* UiStrings::* label_field;
    int card_idx;
};

constexpr std::array<FluentRowDef, 16> kFluentRows = {{
    {kIdRadioJidian,      true,  &UiStrings::dlg_radio_jidian,     0},
    {kIdChkExtraDict,     false, &UiStrings::dlg_chk_extra,        0},
    {kIdRadioHaifeng,     true,  &UiStrings::dlg_radio_haifeng,    0},
    {kIdRadioBuiltin,     true,  &UiStrings::dlg_radio_builtin,    0},
    {kIdChkAutoCommit4,   false, &UiStrings::dlg_chk_auto_commit4, 1},
    {kIdChkPushCommit5,   false, &UiStrings::dlg_chk_push_commit5, 1},
    {kIdChkQuickSelect,   false, &UiStrings::dlg_chk_quick_select, 1},
    {kIdChkCodeHint,      false, &UiStrings::dlg_chk_code_hint,    1},
    {kIdChkDynamicFreq,   false, &UiStrings::dlg_chk_dynamic_freq, 1},
    {kIdChkShiftToggle,   false, &UiStrings::dlg_chk_shift_toggle, 1},
    {kIdChkStatusBar,     false, &UiStrings::dlg_chk_status_bar,   2},
    {kIdChkBeepEmpty4,    false, &UiStrings::dlg_chk_beep_empty4,  2},
    {kIdChkBeepDup4,      false, &UiStrings::dlg_chk_beep_dup4,    2},
    {kIdChkClearEmpty4,   false, &UiStrings::dlg_chk_clear_empty4, 2},
    {kIdChkKeySound,      false, &UiStrings::dlg_chk_key_sound,    2},
    {kIdChkLogKeystrokes, false, &UiStrings::dlg_chk_log_keystrokes, 2},
}};

ScopedIcon CreateBrandIcon(int size_px) noexcept {
    return CreateTransparentGlyphIcon(L'奇', size_px, RGB(35, 105, 200));
}

struct DialogContext {
    QiQiConfig* config = nullptr;
    bool accepted = false;
    bool done = false;
    UINT dpi = 96;
    int hovered_id = 0;
    ScopedGdiHandle<HFONT> font;
    ScopedGdiHandle<HFONT> bold_font;
    ScopedGdiHandle<HBRUSH> surface_brush;
    ScopedIcon icon;
    ComPtr<ID2D1Factory> d2d_factory;
    ComPtr<IDWriteFactory> dwrite_factory;
    ComPtr<ID2D1DCRenderTarget> d2d_target;
    ComPtr<IDWriteTextFormat> fmt_title;
    ComPtr<IDWriteTextFormat> fmt_body;
    ComPtr<IDWriteTextFormat> fmt_caption;
    std::array<RECT, kFluentRows.size()> row_rects{};
    std::array<RECT, 3> footer_btn_rects{};
    std::array<int, 3> footer_btn_ids{{kIdBtnAbout, kIdBtnOk, kIdBtnCancel}};
};

void InitDialogD2D(DialogContext& ctx, UINT dpi) noexcept {
    ctx.dpi = dpi > 0 ? dpi : 96u;
    const auto S = [d = ctx.dpi](int px) noexcept { return static_cast<float>(ScalePxByDpi(px, d)); };

    CreateD2DDcTargetAndDWrite(ctx.d2d_factory, ctx.dwrite_factory, ctx.d2d_target);

    if (ctx.dwrite_factory) {
        std::ignore = ctx.dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, S(16), L"zh-CN", &ctx.fmt_title);
        std::ignore = ctx.dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, S(14), L"zh-CN", &ctx.fmt_body);
        std::ignore = ctx.dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, S(12), L"zh-CN", &ctx.fmt_caption);
        if (ctx.fmt_body) {
            ctx.fmt_body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            ctx.fmt_body->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
}

void PaintFluentSettingsWindow(HWND hwnd, HDC hdc, const RECT& client_rc, const DialogContext& ctx) noexcept {
    if (!ctx.d2d_target) {
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const auto S = [d = ctx.dpi](int px) noexcept { return static_cast<float>(ScalePxByDpi(px, d)); };
    const UiLanguage lang = ctx.config ? ctx.config->ui_language : UiLanguage::Auto;
    const auto& str = GetUiStrings(lang);

    if (FAILED(ctx.d2d_target->BindDC(hdc, &client_rc))) {
        return;
    }

    ctx.d2d_target->BeginDraw();
    ctx.d2d_target->Clear(is_dark ? MakeD2DColor(32, 34, 38) : MakeD2DColor(243, 245, 249));

    ComPtr<ID2D1SolidColorBrush> card_brush;
    ComPtr<ID2D1SolidColorBrush> border_brush;
    ComPtr<ID2D1SolidColorBrush> accent_brush;
    ComPtr<ID2D1SolidColorBrush> text_brush;
    ctx.d2d_target->CreateSolidColorBrush(
        is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255), &card_brush);
    ctx.d2d_target->CreateSolidColorBrush(
        is_dark ? MakeD2DColor(64, 68, 78) : MakeD2DColor(222, 227, 236), &border_brush);
    ctx.d2d_target->CreateSolidColorBrush(
        is_dark ? MakeD2DColor(76, 160, 255) : MakeD2DColor(0, 103, 192), &accent_brush);
    ctx.d2d_target->CreateSolidColorBrush(
        is_dark ? MakeD2DColor(238, 240, 245) : MakeD2DColor(28, 32, 40), &text_brush);

    const float left_pad = S(12);
    const float right_edge = static_cast<float>(client_rc.right) - S(12);
    const float radius = S(8);

    // Draw 3 Fluent rounded cards:
    // Card 0: Dictionary [10..158]
    // Card 1: Input Behavior & Feedback [162..528]
    // Card 2: Appearance & Language [530..598]
    const std::array<D2D1_RECT_F, 3> cards = {{
        D2D1::RectF(left_pad, S(10),  right_edge, S(158)),
        D2D1::RectF(left_pad, S(162), right_edge, S(528)),
        D2D1::RectF(left_pad, S(530), right_edge, S(598)),
    }};

    for (const auto& card_rc : cards) {
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(card_rc, radius, radius);
        if (card_brush) {
            ctx.d2d_target->FillRoundedRectangle(rr, card_brush.Get());
        }
        if (border_brush) {
            ctx.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
        }
    }

    // Draw Fluent vertical accent indicator bar next to active radio/toggle groups
    if (accent_brush) {
        const D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
            D2D1::RectF(left_pad + S(4), S(20), left_pad + S(8), S(54)), S(2), S(2));
        ctx.d2d_target->FillRoundedRectangle(pill, accent_brush.Get());
    }

    // Draw Fluent Toggle Pill Switches on the right edge of each checkbox row
    for (const auto& binding : kSettingsCheckboxes) {
        const bool checked = (::IsDlgButtonChecked(hwnd, binding.control_id) == BST_CHECKED);
        const float cy = S(binding.y_offset_px + 12);
        const float sw_right = right_edge - S(14);
        const float sw_left = sw_right - S(36);
        const D2D1_ROUNDED_RECT track = D2D1::RoundedRect(
            D2D1::RectF(sw_left, cy - S(9), sw_right, cy + S(9)), S(9), S(9));
        if (checked && accent_brush) {
            ctx.d2d_target->FillRoundedRectangle(track, accent_brush.Get());
        } else if (border_brush) {
            ctx.d2d_target->DrawRoundedRectangle(track, border_brush.Get(), 1.2f);
        }
        const float knob_x = checked ? (sw_right - S(9)) : (sw_left + S(9));
        const D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(knob_x, cy), S(6), S(6));
        if (checked && card_brush) {
            ctx.d2d_target->FillEllipse(knob, card_brush.Get());
        } else if (text_brush) {
            ctx.d2d_target->FillEllipse(knob, text_brush.Get());
        }
    }

    std::ignore = ctx.d2d_target->EndDraw();
    (void)str;
}

struct AboutDialogContext {
    UiLanguage ui_language = UiLanguage::Auto;
    UINT dpi = 96;
    bool done = false;
    ScopedGdiHandle<HFONT> body_font;
    ScopedGdiHandle<HFONT> title_font;
    ScopedIcon icon;
};

void CreateChildControl(
    HWND parent,
    HINSTANCE inst,
    HFONT font,
    const wchar_t* cls,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    int id) noexcept {
    const HWND ctrl = ::CreateWindowExW(
        0,
        cls,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        inst,
        nullptr);
    if (ctrl && font) {
        ::SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void UpdateExtraDictCheckboxState(HWND hwnd) noexcept {
    const bool is_jidian = (::IsDlgButtonChecked(hwnd, kIdRadioJidian) == BST_CHECKED);
    if (const HWND chk = ::GetDlgItem(hwnd, kIdChkExtraDict)) {
        ::EnableWindow(chk, is_jidian ? TRUE : FALSE);
    }
}

void HandleSettingsCommand(HWND hwnd, WPARAM wparam, DialogContext& ctx) {
    switch (LOWORD(wparam)) {
        case kIdRadioJidian:
        case kIdRadioHaifeng:
        case kIdRadioBuiltin:
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdChkLogKeystrokes:
            if (HIWORD(wparam) == BN_CLICKED && ::IsDlgButtonChecked(hwnd, kIdChkLogKeystrokes) == BST_CHECKED) {
                const UiLanguage lang = ctx.config ? ctx.config->ui_language : UiLanguage::Auto;
                const auto& str = GetUiStrings(lang);
                const int choice = ::MessageBoxW(
                    hwnd,
                    str.dlg_confirm_log_msg,
                    str.dlg_confirm_log_title,
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
                if (choice != IDYES) {
                    ::CheckDlgButton(hwnd, kIdChkLogKeystrokes, BST_UNCHECKED);
                }
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnOk:
            if (ctx.config) {
                ReadSettingsFromWindowControls(hwnd, *ctx.config);
            }
            ctx.accepted = true;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnCancel:
            ctx.accepted = false;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnAbout: {
            const UiLanguage lang = ctx.config ? ctx.config->ui_language : UiLanguage::Auto;
            ShowAboutDialog(g_dll_instance, hwnd, lang);
            return;
        }
        default:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<DialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [hwnd, ctx](HDC hdc, const RECT& rc) {
                PaintFluentSettingsWindow(hwnd, hdc, rc, *ctx);
            });
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const HWND ctrl = reinterpret_cast<HWND>(lparam);
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            const bool is_dark = IsSystemDarkTheme();
            if (ctrl && ::GetDlgCtrlID(ctrl) == kIdLblPrivacyNotice) {
                ::SetTextColor(hdc, RGB(196, 43, 28));
            } else {
                ::SetTextColor(hdc, is_dark ? RGB(238, 240, 245) : RGB(28, 32, 40));
            }
            ::SetBkMode(hdc, TRANSPARENT);
            if (ctx->surface_brush) {
                return reinterpret_cast<LRESULT>(ctx->surface_brush.Get());
            }
            return reinterpret_cast<LRESULT>(::GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_COMMAND:
            HandleSettingsCommand(hwnd, wparam, *ctx);
            return 0;
        case WM_CLOSE:
            ctx->accepted = false;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                const std::unique_ptr<DialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

void PaintAboutDialog(HDC hdc, const RECT& client_rc, const AboutDialogContext& ctx) {
    const ScopedSaveDC save_dc(hdc);
    const auto S = [dpi = ctx.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    const auto& str = GetUiStrings(ctx.ui_language);

    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(RGB(248, 250, 253)));
    ::FillRect(hdc, &client_rc, bg_brush.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    // 1. Draw transparent brand icon on top-left [24, 20, 80, 76] using the shared ScopedIcon
    if (ctx.icon) {
        ::DrawIconEx(hdc, S(24), S(20), ctx.icon.Get(), S(56), S(56), 0, nullptr, DI_NORMAL);
    }

    // 2. Draw IME Name ("奇奇五笔 (QiQi Wubi IME)") and Version
    {
        const ScopedSelectObject sel_title_font(hdc, ctx.title_font.Get());
        ::SetTextColor(hdc, RGB(24, 30, 42));
        RECT title_rc{S(94), S(22), client_rc.right - S(20), S(50)};
        ::DrawTextW(hdc, str.about_app_name, -1, &title_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    {
        const ScopedSelectObject sel_body_font(hdc, ctx.body_font.Get());
        ::SetTextColor(hdc, RGB(85, 95, 110));
        const std::wstring ver_text = std::wstring(str.about_version_prefix) +
            std::wstring(version::kFullVersionW) + L" (" + std::wstring(version::kArchW) + L")";
        RECT ver_rc{S(94), S(52), client_rc.right - S(20), S(76)};
        ::DrawTextW(hdc, ver_text.c_str(), -1, &ver_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 3. Draw GitHub URL
        ::SetTextColor(hdc, RGB(25, 95, 195));
        const std::wstring url_text = std::wstring(L"GitHub: ") + kGitHubRepoUrl;
        RECT url_rc{S(24), S(90), client_rc.right - S(20), S(114)};
        ::DrawTextW(hdc, url_text.c_str(), -1, &url_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 4. Divider line + Open Source Licenses block
        const ScopedGdiHandle<HPEN> div_pen(::CreatePen(PS_SOLID, 1, RGB(220, 225, 232)));
        const ScopedSelectObject sel_div(hdc, div_pen.Get());
        ::MoveToEx(hdc, S(24), S(122), nullptr);
        ::LineTo(hdc, client_rc.right - S(24), S(122));

        ::SetTextColor(hdc, RGB(45, 52, 64));
        RECT lic_rc{S(24), S(132), client_rc.right - S(24), client_rc.bottom - S(52)};
        ::DrawTextW(hdc, str.about_license_info, -1, &lic_rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<AboutDialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_PAINT:
            return PaintWindowClient(hwnd, [ctx](HDC hdc, const RECT& rc) { PaintAboutDialog(hdc, rc, *ctx); });
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case kIdBtnOk:
                case kIdBtnCancel:
                    ctx->done = true;
                    ::PostMessageW(hwnd, WM_NULL, 0, 0);
                    return 0;
                case kIdBtnOpenGitHub:
                    ::ShellExecuteW(hwnd, L"open", kGitHubRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                default:
                    return 0;
            }
        case WM_CLOSE:
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                const std::unique_ptr<AboutDialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

bool RunModalDialogLoop(HWND hwnd, const bool& done_flag) noexcept {
    if (!hwnd) {
        return false;
    }
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (!done_flag && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

} // namespace

void WriteSettingsToWindowControls(HWND hwnd, const QiQiConfig& config) noexcept {
    ::CheckRadioButton(
        hwnd,
        kIdRadioJidian,
        kIdRadioBuiltin,
        config.primary_dict == PrimaryDict::Haifeng
            ? kIdRadioHaifeng
            : (config.primary_dict == PrimaryDict::BuiltinOnly ? kIdRadioBuiltin : kIdRadioJidian));

    for (const auto& binding : kSettingsCheckboxes) {
        ::CheckDlgButton(
            hwnd,
            binding.control_id,
            (config.*(binding.config_field)) ? BST_CHECKED : BST_UNCHECKED);
    }

    const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
    ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);

    const int lang_idx = (config.ui_language == UiLanguage::ZhCn)
        ? 1
        : (config.ui_language == UiLanguage::EnUs ? 2 : 0);
    ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_SETCURSEL, static_cast<WPARAM>(lang_idx), 0);
    UpdateExtraDictCheckboxState(hwnd);
}

void ReadSettingsFromWindowControls(HWND hwnd, QiQiConfig& config) noexcept {
    if (::IsDlgButtonChecked(hwnd, kIdRadioHaifeng) == BST_CHECKED) {
        config.primary_dict = PrimaryDict::Haifeng;
    } else if (::IsDlgButtonChecked(hwnd, kIdRadioBuiltin) == BST_CHECKED) {
        config.primary_dict = PrimaryDict::BuiltinOnly;
    } else {
        config.primary_dict = PrimaryDict::Jidian;
    }

    for (const auto& binding : kSettingsCheckboxes) {
        config.*(binding.config_field) = (::IsDlgButtonChecked(hwnd, binding.control_id) == BST_CHECKED);
    }

    const LRESULT sel = ::SendDlgItemMessageW(hwnd, kIdComboPageSize, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < 9) {
        config.page_size = gsl::narrow_cast<size_t>(sel + 1);
    }

    const LRESULT lang_sel = ::SendDlgItemMessageW(hwnd, kIdComboUiLang, CB_GETCURSEL, 0, 0);
    if (lang_sel == 1) {
        config.ui_language = UiLanguage::ZhCn;
    } else if (lang_sel == 2) {
        config.ui_language = UiLanguage::EnUs;
    } else if (lang_sel == 0) {
        config.ui_language = UiLanguage::Auto;
    }
}

ScopedWindow CreateSettingsWindow(HINSTANCE instance, HWND parent, QiQiConfig* bound_config) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kSettingsWndClass;
    ::RegisterClassExW(&wc);

    std::unique_ptr<DialogContext> ctx;
    try {
        ctx = std::make_unique<DialogContext>();
    } catch (...) {
        return {};
    }
    ctx->config = bound_config;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    InitDialogD2D(*ctx, mon.dpi);
    const bool is_dark = IsSystemDarkTheme();
    ctx->surface_brush.Reset(::CreateSolidBrush(is_dark ? RGB(43, 46, 52) : RGB(255, 255, 255)));
    ctx->font = CreateUiFont(S(15), FW_NORMAL, DEFAULT_QUALITY);
    ctx->bold_font = CreateUiFont(S(13), FW_SEMIBOLD, DEFAULT_QUALITY);
    ctx->icon = CreateBrandIcon(S(16));

    const auto& str = GetUiStrings(bound_config ? bound_config->ui_language : UiLanguage::Auto);

    const int dlg_w = S(460);
    const int dlg_h = S(676);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->font.Get();
    const HFONT bold_f = ctx->bold_font.Get();
    ScopedWindow hwnd(::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsWndClass,
        str.dlg_title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateSettingsWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), is_dark);
    QIQI_LOG_DEBUG("SettingsWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   dlg_pos.x, dlg_pos.y, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (owned_ctx->icon) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(owned_ctx->icon.Get()));
    }

    // Jidian Wubi 86 group box containing both Jidian Core radio button and Jidian Extra checkbox
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_grp_jidian, BS_GROUPBOX, S(16), S(14), S(412), S(84), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_jidian, BS_AUTORADIOBUTTON | WS_GROUP, S(28), S(38), S(380), S(24), kIdRadioJidian);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_haifeng, BS_AUTORADIOBUTTON, S(24), S(104), S(400), S(24), kIdRadioHaifeng);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_builtin, BS_AUTORADIOBUTTON, S(24), S(132), S(400), S(24), kIdRadioBuiltin);

    for (const auto& binding : kSettingsCheckboxes) {
        CreateChildControl(
            hwnd.Get(),
            instance,
            f,
            L"BUTTON",
            str.*(binding.label_field),
            BS_AUTOCHECKBOX,
            S(binding.x_offset_px),
            S(binding.y_offset_px),
            S(binding.width_px),
            S(24),
            binding.control_id);
    }

    // Highlighted wrapped privacy notice line directly under the keystroke logging checkbox
    CreateChildControl(hwnd.Get(), instance, bold_f, L"STATIC", str.dlg_lbl_privacy_notice, 0, S(44), S(498), S(388), S(32), kIdLblPrivacyNotice);

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_page_size, 0, S(24), S(536), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(532), S(70), S(200), kIdComboPageSize);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_ui_lang, 0, S(24), S(570), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(566), S(200), S(160), kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_about, BS_PUSHBUTTON, S(24), S(606), S(88), S(28), kIdBtnAbout);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_ok, BS_DEFPUSHBUTTON, S(240), S(606), S(86), S(28), kIdBtnOk);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_cancel, BS_PUSHBUTTON, S(334), S(606), S(86), S(28), kIdBtnCancel);

    if (bound_config) {
        WriteSettingsToWindowControls(hwnd.Get(), *bound_config);
    }
    return hwnd;
}

bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept {
    const ScopedWindow hwnd(CreateSettingsWindow(instance, parent, &in_out_config));
    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    if (!hwnd || !ctx) {
        return false;
    }
    RunModalDialogLoop(hwnd.Get(), ctx->done);
    return ctx->accepted;
}

ScopedWindow CreateAboutWindow(HINSTANCE instance, HWND parent, UiLanguage ui_language) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &AboutWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kAboutWndClass;
    ::RegisterClassExW(&wc);

    std::unique_ptr<AboutDialogContext> ctx;
    try {
        ctx = std::make_unique<AboutDialogContext>();
    } catch (...) {
        return {};
    }
    ctx->ui_language = ui_language;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    ctx->dpi = mon.dpi;
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->body_font = CreateUiFont(S(14), FW_NORMAL);
    ctx->title_font = CreateUiFont(S(19), FW_BOLD);
    ctx->icon = CreateBrandIcon(S(56));

    const auto& str = GetUiStrings(ui_language);
    const int dlg_w = S(440);
    const int dlg_h = S(310);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->body_font.Get();
    ScopedWindow hwnd(::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kAboutWndClass,
        str.about_title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateAboutWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    QIQI_LOG_DEBUG("AboutWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   dlg_pos.x, dlg_pos.y, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (owned_ctx->icon) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(owned_ctx->icon.Get()));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.about_btn_github, BS_PUSHBUTTON, S(24), S(236), S(120), S(28), kIdBtnOpenGitHub);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_ok, BS_DEFPUSHBUTTON, S(320), S(236), S(86), S(28), kIdBtnOk);
    return hwnd;
}

bool ShowAboutDialog(HINSTANCE instance, HWND parent, UiLanguage ui_language) noexcept {
    const ScopedWindow hwnd(CreateAboutWindow(instance, parent, ui_language));
    const auto* ctx = reinterpret_cast<const AboutDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    return (hwnd && ctx) ? RunModalDialogLoop(hwnd.Get(), ctx->done) : false;
}

} // namespace qiqi
