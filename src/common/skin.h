// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/theme.h"
#include <span>
#include <vector>

namespace qiqi {

enum class SkinColor {
    Background, Border, CodeText, CandidateText, SecondaryText,
    SelectedBackground, SelectedText, SelectedBorder, EmptyText,
    PageEnabledText, PageDisabledText, MenuHoverBackground, MenuHoverText, Count
};

inline constexpr size_t kSkinColorCount = static_cast<size_t>(SkinColor::Count);
inline constexpr std::array<const wchar_t*, kSkinColorCount> kSkinColorNames{
    L"Background", L"Border", L"CodeText", L"CandidateText", L"SecondaryText",
    L"SelectedBackground", L"SelectedText", L"SelectedBorder", L"EmptyText",
    L"PageEnabledText", L"PageDisabledText", L"MenuHoverBackground", L"MenuHoverText"
};
inline constexpr std::array kCommonImeSkins{
    ImeSkinId::Firefly, ImeSkinId::Inkstone, ImeSkinId::JadePool,
    ImeSkinId::StarryAbyss, ImeSkinId::PeachBlossom
};

constexpr COLORREF Rgb24ToColorRef(DWORD value) noexcept {
    return RGB((value >> 16) & 0xffu, (value >> 8) & 0xffu, value & 0xffu);
}

constexpr DWORD ColorRefToRgb24(COLORREF value) noexcept {
    return (static_cast<DWORD>(GetRValue(value)) << 16) |
        (static_cast<DWORD>(GetGValue(value)) << 8) | GetBValue(value);
}

struct SkinPalette {
    std::array<COLORREF, kSkinColorCount> colors{};

    constexpr COLORREF Get(SkinColor color) const noexcept {
        return gsl::at(colors, static_cast<size_t>(color));
    }
    bool operator==(const SkinPalette&) const noexcept = default;
};

struct SkinPalettes {
    SkinPalette light;
    SkinPalette dark;
    bool operator==(const SkinPalettes&) const noexcept = default;
};

struct BuiltinImeSkin {
    ImeSkinId id;
    std::wstring_view name_zh;
    std::wstring_view name_en;
    SkinPalettes palettes;
};

struct ImeSkin {
    ImeSkinId id = ImeSkinId::Default;
    std::wstring name_zh;
    std::wstring name_en;
    DWORD display_order = 0;
    SkinPalettes palettes;
};

struct ImeSkinCatalog {
    std::wstring id;
    std::vector<ImeSkin> skins;
};

struct ImeSkinSnapshot {
    ImeSkinId id;
    SkinPalettes palettes;
    bool operator==(const ImeSkinSnapshot&) const noexcept = default;
};

struct ImeSkinAppearance {
    DisplayAppearance display;
    SkinPalette colors;
};

std::span<const BuiltinImeSkin> GetBuiltinImeSkins() noexcept;
HRESULT MakeBuiltinImeSkinCatalog(ImeSkinCatalog& output) noexcept;
const ImeSkin* FindImeSkin(const ImeSkinCatalog& catalog, ImeSkinId id) noexcept;
std::wstring_view ImeSkinDisplayName(
    const ImeSkinCatalog& catalog, ImeSkinId id, UiLanguage language) noexcept;
double ColorContrastRatio(COLORREF first, COLORREF second) noexcept;
HRESULT ValidateImeSkinCatalog(const ImeSkinCatalog& catalog) noexcept;

// Only desktop initialization/publishing writes. Readers preserve their output on failure.
HRESULT EnsureBuiltinImeSkinCatalog(const wchar_t* config_subkey = nullptr) noexcept;
HRESULT PublishImeSkinCatalog(
    const ImeSkinCatalog& catalog, const wchar_t* config_subkey = nullptr) noexcept;
HRESULT LoadImeSkinCatalog(
    ImeSkinCatalog& output, const wchar_t* config_subkey = nullptr) noexcept;
HRESULT UpdateImeSkinSnapshot(
    ImeSkinId id, std::optional<ImeSkinSnapshot>& snapshot,
    const wchar_t* config_subkey = nullptr) noexcept;
ImeSkinAppearance ResolveImeSkinAppearance(
    const DisplayPreferences& preferences, const std::optional<ImeSkinSnapshot>& snapshot,
    SystemThemeSource source = SystemThemeSource::App) noexcept;
ImeSkinAppearance ApplyImeSkinAppearance(
    const DisplayAppearance& appearance, const std::optional<ImeSkinSnapshot>& snapshot) noexcept;

} // namespace qiqi
