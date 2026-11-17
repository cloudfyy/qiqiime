// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

#include <objbase.h>

namespace qiqi::dict_manager::tests {

// Building the isolated registry subkey allocates and can throw std::bad_alloc.
#pragma warning(suppress : 26455)
ConfigRegistryFixture::ConfigRegistryFixture() : previous_(GetActiveConfigRegistrySubKey()) {
    GUID id{};
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&id)));
    std::array<wchar_t, 39> text{};
    QIQI_TEST_CHECK(::StringFromGUID2(id, text.data(), gsl::narrow_cast<int>(text.size())) != 0);
    // Aborting a GUI test must never leave a key inside the real configuration.
    subkey_ = L"Software\\QiQiIME_DictManagerTest_" + std::wstring(text.data());
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;KA;;;OW)(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)",
        SDDL_REVISION_1, &descriptor, nullptr));
    const ScopedLocalAlloc release(descriptor);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    DWORD disposition = 0;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey_.c_str(), 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, &security, key_.Put(), &disposition) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(disposition == REG_CREATED_NEW_KEY);
    SetConfigRegistrySubKeyOverrideForTest(subkey_.c_str());
}

ConfigRegistryFixture::~ConfigRegistryFixture() noexcept {
    SetConfigRegistrySubKeyOverrideForTest(previous_);
    key_.Reset();
    QIQI_TEST_CHECK_CLEANUP(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey_.c_str()) == ERROR_SUCCESS);
}

void TestConfigRegistryIsolation() {
    ConfigRegistryFixture isolated_user;
    QIQI_TEST_CHECK(::RegOverridePredefKey(HKEY_CURRENT_USER, isolated_user.Get()) == ERROR_SUCCESS);
    const auto restore_user = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(isolated_user.Subkey().c_str());
        QIQI_TEST_CHECK_CLEANUP(::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS);
    });
    SetConfigRegistrySubKeyOverrideForTest(nullptr);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;;KA;;;OW)(A;OICI;KR;;;AC)(A;OICI;KR;;;S-1-15-2-2)",
        SDDL_REVISION_1, &descriptor, nullptr));
    const ScopedLocalAlloc release(descriptor);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    ScopedRegKey production;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, kDefaultConfigRegSubKey, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, &security, production.Put(), nullptr) == ERROR_SUCCESS);
    constexpr DWORD sentinel = 42;
    QIQI_TEST_CHECK(::RegSetValueExW(production.Get(), L"FixtureSentinel", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&sentinel), sizeof(sentinel)) == ERROR_SUCCESS);
    std::wstring removed;
    {
        ConfigRegistryFixture display;
        removed = display.Subkey();
        QiQiConfig saved;
        saved.page_size = 7;
        QIQI_TEST_CHECK(SaveFixtureConfig(saved, display.Subkey()));
        QiQiConfig loaded;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(loaded) && loaded.page_size == saved.page_size);
    }
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == kDefaultConfigRegSubKey);
    ScopedRegKey absent;
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, removed.c_str(), 0,
        KEY_READ, absent.Put()) == ERROR_FILE_NOT_FOUND);
    DWORD subkeys = 0;
    QIQI_TEST_CHECK(::RegQueryInfoKeyW(production.Get(), nullptr, nullptr, nullptr, &subkeys,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS && subkeys == 0);
    DWORD retained = 0, type = 0, size = sizeof(retained);
    QIQI_TEST_CHECK(::RegQueryValueExW(production.Get(), L"FixtureSentinel", nullptr, &type,
        reinterpret_cast<BYTE*>(&retained), &size) == ERROR_SUCCESS &&
        type == REG_DWORD && retained == sentinel);
}

bool SaveFixtureConfig(const QiQiConfig& config, std::wstring_view expected_subkey) {
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == expected_subkey);
    ::SetLastError(ERROR_SUCCESS);
    const bool saved = SaveConfig(config);
    const DWORD error = ::GetLastError();
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == expected_subkey);
    if (!saved) {
        HKEY key = nullptr;
        const LSTATUS open_error = ::RegOpenKeyExW(HKEY_CURRENT_USER,
            GetActiveConfigRegistrySubKey(), 0, KEY_READ | KEY_WRITE, &key);
        if (key) ::RegCloseKey(key);
        std::wcerr << L"SaveConfig fixture failure: test key=" << expected_subkey
                   << L", GetLastError(non-contractual)=" << error
                   << L", RegOpenKeyEx=" << open_error << L'\n';
    }
    return saved;
}

HWND Child(const DictManagerWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

RECT ToolbarItemRect(const DictManagerWindow& window, ControlId id) {
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
    RECT rect{};
    QIQI_TEST_CHECK(::SendMessageW(Child(window, id == SaveWord ? InlineSave : Toolbar), TB_GETRECT,
        std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
    return rect;
}

void InvokeCommand(const DictManagerWindow& window, ControlId id, bool toolbar) {
    if (toolbar) {
        const HWND target = Child(window, id == SaveWord ? InlineSave : Toolbar);
        QIQI_TEST_CHECK(::SendMessageW(target, TB_COMMANDTOINDEX, std::to_underlying(id), 0) >= 0);
        const auto rect = ToolbarItemRect(window, id);
        const LPARAM point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
        ::SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, point);
        ::SendMessageW(target, WM_LBUTTONUP, 0, point);
        return;
    }
    ::SendMessageW(window.Handle(), WM_COMMAND, std::to_underlying(id), 0);
}

std::wstring MenuText(HMENU menu, ControlId id) {
    std::array<wchar_t, 256> text{};
    QIQI_TEST_CHECK(::GetMenuStringW(menu, std::to_underlying(id), text.data(),
        gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND) > 0);
    return text.data();
}

void DrainQuitMessages() noexcept {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

void ClickRowEdit(const DictManagerWindow& window, int row) {
    const HWND list = Child(window, List);
    ::SetActiveWindow(window.Handle());
    ListView_EnsureVisible(list, row, FALSE);
    RECT rect{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
    QIQI_TEST_CHECK(ListView_GetItemRect(list, row, &rect, LVIR_BOUNDS));
    const POINT point{rect.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) / 2,
        (rect.top + rect.bottom) / 2};
    LVHITTESTINFO hit{};
    hit.pt = point;
    QIQI_TEST_CHECK(ListView_SubItemHitTest(list, &hit) == row);
    QIQI_TEST_CHECK(hit.iSubItem == std::to_underlying(ColumnId::Edit));
    const LPARAM position = MAKELPARAM(point.x, point.y);
    ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, position);
    QIQI_TEST_CHECK(::GetCapture() == list);
    ::SendMessageW(list, WM_LBUTTONUP, 0, position);
    QIQI_TEST_CHECK(::GetCapture() != list);
    QIQI_TEST_CHECK(::GetFocus() == Child(window, EditCodeInput));
}

} // namespace qiqi::dict_manager::tests
