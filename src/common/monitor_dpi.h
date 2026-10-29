#pragma once
#include <windows.h>
#include <gsl/gsl>
#include <algorithm>
#include <optional>

namespace qiqi {

struct MonitorMetrics {
    RECT work_area{0, 0, 1280, 720};
    UINT dpi = 96;
};

namespace detail {
inline std::optional<MonitorMetrics>& MonitorMetricsOverrideStorage() noexcept {
    static std::optional<MonitorMetrics> override_val;
    return override_val;
}
} // namespace detail

// Allow unit tests to simulate arbitrary multi-monitor work areas (including negative coordinates) and DPIs.
inline void SetMonitorMetricsOverrideForTest(std::optional<MonitorMetrics> metrics) noexcept {
    detail::MonitorMetricsOverrideStorage() = metrics;
}

inline int ScalePxByDpi(int px, UINT dpi) noexcept {
    return ::MulDiv(px, gsl::narrow_cast<int>(dpi > 0 ? dpi : 96u), 96);
}

inline UINT GetDpiForMonitorHandle(HMONITOR mon, HWND fallback_hwnd = nullptr) noexcept {
    if (const auto& ov = detail::MonitorMetricsOverrideStorage(); ov.has_value()) {
        return ov->dpi > 0 ? ov->dpi : 96u;
    }
    if (mon) {
        if (const HMODULE shcore = ::GetModuleHandleW(L"shcore.dll")) {
            // HRESULT GetDpiForMonitor(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);
            // MDT_EFFECTIVE_DPI = 0
            using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            const auto get_dpi = reinterpret_cast<GetDpiForMonitorFn>(
                ::GetProcAddress(shcore, "GetDpiForMonitor"));
            if (get_dpi) {
                UINT dpi_x = 96;
                UINT dpi_y = 96;
                if (SUCCEEDED(get_dpi(mon, 0, &dpi_x, &dpi_y)) && dpi_x > 0) {
                    return dpi_x;
                }
            }
        }
    }
    if (fallback_hwnd && ::IsWindow(fallback_hwnd)) {
        const UINT wnd_dpi = ::GetDpiForWindow(fallback_hwnd);
        if (wnd_dpi > 0) {
            return wnd_dpi;
        }
    }
    return 96u;
}

inline MonitorMetrics QueryMonitorForPoint(POINT screen_pt, HWND fallback_hwnd = nullptr) noexcept {
    if (const auto& ov = detail::MonitorMetricsOverrideStorage(); ov.has_value()) {
        return *ov;
    }

    MonitorMetrics result{};
    const HMONITOR mon = ::MonitorFromPoint(screen_pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && ::GetMonitorInfoW(mon, &mi)) {
        result.work_area = mi.rcWork;
    } else {
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &result.work_area, 0);
    }
    result.dpi = GetDpiForMonitorHandle(mon, fallback_hwnd);
    return result;
}

inline MonitorMetrics QueryActiveMonitor(HWND parent_hwnd = nullptr) noexcept {
    if (const auto& ov = detail::MonitorMetricsOverrideStorage(); ov.has_value()) {
        return *ov;
    }

    RECT rc{};
    if (parent_hwnd && ::IsWindow(parent_hwnd) && ::GetWindowRect(parent_hwnd, &rc)) {
        const POINT center{(rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2};
        return QueryMonitorForPoint(center, parent_hwnd);
    }
    POINT cursor_pt{0, 0};
    ::GetCursorPos(&cursor_pt);
    return QueryMonitorForPoint(cursor_pt, parent_hwnd);
}

// Clamp a window of given size into the target monitor's work_area (supports negative coordinates).
constexpr POINT ClampPointToWorkArea(POINT pos, int width, int height, const RECT& work_area) noexcept {
    const LONG max_x = std::max<LONG>(work_area.left, work_area.right - width);
    const LONG max_y = std::max<LONG>(work_area.top, work_area.bottom - height);
    return POINT{
        std::clamp<LONG>(pos.x, work_area.left, max_x),
        std::clamp<LONG>(pos.y, work_area.top, max_y)
    };
}

} // namespace qiqi
