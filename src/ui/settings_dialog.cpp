#include "ui/settings_dialog.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/monitor_dpi.h"
#include "common/scoped_handle.h"
#include "common/version.h"

#include <gsl/gsl>
#include <shellapi.h>
#include <array>
#include <memory>
#include <string>
#include <tuple>

namespace qiqi {

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
constexpr int kIdBtnOpenIni = 110;
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
constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct SettingsCheckboxBinding {
    int control_id;
    bool QiQiConfig::* config_field;
    const wchar_t* UiStrings::* label_field;
    int y_offset_px;
    int width_px;
};

constexpr std::array<SettingsCheckboxBinding, 13> kSettingsCheckboxes = {{
    {kIdChkExtraDict,     &QiQiConfig::enable_jidian_extra,          &UiStrings::dlg_chk_extra,          112, 400},
    {kIdChkAutoCommit4,   &QiQiConfig::auto_commit_4code,            &UiStrings::dlg_chk_auto_commit4,   140, 400},
    {kIdChkPushCommit5,   &QiQiConfig::push_commit_5th,              &UiStrings::dlg_chk_push_commit5,   168, 400},
    {kIdChkQuickSelect,   &QiQiConfig::semicolon_quote_quick_select, &UiStrings::dlg_chk_quick_select,   196, 400},
    {kIdChkCodeHint,      &QiQiConfig::show_code_hint,               &UiStrings::dlg_chk_code_hint,      224, 400},
    {kIdChkDynamicFreq,   &QiQiConfig::dynamic_phrase_freq,          &UiStrings::dlg_chk_dynamic_freq,   252, 400},
    {kIdChkShiftToggle,   &QiQiConfig::shift_toggle_mode,            &UiStrings::dlg_chk_shift_toggle,   280, 400},
    {kIdChkStatusBar,     &QiQiConfig::show_status_bar,              &UiStrings::dlg_chk_status_bar,     308, 400},
    {kIdChkBeepEmpty4,    &QiQiConfig::beep_on_empty_4code,          &UiStrings::dlg_chk_beep_empty4,    336, 400},
    {kIdChkBeepDup4,      &QiQiConfig::beep_on_dup_4code,            &UiStrings::dlg_chk_beep_dup4,      364, 400},
    {kIdChkClearEmpty4,   &QiQiConfig::clear_on_empty_4code,         &UiStrings::dlg_chk_clear_empty4,   392, 400},
    {kIdChkKeySound,      &QiQiConfig::enable_key_sound,             &UiStrings::dlg_chk_key_sound,      420, 400},
    {kIdChkLogKeystrokes, &QiQiConfig::log_keystrokes,               &UiStrings::dlg_chk_log_keystrokes, 448, 412},
}};

ScopedIcon CreateBrandIcon(int size_px) noexcept {
    return CreateTransparentGlyphIcon(L'奇', size_px, RGB(35, 105, 200));
}

struct DialogContext {
    QiQiConfig* config = nullptr;
    bool accepted = false;
    bool done = false;
    ScopedGdiHandle<HFONT> font;
    ScopedIcon icon;
};

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

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<DialogContext>(hwnd, msg, lparam);
    if (msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_COMMAND && ctx) {
        const int id = LOWORD(wparam);
        if (id == kIdRadioJidian || id == kIdRadioHaifeng || id == kIdRadioBuiltin) {
            UpdateExtraDictCheckboxState(hwnd);
            return 0;
        }
        if (id == kIdBtnOk) {
            if (ctx->config) {
                ReadSettingsFromWindowControls(hwnd, *ctx->config);
            }
            ctx->accepted = true;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        }
        if (id == kIdBtnCancel) {
            ctx->accepted = false;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        }
        if (id == kIdBtnOpenIni) {
            const auto cfg_path = GetDefaultUserConfigPath();
            EnsureDefaultConfigFile(cfg_path);
            ::ShellExecuteW(hwnd, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == kIdBtnAbout) {
            const UiLanguage lang = ctx->config ? ctx->config->ui_language : UiLanguage::Auto;
            ShowAboutDialog(g_dll_instance, hwnd, lang);
            return 0;
        }
    }
    if (msg == WM_CLOSE && ctx) {
        ctx->accepted = false;
        ctx->done = true;
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
        return 0;
    }
    if (msg == WM_NCDESTROY && ctx) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        const std::unique_ptr<DialogContext> owned_ctx(ctx);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
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
    if (msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_PAINT && ctx) {
        const ScopedPaintDC paint_dc(hwnd);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        PaintAboutDialog(paint_dc.Get(), rc, *ctx);
        return 0;
    }
    if (msg == WM_COMMAND && ctx) {
        const int id = LOWORD(wparam);
        if (id == kIdBtnOk || id == kIdBtnCancel) {
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        }
        if (id == kIdBtnOpenGitHub) {
            ::ShellExecuteW(hwnd, L"open", kGitHubRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
    }
    if (msg == WM_CLOSE && ctx) {
        ctx->done = true;
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
        return 0;
    }
    if (msg == WM_NCDESTROY && ctx) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        const std::unique_ptr<AboutDialogContext> owned_ctx(ctx);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
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

    LOGFONTW lf{};
    lf.lfHeight = -S(15);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    ctx->font.Reset(::CreateFontIndirectW(&lf));
    ctx->icon = CreateBrandIcon(S(16));

    const auto& str = GetUiStrings(bound_config ? bound_config->ui_language : UiLanguage::Auto);

    const int dlg_w = S(460);
    const int dlg_h = S(624);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->font.Get();
    ScopedWindow hwnd(::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsWndClass,
        str.dlg_title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateSettingsWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    QIQI_LOG_DEBUG("SettingsWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   dlg_pos.x, dlg_pos.y, dlg_w, dlg_h, mon.dpi);
    const auto* owned_ctx = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (owned_ctx->icon) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(owned_ctx->icon.Get()));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_jidian, BS_AUTORADIOBUTTON | WS_GROUP, S(24), S(20), S(400), S(24), kIdRadioJidian);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_haifeng, BS_AUTORADIOBUTTON, S(24), S(48), S(400), S(24), kIdRadioHaifeng);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_builtin, BS_AUTORADIOBUTTON, S(24), S(76), S(400), S(24), kIdRadioBuiltin);

    for (const auto& binding : kSettingsCheckboxes) {
        CreateChildControl(
            hwnd.Get(),
            instance,
            f,
            L"BUTTON",
            str.*(binding.label_field),
            BS_AUTOCHECKBOX,
            S(24),
            S(binding.y_offset_px),
            S(binding.width_px),
            S(24),
            binding.control_id);
    }

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_page_size, 0, S(24), S(484), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(480), S(70), S(200), kIdComboPageSize);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_ui_lang, 0, S(24), S(518), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(514), S(200), S(160), kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_open_ini, BS_PUSHBUTTON, S(24), S(554), S(104), S(28), kIdBtnOpenIni);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_about, BS_PUSHBUTTON, S(136), S(554), S(72), S(28), kIdBtnAbout);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_ok, BS_DEFPUSHBUTTON, S(218), S(554), S(78), S(28), kIdBtnOk);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_cancel, BS_PUSHBUTTON, S(304), S(554), S(78), S(28), kIdBtnCancel);

    if (bound_config) {
        WriteSettingsToWindowControls(hwnd.Get(), *bound_config);
    }
    return hwnd;
}

bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept {
    const ScopedWindow hwnd(CreateSettingsWindow(instance, parent, &in_out_config));
    if (!hwnd) {
        return false;
    }

    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    ::ShowWindow(hwnd.Get(), SW_SHOWNORMAL);
    ::UpdateWindow(hwnd.Get());

    MSG msg{};
    while (ctx && !ctx->done && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    return ctx ? ctx->accepted : false;
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

    LOGFONTW body_lf{};
    body_lf.lfHeight = -S(14);
    body_lf.lfWeight = FW_NORMAL;
    body_lf.lfCharSet = DEFAULT_CHARSET;
    body_lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(body_lf.lfFaceName, L"Microsoft YaHei UI");
    ctx->body_font.Reset(::CreateFontIndirectW(&body_lf));

    LOGFONTW title_lf = body_lf;
    title_lf.lfHeight = -S(19);
    title_lf.lfWeight = FW_BOLD;
    ctx->title_font.Reset(::CreateFontIndirectW(&title_lf));

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
    if (!hwnd) {
        return false;
    }

    const auto* ctx = reinterpret_cast<const AboutDialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    ::ShowWindow(hwnd.Get(), SW_SHOWNORMAL);
    ::UpdateWindow(hwnd.Get());

    MSG msg{};
    while (ctx && !ctx->done && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return true;
}

} // namespace qiqi
