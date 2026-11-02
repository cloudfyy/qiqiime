#pragma once

#include "common/win32_ui_util.h"
#include "common/logger.h"

namespace qiqi::dialog_detail {

inline ScopedIcon LoadOrCreateBrandIcon(int size_px, HINSTANCE instance = nullptr) noexcept {
    // Only load embedded icon resource 1 when running inside standalone QiQiIMESettings.exe
    // (which embeds blue qiqiime-settings.ico); qiqiime.dll embeds white taskbar-only qiqiime.ico.
    bool use_resource = false;
    if (instance) {
        std::array<wchar_t, MAX_PATH> module_path{};
        const DWORD len = ::GetModuleFileNameW(
            instance, module_path.data(), gsl::narrow_cast<DWORD>(module_path.size()));
        if (len > 0 && len < module_path.size()) {
            const std::wstring_view full_view(module_path.data(), len);
            const size_t sep = full_view.find_last_of(L"\\/");
            const std::wstring_view filename =
                (sep == std::wstring_view::npos) ? full_view : full_view.substr(sep + 1);
            use_resource = (filename.size() == 19 &&
                            _wcsnicmp(filename.data(), L"QiQiIMESettings.exe", 19) == 0);
        }
    }
    return LoadAppIconOrBrandFallback(instance, size_px, 1, use_resource);
}

inline int MeasureDialogTextHeight(HFONT font, const wchar_t* text, int width) noexcept {
    if (!font || !text || !*text || width <= 0) {
        QIQI_LOG_ERROR("Dialog text measurement requires a font, nonempty text and positive width");
        return 0;
    }
    const ScopedWindowDC dc;
    if (!dc) {
        QIQI_LOG_ERROR("Dialog text measurement DC unavailable");
        return 0;
    }
    const ScopedSelectObject select(dc.Get(), font);
    if (!select) {
        QIQI_LOG_ERROR("Dialog text measurement font selection failed");
        return 0;
    }
    RECT bounds{0, 0, width, 0};
    if (::DrawTextW(dc.Get(), text, -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) <= 0) {
        QIQI_LOG_ERROR("Dialog text measurement failed");
        return 0;
    }
    return gsl::narrow_cast<int>(bounds.bottom);
}

inline bool RunModalDialogLoop(HWND hwnd, const bool& done_flag, bool route_dialog_keys = false) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) {
        return false;
    }
    const HWND owner = ::GetWindow(hwnd, GW_OWNER);
    const bool enable_owner_on_exit = owner && ::IsWindowEnabled(owner);
    const HWND previous_focus = ::GetFocus();
    if (enable_owner_on_exit) {
        ::EnableWindow(owner, FALSE);
    }
    const auto restore_owner = gsl::finally([owner, enable_owner_on_exit, previous_focus]() noexcept {
        if (enable_owner_on_exit && ::IsWindow(owner)) {
            ::EnableWindow(owner, TRUE);
            ::SetActiveWindow(owner);
            if (::IsWindow(previous_focus) &&
                (previous_focus == owner || ::IsChild(owner, previous_focus))) {
                ::SetFocus(previous_focus);
            }
        }
    });
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::IsWindow(hwnd) && !done_flag) {
        const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1) {
            QIQI_LOG_ERROR("Modal dialog GetMessageW failed (error={})", ::GetLastError());
            return false;
        }
        if (result == 0) {
            ::PostQuitMessage(gsl::narrow_cast<int>(msg.wParam));
            return false;
        }
        if (route_dialog_keys && msg.message == WM_KEYDOWN &&
            (msg.hwnd == hwnd || ::IsChild(hwnd, msg.hwnd)) &&
            (msg.wParam == VK_TAB || msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
            ::SendMessageW(hwnd, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return ::IsWindow(hwnd) != FALSE;
}

} // namespace qiqi::dialog_detail
