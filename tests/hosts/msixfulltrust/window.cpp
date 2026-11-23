// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "window.h"

#include "searchprobe.h"

#include <commctrl.h>
#include <richedit.h>
#include <windowsx.h>

#include <algorithm>
#include <format>
#include <new>
#include <string>

namespace qiqi::host::fulltrust {
namespace {

constexpr wchar_t kWindowClass[] =
    L"QiQiImePackagedFullTrustHostWindowClass";

enum ControlId : int {
    InputId = 100,
    RefreshId = 101,
    SeedShadowId = 102,
    SearchProviderId = 103,
    PackageStatusId = 104,
    RegistryStatusId = 105,
    SearchStatusId = 106,
    DiagnosticStatusId = 107,
};

constexpr int kMargin = 20;
constexpr int kButtonHeight = 34;
constexpr int kButtonWidth = 180;
constexpr int kButtonGap = 12;

HFONT CreateUiFont() noexcept {
    return ::CreateFontW(
        20,
        0,
        0,
        0,
        FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI");
}

void SetControlFont(HWND control, HFONT font) noexcept {
    if (control) {
        ::SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE);
    }
}

} // namespace

HostWindow::HostWindow(
    HINSTANCE instance,
    Diagnostics& diagnostics,
    bool seed_shadow_on_start) noexcept
    : instance_(instance),
      diagnostics_(diagnostics),
      seed_shadow_on_start_(seed_shadow_on_start) {}

HostWindow::~HostWindow() noexcept {
    if (font_) {
        ::DeleteObject(font_);
        font_ = nullptr;
    }
    if (rich_edit_) {
        ::FreeLibrary(rich_edit_);
        rich_edit_ = nullptr;
    }
}

HRESULT HostWindow::CreateAndRun() noexcept {
    INITCOMMONCONTROLSEX controls{
        .dwSize = sizeof(controls),
        .dwICC = ICC_STANDARD_CLASSES,
    };
    if (!::InitCommonControlsEx(&controls)) {
        diagnostics_.Warning(
            L"host.ui",
            std::format(
                L"InitCommonControlsEx failed error={}",
                ::GetLastError()));
    }

    WNDCLASSEXW window_class{
        .cbSize = sizeof(window_class),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = StaticWndProc,
        .cbWndExtra = sizeof(void*),
        .hInstance = instance_,
        .hCursor = ::LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClass,
    };
    if (!::RegisterClassExW(&window_class)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            diagnostics_.Error(
                L"host.ui",
                std::format(
                    L"RegisterClassExW failed error={}",
                    error));
            return HRESULT_FROM_WIN32(error);
        }
    }

    window_ = ::CreateWindowExW(
        0,
        kWindowClass,
        L"QiQiIME Packaged Full-Trust Host",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1020,
        760,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!window_) {
        const DWORD error =
            ::GetLastError() == ERROR_SUCCESS
                ? ERROR_GEN_FAILURE
                : ::GetLastError();
        diagnostics_.Error(
            L"host.ui",
            std::format(
                L"CreateWindowExW failed error={}",
                error));
        return HRESULT_FROM_WIN32(error);
    }

    ::ShowWindow(window_, SW_SHOW);
    ::UpdateWindow(window_);
    diagnostics_.Info(L"host.ui", L"main window created");

    MSG message{};
    while (const BOOL result = ::GetMessageW(
               &message,
               nullptr,
               0,
               0)) {
        if (result == -1) {
            const DWORD error = ::GetLastError();
            diagnostics_.Error(
                L"host.ui",
                std::format(
                    L"GetMessageW failed error={}",
                    error));
            return HRESULT_FROM_WIN32(
                error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error);
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return S_OK;
}

LRESULT CALLBACK HostWindow::StaticWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept {
    HostWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        const auto* create =
            reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<HostWindow*>(
            create->lpCreateParams);
        ::SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->window_ = hwnd;
        }
    } else {
        self = reinterpret_cast<HostWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self
        ? self->WndProc(hwnd, message, wparam, lparam)
        : ::DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT HostWindow::WndProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) noexcept {
    switch (message) {
    case WM_CREATE:
        if (!CreateControls()) {
            return -1;
        }
        return 0;
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        ::SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(
            ::GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_COMMAND: {
        const UINT command = LOWORD(wparam);
        const UINT notification = HIWORD(wparam);
        const HWND source =
            reinterpret_cast<HWND>(lparam);
        if (source == input_) {
            switch (notification) {
            case EN_CHANGE:
                LogInputEvent(L"changed");
                break;
            case EN_SETFOCUS:
                LogInputEvent(L"focus-gained");
                break;
            case EN_KILLFOCUS:
                LogInputEvent(L"focus-lost");
                break;
            default:
                break;
            }
        } else {
            switch (command) {
            case RefreshId:
                RefreshState();
                break;
            case SeedShadowId:
                SeedShadow();
                break;
            case SearchProviderId:
                RunSearchProbe();
                break;
            default:
                break;
            }
        }
        return 0;
    }
    case WM_DESTROY:
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

bool HostWindow::CreateControls() noexcept {
    rich_edit_ = ::LoadLibraryW(L"msftedit.dll");
    if (!rich_edit_) {
        diagnostics_.Error(
            L"host.ui",
            std::format(
                L"LoadLibraryW(msftedit.dll) failed error={}",
                ::GetLastError()));
        return false;
    }
    font_ = CreateUiFont();
    if (!font_) {
        diagnostics_.Error(
            L"host.ui",
            std::format(
                L"CreateFontW failed error={}",
                ::GetLastError()));
        return false;
    }

    input_ = ::CreateWindowExW(
        WS_EX_CLIENTEDGE,
        MSFTEDIT_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL |
            ES_WANTRETURN | ES_LEFT,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(InputId)),
        instance_,
        nullptr);
    refresh_button_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Refresh state",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(RefreshId)),
        instance_,
        nullptr);
    seed_button_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Seed package-private shadow",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(SeedShadowId)),
        instance_,
        nullptr);
    search_button_ = ::CreateWindowExW(
        0,
        L"BUTTON",
        L"Probe TSF search provider",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(SearchProviderId)),
        instance_,
        nullptr);
    package_status_ = ::CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(PackageStatusId)),
        instance_,
        nullptr);
    registry_status_ = ::CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(RegistryStatusId)),
        instance_,
        nullptr);
    search_status_ = ::CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(SearchStatusId)),
        instance_,
        nullptr);
    diagnostic_status_ = ::CreateWindowExW(
        0,
        L"STATIC",
        L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0,
        0,
        0,
        0,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(DiagnosticStatusId)),
        instance_,
        nullptr);
    if (!input_ ||
        !refresh_button_ ||
        !seed_button_ ||
        !search_button_ ||
        !package_status_ ||
        !registry_status_ ||
        !search_status_ ||
        !diagnostic_status_) {
        diagnostics_.Error(
            L"host.ui",
            std::format(
                L"control creation failed error={}",
                ::GetLastError()));
        return false;
    }

    for (HWND control : {
             input_,
             refresh_button_,
             seed_button_,
             search_button_,
             package_status_,
             registry_status_,
             search_status_,
             diagnostic_status_,
         }) {
        SetControlFont(control, font_);
    }
    ::SetWindowTextW(
        input_,
        L"Type input here. Entered text and IME candidates are not logged.");
    ::SendMessageW(
        input_,
        EM_SETLIMITTEXT,
        4096,
        0);
    ::SetFocus(input_);

    LayoutControls();
    RefreshState();
    if (seed_shadow_on_start_) {
        SeedShadow();
    }
    RunSearchProbe();
    return true;
}

void HostWindow::LayoutControls() noexcept {
    if (!window_) {
        return;
    }
    RECT client{};
    ::GetClientRect(window_, &client);
    const int width =
        std::max(0, static_cast<int>(client.right - client.left));
    const int height =
        std::max(0, static_cast<int>(client.bottom - client.top));
    const int content_width = std::max(0, width - 2 * kMargin);
    const int edit_top = kMargin + 54;
    const int edit_height =
        std::max(120, std::min(190, height / 4));
    MoveControl(
        input_,
        {
            kMargin,
            edit_top,
            kMargin + content_width,
            edit_top + edit_height,
        });

    const int button_top =
        edit_top + edit_height + kButtonGap;
    int button_left = kMargin;
    MoveControl(
        refresh_button_,
        {
            button_left,
            button_top,
            button_left + kButtonWidth,
            button_top + kButtonHeight,
        });
    button_left += kButtonWidth + kButtonGap;
    MoveControl(
        seed_button_,
        {
            button_left,
            button_top,
            button_left + 250,
            button_top + kButtonHeight,
        });
    button_left += 250 + kButtonGap;
    MoveControl(
        search_button_,
        {
            button_left,
            button_top,
            button_left + 250,
            button_top + kButtonHeight,
        });

    const int status_top =
        button_top + kButtonHeight + 16;
    const int remaining =
        std::max(0, height - status_top - kMargin);
    const int column_gap = 18;
    const int column_width =
        std::max(0, (content_width - column_gap) / 2);
    const int half_height =
        std::max(0, (remaining - 12) / 2);
    MoveControl(
        package_status_,
        {
            kMargin,
            status_top,
            kMargin + column_width,
            status_top + half_height,
        });
    MoveControl(
        registry_status_,
        {
            kMargin + column_width + column_gap,
            status_top,
            width - kMargin,
            height - kMargin,
        });
    MoveControl(
        search_status_,
        {
            kMargin,
            status_top + half_height + 12,
            kMargin + column_width,
            height - kMargin - 60,
        });
    MoveControl(
        diagnostic_status_,
        {
            kMargin,
            height - kMargin - 52,
            width - kMargin,
            height - kMargin,
        });
}

void HostWindow::MoveControl(
    HWND control,
    const RECT& rect) noexcept {
    if (!control) {
        return;
    }
    ::MoveWindow(
        control,
        rect.left,
        rect.top,
        std::max<int>(
            0,
            static_cast<int>(rect.right - rect.left)),
        std::max<int>(
            0,
            static_cast<int>(rect.bottom - rect.top)),
        TRUE);
}

void HostWindow::RefreshState() noexcept {
    package_state_ = ReadPackageState();
    RefreshPackageAndRegistry();
    UpdateDiagnosticStatus();
}

void HostWindow::RefreshPackageAndRegistry() noexcept {
    const RegistryState registry = ReadQiQiRegistryState();
    try {
        ::SetWindowTextW(
            package_status_,
            FormatPackageState(package_state_).c_str());
        ::SetWindowTextW(
            registry_status_,
            FormatRegistryState(registry).c_str());
        diagnostics_.Info(
            L"host.package",
            std::format(
                L"package={}, app_container={}, integrity={}, sequence={}, "
                L"skin={}, marker={}",
                package_state_.has_package_identity ? L"true" : L"false",
                package_state_.is_app_container ? L"true" : L"false",
                IntegrityLabel(package_state_.integrity_rid),
                registry.config_sequence
                    ? std::to_wstring(*registry.config_sequence)
                    : L"<missing>",
                registry.skin_id ? *registry.skin_id : L"<missing>",
                registry.shadow_marker_present ? L"true" : L"false"));
    } catch (const std::bad_alloc&) {
        diagnostics_.Error(
            L"host.state",
            L"state formatting allocation failed");
    }
}

void HostWindow::SeedShadow() noexcept {
    package_state_ = ReadPackageState();
    const ShadowSeedResult result =
        SeedPackagePrivateQiQiShadow(package_state_);
    try {
        const std::wstring detail = std::format(
            L"seed_hr=0x{:08X}, previous_sequence={}, "
            L"shadow_sequence={}, previous_skin={}, shadow_skin={}",
            static_cast<uint32_t>(result.hr),
            result.previous_sequence,
            result.shadow_sequence,
            result.previous_skin.empty()
                ? L"<missing>"
                : result.previous_skin,
            result.shadow_skin);
        if (SUCCEEDED(result.hr)) {
            diagnostics_.Info(L"host.shadow", detail);
        } else {
            diagnostics_.Error(L"host.shadow", detail);
        }
    } catch (const std::bad_alloc&) {
        diagnostics_.Error(
            L"host.shadow",
            L"shadow result formatting allocation failed");
    }
    RefreshPackageAndRegistry();
    UpdateDiagnosticStatus();
}

void HostWindow::RunSearchProbe() noexcept {
    const SearchProbeResult result =
        ProbeSearchCandidateProvider();
    try {
        ::SetWindowTextW(
            search_status_,
            FormatSearchProbe(result).c_str());
        diagnostics_.Info(
            L"host.search-provider",
            std::format(
                L"activate_hr=0x{:08X}, provider_hr=0x{:08X}, "
                L"function_hr=0x{:08X}, candidates_hr=0x{:08X}, count={}",
                static_cast<uint32_t>(result.activate_hr),
                static_cast<uint32_t>(result.provider_hr),
                static_cast<uint32_t>(result.function_hr),
                static_cast<uint32_t>(result.candidates_hr),
                result.candidate_count),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_.Error(
            L"host.search-provider",
            L"search probe formatting allocation failed",
            DiagnosticArea::Input);
    }
}

void HostWindow::UpdateDiagnosticStatus() noexcept {
    try {
        ::SetWindowTextW(
            diagnostic_status_,
            std::format(
                L"ETW registration error: {}  |  Persistent log: {}  |  "
                L"Persistent error: {}",
                diagnostics_.RegistrationError(),
                diagnostics_.PersistentLogFileName(),
                diagnostics_.PersistentError()).c_str());
    } catch (const std::bad_alloc&) {
        ::SetWindowTextW(
            diagnostic_status_,
            L"Diagnostic status formatting allocation failed.");
    }
}

void HostWindow::LogInputEvent(
    std::wstring_view stage) noexcept {
    const int length =
        input_ ? ::GetWindowTextLengthW(input_) : -1;
    try {
        diagnostics_.Info(
            L"host.input",
            std::format(
                L"stage={}, text_length={}",
                stage,
                length),
            DiagnosticArea::Input);
    } catch (const std::bad_alloc&) {
        diagnostics_.Error(
            L"host.input",
            L"input event formatting allocation failed",
            DiagnosticArea::Input);
    }
}

} // namespace qiqi::host::fulltrust

