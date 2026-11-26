// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/fileutil.h"

#include <shellscalingapi.h>
#include <gsl/gsl>

#include <bit>
#include <format>
#include <new>
#include <stdexcept>
#include <vector>

namespace qiqi::diag::detail {
namespace {

struct MonitorRecord {
    RECT bounds{};
    RECT work{};
    bool primary = false;
    std::wstring device;
    UINT dpi_x = 0;
    UINT dpi_y = 0;
    HRESULT dpi_result = E_NOTIMPL;
    DEVICE_SCALE_FACTOR scale = SCALE_100_PERCENT;
    HRESULT scale_result = E_NOTIMPL;
};

struct MonitorState {
    std::vector<MonitorRecord> records;
    HRESULT first_failure = S_OK;
    HRESULT collection_failure = S_OK;
};

BOOL CALLBACK EnumMonitorCallback(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) noexcept {
    auto* state = std::bit_cast<MonitorState*>(parameter);
    try {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(MONITORINFOEXW);
        if (::GetMonitorInfoW(monitor, &info) == FALSE) {
            if (SUCCEEDED(state->first_failure)) {
                state->first_failure = HRESULT_FROM_WIN32(::GetLastError());
            }
            return TRUE;
        }
        MonitorRecord record{};
        record.bounds = info.rcMonitor;
        record.work = info.rcWork;
        record.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        record.device = info.szDevice;
        record.dpi_result = ::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &record.dpi_x, &record.dpi_y);
        record.scale_result = ::GetScaleFactorForMonitor(monitor, &record.scale);
        state->records.push_back(std::move(record));
        return TRUE;
    } catch (const std::bad_alloc&) {
        state->collection_failure = E_OUTOFMEMORY;
        return FALSE;
    } catch (const std::length_error&) {
        state->collection_failure = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }
}

std::string DpiAwarenessName(PROCESS_DPI_AWARENESS value) noexcept {
    switch (value) {
    case PROCESS_DPI_UNAWARE:
        return "unaware";
    case PROCESS_SYSTEM_DPI_AWARE:
        return "system-aware";
    case PROCESS_PER_MONITOR_DPI_AWARE:
        return "per-monitor-aware";
    default:
        return "unknown";
    }
}

Json RectJson(const RECT& rect) {
    return Json{
        {"left", rect.left},
        {"top", rect.top},
        {"right", rect.right},
        {"bottom", rect.bottom},
        {"width", gsl::narrow_cast<int64_t>(rect.right) - rect.left},
        {"height", gsl::narrow_cast<int64_t>(rect.bottom) - rect.top},
    };
}

} // namespace

void CollectDisplayInformation(Json& stage) {
    Json display = Json::object();
    display["system_dpi"] = ::GetDpiForSystem();
    display["virtual_screen_bounds"] = RectJson(RECT{
        ::GetSystemMetrics(SM_XVIRTUALSCREEN),
        ::GetSystemMetrics(SM_YVIRTUALSCREEN),
        ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
        ::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN),
    });

    PROCESS_DPI_AWARENESS awareness = PROCESS_DPI_UNAWARE;
    const HRESULT awareness_result = ::GetProcessDpiAwareness(nullptr, &awareness);
    AddCheck(stage, CheckResult("display_dpi_awareness", awareness_result,
                                FAILED(awareness_result) ? "warning" : ""));
    if (SUCCEEDED(awareness_result)) {
        display["process_dpi_awareness"] = DpiAwarenessName(awareness);
    }

    MonitorState monitors;
    if (::EnumDisplayMonitors(nullptr, nullptr, EnumMonitorCallback, std::bit_cast<LPARAM>(&monitors)) ==
            FALSE &&
        SUCCEEDED(monitors.collection_failure)) {
        monitors.first_failure = HRESULT_FROM_WIN32(::GetLastError());
    }
    if (FAILED(monitors.collection_failure)) {
        AddCheck(stage, CheckResult("display_monitors", monitors.collection_failure));
    } else {
        AddCheck(stage, CheckResult("display_monitors",
                                    monitors.records.empty() ? HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
                                                             : monitors.first_failure,
                                    FAILED(monitors.first_failure) ? "warning" : ""));
    }
    Json monitor_array = Json::array();
    for (const MonitorRecord& record : monitors.records) {
        Json monitor{
            {"device", WideToUtf8(record.device)},
            {"bounds", RectJson(record.bounds)},
            {"work_area", RectJson(record.work)},
            {"primary", record.primary},
            {"dpi_x", record.dpi_x},
            {"dpi_y", record.dpi_y},
            {"dpi_inspection",
             CheckResult("effective_dpi", record.dpi_result, FAILED(record.dpi_result) ? "warning" : "")},
            {"scale_percent", std::to_underlying(record.scale)},
            {"scale_inspection",
             CheckResult("scale_factor", record.scale_result, FAILED(record.scale_result) ? "warning" : "")},
        };
        monitor_array.push_back(std::move(monitor));
    }
    display["monitors"] = std::move(monitor_array);

    Json adapters = Json::array();
    for (DWORD adapter_index = 0; adapter_index < 32; ++adapter_index) {
        DISPLAY_DEVICEW adapter{.cb = sizeof(DISPLAY_DEVICEW)};
        if (::EnumDisplayDevicesW(nullptr, adapter_index, &adapter, 0) == FALSE) {
            break;
        }
        Json adapter_json{
            {"name", WideToUtf8(adapter.DeviceName)},
            {"description", WideToUtf8(adapter.DeviceString)},
            {"primary", (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0},
            {"attached_to_desktop", (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0},
            {"mirroring", (adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0},
            {"monitors", Json::array()},
        };
        for (DWORD monitor_index = 0; monitor_index < 16; ++monitor_index) {
            DISPLAY_DEVICEW monitor{.cb = sizeof(DISPLAY_DEVICEW)};
            if (::EnumDisplayDevicesW(adapter.DeviceName, monitor_index, &monitor, 0) == FALSE) {
                break;
            }
            adapter_json["monitors"].push_back(Json{
                {"name", WideToUtf8(monitor.DeviceName)},
                {"description", WideToUtf8(monitor.DeviceString)},
            });
        }
        adapters.push_back(std::move(adapter_json));
    }
    display["adapters"] = std::move(adapters);
    stage["display"] = std::move(display);
}

} // namespace qiqi::diag::detail
