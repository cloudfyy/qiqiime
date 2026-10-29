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

void AddMenuEntry(ITfMenu* menu, LangBarMenuId id, const wchar_t* text, bool checked = false) {
    if (!menu || !text) {
        return;
    }
    const DWORD flags = checked ? TF_LBMENUF_CHECKED : 0;
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

} // namespace

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

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT /*pt*/, const RECT* /*prcArea*/) {
    if (click == TF_LBI_CLK_LEFT && action_callback_) {
        action_callback_(LangBarMenuId::ToggleChinese);
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
    AddMenuEntry(pMenu, LangBarMenuId::DictJidian, str.menu_dict_jidian, state_.primary_dict == PrimaryDict::Jidian);
    AddMenuEntry(pMenu, LangBarMenuId::DictHaifeng, str.menu_dict_haifeng, state_.primary_dict == PrimaryDict::Haifeng);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleExtraDict, str.menu_toggle_extra_dict, state_.enable_jidian_extra);
    AddMenuSeparator(pMenu);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleStatusBar, str.menu_toggle_status_bar, state_.show_status_bar);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleUiLanguage, str.menu_toggle_ui_lang);
    AddMenuEntry(pMenu, LangBarMenuId::OpenSettings, str.menu_open_settings);
    AddMenuEntry(pMenu, LangBarMenuId::OpenConfigFile, str.menu_open_config_file);
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
    *phIcon = CreateTextIcon(state_.chinese_mode ? L'中' : L'英');
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

HICON LangBarButton::CreateTextIcon(wchar_t ch) noexcept {
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
    const ScopedWindowDC screen_dc(nullptr);
    if (!screen_dc) {
        return nullptr;
    }

    const ScopedMemoryDC mem_dc(::CreateCompatibleDC(screen_dc.Get()));
    if (!mem_dc) {
        return nullptr;
    }

    const ScopedGdiHandle<HBITMAP> color_bmp(::CreateCompatibleBitmap(screen_dc.Get(), size, size));
    const ScopedGdiHandle<HBITMAP> mask_bmp(::CreateBitmap(size, size, 1, 1, nullptr));
    if (!color_bmp || !mask_bmp) {
        return nullptr;
    }

    const ScopedSelectObject sel_bmp(mem_dc.Get(), color_bmp.Get());

    const auto& palette = GetCurrentThemePalette();
    RECT rc{0, 0, size, size};
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(palette.logo_bg));
    ::FillRect(mem_dc.Get(), &rc, bg_brush.Get());

    LOGFONTW lf{};
    lf.lfHeight = -size + 2;
    lf.lfWeight = FW_BOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    const ScopedGdiHandle<HFONT> font(::CreateFontIndirectW(&lf));
    const ScopedSelectObject sel_font(mem_dc.Get(), font.Get());

    ::SetBkMode(mem_dc.Get(), TRANSPARENT);
    ::SetTextColor(mem_dc.Get(), RGB(255, 255, 255));
    const std::array<wchar_t, 2> text = {ch, L'\0'};
    ::DrawTextW(mem_dc.Get(), text.data(), 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask_bmp.Get();
    ii.hbmColor = color_bmp.Get();
    return ::CreateIconIndirect(&ii);
}

} // namespace qiqi
