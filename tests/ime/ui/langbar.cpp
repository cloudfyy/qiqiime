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

namespace qiqi::test {

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

HWND FindQuickSettingsMenu() noexcept {
    return FindThreadWindowByClass(kMenuWndClass);
}

void CloseQuickSettingsMenu() {
    const HWND menu = FindQuickSettingsMenu();
    QIQI_TEST_CHECK(menu != nullptr);
    ::SendMessageW(menu, WM_CLOSE, 0, 0);
}

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

std::vector<uint32_t> ReadIconPixels(HICON icon, int size);

#pragma pack(push, 1)
struct GroupIconDirectoryEntry {
    BYTE width;
    BYTE height;
    BYTE color_count;
    BYTE reserved;
    WORD planes;
    WORD bit_count;
    DWORD resource_bytes;
    WORD resource_id;
};
#pragma pack(pop)

std::vector<int> ReadEmbeddedIconFrameSizes(HMODULE module) {
    const HRSRC group = ::FindResourceW(
        module, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    QIQI_TEST_CHECK(group != nullptr);
    const HGLOBAL handle = ::LoadResource(module, group);
    QIQI_TEST_CHECK(handle != nullptr);
    const auto* const data =
        static_cast<const BYTE*>(::LockResource(handle));
    const DWORD length = ::SizeofResource(module, group);
    QIQI_TEST_CHECK(data && length >= 6);
    const std::span<const BYTE> bytes(data, gsl::narrow_cast<size_t>(length));
    WORD reserved = 0;
    WORD type = 0;
    WORD count = 0;
    std::memcpy(&reserved, bytes.data(), sizeof(reserved));
    std::memcpy(&type, bytes.subspan(2).data(), sizeof(type));
    std::memcpy(&count, bytes.subspan(4).data(), sizeof(count));
    static_assert(sizeof(GroupIconDirectoryEntry) == 14);
    QIQI_TEST_CHECK(reserved == 0 && type == 1 && count >= 6 &&
                    length >= 6u + static_cast<DWORD>(count) *
                        static_cast<DWORD>(sizeof(GroupIconDirectoryEntry)));
    constexpr std::array required_sizes{16, 20, 24, 32, 40, 48};
    std::array<bool, 257> present{};
    std::vector<int> sizes;
    for (WORD i = 0; i < count; ++i) {
        GroupIconDirectoryEntry entry{};
        std::memcpy(
            &entry,
            bytes.subspan(
                6 + gsl::narrow_cast<size_t>(i) *
                    sizeof(GroupIconDirectoryEntry)).data(),
            sizeof(entry));
        const int width =
            entry.width == 0 ? 256 : gsl::narrow_cast<int>(entry.width);
        const int height =
            entry.height == 0 ? 256 : gsl::narrow_cast<int>(entry.height);
        QIQI_TEST_CHECK(width == height && entry.color_count == 0 &&
                        entry.reserved == 0 && entry.planes == 1 &&
                        entry.bit_count == 32 &&
                        width <= 256 && !gsl::at(present, width));
        gsl::at(present, width) = true;
        const HRSRC frame = ::FindResourceW(
            module, MAKEINTRESOURCEW(entry.resource_id), RT_ICON);
        QIQI_TEST_CHECK(frame != nullptr &&
                        ::SizeofResource(module, frame) ==
                            entry.resource_bytes);
        sizes.push_back(width);
    }
    for (const int size : required_sizes) {
        QIQI_TEST_CHECK(gsl::at(present, size));
    }
    std::ranges::sort(sizes);
    return sizes;
}

void CheckBrandIconPixels(std::span<const uint32_t> pixels, int size) {
    QIQI_TEST_CHECK(pixels.size() ==
                    gsl::narrow_cast<size_t>(size) *
                        gsl::narrow_cast<size_t>(size));
    const auto width = gsl::narrow_cast<size_t>(size);
    size_t black = 0;
    size_t white = 0;
    size_t antialiased = 0;
    size_t visible_on_dark = 0;
    size_t visible_on_light = 0;
    for (size_t i = 0; i < pixels.size(); ++i) {
        const uint32_t pixel = gsl::at(pixels, i);
        const uint32_t alpha = pixel >> 24u;
        const uint32_t rgb = pixel & 0x00FFFFFFu;
        const uint32_t red = (rgb >> 16u) & 0xFFu;
        const uint32_t green = (rgb >> 8u) & 0xFFu;
        const uint32_t blue = rgb & 0xFFu;
        QIQI_TEST_CHECK(red == green && green == blue);
        const size_t x = i % width;
        const size_t y = i / width;
        if (x == 0 || y == 0 || x == width - 1 || y == width - 1) {
            QIQI_TEST_CHECK(alpha == 128 && rgb == 0);
        } else {
            QIQI_TEST_CHECK(alpha == 255);
            if (red == 0) ++black;
            else if (red == 255) ++white;
            else ++antialiased;
        }
        const uint32_t on_dark =
            red * alpha / 255u + 32u * (255u - alpha) / 255u;
        const uint32_t on_light =
            red * alpha / 255u + 240u * (255u - alpha) / 255u;
        visible_on_dark += on_dark >= 72u ? 1u : 0u;
        visible_on_light += on_light <= 200u ? 1u : 0u;
    }
    QIQI_TEST_CHECK(black > pixels.size() / 12 &&
                    black < pixels.size() / 2 &&
                    white > pixels.size() / 8);
    QIQI_TEST_CHECK(antialiased + black + white ==
                    pixels.size() - (4u * gsl::narrow_cast<size_t>(size) - 4u));
    QIQI_TEST_CHECK(visible_on_dark > pixels.size() / 4 &&
                    visible_on_light > pixels.size() / 12);
    QIQI_TEST_CHECK(gsl::at(pixels, width + 1) == 0xFFFFFFFFu);
    QIQI_TEST_CHECK(gsl::at(pixels, width * 2 - 2) == 0xFFFFFFFFu);
}

void TestImeBrandIcon() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto dll = GetModuleFilePath(::GetModuleHandleW(nullptr)).parent_path() / L"qiqiime.dll";
    const ScopedModule resource(::LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    QIQI_TEST_CHECK(resource.IsValid());
    const auto sizes = ReadEmbeddedIconFrameSizes(resource.Get());
    for (const int size : sizes) {
        const ScopedIcon icon(static_cast<HICON>(::LoadImageW(
            resource.Get(), MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, 0)));
        QIQI_TEST_CHECK(icon.IsValid());
        CheckBrandIconPixels(ReadIconPixels(icon.Get(), size), size);
    }
}

std::vector<uint32_t> ReadIconPixels(HICON icon, int size) {
    ICONINFO info{};
    QIQI_TEST_CHECK(::GetIconInfo(icon, &info));
    const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
    const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
    QIQI_TEST_CHECK(mask.IsValid() && color.IsValid());
    BITMAP bitmap{};
    QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
    QIQI_TEST_CHECK(bitmap.bmBitsPixel == 32 && bitmap.bmPlanes == 1 &&
                    bitmap.bmWidth == size && bitmap.bmHeight == size);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint32_t> pixels(gsl::narrow_cast<size_t>(size) * gsl::narrow_cast<size_t>(size));
    const ScopedWindowDC dc(nullptr);
    QIQI_TEST_CHECK(::GetDIBits(dc.Get(), color.Get(), 0, gsl::narrow_cast<UINT>(size),
                              pixels.data(), &bmi, DIB_RGB_COLORS) == size);
    return pixels;
}

constexpr uint32_t ScaleAlpha(uint32_t value, uint32_t alpha) noexcept {
    return (value * alpha + 127u) / 255u;
}

void CheckImeModeIconPixels(std::span<const uint32_t> pixels, int size) {
    QIQI_TEST_CHECK(pixels.size() ==
                    gsl::narrow_cast<size_t>(size) *
                        gsl::narrow_cast<size_t>(size));
    const auto width = gsl::narrow_cast<size_t>(size);
    size_t opaque_glyph = 0;
    size_t outline = 0;
    size_t glyph_coverage = 0;
    size_t visible_on_dark = 0;
    size_t visible_on_light = 0;
    for (size_t index = 0; index < pixels.size(); ++index) {
        const uint32_t pixel = gsl::at(pixels, index);
        const uint32_t alpha = pixel >> 24u;
        const uint32_t red = (pixel >> 16u) & 0xFFu;
        const uint32_t green = (pixel >> 8u) & 0xFFu;
        const uint32_t blue = pixel & 0xFFu;
        QIQI_TEST_CHECK(red == green && green == blue && red <= alpha);
        const size_t x = index % width;
        const size_t y = index / width;
        if (x == 0 || y == 0 || x == width - 1 || y == width - 1) {
            QIQI_TEST_CHECK(alpha == 0);
        }

        uint32_t widest_coverage = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            const auto neighbor_y = gsl::narrow_cast<int>(y) + dy;
            if (neighbor_y < 0 || neighbor_y >= size) continue;
            for (int dx = -1; dx <= 1; ++dx) {
                const auto neighbor_x = gsl::narrow_cast<int>(x) + dx;
                if (neighbor_x < 0 || neighbor_x >= size) continue;
                const uint32_t neighbor = gsl::at(
                    pixels,
                    gsl::narrow_cast<size_t>(neighbor_y * size + neighbor_x));
                widest_coverage = std::max(
                    widest_coverage, (neighbor >> 16u) & 0xFFu);
            }
        }
        const uint32_t stroke_alpha =
            ScaleAlpha(widest_coverage, 128u);
        const uint32_t expected_alpha =
            red + ScaleAlpha(stroke_alpha, 255u - red);
        QIQI_TEST_CHECK(alpha == expected_alpha);
        if (red > 0) ++glyph_coverage;
        if (red == 255 && alpha == 255) ++opaque_glyph;
        if (red == 0 && alpha == 128) ++outline;

        const uint32_t on_dark =
            red + ScaleAlpha(32u, 255u - alpha);
        const uint32_t on_light =
            red + ScaleAlpha(240u, 255u - alpha);
        visible_on_dark += on_dark >= 72u ? 1u : 0u;
        visible_on_light += on_light <= 200u ? 1u : 0u;
    }
    QIQI_TEST_CHECK(opaque_glyph >= 4);
    QIQI_TEST_CHECK(outline >= 8);
    QIQI_TEST_CHECK(glyph_coverage >= 8 &&
                    glyph_coverage + outline < pixels.size());
    QIQI_TEST_CHECK(visible_on_dark >= 8 && visible_on_light >= 8);
}

std::vector<uint32_t> ReadTaskbarIconPixels(LangBarButton& button) {
    ScopedIcon icon;
    QIQI_TEST_CHECK(button.GetIcon(icon.Put()) == S_OK);
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
    return ReadIconPixels(icon.Get(), size);
}

void TestTaskbarThemeIcons() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto restore_overrides = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetTaskbarDarkOverrideForTest(std::nullopt);
    });
    std::array<std::vector<uint32_t>, 2> reference_pixels;
    for (const int size : {16, 20}) {
        for (const wchar_t glyph : {L'中', L'英'}) {
            const ScopedIcon icon = CreateTransparentGlyphIcon(
                glyph, size, kImeModeIconGlyphColor, FW_NORMAL,
                kImeModeIconStroke);
            QIQI_TEST_CHECK(icon.IsValid());
            CheckImeModeIconPixels(ReadIconPixels(icon.Get(), size), size);
        }
    }
    for (const bool app_dark : {false, true}) {
        const ComPtr<LangBarButton> button = MakeComObject<LangBarButton>();
        for (const bool taskbar_dark : {false, true}) {
            SetThemeDarkOverrideForTest(app_dark);
            SetTaskbarDarkOverrideForTest(taskbar_dark);
            QIQI_TEST_CHECK(IsSystemDarkTheme() == app_dark);
            QIQI_TEST_CHECK(IsTaskbarDarkTheme() == taskbar_dark);

            TF_LANGBARITEMINFO item_info{};
            QIQI_TEST_CHECK(button->GetInfo(&item_info) == S_OK);
            QIQI_TEST_CHECK((item_info.dwStyle & TF_LBI_STYLE_TEXTCOLORICON) == 0);

            for (const bool chinese : {false, true}) {
                LangBarState state{};
                state.chinese_mode = chinese;
                button->UpdateState(state);
                const auto pixels = ReadTaskbarIconPixels(*button.Get());
                const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;
                CheckImeModeIconPixels(pixels, size);
                auto& reference = gsl::at(reference_pixels, chinese ? 1u : 0u);
                if (reference.empty()) reference = pixels;
                else QIQI_TEST_CHECK(reference == pixels);
            }
        }
    }
    QIQI_TEST_CHECK(gsl::at(reference_pixels, 0) !=
                    gsl::at(reference_pixels, 1));
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

void TestQuickSettingsMenu() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
    });
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        SetTaskbarDarkOverrideForTest(!dark);
        LangBarState state{};
        state.ui_language = UiLanguage::ZhCn;
        std::vector<LangBarMenuId> actions;
        bool closed_before_opening_window = false;
        static int step = 0;
        step = 0;
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu != nullptr && ::IsWindowVisible(menu));
            static constexpr std::array commands{LangBarMenuId::ToggleChinese,     LangBarMenuId::ToggleFullWidth,
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
            if (step == 4) {
                std::array<wchar_t, 256> label{};
                ::GetDlgItemTextW(menu, static_cast<int>(LangBarMenuId::ToggleChinese), label.data(),
                                  gsl::narrow_cast<int>(label.size()));
                QIQI_TEST_CHECK(std::wstring_view(label.data()) == kEnUsStrings.menu_toggle_chinese);
                ::SendMessageW(menu, WM_THEMECHANGED, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetTaskbarThemePalette().window_bg);
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
        if (actions.size() != 6) {
            std::cerr << "[MENU-DIAGNOSTIC] actions=" << actions.size() << ", timer_step=" << step << ", ids=";
            for (const auto action : actions) std::cerr << static_cast<UINT>(action) << ',';
            std::cerr << '\n';
        }
        QIQI_TEST_CHECK(actions.size() == 6);
        QIQI_TEST_CHECK(!state.chinese_mode && state.full_width && !state.chinese_punct);
        QIQI_TEST_CHECK(state.primary_dict == PrimaryDict::Jidian && state.enable_jidian_extra);
        QIQI_TEST_CHECK(state.show_status_bar && state.ui_language == UiLanguage::EnUs);
        QIQI_TEST_CHECK(closed_before_opening_window);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    }
    SetThemeDarkOverrideForTest(std::nullopt);
    QIQI_TEST_CHECK(!LangBarButton::KeepsMenuOpen(LangBarMenuId::OpenHelp));
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
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

        const auto* old_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        auto monitor = QueryMonitorForPoint(POINT{200, 200});
        monitor.dpi = 144;
        SetMonitorMetricsOverrideForTest(monitor);
        ::SendMessageW(menu, WM_COMMAND,
                       MAKEWPARAM(static_cast<UINT>(LangBarMenuId::ToggleFullWidth), BN_CLICKED), 0);
        const auto new_font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        QIQI_TEST_CHECK(new_font != old_font && ::GetObjectType(new_font) == OBJ_FONT);

        const ScopedMemoryDC dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(dc);
        constexpr COLORREF text_color = RGB(17, 33, 65);
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    for (const UINT dpi : {96u, 120u, 144u, 168u, 192u, 288u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1280, -720, 0, 0}, dpi});
        const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
            ::KillTimer(nullptr, id);
            const HWND menu = FindQuickSettingsMenu();
            QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
            RECT rect{};
            QIQI_TEST_CHECK(::GetWindowRect(menu, &rect));
            QIQI_TEST_CHECK(rect.left >= -1280 && rect.top >= -720);
            QIQI_TEST_CHECK(rect.right <= 0 && rect.bottom <= 0);
            const auto monitor = QueryMonitorForPoint(POINT{-1, -1}, menu);
            const auto model = LangBarButton::BuildSharedMenuItems(LangBarState{});
            int base_height = 8;
            for (const auto& item : model) base_height += item.IsSeparator() ? 8 : 28;
            const int available_height = std::max(1, gsl::narrow_cast<int>(
                monitor.work_area.bottom - monitor.work_area.top) - 2);
            const UINT expected_dpi = std::min(monitor.dpi, gsl::narrow_cast<UINT>(
                std::max(1, ::MulDiv(std::max(1, available_height -
                    gsl::narrow_cast<int>(model.size()) - 2), 96, base_height))));
            int row_top = ScalePxByDpi(4, expected_dpi);
            for (const auto& item : model) {
                const int row_height = ScalePxByDpi(item.IsSeparator() ? 8 : 28, expected_dpi);
                if (!item.IsSeparator()) {
                    const HWND button = ::GetDlgItem(menu, gsl::narrow_cast<int>(std::to_underlying(item.id)));
                    QIQI_TEST_CHECK(button);
                    RECT button_rect{};
                    QIQI_TEST_CHECK(::GetWindowRect(button, &button_rect));
                    POINT origin{button_rect.left, button_rect.top};
                    QIQI_TEST_CHECK(::ScreenToClient(menu, &origin));
                    QIQI_TEST_CHECK(origin.x == 0 && origin.y == row_top);
                    QIQI_TEST_CHECK(button_rect.bottom - button_rect.top == row_height);
                    const auto font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
                    LOGFONTW description{};
                    QIQI_TEST_CHECK(::GetObjectW(font, sizeof(description), &description) == sizeof(description));
                    QIQI_TEST_CHECK(description.lfHeight == -ScalePxByDpi(14, expected_dpi));
                }
                row_top += row_height;
            }
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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

void TestQuickSettingsMenuComfortScrolling() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
    const auto restore_monitor = gsl::finally([&]() noexcept {
        SetMonitorMetricsOverrideForTest(std::move(original_monitor));
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{-1280, -320, 0, 0}, 144});
    for (const auto scale : {UiTextScale::Percent150, UiTextScale::Percent200}) {
        for (const auto theme : {UiTheme::Light, UiTheme::Dark, UiTheme::HighContrast}) {
            static DisplayPreferences display{};
            display = DisplayPreferences{theme, scale, true};
            static int step = 0;
            step = 0;
            static LONG first_top = 0;
            const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
                const HWND menu = FindQuickSettingsMenu();
                QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
                const HWND first = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleChinese));
                const HWND last = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenAbout));
                RECT client{};
                RECT first_rect{};
                RECT last_rect{};
                QIQI_TEST_CHECK(::GetClientRect(menu, &client));
                QIQI_TEST_CHECK(::GetWindowRect(first, &first_rect));
                QIQI_TEST_CHECK(::GetWindowRect(last, &last_rect));
                POINT first_origin{first_rect.left, first_rect.top};
                POINT last_origin{last_rect.left, last_rect.top};
                QIQI_TEST_CHECK(::ScreenToClient(menu, &first_origin));
                QIQI_TEST_CHECK(::ScreenToClient(menu, &last_origin));
                switch (step++) {
                    case 0: {
                        RECT menu_rect{};
                        QIQI_TEST_CHECK(::GetWindowRect(menu, &menu_rect));
                        QIQI_TEST_CHECK(menu_rect.top >= -320 && menu_rect.bottom <= 0);
                        QIQI_TEST_CHECK(menu_rect.left >= -1280 && menu_rect.right <= 0);
                        const auto font = reinterpret_cast<HFONT>(::SendMessageW(first, WM_GETFONT, 0, 0));
                        LOGFONTW description{};
                        QIQI_TEST_CHECK(::GetObjectW(font, sizeof(description), &description) == sizeof(description));
                        QIQI_TEST_CHECK(description.lfHeight == -ScaleDisplayPx(14, 144, display.text_scale));
                        QIQI_TEST_CHECK(first_rect.bottom - first_rect.top == ScaleDisplayPx(44, 144, display.text_scale));
                        QIQI_TEST_CHECK(first_rect.bottom - first_rect.top >= ScalePxByDpi(44, 144));
                        std::array<wchar_t, 256> label{};
                        ::GetWindowTextW(first, label.data(), gsl::narrow_cast<int>(label.size()));
                        QIQI_TEST_CHECK(std::wstring_view(label.data()).find(L"Ctrl") == std::wstring_view::npos);
                        const ScopedWindowDC screen;
                        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
                        const int row_width = first_rect.right - first_rect.left;
                        const int row_height = first_rect.bottom - first_rect.top;
                        const ScopedGdiHandle<HBITMAP> bitmap(
                            ::CreateCompatibleBitmap(screen.Get(), row_width, row_height));
                        QIQI_TEST_CHECK(screen && dc && bitmap);
                        const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
                        DRAWITEMSTRUCT draw{};
                        draw.CtlType = ODT_BUTTON;
                        draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
                        draw.hwndItem = first;
                        draw.hDC = dc.Get();
                        draw.rcItem = RECT{0, 0, row_width, row_height};
                        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
                        const auto appearance = ResolveDisplayAppearance(display);
                        QIQI_TEST_CHECK(::GetPixel(dc.Get(), row_width - 2, row_height / 2) == (appearance.system_high_contrast ?
                            ::GetSysColor(COLOR_MENU) : appearance.palette.window_bg));
                        const HWND scrollbar = ::FindWindowExW(menu, nullptr, L"SCROLLBAR", nullptr);
                        QIQI_TEST_CHECK(scrollbar && ::IsWindowVisible(scrollbar));
                        ::SendMessageW(menu, WM_VSCROLL, SB_BOTTOM, reinterpret_cast<LPARAM>(scrollbar));
                        ::PostMessageW(first, WM_KEYDOWN, VK_UP, 0);
                        break;
                    }
                    case 1:
                        QIQI_TEST_CHECK(::GetFocus() == last);
                        QIQI_TEST_CHECK(last_origin.y >= 0 &&
                            last_origin.y + last_rect.bottom - last_rect.top <= client.bottom);
                        ::PostMessageW(last, WM_KEYDOWN, VK_DOWN, 0);
                        break;
                    case 2:
                        QIQI_TEST_CHECK(::GetFocus() == first);
                        QIQI_TEST_CHECK(first_origin.y >= 0 &&
                            first_origin.y + first_rect.bottom - first_rect.top <= client.bottom);
                        first_top = first_origin.y;
                        ::PostMessageW(menu, WM_MOUSEWHEEL,
                            MAKEWPARAM(0, gsl::narrow_cast<WORD>(-WHEEL_DELTA)), 0);
                        break;
                    default: {
                        QIQI_TEST_CHECK(first_origin.y < first_top);
                        RECT clip{};
                        QIQI_TEST_CHECK(::GetWindowRgnBox(first, &clip) != ERROR && clip.top > 0);
                        ::KillTimer(nullptr, id);
                        ::SendMessageW(menu, WM_CLOSE, 0, 0);
                        break;
                    }
                }
            });
            QIQI_TEST_CHECK(timer != 0);
            const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
            LangBarState state{};
            state.display = display;
            LangBarButton::ShowSharedPopupMenu(state, POINT{-1, -1}, [](LangBarMenuId) {
                QIQI_TEST_CHECK(false);
            }, true);
        }
    }
}

void TestQuickSettingsMenuNavigation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    static int step = 0;
    step = 0;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        switch (step++) {
            case 0:
                QIQI_TEST_CHECK(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::ToggleExtraDict)) == nullptr);
                ::SetFocus(::GetDlgItem(menu, static_cast<int>(LangBarMenuId::TogglePunctuation)));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_DOWN, 0);
                break;
            case 1:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::ToggleStatusBar));
                ::PostMessageW(::GetFocus(), WM_KEYDOWN, VK_UP, 0);
                break;
            case 2:
                QIQI_TEST_CHECK(::GetDlgCtrlID(::GetFocus()) == static_cast<int>(LangBarMenuId::TogglePunctuation));
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
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto original_theme = detail::ThemeDarkOverrideStorage();
    const auto original_taskbar = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() {
        SetThemeDarkOverrideForTest(original_theme);
        SetTaskbarDarkOverrideForTest(original_taskbar);
    });
    for (const bool dark : {false, true}) {
        SetThemeDarkOverrideForTest(dark);
        SetTaskbarDarkOverrideForTest(!dark);
        static bool updated_dark = false;
        updated_dark = dark;
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
            SetThemeDarkOverrideForTest(!updated_dark);
            for (const UINT message : {WM_SETTINGCHANGE, WM_THEMECHANGED}) {
                ::SendMessageW(menu, message, 0, 0);
                ::RedrawWindow(menu, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                const ScopedWindowDC dc(menu);
                QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == GetThemePalette(!updated_dark).window_bg);
            }
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

void TestQuickSettingsMenuSkin() {
    const TsfTestCaseTrace trace(__func__);
    const auto old_app = detail::ThemeDarkOverrideStorage();
    const auto old_windows = detail::TaskbarDarkOverrideStorage();
    const auto restore = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(old_app);
        SetTaskbarDarkOverrideForTest(old_windows);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    SetHighContrastOverrideForTest(false);
    const auto& skin = GetBuiltinImeSkins().front();
    static SkinPalette expected_palette;
    static bool observed = false;
    for (const bool app_dark : {false, true}) {
        for (const bool windows_dark : {false, true}) {
            SetThemeDarkOverrideForTest(app_dark);
            SetTaskbarDarkOverrideForTest(windows_dark);
            expected_palette = windows_dark ? skin.palettes.dark : skin.palettes.light;
            const auto snapshot = std::optional<ImeSkinSnapshot>{
                ImeSkinSnapshot{skin.id, skin.palettes}};
            const auto windows_appearance = ResolveImeSkinAppearance(
                {UiTheme::System, UiTextScale::Percent100, false},
                snapshot, SystemThemeSource::Windows);
            QIQI_TEST_CHECK(windows_appearance.colors == expected_palette);
            if (app_dark) continue;
            observed = false;
            const UINT_PTR timer = ::SetTimer(nullptr, 0, 15,
                [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                const HWND menu = FindQuickSettingsMenu();
                if (!menu) return;
                observed = true;
                ::KillTimer(nullptr, id);
                const auto& palette = expected_palette;
                const ScopedWindowDC screen(menu);
                QIQI_TEST_CHECK(::GetPixel(
                    screen.Get(), 1, 1) == palette.Get(SkinColor::Background));
                const HWND button = ::GetDlgItem(
                    menu, static_cast<int>(LangBarMenuId::ToggleChinese));
                RECT bounds{};
                QIQI_TEST_CHECK(::GetClientRect(button, &bounds));
                const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
                const ScopedGdiHandle<HBITMAP> bitmap(
                    ::CreateCompatibleBitmap(screen.Get(), bounds.right, bounds.bottom));
                QIQI_TEST_CHECK(dc && bitmap);
                const auto count_painted_pixels =
                    [&](UINT state, COLORREF expected, const RECT& area,
                        bool exclude_expected = false) {
                    const ScopedSelectObject selected_bitmap(dc.Get(), bitmap.Get());
                    DRAWITEMSTRUCT draw{};
                    draw.CtlType = ODT_BUTTON;
                    draw.CtlID = static_cast<UINT>(LangBarMenuId::ToggleChinese);
                    draw.hwndItem = button;
                    draw.hDC = dc.Get();
                    draw.rcItem = bounds;
                    draw.itemState = state;
                    ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID,
                        reinterpret_cast<LPARAM>(&draw));
                    const int left = std::max(0L, area.left);
                    const int right = std::min(bounds.right, area.right);
                    const int top = std::max(0L, area.top);
                    const int bottom = std::min(bounds.bottom, area.bottom);
                    size_t matches = 0;
                    for (int y = top; y < bottom; ++y) {
                        for (int x = left; x < right; ++x) {
                            const bool match = ::GetPixel(dc.Get(), x, y) == expected;
                            if (match != exclude_expected) ++matches;
                        }
                    }
                    return matches;
                };
                QIQI_TEST_CHECK(count_painted_pixels(ODS_SELECTED,
                    palette.Get(SkinColor::MenuHoverBackground), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_SELECTED,
                    palette.Get(SkinColor::MenuHoverText), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_DISABLED,
                    palette.Get(SkinColor::PageDisabledText), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(ODS_FOCUS,
                    palette.Get(SkinColor::SelectedBorder), bounds) > 0);
                const RECT gutter{0, 0, bounds.bottom, bounds.bottom};
                QIQI_TEST_CHECK(count_painted_pixels(0,
                    palette.Get(SkinColor::Background), gutter, true) > 0);

                SetHighContrastOverrideForTest(true);
                ::SendMessageW(menu, WM_SETTINGCHANGE, 0, 0);
                QIQI_TEST_CHECK(count_painted_pixels(
                    ODS_SELECTED, ::GetSysColor(COLOR_HIGHLIGHT), bounds) > 0);
                QIQI_TEST_CHECK(count_painted_pixels(
                    ODS_SELECTED, ::GetSysColor(COLOR_HIGHLIGHTTEXT), bounds) > 0);
                SetHighContrastOverrideForTest(false);
                ::SendMessageW(menu, WM_CLOSE, 0, 0);
            });
            QIQI_TEST_CHECK(timer != 0);
            const auto stop = gsl::finally([timer]() noexcept {
                ::KillTimer(nullptr, timer);
            });
            LangBarState state;
            state.chinese_mode = true;
            state.skin_snapshot = ImeSkinSnapshot{skin.id, skin.palettes};
            LangBarButton::ShowSharedPopupMenu(
                state, POINT{200, 200}, [](LangBarMenuId) noexcept {});
            QIQI_TEST_CHECK(observed);
        }
    }
}

void TestQuickSettingsMenuLiteralAmpersand() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    LangBarState state{};
    state.ui_language = UiLanguage::EnUs;
    state.display.theme = UiTheme::Light;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        const HWND menu = FindQuickSettingsMenu();
        QIQI_TEST_CHECK(menu && ::IsWindowVisible(menu));
        const HWND button = ::GetDlgItem(menu, static_cast<int>(LangBarMenuId::OpenHelp));
        RECT rect{};
        QIQI_TEST_CHECK(button && ::GetClientRect(button, &rect));
        const ScopedWindowDC screen;
        const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
        const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
        QIQI_TEST_CHECK(screen && dc && bitmap);
        const ScopedSelectObject select_bitmap(dc.Get(), bitmap.Get());
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_BUTTON;
        draw.CtlID = static_cast<UINT>(LangBarMenuId::OpenHelp);
        draw.hwndItem = button;
        draw.hDC = dc.Get();
        draw.rcItem = rect;
        ::SendMessageW(menu, WM_DRAWITEM, draw.CtlID, reinterpret_cast<LPARAM>(&draw));
        const auto pixels = [&]() {
            std::vector<COLORREF> result;
            result.reserve(gsl::narrow_cast<size_t>(rect.right * rect.bottom));
            for (int y = 0; y < rect.bottom; ++y) {
                for (int x = 0; x < rect.right; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel != CLR_INVALID);
                    result.push_back(pixel);
                }
            }
            return result;
        };
        const auto actual = pixels();
        const auto appearance = ResolveDisplayAppearance({UiTheme::Light});
        const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(appearance.palette.window_bg));
        QIQI_TEST_CHECK(background && ::FillRect(dc.Get(), &rect, background.Get()));
        ::SetBkMode(dc.Get(), TRANSPARENT);
        ::SetTextColor(dc.Get(), appearance.palette.text_primary);
        const auto font = reinterpret_cast<HFONT>(::SendMessageW(button, WM_GETFONT, 0, 0));
        const ScopedSelectObject select_font(dc.Get(), font);
        RECT text_rect = rect;
        text_rect.left += rect.bottom + rect.bottom / 4;
        text_rect.right -= rect.bottom / 2;
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), GetUiStrings(UiLanguage::EnUs).menu_open_help, -1, &text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX));
        QIQI_TEST_CHECK(actual == pixels());
        QIQI_TEST_CHECK(::FillRect(dc.Get(), &rect, background.Get()));
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), GetUiStrings(UiLanguage::EnUs).menu_open_help, -1, &text_rect,
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
        QIQI_TEST_CHECK(actual != pixels());
        ::SendMessageW(menu, WM_CLOSE, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([&]() { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(state, POINT{200, 200}, [](LangBarMenuId) {
        QIQI_TEST_CHECK(false);
    });
}

void TestQuickSettingsMenuQuitMessage() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 15, [](HWND, UINT, UINT_PTR id, DWORD) {
        ::KillTimer(nullptr, id);
        QIQI_TEST_CHECK(FindQuickSettingsMenu() != nullptr);
        ::PostQuitMessage(42);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    LangBarButton::ShowSharedPopupMenu(LangBarState{}, POINT{200, 200}, [](LangBarMenuId) {
        QIQI_TEST_CHECK(false);
    });
    QIQI_TEST_CHECK(FindQuickSettingsMenu() == nullptr);
    QIQI_TEST_CHECK(::PostThreadMessageW(::GetCurrentThreadId(), WM_APP, 0, 0));
    CheckQuitMessage(42);
    MSG message{};
    QIQI_TEST_CHECK(!::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE));
}

void TestQuickSettingsMenuFocusRestoration() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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
            const auto* menu = FindQuickSettingsMenu();
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

void RunTsfImeIconTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestImeBrandIcon();
    TestTaskbarThemeIcons();
    TestLangBarThemeNotifications();
}

void RunMenuSkinTests() { TestQuickSettingsMenuSkin(); }

void RunLangBarMenuTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestSharedMenuItems();
    TestQuickSettingsMenu();
    TestQuickSettingsMenuResourceLifetime();
    TestQuickSettingsMenuKeyboardAndDismissal();
    TestQuickSettingsMenuWorkArea();
    TestQuickSettingsMenuComfortScrolling();
    TestQuickSettingsMenuCommandValidation();
    TestQuickSettingsMenuNavigation();
    TestQuickSettingsMenuLivePainting();
    TestQuickSettingsMenuLiteralAmpersand();
    TestQuickSettingsMenuQuitMessage();
    TestQuickSettingsMenuFocusRestoration();
    TestLangBarButton();
    TestLangBarReentrantNotifications();
}

} // namespace qiqi::test
