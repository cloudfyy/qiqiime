// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"
#include "common/skin.h"
#include <sddl.h>
#include <cstdint>
#include <utility>
#include <vector>

namespace qiqi::test::common_cases {
namespace {

template <typename Operation>
void WithRestrictedRegistryKey(
    HKEY base, std::wstring_view path, std::wstring_view security, Operation&& operation) {
    const std::wstring key_path(path);
    ScopedRegKey key;
    QIQI_TEST_CHECK(::RegOpenKeyExW(base, key_path.c_str(), 0,
        READ_CONTROL | WRITE_DAC, key.Put()) == ERROR_SUCCESS);
    DWORD original_size = 0;
    QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION,
        nullptr, &original_size) == ERROR_INSUFFICIENT_BUFFER);
    std::vector<uint8_t> original(original_size);
    DWORD actual_size = original_size;
    QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION,
        original.data(), &actual_size) == ERROR_SUCCESS);
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    QIQI_TEST_CHECK(::GetSecurityDescriptorControl(
        reinterpret_cast<PSECURITY_DESCRIPTOR>(original.data()), &control, &revision));
    const SECURITY_INFORMATION restore_flags = DACL_SECURITY_INFORMATION |
        ((control & SE_DACL_PROTECTED)
            ? PROTECTED_DACL_SECURITY_INFORMATION
            : UNPROTECTED_DACL_SECURITY_INFORMATION);
    PSECURITY_DESCRIPTOR restricted = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        std::wstring(security).c_str(), SDDL_REVISION_1, &restricted, nullptr));
    const ScopedLocalAlloc release_restricted(restricted);
    QIQI_TEST_CHECK(::RegSetKeySecurity(key.Get(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        restricted) == ERROR_SUCCESS);
    const auto restore = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(::RegSetKeySecurity(key.Get(),
            restore_flags, original.data()) == ERROR_SUCCESS);
    });
    std::forward<Operation>(operation)();
}

} // namespace

void TestImeSkins() {
    const std::wstring subkey = L"Software\\QiQiIME_SkinTest_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(::GetTickCount64());
    const auto cleanup = gsl::finally([&]() noexcept {
        const LSTATUS error = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND);
    });
    std::optional<ImeSkinSnapshot> snapshot;
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Default, snapshot, subkey.c_str()) == S_OK);
    ScopedRegKey root;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, root.Put()) ==
        ERROR_FILE_NOT_FOUND);

    ImeSkinCatalog builtin;
    QIQI_TEST_CHECK(MakeBuiltinImeSkinCatalog(builtin) == S_OK && builtin.skins.size() == 21);
    QIQI_TEST_CHECK(EnsureBuiltinImeSkinCatalog(subkey.c_str()) == S_OK);
    ImeSkinCatalog loaded;
    QIQI_TEST_CHECK(LoadImeSkinCatalog(loaded, subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(loaded.id == builtin.id && loaded.skins.size() == builtin.skins.size());
    size_t verified_fields = 0;
    for (const auto& expected : builtin.skins) {
        const auto* actual = FindImeSkin(loaded, expected.id);
        QIQI_TEST_CHECK(actual && actual->name_zh == expected.name_zh && actual->name_en == expected.name_en);
        QIQI_TEST_CHECK(actual->display_order == expected.display_order && actual->palettes == expected.palettes);
        for (const SkinPalette* palette : {&actual->palettes.light, &actual->palettes.dark}) {
            for (const COLORREF color : palette->colors) {
                const DWORD rgb24 = ColorRefToRgb24(color);
                QIQI_TEST_CHECK((rgb24 & 0xff000000u) == 0 && Rgb24ToColorRef(rgb24) == color);
                ++verified_fields;
            }
        }
    }
    QIQI_TEST_CHECK(verified_fields == 42 * kSkinColorCount);
    auto incomplete = builtin;
    incomplete.skins.pop_back();
    QIQI_TEST_CHECK(FAILED(ValidateImeSkinCatalog(incomplete)));
    auto duplicate_id = builtin;
    duplicate_id.skins.back().id = duplicate_id.skins.front().id;
    QIQI_TEST_CHECK(FAILED(ValidateImeSkinCatalog(duplicate_id)));
    ScopedRegKey light;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\builtin-v1\\firefly\\Light").c_str(), 0,
        KEY_READ | KEY_SET_VALUE, light.Put()) == ERROR_SUCCESS);
    DWORD rgb24 = 0;
    DWORD size = sizeof(rgb24);
    QIQI_TEST_CHECK(::RegGetValueW(light.Get(), nullptr, L"Background", RRF_RT_REG_DWORD,
        nullptr, &rgb24, &size) == ERROR_SUCCESS && rgb24 == 0x00f5d96a);
    QIQI_TEST_CHECK(FindImeSkin(loaded, ImeSkinId::Firefly)->palettes.light.Get(SkinColor::Background) ==
        RGB(245, 217, 106));
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) == S_OK && snapshot);
    const auto previous = snapshot;
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Invalid, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && snapshot == previous);

    ImeSkinCatalog updated = builtin;
    updated.id = L"test-v2";
    auto& updated_light = updated.skins.front().palettes.light;
    gsl::at(updated_light.colors, static_cast<size_t>(SkinColor::EmptyText)) =
        updated_light.Get(SkinColor::CodeText);
    const auto sequence = QueryRegistryConfigSequence(subkey.c_str());
    QIQI_TEST_CHECK(PublishImeSkinCatalog(updated, subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);
    QIQI_TEST_CHECK(EnsureBuiltinImeSkinCatalog(subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);
    QIQI_TEST_CHECK(LoadImeSkinCatalog(loaded, subkey.c_str()) == S_OK && loaded.id == L"test-v2");
    QIQI_TEST_CHECK(FindImeSkin(loaded, ImeSkinId::Firefly)->palettes == updated.skins.front().palettes);
    QIQI_TEST_CHECK(PublishImeSkinCatalog(updated, subkey.c_str()) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);

    auto invalid = updated;
    invalid.id = L"bad-v3";
    auto& invalid_light = invalid.skins.front().palettes.light;
    gsl::at(invalid_light.colors, static_cast<size_t>(SkinColor::CandidateText)) =
        invalid_light.Get(SkinColor::Background);
    QIQI_TEST_CHECK(FAILED(PublishImeSkinCatalog(invalid, subkey.c_str())));
    QIQI_TEST_CHECK(LoadImeSkinCatalog(loaded, subkey.c_str()) == S_OK && loaded.id == L"test-v2");
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) == S_OK);
    const auto last_good = snapshot;

    auto denied_catalog = loaded;
    auto denied_snapshot = snapshot;
    WithRestrictedRegistryKey(HKEY_CURRENT_USER,
        subkey + L"\\Skins", L"D:P(D;;KR;;;WD)(A;;KA;;;WD)", [&]() {
            QIQI_TEST_CHECK(LoadImeSkinCatalog(denied_catalog, subkey.c_str()) ==
                HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
            QIQI_TEST_CHECK(denied_catalog.id == L"test-v2");
            QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, denied_snapshot, subkey.c_str()) ==
                HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
            QIQI_TEST_CHECK(denied_snapshot == last_good);
        });

    auto staged = updated;
    staged.id = L"test-v3";
    WithRestrictedRegistryKey(HKEY_CURRENT_USER,
        subkey + L"\\Skins\\Catalogs", L"D:P(D;;KW;;;WD)(A;;KA;;;WD)", [&]() {
            QIQI_TEST_CHECK(PublishImeSkinCatalog(staged, subkey.c_str()) ==
                HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
        });
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);
    QIQI_TEST_CHECK(LoadImeSkinCatalog(loaded, subkey.c_str()) == S_OK && loaded.id == L"test-v2");
    ScopedRegKey staged_key;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\test-v3").c_str(), 0, KEY_READ,
        staged_key.Put()) == ERROR_FILE_NOT_FOUND);

    ScopedRegKey catalog_key;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\test-v2").c_str(), 0,
        KEY_READ | KEY_SET_VALUE, catalog_key.Put()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(WriteRegistryDwordResult(catalog_key.Get(), L"SchemaVersion", 2) == S_OK);
    ImeSkinCatalog rejected{L"unchanged", {}};
    QIQI_TEST_CHECK(LoadImeSkinCatalog(rejected, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) && rejected.id == L"unchanged");
    QIQI_TEST_CHECK(WriteRegistryDwordResult(catalog_key.Get(), L"SchemaVersion", 1) == S_OK);
    QIQI_TEST_CHECK(WriteRegistryStringResult(
        catalog_key.Get(), L"ColorEncoding", L"RGBA") == S_OK);
    QIQI_TEST_CHECK(LoadImeSkinCatalog(rejected, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && rejected.id == L"unchanged");
    QIQI_TEST_CHECK(WriteRegistryStringResult(
        catalog_key.Get(), L"ColorEncoding", L"RGB24") == S_OK);

    light.Reset();
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\test-v2\\firefly\\Light").c_str(), 0,
        KEY_SET_VALUE, light.Put()) == ERROR_SUCCESS);
    const DWORD original_background =
        ColorRefToRgb24(updated.skins.front().palettes.light.Get(SkinColor::Background));
    QIQI_TEST_CHECK(WriteRegistryStringResult(
        light.Get(), L"Background", L"not-rgb24") == S_OK);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_MORE_DATA));
    QIQI_TEST_CHECK(snapshot == last_good);
    QIQI_TEST_CHECK(WriteRegistryDwordResult(
        light.Get(), L"Background", original_background) == S_OK);
    QIQI_TEST_CHECK(WriteRegistryDwordResult(
        light.Get(), L"Background", 0x01000000u) == S_OK);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    QIQI_TEST_CHECK(snapshot == last_good);
    QIQI_TEST_CHECK(WriteRegistryDwordResult(
        light.Get(), L"Background", original_background) == S_OK);
    QIQI_TEST_CHECK(::RegDeleteValueW(light.Get(), L"EmptyText") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    QIQI_TEST_CHECK(snapshot == last_good);
    QIQI_TEST_CHECK(EnsureBuiltinImeSkinCatalog(subkey.c_str()) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    QIQI_TEST_CHECK(WriteRegistryDwordResult(light.Get(), L"EmptyText",
        ColorRefToRgb24(updated.skins.front().palettes.light.Get(SkinColor::EmptyText))) == S_OK);
    ScopedRegKey firefly_key;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\test-v2\\firefly").c_str(), 0,
        KEY_ALL_ACCESS, firefly_key.Put()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(::RegDeleteTreeW(firefly_key.Get(), L"Dark") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    QIQI_TEST_CHECK(snapshot == last_good);

    QiQiConfig config;
    config.ime_skin_id = ImeSkinId::PeachBlossom;
    config.page_size = 7;
    QIQI_TEST_CHECK(SaveConfigToRegistryResult(config, subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
        KEY_READ | KEY_SET_VALUE, root.Put()) == ERROR_SUCCESS);
    std::wstring id;
    QIQI_TEST_CHECK(ReadRegistryStringResult(root.Get(), L"ImeSkinId", id) == S_OK && id == L"peach-blossom");
    config.ime_skin_id = ImeSkinId::Invalid;
    config.page_size = 9;
    QIQI_TEST_CHECK(SaveConfigToRegistryResult(config, subkey.c_str()) == E_INVALIDARG);
    QIQI_TEST_CHECK(LoadConfigFromRegistry(config, nullptr, subkey.c_str()));
    QIQI_TEST_CHECK(config.ime_skin_id == ImeSkinId::PeachBlossom && config.page_size == 7);
    QIQI_TEST_CHECK(WriteRegistryStringResult(root.Get(), L"ImeSkinId", L"not-a-skin") == S_OK);
    QIQI_TEST_CHECK(LoadConfigFromRegistry(config, nullptr, subkey.c_str()) && config.ime_skin_id == ImeSkinId::Invalid);
    QIQI_TEST_CHECK(::RegDeleteValueW(root.Get(), L"ImeSkinId") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(LoadConfigFromRegistry(config, nullptr, subkey.c_str()) && config.ime_skin_id == ImeSkinId::Default);
    DWORD unchanged = 42;
    QIQI_TEST_CHECK(WriteRegistryStringResult(root.Get(), L"BadDword", L"text") == S_OK);
    QIQI_TEST_CHECK(ReadRegistryDwordResult(root.Get(), L"BadDword", unchanged) ==
        HRESULT_FROM_WIN32(ERROR_MORE_DATA) && unchanged == 42);
    QIQI_TEST_CHECK(WriteRegistryStringResult(root.Get(), L"BadDword", L"x") == S_OK);
    QIQI_TEST_CHECK(ReadRegistryDwordResult(root.Get(), L"BadDword", unchanged) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && unchanged == 42);
    constexpr WORD short_value = 7;
    QIQI_TEST_CHECK(::RegSetValueExW(root.Get(), L"BadDword", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&short_value), sizeof(short_value)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadRegistryDwordResult(root.Get(), L"BadDword", unchanged) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA) && unchanged == 42);
    QIQI_TEST_CHECK(::RegDeleteValueW(root.Get(), L"BadDword") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadRegistryDwordResult(root.Get(), L"BadDword", unchanged) ==
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && unchanged == 42);

    const auto old_app_dark = detail::ThemeDarkOverrideStorage();
    const auto old_windows_dark = detail::TaskbarDarkOverrideStorage();
    const auto restore_theme = gsl::finally([&]() noexcept {
        SetHighContrastOverrideForTest(std::nullopt);
        SetThemeDarkOverrideForTest(old_app_dark);
        SetTaskbarDarkOverrideForTest(old_windows_dark);
    });
    SetHighContrastOverrideForTest(false);
    SetThemeDarkOverrideForTest(false);
    SetTaskbarDarkOverrideForTest(true);
    QIQI_TEST_CHECK(ResolveImeSkinAppearance({}, last_good).colors == last_good->palettes.light);
    QIQI_TEST_CHECK(ResolveImeSkinAppearance({}, last_good, SystemThemeSource::Windows).colors == last_good->palettes.dark);
    QIQI_TEST_CHECK(ResolveImeSkinAppearance({UiTheme::Dark}, last_good).colors == last_good->palettes.dark);
    QIQI_TEST_CHECK(ResolveImeSkinAppearance({UiTheme::HighContrast}, last_good).display.high_contrast);
    SetHighContrastOverrideForTest(true);
    QIQI_TEST_CHECK(ResolveImeSkinAppearance({UiTheme::Light}, last_good).colors.Get(SkinColor::Background) ==
        ::GetSysColor(COLOR_WINDOW));
}

} // namespace qiqi::test::common_cases
