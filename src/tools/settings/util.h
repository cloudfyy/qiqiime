#pragma once

#include "common/win32_ui_util.h"
#include "common/logger.h"

namespace qiqi::dialog_detail {

inline ScopedIcon LoadOrCreateBrandIcon(int size_px) noexcept {
    return CreateTransparentGlyphIcon(L'奇', size_px, RGB(35, 105, 200));
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
            ::PostQuitMessage(static_cast<int>(msg.wParam));
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
