// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controls.h"

#include "common/logger.h"
#include "common/uiutil.h"

#include <commctrl.h>
#include <cwchar>
#include <gsl/gsl>

namespace qiqi {
namespace {
constexpr const wchar_t* kUnderlineComboBoldIdProp = L"QiQiUnderlineComboBoldId";

LRESULT CALLBACK FluentToggleSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const int radio_range_pack = gsl::narrow_cast<int>(dwRefData);
    const bool is_radio = (radio_range_pack != 0);
    switch (msg) {
        case BM_GETCHECK:
            return ::GetPropW(hwnd, L"QiQiChecked") ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK: {
            if (wparam == BST_CHECKED) {
                if (!::SetPropW(hwnd, L"QiQiChecked", reinterpret_cast<HANDLE>(1))) {
                    QIQI_LOG_ERROR("Fluent toggle SetPropW failed (error={})", ::GetLastError());
                }
            } else {
                ::RemovePropW(hwnd, L"QiQiChecked");
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            if (msg == WM_KEYUP && wparam != VK_SPACE) {
                break;
            }
            if ((::SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) == 0) {
                break;
            }
            if (msg == WM_LBUTTONUP) {
                RECT rect{};
                ::GetClientRect(hwnd, &rect);
                if (!::PtInRect(&rect, PointFromLParam(lparam))) {
                    break;
                }
            }
            if (::IsWindowEnabled(hwnd)) {
                const HWND parent = ::GetParent(hwnd);
                const int id = ::GetDlgCtrlID(hwnd);
                if (is_radio) {
                    const int first_id = LOWORD(radio_range_pack);
                    const int last_id = HIWORD(radio_range_pack);
                    for (int rid = first_id; rid <= last_id; ++rid) {
                        if (const HWND rctrl = ::GetDlgItem(parent, rid)) {
                            DWORD_PTR rref = 0;
                            if (::GetWindowSubclass(rctrl, &FluentToggleSubclassProc, uIdSubclass, &rref) &&
                                rref == dwRefData) {
                                ::SendMessageW(rctrl, BM_SETCHECK, (rid == id) ? BST_CHECKED : BST_UNCHECKED, 0);
                            }
                        }
                    }
                } else {
                    const bool cur = (::SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    ::SendMessageW(hwnd, BM_SETCHECK, cur ? BST_UNCHECKED : BST_CHECKED, 0);
                }
            }
            break;
        }
        case WM_NCDESTROY:
            ::RemovePropW(hwnd, L"QiQiChecked");
            ::RemoveWindowSubclass(hwnd, &FluentToggleSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK FluentComboBoxSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const auto* d2d = reinterpret_cast<const FluentD2DThemeContext*>(dwRefData);
    const bool fluent = d2d && d2d->d2d_target && d2d->fmt_body && !d2d->appearance.system_high_contrast;
    switch (msg) {
        case WM_ERASEBKGND:
            if (fluent) return 1;
            break;
        case WM_PAINT: {
            if (!fluent) {
                break;
            }
            const ScopedPaintDC paint_dc(hwnd);
            PaintFluentComboBoxFrame(hwnd, paint_dc.Get(), *d2d);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case CB_SETCURSEL: {
            const LRESULT res = ::DefSubclassProc(hwnd, msg, wparam, lparam);
            DWORD_PTR current_ref = 0;
            // A parent notification may destroy the child and its borrowed context.
            if (::GetWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass, &current_ref) &&
                current_ref == dwRefData && fluent) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return res;
        }
        case WM_NCDESTROY:
            ::RemovePropW(hwnd, kUnderlineComboBoldIdProp);
            ::RemoveWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

} // namespace

HWND CreateFluentChildControl(
    HWND parent,
    HINSTANCE inst,
    HFONT font,
    const FluentD2DThemeContext* d2d_ctx,
    const wchar_t* cls,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    int id,
    DWORD_PTR radio_group_range) noexcept {
    const bool is_button = (_wcsicmp(cls, L"BUTTON") == 0);
    const DWORD button_type = style & BS_TYPEMASK;
    const bool is_radio = is_button && button_type == BS_AUTORADIOBUTTON;
    const bool is_check = is_button && button_type == BS_AUTOCHECKBOX;
    const bool is_combo = (_wcsicmp(cls, L"COMBOBOX") == 0);
    if (is_radio && (radio_group_range == 0 || id < LOWORD(radio_group_range) || id > HIWORD(radio_group_range))) {
        QIQI_LOG_ERROR("Fluent radio control {} has an invalid group range", id);
        return nullptr;
    }
    DWORD actual_style = style;
    if (is_radio || is_check) {
        actual_style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW | WS_TABSTOP;
    }
    ScopedWindow ctrl(::CreateWindowExW(
        0,
        cls,
        text,
        WS_CHILD | WS_VISIBLE | actual_style,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(gsl::narrow_cast<INT_PTR>(id)),
        inst,
        nullptr));
    if (!ctrl) {
        QIQI_LOG_ERROR("CreateFluentChildControl {} failed (error={})", id, ::GetLastError());
        return nullptr;
    }
    if (font) {
        ::SendMessageW(ctrl.Get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (is_radio || is_check) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentToggleSubclassProc, 1, is_radio ? radio_group_range : 0)) {
            QIQI_LOG_ERROR("Fluent toggle {} SetWindowSubclass failed", id);
            return nullptr;
        }
    } else if (is_combo && d2d_ctx) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentComboBoxSubclassProc, 2, reinterpret_cast<DWORD_PTR>(d2d_ctx))) {
            QIQI_LOG_ERROR("Fluent combo {} SetWindowSubclass failed", id);
            return nullptr;
        }
    }
    return ctrl.Detach();
}

} // namespace qiqi
