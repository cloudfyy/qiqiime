// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hostapp.h"

#include <winrt/Windows.Foundation.h>

#include <format>
#include <memory>
#include <new>
#include <string>

int __stdcall WinMain(
    HINSTANCE,
    HINSTANCE,
    LPSTR,
    int) {
    std::shared_ptr<qiqi::host::Diagnostics> diagnostics;
    try {
        diagnostics = std::make_shared<qiqi::host::Diagnostics>();
        diagnostics->Info(L"host.startup", L"WinMain entered");
        winrt::init_apartment();
        diagnostics->Info(
            L"host.startup",
            L"WinRT initialized with the default multithreaded apartment");
        winrt::Windows::UI::Xaml::Application::Start(
            [diagnostics](auto&&) {
                winrt::make<qiqi::host::HostApp>(diagnostics);
            });
        return 0;
    } catch (const std::bad_alloc&) {
        if (diagnostics) {
            diagnostics->Error(
                L"host.startup",
                L"entrypoint allocation failed");
        }
        ::OutputDebugStringW(
            L"QiQiIME AppContainer Search Host terminated after allocation failure.\r\n");
        return 2;
    } catch (const winrt::hresult_error& error) {
        const HRESULT code = error.code();
        if (diagnostics) {
            try {
                diagnostics->Error(
                    L"host.startup",
                    std::format(
                        L"entrypoint failed hr=0x{:08X}",
                        static_cast<uint32_t>(code)));
            } catch (const std::bad_alloc&) {
                diagnostics->Error(
                    L"host.startup",
                    L"entrypoint HRESULT reporting allocation failed");
            }
        }
        const std::wstring message = std::format(
            L"QiQiIME AppContainer Search Host terminated with HRESULT 0x{:08X}.\r\n",
            static_cast<uint32_t>(code));
        ::OutputDebugStringW(message.c_str());
        return static_cast<int>(code);
    }
}
