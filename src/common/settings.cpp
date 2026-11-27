// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/settings.h"

#include "common/comobj.h"
#include "common/controls.h"
#include "common/dlgutil.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"

#include <shellapi.h>
#include <array>
#include <filesystem>
#include <memory>
#include <new>
#include <string>
#include <system_error>

namespace qiqi {
namespace {

constexpr int kToastWidth = 440;
constexpr int kToastHeight = 88;
constexpr int kToastWorkAreaInset = 20;
constexpr UINT_PTR kToastTimerId = 1;
constexpr UINT kToastDurationMs = 3000;
constexpr int kToastCardInset = 8;
constexpr RECT kToastTextInsets{34, 18, 24, 18};
constexpr UINT kUpdateToastLanguageMessage = WM_APP + 1;
constexpr std::wstring_view kSettingsExecutable =
    L"QiQiIMESettings.exe";
constexpr std::wstring_view kSettingsProtocol =
    L"qiqiime-settings:";

thread_local SettingsLauncherForTest settings_launcher_for_test =
    nullptr;

struct ToastContext {
    UiLanguage language = UiLanguage::Auto;
    SettingsToastKind kind = SettingsToastKind::SaveFailure;
    FluentD2DThemeContext d2d;
    bool holds_module_reference = false;
};

const wchar_t* GetToastText(
    UiLanguage language, SettingsToastKind kind) noexcept {
    const auto& str = GetUiStrings(language);
    switch (kind) {
        case SettingsToastKind::SaveFailure:
            return str.quick_settings_save_failed;
        case SettingsToastKind::NotificationFailure:
            return str.quick_settings_notify_failed;
        case SettingsToastKind::LaunchFailure:
            return str.quick_settings_open_failed;
        case SettingsToastKind::FullCodeUnavailable:
            return str.dlg_full_code_unavailable;
        default:
            QIQI_LOG_ERROR("Unknown Settings toast kind");
            return nullptr;
    }
}

void ApplyToastLanguage(
    HWND hwnd, ToastContext& context, UiLanguage language) noexcept {
    context.language = language;
    LOGFONTW face{};
    if (TryGetLocalizedUiFont(language, face)) {
        context.d2d.UpdateAppearance(context.d2d.dpi, context.d2d.display_preferences, face);
    }
    ::SetWindowTextW(
        hwnd, GetToastText(context.language, context.kind));
    ::InvalidateRect(hwnd, nullptr, TRUE);
}

void PaintToast(
    HDC dc, const RECT& bounds, const ToastContext& context) noexcept {
    if (!context.d2d.d2d_target) return;
    const ScopedD2DDcDraw draw(
        context.d2d.d2d_target.Get(), dc, bounds);
    if (!draw) return;
    const bool dark = IsSystemDarkTheme();
    const auto scale = [&context](int px) noexcept {
        return context.d2d.ScaleF(px);
    };
    context.d2d.d2d_target->Clear(
        ColorRefToD2DColor(
            dark ? kDarkWindowBg : kLightCaptionBg));
    const std::array<FluentCardSpec, 1> cards{{
        {D2D1::RectF(
             scale(kToastCardInset),
             scale(kToastCardInset),
             static_cast<float>(bounds.right) -
                 scale(kToastCardInset),
             static_cast<float>(bounds.bottom) -
                 scale(kToastCardInset)),
         nullptr,
         nullptr}
    }};
    DrawFluentCardGroup(
        context.d2d, cards, false, dark);
    const auto text_brush = CreateSolidBrush(
        context.d2d.d2d_target.Get(),
        ColorRefToD2DColor(
            dark ? kDarkTextColor : kLightTextColor));
    const wchar_t* const text =
        GetToastText(context.language, context.kind);
    if (context.d2d.fmt_body && text_brush && text) {
        context.d2d.d2d_target->DrawText(
            text,
            gsl::narrow_cast<UINT32>(std::wcslen(text)),
            context.d2d.fmt_body.Get(),
            D2D1::RectF(
                scale(kToastTextInsets.left),
                scale(kToastTextInsets.top),
                static_cast<float>(bounds.right) -
                    scale(kToastTextInsets.right),
                static_cast<float>(bounds.bottom) -
                    scale(kToastTextInsets.bottom)),
            text_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
}

LRESULT CALLBACK ToastWndProc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* context =
        BindOrGetWindowUserData<ToastContext>(
            hwnd, message, lparam);
    if (!context || message == WM_NCCREATE) {
        return ::DefWindowProcW(
            hwnd, message, wparam, lparam);
    }
    switch (message) {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_PAINT:
            return PaintWindowClient(
                hwnd,
                [context](HDC dc, const RECT& bounds) noexcept {
                    PaintToast(dc, bounds, *context);
                });
        case WM_PRINTCLIENT: {
            RECT bounds{};
            ::GetClientRect(hwnd, &bounds);
            PaintToast(
                reinterpret_cast<HDC>(wparam),
                bounds,
                *context);
            return 0;
        }
        case WM_THEMECHANGED:
            context->d2d.RefreshThemeBrushes();
            ::SetWindowTextW(
                hwnd,
                GetToastText(
                    context->language, context->kind));
            ::InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_SETTINGCHANGE:
            ApplyToastLanguage(hwnd, *context, context->language);
            return 0;
        case kUpdateToastLanguageMessage:
            if (const auto language = dialog_detail::DecodeDialogLanguage(wparam)) {
                ApplyToastLanguage(hwnd, *context, *language);
            }
            return 0;
        case WM_TIMER:
            if (wparam == kToastTimerId) {
                ::KillTimer(hwnd, kToastTimerId);
                ::DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_NCDESTROY: {
            ::SetWindowLongPtrW(
                hwnd, GWLP_USERDATA, 0);
            const std::unique_ptr<ToastContext> owned(context);
            if (owned->holds_module_reference) {
                --g_com_object_count;
            }
            return ::DefWindowProcW(
                hwnd, message, wparam, lparam);
        }
        default:
            break;
    }
    return ::DefWindowProcW(
        hwnd, message, wparam, lparam);
}

HRESULT LaunchSettingsProtocol(HWND owner) noexcept {
    const HINSTANCE result = ::ShellExecuteW(
        owner,
        L"open",
        std::data(kSettingsProtocol),
        nullptr,
        nullptr,
        SW_SHOWNORMAL);
    const INT_PTR code =
        reinterpret_cast<INT_PTR>(result);
    if (code > 32) return S_OK;
    QIQI_LOG_ERROR(
        "Settings URI launch failed (shell_code={})",
        code);
    switch (code) {
        case 0: return E_OUTOFMEMORY;
        case SE_ERR_FNF:
        case SE_ERR_PNF:
        case SE_ERR_ACCESSDENIED:
        case SE_ERR_OOM:
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(code));
        default:
            return E_FAIL;
    }
}

HRESULT LaunchSettingsExecutable(
    HINSTANCE module) noexcept {
    if (IsProcessAppContainer()) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }
    try {
        const std::filesystem::path module_path =
            GetModuleFilePath(module);
        if (module_path.empty()) {
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        const std::filesystem::path executable =
            module_path.parent_path() /
            std::filesystem::path(kSettingsExecutable);
        std::error_code error;
        const bool present =
            std::filesystem::exists(executable, error);
        if (error) {
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(
                    error.value()));
        }
        if (!present) {
            return HRESULT_FROM_WIN32(
                ERROR_FILE_NOT_FOUND);
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::wstring command =
            L"\"" + executable.wstring() + L"\"";
        if (!::CreateProcessW(
                executable.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                nullptr,
                &startup,
                &process)) {
            const DWORD failure = ::GetLastError();
            QIQI_LOG_ERROR(
                "Standalone Settings process creation failed "
                "(error={})",
                failure);
            return HRESULT_FROM_WIN32(failure);
        }
        const ScopedKernelHandle process_handle(
            process.hProcess);
        const ScopedKernelHandle thread_handle(
            process.hThread);
        ::AllowSetForegroundWindow(process.dwProcessId);
        QIQI_LOG_INFO(
            "Launched standalone Settings process from '{}'",
            WideToUtf8(executable.wstring()));
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "Standalone Settings launch ran out of memory");
        return E_OUTOFMEMORY;
    }
}

} // namespace

bool ActivateExistingSettingsWindow() noexcept {
    const HWND existing =
        ::FindWindowW(kSettingsWndClass, nullptr);
    if (!existing || !::IsWindow(existing)) return false;
    if (::IsIconic(existing)) {
        ::ShowWindow(existing, SW_RESTORE);
    }
    const HWND popup =
        ::GetLastActivePopup(existing);
    const HWND target =
        popup && ::IsWindow(popup) ? popup : existing;
    if (::IsIconic(target)) {
        ::ShowWindow(target, SW_RESTORE);
    }
    DWORD target_pid = 0;
    ::GetWindowThreadProcessId(target, &target_pid);
    if (target_pid != 0 &&
        target_pid != ::GetCurrentProcessId()) {
        ::AllowSetForegroundWindow(target_pid);
    }
    ::SetForegroundWindow(target);
    ::BringWindowToTop(target);
    if (target_pid == ::GetCurrentProcessId()) {
        ::SetActiveWindow(target);
    }
    return true;
}

void SetSettingsLauncherForTest(
    SettingsLauncherForTest launcher) noexcept {
    settings_launcher_for_test = launcher;
}

HRESULT OpenStandaloneSettings(
    HINSTANCE module,
    HWND owner,
    UiLanguage language) noexcept {
    if (settings_launcher_for_test) {
        const HRESULT result =
            settings_launcher_for_test(module, owner);
        if (FAILED(result)) {
            ShowSettingsToast(
                language,
                SettingsToastKind::LaunchFailure);
        }
        return result;
    }
    if (ActivateExistingSettingsWindow()) return S_OK;

    const HRESULT executable_result =
        LaunchSettingsExecutable(module);
    if (SUCCEEDED(executable_result)) return S_OK;
    const HRESULT protocol_result =
        LaunchSettingsProtocol(owner);
    if (SUCCEEDED(protocol_result)) return S_OK;

    QIQI_LOG_ERROR(
        "Standalone Settings launch failed "
        "(executable_hr=0x{:08X}, protocol_hr=0x{:08X})",
        gsl::narrow_cast<uint32_t>(executable_result),
        gsl::narrow_cast<uint32_t>(protocol_result));
    ShowSettingsToast(
        language,
        SettingsToastKind::LaunchFailure);
    return FAILED(executable_result)
        ? executable_result
        : protocol_result;
}

void ShowSettingsToast(
    UiLanguage language,
    SettingsToastKind kind) noexcept {
    const wchar_t* const text =
        GetToastText(language, kind);
    if (!text) return;
    const HINSTANCE instance =
        g_dll_instance
            ? g_dll_instance
            : ::GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &ToastWndProc;
    window_class.hInstance = instance;
    window_class.lpszClassName =
        kSettingsToastWndClass;
    if (!::RegisterClassExW(&window_class) &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        QIQI_LOG_ERROR(
            "Settings toast window registration failed "
            "(error={})",
            ::GetLastError());
        return;
    }

    std::unique_ptr<ToastContext> context;
    try {
        context = std::make_unique<ToastContext>();
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "Settings toast context allocation failed");
        return;
    }
    const MonitorMetrics monitor =
        QueryActiveMonitor();
    context->language = language;
    context->kind = kind;
    LOGFONTW face{};
    if (!TryGetLocalizedUiFont(language, face)) return;
    context->d2d.Init(monitor.dpi, &face);
    if (!context->d2d.d2d_target ||
        !context->d2d.fmt_body) {
        QIQI_LOG_ERROR(
            "Settings toast requires Direct2D and "
            "DirectWrite");
        return;
    }
    std::ignore = log::CheckHResult(
        context->d2d.fmt_body->SetWordWrapping(
            DWRITE_WORD_WRAPPING_WRAP),
        "SettingsToast::SetWordWrapping");
    std::ignore = log::CheckHResult(
        context->d2d.fmt_body->SetTextAlignment(
            DWRITE_TEXT_ALIGNMENT_LEADING),
        "SettingsToast::SetTextAlignment");
    std::ignore = log::CheckHResult(
        context->d2d.fmt_body->SetParagraphAlignment(
            DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
        "SettingsToast::SetParagraphAlignment");

    const int width =
        ScalePxByDpi(kToastWidth, monitor.dpi);
    const int height =
        ScalePxByDpi(kToastHeight, monitor.dpi);
    const POINT position = ClampPointToWorkArea(
        POINT{
            monitor.work_area.right - width -
                ScalePxByDpi(
                    kToastWorkAreaInset, monitor.dpi),
            monitor.work_area.bottom - height -
                ScalePxByDpi(
                    kToastWorkAreaInset, monitor.dpi)},
        width,
        height,
        monitor.work_area);
    ToastContext* const owned = context.get();
    ScopedWindow window(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE,
        kSettingsToastWndClass,
        text,
        WS_POPUP,
        position.x,
        position.y,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        owned));
    if (!window) {
        QIQI_LOG_ERROR(
            "Settings toast window creation failed "
            "(error={})",
            ::GetLastError());
        return;
    }
    std::ignore = context.release();
    if (!::SetTimer(
            window.Get(),
            kToastTimerId,
            kToastDurationMs,
            nullptr)) {
        QIQI_LOG_ERROR(
            "Settings toast timer creation failed "
            "(error={})",
            ::GetLastError());
        return;
    }
    owned->holds_module_reference = true;
    ++g_com_object_count;
    const HWND raw_window = window.Detach();
    ::ShowWindow(raw_window, SW_SHOWNOACTIVATE);
    ::UpdateWindow(raw_window);
}

void UpdateSettingsToastLanguage(
    UiLanguage language) noexcept {
    const auto callback =
        [](HWND hwnd, LPARAM language_param) noexcept -> BOOL {
            std::array<wchar_t, 128> class_name{};
            if (::GetClassNameW(
                    hwnd,
                    class_name.data(),
                    gsl::narrow_cast<int>(
                        class_name.size())) > 0 &&
                std::wcscmp(
                    class_name.data(),
                    kSettingsToastWndClass) == 0) {
                ::SendMessageW(
                    hwnd,
                    kUpdateToastLanguageMessage,
                    language_param,
                    0);
            }
            return TRUE;
        };
    if (!::EnumThreadWindows(
            ::GetCurrentThreadId(),
            callback,
            std::to_underlying(language))) {
        QIQI_LOG_WARN(
            "Enumerating Settings toast windows for "
            "language update failed (error={})",
            ::GetLastError());
    }
}

} // namespace qiqi
