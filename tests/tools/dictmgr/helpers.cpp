// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

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
