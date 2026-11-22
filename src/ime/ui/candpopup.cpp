// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/logger.h"
#include <gsl/gsl>
#include <dwmapi.h>
#include <utility>

namespace qiqi {
namespace {
void UnbindCandidateWindow(HWND hwnd) noexcept {
    const ScopedLastErrorRestore preserve_error;
    ::SetLastError(ERROR_SUCCESS);
    if (::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0) == 0) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_SUCCESS) {
            QIQI_LOG_WARN("CandidateUI: clearing popup user data failed (error={})", error);
        }
    }
}
} // namespace

STDMETHODIMP_(ULONG) CandidateUI::Release() noexcept {
    return ReleaseWithFinalCleanup([this]() noexcept {
        if (!DestroyPopupWindow() && hwnd_) {
            // Even if native destruction failed, don't leave a window bound to
            // this object or run the ScopedWindow fallback at refcount zero.
            UnbindCandidateWindow(hwnd_.Get());
            hwnd_.Reset();
        }
    });
}

STDMETHODIMP CandidateUI::Show(BOOL bShow) noexcept {
    const ComPtr<CandidateUI> keep_alive(this);
    const bool blocked =
        bShow && (!presentation_enabled_ || destroying_window_ ||
                  searchbox_integration_);
    if (blocked) {
        show_requested_ = false;
        is_shown_ = FALSE;
        if (hwnd_) {
            CloseAccessibilityMenu();
            ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
        return S_FALSE;
    }
    show_requested_ = bShow != FALSE;
    is_shown_ = FALSE;
    if (bShow) {
        if (EnsurePopupWindow()) {
            is_shown_ = TRUE;
            if (!::IsWindowVisible(hwnd_.Get())) {
                ::ShowWindow(hwnd_.Get(), SW_SHOWNOACTIVATE);
                ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
            }
            LogPopupVisibility();
            if (!accessibility_open_announced_) {
                accessibility_open_announced_ = true;
                std::ignore = RaiseAccessibilityEvent(UIA_MenuOpenedEventId);
                if (AccessibleCandidateCount() > 0) {
                    std::ignore = RaiseAccessibilityEvent(
                        UIA_SelectionItem_ElementSelectedEventId, SelectedPageIndex());
                }
            }
        } else {
            return S_FALSE;
        }
    } else if (hwnd_) {
        CloseAccessibilityMenu();
        ::ShowWindow(hwnd_.Get(), SW_HIDE);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* pbShow) noexcept {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = is_shown_;
    return S_OK;
}


void CandidateUI::MoveWindowTo(
    const RECT& text_rect, HWND host_window, bool require_owned_window) {
    const ComPtr<CandidateUI> keep_alive(this);
    const HWND owner = (host_window && ::IsWindow(host_window))
        ? ::GetAncestor(host_window, GA_ROOT)
        : nullptr;
    if (require_owned_window && (!host_window || !::IsWindow(host_window) || !owner || !::IsWindow(owner))) {
        QIQI_LOG_WARN(
            "CandidateUI::MoveWindowTo owner unavailable (require_owned={}, host_valid={}, root_valid={}); presentation deferred",
            require_owned_window, host_window && ::IsWindow(host_window), owner && ::IsWindow(owner));
    }
    require_owned_window_ = require_owned_window;
    if (require_owned_window && (!owner || !::IsWindow(owner))) {
        const bool retry = show_requested_;
        if (hwnd_) {
            std::ignore = ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
        is_shown_ = FALSE;
        if (DestroyPopupWindow()) {
            show_requested_ = retry;
        }
        popup_owner_ = nullptr;
        text_rect_ = text_rect;
        RefreshPopupLayout();
        return;
    }
    if (owner != popup_owner_ && hwnd_) {
        // Establish the host relationship at creation, including for immersive hosts.
        const bool retry = show_requested_;
        if (!DestroyPopupWindow()) return;
        show_requested_ = retry;
    }
    popup_owner_ = owner;
    text_rect_ = text_rect;
    RefreshPopupLayout();
    if (show_requested_ && !is_shown_) {
        std::ignore = Show(TRUE);
    }
}


bool CandidateUI::EnsurePopupWindow() noexcept {
    if (!presentation_enabled_ || destroying_window_) return false;
    if (hwnd_ && ::IsWindow(hwnd_.Get())) {
        if (require_owned_window_ && (!popup_owner_ || !::IsWindow(popup_owner_))) {
            QIQI_LOG_WARN(
                "CandidateUI::EnsurePopupWindow existing popup owner is invalid; presentation deferred");
            return false;
        }
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &CandidateUI::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCandidateWndClass;
    ::RegisterClassExW(&wc);

    if (!popup_owner_ && require_owned_window_) {
        QIQI_LOG_WARN("CandidateUI::EnsurePopupWindow owner missing; presentation deferred");
        return false;
    }
    if (popup_owner_ && !::IsWindow(popup_owner_)) {
        QIQI_LOG_WARN(
            "CandidateUI::EnsurePopupWindow owner became invalid before creation; presentation deferred");
        return false;
    }
    hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandidateWndClass,
        L"",
        WS_POPUP | (content_height_ > popup_height_ ? WS_VSCROLL : 0),
        popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
        popup_owner_, nullptr, instance_, this));

    if (!hwnd_) {
        QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
    } else {
        ApplyDisplayThemeToWindow(hwnd_.Get(), appearance_);
        UpdateScrollBar();
        QIQI_LOG_DEBUG("CandidateUI popup window created (pos=({}, {}), size={}x{}, dpi={})",
                       popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_);
    }
    return hwnd_.IsValid();
}

void CandidateUI::NotifyPopupWindowState(HWND window, bool destroyed) noexcept {
    DWORD event = EVENT_OBJECT_IME_HIDE;
    if (destroyed || !::IsWindowVisible(window)) {
        if (!announced_popup_rect_) return;
        announced_popup_rect_.reset();
    } else {
        RECT rect{};
        if (!::GetWindowRect(window, &rect)) {
            QIQI_LOG_ERROR("CandidateUI::NotifyPopupWindowState GetWindowRect failed (error={})",
                           ::GetLastError());
            return;
        }
        if (!announced_popup_rect_) {
            event = EVENT_OBJECT_IME_SHOW;
        } else if (!::EqualRect(&rect, &*announced_popup_rect_)) {
            event = EVENT_OBJECT_IME_CHANGE;
        } else {
            return;
        }
        announced_popup_rect_ = rect;
    }
    // In-context WinEvent clients may reenter before NotifyWinEvent returns.
    QIQI_LOG_CAT_DEBUG(log::Keyword::UI, "CandidateImeEvent(event=0x{:04x}, object={}, child={})",
                       event, OBJID_CLIENT, CHILDID_SELF);
    ::NotifyWinEvent(event, window, OBJID_CLIENT, CHILDID_SELF);
}

void CandidateUI::LogPopupVisibility() const noexcept {
    if (!log::ShouldLog(log::Level::debug) || !hwnd_) return;
    WINDOWINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetWindowInfo(hwnd_.Get(), &info)) {
        QIQI_LOG_ERROR("CandidateUI::LogPopupVisibility GetWindowInfo failed (error={})", ::GetLastError());
        return;
    }
    DWORD cloaked = 0;
    const HRESULT cloak_hr = ::DwmGetWindowAttribute(hwnd_.Get(), DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    std::ignore = log::CheckHResult(cloak_hr, "CandidateUI::DwmGetWindowAttribute(DWMWA_CLOAKED)", log::Level::debug);
    QIQI_LOG_DEBUG("CandidateVisibility(visible={}, topmost={}, has_owner={}, cloaked={}, cloak_hr=0x{:08x}, rect=({}, {}, {}, {}))",
                   ::IsWindowVisible(hwnd_.Get()) != FALSE, (info.dwExStyle & WS_EX_TOPMOST) != 0,
                   ::GetWindow(hwnd_.Get(), GW_OWNER) != nullptr,
                   SUCCEEDED(cloak_hr) ? gsl::narrow_cast<int>(cloaked) : -1,
                   gsl::narrow_cast<unsigned int>(cloak_hr),
                   info.rcWindow.left, info.rcWindow.top, info.rcWindow.right, info.rcWindow.bottom);
}

void CandidateUI::CloseAccessibilityMenu() noexcept {
    if (std::exchange(accessibility_open_announced_, false)) {
        std::ignore = RaiseAccessibilityEvent(UIA_MenuClosedEventId);
    }
}

void CandidateUI::DisconnectAccessibilityProvider() noexcept {
    if (accessibility_provider_needs_disconnect_ &&
        log::CheckHResult(::UiaDisconnectProvider(this),
                          "CandidateUI::UiaDisconnectProvider")) {
        accessibility_provider_needs_disconnect_ = false;
    }
}

bool CandidateUI::DestroyPopupWindow() noexcept {
    if (destroying_window_) return true;
    if (!CheckWindowOwnerThread(hwnd_.Get(), "CandidateUI::DestroyPopupWindow")) return false;
    const ComPtr<CandidateUI> keep_alive(this);
    destroying_window_ = true;
    const auto finish = gsl::finally([this]() noexcept { destroying_window_ = false; });
    CloseAccessibilityMenu();
    is_shown_ = FALSE;
    show_requested_ = false;
    DisconnectAccessibilityProvider();
    return DestroyWindowOnOwnerThread(hwnd_, "CandidateUI::DestroyPopupWindow");
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = BindOrGetWindowUserData<CandidateUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    const ComPtr<CandidateUI> keep_alive(self);

    switch (msg) {
        case WM_GETOBJECT:
            if (lparam == UiaRootObjectId && self->hwnd_.Get() == hwnd &&
                !self->destroying_window_) {
                self->accessibility_provider_needs_disconnect_ = true;
                return ::UiaReturnRawElementProvider(hwnd, wparam, lparam, self);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_NCDESTROY: {
            const bool already_destroying = std::exchange(self->destroying_window_, true);
            const auto finish = gsl::finally([self, already_destroying]() noexcept {
                self->destroying_window_ = already_destroying;
            });
            UnbindCandidateWindow(hwnd);
            if (self->hwnd_.Get() == hwnd) {
                self->CloseAccessibilityMenu();
                if (!already_destroying) self->DisconnectAccessibilityProvider();
                std::ignore = self->hwnd_.Detach();
                self->is_shown_ = FALSE;
                self->popup_owner_ = nullptr;
                // Clear the map for this HWND even if UIA only saw raised events.
                std::ignore = ::UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
                self->NotifyPopupWindowState(hwnd, true);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_WINDOWPOSCHANGED: {
            const LRESULT result = ::DefWindowProcW(hwnd, msg, wparam, lparam);
            if (self->hwnd_.Get() == hwnd) self->NotifyPopupWindowState(hwnd);
            return result;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            self->OnLeftButtonDown(PointFromLParam(lparam));
            return 0;
        case WM_MOUSEWHEEL: {
            self->wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
            const int steps = self->wheel_delta_ / WHEEL_DELTA;
            self->wheel_delta_ %= WHEEL_DELTA;
            self->ScrollContent(self->scroll_offset_ - steps * self->line_height_ * 3);
            return 0;
        }
        case WM_VSCROLL: {
            int position = self->scroll_offset_;
            switch (LOWORD(wparam)) {
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = self->content_height_; break;
                case SB_LINEUP: position -= self->line_height_; break;
                case SB_LINEDOWN: position += self->line_height_; break;
                case SB_PAGEUP: position -= self->popup_height_; break;
                case SB_PAGEDOWN: position += self->popup_height_; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    SCROLLINFO info{};
                    info.cbSize = sizeof(info);
                    info.fMask = SIF_TRACKPOS;
                    if (::GetScrollInfo(hwnd, SB_VERT, &info)) position = info.nTrackPos;
                    break;
                }
                default: break;
            }
            self->ScrollContent(position);
            return 0;
        }
        case WM_DPICHANGED:
            if (!self->text_rect_ && lparam != 0) {
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                self->popup_pos_.x = suggested->left;
                self->popup_pos_.y = suggested->top;
            }
            self->RefreshPopupLayout(HIWORD(wparam));
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            self->RefreshAppearance();
            self->RefreshPopupLayout();
            ApplyDisplayThemeToWindow(hwnd, self->appearance_);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [self](HDC hdc, const RECT& rc) { self->PaintPopup(hdc, rc); });
        case WM_PRINTCLIENT: {
            RECT rect{};
            if (wparam && ::GetClientRect(hwnd, &rect)) self->PaintPopup(reinterpret_cast<HDC>(wparam), rect);
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}


} // namespace qiqi
