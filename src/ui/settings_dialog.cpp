#include "ui/settings_dialog.h"
#include "common/scoped_handle.h"

#include <gsl/gsl>
#include <shellapi.h>
#include <array>
#include <memory>
#include <tuple>

namespace qiqi {

namespace {

constexpr const wchar_t* kSettingsWndClass = L"QiQiIMESettingsDialogClass";

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
        return TRUE;
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

    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    if (combo) {
        const int idx = gsl::narrow_cast<int>(std::clamp<size_t>(config.page_size, 1, 9) - 1);
        ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
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

    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    if (combo) {
        const LRESULT sel = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < 9) {
            config.page_size = gsl::narrow_cast<size_t>(sel + 1);
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

    LOGFONTW lf{};
    lf.lfHeight = -15;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    ctx->font.Reset(::CreateFontIndirectW(&lf));

    const HFONT f = ctx->font.Get();
    const HWND hwnd = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kSettingsWndClass,
        L"QiQi 五笔输入法 - 属性设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        240, 200, 400, 360,
        parent, nullptr, instance, ctx.get());
    if (!hwnd) {
        return nullptr;
    }
    std::ignore = ctx.release(); // Ownership transferred to hwnd (reclaimed on WM_NCDESTROY)

    CreateChildControl(hwnd, instance, f, L"BUTTON", L"极点五笔 86 核心词库 (推荐)", BS_AUTORADIOBUTTON | WS_GROUP, 24, 20, 340, 24, kIdRadioJidian);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"海峰五笔 86 超大字符集词库 (含生僻字)", BS_AUTORADIOBUTTON, 24, 48, 340, 24, kIdRadioHaifeng);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"仅使用内置精简保底词库", BS_AUTORADIOBUTTON, 24, 76, 340, 24, kIdRadioBuiltin);

    CreateChildControl(hwnd, instance, f, L"BUTTON", L"启用极点现代扩展词库 (Extra)", BS_AUTOCHECKBOX, 24, 112, 340, 24, kIdChkExtraDict);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"四码唯一自动上屏", BS_AUTOCHECKBOX, 24, 140, 340, 24, kIdChkAutoCommit4);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"第五码自动顶字上屏", BS_AUTOCHECKBOX, 24, 168, 340, 24, kIdChkPushCommit5);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"允许单按 Shift 键切换中英文", BS_AUTOCHECKBOX, 24, 196, 340, 24, kIdChkShiftToggle);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"显示桌面浮动状态条", BS_AUTOCHECKBOX, 24, 224, 340, 24, kIdChkStatusBar);

    CreateChildControl(hwnd, instance, f, L"STATIC", L"每页候选词数量:", 0, 24, 260, 120, 24, 0);
    CreateChildControl(hwnd, instance, f, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 150, 256, 70, 200, kIdComboPageSize);
    const HWND combo = ::GetDlgItem(hwnd, kIdComboPageSize);
    for (const wchar_t* label : kPageSizeLabels) {
        ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    }

    CreateChildControl(hwnd, instance, f, L"BUTTON", L"编辑 config.ini", BS_PUSHBUTTON, 24, 294, 110, 28, kIdBtnOpenIni);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"确定", BS_DEFPUSHBUTTON, 200, 294, 80, 28, kIdBtnOk);
    CreateChildControl(hwnd, instance, f, L"BUTTON", L"取消", BS_PUSHBUTTON, 290, 294, 80, 28, kIdBtnCancel);

    if (bound_config) {
        WriteSettingsToWindowControls(hwnd, *bound_config);
    }
    return hwnd;
}

bool ShowSettingsDialog(HINSTANCE instance, HWND parent, QiQiConfig& in_out_config) noexcept {
    const HWND hwnd = CreateSettingsWindow(instance, parent, &in_out_config);
    if (!hwnd) {
        return false;
    }

    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (ctx && !ctx->done && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    const bool accepted = ctx ? ctx->accepted : false;
    ::DestroyWindow(hwnd);
    return accepted;
}

} // namespace qiqi
