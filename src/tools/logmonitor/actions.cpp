// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/logmonitor/internal.h"
#include "tools/logmonitor/permissions.h"
#include "common/dialog.h"
#include "common/dlgutil.h"
#include "common/fileutil.h"
#include "common/theme.h"
#include "common/window.h"
#include "common/uiutil.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <format>
#include <system_error>

namespace qiqi::monitor {
using enum ControlId;
namespace {
constexpr int kDetailKeyboardResizeStep = 10;
constexpr int kSplitterGripHeight = 2;
}

void MonitorWindow::Impl::ResizeDetails(int height) noexcept {
    detail_height = std::clamp(height, kMonitorMinimumDetailHeight, detail_limit);
    Layout();
    ::InvalidateRect(Child(DetailsSplitter), nullptr, FALSE);
}

LRESULT CALLBACK MonitorWindow::Impl::SplitterProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    switch (message) {
    case WM_SETCURSOR:
        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZENS));
        return TRUE;
    case WM_LBUTTONDOWN:
        ::SetFocus(window);
        self->splitter_drag_offset = PointFromLParam(lparam).y;
        ::SetCapture(window);
        if (::GetCapture() != window) {
            self->diagnostics.Record(2, L"Details splitter mouse capture failed");
        }
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (::GetCapture() == window) {
            POINT point = PointFromLParam(lparam);
            if (!::ClientToScreen(window, &point) || !::ScreenToClient(self->hwnd, &point)) {
                self->diagnostics.Record(2, L"Map details splitter mouse coordinates", ::GetLastError());
                return 0;
            }
            RECT rect{};
            if (!::GetClientRect(self->hwnd, &rect)) {
                self->diagnostics.Record(2, L"Get details splitter layout bounds", ::GetLastError());
                return 0;
            }
            const int footer = self->theme.ScalePx(kMonitorDetailsFooterSpace) +
                FluentControlHeight(self->theme, kMonitorSplitterHeight) - self->theme.ScalePx(kMonitorSplitterHeight);
            const int height = self->viewport.ContentHeight() - footer -
                (point.y + self->viewport.Offset() - self->splitter_drag_offset);
            self->ResizeDetails(::MulDiv(height, kMonitorMinimumDetailHeight,
                self->theme.ScalePx(kMonitorMinimumDetailHeight)));
        }
        return 0;
    case WM_LBUTTONUP:
        if (::GetCapture() == window && !::ReleaseCapture()) {
            self->diagnostics.Record(2, L"Release details splitter mouse capture", ::GetLastError());
        }
        return 0;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_UP: self->ResizeDetails(self->detail_height + kDetailKeyboardResizeStep); return 0;
        case VK_DOWN: self->ResizeDetails(self->detail_height - kDetailKeyboardResizeStep); return 0;
        case VK_HOME: self->ResizeDetails(kMonitorMinimumDetailHeight); return 0;
        case VK_END: self->ResizeDetails(self->detail_limit); return 0;
        default: break;
        }
        break;
    case WM_CAPTURECHANGED: case WM_SETFOCUS: case WM_KILLFOCUS:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        return PaintWindowClient(window, [window, self](HDC dc, const RECT& rect) noexcept {
            const HBRUSH background = IsHighContrastEnabled() ? ::GetSysColorBrush(COLOR_BTNFACE) :
                self->theme.surface_brush.Get();
            ::FillRect(dc, &rect, background ? background : ::GetSysColorBrush(COLOR_BTNFACE));
            RECT line = rect;
            line.top = (rect.bottom - self->theme.ScalePx(kSplitterGripHeight)) / 2;
            line.bottom = line.top + self->theme.ScalePx(kSplitterGripHeight);
            ::FillRect(dc, &line, ::GetSysColorBrush(
                ::GetCapture() == window || ::GetFocus() == window ? COLOR_HIGHLIGHT : COLOR_3DSHADOW));
        });
    case WM_NCDESTROY:
        ::RemoveWindowSubclass(window, SplitterProcedure, subclass);
        break;
    default: break;
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK MonitorWindow::Impl::ListProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, ListProcedure, subclass);
    } else if (message == WM_KEYDOWN && self && self->ready && (::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        if (wparam == 'C' || wparam == VK_INSERT) {
            try {
                self->CopySelection();
            } catch (const std::exception&) {
                self->diagnostics.Record(2, L"Copy keyboard shortcut failed", ERROR_UNHANDLED_EXCEPTION);
            }
            return 0;
        }
        if (wparam == 'A') {
            try {
                self->SelectAllRows();
            } catch (const std::exception&) {
                self->diagnostics.Record(2, L"Select all shortcut failed", ERROR_UNHANDLED_EXCEPTION);
            }
            return 0;
        }
    } else if (message == WM_NOTIFY && self) {
        bool header_failed = false;
        const auto drawn = TryCustomDrawDarkListViewHeader(window, lparam, self->font.Get(), self->theme, &header_failed);
        if (header_failed) {
            self->diagnostics.Record(2, L"Read dark list header", ERROR_INVALID_DATA);
        }
        if (drawn) return *drawn;
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

void MonitorWindow::Impl::CopySelection() {
    if (metrics_view) {
        if (!metric_selected) {
            diagnostics.Record(3, L"Copy metrics requires a selected dimension", ERROR_NOT_FOUND);
            return;
        }
        const ULONG error = CopyTextToClipboard(hwnd, MetricDetails());
        if (error != ERROR_SUCCESS) Failure(Text(L"复制指标失败", L"Copy metrics failed"), error);
        return;
    }
    const auto items = CollectSelectedEvents();
    if (items.empty()) return;
    const auto text = FormatEventsForClipboard(items);
    const ULONG error = CopyTextToClipboard(hwnd, text);
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"复制到剪贴板失败", L"Copy to clipboard failed"), error);
    } else {
        diagnostics.Record(4, std::format(L"Copied {} log record(s) to clipboard", items.size()));
    }
}

void MonitorWindow::Impl::ShowListContextMenu(POINT screen_pt) {
    screen_pt = ResolveListViewContextMenuPoint(Child(metrics_view ? MetricList : List), screen_pt, theme);
    const auto* selected_event = FirstSelectedEvent();
    const bool has_sel = metrics_view ? metric_selected.has_value() : selected_event != nullptr;
    const bool has_rows = !metrics_view && !rows.empty();
    const bool has_activity = metrics_view ? SelectedMetricActivity() != GUID_NULL : has_sel && selected_event->activity_id != GUID_NULL;
    std::vector<ToolCommand> commands;
    for (const auto& [id, enabled] : std::array{
             std::pair{Copy, has_sel}, std::pair{SelectAll, has_rows}, std::pair{FilterActivity, has_activity},
             std::pair{ClearActivity, !metrics_view && filter.activity_id.has_value()}, std::pair{Clear, true}}) {
        commands.push_back({gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), ToolIcon::None, enabled});
    }
    const UINT cmd = command_ui.TrackContextMenu(hwnd, screen_pt, commands);
    if (cmd) Command(cmd, 0);
}

void MonitorWindow::Impl::ExportSnapshot() {
    if (metrics_view) {
        ExportMetricsSnapshot();
        return;
    }
    std::vector<Event> snapshot;
    snapshot.reserve(rows.size());
    for (const uint64_t id : rows) if (const auto* event = Current().Find(id)) snapshot.push_back(*event);
    if (::MessageBoxW(hwnd,
        Text(L"导出当前筛选快照可能包含进程名、路径和输入内容。请在分享前检查并脱敏。继续？",
             L"Export may contain process names, paths and input content. Review/redact before sharing. Continue?"),
        Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    std::array<wchar_t, kWin32PathBufferChars> path{};
    wcscpy_s(path.data(), path.size(), diagnostic_view ? L"qiqi-monitor-diagnostics.log" : L"qiqi-ime-events.log");
    const auto selection = dialog_detail::PromptForFile(hwnd,
        Text(L"导出筛选快照", L"Export filtered snapshot"),
        Text(L"UTF-8 日志 (*.log)\0*.log\0所有文件\0*.*\0", L"UTF-8 log (*.log)\0*.log\0All files\0*.*\0"),
        path, dialog_detail::FileDialogMode::Save, L"log");
    if (!selection) {
        if (selection.status == dialog_detail::FileDialogStatus::Failed) Failure(L"GetSaveFileName", selection.error);
        return;
    }
    const auto bytes = ExportEvents(snapshot, diagnostic_view ? L"QiQiIME.LogMonitor" : L"QiQiIME.TraceLogging");
    const ULONG error = WriteExport(path.data(), bytes);
    if (error) {
        Failure(Text(L"导出失败，目标文件可能不完整；内存日志仍保留", L"Export failed; target may be partial. Buffered events retained"), error);
    } else {
        diagnostics.Record(4, std::format(L"Export completed: {} records", snapshot.size()));
    }
}

bool MonitorWindow::Impl::RecoverCapturePermission(const CaptureStartFailure& failure) {
    if (!failure.IsPermissionDenied()) return false;
    Tick();
    CapturePermissions permissions;
    const ULONG permission_error = QueryCapturePermissions(permissions);
    if (permission_error != ERROR_SUCCESS) {
        Failure(Text(L"无法检查监控器令牌权限；请查看工具诊断中的采集错误",
                     L"Cannot inspect monitor token permissions; see diagnostics for the capture error"), permission_error);
        return false;
    }
    if (permissions.elevated) {
        Failure(std::format(L"{}: {}", failure.operation, Text(
            L"监控器已提升权限，仍被拒绝访问。请由管理员检查 ETW 会话和提供程序权限策略。",
            L"The monitor is already elevated. Ask an administrator to inspect ETW session and provider permissions.")),
            failure.error);
        return false;
    }
    bool accepted = false;
    const HRESULT result = ConfirmElevatedRestart(hwnd, ui_language, failure, accepted, !events.Events().empty());
    if (FAILED(result)) {
        Failure(L"TaskDialogIndirect(elevated restart)", gsl::narrow_cast<ULONG>(result));
        return false;
    }
    if (!accepted) {
        diagnostics.Record(4, L"Elevated restart declined; original monitor and buffered logs retained");
        return false;
    }
    if (!collector.Stop()) return false;
    const ULONG error = LaunchElevatedMonitor(hwnd, ui_language);
    if (error == ERROR_CANCELLED) {
        diagnostics.Record(3, L"Elevated restart cancelled by Windows; original monitor and buffered logs retained", error);
        return false;
    }
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"以管理员身份重启失败；原窗口和内存日志保留",
                     L"Elevated restart failed; original window and buffered logs retained"), error);
        return false;
    }
    diagnostics.Record(4, L"Elevated monitor launched; closing original monitor");
    ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
    return true;
}

void MonitorWindow::Impl::Command(int id, int notification) {
    switch (gsl::narrow_cast<ControlId>(id)) {
    case Exit: ::SendMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case SelectAll: SelectAllRows(); break;
    case FilterActivity: {
        if (metrics_view) {
            const GUID activity = SelectedMetricActivity();
            if (activity == GUID_NULL) {
                diagnostics.Record(3, L"Metric activity is unavailable", ERROR_NOT_FOUND);
                break;
            }
            diagnostic_view = false;
            filter = {};
            filter.activity_id = activity;
            ::SetWindowTextW(Child(Process), L"");
            ::SetWindowTextW(Child(Search), L"");
            ::SendMessageW(Child(Level), CB_SETCURSEL, 0, 0);
            ::SendMessageW(Child(Category), CB_SETCURSEL, 0, 0);
            automatic_pid = false;
            Refilter();
            ShowMetrics(false);
            break;
        }
        const auto* event = FirstSelectedEvent();
        if (event && event->activity_id != GUID_NULL) {
            filter.activity_id = event->activity_id;
            Refilter();
            UpdateStatus();
        } else {
            diagnostics.Record(3, L"Activity filter requires a selected activity", ERROR_NOT_FOUND);
        }
        break;
    }
    case ClearActivity:
        if (metrics_view) {
            diagnostics.Record(3, L"Clear activity filter requires log view", ERROR_NOT_SUPPORTED);
            break;
        }
        filter.activity_id.reset();
        Refilter();
        UpdateStatus();
        break;
    case Start:
        if (collector.Running()) {
            collector.Stop();
        } else if (collector.Start(metrics_view)) {
            last_error.clear();
        } else if (const auto& failure = collector.LastStartFailure();
                   failure && RecoverCapturePermission(*failure)) {
            return;
        }
        Tick();
        break;
    case MetricsView:
        if (!metrics_view) ShowMetrics(true);
        break;
    case Pause:
        paused = !paused;
        RefreshCommandState();
        break;
    case Clear:
        if (metrics_view) {
            Tick();
            metrics.Clear();
            metric_selected.reset();
            RebuildMetricRows();
            UpdateStatus();
            break;
        }
        Current().Clear();
        selected = 0;
        selected_ids.clear();
        RefreshProcessChoices();
        RebuildRows();
        UpdateStatus();
        break;
    case Copy: CopySelection(); break;
    case Export: ExportSnapshot(); break;
    case Source:
    case DiagnosticsView: {
        const bool next_diagnostic = id == std::to_underlying(DiagnosticsView);
        if (next_diagnostic == diagnostic_view) {
            if (metrics_view) ShowMetrics(false);
            break;
        }
        diagnostic_view = next_diagnostic;
        selected = 0;
        selected_ids.clear();
        ::SetWindowTextW(Child(Process), L"");
        filter.pid.reset();
        filter.activity_id.reset();
        automatic_pid = false;
        ::SetWindowTextW(Child(Search), L"");
        ::SendMessageW(Child(Level), CB_SETCURSEL, 0, 0);
        ::SendMessageW(Child(Category), CB_SETCURSEL, 0, 0);
        Refilter();
        ShowMetrics(false);
        break;
    }
    case About: {
        if (!ShowAboutDialog(instance, hwnd, ui_language, true)) {
            diagnostics.Record(2, L"About dialog creation or message loop did not complete");
        }
        break;
    }
    case Process:
        if (!updating_filters && (notification == CBN_EDITCHANGE || notification == CBN_SELCHANGE)) {
            if (notification == CBN_SELCHANGE) {
                const LRESULT index = ::SendMessageW(Child(Process), CB_GETCURSEL, 0, 0);
                if (index < 0 || gsl::narrow_cast<size_t>(index) >= process_choices.names.size()) {
                    diagnostics.Record(2, L"Invalid process selection", ERROR_INVALID_DATA);
                    break;
                }
                ::SetWindowTextW(Child(Process), gsl::at(process_choices.names, gsl::narrow_cast<size_t>(index)).c_str());
            }
            Refilter();
        } else if (!updating_filters && notification == CBN_CLOSEUP) {
            Refilter();
        }
        break;
    case Pid:
        if (!updating_filters && notification == CBN_SELCHANGE) {
            const LRESULT index = ::SendMessageW(Child(Pid), CB_GETCURSEL, 0, 0);
            if (index < 0 || automatic_pid ||
                gsl::narrow_cast<size_t>(index) > process_choices.pids.size()) {
                diagnostics.Record(2, L"Invalid PID selection", ERROR_INVALID_DATA);
                break;
            }
            filter.pid = index == 0 ? std::nullopt :
                std::optional<DWORD>(gsl::at(process_choices.pids, gsl::narrow_cast<size_t>(index - 1)));
            Refilter();
        } else if (!updating_filters && notification == CBN_CLOSEUP) {
            Refilter();
        }
        break;
    case Search:
        if (notification == EN_CHANGE) Refilter();
        break;
    case Level: if (notification == CBN_SELCHANGE) Refilter(); break;
    case Category: if (notification == CBN_SELCHANGE) Refilter(); break;
    case MetricInstanceFilter: case MetricKindFilter: case MetricOutcomeFilter:
    case MetricModeFilter: case MetricWindowFilter:
        if (notification == CBN_SELCHANGE) RefilterMetrics();
        break;
    case LanguageAuto:
    case LanguageZh:
    case LanguageEn:
        ui_language = gsl::narrow_cast<UiLanguage>(id - std::to_underlying(LanguageAuto));
        ApplyLanguage();
        break;
    default: break;
    }
}

} // namespace qiqi::monitor
