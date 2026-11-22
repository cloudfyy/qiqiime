// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
#include "ime/ui/fltbarbtn.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "ime/ui/langtest.h"

namespace qiqi::test::langbar_cases {

using Microsoft::WRL::ComPtr;

namespace {

class MockTfMenu final : public ITfMenu {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfMenu)) {
            *ppvObj = static_cast<ITfMenu*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return 1; }
    STDMETHODIMP_(ULONG) Release() noexcept override { return 1; }
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
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (FAILED(query_result_)) {
            *ppvObj = nullptr;
            return query_result_;
        }
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfLangBarItemSink)) {
            *ppvObj = static_cast<ITfLangBarItemSink*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return 1; }
    STDMETHODIMP_(ULONG) Release() noexcept override { return 1; }
    STDMETHODIMP OnUpdate(DWORD dwFlags) override {
        last_update_flags_ = dwFlags;
        ++update_count_;
        if (callback) callback();
        return update_result_;
    }

    DWORD last_update_flags_ = 0;
    unsigned int update_count_ = 0;
    HRESULT query_result_ = S_OK;
    HRESULT update_result_ = S_OK;
    std::function<void()> callback;
};

class ReentrantLangBarItemSink final
    : public ComObjectBase<ReentrantLangBarItemSink, ITfLangBarItemSink> {
public:
    explicit ReentrantLangBarItemSink(bool& destroyed) noexcept : destroyed_(destroyed) {}
    ~ReentrantLangBarItemSink() noexcept { destroyed_ = true; }

    ReentrantLangBarItemSink(const ReentrantLangBarItemSink&) = delete;
#pragma warning(suppress : 26456) // The base assignment operators are also deleted.
    ReentrantLangBarItemSink& operator=(const ReentrantLangBarItemSink&) = delete;
    ReentrantLangBarItemSink(ReentrantLangBarItemSink&&) = delete;
#pragma warning(suppress : 26456)
    ReentrantLangBarItemSink& operator=(ReentrantLangBarItemSink&&) = delete;

    STDMETHODIMP OnUpdate(DWORD flags) override {
        QIQI_TEST_CHECK(flags != 0);
        callback();
        QIQI_TEST_CHECK(!destroyed_);
        return S_OK;
    }

    std::function<void()> callback;

private:
    bool& destroyed_;
};

} // namespace

void TestLangBarButton() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    LangBarMenuId last_action = static_cast<LangBarMenuId>(0);
    auto btn = MakeComObject<LangBarButton>([&last_action](LangBarMenuId id) { last_action = id; });

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
    MockLangBarItemSink denied_sink;
    denied_sink.query_result_ = E_ACCESSDENIED;
    DWORD cookie = 0;
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, nullptr, &cookie) == E_INVALIDARG);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_IUnknown, &sink, &cookie) == CONNECT_E_CANNOTCONNECT);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, &denied_sink, &cookie) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie)) && cookie != 0);
    QIQI_TEST_CHECK(lb_src->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == CONNECT_E_ADVISELIMIT);

    QIQI_TEST_CHECK(btn->Show(FALSE) == S_OK);
    QIQI_TEST_CHECK(btn->GetStatus(&status) == S_OK && status == TF_LBI_STATUS_HIDDEN);
    QIQI_TEST_CHECK(sink.last_update_flags_ == TF_LBI_STATUS && sink.update_count_ == 1);
    QIQI_TEST_CHECK(btn->Show(FALSE) == S_OK && sink.update_count_ == 1);
    LangBarState st{};
    st.chinese_mode = false;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(sink.last_update_flags_ == (TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP));
    QIQI_TEST_CHECK(btn->GetStatus(&status) == S_OK && status == TF_LBI_STATUS_HIDDEN);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetText(text.Put())) && std::wstring(text.Get()) == L"英");
    sink.update_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(btn->Show(TRUE) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(btn->GetStatus(&status) == S_OK && status == 0);
    sink.update_result_ = S_OK;
    QIQI_TEST_CHECK(btn->Show(FALSE) == S_OK && btn->Show(TRUE) == S_OK);

    QIQI_TEST_CHECK(lb_src->UnadviseSink(9999) == CONNECT_E_NOCONNECTION);
    QIQI_TEST_CHECK(SUCCEEDED(lb_src->UnadviseSink(cookie)));

    // Menu & Click (Chinese UI)
    st.ui_language = UiLanguage::ZhCn;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    const std::span<const wchar_t> zh_description(info.szDescription);
    const std::wstring_view zh_expected = L"QiQi 五笔状态与菜单";
    QIQI_TEST_CHECK(std::equal(zh_expected.begin(), zh_expected.end(), zh_description.begin()) &&
                    gsl::at(zh_description, zh_expected.size()) == L'\0');
    QIQI_TEST_CHECK(btn->InitMenu(nullptr) == E_INVALIDARG);
    MockTfMenu menu_zh;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_zh)));
    QIQI_TEST_CHECK(menu_zh.item_ids_.size() == 8 && menu_zh.separators_ == 1);
    QIQI_TEST_CHECK(!menu_zh.item_texts_.empty() &&
                    menu_zh.item_texts_.front().find(L"中文输入模式") != std::wstring::npos);

    // Test English UI language on LangBarButton (GetInfo, GetTooltipString, InitMenu)
    st.ui_language = UiLanguage::EnUs;
    btn->UpdateState(st);
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetInfo(&info)));
    const std::span<const wchar_t> en_description(info.szDescription);
    const std::wstring_view en_expected = L"QiQi Wubi Status & Menu";
    QIQI_TEST_CHECK(std::equal(en_expected.begin(), en_expected.end(), en_description.begin()) &&
                    gsl::at(en_description, en_expected.size()) == L'\0');
    QIQI_TEST_CHECK(SUCCEEDED(btn->GetTooltipString(tooltip.Put())) &&
                    std::wstring(tooltip.Get()).find(L"Left-click") != std::wstring::npos);
    MockTfMenu menu_en;
    QIQI_TEST_CHECK(SUCCEEDED(btn->InitMenu(&menu_en)));
    QIQI_TEST_CHECK(menu_en.item_ids_.size() == 8 && menu_en.separators_ == 1);
    QIQI_TEST_CHECK(!menu_en.item_texts_.empty() &&
                    menu_en.item_texts_.front().find(L"Chinese Input Mode") != std::wstring::npos);

    const POINT pt{0, 0};
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
    QIQI_TEST_CHECK(btn.Detach()->Release() == 0);
}

void TestLangBarReentrantNotifications() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    for (const bool visibility_update : {false, true}) {
        bool sink_destroyed = false;
        auto button = MakeComObject<LangBarButton>();
        auto sink = MakeComObject<ReentrantLangBarItemSink>(sink_destroyed);
        DWORD cookie = 0;
        QIQI_TEST_CHECK(button->AdviseSink(IID_ITfLangBarItemSink, sink.Get(), &cookie) == S_OK);
        sink->callback = [&] {
            QIQI_TEST_CHECK(button->UnadviseSink(cookie) == S_OK);
            sink.Reset();
            button.Reset();
            QIQI_TEST_CHECK(!sink_destroyed);
        };
        if (visibility_update) {
            QIQI_TEST_CHECK(button->Show(FALSE) == S_OK);
        } else {
            LangBarState state{};
            state.chinese_mode = false;
            button->UpdateState(state);
        }
        QIQI_TEST_CHECK(!button && !sink && sink_destroyed);
    }
    {
        ComPtr<LangBarButton> button;
        unsigned int calls = 0;
        button = MakeComObject<LangBarButton>([&](LangBarMenuId) {
            ++calls;
            button->Disconnect();
            button.Reset();
        });
        QIQI_TEST_CHECK(button->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleChinese)) == S_OK);
        QIQI_TEST_CHECK(calls == 1 && !button);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}


void TestLangBarThemeNotifications() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar_theme = detail::TaskbarDarkOverrideStorage();
    const auto restore_overrides = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar_theme);
    });
    const auto dispatch_refresh = [](HWND window) {
        MSG message{};
        QIQI_TEST_CHECK(::PeekMessageW(&message, window, WM_APP, WM_APP, PM_REMOVE));
        ::DispatchMessageW(&message);
        QIQI_TEST_CHECK(!::PeekMessageW(&message, window, WM_APP, WM_APP, PM_REMOVE));
    };
    const auto find_window = [] {
        const HWND window = FindThreadWindowByClass(kLangBarThemeWndClass);
        QIQI_TEST_CHECK(window != nullptr && !::IsWindowVisible(window));
        QIQI_TEST_CHECK(::GetAncestor(window, GA_ROOT) == window);
        QIQI_TEST_CHECK(::GetParent(window) == nullptr);
        return window;
    };
    MockLangBarItemSink sink;
    auto button = MakeComObject<LangBarButton>();
    sink.callback = [&] {
        const auto pixels = ReadTaskbarIconPixels(*button.Get());
        const int size = ::GetSystemMetrics(SM_CXSMICON) > 0
            ? ::GetSystemMetrics(SM_CXSMICON)
            : 16;
        CheckImeModeIconPixels(pixels, size);
    };
    DWORD cookie = 0;
    QIQI_TEST_CHECK(button->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == S_OK);
    HWND window = find_window();
    for (const bool dark : {false, true, false}) {
        SetThemeDarkOverrideForTest(!dark);
        SetTaskbarDarkOverrideForTest(dark);
        for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED, WM_SYSCOLORCHANGE}) {
            const auto count = sink.update_count_;
            ::SendMessageW(window, message, 0, 0);
            ::SendMessageW(window, message, 0, 0);
            QIQI_TEST_CHECK(sink.update_count_ == count);
            dispatch_refresh(window);
            QIQI_TEST_CHECK(sink.update_count_ == count + 1 && sink.last_update_flags_ == TF_LBI_ICON);
            QIQI_TEST_CHECK(button->State().chinese_mode);
        }
    }
    sink.update_result_ = E_ACCESSDENIED;
    ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
    dispatch_refresh(window);
    sink.update_result_ = S_OK;
    const auto count = sink.update_count_;
    ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
    dispatch_refresh(window);
    QIQI_TEST_CHECK(sink.update_count_ == count + 1);
    ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
    QIQI_TEST_CHECK(button->UnadviseSink(cookie) == S_OK);
    QIQI_TEST_CHECK(!::IsWindow(window));
    QIQI_TEST_CHECK(button->AdviseSink(IID_ITfLangBarItemSink, &sink, &cookie) == S_OK);
    window = find_window();
    ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
    dispatch_refresh(window);
    QIQI_TEST_CHECK(sink.update_count_ == count + 2);
    button.Reset();
    QIQI_TEST_CHECK(!::IsWindow(window));

    for (const bool disconnect : {false, true}) {
        bool sink_destroyed = false;
        auto reentrant_sink = MakeComObject<ReentrantLangBarItemSink>(sink_destroyed);
        button = MakeComObject<LangBarButton>();
        QIQI_TEST_CHECK(button->AdviseSink(IID_ITfLangBarItemSink, reentrant_sink.Get(), &cookie) == S_OK);
        window = find_window();
        reentrant_sink->callback = [&] {
            if (disconnect) button->Disconnect();
            else QIQI_TEST_CHECK(button->UnadviseSink(cookie) == S_OK);
            reentrant_sink.Reset();
            button.Reset();
        };
        ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
        dispatch_refresh(window);
        QIQI_TEST_CHECK(!button && !reentrant_sink && sink_destroyed && !::IsWindow(window));
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

void TestSharedMenuItems() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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
                QIQI_TEST_CHECK(tsf_menu.separators_ == 1);
                size_t command_index = 0;
                for (size_t i = 0; i < items.size(); ++i) {
                    const auto& item = items.at(i);
                    const UINT flags = ::GetMenuState(menu.Get(), gsl::narrow_cast<UINT>(i), MF_BYPOSITION);
                    QIQI_TEST_CHECK(((flags & MF_SEPARATOR) != 0) == item.IsSeparator());
                    if (item.IsSeparator()) {
                        continue;
                    }
                    QIQI_TEST_CHECK(item.id != LangBarMenuId::DictJidian &&
                                    item.id != LangBarMenuId::DictHaifeng &&
                                    item.id != LangBarMenuId::ToggleExtraDict);
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


} // namespace qiqi::test::langbar_cases
