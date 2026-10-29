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
constexpr int kIdBtnOpenGitHub = 201;
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

ScopedIcon CreateBrandIcon(int size_px) noexcept {
    const int sz = std::max(16, size_px);
    const ScopedWindowDC screen_dc(nullptr);
    if (!screen_dc) {
        return {};
    }
    const ScopedMemoryDC mem_dc(::CreateCompatibleDC(screen_dc.Get()));
    if (!mem_dc) {
        return {};
    }
    const ScopedGdiHandle<HBITMAP> color_bmp(::CreateCompatibleBitmap(screen_dc.Get(), sz, sz));
    const ScopedGdiHandle<HBITMAP> mask_bmp(::CreateBitmap(sz, sz, 1, 1, nullptr));
    if (!color_bmp || !mask_bmp) {
        return {};
    }

    const ScopedSelectObject sel_bmp(mem_dc.Get(), color_bmp.Get());
    RECT rc{0, 0, sz, sz};
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(RGB(35, 105, 200)));
    ::FillRect(mem_dc.Get(), &rc, bg_brush.Get());

    LOGFONTW lf{};
    lf.lfHeight = -(sz - 4);
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    const ScopedGdiHandle<HFONT> font(::CreateFontIndirectW(&lf));
    const ScopedSelectObject sel_font(mem_dc.Get(), font.Get());

    ::SetBkMode(mem_dc.Get(), TRANSPARENT);
    ::SetTextColor(mem_dc.Get(), RGB(255, 255, 255));
    ::DrawTextW(mem_dc.Get(), L"奇", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp.Get();
    ii.hbmColor = color_bmp.Get();
    return ScopedIcon(::CreateIconIndirect(&ii));
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
    ScopedGdiHandle<HFONT> badge_font;
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
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
    const auto S = [dpi = ctx.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    const auto& str = GetUiStrings(ctx.ui_language);

    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(RGB(248, 250, 253)));
    ::FillRect(hdc, &client_rc, bg_brush.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    // 1. Draw rounded blue brand icon badge on top-left [24, 20, 80, 76] with "奇"
    RECT badge_rc{S(24), S(20), S(80), S(76)};
    {
        const ScopedGdiHandle<HBRUSH> badge_brush(::CreateSolidBrush(RGB(35, 105, 200)));
        const ScopedGdiHandle<HPEN> badge_pen(::CreatePen(PS_SOLID, 1, RGB(25, 85, 170)));
        const ScopedSelectObject sel_brush(hdc, badge_brush.Get());
        const ScopedSelectObject sel_pen(hdc, badge_pen.Get());
        ::RoundRect(hdc, badge_rc.left, badge_rc.top, badge_rc.right, badge_rc.bottom, S(14), S(14));

        const ScopedSelectObject sel_badge_font(hdc, ctx.badge_font.Get());
        ::SetTextColor(hdc, RGB(255, 255, 255));
        ::DrawTextW(hdc, L"奇", -1, &badge_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* ctx = reinterpret_cast<AboutDialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

    ::CheckDlgButton(hwnd, kIdChkExtraDict, config.enable_jidian_extra ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkAutoCommit4, config.auto_commit_4code ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkPushCommit5, config.push_commit_5th ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkQuickSelect, config.semicolon_quote_quick_select ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkShiftToggle, config.shift_toggle_mode ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkStatusBar, config.show_status_bar ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkBeepEmpty4, config.beep_on_empty_4code ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkBeepDup4, config.beep_on_dup_4code ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkClearEmpty4, config.clear_on_empty_4code ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkKeySound, config.enable_key_sound ? BST_CHECKED : BST_UNCHECKED);

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

    config.enable_jidian_extra = (::IsDlgButtonChecked(hwnd, kIdChkExtraDict) == BST_CHECKED);
    config.auto_commit_4code = (::IsDlgButtonChecked(hwnd, kIdChkAutoCommit4) == BST_CHECKED);
    config.push_commit_5th = (::IsDlgButtonChecked(hwnd, kIdChkPushCommit5) == BST_CHECKED);
    config.semicolon_quote_quick_select = (::IsDlgButtonChecked(hwnd, kIdChkQuickSelect) == BST_CHECKED);
    config.shift_toggle_mode = (::IsDlgButtonChecked(hwnd, kIdChkShiftToggle) == BST_CHECKED);
    config.show_status_bar = (::IsDlgButtonChecked(hwnd, kIdChkStatusBar) == BST_CHECKED);
    config.beep_on_empty_4code = (::IsDlgButtonChecked(hwnd, kIdChkBeepEmpty4) == BST_CHECKED);
    config.beep_on_dup_4code = (::IsDlgButtonChecked(hwnd, kIdChkBeepDup4) == BST_CHECKED);
    config.clear_on_empty_4code = (::IsDlgButtonChecked(hwnd, kIdChkClearEmpty4) == BST_CHECKED);
    config.enable_key_sound = (::IsDlgButtonChecked(hwnd, kIdChkKeySound) == BST_CHECKED);

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

    const int dlg_w = S(420);
    const int dlg_h = S(540);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->font.Get();
    const HICON ic = ctx->icon.Get();
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
    std::ignore = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (ic) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(ic));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_jidian, BS_AUTORADIOBUTTON | WS_GROUP, S(24), S(20), S(360), S(24), kIdRadioJidian);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_haifeng, BS_AUTORADIOBUTTON, S(24), S(48), S(360), S(24), kIdRadioHaifeng);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_radio_builtin, BS_AUTORADIOBUTTON, S(24), S(76), S(360), S(24), kIdRadioBuiltin);

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_extra, BS_AUTOCHECKBOX, S(24), S(112), S(360), S(24), kIdChkExtraDict);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_auto_commit4, BS_AUTOCHECKBOX, S(24), S(140), S(360), S(24), kIdChkAutoCommit4);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_push_commit5, BS_AUTOCHECKBOX, S(24), S(168), S(360), S(24), kIdChkPushCommit5);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_quick_select, BS_AUTOCHECKBOX, S(24), S(196), S(360), S(24), kIdChkQuickSelect);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_shift_toggle, BS_AUTOCHECKBOX, S(24), S(224), S(360), S(24), kIdChkShiftToggle);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_status_bar, BS_AUTOCHECKBOX, S(24), S(252), S(360), S(24), kIdChkStatusBar);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_beep_empty4, BS_AUTOCHECKBOX, S(24), S(280), S(360), S(24), kIdChkBeepEmpty4);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_beep_dup4, BS_AUTOCHECKBOX, S(24), S(308), S(360), S(24), kIdChkBeepDup4);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_clear_empty4, BS_AUTOCHECKBOX, S(24), S(336), S(360), S(24), kIdChkClearEmpty4);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_chk_key_sound, BS_AUTOCHECKBOX, S(24), S(364), S(360), S(24), kIdChkKeySound);

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_page_size, 0, S(24), S(400), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(396), S(70), S(200), kIdComboPageSize);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboPageSize, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"STATIC", str.dlg_lbl_ui_lang, 0, S(24), S(434), S(150), S(24), 0);
    CreateChildControl(hwnd.Get(), instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(430), S(200), S(160), kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendDlgItemMessageW(hwnd.Get(), kIdComboUiLang, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_open_ini, BS_PUSHBUTTON, S(24), S(470), S(104), S(28), kIdBtnOpenIni);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_about, BS_PUSHBUTTON, S(136), S(470), S(72), S(28), kIdBtnAbout);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_ok, BS_DEFPUSHBUTTON, S(218), S(470), S(78), S(28), kIdBtnOk);
    CreateChildControl(hwnd.Get(), instance, f, L"BUTTON", str.dlg_btn_cancel, BS_PUSHBUTTON, S(304), S(470), S(78), S(28), kIdBtnCancel);

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

    LOGFONTW badge_lf = body_lf;
    badge_lf.lfHeight = -S(30);
    badge_lf.lfWeight = FW_BOLD;
    ctx->badge_font.Reset(::CreateFontIndirectW(&badge_lf));

    ctx->icon = CreateBrandIcon(S(16));

    const auto& str = GetUiStrings(ui_language);
    const int dlg_w = S(440);
    const int dlg_h = S(310);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->body_font.Get();
    const HICON ic = ctx->icon.Get();
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
    std::ignore = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)
    if (ic) {
        ::SendMessageW(hwnd.Get(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(ic));
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
