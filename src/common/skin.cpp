// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/skin.h"
#include "common/fileutil.h"
#include "common/i18n.h"
#include <cmath>
#include <new>

namespace qiqi {
namespace {

constexpr SkinPalette MakePalette(const std::array<DWORD, kSkinColorCount>& values) noexcept {
    SkinPalette palette;
    for (size_t i = 0; i < values.size(); ++i) {
        gsl::at(palette.colors, i) = Rgb24ToColorRef(gsl::at(values, i));
    }
    return palette;
}

// Complete Light/Dark definitions from docs/ime-skins.reg, in registry field order.
constexpr std::array<BuiltinImeSkin, 21> kBuiltinSkins{{
    {ImeSkinId::Firefly, L"流萤", L"Firefly Glow", {MakePalette({0x00f5d96a, 0x008b7330, 0x005b4a0b, 0x0030280f, 0x005c4d21, 0x00b2c944, 0x00273509, 0x00536218, 0x005c4d21, 0x005b4a0b, 0x005c4d21, 0x00e5c856, 0x0030280f}), MakePalette({0x0025271a, 0x00899254, 0x00d9cb6b, 0x00f3ebc6, 0x00c6be9b, 0x00495a25, 0x00f3f2cb, 0x00b8ca60, 0x00c6be9b, 0x00d9cb6b, 0x00c6be9b, 0x00343c20, 0x00f3ebc6})}},
    {ImeSkinId::Inkstone, L"墨砚", L"Inkstone", {MakePalette({0x00eceeef, 0x00858e98, 0x00313942, 0x001e242a, 0x00515963, 0x001f242a, 0x00ffffff, 0x00737d87, 0x00515963, 0x00313942, 0x00515963, 0x00d8dee4, 0x001e242a}), MakePalette({0x00202124, 0x00727a85, 0x00d8dde5, 0x00eceff3, 0x00b1b7c0, 0x00dadee4, 0x0016191d, 0x00777f89, 0x00b1b7c0, 0x00d8dde5, 0x00b1b7c0, 0x0035383d, 0x00eceff3})}},
    {ImeSkinId::JadePool, L"碧潭", L"Jade Pool", {MakePalette({0x00d7efe5, 0x006f9581, 0x00215f48, 0x00163c30, 0x003d6554, 0x0021694f, 0x00ffffff, 0x00527963, 0x003d6554, 0x00215f48, 0x003d6554, 0x00bce0d0, 0x00163c30}), MakePalette({0x00152b24, 0x005d997a, 0x008dccad, 0x00e0f2e8, 0x00a8cdba, 0x00285b49, 0x00e9f7ed, 0x009ccdab, 0x00a8cdba, 0x008dccad, 0x00a8cdba, 0x00203f33, 0x00e0f2e8})}},
    {ImeSkinId::StarryAbyss, L"星渊", L"Starry Abyss", {MakePalette({0x00e8eff7, 0x007990aa, 0x002f547f, 0x0020344e, 0x004e637e, 0x00355b87, 0x00ffffff, 0x0054769d, 0x004e637e, 0x002f547f, 0x004e637e, 0x00ccdaec, 0x0020344e}), MakePalette({0x001c2633, 0x005f7fa5, 0x008cbaf0, 0x00e3edfa, 0x00a9bcd6, 0x00294769, 0x00eff5fe, 0x0082b4e8, 0x00a9bcd6, 0x008cbaf0, 0x00a9bcd6, 0x0025364c, 0x00e3edfa})}},
    {ImeSkinId::AutumnBrocade, L"秋绮", L"Autumn Brocade", {MakePalette({0x00f0dea0, 0x009b6e42, 0x007e3157, 0x003f3216, 0x0069542e, 0x00dec468, 0x00382609, 0x009a3868, 0x0069542e, 0x007e3157, 0x0069542e, 0x00e4cf89, 0x003f3216}), MakePalette({0x002b251d, 0x00ad8562, 0x00f0acc7, 0x00f3e5c5, 0x00ccbba0, 0x00574627, 0x00fff0cd, 0x00df8cb1, 0x00ccbba0, 0x00f0acc7, 0x00ccbba0, 0x003d3223, 0x00f3e5c5})}},
    {ImeSkinId::BambooDew, L"竹露", L"Bamboo Dew", {MakePalette({0x00e2edbd, 0x007b995b, 0x00365d27, 0x002c3d1c, 0x0053693a, 0x003c672d, 0x00ffffff, 0x0062864a, 0x0053693a, 0x00365d27, 0x0053693a, 0x00cddea0, 0x002c3d1c}), MakePalette({0x001e2a1b, 0x006f9451, 0x00a2cb79, 0x00e5f0d6, 0x00b3c89b, 0x0037582b, 0x00eef6dd, 0x00a1c776, 0x00b3c89b, 0x00a2cb79, 0x00b3c89b, 0x002b3d23, 0x00e5f0d6})}},
    {ImeSkinId::IvoryPaper, L"素笺", L"Ivory Paper", {MakePalette({0x00f5f6f7, 0x008c9fb1, 0x00075da8, 0x0020242a, 0x0058616d, 0x000067c0, 0x00ffffff, 0x00426d98, 0x0058616d, 0x00075da8, 0x0058616d, 0x00dceaf7, 0x0020242a}), MakePalette({0x00202832, 0x006086b3, 0x009ac9ff, 0x00e8f0fb, 0x00b2c2d8, 0x00214b77, 0x00ffffff, 0x008cbcf0, 0x00b2c2d8, 0x009ac9ff, 0x00b2c2d8, 0x002b3c51, 0x00e8f0fb})}},
    {ImeSkinId::ObsidianNight, L"玄夜", L"Obsidian Night", {MakePalette({0x00e7f4f8, 0x006b9faa, 0x00146278, 0x001e3945, 0x00446678, 0x0081d3ed, 0x0007313e, 0x00296e83, 0x00446678, 0x00146278, 0x00446678, 0x00cce9f3, 0x001e3945}), MakePalette({0x00303030, 0x00658e99, 0x007bd1ef, 0x00f4f4f4, 0x00bbc6cc, 0x0045beeb, 0x00062e3d, 0x007ab6c9, 0x00bbc6cc, 0x007bd1ef, 0x00bbc6cc, 0x00344751, 0x00f4f4f4})}},
    {ImeSkinId::IcySilk, L"冰绡", L"Icy Silk", {MakePalette({0x00d5f1f7, 0x00749eb5, 0x00155e9b, 0x00102f3c, 0x00466775, 0x000068c4, 0x00ffffff, 0x00407395, 0x00466775, 0x00155e9b, 0x00466775, 0x00b8e0ee, 0x00102f3c}), MakePalette({0x00172c35, 0x00477e94, 0x0084d9ee, 0x00ddf4fa, 0x00aaccd8, 0x00174b78, 0x00f7fcff, 0x0078c5e9, 0x00aaccd8, 0x0084d9ee, 0x00aaccd8, 0x00233f4d, 0x00ddf4fa})}},
    {ImeSkinId::InkSeal, L"墨印", L"Ink Seal", {MakePalette({0x00ffffff, 0x00000000, 0x00222222, 0x00111111, 0x00555555, 0x00000000, 0x00ffffff, 0x00777777, 0x00555555, 0x00222222, 0x00555555, 0x00e0e0e0, 0x00111111}), MakePalette({0x00111111, 0x00ffffff, 0x00f4f4f4, 0x00f4f4f4, 0x00bbbbbb, 0x00ffffff, 0x00000000, 0x00777777, 0x00bbbbbb, 0x00f4f4f4, 0x00bbbbbb, 0x00303030, 0x00f4f4f4})}},
    {ImeSkinId::TerracottaDusk, L"赭霞", L"Terracotta Dusk", {MakePalette({0x00f8e2d4, 0x00af8973, 0x00883b21, 0x004e2818, 0x0077513e, 0x00a94329, 0x00ffffff, 0x009b664d, 0x0077513e, 0x00883b21, 0x0077513e, 0x00eecbb5, 0x004e2818}), MakePalette({0x00322119, 0x00b47f5d, 0x00f1b080, 0x00f7e8db, 0x00d0b39c, 0x00793e26, 0x00fff0e5, 0x00d79972, 0x00d0b39c, 0x00f1b080, 0x00d0b39c, 0x004b3023, 0x00f7e8db})}},
    {ImeSkinId::SmokyMist, L"烟岚", L"Smoky Mist", {MakePalette({0x00e4ebef, 0x00849ca8, 0x00345f74, 0x00203641, 0x00526b78, 0x00486d81, 0x00ffffff, 0x005f8191, 0x00526b78, 0x00345f74, 0x00526b78, 0x00ccdae3, 0x00203641}), MakePalette({0x00242e35, 0x00647e8d, 0x00a2cad8, 0x00e8f1f5, 0x00b1c6d1, 0x00a2cad8, 0x00122d39, 0x007795a4, 0x00b1c6d1, 0x00a2cad8, 0x00b1c6d1, 0x0033444f, 0x00e8f1f5})}},
    {ImeSkinId::SilverFrost, L"银霜", L"Silver Frost", {MakePalette({0x00ecedeb, 0x0089928b, 0x00444a46, 0x002b2d2c, 0x00626761, 0x005d655f, 0x00ffffff, 0x00747b75, 0x00626761, 0x00444a46, 0x00626761, 0x00d8ddd9, 0x002b2d2c}), MakePalette({0x00282c2a, 0x006d8175, 0x00d2ded6, 0x00edf1ee, 0x00bdc8c0, 0x00bcc8bf, 0x001b2a20, 0x007b9685, 0x00bdc8c0, 0x00d2ded6, 0x00bdc8c0, 0x003b443e, 0x00edf1ee})}},
    {ImeSkinId::SunsetClouds, L"落霞", L"Sunset Clouds", {MakePalette({0x00f7d8cc, 0x00bc8877, 0x009b3928, 0x0050241f, 0x00815349, 0x00af381e, 0x00ffffff, 0x00a9614b, 0x00815349, 0x009b3928, 0x00815349, 0x00eec0ab, 0x0050241f}), MakePalette({0x00342224, 0x00b47b66, 0x00f4af93, 0x00fae4dc, 0x00dab8ad, 0x007d382d, 0x00fff1e7, 0x00e4a080, 0x00dab8ad, 0x00f4af93, 0x00dab8ad, 0x004b2f2b, 0x00fae4dc})}},
    {ImeSkinId::VioletPath, L"紫陌", L"Violet Path", {MakePalette({0x00eee8f4, 0x009f86b6, 0x0063308c, 0x002e2349, 0x0068577e, 0x007031c8, 0x00ffffff, 0x008760a7, 0x0068577e, 0x0063308c, 0x0068577e, 0x00dfd1ec, 0x002e2349}), MakePalette({0x00282236, 0x009574b4, 0x00c99af6, 0x00f0e7fa, 0x00c4b1d8, 0x00513178, 0x00fcf5ff, 0x00b590dd, 0x00c4b1d8, 0x00c99af6, 0x00c4b1d8, 0x003b2d4f, 0x00f0e7fa})}},
    {ImeSkinId::TeaMist, L"茶烟", L"Tea Mist", {MakePalette({0x00f0e3d8, 0x00b59a84, 0x007d3d22, 0x00503629, 0x007a5e4a, 0x009b451e, 0x00ffffff, 0x009b765c, 0x007a5e4a, 0x007d3d22, 0x007a5e4a, 0x00e4d0bf, 0x00503629}), MakePalette({0x00302820, 0x00a98565, 0x00e9c39b, 0x00f4eadd, 0x00ccb9a1, 0x00684831, 0x00fff1e3, 0x00c7a17b, 0x00ccb9a1, 0x00e9c39b, 0x00ccb9a1, 0x0045362a, 0x00f4eadd})}},
    {ImeSkinId::AzureTides, L"沧澜", L"Azure Tides", {MakePalette({0x00d7ecf2, 0x007395a6, 0x00175c72, 0x00123742, 0x00436876, 0x00126f78, 0x00ffffff, 0x004d8191, 0x00436876, 0x00175c72, 0x00436876, 0x00bddeeb, 0x00123742}), MakePalette({0x001c2b38, 0x005b869d, 0x009dcedc, 0x00e2f2f7, 0x00abc9d4, 0x00205d6c, 0x00f1fbfe, 0x0087bac8, 0x00abc9d4, 0x009dcedc, 0x00abc9d4, 0x002a4152, 0x00e2f2f7})}},
    {ImeSkinId::ThawingSnow, L"霁雪", L"Thawing Snow", {MakePalette({0x00f1e7f4, 0x00aa8db8, 0x00873976, 0x00422b4c, 0x00755a83, 0x0096378c, 0x00ffffff, 0x009763a3, 0x00755a83, 0x00873976, 0x00755a83, 0x00dfd8f0, 0x00422b4c}), MakePalette({0x002f263c, 0x00a684b9, 0x00edaade, 0x00f5eaf8, 0x00ceb5d8, 0x00723564, 0x00fff4fd, 0x00c7a1dd, 0x00ceb5d8, 0x00edaade, 0x00ceb5d8, 0x0044354f, 0x00f5eaf8})}},
    {ImeSkinId::SpringShore, L"春汀", L"Spring Shore", {MakePalette({0x00e6f1e7, 0x0087a191, 0x0021613d, 0x00234b35, 0x00536b5c, 0x000d742f, 0x00ffffff, 0x005c816a, 0x00536b5c, 0x0021613d, 0x00536b5c, 0x00cee1d4, 0x00234b35}), MakePalette({0x00202e27, 0x006b947d, 0x009fd3b0, 0x00e8f4ec, 0x00afcbbc, 0x00285c3b, 0x00effaf1, 0x0099c1ac, 0x00afcbbc, 0x009fd3b0, 0x00afcbbc, 0x00304336, 0x00e8f4ec})}},
    {ImeSkinId::PeachBlossom, L"桃夭", L"Peach Blossom", {MakePalette({0x00f9d8df, 0x00c39299, 0x009d3d4b, 0x00672d38, 0x0086525b, 0x00b63115, 0x00ffffff, 0x00ad6773, 0x0086525b, 0x009d3d4b, 0x0086525b, 0x00f0beca, 0x00672d38}), MakePalette({0x0035232a, 0x00b27c86, 0x00f1a599, 0x00f9e4e9, 0x00d9adb9, 0x00803d37, 0x00fff1e5, 0x00e7a3b2, 0x00d9adb9, 0x00f1a599, 0x00d9adb9, 0x004d2e35, 0x00f9e4e9})}},
    {ImeSkinId::EmeraldHaze, L"翠微", L"Emerald Haze", {MakePalette({0x00d0f2e8, 0x0076a89a, 0x00155f65, 0x00134a48, 0x00406f68, 0x00126f78, 0x00ffffff, 0x0045877e, 0x00406f68, 0x00155f65, 0x00406f68, 0x00ade3d1, 0x00134a48}), MakePalette({0x00172f2c, 0x00579981, 0x008cddd7, 0x00e1f7ef, 0x00abd7c8, 0x00235f5b, 0x00f0fcf7, 0x0082c5ac, 0x00abd7c8, 0x008cddd7, 0x00abd7c8, 0x00244640, 0x00e1f7ef})}},
}};

constexpr HRESULT kInvalidSkinData = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);

bool ValidCatalogId(std::wstring_view id) noexcept {
    return !id.empty() && id.size() <= 64 && id.front() >= L'a' && id.front() <= L'z' &&
        std::ranges::all_of(id, [](wchar_t ch) noexcept {
            return (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-';
        });
}

HRESULT ValidateName(std::wstring_view name) noexcept {
    if (name.empty() || name.size() > 128 ||
        std::ranges::any_of(name, [](wchar_t ch) noexcept { return ch < L' '; })) return kInvalidSkinData;
    if (!::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(),
            gsl::narrow_cast<int>(name.size()), nullptr, 0, nullptr, nullptr)) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }
    return S_OK;
}

HRESULT ValidatePalette(const SkinPalette& palette) noexcept {
    if (std::ranges::any_of(palette.colors, [](COLORREF value) noexcept {
            return (value & 0xff000000u) != 0;
        })) return kInvalidSkinData;
    for (const auto role : {SkinColor::CodeText, SkinColor::CandidateText, SkinColor::SecondaryText,
            SkinColor::EmptyText, SkinColor::PageEnabledText, SkinColor::PageDisabledText}) {
        if (ColorContrastRatio(palette.Get(role), palette.Get(SkinColor::Background)) < 4.5)
            return kInvalidSkinData;
    }
    if (ColorContrastRatio(palette.Get(SkinColor::SelectedText),
            palette.Get(SkinColor::SelectedBackground)) < 4.5 ||
        ColorContrastRatio(palette.Get(SkinColor::MenuHoverText),
            palette.Get(SkinColor::MenuHoverBackground)) < 4.5 ||
        ColorContrastRatio(palette.Get(SkinColor::SelectedBorder),
            palette.Get(SkinColor::Background)) < 3.0) return kInvalidSkinData;
    return S_OK;
}

template <typename Operation>
HRESULT SkinOperation(std::string_view name, Operation operation) noexcept {
    HRESULT result = S_OK;
    try {
        result = operation();
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
    }
    if (FAILED(result)) {
        QIQI_LOG_WARN("{}: skin configuration unavailable (hr=0x{:08X})",
            name, static_cast<unsigned long>(result));
    }
    return result;
}

HRESULT CheckKeyShape(HKEY key, DWORD children, DWORD values) noexcept {
    DWORD actual_children = 0;
    DWORD actual_values = 0;
    const LSTATUS error = ::RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, &actual_children,
        nullptr, nullptr, &actual_values, nullptr, nullptr, nullptr, nullptr);
    if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
    return children == actual_children && values == actual_values ? S_OK : kInvalidSkinData;
}

HRESULT OpenSkinKey(HKEY parent, const wchar_t* path, ScopedRegKey& key, HANDLE transaction = nullptr) noexcept {
    return HRESULT_FROM_WIN32(transaction
        ? ::RegOpenKeyTransactedW(parent, path, 0, KEY_READ, key.Put(), transaction, nullptr)
        : ::RegOpenKeyExW(parent, path, 0, KEY_READ, key.Put()));
}

HRESULT ReadPalette(HKEY parent, const wchar_t* variant, SkinPalette& palette, HANDLE transaction) {
    ScopedRegKey key;
    HRESULT result = OpenSkinKey(parent, variant, key, transaction);
    if (FAILED(result)) return result;
    for (size_t index = 0; index < kSkinColorCount; ++index) {
        DWORD value = 0;
        result = ReadRegistryDwordResult(key.Get(), gsl::at(kSkinColorNames, index), value);
        if (FAILED(result)) return result;
        if ((value & 0xff000000u) != 0) return kInvalidSkinData;
        gsl::at(palette.colors, index) = Rgb24ToColorRef(value);
    }
    return CheckKeyShape(key.Get(), 0, gsl::narrow_cast<DWORD>(kSkinColorCount));
}

HRESULT ReadCatalog(HKEY key, ImeSkinCatalog& catalog, HANDLE transaction = nullptr) {
    DWORD version = 0;
    HRESULT result = ReadRegistryDwordResult(key, L"SchemaVersion", version);
    if (FAILED(result)) return result;
    if (version != 1) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    std::wstring encoding;
    result = ReadRegistryStringResult(key, L"ColorEncoding", encoding);
    if (FAILED(result)) return result;
    if (encoding != L"RGB24") return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    for (DWORD index = 0; index < kBuiltinSkins.size(); ++index) {
        std::array<wchar_t, 65> name{};
        DWORD size = gsl::narrow_cast<DWORD>(name.size());
        const LSTATUS error = ::RegEnumKeyExW(key, index, name.data(), &size, nullptr, nullptr, nullptr, nullptr);
        if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);
        const auto id = ParseImeSkinId(std::wstring_view(name.data(), size));
        if (!id || *id == ImeSkinId::Default) return kInvalidSkinData;
        ScopedRegKey skin_key;
        result = OpenSkinKey(key, name.data(), skin_key, transaction);
        if (FAILED(result)) return result;
        ImeSkin skin;
        skin.id = *id;
        result = ReadRegistryStringResult(skin_key.Get(), L"NameZh", skin.name_zh);
        if (FAILED(result)) return result;
        result = ReadRegistryStringResult(skin_key.Get(), L"NameEn", skin.name_en);
        if (FAILED(result)) return result;
        result = ReadRegistryDwordResult(skin_key.Get(), L"DisplayOrder", skin.display_order);
        if (FAILED(result)) return result;
        result = ReadPalette(skin_key.Get(), L"Light", skin.palettes.light, transaction);
        if (FAILED(result)) return result;
        result = ReadPalette(skin_key.Get(), L"Dark", skin.palettes.dark, transaction);
        if (FAILED(result)) return result;
        result = CheckKeyShape(skin_key.Get(), 2, 3);
        if (FAILED(result)) return result;
        catalog.skins.push_back(std::move(skin));
    }
    result = CheckKeyShape(key, gsl::narrow_cast<DWORD>(kBuiltinSkins.size()), 2);
    if (FAILED(result)) return result;
    result = ValidateImeSkinCatalog(catalog);
    if (SUCCEEDED(result)) std::ranges::sort(catalog.skins, {}, &ImeSkin::display_order);
    return result;
}

HRESULT CreateSkinKey(HKEY parent, const wchar_t* path, ScopedRegKey& key, HANDLE transaction, DWORD* disposition = nullptr) noexcept {
    return HRESULT_FROM_WIN32(::RegCreateKeyTransactedW(parent, path, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr, key.Put(), disposition, transaction, nullptr));
}

HRESULT WritePalette(HKEY parent, const wchar_t* variant, const SkinPalette& palette, HANDLE transaction) {
    ScopedRegKey key;
    HRESULT result = CreateSkinKey(parent, variant, key, transaction);
    if (FAILED(result)) return result;
    for (size_t index = 0; index < kSkinColorCount; ++index) {
        result = WriteRegistryDwordResult(key.Get(), gsl::at(kSkinColorNames, index),
            ColorRefToRgb24(gsl::at(palette.colors, index)));
        if (FAILED(result)) return result;
    }
    return S_OK;
}

HRESULT PublishCatalog(const ImeSkinCatalog& catalog, const wchar_t* subkey, bool initialize) {
    if (IsProcessAppContainer()) return E_ACCESSDENIED;
    HRESULT result = ValidateImeSkinCatalog(catalog);
    if (FAILED(result)) return result;
    ScopedKernelTransaction transaction;
    if (!transaction) return HRESULT_FROM_WIN32(transaction.Error());
    ScopedRegKey root;
    result = OpenConfigRegistryKeyResult(root.Put(), KEY_READ | KEY_WRITE, subkey, transaction.Get());
    if (FAILED(result)) return result;
    ScopedRegKey skins;
    result = CreateSkinKey(root.Get(), L"Skins", skins, transaction.Get());
    if (FAILED(result)) return result;
    if (initialize) {
        std::wstring active;
        result = ReadRegistryStringResult(skins.Get(), L"ActiveCatalog", active);
        if (SUCCEEDED(result)) {
            if (!ValidCatalogId(active)) return kInvalidSkinData;
            ScopedRegKey active_key;
            result = OpenSkinKey(skins.Get(), (L"Catalogs\\" + active).c_str(), active_key, transaction.Get());
            if (FAILED(result)) return result;
            ImeSkinCatalog existing{active, {}};
            return ReadCatalog(active_key.Get(), existing, transaction.Get());
        }
        if (result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return result;
    }
    ScopedRegKey catalog_key;
    DWORD disposition = 0;
    result = CreateSkinKey(skins.Get(), (L"Catalogs\\" + catalog.id).c_str(),
        catalog_key, transaction.Get(), &disposition);
    if (FAILED(result)) return result;
    if (disposition != REG_CREATED_NEW_KEY && !initialize)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (disposition == REG_CREATED_NEW_KEY) {
        result = WriteRegistryDwordResult(catalog_key.Get(), L"SchemaVersion", 1);
        if (FAILED(result)) return result;
        result = WriteRegistryStringResult(catalog_key.Get(), L"ColorEncoding", L"RGB24");
        if (FAILED(result)) return result;
        for (const auto& skin : catalog.skins) {
            ScopedRegKey skin_key;
            const auto id = Utf8ToWide(ImeSkinIdToString(skin.id));
            result = CreateSkinKey(catalog_key.Get(), id.c_str(), skin_key, transaction.Get());
            if (FAILED(result)) return result;
            result = WriteRegistryStringResult(skin_key.Get(), L"NameZh", skin.name_zh);
            if (FAILED(result)) return result;
            result = WriteRegistryStringResult(skin_key.Get(), L"NameEn", skin.name_en);
            if (FAILED(result)) return result;
            result = WriteRegistryDwordResult(skin_key.Get(), L"DisplayOrder", skin.display_order);
            if (FAILED(result)) return result;
            result = WritePalette(skin_key.Get(), L"Light", skin.palettes.light, transaction.Get());
            if (FAILED(result)) return result;
            result = WritePalette(skin_key.Get(), L"Dark", skin.palettes.dark, transaction.Get());
            if (FAILED(result)) return result;
        }
    }
    ImeSkinCatalog verified{catalog.id, {}};
    result = ReadCatalog(catalog_key.Get(), verified, transaction.Get());
    if (FAILED(result)) return result;
    result = WriteRegistryStringResult(skins.Get(), L"ActiveCatalog", catalog.id);
    if (FAILED(result)) return result;
    // Initial catalog installation is not a user settings edit.
    if (!initialize) {
        result = BumpConfigRegistrySequenceResult(root.Get());
        if (FAILED(result)) return result;
    }
    catalog_key.Reset();
    skins.Reset();
    root.Reset();
    return transaction.Commit() ? S_OK : HRESULT_FROM_WIN32(transaction.Error());
}

} // namespace

std::span<const BuiltinImeSkin> GetBuiltinImeSkins() noexcept { return kBuiltinSkins; }

HRESULT MakeBuiltinImeSkinCatalog(ImeSkinCatalog& output) noexcept {
    return SkinOperation("MakeBuiltinImeSkinCatalog", [&]() {
        ImeSkinCatalog catalog{L"builtin-v1", {}};
        for (const auto& skin : kBuiltinSkins) {
            catalog.skins.push_back({skin.id, std::wstring(skin.name_zh), std::wstring(skin.name_en),
                static_cast<DWORD>(skin.id), skin.palettes});
        }
        const HRESULT result = ValidateImeSkinCatalog(catalog);
        if (SUCCEEDED(result)) output = std::move(catalog);
        return result;
    });
}

const ImeSkin* FindImeSkin(const ImeSkinCatalog& catalog, ImeSkinId id) noexcept {
    const auto found = std::ranges::find(catalog.skins, id, &ImeSkin::id);
    return found == catalog.skins.end() ? nullptr : &*found;
}

std::wstring_view ImeSkinDisplayName(const ImeSkinCatalog& catalog, ImeSkinId id, UiLanguage language) noexcept {
    if (id == ImeSkinId::Default) return LocalizedUiText(language, L"默认", L"Default");
    const auto* skin = FindImeSkin(catalog, id);
    if (!skin) return LocalizedUiText(language, L"不可用", L"Unavailable");
    return ResolveUiLanguage(language) == UiLanguage::EnUs ? skin->name_en : skin->name_zh;
}

double ColorContrastRatio(COLORREF first, COLORREF second) noexcept {
    const auto luminance = [](COLORREF color) noexcept {
        const auto linear = [](BYTE channel) noexcept {
            const double value = static_cast<double>(channel) / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(GetRValue(color)) + 0.7152 * linear(GetGValue(color)) + 0.0722 * linear(GetBValue(color));
    };
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

HRESULT ValidateImeSkinCatalog(const ImeSkinCatalog& catalog) noexcept {
    if (!ValidCatalogId(catalog.id) || catalog.skins.size() != kBuiltinSkins.size()) return kInvalidSkinData;
    std::array<bool, 22> ids{};
    std::array<bool, 22> orders{};
    for (const auto& skin : catalog.skins) {
        const auto id = static_cast<size_t>(skin.id);
        if (id == 0 || id >= ids.size() || gsl::at(ids, id) ||
            skin.display_order == 0 || skin.display_order >= orders.size() ||
            gsl::at(orders, skin.display_order)) return kInvalidSkinData;
        gsl::at(ids, id) = true;
        gsl::at(orders, skin.display_order) = true;
        HRESULT result = ValidateName(skin.name_zh);
        if (FAILED(result)) return result;
        result = ValidateName(skin.name_en);
        if (FAILED(result)) return result;
        result = ValidatePalette(skin.palettes.light);
        if (FAILED(result)) return result;
        result = ValidatePalette(skin.palettes.dark);
        if (FAILED(result)) return result;
    }
    return S_OK;
}

HRESULT LoadImeSkinCatalog(ImeSkinCatalog& output, const wchar_t* subkey) noexcept {
    return SkinOperation("LoadImeSkinCatalog", [&]() {
        ScopedRegKey root;
        HRESULT result = OpenConfigRegistryKeyResult(root.Put(), KEY_READ, subkey);
        if (FAILED(result)) return result;
        ScopedRegKey skins;
        result = OpenSkinKey(root.Get(), L"Skins", skins);
        if (FAILED(result)) return result;
        ImeSkinCatalog catalog;
        result = ReadRegistryStringResult(skins.Get(), L"ActiveCatalog", catalog.id);
        if (FAILED(result)) return result;
        if (!ValidCatalogId(catalog.id)) return kInvalidSkinData;
        ScopedRegKey key;
        result = OpenSkinKey(skins.Get(), (L"Catalogs\\" + catalog.id).c_str(), key);
        if (FAILED(result)) return result;
        result = ReadCatalog(key.Get(), catalog);
        if (SUCCEEDED(result)) output = std::move(catalog);
        return result;
    });
}

HRESULT EnsureBuiltinImeSkinCatalog(const wchar_t* subkey) noexcept {
    return SkinOperation("EnsureBuiltinImeSkinCatalog", [&]() {
        if (IsProcessAppContainer()) return E_ACCESSDENIED;
        ScopedRegKey root;
        HRESULT result = OpenConfigRegistryKeyResult(root.Put(), KEY_READ, subkey);
        if (SUCCEEDED(result)) {
            ScopedRegKey skins;
            result = OpenSkinKey(root.Get(), L"Skins", skins);
            if (SUCCEEDED(result)) {
                std::wstring active;
                result = ReadRegistryStringResult(skins.Get(), L"ActiveCatalog", active);
                if (SUCCEEDED(result)) {
                    ImeSkinCatalog catalog;
                    return LoadImeSkinCatalog(catalog, subkey);
                }
            }
        }
        if (result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) return result;
        ImeSkinCatalog catalog;
        result = MakeBuiltinImeSkinCatalog(catalog);
        return FAILED(result) ? result : PublishCatalog(catalog, subkey, true);
    });
}

HRESULT PublishImeSkinCatalog(const ImeSkinCatalog& catalog, const wchar_t* subkey) noexcept {
    return SkinOperation("PublishImeSkinCatalog", [&]() { return PublishCatalog(catalog, subkey, false); });
}

HRESULT UpdateImeSkinSnapshot(ImeSkinId id, std::optional<ImeSkinSnapshot>& snapshot, const wchar_t* subkey) noexcept {
    if (id == ImeSkinId::Default) {
        snapshot.reset();
        return S_OK;
    }
    return SkinOperation("UpdateImeSkinSnapshot", [&]() {
        if (ImeSkinIdToString(id).empty()) return kInvalidSkinData;
        ImeSkinCatalog catalog;
        const HRESULT result = LoadImeSkinCatalog(catalog, subkey);
        if (FAILED(result)) return result;
        const auto* skin = FindImeSkin(catalog, id);
        if (!skin) return kInvalidSkinData;
        snapshot = ImeSkinSnapshot{id, skin->palettes};
        return S_OK;
    });
}

ImeSkinAppearance ResolveImeSkinAppearance(const DisplayPreferences& preferences,
    const std::optional<ImeSkinSnapshot>& snapshot, SystemThemeSource source) noexcept {
    return ApplyImeSkinAppearance(ResolveDisplayAppearance(preferences, source), snapshot);
}

ImeSkinAppearance ApplyImeSkinAppearance(
    const DisplayAppearance& appearance, const std::optional<ImeSkinSnapshot>& snapshot) noexcept {
    ImeSkinAppearance result;
    result.display = appearance;
    auto& base = result.display.palette;
    result.colors.colors = {base.window_bg, base.border_color, base.text_accent, base.text_primary,
        base.text_secondary, base.selection_bg, base.selection_text, base.focus_color,
        base.text_primary, base.text_accent, base.text_secondary, base.hover_bg, base.text_primary};
    if (!snapshot || result.display.high_contrast) return result;
    result.colors = base.is_dark ? snapshot->palettes.dark : snapshot->palettes.light;
    const auto& colors = result.colors;
    base.window_bg = colors.Get(SkinColor::Background);
    base.border_color = colors.Get(SkinColor::Border);
    base.text_accent = colors.Get(SkinColor::CodeText);
    base.text_primary = colors.Get(SkinColor::CandidateText);
    base.text_secondary = colors.Get(SkinColor::SecondaryText);
    base.selection_bg = colors.Get(SkinColor::SelectedBackground);
    base.selection_text = colors.Get(SkinColor::SelectedText);
    base.focus_color = colors.Get(SkinColor::SelectedBorder);
    base.hover_bg = colors.Get(SkinColor::MenuHoverBackground);
    base.surface_bg = base.window_bg;
    base.input_bg = base.window_bg;
    return result;
}

} // namespace qiqi
