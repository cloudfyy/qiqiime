#include "common.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/monitor_dpi.h"
#include "common/theme.h"
#include "ui/langbar_button.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <array>
#include <string>
#include <vector>

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

namespace {

class MockTfMenu final : public ITfMenu {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfMenu)) {
            *ppvObj = static_cast<ITfMenu*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP AddMenuItem(UINT uId, DWORD dwFlags, HBITMAP, HBITMAP, const WCHAR* pch, ULONG cch, ITfMenu**) override {
        if (dwFlags & TF_LBMENUF_SEPARATOR) {
            ++separators_;
        } else {
            item_ids_.push_back(uId);
            item_flags_.push_back(dwFlags);
            if (pch && cch > 0) {
                item_texts_.emplace_back(pch, cch);
            }
        }
        return S_OK;
    }

    std::vector<UINT> item_ids_;
    std::vector<DWORD> item_flags_;
    std::vector<std::wstring> item_texts_;
    size_t separators_ = 0;
};

class MockLangBarItemSink final : public ITfLangBarItemSink {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItemSink)) {
            *ppvObj = static_cast<ITfLangBarItemSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 1; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }
    STDMETHODIMP OnUpdate(DWORD dwFlags) override {
        last_update_flags_ = dwFlags;
        return S_OK;
    }

    DWORD last_update_flags_ = 0;
};

HWND FindQuickSettingsMenu() {
    return FindThreadWindowByClass(kMenuWndClass);
}

void CloseQuickSettingsMenu() {
    const HWND menu = FindQuickSettingsMenu();
    QIQI_TEST_CHECK(menu != nullptr);
    ::SendMessageW(menu, WM_CLOSE, 0, 0);
}

void TestLangBarButton() {
    LangBarMenuId last_action = static_cast<LangBarMenuId>(0);
    auto* btn = new LangBarButton([&last_action](LangBarMenuId id) { last_action = id; });

    QIQI_TEST_CHECK(btn->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(btn->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);

    ComPtr<ITfLangBarItem> lb_item;
    ComPtr<ITfLangBarItemButton> lb_btn;
    ComPtr<ITfSource> lb_src;
    QIQI_TEST_CHECK(SUCCEEDED(btn->QueryInterface(IID_PPV_ARGS(&lb_item))) && lb_item);
    QIQI_TEST_CHECK(SUCCEEDED(btn->QueryInterface(IID_PPV_ARGS(&lb_btn))));
    QIQI_TEST_CHECK(SUCCEEDED(btn->QueryInterface(IID_PPV_ARGS(&lb_src))));
    lb_item.Reset();

    // Info, Status, Show, Tooltip, Text, Icon
    QIQI_TEST_CHECK(btn->GetInfo(nullptr) == E_INVALIDARG);
    TF_LANGBARITEMINFO info{};
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(IsEqualCLSID(info.clsidService, kClsidQiQiTextService));
    QIQI_TEST_CHECK((info.dwStyle & TF_LBI_STYLE_TEXTCOLORICON) == 0);

    QIQI_TEST_CHECK(btn->GetStatus(nullptr) == E_INVALIDARG);
    DWORD status = 99;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetStatus(&status)) && status == 0);
    QIQI_TEST_CHECK(SUCCEEDED(btn->Show(TRUE)));

    QIQI_TEST_CHECK(btn->GetTooltipString(nullptr) == E_INVALIDARG);
    ScopedBstr tooltip;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(tooltip.Put())) && tooltip.IsValid());

    QIQI_TEST_CHECK(btn->GetText(nullptr) == E_INVALIDARG);
    ScopedBstr text;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(text.Put())) && std::wstring(text.Get()) == L"中");

    QIQI_TEST_CHECK(btn->GetIcon(nullptr) == E_INVALIDARG);
    ScopedIcon icon;
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetIcon(icon.Put())) && icon.IsValid());

    // Verify CreateTransparentGlyphIcon creates a 32-bit ARGB icon with transparent corner pixels
    {
        const ScopedIcon glyph_icon = CreateTransparentGlyphIcon(L'奇', 32, RGB(35, 105, 200));
        QIQI_TEST_CHECK(glyph_icon.IsValid());
        ICONINFO ii{};
        QIQI_TEST_CHECK(::GetIconInfo(glyph_icon.Get(), &ii) != FALSE);
        const ScopedGdiHandle<HBITMAP> hbm_color(ii.hbmColor);
        const ScopedGdiHandle<HBITMAP> hbm_mask(ii.hbmMask);
        BITMAP bm{};
        QIQI_TEST_CHECK(::GetObjectW(hbm_color.Get(), sizeof(bm), &bm) == sizeof(bm));
        QIQI_TEST_CHECK(bm.bmWidth == 32 && bm.bmHeight == 32 && bm.bmBitsPixel == 32);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 32;
        bmi.bmiHeader.biHeight = -32;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint32_t> pixels(32 * 32, 0xFFFFFFFFu);
        const ScopedWindowDC sdc(nullptr);
        QIQI_TEST_CHECK(::GetDIBits(sdc.Get(), hbm_color.Get(), 0, 32, pixels.data(), &bmi, DIB_RGB_COLORS) == 32);
        // Top-left corner (0,0) must be completely transparent (alpha == 0), while center area has non-zero alpha
        QIQI_TEST_CHECK(pixels.front() == 0u);
        bool has_opaque_glyph_pixel = false;
        for (const uint32_t px : pixels) {
            if ((px >> 24u) > 0u) {
                has_opaque_glyph_pixel = true;
                break;
            }
        }
        QIQI_TEST_CHECK(has_opaque_glyph_pixel);
    }

    // AdviseSink / UpdateState / UnadviseSink
    MockLangBarItemSink sink;
    DWORD cookie = 0;
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, nullptr, &cookie) == E_INVALIDARG);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_IUnknown, &sink, &cookie) == CONNECT_E_CANNOTCONNECT);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie)) && cookie != 0);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == CONNECT_E_ADVISELIMIT);

    LangBarState st{};
    st.chinese_mode = false;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(sink.last_update_flags_ != 0);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(text.Put())) && std::wstring(text.Get()) == L"英");

    QIQI_TEST_CHECK(lb_src->UnadviseSink(9999) == CONNECT_E_NOCONNECTION);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->UnadviseSink(cookie)));

    // Menu & Click (Chinese UI)
    st.ui_language = UiLanguage::ZhCn;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(std::wstring(info.szDescription) == L"QiQi 五笔状态与菜单");
    QIQI_TEST_CHECK(btn->InitMenu(nullptr) == E_INVALIDARG);
    MockTfMenu menu_zh;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_zh)));
    QIQI_TEST_CHECK(menu_zh.item_ids_.size() == 10 && menu_zh.separators_ == 2);
    QIQI_TEST_CHECK(!menu_zh.item_texts_.empty() &&
                    menu_zh.item_texts_.front().find(L"中文输入模式") != std::wstring::npos);

    // Test English UI language on LangBarButton (GetInfo, GetTooltipString, InitMenu)
    st.ui_language = UiLanguage::EnUs;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    QIQI_TEST_CHECK(std::wstring(info.szDescription) == L"QiQi Wubi Status & Menu");
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(tooltip.Put())) &&
                    std::wstring(tooltip.Get()).find(L"Left-click") != std::wstring::npos);
    MockTfMenu menu_en;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_en)));
    QIQI_TEST_CHECK(menu_en.item_ids_.size() == 10 && menu_en.separators_ == 2);
    QIQI_TEST_CHECK(!menu_en.item_texts_.empty() &&
                    menu_en.item_texts_.front().find(L"Chinese Input Mode") != std::wstring::npos);

    POINT pt{0, 0};
    QIQI_TEST_CHECK(SUCCEEDED(btn->OnClick(TF_LBI_CLK_LEFT, pt, nullptr)));
    QIQI_TEST_CHECK(last_action == LangBarMenuId::ToggleChinese);

    // Test right-click on taskbar tray button (TF_LBI_CLK_RIGHT) popping up context menu:
    // 1) Dismissed without selection
    ::SetTimer(nullptr, 6, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        CloseQuickSettingsMenu();
    });
    QIQI_TEST_CHECK(SUCCEEDED(btn->OnClick(TF_LBI_CLK_RIGHT, pt, nullptr)));

    // 2) Explicit screen coordinates (dismissed by timer)
    ::SetTimer(nullptr, 7, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        ::KillTimer(nullptr, id);
        CloseQuickSettingsMenu();
    });
    const POINT explicit_pt{200, 200};
    QIQI_TEST_CHECK(SUCCEEDED(btn->OnClick(TF_LBI_CLK_RIGHT, explicit_pt, nullptr)));

    // 3) LangBarButton with empty callback safely ignores OnClick
    {
        const ComPtr<LangBarButton> no_cb_btn = MakeComObject<LangBarButton>();
        QIQI_TEST_CHECK(SUCCEEDED(no_cb_btn->OnClick(TF_LBI_CLK_LEFT, pt, nullptr)));
        QIQI_TEST_CHECK(SUCCEEDED(no_cb_btn->OnClick(TF_LBI_CLK_RIGHT, pt, nullptr)));
    }

    QIQI_TEST_CHECK(SUCCEEDED(btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictHaifeng))));
    QIQI_TEST_CHECK(last_action == LangBarMenuId::DictHaifeng);

    lb_src.Reset();
    lb_btn.Reset();
    QIQI_TEST_CHECK(btn->Release() == 0);
}

void TestTaskbarThemeIcons() {
    const auto restore_overrides = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetTaskbarDarkOverrideForTest(std::nullopt);
    });
    for (const bool app_dark : {false, true}) {
        for (const bool taskbar_dark : {false, true}) {
            SetThemeDarkOverrideForTest(app_dark);
            SetTaskbarDarkOverrideForTest(taskbar_dark);
            QIQI_TEST_CHECK(IsSystemDarkTheme() == app_dark);
            QIQI_TEST_CHECK(IsTaskbarDarkTheme() == taskbar_dark);
            QIQI_TEST_CHECK(GetTaskbarIconColor() == (taskbar_dark ? RGB(255, 255, 255) : RGB(0, 0, 0)));

            const ComPtr<LangBarButton> button = MakeComObject<LangBarButton>();
            TF_LANGBARITEMINFO item_info{};
            QIQI_TEST_CHECK(button->GetInfo(&item_info) == S_OK);
            QIQI_TEST_CHECK((item_info.dwStyle & TF_LBI_STYLE_TEXTCOLORICON) == 0);

            for (const bool chinese : {false, true}) {
                LangBarState state{};
                state.chinese_mode = chinese;
                button->UpdateState(state);
                ScopedIcon icon;
                QIQI_TEST_CHECK(button->GetIcon(icon.Put()) == S_OK);
                ICONINFO info{};
                QIQI_TEST_CHECK(::GetIconInfo(icon.Get(), &info));
                const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
                const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
                QIQI_TEST_CHECK(mask.IsValid() && color.IsValid());
                BITMAP bitmap{};
                QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
                QIQI_TEST_CHECK(bitmap.bmBitsPixel == 32 && bitmap.bmHeight == bitmap.bmWidth);

                BITMAPINFO bmi{};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = bitmap.bmWidth;
                bmi.bmiHeader.biHeight = -bitmap.bmHeight;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                std::vector<uint32_t> pixels(
                    gsl::narrow_cast<size_t>(bitmap.bmWidth) * gsl::narrow_cast<size_t>(bitmap.bmHeight));
                const ScopedWindowDC screen_dc(nullptr);
                QIQI_TEST_CHECK(::GetDIBits(
                    screen_dc.Get(), color.Get(), 0, gsl::narrow_cast<UINT>(bitmap.bmHeight),
                    pixels.data(), &bmi, DIB_RGB_COLORS) == bitmap.bmHeight);

                int glyph_pixels = 0;
                int transparent_pixels = 0;
                int bright_pixels = 0;
                for (const uint32_t px : pixels) {
                    const uint32_t alpha = (px >> 24u) & 0xFFu;
                    const uint32_t rgb = px & 0x00FFFFFFu;
                    if (alpha == 0) {
                        QIQI_TEST_CHECK(rgb == 0);
                        ++transparent_pixels;
                    } else {
                        ++glyph_pixels;
                        if (taskbar_dark) {
                            if (rgb == 0x00FFFFFFu) {
                                ++bright_pixels;
                            }
                        } else {
                            QIQI_TEST_CHECK(rgb == 0);
                        }
                    }
                }
                QIQI_TEST_CHECK(glyph_pixels >= 10);
                QIQI_TEST_CHECK(transparent_pixels > glyph_pixels);
                if (taskbar_dark) {
                    QIQI_TEST_CHECK(bright_pixels > 0);
                }
                QIQI_TEST_CHECK(pixels.front() == 0);
            }
        }
    }
}

void TestSharedMenuItems() {
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const PrimaryDict dictionary : {PrimaryDict::Jidian, PrimaryDict::Haifeng}) {
            for (const bool enabled : {false, true}) {
                LangBarState state{};
                state.ui_language = language;
                state.primary_dict = dictionary;
                state.chinese_mode = enabled;
                state.full_width = enabled;
                state.chinese_punct = enabled;
                state.show_status_bar = enabled;
                state.enable_jidian_extra = enabled;
                const auto items = LangBarButton::BuildSharedMenuItems(state);
                ScopedGdiHandle<HBRUSH> brush;
                const ScopedMenu menu = LangBarButton::BuildSharedPopupMenu(state, nullptr, brush);
                QIQI_TEST_CHECK(menu && ::GetMenuItemCount(menu.Get()) == gsl::narrow_cast<int>(items.size()));
                const auto button = MakeComObject<LangBarButton>();
                button->UpdateState(state);
                MockTfMenu tsf_menu;
                QIQI_TEST_CHECK(button->InitMenu(&tsf_menu) == S_OK);
                QIQI_TEST_CHECK(tsf_menu.separators_ == 2);
                size_t command_index = 0;
                for (size_t i = 0; i < items.size(); ++i) {
                    const auto& item = items.at(i);
                    const UINT flags = ::GetMenuState(menu.Get(), gsl::narrow_cast<UINT>(i), MF_BYPOSITION);
                    QIQI_TEST_CHECK(((flags & MF_SEPARATOR) != 0) == item.IsSeparator());
                    if (item.IsSeparator()) {
                        continue;
                    }
                    QIQI_TEST_CHECK(::GetMenuItemID(menu.Get(), gsl::narrow_cast<int>(i)) == static_cast<UINT>(item.id));
                    QIQI_TEST_CHECK(((flags & MF_CHECKED) != 0) == item.checked);
                    QIQI_TEST_CHECK(((flags & (MF_DISABLED | MF_GRAYED)) == 0) == item.enabled);
                    std::array<wchar_t, 256> text{};
                    ::GetMenuStringW(menu.Get(), static_cast<UINT>(item.id), text.data(),
                                     gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND);
                    QIQI_TEST_CHECK(std::wstring_view(text.data()) == item.text);
                    QIQI_TEST_CHECK(tsf_menu.item_ids_.at(command_index) == static_cast<UINT>(item.id));
                    QIQI_TEST_CHECK(tsf_menu.item_texts_.at(command_index) == item.text);
                    const DWORD tsf_flags = tsf_menu.item_flags_.at(command_index);
                    QIQI_TEST_CHECK(((tsf_flags & TF_LBMENUF_CHECKED) != 0) == item.checked);
                    QIQI_TEST_CHECK(((tsf_flags & TF_LBMENUF_GRAYED) == 0) == item.enabled);
                    ++command_index;
                }
                QIQI_TEST_CHECK(command_index == tsf_menu.item_ids_.size());
            }
        }
    }
}

void TestQuickSettingsMenu() {
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        LangBarState state{};
        state.ui_language = UiLanguage::ZhCn;
        std::vector<LangBarMenuId> actions;
        bool closed_before_opening_window = false;
        static int step = 0;
        step = 0;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu != nullptr && ::IsWindowVisible(menu));
            static constexpr std::array commands{LangBarMenuId::ToggleChinese,     LangBarMenuId::DictHaifeng,
                                                 LangBarMenuId::ToggleExtraDict,   LangBarMenuId::DictJidian,
                                                 LangBarMenuId::ToggleExtraDict,   LangBarMenuId::ToggleFullWidth,
                                                 LangBarMenuId::TogglePunctuation, LangBarMenuId::ToggleStatusBar,
                                                 LangBarMenuId::ToggleUiLanguage,  LangBarMenuId::OpenSettings};
            const HWND original = menu;
            const auto command = commands.at(gsl::narrow_cast<size_t>(step));
            if (command == LangBarMenuId::OpenSettings)
                ::KillTimer(nullptr, id);
            ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(static_cast<UINT>(command), BN_CLICKED), 0);
            if (LangBarButton::KeepsMenuOpen(command)) {
                QIQI_TEST_CHECK(FindQuickSettingsMenu() == original && ::IsWindowVisible(original));
            } else {
                QIQI_TEST_CHECK(!::IsWindowVisible(original));
            }
            if (step == 1) {
                QIQI_TEST_CHECK(
                    !::IsWindowEnabled(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleExtraDict))));
            }
            if (step == 3) {
                QIQI_TEST_CHECK(
                    ::IsWindowEnabled(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleExtraDict))));
            }
            if (step == 8) {
                std::array<wchar_t, 256> label{};
                ::GetDlgItemTextW(menu, static_cast<int>(LangBarMenuId::ToggleChinese), label.data(),
                                  gsl::narrow_cast<int>(label.size()));
                QIQI_TEST_CHECK(std::wstring_view(label.data()) == kEnUsStrings.menu_toggle_chinese);
                ::SendMessageW(menu, WM_THEMECHANGED, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetCurrentThemePalette().window_bg);
            }
            ++step;
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            actions.push_back(action);
            switch (action) {
                case LangBarMenuId::ToggleChinese:
                    state.chinese_mode = !state.chinese_mode;
                    break;
                case LangBarMenuId::DictHaifeng:
                    state.primary_dict = PrimaryDict::Haifeng;
                    break;
                case LangBarMenuId::DictJidian:
                    state.primary_dict = PrimaryDict::Jidian;
                    break;
                case LangBarMenuId::ToggleExtraDict:
                    state.enable_jidian_extra = !state.enable_jidian_extra;
                    break;
                case LangBarMenuId::ToggleFullWidth:
                    state.full_width = !state.full_width;
                    break;
                case LangBarMenuId::TogglePunctuation:
                    state.chinese_punct = !state.chinese_punct;
                    break;
                case LangBarMenuId::ToggleStatusBar:
                    state.show_status_bar = !state.show_status_bar;
                    break;
                case LangBarMenuId::ToggleUiLanguage:
                    state.ui_language = UiLanguage::EnUs;
                    break;
                case LangBarMenuId::OpenSettings:
                    closed_before_opening_window = FindQuickSettingsMenu() == nullptr;
                    break;
                default:
                    QIQI_TEST_CHECK(false);
                    break;
            }
        });
        ::KillTimer(nullptr, timer);
        QIQI_TEST_CHECK(actions.size() == 9);
        QIQI_TEST_CHECK(!state.chinese_mode && state.full_width && !state.chinese_punct);
        QIQI_TEST_CHECK(state.primary_dict == PrimaryDict::Jidian && !state.enable_jidian_extra);
        QIQI_TEST_CHECK(state.show_status_bar && state.ui_language == UiLanguage::EnUs);
        QIQI_TEST_CHECK(closed_before_opening_window);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
    SetThemeDarkOverrideForTest(std::nullopt);
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenAbout));
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenSettings));
}

LRESULT CALLBACK CheckMenuButtonFontLifetime(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data) {
    if (message == WM_NCDESTROY) {
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(hwnd, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(::GetObjectType(font) == OBJ_FONT);
        ++*reinterpret_cast<int*>(data);
        ::RemoveWindowSubclass(hwnd, CheckMenuButtonFontLifetime, id);
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
}

void TestQuickSettingsMenuResourceLifetime() {
    const auto original_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([&]() { SetMonitorMetricsOverrideForTest(original_monitor); });
    auto monitor = QueryMonitorForPoint(POINT{200, 200});
    monitor.dpi = 96;
    SetMonitorMetricsOverrideForTest(monitor);
    LangBarState state{};
    int actions = 0;
    static int destroyed_buttons = 0;
    destroyed_buttons = 0;
    LangBarButton::ActionCallback callback = [&](LangBarMenuId action) {
        QIQI_TEST_CHECK(action == LangBarMenuId::ToggleFullWidth);
        state.full_width = !state.full_width;
        ++actions;
        callback = {};
    };
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
        QIQI_TEST_CHECK(::SetWindowSubclass(button, CheckMenuButtonFontLifetime, 1,
                                           reinterpret_cast<DWORD_PTR>(&destroyed_buttons)));
        ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        const DWORD gdi_before = ::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS);
        QIQI_TEST_CHECK(gdi_before > 0);
        for (int i = 0; i < 16; ++i) {
            const auto old_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
            QIQI_TEST_CHECK(::GetObjectType(old_font) == OBJ_FONT);
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleFullWidth), BN_CLICKED), 0);
            const auto new_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
            QIQI_TEST_CHECK(::GetObjectType(new_font) == OBJ_FONT);
            ::GdiFlush();
            QIQI_TEST_CHECK(new_font == old_font);
            ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
        ::GdiFlush();
        QIQI_TEST_CHECK(::GetGuiResources(::GetCurrentProcess(), GR_GDIOBJECTS) <= gdi_before);

        const auto old_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        auto monitor = QueryMonitorForPoint(POINT{200, 200});
        monitor.dpi = 144;
        SetMonitorMetricsOverrideForTest(monitor);
        ::SendMessageW(menu, WM_COMMAND,
                       MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleFullWidth), BN_CLICKED), 0);
        const auto new_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(new_font != old_font && ::GetObjectType(new_font) == OBJ_FONT);

        const ScopedMemoryDC dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(dc);
        const COLORREF text_color = RGB(17, 33, 65);
        ::SetTextColor(dc.Get(), text_color);
        ::SetBkMode(dc.Get(), OPAQUE);
        const HGDIOBJ original_font = ::GetCurrentObject(dc.Get(), OBJ_FONT);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
        draw.hwndItem = button;
        draw.hDC = dc.Get();
        ::GetClientRect(button, &draw.rcItem);
        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
        QIQI_TEST_CHECK(::GetTextColor(dc.Get()) == text_color);
        QIQI_TEST_CHECK(::GetBkMode(dc.Get()) == OPAQUE);
        QIQI_TEST_CHECK(::GetCurrentObject(dc.Get(), OBJ_FONT) == original_font);
        ::SendMessageW(menu, WM_CLOSE, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, callback);
    QIQI_TEST_CHECK(actions == 17 && state.full_width && !callback);
    QIQI_TEST_CHECK(destroyed_buttons == 1 && FindQuickSettingsMenu() == nullptr);
}

void TestQuickSettingsMenuKeyboardAndDismissal() {
    for (const int dismissal : {0, 1, 2}) {
        LangBarState state{};
        int actions = 0;
        const ScopedWindow outside(::CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP,
            0, 0, 1, 1, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(outside);
        static int step = 0;
        static int dismiss_mode = 0;
        static HWND outside_window = nullptr;
        step = 0;
        dismiss_mode = dismissal;
        outside_window = outside.Get();
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            const HWND focus = ::GetFocus();
            switch (step++) {
                case 0:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::ToggleChinese));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_UP, 0);
                    break;
                case 1:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::OpenAbout));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_DOWN, 0);
                    break;
                case 2:
                    QIQI_TEST_CHECK(::GetDlgCtrlID(focus) == static_cast<int>(LangBarMenuId::ToggleChinese));
                    ::PostMessageW(focus, WM_KEYDOWN, VK_SPACE, 0);
                    break;
                default:
                    ::KillTimer(nullptr, id);
                    if (dismiss_mode == 1) {
                        ::SendMessageW(menu, WM_ACTIVATE, WA_INACTIVE, 0);
                    } else if (dismiss_mode == 2) {
                        ::PostMessageW(outside_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
                    } else {
                        ::PostMessageW(focus, WM_KEYDOWN, VK_ESCAPE, 0);
                    }
                    break;
            }
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            QIQI_TEST_CHECK(action == LangBarMenuId::ToggleChinese);
            state.chinese_mode = !state.chinese_mode;
            ++actions;
        });
        ::KillTimer(nullptr, timer);
        QIQI_TEST_CHECK(actions == 1 && !state.chinese_mode);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

void TestQuickSettingsMenuWorkArea() {
    const auto original_monitor = detail::MonitorMetricsOverrideStorage();
    const auto restore_monitor = gsl::finally([&]() { SetMonitorMetricsOverrideForTest(original_monitor); });
    for (const UINT dpi : {96u, 144u, 288u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1280, -720, 0, 0}, dpi});
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(menu, &rect));
            QIQI_TEST_CHECK(rect.left >= -1280 && rect.top >= -720);
            QIQI_TEST_CHECK(rect.right <= 0 && rect.bottom <= 0);
            ::SendMessageW(menu, WM_CLOSE, 0, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        LangBarButton::ShowSharedPopupMenu(
            LangBarState{}, POINT{-1, -1}, [](LangBarMenuId) { QIQI_TEST_CHECK(false); }, true);
        ::KillTimer(nullptr, timer);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

void TestQuickSettingsMenuCommandValidation() {
    LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, {});
    QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    for (const auto action : {LangBarMenuId::OpenSettings, LangBarMenuId::OpenAbout}) {
        static LangBarMenuId closing_action{};
        closing_action = action;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            for (const UINT command : {0u, 0xffffu, static_cast<UINT>(LangBarMenuId::ToggleExtraDict)}) {
                ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(command, BN_CLICKED), 0);
                QIQI_TEST_CHECK(FindQuickSettingsMenu() == menu && ::IsWindowVisible(menu));
            }
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleChinese), BN_DOUBLECLICKED), 0);
            QIQI_TEST_CHECK(::IsWindowVisible(menu));
            ::SendMessageW(menu, WM_COMMAND, MAKEWPARAM(static_cast<UINT>(closing_action), BN_CLICKED), 0);
            QIQI_TEST_CHECK(!::IsWindowVisible(menu));
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarState state{};
        state.primary_dict = PrimaryDict::Haifeng;
        int callbacks = 0;
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId selected) {
            QIQI_TEST_CHECK(selected == action);
            QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
            ++callbacks;
        });
        QIQI_TEST_CHECK(callbacks == 1 && state.chinese_mode);
    }
}

void TestQuickSettingsMenuDisabledNavigation() {
    static int step = 0;
    step = 0;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        switch (step++) {
            case 0:
                QIQI_TEST_CHECK(!::IsWindowEnabled(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleExtraDict))));
                ::SetFocus(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::DictHaifeng)));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_DOWN, 0);
                break;
            case 1:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::ToggleStatusBar));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_UP, 0);
                break;
            case 2:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::DictHaifeng));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_TAB, 0);
                break;
            default:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::ToggleStatusBar));
                ::KillTimer(nullptr, id);
                ::SetFocus(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenSettings)));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_RETURN, 0);
                break;
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarState state{};
    state.primary_dict = PrimaryDict::Haifeng;
    int callbacks = 0;
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
        QIQI_TEST_CHECK(action == LangBarMenuId::OpenSettings);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
        ++callbacks;
    });
    QIQI_TEST_CHECK(callbacks == 1 && step == 4);
}

int CountQuickSettingsCheckPixels(HWND menu) {
    const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
    RECT rect{};
    QIQI_TEST_CHECK(::GetClientRect(button, &rect));
    QIQI_TEST_CHECK(rect.right > rect.bottom && rect.bottom > 0);
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
    DRAWITEMSTRUCT draw{};
    draw.CtlType = ODT_BUTTON;
    draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
    draw.hwndItem = button;
    draw.hDC = dc.Get();
    draw.rcItem = rect;
    ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
    const COLORREF background = ::GetPixel(dc.Get(), 0, 0);
    QIQI_TEST_CHECK(background != CLR_INVALID);
    int pixels = 0;
    for (int y = 0; y < rect.bottom; ++y) {
        for (int x = 0; x < rect.bottom; ++x) {
            if (::GetPixel(dc.Get(), x, y) != background) {
                ++pixels;
            }
        }
    }
    return pixels;
}

void TestQuickSettingsMenuLivePainting() {
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
    });
    for (const bool dark : {false, true}) {
        // Verify quick settings menu follows taskbar theme even when app theme differs.
        SetThemeDarkOverrideForTest(!dark);
        SetTaskbarDarkOverrideForTest(dark);
        static bool updated_dark = false;
        updated_dark = !dark;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            {
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(!updated_dark).window_bg);
            }
            QIQI_TEST_CHECK(CountQuickSettingsCheckPixels(menu) > 0);
            ::SendMessageW(menu, WM_COMMAND,
                           MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleChinese), BN_CLICKED), 0);
            QIQI_TEST_CHECK(::IsWindowVisible(menu));
            QIQI_TEST_CHECK(CountQuickSettingsCheckPixels(menu) == 0);
            SetTaskbarDarkOverrideForTest(updated_dark);
            for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                ::SendMessageW(menu, message, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(updated_dark).window_bg);
            }
            ::SendMessageW(menu, WM_CLOSE, 0, 0);
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarState state{};
        int callbacks = 0;
        LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [&](LangBarMenuId action) {
            QIQI_TEST_CHECK(action == LangBarMenuId::ToggleChinese);
            state.chinese_mode = !state.chinese_mode;
            ++callbacks;
        });
        QIQI_TEST_CHECK(callbacks == 1 && !state.chinese_mode);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
}

void TestQuickSettingsMenuQuitMessage() {
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() != nullptr);
        ::PostQuitMessage(42);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, [](LangBarMenuId) {
        QIQI_TEST_CHECK(false);
    });
    QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    MSG message{};
    QIQI_TEST_CHECK(::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
    QIQI_TEST_CHECK(message.message == WM_QUIT && message.wParam == 42);
    QIQI_TEST_CHECK(!::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
}

void TestQuickSettingsMenuFocusRestoration() {
    for (const bool activate_other : {false, true}) {
        const ScopedWindow previous(::CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            20, 20, 160, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        const ScopedWindow other(::CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            40, 40, 160, 100, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(previous && other);
        ::ShowWindow(previous.Get(), SW_SHOW);
        if (!::SetForegroundWindow(previous.Get()) || ::GetForegroundWindow() != previous.Get()) {
            // Windows ForegroundLockTimeout prevents background CTest processes from taking foreground focus
            // while the user is actively interacting with another window; skip foreground-only assertion.
            return;
        }
        static HWND next_foreground = nullptr;
        next_foreground = activate_other ? other.Get() : nullptr;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::GetForegroundWindow() == menu);
            if (next_foreground) {
                ::ShowWindow(next_foreground, SW_SHOW);
                ::SetForegroundWindow(next_foreground);
                QIQI_TEST_CHECK(::GetForegroundWindow() == next_foreground);
            } else {
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
            }
        });
        QIQI_TEST_CHECK(timer != 0);
        const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
        LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, [](LangBarMenuId) {
            QIQI_TEST_CHECK(false);
        });
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
        QIQI_TEST_CHECK(::GetForegroundWindow() == (activate_other ? other.Get() : previous.Get()));
    }
}

} // namespace

void RunLangBarMenuTests() {
    TestTaskbarThemeIcons();
    TestSharedMenuItems();
    TestQuickSettingsMenu();
    TestQuickSettingsMenuResourceLifetime();
    TestQuickSettingsMenuKeyboardAndDismissal();
    TestQuickSettingsMenuWorkArea();
    TestQuickSettingsMenuCommandValidation();
    TestQuickSettingsMenuDisabledNavigation();
    TestQuickSettingsMenuLivePainting();
    TestQuickSettingsMenuQuitMessage();
    TestQuickSettingsMenuFocusRestoration();
    TestLangBarButton();
}

} // namespace qiqi::test
