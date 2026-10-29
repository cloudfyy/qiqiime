#include "ui/status_bar.h"
#include "common/guids.h"
#include "common/i18n.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>

namespace qiqi {

namespace {
constexpr int kLogoWidth = 48;
constexpr int kCellWidth = 32;
} // namespace

StatusBarUI::StatusBarUI(HINSTANCE instance, ActionCallback cb) noexcept
    : instance_(instance), action_callback_(std::move(cb)) {
    LOGFONTW lf{};
    lf.lfHeight = -15;
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
    font_.Reset(::CreateFontIndirectW(&lf));
}

StatusBarUI::~StatusBarUI() noexcept {
    DestroyBarWindow();
}

void StatusBarUI::Show(bool visible) noexcept {
    visible_ = visible;
    if (visible) {
        if (EnsureBarWindow()) {
            ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
            ::InvalidateRect(hwnd_, nullptr, TRUE);
        }
    } else if (hwnd_) {
        ::ShowWindow(hwnd_, SW_HIDE);
    }
}

void StatusBarUI::UpdateState(const LangBarState& state) noexcept {
    state_ = state;
    Show(state.show_status_bar);
    if (visible_ && hwnd_) {
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

bool StatusBarUI::EnsureBarWindow() noexcept {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        return true;
    }

    if (!pos_initialized_) {
        RECT work_rc{0, 0, 1280, 720};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_rc, 0);
        pos_.x = std::max<LONG>(work_rc.left, work_rc.right - kBarWidth - 24);
        pos_.y = std::max<LONG>(work_rc.top, work_rc.bottom - kBarHeight - 24);
        pos_initialized_ = true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &StatusBarUI::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_HAND);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kStatusBarWndClass;
    ::RegisterClassExW(&wc);

    hwnd_ = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kStatusBarWndClass,
        L"",
        WS_POPUP | WS_BORDER,
        pos_.x, pos_.y, kBarWidth, kBarHeight,
        nullptr, nullptr, instance_, this);
    return hwnd_ != nullptr;
}

void StatusBarUI::DestroyBarWindow() noexcept {
    if (hwnd_ && ::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    visible_ = false;
}

void StatusBarUI::PaintBar(HDC hdc, const RECT& client_rect) const {
    const ScopedGdiHandle<HBRUSH> bg_brush(::CreateSolidBrush(RGB(246, 248, 252)));
    ::FillRect(hdc, &client_rect, bg_brush.Get());

    const ScopedSelectObject sel_font(hdc, font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);

    const auto& str = GetUiStrings(state_.ui_language);

    // Logo area [0, kLogoWidth)
    RECT logo_rc{0, 0, kLogoWidth, client_rect.bottom};
    const ScopedGdiHandle<HBRUSH> logo_brush(::CreateSolidBrush(RGB(35, 105, 200)));
    ::FillRect(hdc, &logo_rc, logo_brush.Get());
    ::SetTextColor(hdc, RGB(255, 255, 255));
    ::DrawTextW(hdc, str.status_logo, -1, &logo_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 4 interactive cells: [中/英] [半/全] [。/.] [设]
    const std::array<const wchar_t*, 4> labels = {
        state_.chinese_mode ? str.status_mode_cn : str.status_mode_en,
        state_.full_width ? str.status_width_full : str.status_width_half,
        state_.chinese_punct ? L"。" : L".",
        str.status_settings
    };

    ::SetTextColor(hdc, RGB(30, 35, 45));
    for (size_t i = 0; i < labels.size(); ++i) {
        const int left = kLogoWidth + gsl::narrow_cast<int>(i) * kCellWidth;
        RECT cell_rc{left, 0, left + kCellWidth, client_rect.bottom};
        ::DrawTextW(hdc, gsl::at(labels, i), -1, &cell_rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void StatusBarUI::OnLeftButtonDown(POINT pt) const {
    if (!action_callback_ || pt.x < kLogoWidth) {
        return;
    }
    const int cell_idx = (pt.x - kLogoWidth) / kCellWidth;
    switch (cell_idx) {
        case 0: action_callback_(LangBarMenuId::ToggleChinese); break;
        case 1: action_callback_(LangBarMenuId::ToggleFullWidth); break;
        case 2: action_callback_(LangBarMenuId::TogglePunctuation); break;
        case 3: action_callback_(LangBarMenuId::OpenSettings); break;
        default: break;
    }
}

LRESULT CALLBACK StatusBarUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    const auto* self = reinterpret_cast<const StatusBarUI*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    if (msg == WM_MOUSEACTIVATE) {
        return MA_NOACTIVATE;
    }
    if (msg == WM_NCHITTEST) {
        POINT pt = PointFromLParam(lparam);
        ::ScreenToClient(hwnd, &pt);
        if (pt.x >= 0 && pt.x < kLogoWidth) {
            return HTCAPTION; // Allow dragging by the "五笔" logo area
        }
        return HTCLIENT;
    }
    if (msg == WM_LBUTTONDOWN) {
        self->OnLeftButtonDown(PointFromLParam(lparam));
        return 0;
    }
    if (msg == WM_PAINT) {
        const ScopedPaintDC paint_dc(hwnd);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        self->PaintBar(paint_dc.Get(), rc);
        return 0;
    }

    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace qiqi
