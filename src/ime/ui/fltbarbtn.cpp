// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/fltbarbtn.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/RAII.h"

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
        static_cast<UINT>(id),
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
    HMENU menu, LangBarMenuId id, const wchar_t* text, bool checked = false, bool enabled = true) noexcept {
    if (!menu || !text) {
        return;
    }
    const UINT flags = MF_STRING |
        (checked ? MF_CHECKED : MF_UNCHECKED) |
        (enabled ? MF_ENABLED : (MF_GRAYED | MF_DISABLED));
    if (!::AppendMenuW(menu, flags, static_cast<UINT_PTR>(id), text)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("LangBarButton::AppendMenuW failed (error={})", error);
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

ScopedMenu LangBarButton::BuildSharedPopupMenu(
    const LangBarState& state, HWND owner_hwnd, ScopedGdiHandle<HBRUSH>& out_bg_brush) noexcept {
    const auto& palette = GetTaskbarThemePalette();
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

    for (const auto& item : BuildSharedMenuItems(state)) {
        if (item.IsSeparator()) {
            ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
        } else {
            AppendWin32PopupItem(menu.Get(), item.id, item.text, item.checked, item.enabled);
        }
    }
    return menu;
}

LangBarButton::LangBarButton(ActionCallback cb) noexcept
    : log_id_(++next_langbar_log_id), action_callback_(std::move(cb)) {}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO* pInfo) noexcept {
    if (!pInfo) {
        return E_INVALIDARG;
    }
    const auto& str = GetUiStrings(state_.ui_language);
    pInfo->clsidService = kClsidQiQiTextService;
    pInfo->guidItem = kGuidLbiInputMode;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY |
                     TF_LBI_STYLE_TEXTCOLORICON;
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
    if (!action_callback_) {
        return S_OK;
    }
    if (click == TF_LBI_CLK_LEFT) {
        return OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleChinese));
    }
    if (click == TF_LBI_CLK_RIGHT) {
        if (pt.x == 0 && pt.y == 0) {
            ::GetCursorPos(&pt);
        }
        std::ignore = OnMenuSelect(static_cast<UINT>(LangBarMenuId::SyncMenuState));
        if (!action_callback_) return S_OK;
        ShowSharedPopupMenu(state_, pt, [self_holder](LangBarMenuId id) {
            std::ignore = self_holder->OnMenuSelect(static_cast<UINT>(id));
        }, true);
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu* pMenu) {
    if (!pMenu) {
        return E_INVALIDARG;
    }
    const ComPtr<LangBarButton> self_holder(this);
    std::ignore = OnMenuSelect(static_cast<UINT>(LangBarMenuId::SyncMenuState));
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
    sink_ = std::move(sink);
    sink_cookie_ = kLangBarSinkCookie;
    *pdwCookie = sink_cookie_;
    QIQI_LOG_DEBUG("LangBarButton::AdviseSink(connected=true) item={}, cookie={}", log_id_, sink_cookie_);
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD dwCookie) {
    if (!sink_ || dwCookie != sink_cookie_) {
        return CONNECT_E_NOCONNECTION;
    }
    const ComPtr<LangBarButton> self_holder(this);
    ComPtr<ITfLangBarItemSink> sink = std::move(sink_);
    sink_cookie_ = 0;
    QIQI_LOG_DEBUG("LangBarButton::UnadviseSink(connected=false) item={}, cookie={}", log_id_, dwCookie);
    return S_OK;
}

void LangBarButton::UpdateState(const LangBarState& state) {
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
    const HRESULT hr = sink->OnUpdate(flags);
    std::ignore = log::CheckHResult(hr, "LangBarButton::OnUpdate");
    QIQI_LOG_DEBUG("LangBarButton::OnUpdate(chinese={}, hr=0x{:08x}) item={}, flags=0x{:x}, visible={}, connected_after={}",
                   state_.chinese_mode, gsl::narrow_cast<unsigned int>(hr), log_id_, flags, visible_, sink_ != nullptr);
    return hr;
}

void LangBarButton::Disconnect() noexcept {
    const ComPtr<LangBarButton> self_holder(this);
    action_callback_ = {};
    visible_ = false;
    ComPtr<ITfLangBarItemSink> sink = std::move(sink_);
    sink_cookie_ = 0;
    QIQI_LOG_DEBUG("LangBarButton::Disconnect(item={}, had_sink={})", log_id_, sink != nullptr);
}

ScopedIcon LangBarButton::CreateTextIcon(wchar_t ch) noexcept {
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
    const ScopedMemoryDC dc(::CreateCompatibleDC(nullptr));
    const ScopedGdiHandle<HBITMAP> mask(::CreateBitmap(size, size * 2, 1, 1, nullptr));
    const auto font = CreateUiFont(size - 2, FW_SEMIBOLD, NONANTIALIASED_QUALITY);
    if (!dc || !mask || !font) {
        QIQI_LOG_WARN("LangBarButton::CreateTextIcon failed to allocate monochrome drawing resources");
        return {};
    }
    {
        const ScopedSelectObject select_mask(dc.Get(), mask.Get());
        const ScopedSelectObject select_font(dc.Get(), font.Get());
        if (!select_mask || !select_font) {
            QIQI_LOG_WARN("LangBarButton::CreateTextIcon failed to select monochrome drawing resources");
            return {};
        }
        // A monochrome HICON has stacked AND/XOR masks, with no color bitmap.
        // AND=1/XOR=0 is transparent; AND=0/XOR=0 is a black theme-color glyph.
        if (!::PatBlt(dc.Get(), 0, 0, size, size, WHITENESS) ||
            !::PatBlt(dc.Get(), 0, size, size, size, BLACKNESS) ||
            ::SetBkMode(dc.Get(), TRANSPARENT) == 0 ||
            ::SetTextColor(dc.Get(), RGB(0, 0, 0)) == CLR_INVALID) {
            QIQI_LOG_WARN("LangBarButton::CreateTextIcon failed to initialize monochrome masks");
            return {};
        }
        RECT rect{0, 0, size, size};
        const std::array<wchar_t, 2> text{ch, L'\0'};
        if (!::DrawTextW(dc.Get(), text.data(), 1, &rect,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX)) {
            QIQI_LOG_WARN("LangBarButton::CreateTextIcon failed to draw monochrome glyph");
            return {};
        }
    }
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask.Get();
    ScopedIcon icon(::CreateIconIndirect(&info));
    if (!icon) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("LangBarButton::CreateTextIcon::CreateIconIndirect failed (error={})", error);
    }
    return icon;
}

} // namespace qiqi
