// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"

#include <gsl/gsl>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;
namespace layout = dict_manager_layout;

LRESULT CALLBACK DictManagerWindow::Impl::ListProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, ListProcedure, subclass);
    } else if (message == WM_KEYDOWN && self && self->ready) {
        try {
            if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (wparam == 'C' || wparam == VK_INSERT) {
                    self->CopySelection();
                    return 0;
                }
                if (wparam == 'A') {
                    self->SelectAllRows();
                    return 0;
                }
            } else if (wparam == VK_DELETE) {
                self->Command(std::to_underlying(Delete), 0);
                return 0;
            } else if (wparam == VK_F2) {
                self->BeginEdit(ListView_GetNextItem(window, -1, LVNI_FOCUSED), std::to_underlying(ColumnId::Word));
                return 0;
            } else if (wparam == VK_ESCAPE && self->editor.Active()) {
                self->CancelEditing();
                return 0;
            }
        } catch (const std::exception&) {
            QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary, "DictManager ListProcedure shortcut failed with an exception");
            return 0;
        }
    } else if (message == WM_NOTIFY && self) {
        if (const auto drawn = TryCustomDrawDarkListViewHeader(window, lparam, self->font.Get(), self->theme)) {
            return *drawn;
        }
    }
    int activate_edit_row = -1;
    bool edit_mouse_handled = false;
    if (self && self->ready) {
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
            LVHITTESTINFO hit{};
            hit.pt = PointFromLParam(lparam);
            const int row = ListView_SubItemHitTest(window, &hit);
            const bool edit_icon = row >= 0 && hit.iSubItem == std::to_underlying(ColumnId::Edit);
            if (message == WM_LBUTTONDOWN) {
                self->pressed_edit_row = edit_icon ? row : -1;
                if (edit_icon) {
                    ::SetFocus(window);
                    ListView_SetItemState(window, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemState(window, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ::SetCapture(window);
                    if (::GetCapture() != window) {
                        self->pressed_edit_row = -1;
                        ReportCommandError(self, "SetCapture(edit icon)", ERROR_SUCCESS);
                    }
                    edit_mouse_handled = true;
                }
            } else {
                const int pressed = std::exchange(self->pressed_edit_row, -1);
                if (pressed >= 0) {
                    if (::GetCapture() == window && !::ReleaseCapture()) {
                        ReportCommandError(self, "ReleaseCapture(edit icon)", ::GetLastError());
                    }
                    if (edit_icon && row == pressed) activate_edit_row = row;
                    edit_mouse_handled = true;
                }
            }
        } else if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
            self->pressed_edit_row = -1;
            if (message == WM_CANCELMODE && ::GetCapture() == window && !::ReleaseCapture()) {
                ReportCommandError(self, "ReleaseCapture(cancel edit icon)", ::GetLastError());
            }
        }
    }
    const LRESULT result = edit_mouse_handled ? 0 : ::DefSubclassProc(window, message, wparam, lparam);
    if (self && self->ready && activate_edit_row >= 0) {
        try {
            self->BeginEdit(activate_edit_row, std::to_underlying(ColumnId::Code));
        } catch (const std::exception&) {
            QIQI_LOG_ERROR("DictManager edit icon operation failed with an exception");
            ::SetWindowTextW(self->Child(Status), self->Text(L"编辑操作失败，请重试。", L"Edit operation failed; please retry."));
        }
    }
    if (self && self->ready && (message == WM_HSCROLL || message == WM_VSCROLL ||
            message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_SIZE ||
            message == WM_NOTIFY || message == LVM_SCROLL || message == LVM_ENSUREVISIBLE)) {
        self->PositionEditor();
    }
    return result;
}

LRESULT CALLBACK DictManagerWindow::Impl::EditorProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, EditorProcedure, subclass);
    } else if (self) {
        if (message == WM_IME_STARTCOMPOSITION) self->ime_composing = true;
        if (message == WM_IME_ENDCOMPOSITION) self->ime_composing = false;
        if (message == WM_GETDLGCODE && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            return ::DefSubclassProc(window, message, wparam, lparam) | DLGC_WANTMESSAGE;
        }
        if (message == WM_CHAR && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) return 0;
        if (message == WM_KEYDOWN && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            try {
                if (wparam == VK_RETURN) self->ExecuteAddOrUpdate();
                else self->CancelEditing();
            } catch (const std::exception&) {
                QIQI_LOG_ERROR("DictManager inline editor keyboard operation failed");
                ::SetWindowTextW(self->Child(Status), self->Text(L"编辑操作失败，请重试。", L"Edit operation failed; please retry."));
            }
            return 0;
        }
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK DictManagerWindow::Impl::Procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = BindOrGetWindowUserData<Impl>(window, message, lparam);
    if (!self || message == WM_NCCREATE) return ::DefWindowProcW(window, message, wparam, lparam);
    try {
        if (self->viewport.Handle(window, message, wparam, lparam, self->theme)) {
            self->Layout();
            if (message == WM_VSCROLL || message == WM_MOUSEWHEEL) return 0;
        }
        if (const auto handled = self->command_ui.HandleMessage(window, message, wparam, lparam, self->theme)) {
            return *handled;
        }
        if (const auto handled = HandleFluentToolWindowMessage(
                window, message, wparam, lparam, self->theme,
                0, layout::kMinimumWindowWidth, layout::kMinimumWindowHeight)) {
            return *handled;
        }
        switch (message) {
        case kToolConfigChangedMessage:
            self->config_subscription.RequestRefresh(window);
            return 0;
        case kToolConfigLoadedMessage:
            if (self->config_subscription.CompleteRefresh(window, wparam, lparam) && self->ready) self->Tick();
            return 0;
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_TIMER:
            if (self->ready) self->Tick();
            return 0;
        case WM_COMMAND:
            if (self->ready) self->Command(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_NOTIFY:
            if (self->ready && lparam) return self->Notify(*reinterpret_cast<NMHDR*>(lparam));
            return 0;
        case WM_CONTEXTMENU:
            if (self->ready && reinterpret_cast<HWND>(wparam) == self->Child(List)) {
                self->ShowListContextMenu(PointFromLParam(lparam));
                return 0;
            }
            break;
        case WM_DPICHANGED:
            if (lparam) self->UpdateDpi(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE:
            self->RecoverDisplay();
            return 0;
        case WM_SETTINGCHANGE:
            if (wparam == SPI_SETWORKAREA) self->RecoverDisplay();
            if (self->ready) self->RefreshDisplay(LoadConfig());
            [[fallthrough]];
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            self->UpdateTheme();
            return 0;
        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            ::KillTimer(window, kRefreshTimerId);
            ::PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (self->owner->window_.Get() == window) std::ignore = self->owner->window_.Detach();
            self->hwnd = nullptr;
            self->ready = false;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    } catch (const std::exception&) {
        QIQI_LOG_ERROR("DictManager window operation failed with an exception");
        ::SetWindowTextW(
            self->Child(Status),
            self->Text(L"操作发生异常，请重试。", L"Operation failed with an exception."));
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace qiqi::dict_manager
