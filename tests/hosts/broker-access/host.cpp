// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "brokerprobe.h"
#include "tokeninfo.h"

#include <commctrl.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_lpac_provider,
    "QiQiIME.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99));
#pragma warning(pop)

#ifndef QIQI_HOST_EXECUTABLE_NAME
#define QIQI_HOST_EXECUTABLE_NAME \
    L"QiQiImePermissionHost.exe"
#endif

#ifndef QIQI_HOST_DISPLAY_NAME
#define QIQI_HOST_DISPLAY_NAME \
    L"QiQiIME AppContainer Permission Host"
#endif

namespace qiqi::host::permissions {
namespace {

constexpr wchar_t kWindowClass[] =
    L"QiQiImeAppContainerPermissionWindowClass";
constexpr std::wstring_view kProcessName =
    QIQI_HOST_EXECUTABLE_NAME;
constexpr std::wstring_view kLogFileName =
    L"qiqi-appcontainer-permission-host.log";
constexpr uint64_t kGeneralKeyword = 0x0001;
constexpr uint64_t kInputKeyword = 0x0004;
constexpr UINT kInputId = 100;
constexpr UINT kRefreshId = 101;
constexpr UINT kRegistryId = 102;
constexpr UINT kTsfId = 103;
constexpr UINT kBrokerId = 104;
constexpr UINT kTokenStatusId = 110;
constexpr UINT kRegistryStatusId = 111;
constexpr UINT kTsfStatusId = 112;
constexpr UINT kBrokerStatusId = 113;
constexpr UINT kDiagnosticStatusId = 114;
constexpr int kMargin = 20;
constexpr int kButtonWidth = 180;
constexpr int kButtonHeight = 34;
constexpr int kButtonGap = 12;

using Microsoft::WRL::ComPtr;

template <uint8_t Level, uint64_t Keyword>
void EmitEtw(std::wstring_view message) noexcept {
    if constexpr (Level == WINEVENT_LEVEL_ERROR) {
        TraceLoggingWrite(
            g_qiqi_lpac_provider,
            "QiQiLogError",
            TraceLoggingLevel(Level),
            TraceLoggingKeyword(Keyword),
            TraceLoggingCountedWideString(
                kProcessName.data(),
                static_cast<USHORT>(kProcessName.size()),
                "process"),
            TraceLoggingCountedWideString(
                message.data(),
                static_cast<USHORT>(message.size()),
                "message"));
    } else {
        TraceLoggingWrite(
            g_qiqi_lpac_provider,
            "QiQiLogInfo",
            TraceLoggingLevel(Level),
            TraceLoggingKeyword(Keyword),
            TraceLoggingCountedWideString(
                kProcessName.data(),
                static_cast<USHORT>(kProcessName.size()),
                "process"),
            TraceLoggingCountedWideString(
                message.data(),
                static_cast<USHORT>(message.size()),
                "message"));
    }
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int count = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(
        static_cast<size_t>(count),
        '\0');
    const int converted = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        count,
        nullptr,
        nullptr);
    if (converted != count) {
        return {};
    }
    return result;
}

std::wstring Timestamp(std::wstring_view message) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    return std::format(
        L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        message);
}

class Diagnostics final {
public:
    Diagnostics() noexcept
        : etw_error_(::TraceLoggingRegister(
              g_qiqi_lpac_provider)) {
        OpenLog();
    }

    ~Diagnostics() noexcept {
        if (log_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(log_);
        }
        if (etw_error_ == ERROR_SUCCESS) {
            ::TraceLoggingUnregister(g_qiqi_lpac_provider);
        }
    }

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

    void Info(
        std::wstring_view stage,
        std::wstring_view detail,
        bool input = false) noexcept {
        Write(
            WINEVENT_LEVEL_INFO,
            stage,
            detail,
            input);
    }

    void Error(
        std::wstring_view stage,
        std::wstring_view detail,
        bool input = false) noexcept {
        Write(
            WINEVENT_LEVEL_ERROR,
            stage,
            detail,
            input);
    }

    ULONG EtwError() const noexcept {
        return etw_error_;
    }

    DWORD LogError() const noexcept {
        return log_error_;
    }

    std::wstring_view LogName() const noexcept {
        return kLogFileName;
    }

private:
    void OpenLog() noexcept {
        try {
            std::wstring buffer(
                MAX_PATH,
                L'\0');
            for (;;) {
                const DWORD length =
                    ::GetEnvironmentVariableW(
                        L"LOCALAPPDATA",
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()));
                if (length == 0) {
                    log_error_ = ::GetLastError();
                    return;
                }
                if (length >= buffer.size()) {
                    buffer.resize(
                        static_cast<size_t>(length) + 1);
                    continue;
                }
                buffer.resize(length);
                break;
            }
            const std::filesystem::path directory =
                std::filesystem::path(buffer) /
                L"QiQiImeAppContainerPermissions";
            std::error_code error;
            std::filesystem::create_directories(
                directory,
                error);
            if (error) {
                log_error_ =
                    static_cast<DWORD>(error.value());
                return;
            }
            log_ = ::CreateFileW(
                (directory / kLogFileName).c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ |
                    FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL |
                    SECURITY_SQOS_PRESENT |
                    SECURITY_ANONYMOUS,
                nullptr);
            if (log_ == INVALID_HANDLE_VALUE) {
                log_error_ = ::GetLastError();
            }
        } catch (const std::bad_alloc&) {
            log_error_ = ERROR_NOT_ENOUGH_MEMORY;
        } catch (const std::length_error&) {
            log_error_ = ERROR_ARITHMETIC_OVERFLOW;
        }
    }

    void Write(
        uint8_t level,
        std::wstring_view stage,
        std::wstring_view detail,
        bool input) noexcept {
        try {
            const std::wstring message =
                std::format(L"{}: {}", stage, detail);
            if (etw_error_ == ERROR_SUCCESS) {
                const uint64_t keyword =
                    input ? kInputKeyword : kGeneralKeyword;
                if (level == WINEVENT_LEVEL_ERROR) {
                    if (keyword == kInputKeyword) {
                        EmitEtw<
                            WINEVENT_LEVEL_ERROR,
                            kInputKeyword>(message);
                    } else {
                        EmitEtw<
                            WINEVENT_LEVEL_ERROR,
                            kGeneralKeyword>(message);
                    }
                } else if (keyword == kInputKeyword) {
                    EmitEtw<
                        WINEVENT_LEVEL_INFO,
                        kInputKeyword>(message);
                } else {
                    EmitEtw<
                        WINEVENT_LEVEL_INFO,
                        kGeneralKeyword>(message);
                }
            }
            if (log_ != INVALID_HANDLE_VALUE) {
                const std::string bytes =
                    ToUtf8(Timestamp(message));
                if (bytes.empty()) {
                    const DWORD error =
                        ::GetLastError();
                    log_error_ =
                        error != ERROR_SUCCESS
                            ? error
                            : ERROR_INVALID_DATA;
                    return;
                }
                DWORD written = 0;
                if (!::WriteFile(
                        log_,
                        bytes.data(),
                        static_cast<DWORD>(bytes.size()),
                        &written,
                        nullptr)) {
                    log_error_ = ::GetLastError();
                } else if (written != bytes.size()) {
                    log_error_ = ERROR_WRITE_FAULT;
                } else {
                    log_error_ = ERROR_SUCCESS;
                }
            }
        } catch (const std::bad_alloc&) {
            log_error_ = ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    ULONG etw_error_ = ERROR_SUCCESS;
    DWORD log_error_ = ERROR_SUCCESS;
    HANDLE log_ = INVALID_HANDLE_VALUE;
};

struct RegistryProbe {
    DWORD open_error = ERROR_NOT_READY;
    DWORD sequence_error = ERROR_NOT_READY;
    bool sequence_available = false;
    uint64_t sequence = 0;
};

struct TsfProbe {
    HRESULT coinitialize = E_PENDING;
    HRESULT create = E_PENDING;
    HRESULT activate = E_PENDING;
    HRESULT compartment_manager = E_PENDING;
    HRESULT deactivate = E_PENDING;
};

RegistryProbe ProbeRegistry() noexcept {
    RegistryProbe result;
    HKEY raw_key = nullptr;
    result.open_error = ::RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\QiQiIME",
        0,
        KEY_READ,
        &raw_key);
    if (result.open_error != ERROR_SUCCESS) {
        return result;
    }
    const std::unique_ptr<
        std::remove_pointer_t<HKEY>,
        decltype([](HKEY key) {
            if (key) {
                ::RegCloseKey(key);
            }
        })> key(raw_key);
    DWORD type = REG_NONE;
    DWORD size =
        sizeof(result.sequence);
    result.sequence_error = ::RegQueryValueExW(
        key.get(),
        L"ConfigSequence",
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(&result.sequence),
        &size);
    result.sequence_available =
        result.sequence_error == ERROR_SUCCESS &&
        type == REG_QWORD &&
        size == sizeof(result.sequence);
    if (result.sequence_error == ERROR_SUCCESS &&
        !result.sequence_available) {
        result.sequence_error =
            ERROR_INVALID_DATA;
    }
    return result;
}

TsfProbe ProbeTsf() noexcept {
    TsfProbe result;
    result.coinitialize = ::CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED);
    const bool initialized =
        SUCCEEDED(result.coinitialize);
    if (FAILED(result.coinitialize)) {
        return result;
    }
    ComPtr<ITfThreadMgr> thread_manager;
    result.create = ::CoCreateInstance(
        CLSID_TF_ThreadMgr,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&thread_manager));
    if (SUCCEEDED(result.create) && !thread_manager) {
        result.create = E_UNEXPECTED;
    }
    TfClientId client_id =
        TF_CLIENTID_NULL;
    if (SUCCEEDED(result.create)) {
        result.activate =
            thread_manager->Activate(&client_id);
    }
    if (SUCCEEDED(result.activate) &&
        client_id != TF_CLIENTID_NULL) {
        ComPtr<ITfCompartmentMgr> compartment_manager;
        result.compartment_manager =
            thread_manager->GetGlobalCompartment(
                &compartment_manager);
    }
    if (thread_manager &&
        SUCCEEDED(result.activate)) {
        result.deactivate =
            thread_manager->Deactivate();
    }
    if (initialized) {
        ::CoUninitialize();
    }
    return result;
}

class HostWindow final {
public:
    HostWindow(
        HINSTANCE instance,
        std::wstring profile) noexcept
        : instance_(instance),
          profile_(std::move(profile)) {
    }

    ~HostWindow() noexcept {
        if (font_) {
            ::DeleteObject(font_);
        }
        if (rich_edit_) {
            ::FreeLibrary(rich_edit_);
        }
    }

    int Run() noexcept {
        INITCOMMONCONTROLSEX controls{
            .dwSize = sizeof(controls),
            .dwICC = ICC_STANDARD_CLASSES,
        };
        ::InitCommonControlsEx(&controls);
        const HICON window_icon =
            reinterpret_cast<HICON>(
                ::LoadImageW(
                    instance_,
                    MAKEINTRESOURCEW(1),
                    IMAGE_ICON,
                    ::GetSystemMetrics(SM_CXICON),
                    ::GetSystemMetrics(SM_CYICON),
                    LR_DEFAULTCOLOR | LR_SHARED));
        const DWORD window_icon_error =
            window_icon
                ? ERROR_SUCCESS
                : ::GetLastError();
        const HICON small_window_icon =
            reinterpret_cast<HICON>(
                ::LoadImageW(
                    instance_,
                    MAKEINTRESOURCEW(1),
                    IMAGE_ICON,
                    ::GetSystemMetrics(
                        SM_CXSMICON),
                    ::GetSystemMetrics(
                        SM_CYSMICON),
                    LR_DEFAULTCOLOR |
                        LR_SHARED));
        const DWORD small_window_icon_error =
            small_window_icon
                ? ERROR_SUCCESS
                : ::GetLastError();
        if (!window_icon ||
            !small_window_icon) {
            std::array<wchar_t, 96> message{};
            ::swprintf_s(
                message.data(),
                message.size(),
                L"LoadImageW icon failed error=%lu",
                window_icon_error != ERROR_SUCCESS
                    ? window_icon_error
                    : small_window_icon_error);
            diagnostics_.Error(
                L"host.ui",
                message.data());
        }
        WNDCLASSEXW window_class{
            .cbSize = sizeof(window_class),
            .style = CS_HREDRAW | CS_VREDRAW,
            .lpfnWndProc = &StaticWndProc,
            .cbWndExtra = sizeof(void*),
            .hInstance = instance_,
            .hIcon = window_icon,
            .hCursor =
                ::LoadCursorW(nullptr, IDC_ARROW),
            .hbrBackground =
                reinterpret_cast<HBRUSH>(
                    COLOR_WINDOW + 1),
            .lpszClassName = kWindowClass,
            .hIconSm = small_window_icon,
        };
        if (!::RegisterClassExW(&window_class)) {
            const DWORD error =
                ::GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS) {
                diagnostics_.Error(
                    L"host.ui",
                    std::format(
                        L"RegisterClassExW failed error={}",
                        error));
                return 1;
            }
        }
        window_ = ::CreateWindowExW(
            0,
            kWindowClass,
            QIQI_HOST_DISPLAY_NAME,
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1050,
            760,
            nullptr,
            nullptr,
            instance_,
            this);
        if (!window_) {
            diagnostics_.Error(
                L"host.ui",
                std::format(
                    L"CreateWindowExW failed error={}",
                    ::GetLastError()));
            return 1;
        }
        ::ShowWindow(window_, SW_SHOW);
        ::UpdateWindow(window_);
        diagnostics_.Info(
            L"host.startup",
            std::format(
                L"profile={}",
                profile_));
        MSG message{};
        while (const BOOL result =
                   ::GetMessageW(
                       &message,
                       nullptr,
                       0,
                       0)) {
            if (result == -1) {
                return 1;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        return 0;
    }

private:
    static LRESULT CALLBACK StaticWndProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept {
        HostWindow* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<CREATESTRUCTW*>(
                    lparam);
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
                ::GetWindowLongPtrW(
                    hwnd,
                    GWLP_USERDATA));
        }
        return self
            ? self->WndProc(
                  hwnd,
                  message,
                  wparam,
                  lparam)
            : ::DefWindowProcW(
                  hwnd,
                  message,
                  wparam,
                  lparam);
    }

    LRESULT WndProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept {
        switch (message) {
            case WM_CREATE:
                return CreateControls()
                    ? 0
                    : -1;
            case WM_SIZE:
                LayoutControls();
                return 0;
            case WM_CTLCOLORSTATIC:
                ::SetBkMode(
                    reinterpret_cast<HDC>(wparam),
                    TRANSPARENT);
                return reinterpret_cast<LRESULT>(
                    ::GetSysColorBrush(COLOR_WINDOW));
            case WM_COMMAND:
                HandleCommand(
                    LOWORD(wparam));
                return 0;
            case WM_DESTROY:
                ::SetWindowLongPtrW(
                    hwnd,
                    GWLP_USERDATA,
                    0);
                ::PostQuitMessage(0);
                return 0;
            default:
                return ::DefWindowProcW(
                    hwnd,
                    message,
                    wparam,
                    lparam);
        }
    }

    bool CreateControls() noexcept {
        rich_edit_ = ::LoadLibraryW(
            L"msftedit.dll");
        if (!rich_edit_) {
            diagnostics_.Error(
                L"host.ui",
                std::format(
                    L"LoadLibraryW failed error={}",
                    ::GetLastError()));
            return false;
        }
        font_ = ::CreateFontW(
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
        if (!font_) {
            diagnostics_.Error(
                L"host.ui",
                L"CreateFontW failed");
            return false;
        }

        input_ = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            MSFTEDIT_CLASS,
            L"",
            WS_CHILD |
                WS_VISIBLE |
                WS_VSCROLL |
                ES_MULTILINE |
                ES_AUTOVSCROLL |
                ES_WANTRETURN,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kInputId)),
            instance_,
            nullptr);
        refresh_button_ = CreateButton(
            L"Refresh token",
            kRefreshId);
        registry_button_ = CreateButton(
            L"Probe registry",
            kRegistryId);
        tsf_button_ = CreateButton(
            L"Probe TSF COM",
            kTsfId);
        broker_button_ = CreateButton(
            L"Probe Config Broker",
            kBrokerId);
        token_status_ = CreateStatic(
            kTokenStatusId);
        registry_status_ = CreateStatic(
            kRegistryStatusId);
        tsf_status_ = CreateStatic(
            kTsfStatusId);
        broker_status_ = CreateStatic(
            kBrokerStatusId);
        diagnostic_status_ = CreateStatic(
            kDiagnosticStatusId);
        if (!input_ ||
            !refresh_button_ ||
            !registry_button_ ||
            !tsf_button_ ||
            !broker_button_ ||
            !token_status_ ||
            !registry_status_ ||
            !tsf_status_ ||
            !broker_status_ ||
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
                 registry_button_,
                 tsf_button_,
                 broker_button_,
                 token_status_,
                 registry_status_,
                 tsf_status_,
                 broker_status_,
                 diagnostic_status_,
             }) {
            ::SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(font_),
                TRUE);
        }
        ::SendMessageW(
            input_,
            EM_SETCUEBANNER,
            TRUE,
            reinterpret_cast<LPARAM>(
                L"Type IME input here. Text and "
                L"candidate contents are not logged."));
        ::SendMessageW(
            input_,
            EM_SETLIMITTEXT,
            4096,
            0);
        ::SetFocus(input_);
        RefreshAll();
        LayoutControls();
        return true;
    }

    HWND CreateButton(
        const wchar_t* text,
        UINT id) noexcept {
        return ::CreateWindowExW(
            0,
            L"BUTTON",
            text,
            WS_CHILD |
                WS_VISIBLE |
                BS_PUSHBUTTON,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
    }

    HWND CreateStatic(UINT id) noexcept {
        return ::CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD |
                WS_VISIBLE |
                SS_LEFT |
                SS_NOPREFIX,
            0,
            0,
            0,
            0,
            window_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
    }

    void HandleCommand(UINT id) noexcept {
        switch (id) {
            case kRefreshId:
                RefreshToken();
                break;
            case kRegistryId:
                RefreshRegistry();
                break;
            case kTsfId:
                RefreshTsf();
                break;
            case kBrokerId:
                RefreshBroker();
                break;
            default:
                break;
        }
    }

    void Move(
        HWND control,
        const RECT& rect) noexcept {
        if (!control) {
            return;
        }
        ::MoveWindow(
            control,
            rect.left,
            rect.top,
            (std::max)(
                0,
                static_cast<int>(
                    rect.right - rect.left)),
            (std::max)(
                0,
                static_cast<int>(
                    rect.bottom - rect.top)),
            TRUE);
    }

    void LayoutControls() noexcept {
        RECT client{};
        ::GetClientRect(window_, &client);
        const int width =
            std::max<int>(
                0,
                client.right - client.left);
        const int height =
            std::max<int>(
                0,
                client.bottom - client.top);
        const int content_width =
            std::max<int>(
                0,
                width - 2 * kMargin);
        const int edit_top =
            kMargin;
        const int edit_height =
            std::max<int>(
                130,
                std::min<int>(
                    190,
                    height / 4));
        Move(
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
        for (HWND button : {
                 refresh_button_,
                 registry_button_,
                 tsf_button_,
                 broker_button_,
             }) {
            Move(
                button,
                {
                    button_left,
                    button_top,
                    button_left + kButtonWidth,
                    button_top + kButtonHeight,
                });
            button_left +=
                kButtonWidth + kButtonGap;
        }
        const int status_top =
            button_top +
            kButtonHeight +
            18;
        const int available_height =
            std::max<int>(
                0,
                height -
                    status_top -
                    kMargin -
                    55);
        const int each =
            std::max<int>(
                0,
                (available_height - 30) / 4);
        Move(
            token_status_,
            {
                kMargin,
                status_top,
                width - kMargin,
                status_top + each,
            });
        Move(
            registry_status_,
            {
                kMargin,
                status_top + each + 10,
                width - kMargin,
                status_top + 2 * each + 10,
            });
        Move(
            tsf_status_,
            {
                kMargin,
                status_top + 2 * (each + 10),
                width - kMargin,
                status_top + 3 * each + 20,
            });
        Move(
            broker_status_,
            {
                kMargin,
                status_top + 3 * (each + 10),
                width - kMargin,
                status_top + 4 * each + 30,
            });
        Move(
            diagnostic_status_,
            {
                kMargin,
                height - kMargin - 46,
                width - kMargin,
                height - kMargin,
            });
    }

    void RefreshToken() noexcept {
        try {
            token_state_ =
                token::ReadCurrentProcessState();
            const std::wstring text =
                token::FormatState(token_state_);
            ::SetWindowTextW(
                token_status_,
                text.c_str());
            diagnostics_.Info(
                L"host.token",
                text);
        } catch (const std::bad_alloc&) {
            diagnostics_.Error(
                L"host.token",
                L"token status allocation failed");
        }
    }

    void RefreshRegistry() noexcept {
        const RegistryProbe probe =
            ProbeRegistry();
        try {
            const std::wstring text =
                std::format(
                    L"Registry: open_error={}; "
                    L"sequence_error={}; available={}; "
                    L"sequence={}",
                    probe.open_error,
                    probe.sequence_error,
                    probe.sequence_available
                        ? L"true"
                        : L"false",
                    probe.sequence_available
                        ? probe.sequence
                        : 0);
            ::SetWindowTextW(
                registry_status_,
                text.c_str());
            diagnostics_.Info(
                L"host.registry",
                text);
        } catch (const std::bad_alloc&) {
            diagnostics_.Error(
                L"host.registry",
                L"registry status allocation failed");
        }
    }

    void RefreshTsf() noexcept {
        const TsfProbe probe =
            ProbeTsf();
        try {
            const uint32_t coinitialize =
                std::bit_cast<uint32_t>(
                    probe.coinitialize);
            const std::wstring text =
                std::format(
                    L"TSF COM: coinitialize=0x{:08X}; "
                    L"create=0x{:08X}; activate=0x{:08X}; "
                    L"global_compartment_mgr=0x{:08X}; "
                    L"deactivate=0x{:08X}",
                    coinitialize,
                    std::bit_cast<uint32_t>(
                        probe.create),
                    static_cast<uint32_t>(
                        probe.activate),
                    static_cast<uint32_t>(
                        probe.compartment_manager),
                    static_cast<uint32_t>(
                        probe.deactivate));
            ::SetWindowTextW(
                tsf_status_,
                text.c_str());
            diagnostics_.Info(
                L"host.tsf",
                text);
        } catch (const std::bad_alloc&) {
            diagnostics_.Error(
                L"host.tsf",
                L"TSF status allocation failed");
        }
    }

    void RefreshBroker() noexcept {
        const broker::ProbeResult probe =
            broker::ProbeConfiguration();
        try {
            const std::wstring text =
                std::format(
                    L"Config Broker: coinitialize=0x{:08X}; "
                    L"create=0x{:08X}; blanket=0x{:08X}; "
                    L"read=0x{:08X}; revision={}; "
                    L"snapshot_bytes={}",
                    probe.ApartmentBits(),
                    static_cast<uint32_t>(
                        probe.create),
                    std::bit_cast<uint32_t>(
                        probe.blanket),
                    std::bit_cast<uint32_t>(
                        probe.read),
                    probe.revision,
                    probe.snapshot_size);
            ::SetWindowTextW(
                broker_status_,
                text.c_str());
            if (SUCCEEDED(probe.read)) {
                diagnostics_.Info(
                    L"host.broker",
                    text);
            } else {
                diagnostics_.Error(
                    L"host.broker",
                    text);
            }
        } catch (const std::bad_alloc&) {
            diagnostics_.Error(
                L"host.broker",
                L"broker status allocation failed");
        }
    }

    void RefreshDiagnostics() noexcept {
        try {
            ::SetWindowTextW(
                diagnostic_status_,
                std::format(
                    L"ETW error={}; persistent log={} at "
                    L"LOCALAPPDATA; persistent error={}",
                    diagnostics_.EtwError(),
                    diagnostics_.LogName(),
                    diagnostics_.LogError()).c_str());
        } catch (const std::bad_alloc&) {
            ::SetWindowTextW(
                diagnostic_status_,
                L"Diagnostic status allocation failed.");
        }
    }

    void RefreshAll() noexcept {
        RefreshToken();
        RefreshRegistry();
        RefreshTsf();
        RefreshBroker();
        RefreshDiagnostics();
    }

    HINSTANCE instance_ = nullptr;
    std::wstring profile_;
    Diagnostics diagnostics_;
    HWND window_ = nullptr;
    HWND input_ = nullptr;
    HWND refresh_button_ = nullptr;
    HWND registry_button_ = nullptr;
    HWND tsf_button_ = nullptr;
    HWND broker_button_ = nullptr;
    HWND token_status_ = nullptr;
    HWND registry_status_ = nullptr;
    HWND tsf_status_ = nullptr;
    HWND broker_status_ = nullptr;
    HWND diagnostic_status_ = nullptr;
    HMODULE rich_edit_ = nullptr;
    HFONT font_ = nullptr;
    token::State token_state_;
};

std::wstring ParseProfileArgument() noexcept {
    constexpr std::wstring_view prefix =
        L"--profile=";
    int count = 0;
    wchar_t** args =
        ::CommandLineToArgvW(
            ::GetCommandLineW(),
            &count);
    std::wstring profile = L"unknown";
    if (args) {
        for (int index = 1;
             index < count;
             ++index) {
            const std::wstring_view value =
                args[index];
            if (value.starts_with(prefix)) {
                try {
                    profile.assign(
                        value.substr(prefix.size()));
                } catch (const std::bad_alloc&) {
                    profile = L"allocation-failed";
                }
                break;
            }
        }
        ::LocalFree(args);
    }
    return profile;
}

bool HasArgument(std::wstring_view expected) noexcept {
    int count = 0;
    wchar_t** args =
        ::CommandLineToArgvW(
            ::GetCommandLineW(),
            &count);
    if (!args) {
        return false;
    }
    bool found = false;
    for (int index = 1;
         index < count;
         ++index) {
        if (expected == args[index]) {
            found = true;
            break;
        }
    }
    ::LocalFree(args);
    return found;
}

int RunSmoke(std::wstring_view profile) noexcept {
    Diagnostics diagnostics;
    const token::State token_state =
        token::ReadCurrentProcessState();
    const RegistryProbe registry =
        ProbeRegistry();
    const TsfProbe tsf =
        ProbeTsf();
    try {
        diagnostics.Info(
            L"host.smoke-token",
            token::FormatState(token_state));
        diagnostics.Info(
            L"host.smoke-registry",
            std::format(
                L"open_error={}, sequence_available={}",
                registry.open_error,
                registry.sequence_available
                    ? L"true"
                    : L"false"));
        diagnostics.Info(
            L"host.smoke-tsf",
            std::format(
                L"coinitialize=0x{:08X}, "
                L"create=0x{:08X}, activate=0x{:08X}",
                static_cast<uint32_t>(
                    tsf.coinitialize),
                static_cast<uint32_t>(
                    tsf.create),
                static_cast<uint32_t>(
                    tsf.activate)));
    } catch (const std::bad_alloc&) {
        diagnostics.Error(
            L"host.smoke",
            L"format allocation failed");
        return 1;
    }

    const bool lpac =
        token_state.is_less_privileged_app_container ||
        token_state.inferred_less_privileged_app_container;
    const bool broad_group_profile =
        profile == L"broadGroupCom";
    if (!token_state.is_app_container ||
        (!broad_group_profile &&
         (!lpac ||
          token_state
              .has_all_application_packages))) {
        diagnostics.Error(
            L"host.smoke",
            broad_group_profile
                ? L"all-application-packages boundary assertion failed"
                : L"package-only AppContainer boundary assertion failed");
        return 1;
    }
    if (profile == L"zero") {
        return token_state.capability_sids.empty() &&
                registry.open_error ==
                    ERROR_ACCESS_DENIED &&
                tsf.coinitialize ==
                    HRESULT_FROM_WIN32(
                        ERROR_ACCESS_DENIED)
            ? 0
            : 1;
    }
    if (profile == L"registryRead") {
        return token_state.capability_sids.size() == 1 &&
                registry.open_error ==
                    ERROR_SUCCESS &&
                registry.sequence_available &&
                tsf.coinitialize ==
                    HRESULT_FROM_WIN32(
                        ERROR_ACCESS_DENIED)
            ? 0
            : 1;
    }
    if (profile == L"broadGroupCom" ||
        profile == L"packageOnlyCom" ||
        profile == L"lpacCom") {
        const bool registry_expected =
            broad_group_profile
                ? registry.open_error ==
                    ERROR_SUCCESS &&
                    registry.sequence_available
                : registry.open_error ==
                    ERROR_ACCESS_DENIED;
        const bool tsf_expected =
            broad_group_profile
                ? SUCCEEDED(tsf.create) &&
                    SUCCEEDED(tsf.activate)
                : tsf.create ==
                    HRESULT_FROM_WIN32(
                        ERROR_ACCESS_DENIED);
        const bool boundary_ok =
            token_state.capability_sids.size() ==
                1 &&
            registry_expected &&
            SUCCEEDED(tsf.coinitialize) &&
            tsf_expected;
        if (!boundary_ok) {
            return 1;
        }
        if (!HasArgument(
                L"--broker-smoke")) {
            return 0;
        }
        const broker::ProbeResult broker_probe =
            broker::ProbeConfiguration();
        try {
            diagnostics.Info(
                L"host.smoke-broker",
                std::format(
                    L"coinitialize=0x{:08X}; "
                    L"create=0x{:08X}; "
                    L"blanket=0x{:08X}; "
                    L"read=0x{:08X}; "
                    L"revision={}",
                    broker_probe.ApartmentBits(),
                    std::bit_cast<uint32_t>(
                        broker_probe.create),
                    std::bit_cast<uint32_t>(
                        broker_probe.blanket),
                    std::bit_cast<uint32_t>(
                        broker_probe.read),
                    broker_probe.revision));
        } catch (const std::bad_alloc&) {
            diagnostics.Error(
                L"host.smoke-broker",
                L"result formatting allocation failed");
            return 1;
        }
        if (!broker_probe.ApartmentSucceeded()) {
            return 9;
        }
        if (FAILED(broker_probe.create)) {
            return 10;
        }
        if (FAILED(broker_probe.blanket)) {
            return 11;
        }
        if (FAILED(broker_probe.read)) {
            return 12;
        }
        return 0;
    }
    diagnostics.Error(
        L"host.smoke",
        L"unknown AppContainer permission profile");
    return 1;
}

} // namespace

} // namespace qiqi::host::permissions

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int) {
    const std::wstring profile =
        qiqi::host::permissions::ParseProfileArgument();
    if (qiqi::host::permissions::HasArgument(
            L"--smoke") ||
        qiqi::host::permissions::HasArgument(
            L"--broker-smoke")) {
        return qiqi::host::permissions::RunSmoke(profile);
    }
    ::SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    return qiqi::host::permissions::HostWindow(
               instance,
               profile)
        .Run();
}
