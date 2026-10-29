#include "ui/settings_dialog.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/scoped_handle.h"

#include <gsl/gsl>
#include <shellapi.h>
#include <array>
#include <memory>
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
constexpr int kIdBtnOk = IDOK;
constexpr int kIdBtnCancel = IDCANCEL;

constexpr std::array<const wchar_t*, 9> kPageSizeLabels = {
    L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
};

struct DialogContext {
    QiQiConfig* config = nullptr;
    bool accepted = false;
    bool done = false;
    ScopedGdiHandle<HFONT> font;
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

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_COMMAND && ctx) {
        const int id = LOWORD(wparam);
        if (id == kIdBtnOk) {
            if (ctx->config) {
                ReadSettingsFromWindowControls(hwnd, *ctx->config);
            }
            ctx->accepted = true;
            ctx->done = true;
            return 0;
        }
        if (id == kIdBtnCancel) {
            ctx->accepted = false;
            ctx->done = true;
            return 0;
        }
        if (id == kIdBtnOpenIni) {
            const auto cfg_path = GetDefaultUserConfigPath();
            EnsureDefaultConfigFile(cfg_path);
            ::ShellExecuteW(hwnd, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
    }
    if (msg == WM_CLOSE && ctx) {
        ctx->accepted = false;
        ctx->done = true;
        return 0;
    }
    if (msg == WM_NCDESTROY && ctx) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        const std::unique_ptr<DialogContext> owned_ctx(ctx);
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
    ::CheckDlgButton(hwnd, kIdChkShiftToggle, config.shift_toggle_mode ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkStatusBar, config.show_status_bar ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkBeepEmpty4, config.beep_on_empty_4code ? BST_CHECKED : BST_UNCHECKED);
    ::CheckDlgButton(hwnd, kIdChkKeySound, config.enable_key_sound ? BST_CHECKED : BST_UNCHECKED);

    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    if (combo) {
        const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
        ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
    }

    const HWND lang_combo = ::GetDlgItem(hwnd, kIdComboUiLang);
    if (lang_combo) {
        const int lang_idx = (config.ui_language == UiLanguage::ZhCn)
            ? 1
            : (config.ui_language == UiLanguage::EnUs ? 2 : 0);
        ::SendMessageW(lang_combo, CB_SETCURSEL, static_cast<WPARAM>(lang_idx), 0);
    }
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
    config.shift_toggle_mode = (::IsDlgButtonChecked(hwnd, kIdChkShiftToggle) == BST_CHECKED);
    config.show_status_bar = (::IsDlgButtonChecked(hwnd, kIdChkStatusBar) == BST_CHECKED);
    config.beep_on_empty_4code = (::IsDlgButtonChecked(hwnd, kIdChkBeepEmpty4) == BST_CHECKED);
    config.enable_key_sound = (::IsDlgButtonChecked(hwnd, kIdChkKeySound) == BST_CHECKED);

    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    if (combo) {
        const LRESULT sel = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < 9) {
            config.page_size = gsl::narrow_cast<size_t>(sel + 1);
        }
    }

    const HWND lang_combo = ::GetDlgItem(hwnd, kIdComboUiLang);
    if (lang_combo) {
        const LRESULT lang_sel = ::SendMessageW(lang_combo, CB_GETCURSEL, 0, 0);
        if (lang_sel == 1) {
            config.ui_language = UiLanguage::ZhCn;
        } else if (lang_sel == 2) {
            config.ui_language = UiLanguage::EnUs;
        } else if (lang_sel == 0) {
            config.ui_language = UiLanguage::Auto;
        }
    }
}

HWND CreateSettingsWindow(HINSTANCE instance, HWND parent, QiQiConfig* bound_config) noexcept {
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
        return nullptr;
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

    const auto& str = GetUiStrings(bound_config ? bound_config->ui_language : UiLanguage::Auto);

    const int dlg_w = S(420);
    const int dlg_h = S(456);
    const POINT centered{
        (mon.work_area.left + mon.work_area.right - dlg_w) / 2,
        (mon.work_area.top + mon.work_area.bottom - dlg_h) / 2
    };
    const POINT dlg_pos = ClampPointToWorkArea(centered, dlg_w, dlg_h, mon.work_area);

    const HFONT f = ctx->font.Get();
    const HWND hwnd = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsWndClass,
        str.dlg_title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        dlg_pos.x, dlg_pos.y, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get());
    if (!hwnd) {
        return nullptr;
    }
    std::ignore = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)

    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_radio_jidian, BS_AUTORADIOBUTTON | WS_GROUP, S(24), S(20), S(360), S(24), kIdRadioJidian);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_radio_haifeng, BS_AUTORADIOBUTTON, S(24), S(48), S(360), S(24), kIdRadioHaifeng);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_radio_builtin, BS_AUTORADIOBUTTON, S(24), S(76), S(360), S(24), kIdRadioBuiltin);

    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_extra, BS_AUTOCHECKBOX, S(24), S(112), S(360), S(24), kIdChkExtraDict);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_auto_commit4, BS_AUTOCHECKBOX, S(24), S(140), S(360), S(24), kIdChkAutoCommit4);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_push_commit5, BS_AUTOCHECKBOX, S(24), S(168), S(360), S(24), kIdChkPushCommit5);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_shift_toggle, BS_AUTOCHECKBOX, S(24), S(196), S(360), S(24), kIdChkShiftToggle);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_status_bar, BS_AUTOCHECKBOX, S(24), S(224), S(360), S(24), kIdChkStatusBar);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_beep_empty4, BS_AUTOCHECKBOX, S(24), S(252), S(360), S(24), kIdChkBeepEmpty4);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_chk_key_sound, BS_AUTOCHECKBOX, S(24), S(280), S(360), S(24), kIdChkKeySound);

    CreateChildControl(hwnd, instance, f, L"STATIC", str.dlg_lbl_page_size, 0, S(24), S(316), S(150), S(24), 0);
    CreateChildControl(hwnd, instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(312), S(70), S(200), kIdComboPageSize);
    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateChildControl(hwnd, instance, f, L"STATIC", str.dlg_lbl_ui_lang, 0, S(24), S(350), S(150), S(24), 0);
    CreateChildControl(hwnd, instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, S(180), S(346), S(200), S(160), kIdComboUiLang);
    const HWND lang_combo = ::GetDlgItem(hwnd, kIdComboUiLang);
    const std::array<const wchar_t*, 3> lang_items = {str.dlg_lang_auto, str.dlg_lang_zh, str.dlg_lang_en};
    for (const wchar_t* item : lang_items) {
        ::SendMessageW(lang_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    }

    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_btn_open_ini, BS_PUSHBUTTON, S(24), S(386), S(110), S(28), kIdBtnOpenIni);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_btn_ok, BS_DEFPUSHBUTTON, S(210), S(386), S(80), S(28), kIdBtnOk);
    CreateChildControl(hwnd, instance, f, L"BUTTON", str.dlg_btn_cancel, BS_PUSHBUTTON, S(300), S(386), S(80), S(28), kIdBtnCancel);

    if (bound_config) {
        WriteSettingsToWindowControls(hwnd, *bound_config);
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

} // namespace qiqi
