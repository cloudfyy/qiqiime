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


} // namespace

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

namespace {

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

ScopedIcon CreateExpectedModeIcon(
    wchar_t glyph,
    int size) noexcept {
    return CreateTransparentGlyphIcon(
        glyph,
        size,
        kImeModeIconGlyphColor,
        FW_SEMIBOLD,
        kImeModeIconStroke);
}

constexpr uint32_t ScaleAlpha(uint32_t value, uint32_t alpha) noexcept {
    return (value * alpha + 127u) / 255u;
}

} // namespace

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
            if (alpha != 0) {
                std::fprintf(
                    stderr,
                    "[ICON] edge pixel size=%d x=%zu y=%zu alpha=%u\n",
                    size,
                    x,
                    y,
                    alpha);
            }
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
            const ScopedIcon icon =
                CreateExpectedModeIcon(
                    glyph,
                    size);
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
                const ScopedIcon expected_icon =
                    CreateExpectedModeIcon(
                        chinese ? L'中' : L'英',
                        size);
                QIQI_TEST_CHECK(expected_icon.IsValid());
                QIQI_TEST_CHECK(
                    pixels ==
                    ReadIconPixels(
                        expected_icon.Get(),
                        size));
                auto& reference = gsl::at(reference_pixels, chinese ? 1u : 0u);
                if (reference.empty()) reference = pixels;
                else QIQI_TEST_CHECK(reference == pixels);
            }
        }
    }
    QIQI_TEST_CHECK(gsl::at(reference_pixels, 0) !=
                    gsl::at(reference_pixels, 1));
}


} // namespace qiqi::test::langbar_cases
