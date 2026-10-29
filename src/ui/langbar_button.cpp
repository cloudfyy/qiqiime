#include "ui/langbar_button.h"
#include "common/guids.h"
#include "common/scoped_handle.h"

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
    : action_callback_(std::move(cb)) {}

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
    pInfo->clsidService = kClsidQiQiTextService;
    pInfo->guidItem = kGuidLbiInputMode;
    pInfo->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY;
    pInfo->ulSort = 0;
    wcscpy_s(pInfo->szDescription, L"QiQi 五笔状态与菜单");
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
    if (!pbstrToolTip) {
        return E_INVALIDARG;
    }
    *pbstrToolTip = ::SysAllocString(L"QiQi 五笔 (左键切换中英，右键打开菜单)");
    return *pbstrToolTip ? S_OK : E_OUTOFMEMORY;
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

    AddMenuEntry(pMenu, LangBarMenuId::ToggleChinese, L"中文输入模式 (Shift / Ctrl+Space)", state_.chinese_mode);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleFullWidth, L"全角字符模式 (Shift+Space)", state_.full_width);
    AddMenuEntry(pMenu, LangBarMenuId::TogglePunctuation, L"中文标点符号 (Ctrl+.)", state_.chinese_punct);
    AddMenuSeparator(pMenu);
    AddMenuEntry(pMenu, LangBarMenuId::DictJidian, L"词库：极点五笔 86", state_.primary_dict == PrimaryDict::Jidian);
    AddMenuEntry(pMenu, LangBarMenuId::DictHaifeng, L"词库：海峰五笔 86 (超大字符集)", state_.primary_dict == PrimaryDict::Haifeng);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleExtraDict, L"启用极点现代扩展词库", state_.enable_jidian_extra);
    AddMenuSeparator(pMenu);
    AddMenuEntry(pMenu, LangBarMenuId::ToggleStatusBar, L"显示桌面浮动状态条", state_.show_status_bar);
    AddMenuEntry(pMenu, LangBarMenuId::OpenSettings, L"输入法属性设置...");
    AddMenuEntry(pMenu, LangBarMenuId::OpenConfigFile, L"编辑配置文件 (config.ini)");
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
    if (!pbstrText) {
        return E_INVALIDARG;
    }
    *pbstrText = ::SysAllocString(state_.chinese_mode ? L"中" : L"英");
    return *pbstrText ? S_OK : E_OUTOFMEMORY;
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

    RECT rc{0, 0, size, size};
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(RGB(35, 105, 200)));
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
