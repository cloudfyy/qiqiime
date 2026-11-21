// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"
#include "common/skin.h"

namespace qiqi::test::common_cases {

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
    for (const auto& expected : builtin.skins) {
        const auto* actual = FindImeSkin(loaded, expected.id);
        QIQI_TEST_CHECK(actual && actual->name_zh == expected.name_zh && actual->name_en == expected.name_en);
        QIQI_TEST_CHECK(actual->display_order == expected.display_order && actual->palettes == expected.palettes);
    }
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
    updated.skins.front().palettes.light.colors[static_cast<size_t>(SkinColor::EmptyText)] =
        updated.skins.front().palettes.light.Get(SkinColor::CodeText);
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
    invalid.skins.front().palettes.light.colors[static_cast<size_t>(SkinColor::CandidateText)] =
        invalid.skins.front().palettes.light.Get(SkinColor::Background);
    QIQI_TEST_CHECK(FAILED(PublishImeSkinCatalog(invalid, subkey.c_str())));
    QIQI_TEST_CHECK(LoadImeSkinCatalog(loaded, subkey.c_str()) == S_OK && loaded.id == L"test-v2");
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(subkey.c_str()) == sequence + 1);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) == S_OK);
    const auto last_good = snapshot;
    light.Reset();
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER,
        (subkey + L"\\Skins\\Catalogs\\test-v2\\firefly\\Light").c_str(), 0,
        KEY_SET_VALUE, light.Put()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(::RegDeleteValueW(light.Get(), L"EmptyText") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(UpdateImeSkinSnapshot(ImeSkinId::Firefly, snapshot, subkey.c_str()) ==
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    QIQI_TEST_CHECK(snapshot == last_good);
    QIQI_TEST_CHECK(EnsureBuiltinImeSkinCatalog(subkey.c_str()) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));

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
    const WORD short_value = 7;
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
