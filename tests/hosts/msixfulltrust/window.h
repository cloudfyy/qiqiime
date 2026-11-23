// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "diagnostics.h"
#include "hoststate.h"

#include <windows.h>

#include <string_view>

namespace qiqi::host::fulltrust {

class HostWindow final {
public:
    HostWindow(
        HINSTANCE instance,
        Diagnostics& diagnostics,
        bool seed_shadow_on_start) noexcept;
    ~HostWindow() noexcept;

    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;

    [[nodiscard]] HRESULT CreateAndRun() noexcept;

private:
    static LRESULT CALLBACK StaticWndProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;
    LRESULT WndProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam) noexcept;

    bool CreateControls() noexcept;
    void LayoutControls() noexcept;
    void MoveControl(HWND control, const RECT& rect) noexcept;
    void RefreshState() noexcept;
    void RefreshPackageAndRegistry() noexcept;
    void SeedShadow() noexcept;
    void RunSearchProbe() noexcept;
    void UpdateDiagnosticStatus() noexcept;
    void LogInputEvent(std::wstring_view stage) noexcept;

    HINSTANCE instance_ = nullptr;
    Diagnostics& diagnostics_;
    bool seed_shadow_on_start_ = false;
    HWND window_ = nullptr;
    HWND input_ = nullptr;
    HWND refresh_button_ = nullptr;
    HWND seed_button_ = nullptr;
    HWND search_button_ = nullptr;
    HWND package_status_ = nullptr;
    HWND registry_status_ = nullptr;
    HWND search_status_ = nullptr;
    HWND diagnostic_status_ = nullptr;
    HMODULE rich_edit_ = nullptr;
    HFONT font_ = nullptr;
    PackageState package_state_{};
};

} // namespace qiqi::host::fulltrust

