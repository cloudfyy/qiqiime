// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/fltbarbtn.h"
#include "common/config.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/RAII.h"

#include <gsl/gsl>
#include <oleauto.h>
#include <array>
#include <bit>
#include <cwchar>

namespace qiqi {

namespace {

// Standard Windows TSF system tray input mode indicator GUID: {2C77A81E-41CC-4178-A3A7-5F8A987568E6}
constexpr GUID kGuidLbiInputMode = {
    0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}
};

constexpr DWORD kLangBarSinkCookie = 1;
constexpr UINT kRefreshThemeMessage = WM_APP;
std::atomic<uint64_t> next_langbar_log_id{0};

void AddMenuEntry(ITfMenu* menu, LangBarMenuId id, const wchar_t* text, bool checked = false, bool enabled = true) {
    if (!menu || !text) {
        return;
    }
    DWORD flags = checked ? TF_LBMENUF_CHECKED : 0;
    if (!enabled) {
        flags |= TF_LBMENUF_GRAYED;
    }
    std::ignore = log::CheckHResult(menu->AddMenuItem(
        std::to_underlying(id),
        flags,
        nullptr,
        nullptr,
        text,
        gsl::narrow_cast<ULONG>(wcslen(text)),
        nullptr), "LangBarButton::AddMenuItem");
}

void AddMenuSeparator(ITfMenu* menu) {
    if (menu) {
        std::ignore = log::CheckHResult(
            menu->AddMenuItem(0, TF_LBMENUF_SEPARATOR, nullptr, nullptr, nullptr, 0, nullptr),
            "LangBarButton::AddMenuItem(separator)");
    }
}

void AppendWin32PopupItem(
    HMENU menu,
    LangBarMenuId id,
    const wchar_t* text,
    bool checked = false,
    bool enabled = true) noexcept {
    if (!menu || !text) {
        return;
    }
    const UINT flags =
        MF_STRING |
        (checked
             ? MF_CHECKED
             : MF_UNCHECKED) |
        (enabled
             ? MF_ENABLED
             : (MF_GRAYED |
                MF_DISABLED));
    if (!::AppendMenuW(
            menu,
            flags,
            std::to_underlying(id),
            text)) {
        QIQI_LOG_WARN(
            "LangBarButton::AppendMenuW "
            "failed (error={})",
            ::GetLastError());
    }
}

} // namespace

LangBarButton::MenuItems LangBarButton::BuildSharedMenuItems(const LangBarState& state) noexcept {
    const auto& str = GetUiStrings(state.ui_language);
    return {{
        {LangBarMenuId::ToggleChinese, str.menu_toggle_chinese, state.chinese_mode},
        {LangBarMenuId::ToggleFullWidth, str.menu_toggle_full_width, state.full_width},
        {LangBarMenuId::TogglePunctuation, str.menu_toggle_punct, state.chinese_punct},
        {},
        {LangBarMenuId::ToggleStatusBar, str.menu_toggle_status_bar, state.show_status_bar},
        {LangBarMenuId::ToggleUiLanguage, str.menu_toggle_ui_lang},
        {LangBarMenuId::OpenSettings, str.menu_open_settings},
        {LangBarMenuId::OpenHelp, str.menu_open_help},
        {LangBarMenuId::OpenAbout, str.menu_open_about}
    }};
}

ScopedMenu LangBarButton::
    BuildSharedPopupMenu(
        const LangBarState& state,
        HWND owner_hwnd,
        ScopedGdiHandle<HBRUSH>&
            out_bg_brush) noexcept {
    const auto& palette =
        GetTaskbarThemePalette();
    ApplySystemThemeToMenuAndWindow(
        owner_hwnd,
        palette.is_dark);

    ScopedMenu menu(
        ::CreatePopupMenu());
    if (!menu) {
        return menu;
    }
    out_bg_brush.Reset(
        ::CreateSolidBrush(
            palette.window_bg));
    if (out_bg_brush) {
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack =
            out_bg_brush.Get();
        ::SetMenuInfo(
            menu.Get(),
            &info);
    }
    for (const auto& item :
         BuildSharedMenuItems(state)) {
        if (item.IsSeparator()) {
            ::AppendMenuW(
                menu.Get(),
                MF_SEPARATOR,
                0,
                nullptr);
        } else {
            AppendWin32PopupItem(
                menu.Get(),
                item.id,
                item.text,
                item.checked,
                item.enabled);
        }
    }
    return menu;
}

LangBarButton::LangBarButton(
    ActionCallback callback,
    MenuOwnerCallback menu_owner_callback) noexcept
    : log_id_(++next_langbar_log_id),
      action_callback_(std::move(callback)),
      menu_owner_callback_(std::move(menu_owner_callback)) {}

LangBarButton::~LangBarButton() noexcept {
    StopThemeNotifications();
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) noexcept {
    if (!pInfo) {
        return E_INVALIDARG;
    }
    const auto& str = GetUiStrings(state_.ui_language);
    pInfo->clsidService = kClsidQiQiTextService;
    pInfo->guidItem = kGuidLbiInputMode;
    pInfo->dwStyle =
        (UsesTsfOwnedMenu()
            ? TF_LBI_STYLE_BTN_MENU
            : TF_LBI_STYLE_BTN_BUTTON |
                TF_LBI_STYLE_BTN_MENU) |
        TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    wcscpy_s(pInfo->szDescription, str.langbar_desc);
    QIQI_LOG_DEBUG("LangBarButton::GetInfo(item={}, style=0x{:x}, visible={})",
                   log_id_, pInfo->dwStyle, visible_);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD* pdwStatus) noexcept {
    if (!pdwStatus) {
        return E_INVALIDARG;
    }
    *pdwStatus = visible_ ? 0 : TF_LBI_STATUS_HIDDEN;
    QIQI_LOG_DEBUG("LangBarButton::GetStatus(item={}, status=0x{:x}, sink={})",
                   log_id_, *pdwStatus, sink_ != nullptr);
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL fShow) noexcept {
    const bool visible = fShow != FALSE;
    QIQI_LOG_DEBUG("LangBarButton::Show(item={}, visible_before={}, visible_after={}, sink={})",
                   log_id_, visible_, visible, sink_ != nullptr);
    if (visible_ == visible) {
        return S_OK;
    }
    visible_ = visible;
    return NotifyUpdate(TF_LBI_STATUS);
}

STDMETHODIMP LangBarButton::GetTooltipString(BSTR* pbstrToolTip) noexcept {
    const auto& str = GetUiStrings(state_.ui_language);
    return CopyToBstr(str.langbar_tooltip, pbstrToolTip);
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT pt, const RECT* /*prcArea*/) {
    const ComPtr<LangBarButton> self_holder(this);
    QIQI_LOG_INFO(
        "LangBarButton::OnClick(item={}, click={}, point=({}, {}), app_container={})",
        log_id_,
        std::to_underlying(click),
        pt.x,
        pt.y,
        IsProcessAppContainer());
    if (!action_callback_) {
        return S_OK;
    }
    if (click == TF_LBI_CLK_LEFT) {
        return OnMenuSelect(std::to_underlying(LangBarMenuId::ToggleChinese));
    }
    if (click == TF_LBI_CLK_RIGHT) {
        if (pt.x == 0 && pt.y == 0) {
            ::GetCursorPos(&pt);
        }
        std::ignore = OnMenuSelect(std::to_underlying(LangBarMenuId::SyncMenuState));
        if (!action_callback_) return S_OK;
        if (UsesTsfOwnedMenu()) {
            return ShowNativePopupMenu(
                pt,
                true);
        }
        ShowSharedPopupMenu(state_, pt, [self_holder](LangBarMenuId id) {
            std::ignore = self_holder->OnMenuSelect(std::to_underlying(id));
        }, true);
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* pMenu) {
    if (!pMenu) {
        return E_INVALIDARG;
    }
    QIQI_LOG_INFO(
        "LangBarButton::InitMenu(item={}, app_container={})",
        log_id_,
        IsProcessAppContainer());
    const ComPtr<LangBarButton> self_holder(this);
    std::ignore = OnMenuSelect(std::to_underlying(LangBarMenuId::SyncMenuState));
    for (const auto& item : BuildSharedMenuItems(state_)) {
        if (item.IsSeparator()) {
            AddMenuSeparator(pMenu);
        } else {
            AddMenuEntry(pMenu, item.id, item.text, item.checked, item.enabled);
        }
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT wID) {
    const ComPtr<LangBarButton> self_holder(this);
    if (const auto callback = action_callback_) {
        callback(static_cast<LangBarMenuId>(wID));
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON* phIcon) noexcept {
    if (!phIcon) {
        return E_INVALIDARG;
    }
    ScopedIcon icon = CreateTextIcon(state_.chinese_mode ? L'中' : L'英');
    *phIcon = icon.Detach();
    if (!*phIcon) {
        QIQI_LOG_WARN("LangBarButton::GetIcon failed to create taskbar glyph icon (item={}, hr=0x{:08x}, visible={})",
                      log_id_, gsl::narrow_cast<unsigned int>(E_FAIL), visible_);
        return E_FAIL;
    }
    QIQI_LOG_DEBUG("LangBarButton::GetIcon(chinese={}, created=true) item={}, visible={}",
                   state_.chinese_mode, log_id_, visible_);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetText(BSTR* pbstrText) noexcept {
    const HRESULT hr = CopyToBstr(state_.chinese_mode ? L"中" : L"英", pbstrText);
    QIQI_LOG_DEBUG("LangBarButton::GetText(chinese={}, hr=0x{:08x}) item={}",
                   state_.chinese_mode, gsl::narrow_cast<unsigned int>(hr), log_id_);
    std::ignore = log::CheckHResult(hr, "LangBarButton::GetText");
    return hr;
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
    const ComPtr<LangBarButton> self_holder(this);
    ComPtr<ITfLangBarItemSink> sink;
    HRESULT hr = punk->QueryInterface(IID_PPV_ARGS(&sink));
    if (SUCCEEDED(hr) && !sink) hr = E_UNEXPECTED;
    QIQI_LOG_DEBUG("LangBarButton::AdviseSink(item={}, hr=0x{:08x})",
                   log_id_, gsl::narrow_cast<unsigned int>(hr));
    if (!log::CheckHResult(hr, "LangBarButton::QueryInterface(ITfLangBarItemSink)")) {
        return hr;
    }
    hr = StartThemeNotifications();
    if (FAILED(hr)) return hr;
    sink_ = std::move(sink);
    sink_cookie_ = kLangBarSinkCookie;
    *pdwCookie = sink_cookie_;
    QIQI_LOG_DEBUG("LangBarButton::AdviseSink(connected=true) item={}, cookie={}", log_id_, sink_cookie_);
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) noexcept {
    if (!sink_ || dwCookie != sink_cookie_) {
        return CONNECT_E_NOCONNECTION;
    }
    const ComPtr<LangBarButton> self_holder(this);
    ComPtr<ITfLangBarItemSink> sink = std::move(sink_);
    sink_cookie_ = 0;
    StopThemeNotifications();
    QIQI_LOG_DEBUG("LangBarButton::UnadviseSink(connected=false) item={}, cookie={}", log_id_, dwCookie);
    return S_OK;
}

void LangBarButton::UpdateState(const LangBarState& state) noexcept {
    QIQI_LOG_DEBUG("LangBarButton::UpdateState(chinese_before={}, chinese_after={}, sink={}) item={}, visible={}",
                   state_.chinese_mode, state.chinese_mode, sink_ != nullptr, log_id_, visible_);
    state_ = state;
    std::ignore = NotifyUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

HRESULT LangBarButton::NotifyUpdate(DWORD flags) noexcept {
    const ComPtr<LangBarButton> self_holder(this);
    const ComPtr<ITfLangBarItemSink> sink = sink_;
    if (!sink) {
        QIQI_LOG_DEBUG("LangBarButton::OnUpdate(item={}, flags=0x{:x}, sink=false, skipped=true)",
                       log_id_, flags);
        return S_OK;
    }
#pragma warning(suppress : 26447) // COM sinks report failure through HRESULT, not C++ exceptions.
    const HRESULT hr = sink->OnUpdate(flags);
    std::ignore = log::CheckHResult(hr, "LangBarButton::OnUpdate");
    QIQI_LOG_DEBUG("LangBarButton::OnUpdate(chinese={}, hr=0x{:08x}) item={}, flags=0x{:x}, visible={}, connected_after={}",
                   state_.chinese_mode, gsl::narrow_cast<unsigned int>(hr), log_id_, flags, visible_, sink_ != nullptr);
    return hr;
}

void LangBarButton::Disconnect() noexcept {
    const ComPtr<LangBarButton> self_holder(this);
    action_callback_ = {};
    menu_owner_callback_ = {};
    visible_ = false;
    ComPtr<ITfLangBarItemSink> sink = std::move(sink_);
    sink_cookie_ = 0;
    StopThemeNotifications();
    QIQI_LOG_DEBUG("LangBarButton::Disconnect(item={}, had_sink={})", log_id_, sink != nullptr);
}

bool LangBarButton::UsesTsfOwnedMenu() const noexcept {
    return force_app_container_for_test_ ||
        IsProcessAppContainer();
}

HWND LangBarButton::ResolveNativeMenuOwner() const {
    if (menu_owner_callback_) {
        const HWND owner =
            menu_owner_callback_();
        if (owner &&
            ::IsWindow(owner)) {
            DWORD owner_process = 0;
            const DWORD owner_thread =
                ::GetWindowThreadProcessId(
                    owner,
                    &owner_process);
            if (owner_thread ==
                    ::GetCurrentThreadId() &&
                owner_process ==
                    ::GetCurrentProcessId()) {
                return owner;
            }
        }
    }
    return theme_window_.Get();
}

HRESULT LangBarButton::ShowNativePopupMenu(
    POINT screen_pt,
    bool align_bottom) {
    if (!theme_window_ ||
        !::IsWindow(
            theme_window_.Get())) {
        const HRESULT window_result =
            StartThemeNotifications();
        if (FAILED(window_result)) {
            return window_result;
        }
    }
    if (!theme_window_ ||
        !::IsWindow(
            theme_window_.Get())) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_WINDOW_HANDLE);
    }
    const HWND owner =
        ResolveNativeMenuOwner();
    if (!owner ||
        !::IsWindow(owner)) {
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_WINDOW_HANDLE);
    }
    ScopedGdiHandle<HBRUSH> background;
    ScopedMenu menu =
        BuildSharedPopupMenu(
            state_,
            theme_window_.Get(),
            background);
    if (!menu) {
        const DWORD error =
            ::GetLastError();
        QIQI_LOG_WARN(
            "LangBarButton::"
            "ShowNativePopupMenu"
            "(item={}, open=false, "
            "error={})",
            log_id_,
            error);
        return HRESULT_FROM_WIN32(
            error != ERROR_SUCCESS
                ? error
                : ERROR_NOT_ENOUGH_MEMORY);
    }
    if (::GetMenuItemCount(
            menu.Get()) !=
        gsl::narrow_cast<int>(
            BuildSharedMenuItems(state_)
                .size())) {
        QIQI_LOG_WARN(
            "LangBarButton::"
            "ShowNativePopupMenu"
            "(item={}, open=false, "
            "error={})",
            log_id_,
            ERROR_INVALID_DATA);
        return HRESULT_FROM_WIN32(
            ERROR_INVALID_DATA);
    }

    const bool set_foreground =
        ::SetForegroundWindow(owner) != FALSE;
    const DWORD foreground_error =
        set_foreground
            ? ERROR_SUCCESS
            : ::GetLastError();
    if (!set_foreground) {
        QIQI_LOG_WARN(
            "LangBarButton::"
            "ShowNativePopupMenu"
            "(item={}, owner_foreground=false, "
            "error={})",
            log_id_,
            foreground_error);
    }
    ::SetLastError(ERROR_SUCCESS);
    const UINT flags =
        TPM_NONOTIFY |
        TPM_RETURNCMD |
        TPM_RIGHTBUTTON |
        TPM_LEFTALIGN |
        TPM_HORPOSANIMATION |
        (align_bottom
             ? TPM_BOTTOMALIGN
             : TPM_TOPALIGN);
    const UINT command =
        ::TrackPopupMenuEx(
            menu.Get(),
            flags,
            screen_pt.x,
            screen_pt.y,
            owner,
            nullptr);
    const DWORD error =
        ::GetLastError();
    if (command == 0 &&
        error != ERROR_SUCCESS) {
        QIQI_LOG_WARN(
            "LangBarButton::"
            "ShowNativePopupMenu"
            "(item={}, open=false, "
            "error={})",
            log_id_,
            error);
        return HRESULT_FROM_WIN32(
            error);
    }
    const bool posted_null =
        ::PostMessageW(
            owner,
            WM_NULL,
            0,
            0) != FALSE;
    const DWORD post_error =
        posted_null
            ? ERROR_SUCCESS
            : ::GetLastError();
    if (!posted_null) {
        QIQI_LOG_WARN(
            "LangBarButton::"
            "ShowNativePopupMenu"
            "(item={}, post_null=false, "
            "error={})",
            log_id_,
            post_error);
    }
    QIQI_LOG_INFO(
        "LangBarButton::"
        "ShowNativePopupMenu"
        "(item={}, owner={}, "
        "owner_source={}, command={}, "
        "foreground_error={}, "
        "track_error={}, post_error={})",
        log_id_,
        std::bit_cast<uintptr_t>(
            owner),
        owner == theme_window_.Get()
            ? "fallback"
            : "context",
        command,
        foreground_error,
        error,
        post_error);
    if (command != 0) {
        std::ignore =
            OnMenuSelect(command);
    }
    return S_OK;
}

ScopedIcon LangBarButton::CreateTextIcon(wchar_t ch) noexcept {
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
    return CreateTransparentGlyphIcon(
        ch, size, kImeModeIconGlyphColor, kImeModeIconFontWeight,
        kImeModeIconStroke);
}

HRESULT LangBarButton::StartThemeNotifications() noexcept {
    if (theme_window_) return S_OK;
    const HINSTANCE instance = g_dll_instance ? g_dll_instance : ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &LangBarButton::ThemeWndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kLangBarThemeWndClass;
    if (!::RegisterClassExW(&wc)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            QIQI_LOG_WARN("LangBarButton::RegisterClassExW(theme) failed (error={})", error);
            return HRESULT_FROM_WIN32(error);
        }
    }
    // Message-only windows do not receive system theme broadcasts.
    theme_window_.Reset(::CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kLangBarThemeWndClass, L"", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, this));
    if (!theme_window_) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("LangBarButton::CreateWindowExW(theme) failed (error={})", error);
        return HRESULT_FROM_WIN32(error);
    }
    return S_OK;
}

void LangBarButton::StopThemeNotifications() noexcept {
    std::ignore = DestroyWindowOnOwnerThread(theme_window_, "LangBarButton::DestroyThemeWindow");
    theme_update_pending_ = false;
}

LRESULT CALLBACK LangBarButton::ThemeWndProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetLastError(ERROR_SUCCESS);
        if (!::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams))) {
            const DWORD error = ::GetLastError();
            if (error != ERROR_SUCCESS) {
                QIQI_LOG_WARN("LangBarButton::SetWindowLongPtrW(theme) failed (error={})", error);
                return FALSE;
            }
        }
    }
    auto* button = reinterpret_cast<LangBarButton*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (button) {
        switch (message) {
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (button->theme_window_.Get() == hwnd) std::ignore = button->theme_window_.Detach();
            break;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            // Let the Shell finish its broadcast before calling its TSF sink.
            if (!button->theme_update_pending_) {
                if (::PostMessageW(hwnd, kRefreshThemeMessage, 0, 0)) {
                    button->theme_update_pending_ = true;
                } else {
                    const DWORD error = ::GetLastError();
                    QIQI_LOG_WARN("LangBarButton::PostMessageW(theme) failed (error={})", error);
                }
            }
            return 0;
        case kRefreshThemeMessage: {
            const ComPtr<LangBarButton> self_holder(button);
            button->theme_update_pending_ = false;
            std::ignore = button->NotifyUpdate(TF_LBI_ICON);
            return 0;
        }
        default:
            break;
        }
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace qiqi
