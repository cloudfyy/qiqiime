#include "tools/log_monitor/internal.h"
#include "tools/log_monitor/permissions.h"
#include "common/about_dialog.h"
#include "common/theme.h"
#include "common/window.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <format>
#include <system_error>

namespace qiqi::monitor {
using enum ControlId;

void MonitorWindow::Impl::ResizeDetails(int height) noexcept {
    detail_height = std::clamp(height, 60, detail_limit);
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
            const int height = rect.bottom - self->theme.ScalePx(50) -
                (point.y - self->splitter_drag_offset);
            self->ResizeDetails(::MulDiv(height, 96, gsl::narrow_cast<int>(self->theme.dpi)));
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
        case VK_UP: self->ResizeDetails(self->detail_height + 10); return 0;
        case VK_DOWN: self->ResizeDetails(self->detail_height - 10); return 0;
        case VK_HOME: self->ResizeDetails(60); return 0;
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
            line.top = (rect.bottom - self->theme.ScalePx(2)) / 2;
            line.bottom = line.top + self->theme.ScalePx(2);
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
    screen_pt = ResolveListViewContextMenuPoint(Child(List), screen_pt, theme);
    const ScopedMenu menu(::CreatePopupMenu());
    if (!menu) {
        diagnostics.Record(2, L"CreatePopupMenu", ::GetLastError());
        return;
    }
    const auto* selected_event = FirstSelectedEvent();
    const bool has_sel = selected_event != nullptr;
    const bool has_rows = !rows.empty();
    const bool has_activity = has_sel && selected_event->activity_id != GUID_NULL;
    for (const auto& [id, enabled] : std::array{
             std::pair{Copy, has_sel}, std::pair{SelectAll, has_rows}, std::pair{FilterActivity, has_activity},
             std::pair{ClearActivity, filter.activity_id.has_value()}}) {
        std::wstring text(CommandText(id));
        if (id == Copy) text += L"\tCtrl+C";
        if (id == SelectAll) text += L"\tCtrl+A";
        if (!::AppendMenuW(menu.Get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), std::to_underlying(id), text.c_str())) {
            diagnostics.Record(2, L"AppendMenuW(context)", ::GetLastError());
            return;
        }
    }
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    if (cmd) Command(cmd, 0);
}

void MonitorWindow::Impl::ExportSnapshot() {
    std::vector<Event> snapshot;
    snapshot.reserve(rows.size());
    for (const uint64_t id : rows) if (const auto* event = Current().Find(id)) snapshot.push_back(*event);
    if (::MessageBoxW(hwnd,
        Text(L"导出当前筛选快照可能包含进程名、路径和输入内容。请在分享前检查并脱敏。继续？",
             L"Export may contain process names, paths and input content. Review/redact before sharing. Continue?"),
        Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    std::array<wchar_t, 32768> path{};
    wcscpy_s(path.data(), path.size(), diagnostic_view ? L"qiqi-monitor-diagnostics.log" : L"qiqi-ime-events.log");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"导出筛选快照", L"Export filtered snapshot");
    dialog.lpstrFilter = Text(L"UTF-8 日志 (*.log)\0*.log\0所有文件\0*.*\0", L"UTF-8 log (*.log)\0*.log\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"log";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&dialog)) {
        const DWORD error = ::CommDlgExtendedError();
        if (error) Failure(L"GetSaveFileName", error);
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
    const ULONG error = LaunchElevatedMonitor(hwnd);
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
        filter.activity_id.reset();
        Refilter();
        UpdateStatus();
        break;
    case Start:
        if (collector.Running()) {
            collector.Stop();
        } else if (collector.Start()) {
            last_error.clear();
        } else if (const auto& failure = collector.LastStartFailure();
                   failure && RecoverCapturePermission(*failure)) {
            return;
        }
        Tick();
        break;
    case Pause:
        paused = !paused;
        RefreshCommandState();
        break;
    case Clear:
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
        diagnostic_view = !diagnostic_view;
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
        UpdateStatus();
        break;
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
