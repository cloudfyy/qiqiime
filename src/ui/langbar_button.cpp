#include "ui/langbar_button.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/scoped_handle.h"
#include "common/theme.h"

#include <gsl/gsl>
#include <oleauto.h>
#include <array>
#include <cwchar>

namespace qiqi {

namespace {

// Standard Windows TSF system tray input mode indicator GUID: {2C77A81E-41CC-4178-A3A7-5F8A987568E6}
constexpr GUID kGuidLbiInputMode = {
    0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}
};

constexpr DWORD kLangBarSinkCookie = 1;

void AddMenuEntry(ITfMenu* menu, LangBarMenuId id, const wchar_t* text, bool checked = false, bool enabled = true) {
    if (!menu || !text) {
        return;
    }
    DWORD flags = checked ? TF_LBMENUF_CHECKED : 0;
    if (!enabled) {
        flags |= TF_LBMENUF_GRAYED;
    }
    menu->AddMenuItem(
        static_cast<UINT>(id),
        flags,
        nullptr,
        nullptr,
        text,
        gsl::narrow_cast<ULONG>(wcslen(text)),
        nullptr);
}

void AddMenuSeparator(ITfMenu* menu) {
    if (menu) {
        menu->AddMenuItem(0, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr);
    }
}

void AppendWin32PopupItem(
    HMENU menu, LangBarMenuId id, const wchar_t* text, bool checked = false, bool enabled = true) noexcept {
    if (!menu || !text) {
        return;
    }
    const UINT flags = MF_STRING |
        (checked ? MF_CHECKED : MF_UNCHECKED) |
        (enabled ? MF_ENABLED : (MF_GRAYED | MF_DISABLED));
    ::AppendMenuW(menu, flags, static_cast<UINT_PTR>(id), text);
}

} // namespace

ScopedMenu LangBarButton::BuildSharedPopupMenu(
    const LangBarState& state, HWND owner_hwnd, ScopedGdiHandle<HBRUSH>& out_bg_brush) noexcept {
    const auto& palette = GetCurrentThemePalette();
    ApplySystemThemeToMenuAndWindow(owner_hwnd, palette.is_dark);

    ScopedMenu menu(::CreatePopupMenu());
    if (!menu) {
        return menu;
    }

    out_bg_brush.Reset(::CreateSolidBrush(palette.window_bg));
    if (out_bg_brush) {
        MENUINFO mi{};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIM_BACKGROUND;
        mi.hbrBack = out_bg_brush.Get();
        ::SetMenuInfo(menu.Get(), &mi);
    }

    const auto& str = GetUiStrings(state.ui_language);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::ToggleChinese, str.menu_toggle_chinese, state.chinese_mode);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::ToggleFullWidth, str.menu_toggle_full_width, state.full_width);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::TogglePunctuation, str.menu_toggle_punct, state.chinese_punct);
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    const bool is_jidian = (state.primary_dict == PrimaryDict::Jidian);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::DictJidian, str.menu_dict_jidian, is_jidian);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::DictHaifeng, str.menu_dict_haifeng, state.primary_dict == PrimaryDict::Haifeng);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::ToggleExtraDict, str.menu_toggle_extra_dict, is_jidian && state.enable_jidian_extra, is_jidian);
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::ToggleStatusBar, str.menu_toggle_status_bar, state.show_status_bar);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::ToggleUiLanguage, str.menu_toggle_ui_lang);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::OpenSettings, str.menu_open_settings);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::OpenConfigFile, str.menu_open_config_file);
    AppendWin32PopupItem(menu.Get(), LangBarMenuId::OpenAbout, str.menu_open_about);
    return menu;
}

LangBarButton::LangBarButton(ActionCallback cb) noexcept
    : action_callback_(std::move(cb)) {
    ++g_com_object_count;
}

LangBarButton::~LangBarButton() noexcept {
    --g_com_object_count;
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void** ppvObj) noexcept {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    if (IsEqualIID(riid, IID_IUnknown) ||
        IsEqualIID(riid, IID_ITfLangBarItem) ||
        IsEqualIID(riid, IID_ITfLangBarItemButton)) {
        *ppvObj = static_cast<ITfLangBarItemButton*>(this);
    } else if (IsEqualIID(riid, IID_ITfSource)) {
        *ppvObj = static_cast<ITfSource*>(this);
    } else {
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() noexcept {
    return ++ref_count_;
}

STDMETHODIMP_(ULONG) LangBarButton::Release() noexcept {
    const ULONG count = --ref_count_;
    if (count == 0) {
        delete this;
    }
    return count;
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) noexcept {
    if (!pInfo) {
        return E_INVALIDARG;
    }
    const auto& str = GetUiStrings(state_.ui_language);
    pInfo->clsidService = kClsidQiQiTextService;
    pInfo->guidItem = kGuidLbiInputMode;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    wcscpy_s(pInfo->szDescription, str.langbar_desc);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* pdwStatus) noexcept {
    if (!pdwStatus) {
        return E_INVALIDARG;
    }
    *pdwStatus = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL /*fShow*/) noexcept {
    return S_OK;
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* pbstrToolTip) noexcept {
    const auto& str = GetUiStrings(state_.ui_language);
    return CopyToBstr(str.langbar_tooltip, pbstrToolTip);
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT* /*prcArea*/) {
    if (!action_callback_) {
        return S_OK;
    }
    if (click == TF_LBI_CLK_LEFT) {
        action_callback_(LangBarMenuId::ToggleChinese);
        return S_OK;
    }
    if (click == TF_LBI_CLK_RIGHT) {
        ScopedGdiHandle<HBRUSH> menu_bg;
        const ScopedMenu menu = BuildSharedPopupMenu(state_, nullptr, menu_bg);
        if (!menu) {
            return S_OK;
        }
        if (pt.x == 0 && pt.y == 0) {
            ::GetCursorPos(&pt);
        }
        // Create a temporary message-only / popup host window so TrackPopupMenu works reliably from taskbar tray
        const ScopedWindow host_hwnd(::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC",
            L"",
            WS_POPUP,
            pt.x, pt.y, 1, 1,
            nullptr, nullptr, g_dll_instance, nullptr));
        const HWND menu_owner = host_hwnd ? host_hwnd.Get() : ::GetForegroundWindow();
        if (menu_owner) {
            ::SetForegroundWindow(menu_owner);
        }
        const BOOL cmd = ::TrackPopupMenu(
            menu.Get(),
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
            pt.x,
            pt.y,
            0,
            menu_owner,
            nullptr);
        if (menu_owner) {
            ::PostMessageW(menu_owner, WM_NULL, 0, 0);
        }
        if (cmd > 0) {
            action_callback_(static_cast<LangBarMenuId>(cmd));
        }
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* pMenu) {
    if (!pMenu) {
        return E_INVALIDARG;
    }
    ApplySystemThemeToMenuAndWindow(nullptr, IsSystemDarkTheme());
    const auto& str = GetUiStrings(state_.ui_language);

    AddMenuEntry(pMenu, LangBarMenuId::ToggleChinese, str.menu_toggle_chinese, state_.chinese_mode);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleFullWidth, str.menu_toggle_full_width, state_.full_width);
    AddMenuEntry(pMenu, LangBarMenuId::TogglePunctuation, str.menu_toggle_punct, state_.chinese_punct);
    AddMenuSeparator(pMenu);
    const bool is_jidian = (state_.primary_dict == PrimaryDict::Jidian);
    AddMenuEntry(pMenu, LangBarMenuId::DictJidian, str.menu_dict_jidian, is_jidian);
    AddMenuEntry(pMenu, LangBarMenuId::DictHaifeng, str.menu_dict_haifeng, state_.primary_dict == PrimaryDict::Haifeng);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleExtraDict, str.menu_toggle_extra_dict, is_jidian && state_.enable_jidian_extra, is_jidian);
    AddMenuSeparator(pMenu);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleStatusBar, str.menu_toggle_status_bar, state_.show_status_bar);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleUiLanguage, str.menu_toggle_ui_lang);
    AddMenuEntry(pMenu, LangBarMenuId::OpenSettings, str.menu_open_settings);
    AddMenuEntry(pMenu, LangBarMenuId::OpenConfigFile, str.menu_open_config_file);
    AddMenuEntry(pMenu, LangBarMenuId::OpenAbout, str.menu_open_about);
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT wID) {
    if (action_callback_) {
        action_callback_(static_cast<LangBarMenuId>(wID));
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* phIcon) noexcept {
    if (!phIcon) {
        return E_INVALIDARG;
    }
    ScopedIcon icon = CreateTextIcon(state_.chinese_mode ? L'中' : L'英');
    *phIcon = icon.Detach();
    return *phIcon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR* pbstrText) noexcept {
    return CopyToBstr(state_.chinese_mode ? L"中" : L"英", pbstrText);
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown* punk, DWORD* pdwCookie) {
    if (!punk || !pdwCookie) {
        return E_INVALIDARG;
    }
    if (!IsEqualIID(riid, IID_ITfLangBarItemSink)) {
        return CONNECT_E_CANNOTCONNECT;
    }
    if (sink_) {
        return CONNECT_E_ADVISELIMIT;
    }
    if (FAILED(punk->QueryInterface(IID_PPV_ARGS(&sink_))) || !sink_) {
        return E_NOINTERFACE;
    }
    sink_cookie_ = kLangBarSinkCookie;
    *pdwCookie = sink_cookie_;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) {
    if (!sink_ || dwCookie != sink_cookie_) {
        return CONNECT_E_NOCONNECTION;
    }
    sink_.Reset();
    sink_cookie_ = 0;
    return S_OK;
}

void LangBarButton::UpdateState(const LangBarState& state) {
    state_ = state;
    if (sink_) {
        sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

ScopedIcon LangBarButton::CreateTextIcon(wchar_t ch) noexcept {
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
    const auto& palette = GetCurrentThemePalette();
    return CreateTransparentGlyphIcon(ch, size, palette.text_primary);
}

} // namespace qiqi
