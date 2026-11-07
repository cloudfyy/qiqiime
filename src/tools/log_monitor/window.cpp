#include "tools/log_monitor/internal.h"
#include "tools/log_monitor/permissions.h"
#include "common/theme.h"
#include "common/window.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <format>
#include <system_error>

namespace qiqi::monitor {
using enum ControlId;

void MonitorWindow::Impl::Failure(std::wstring_view operation, ULONG error) {
    diagnostics.Record(2, operation, error);
    const auto message = std::format(L"{}\r\n{}={} (0x{:08X})", operation, Text(L"错误码", L"Error"), error, error);
    ::MessageBoxW(hwnd, message.c_str(), Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), MB_OK | MB_ICONERROR);
}

LRESULT CALLBACK MonitorWindow::Impl::Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = BindOrGetWindowUserData<Impl>(window, message, lparam);
    if (!self || message == WM_NCCREATE) return ::DefWindowProcW(window, message, wparam, lparam);
    try {
        if (const auto handled = self->command_ui.HandleMessage(window, message, wparam, lparam, self->font.Get(), self->theme)) {
            return *handled;
        }
        if (const auto handled = HandleFluentToolWindowMessage(
                window, message, wparam, lparam, self->theme,
                0, 760, 560)) {
            return *handled;
        }
        switch (message) {
        case WM_SIZE: self->Layout(); return 0;
        case WM_TIMER: if (self->ready) self->Tick(); return 0;
        case WM_COMMAND: if (self->ready) self->Command(LOWORD(wparam), HIWORD(wparam)); return 0;
        case WM_NOTIFY: if (self->ready && lparam) self->Notify(*reinterpret_cast<NMHDR*>(lparam)); return 0;
        case WM_CONTEXTMENU:
            if (self->ready && reinterpret_cast<HWND>(wparam) == self->Child(List)) {
                self->ShowListContextMenu(PointFromLParam(lparam));
                return 0;
            }
            break;
        case WM_DPICHANGED: {
            if (lparam) self->UpdateDpi(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
            return 0;
        }
        case WM_DISPLAYCHANGE:
            self->RecoverDisplay();
            return 0;
        case WM_SETTINGCHANGE:
            if (wparam == SPI_SETWORKAREA) self->RecoverDisplay();
            if (self->ready && self->ui_language == UiLanguage::Auto) self->ApplyLanguage();
            [[fallthrough]];
        case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: case WM_DWMCOLORIZATIONCOLORCHANGED:
            self->UpdateTheme();
            return 0;
        case WM_CLOSE:
            if (!self->collector.Stop()) { self->Tick(); return 0; }
            ::DestroyWindow(window);
            return 0;
        case WM_DESTROY: ::KillTimer(window, 1); ::PostQuitMessage(0); return 0;
        case WM_NCDESTROY:
            if (self->owner->window_.Get() == window) std::ignore = self->owner->window_.Detach();
            self->hwnd = nullptr;
            self->ready = false;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default: break;
        }
    } catch (const std::system_error& error) {
        self->diagnostics.Record(2, L"GUI/export system error", gsl::narrow_cast<ULONG>(error.code().value()));
        ::SetWindowTextW(self->Child(Status), self->Text(L"操作失败，请查看工具诊断；内存日志仍保留。", L"Operation failed; see diagnostics. Buffered records retained."));
    } catch (const std::exception&) {
        self->diagnostics.Record(2, L"GUI operation exception", ERROR_UNHANDLED_EXCEPTION);
        ::SetWindowTextW(self->Child(Status), self->Text(L"操作失败，请查看工具诊断；内存日志仍保留。", L"Operation failed; see diagnostics. Buffered records retained."));
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

#pragma warning(suppress : 26455) // Allocation failures reach the executable's startup error dialog.
MonitorWindow::MonitorWindow() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
MonitorWindow::~MonitorWindow() noexcept = default;

bool MonitorWindow::Create(HINSTANCE instance) {
    if (window_) return true;
    impl_->instance = instance;
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!::InitCommonControlsEx(&controls)) {
        impl_->Failure(L"InitCommonControlsEx", ERROR_DLL_INIT_FAILED);
        return false;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = &Impl::Procedure;
    cls.lpszClassName = kWindowClass.data();
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!::RegisterClassExW(&cls)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            impl_->Failure(L"RegisterClassEx", error);
            return false;
        }
    }
    const auto title = FormatMonitorWindowTitle(impl_->ui_language);
    const auto monitor = QueryActiveMonitor(nullptr);
    const auto rect = ComputeCenteredToolWindowRect(monitor, 1160, 780, monitor.dpi);
    window_.Reset(::CreateWindowExW(0, kWindowClass.data(), title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, impl_.get()));
    if (!window_) {
        impl_->Failure(L"CreateWindowEx", ::GetLastError());
        return false;
    }
    impl_->hwnd = window_.Get();
    if (!impl_->CreateControls()) return false;
    impl_->UpdateDpi(impl_->theme.dpi, ComputeCenteredToolWindowRect(monitor, 1160, 780, impl_->theme.dpi));
    impl_->Layout();
    impl_->Tick();
    return true;
}

int RunMonitor(HINSTANCE instance, int show) {
    MonitorWindow window;
    if (!window.Create(instance)) return 1;
    return RunToolWindowMessageLoop(window.Handle(), show);
}

} // namespace qiqi::monitor
