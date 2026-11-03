#include "tools/log_monitor/internal.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <format>
#include <system_error>

namespace qiqi::monitor {
using enum ControlId;

namespace {
std::wstring ReadText(HWND control) {
    const int size = ::GetWindowTextLengthW(control);
    std::wstring result(gsl::narrow_cast<size_t>(size) + 1, L'\0');
    const int copied = ::GetWindowTextW(control, result.data(), size + 1);
    result.resize(gsl::narrow_cast<size_t>(copied));
    return result;
}

} // namespace

void MonitorWindow::Impl::RefreshProcessChoices(bool force) {
    if (!force && (::SendMessageW(Child(Process), CB_GETDROPPEDSTATE, 0, 0) ||
                   ::SendMessageW(Child(Pid), CB_GETDROPPEDSTATE, 0, 0))) {
        process_choices_pending = true;
        return;
    }
    updating_filters = true;
    const auto restore = gsl::finally([this]() noexcept { updating_filters = false; });
    auto next = CollectProcessChoices(Current(), filter.process);
    const auto replace = [&](ControlId id, const std::vector<std::wstring>& items) {
        std::vector<const wchar_t*> text;
        text.reserve(items.size());
        for (const auto& item : items) text.push_back(item.c_str());
        const HRESULT hr = ReplaceComboBoxItems(Child(id), text);
        if (FAILED(hr)) throw std::system_error(gsl::narrow_cast<int>(hr),
            std::system_category(), "RefreshProcessChoices::ReplaceComboBoxItems");
    };
    if (next.names != process_choices.names) {
        // Preserve typed text and caret when refreshing the suggestion list.
        const LRESULT edit_selection = ::SendMessageW(Child(Process), CB_GETEDITSEL, 0, 0);
        replace(Process, next.names);
        ::SetWindowTextW(Child(Process), filter.process.c_str());
        ::SendMessageW(Child(Process), CB_SETEDITSEL, 0, edit_selection);
    }
    const bool single = !filter.process.empty() && next.pids.size() == 1;
    const bool was_single = automatic_pid;
    if (single) {
        filter.pid = next.pids.front();
    } else if (automatic_pid || (filter.pid &&
               std::find(next.pids.begin(), next.pids.end(), *filter.pid) == next.pids.end())) {
        filter.pid.reset();
    }
    automatic_pid = single;
    if (force || next.pids != process_choices.pids || single != was_single) {
        std::vector<std::wstring> pids;
        if (!single) pids.emplace_back(Text(L"全部", L"All"));
        for (const DWORD pid : next.pids) pids.push_back(std::to_wstring(pid));
        replace(Pid, pids);
    }
    int selected_pid = 0;
    if (filter.pid && !single) {
        const auto found = std::find(next.pids.begin(), next.pids.end(), *filter.pid);
        if (found != next.pids.end()) selected_pid = gsl::narrow_cast<int>(found - next.pids.begin()) + 1;
    }
    ::SendMessageW(Child(Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(selected_pid), 0);
    ::EnableWindow(Child(Pid), !single && !next.pids.empty());
    process_choices = std::move(next);
    process_choices_pending = false;
}

void MonitorWindow::Impl::Refilter() {
    const auto process = ReadText(Child(Process));
    if (process != filter.process) {
        filter.process = process;
        filter.pid.reset();
        automatic_pid = false;
    }
    RefreshProcessChoices();
    const LRESULT level = ::SendMessageW(Child(Level), CB_GETCURSEL, 0, 0);
    filter.level = level >= 0 ? gsl::narrow_cast<UCHAR>(level) : 0;
    filter.text = ReadText(Child(Search));
    RebuildRows();
}

void MonitorWindow::Impl::SyncSelectionFromList() {
    selected_ids.clear();
    const HWND list = Child(List);
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < rows.size()) {
            selected_ids.push_back(gsl::at(rows, gsl::narrow_cast<size_t>(index)));
        }
    }
    const int focused = ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
    if (focused >= 0 && gsl::narrow_cast<size_t>(focused) < rows.size()) {
        selected = gsl::at(rows, gsl::narrow_cast<size_t>(focused));
    } else {
        selected = selected_ids.empty() ? 0 : selected_ids.back();
    }
    UpdateDetails();
}

void MonitorWindow::Impl::SelectAllRows() {
    const HWND list = Child(List);
    if (rows.empty()) return;
    {
        updating_rows = true;
        const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    }
    SyncSelectionFromList();
}

void MonitorWindow::Impl::RebuildRows() {
    updating_rows = true;
    const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
    std::vector<uint64_t> next;
    next.reserve(Current().Events().size());
    for (const auto& event : Current().Events()) {
        if (filter.Matches(event)) next.push_back(event.id);
    }
    rows.swap(next);
    const HWND list = Child(List);
    ListView_SetItemCountEx(list, gsl::narrow_cast<int>(rows.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    std::vector<uint64_t> retained;
    retained.reserve(selected_ids.size());
    int focus_index = -1;
    auto selection = selected_ids.begin();
    for (size_t i = 0; i < rows.size(); ++i) {
        const uint64_t id = gsl::at(rows, i);
        while (selection != selected_ids.end() && *selection < id) ++selection;
        if (selection != selected_ids.end() && *selection == id) {
            retained.push_back(id);
            ListView_SetItemState(list, gsl::narrow_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
            if (id == selected || focus_index < 0) focus_index = gsl::narrow_cast<int>(i);
        }
    }
    selected_ids.swap(retained);
    if (focus_index >= 0 && gsl::narrow_cast<size_t>(focus_index) < rows.size()) {
        selected = gsl::at(rows, gsl::narrow_cast<size_t>(focus_index));
        ListView_SetItemState(list, focus_index, LVIS_FOCUSED, LVIS_FOCUSED);
    } else {
        selected = 0;
    }
    UpdateDetails();
    if (!paused && !rows.empty()) ListView_EnsureVisible(list, gsl::narrow_cast<int>(rows.size() - 1), FALSE);
    ::InvalidateRect(list, nullptr, FALSE);
}

void MonitorWindow::Impl::Tick() {
    bool changed = false;
    for (auto& event : pending.Take()) { events.Push(std::move(event)); changed = true; }
    for (auto& event : diagnostics.queue.Take()) {
        if (event.level <= 2) last_error = event.message.substr(0, 512);
        tools.Push(std::move(event));
        changed = true;
    }
    if (++ticks % 10 == 0) collector.PollLoss();
    if (changed || process_choices_pending) {
        RefreshProcessChoices();
        if (changed || !process_choices_pending) RebuildRows();
    }
    UpdateStatus();
}

void MonitorWindow::Impl::UpdateStatus() {
    ::SetWindowTextW(Child(Start), collector.Running() ? Text(L"停止采集", L"Stop") : Text(L"开始采集", L"Start"));
    auto status = std::format(
        L"{} | {}: {} / {} | {}: {} KiB | {}: {} | {}: {}/{} | {}: {} | {}: {}",
        collector.Running() ? Text(L"采集中", L"Capturing") : Text(L"已停止", L"Stopped"),
        Text(L"显示", L"Visible"), rows.size(), Current().Events().size(),
        Text(L"缓冲", L"Buffer"), Current().Bytes() / 1024,
        Text(L"本地淘汰", L"Local eviction"), diagnostic_view ?
            tools.Evicted() + diagnostics.queue.Evicted() : events.Evicted() + pending.Evicted(),
        Text(L"ETW 丢失(事件/缓冲)", L"ETW lost(events/buffers)"), collector.events_lost.load(), collector.buffers_lost.load(),
        Text(L"解码失败", L"Decode"), collector.decode_errors.load(), Text(L"内存异常", L"Memory"),
        collector.allocation_errors.load() + diagnostics.allocation_errors.load());
    if (!last_error.empty()) status = std::wstring(Text(L"错误: ", L"ERROR: ")) + last_error + L" | " + status;
    ::SetWindowTextW(Child(Status), status.c_str());
}

std::wstring MonitorWindow::Impl::Cell(const Event& event, int column) const {
    switch (column) {
    case 0: return FormatTime(event.time);
    case 1: return event.process;
    case 2: return std::to_wstring(event.pid);
    case 3: return std::to_wstring(event.tid);
    case 4: return LevelText(event.level);
    default: {
        std::wstring preview = event.message.substr(0, 1024);
        std::replace(preview.begin(), preview.end(), L'\r', L' ');
        std::replace(preview.begin(), preview.end(), L'\n', L' ');
        std::replace(preview.begin(), preview.end(), L'\0', L'\x2400');
        return preview;
    }
    }
}

void MonitorWindow::Impl::Notify(NMHDR& header) {
    if (header.idFrom != gsl::narrow_cast<UINT_PTR>(std::to_underlying(List))) return;
    if (header.code == LVN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMLVDISPINFOW*>(&header);
        if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 &&
            gsl::narrow_cast<size_t>(info.item.iItem) < rows.size()) {
            if (const auto* event = Current().Find(gsl::at(rows, gsl::narrow_cast<size_t>(info.item.iItem)))) {
                const auto text = Cell(*event, info.item.iSubItem);
                if (info.item.pszText && info.item.cchTextMax > 0) {
                    wcsncpy_s(info.item.pszText, gsl::narrow_cast<size_t>(info.item.cchTextMax),
                        text.c_str(), _TRUNCATE);
                }
            }
        }
    } else if (header.code == LVN_ITEMCHANGED && !updating_rows) {
        const auto& change = *reinterpret_cast<const NMLISTVIEW*>(&header);
        if ((change.uChanged & LVIF_STATE) &&
            ((change.uOldState ^ change.uNewState) & LVIS_SELECTED)) {
            SyncSelectionFromList();
        }
    } else if (header.code == LVN_ODSTATECHANGED && !updating_rows) {
        SyncSelectionFromList();
    }
}

std::vector<Event> MonitorWindow::Impl::CollectSelectedEvents() const {
    std::vector<Event> result;
    const HWND list = Child(List);
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < rows.size()) {
            if (const auto* event = Current().Find(gsl::at(rows, gsl::narrow_cast<size_t>(index)))) {
                result.push_back(*event);
            }
        }
    }
    if (result.empty() && selected != 0) {
        if (const auto* event = Current().Find(selected)) {
            result.push_back(*event);
        }
    }
    return result;
}

void MonitorWindow::Impl::UpdateDetails() {
    const auto items = CollectSelectedEvents();
    if (items.size() > 1) {
        std::wstring text = std::format(L"{}: {}\r\n", Text(L"已选择日志项", L"Selected entries"), items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) text += L"\r\n\r\n";
            const auto& event = gsl::at(items, i);
            text += FormatEventEntry(event, LevelText(event.level));
        }
        ::SetWindowTextW(Child(Details), text.c_str());
    } else if (items.size() == 1) {
        const auto& event = items.front();
        const auto text = FormatEventEntry(event, LevelText(event.level));
        ::SetWindowTextW(Child(Details), text.c_str());
    } else {
        ::SetWindowTextW(Child(Details), L"");
    }
}

} // namespace qiqi::monitor
