#include "tools/log_monitor/internal.h"
#include "common/about_dialog.h"
#include "common/theme.h"
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
    } else if (message == WM_NOTIFY && lparam && !IsHighContrastEnabled() && IsSystemDarkTheme()) {
        const auto& notification = *reinterpret_cast<const NMHDR*>(lparam);
        if (notification.hwndFrom == ListView_GetHeader(window) && notification.code == NM_CUSTOMDRAW) {
            const auto& draw = *reinterpret_cast<const NMCUSTOMDRAW*>(lparam);
            if (draw.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw.dwDrawStage == CDDS_ITEMPREPAINT) {
                std::array<wchar_t, 256> text{};
                HDITEMW item{};
                item.mask = HDI_TEXT | HDI_FORMAT;
                item.pszText = text.data();
                item.cchTextMax = gsl::narrow_cast<int>(text.size());
                if (!Header_GetItem(notification.hwndFrom, gsl::narrow_cast<int>(draw.dwItemSpec), &item)) {
                    self->diagnostics.Record(2, L"Read dark list header", ERROR_INVALID_DATA);
                    return CDRF_DODEFAULT;
                }
                // Native dark header themes can retain black text on supported Windows builds.
                const ScopedSaveDC saved(draw.hdc);
                const ScopedSelectObject selected_font(draw.hdc, self->font.Get());
                ::SetDCBrushColor(draw.hdc, (draw.uItemState & CDIS_HOT) ? RGB(66, 70, 78) : RGB(54, 58, 66));
                const auto brush = ::GetSysColorBrush(COLOR_WINDOWFRAME);
                ::FillRect(draw.hdc, &draw.rc, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
                const RECT border{draw.rc.right - 1, draw.rc.top, draw.rc.right, draw.rc.bottom};
                ::FillRect(draw.hdc, &border, brush);
                RECT label = draw.rc;
                label.left += self->theme.ScalePx(6);
                label.right -= self->theme.ScalePx(6);
                ::SetTextColor(draw.hdc, RGB(238, 240, 245));
                ::SetBkMode(draw.hdc, TRANSPARENT);
                const UINT alignment = (item.fmt & HDF_RIGHT) ? DT_RIGHT : ((item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT);
                ::DrawTextW(draw.hdc, text.data(), -1, &label,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | alignment);
                return CDRF_SKIPDEFAULT;
            }
        }
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
    const HWND list = Child(List);
    if (screen_pt.x == -1 && screen_pt.y == -1) {
        RECT rc{};
        const int sel = ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
        if (sel >= 0 && ::SendMessageW(list, LVM_GETITEMRECT, gsl::narrow_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(&rc))) {
            screen_pt = {rc.left + theme.ScalePx(16), rc.bottom};
            ::ClientToScreen(list, &screen_pt);
        } else if (::GetWindowRect(list, &rc)) {
            screen_pt = {rc.left + theme.ScalePx(16), rc.top + theme.ScalePx(28)};
        }
    }
    const ScopedMenu menu(::CreatePopupMenu());
    if (!menu) {
        diagnostics.Record(2, L"CreatePopupMenu", ::GetLastError());
        return;
    }
    const auto selected_items = CollectSelectedEvents();
    const bool has_sel = !selected_items.empty();
    const bool has_rows = !rows.empty();
    const bool has_activity = has_sel && selected_items.front().activity_id != GUID_NULL;
    ::AppendMenuW(menu.Get(), MF_STRING | (has_sel ? MF_ENABLED : MF_GRAYED), 1,
        Text(L"复制选中日志项\tCtrl+C", L"Copy selected entries\tCtrl+C"));
    ::AppendMenuW(menu.Get(), MF_STRING | (has_rows ? MF_ENABLED : MF_GRAYED), 2,
        Text(L"全选\tCtrl+A", L"Select all\tCtrl+A"));
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.Get(), MF_STRING | (has_activity ? MF_ENABLED : MF_GRAYED), 3,
        Text(L"按当前活动关联筛选", L"Filter by selected activity"));
    ::AppendMenuW(menu.Get(), MF_STRING | (filter.activity_id.has_value() ? MF_ENABLED : MF_GRAYED), 4,
        Text(L"清除活动关联筛选", L"Clear activity filter"));
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    if (cmd == 1) CopySelection();
    else if (cmd == 2) SelectAllRows();
    else if (cmd == 3 && has_activity) {
        filter.activity_id = selected_items.front().activity_id;
        Refilter();
        UpdateStatus();
    } else if (cmd == 4) {
        filter.activity_id.reset();
        Refilter();
        UpdateStatus();
    }
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

void MonitorWindow::Impl::Command(int id, int notification) {
    switch (gsl::narrow_cast<ControlId>(id)) {
    case Start:
        if (collector.Running()) {
            collector.Stop();
        } else if (collector.Start()) {
            last_error.clear();
        }
        Tick();
        break;
    case Pause:
        paused = !paused;
        ::SetWindowTextW(Child(Pause), paused ? Text(L"恢复滚动", L"Resume scroll") : Text(L"暂停滚动", L"Pause scroll"));
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
        ::SetWindowTextW(Child(Source), diagnostic_view ? Text(L"输入法日志", L"IME events") : Text(L"工具诊断", L"Diagnostics"));
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
    case Language:
        if (notification == CBN_SELCHANGE) {
            const LRESULT choice = ::SendMessageW(Child(Language), CB_GETCURSEL, 0, 0);
            if (choice >= 0 && choice <= 2) {
                ui_language = gsl::narrow_cast<UiLanguage>(choice);
                ApplyLanguage();
            } else {
                diagnostics.Record(2, L"Invalid language selection", ERROR_INVALID_DATA);
            }
        }
        break;
    default: break;
    }
}

} // namespace qiqi::monitor
