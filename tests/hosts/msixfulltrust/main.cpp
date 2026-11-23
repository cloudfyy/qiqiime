// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagnostics.h"
#include "window.h"

#include <objbase.h>
#include <windows.h>

#include <cstdint>
#include <format>
#include <string_view>

namespace {

class ComScope final {
public:
    explicit ComScope(HRESULT hr) noexcept
        : hr_(hr),
          initialized_(SUCCEEDED(hr)) {}
    ~ComScope() noexcept {
        if (initialized_) {
            ::CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    HRESULT Get() const noexcept {
        return hr_;
    }

private:
    HRESULT hr_ = S_OK;
    bool initialized_ = false;
};

} // namespace

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR command_line,
    int) {
    ::SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const std::wstring_view arguments(command_line);
    const bool seed_shadow = arguments.find(
        L"--seed-qiqi-shadow") != std::wstring_view::npos;
    qiqi::host::fulltrust::Diagnostics diagnostics;
    diagnostics.Info(
        L"host.startup",
        L"wWinMain entered");

    const ComScope com(
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    if (FAILED(com.Get())) {
        try {
            diagnostics.Error(
                L"host.startup",
                std::format(
                    L"CoInitializeEx failed hr=0x{:08X}",
                    static_cast<uint32_t>(com.Get())));
        } catch (const std::bad_alloc&) {
            diagnostics.Error(
                L"host.startup",
                L"CoInitializeEx failure detail allocation failed");
        }
        return static_cast<int>(com.Get());
    }

    qiqi::host::fulltrust::HostWindow window(
        instance,
        diagnostics,
        seed_shadow);
    return static_cast<int>(window.CreateAndRun());
}

