// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test::controltest {
static_assert(kFluentBodyFontSize == 14 && kFluentComboItemHeight == 22);
static_assert(kFluentComboTextCapacity == 128);
static_assert(kSmallBrandIconSize == 16 && kLargeBrandIconSize == 48);
namespace {

LRESULT CALLBACK CountComboTextReads(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR context) noexcept {
    if (message == CB_GETLBTEXT) {
        ++*reinterpret_cast<size_t*>(context);
    }
    return ::DefSubclassProc(hwnd, message, wparam, lparam);
}
} // namespace

void TestComboTextReadBounds(HWND combo, HDC dc, const FluentD2DThemeContext& d2d) {
    constexpr UINT_PTR kReadObserverSubclassId = 2;
    size_t reads = 0;
    QIQI_TEST_CHECK(::SetWindowSubclass(
        combo, CountComboTextReads, kReadObserverSubclassId, reinterpret_cast<DWORD_PTR>(&reads)));
    const auto remove_observer = gsl::finally([combo]() noexcept {
        ::RemoveWindowSubclass(combo, CountComboTextReads, kReadObserverSubclassId);
    });
    for (const size_t length : {126u, 127u, 128u}) {
        const std::wstring text(length, L'x');
        ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        QIQI_TEST_CHECK(::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())) == 0);
        QIQI_TEST_CHECK(::SendMessageW(combo, CB_SETCURSEL, 0, 0) == 0);
        reads = 0;
        PaintFluentComboBoxFrame(combo, dc, d2d);
        QIQI_TEST_CHECK(reads == (length < 127 ? 1u : 0u));
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_COMBOBOX;
        item.hwndItem = combo;
        item.hDC = dc;
        item.rcItem = {0, 0, 240, 32};
        reads = 0;
        DrawFluentComboBoxItem(item, d2d);
        QIQI_TEST_CHECK(reads == (length < 128 ? 1u : 0u));
    }
}
namespace {

LRESULT CALLBACK FailColumnInsertion(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR) noexcept {
    if (msg == LVM_INSERTCOLUMNW) return -1;
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}
} // namespace

void TestListViewColumns(HWND owner, HWND label) {
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    QIQI_TEST_CHECK(::InitCommonControlsEx(&controls));
    ScopedWindow list(::CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT,
        0, 0, 240, 100, owner, nullptr, ::GetModuleHandleW(nullptr), nullptr));
    QIQI_TEST_CHECK(list);
    const std::array labels{L"First", L"Second"};
    constexpr std::array widths{40, 120};
    QIQI_TEST_CHECK(InsertListViewColumns(nullptr, labels, widths, 96) == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
    QIQI_TEST_CHECK(InsertListViewColumns(label, labels, widths, 96) == E_INVALIDARG);
    QIQI_TEST_CHECK(InsertListViewColumns(list.Get(), labels, std::span(widths).first(1), 96) == E_INVALIDARG);
    const std::array<const wchar_t*, 2> invalid{L"First", nullptr};
    QIQI_TEST_CHECK(InsertListViewColumns(list.Get(), invalid, widths, 96) == E_INVALIDARG);
    constexpr std::array negative{40, -1};
    QIQI_TEST_CHECK(InsertListViewColumns(list.Get(), labels, negative, 96) == E_INVALIDARG);
    QIQI_TEST_CHECK(Header_GetItemCount(ListView_GetHeader(list.Get())) == 0);
    for (const UINT dpi : {0u, 96u, 144u, 192u}) {
        QIQI_TEST_CHECK(InsertListViewColumns(list.Get(), labels, widths, dpi) == S_OK);
        QIQI_TEST_CHECK(Header_GetItemCount(ListView_GetHeader(list.Get())) == 2);
        for (size_t i = 0; i < labels.size(); ++i) {
            std::array<wchar_t, 32> text{};
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = text.data();
            column.cchTextMax = gsl::narrow_cast<int>(text.size());
            QIQI_TEST_CHECK(ListView_GetColumn(list.Get(), gsl::narrow_cast<int>(i), &column));
            QIQI_TEST_CHECK(std::wstring_view(text.data()) == gsl::at(labels, i));
            QIQI_TEST_CHECK(column.cx == ScalePxByDpi(gsl::at(widths, i), dpi));
        }
        QIQI_TEST_CHECK(ListView_DeleteColumn(list.Get(), 1));
        QIQI_TEST_CHECK(ListView_DeleteColumn(list.Get(), 0));
    }
    QIQI_TEST_CHECK(::SetWindowSubclass(list.Get(), FailColumnInsertion, 89, 0));
    const auto restore = gsl::finally([hwnd = list.Get()]() noexcept {
        ::RemoveWindowSubclass(hwnd, FailColumnInsertion, 89);
    });
    QIQI_TEST_CHECK(InsertListViewColumns(list.Get(), labels, widths, 96) == E_FAIL);
    QIQI_TEST_CHECK(Header_GetItemCount(ListView_GetHeader(list.Get())) == 0);
}
namespace {

LRESULT CALLBACK FailComboMutation(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR, DWORD_PTR failure) noexcept {
    if (msg == CB_ADDSTRING && failure == 1) return CB_ERRSPACE;
    if ((msg == CB_ADDSTRING && failure == 2) || (msg == CB_SETCURSEL && failure == 3)) return CB_ERR;
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}
} // namespace

void TestComboItemReplacement(HWND combo, HWND label) {
    const std::array original{L"First", L"Second"};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, original) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 2);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 0);
    ::SendMessageW(combo, CB_SETCURSEL, 1, 0);
    const std::array translated{L"第一项", L"第二项", L"第三项"};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, translated) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);
    std::array<wchar_t, 16> selected{};
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETLBTEXT, 1, reinterpret_cast<LPARAM>(selected.data())) == 3);
    QIQI_TEST_CHECK(std::wstring_view(selected.data()) == gsl::at(translated, 1));
    const std::array<const wchar_t*, 1> invalid{nullptr};
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, invalid) == E_INVALIDARG);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 3);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ReplaceComboBoxItems(label, original) == E_INVALIDARG);
    QIQI_TEST_CHECK(ReplaceComboBoxItems(nullptr, original) == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE));
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, std::span(original).first(1)) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == 0);
    for (DWORD_PTR failure = 1; failure <= 3; ++failure) {
        QIQI_TEST_CHECK(::SetWindowSubclass(combo, FailComboMutation, 88, failure));
        const auto restore = gsl::finally([combo]() noexcept {
            ::RemoveWindowSubclass(combo, FailComboMutation, 88);
        });
        QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, original) == (failure == 1 ? E_OUTOFMEMORY : E_FAIL));
    }
    QIQI_TEST_CHECK(ReplaceComboBoxItems(combo, {}) == S_OK);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCOUNT, 0, 0) == 0);
    QIQI_TEST_CHECK(::SendMessageW(combo, CB_GETCURSEL, 0, 0) == CB_ERR);
}
} // namespace qiqi::test::controltest
