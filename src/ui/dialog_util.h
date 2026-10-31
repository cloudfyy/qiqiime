#pragma once

#include "common/win32_ui_util.h"
#include "common/logger.h"

namespace qiqi::dialog_detail {

inline ScopedIcon LoadOrCreateBrandIcon(int size_px) noexcept {
    return CreateTransparentGlyphIcon(L'奇', size_px, RGB(35, 105, 200));
}

inline bool RunModalDialogLoop(HWND hwnd, const bool& done_flag) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) {
        return false;
    }
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
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return ::IsWindow(hwnd) != FALSE;
}

} // namespace qiqi::dialog_detail
