// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/log_monitor/internal.h"
#include "tools/log_monitor/permissions.h"
#include "common/theme.h"
#include "common/window.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <format>
#include <system_error>
#include <uxtheme.h>

namespace qiqi::monitor {
using enum ControlId;
namespace {
constexpr int kWindowMargin = 12;
constexpr int kFilterControlGap = 4;
constexpr int kFilterRowStride = 38;
constexpr int kFilterLabelHeight = 24;
constexpr int kSearchHeight = 28;
constexpr int kFilterDropdownHeight = 240;
constexpr int kFilterGroupGap = 10;
constexpr int kFilterSectionHeight = 42;
constexpr int kMinimumListHeight = 60;
constexpr int kSplitterHeight = 8;
constexpr int kStatusBottomOffset = 30;
constexpr int kStatusHeight = 24;
constexpr WPARAM kDetailsTextLimitChars = 1024 * 1024;
constexpr size_t kFilterControlTextCapacity = 128;
constexpr std::array kLogColumnWidths{180, 140, 72, 72, 80, 145, 420};
struct FilterSize {
    ControlId label;
    ControlId input;
    int label_width;
    int input_width;
    bool fill_row;
};
constexpr std::array kFilterSizes{
    FilterSize{ProcessLabel, Process, 60, 150, false},
    FilterSize{PidLabel, Pid, 28, 82, false},
    FilterSize{LevelLabel, Level, 44, 110, false},
    FilterSize{CategoryLabel, Category, 72, 126, false},
    FilterSize{SearchLabel, Search, 68, 200, true}};
constexpr std::array kMetricFilterSizes{
    FilterSize{MetricInstanceLabel, MetricInstanceFilter, 68, 220, false},
    FilterSize{MetricKindLabel, MetricKindFilter, 52, 150, false},
    FilterSize{MetricOutcomeLabel, MetricOutcomeFilter, 68, 130, false},
    FilterSize{MetricModeLabel, MetricModeFilter, 52, 140, false},
    FilterSize{MetricWindowLabel, MetricWindowFilter, 68, 160, false}};
}

void MonitorWindow::Impl::ReportCommandError(void* context, const char* operation, ULONG error) noexcept {
    auto* self = static_cast<Impl*>(context);
    std::array<wchar_t, kFilterControlTextCapacity> text{};
    size_t count = 0;
    for (const char ch : std::string_view(operation)) {
        if (count == text.size() - 1) break;
        gsl::at(text, count++) = gsl::narrow_cast<wchar_t>(ch);
    }
    self->diagnostics.Record(2, std::wstring_view(text.data(), count), error);
}

const wchar_t* MonitorWindow::Impl::CommandText(ControlId id) const noexcept {
    switch (id) {
    case Start:
        if (collector.Running()) return collector.CollectingMetrics() ?
            Text(L"停止采集 (Logs + Metrics)", L"Stop capture (Logs + Metrics)") :
            Text(L"停止采集 (Logs)", L"Stop capture (Logs)");
        return metrics_view ? Text(L"开始采集 (Logs + Metrics)", L"Start capture (Logs + Metrics)") :
            Text(L"开始采集 (Logs)", L"Start capture (Logs)");
    case Pause: return paused ? Text(L"恢复自动滚动", L"Resume auto-scroll") : Text(L"暂停自动滚动", L"Pause auto-scroll");
    case Clear: return metrics_view ? Text(L"清空指标", L"Clear metrics") : Text(L"清空", L"Clear");
    case Copy: return Text(L"复制", L"Copy");
    case Export: return metrics_view ? Text(L"导出指标 JSON...", L"Export Metrics JSON...") : Text(L"导出...", L"Export...");
    case Source: return Text(L"输入法日志", L"IME events");
    case DiagnosticsView: return Text(L"工具诊断", L"Diagnostics");
    case MetricsView: return Text(L"指标视图", L"Metrics view");
    case About: return Text(L"关于...", L"About...");
    case SelectAll: return Text(L"全选", L"Select All");
    case FilterActivity: return Text(L"按当前活动关联筛选", L"Filter by selected activity");
    case ClearActivity: return Text(L"清除活动关联筛选", L"Clear activity filter");
    case LanguageAuto: return GetUiStrings(ui_language).dlg_lang_auto;
    case LanguageZh: return GetUiStrings(ui_language).dlg_lang_zh;
    case LanguageEn: return GetUiStrings(ui_language).dlg_lang_en;
    case Exit: return Text(L"退出", L"Exit");
    default: return L"";
    }
}

ToolCommand MonitorWindow::Impl::UiCommand(ControlId id) const noexcept {
    ToolIcon icon = ToolIcon::None;
    switch (id) {
    case Start: icon = collector.Running() ? ToolIcon::Stop : ToolIcon::Start; break;
    case Pause: icon = paused ? ToolIcon::Resume : ToolIcon::Pause; break;
    case Clear: icon = ToolIcon::Delete; break;
    case Copy: icon = ToolIcon::Copy; break;
    case Export: icon = ToolIcon::Save; break;
    case Source: icon = ToolIcon::Events; break;
    case DiagnosticsView: icon = ToolIcon::Diagnostics; break;
    case MetricsView: icon = ToolIcon::Metrics; break;
    default: break;
    }
    const bool checked = (id == Start && collector.Running()) || (id == Pause && paused) ||
        (id == Source && !diagnostic_view && !metrics_view) ||
        (id == DiagnosticsView && diagnostic_view && !metrics_view) ||
        (id == MetricsView && metrics_view) ||
        (id >= LanguageAuto && id <= LanguageEn &&
            std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(ui_language));
    const auto* selected_event = id == FilterActivity ? FirstSelectedEvent() : nullptr;
    bool enabled = true;
    switch (id) {
    case SelectAll: enabled = !metrics_view; break;
    case Copy: enabled = !metrics_view || metric_selected.has_value(); break;
    case FilterActivity:
        enabled = metrics_view ? SelectedMetricActivity() != GUID_NULL : selected_event && selected_event->activity_id != GUID_NULL;
        break;
    case ClearActivity: enabled = !metrics_view && filter.activity_id.has_value(); break;
    default: break;
    }
    const bool radio = id == Source || id == DiagnosticsView || id == MetricsView;
    return {gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), icon, enabled, checked, radio};
}

void MonitorWindow::Impl::UpdateCommands() {
    const ToolCommand separator{};
    const std::array file{UiCommand(Start), UiCommand(Pause), separator, UiCommand(Export), separator, UiCommand(Exit)};
    const std::array edit{UiCommand(Copy), UiCommand(SelectAll), separator, UiCommand(Clear)};
    const std::array view{UiCommand(Source), UiCommand(MetricsView), UiCommand(DiagnosticsView),
        separator, UiCommand(FilterActivity), UiCommand(ClearActivity)};
    const std::array language{UiCommand(LanguageAuto), UiCommand(LanguageZh), UiCommand(LanguageEn)};
    const std::array help{UiCommand(About)};
    const std::array menus{
        ToolMenu{Text(L"文件(&F)", L"&File"), L'f', file},
        ToolMenu{Text(L"编辑(&E)", L"&Edit"), L'e', edit},
        ToolMenu{Text(L"查看(&V)", L"&View"), L'v', view},
        ToolMenu{Text(L"语言(&L)", L"&Language"), L'l', language, true},
        ToolMenu{Text(L"帮助(&H)", L"&Help"), L'h', help}};
    command_ui.SetMenu(hwnd, menus);
    const std::array buttons{UiCommand(Start), UiCommand(Export), UiCommand(Clear),
        UiCommand(Copy), UiCommand(Pause), separator, UiCommand(Source), UiCommand(MetricsView),
        UiCommand(DiagnosticsView)};
    command_ui.SetToolbar(Child(Toolbar), buttons);
    command_state.reset();
    RefreshCommandState();
}

void MonitorWindow::Impl::RefreshCommandState() {
    const auto* event = FirstSelectedEvent();
    const std::array state{collector.Running(), paused, diagnostic_view,
        metrics_view ? SelectedMetricActivity() != GUID_NULL : event && event->activity_id != GUID_NULL,
        filter.activity_id.has_value(), collector.CollectingMetrics(), metrics_view, metric_selected.has_value()};
    if (command_state == state) return;
    for (const auto id : {Start, Pause, Source, DiagnosticsView, FilterActivity, ClearActivity, MetricsView, Export, Clear, SelectAll, Copy})
        command_ui.UpdateCommand(hwnd, UiCommand(id));
    command_state = state;
}

void MonitorWindow::Impl::UpdateIcons() noexcept {
    if (!UpdateWindowBrandIcons(hwnd, instance, theme, small_icon, big_icon)) {
        diagnostics.Record(2, L"Load DPI-scaled brand icons", ::GetLastError());
    }
}

bool MonitorWindow::Impl::Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id) {
    const auto* child = CreateFluentChildControl(hwnd, instance, font.Get(), &theme,
        cls, text, style, 0, 0, 10, 10, std::to_underlying(id));
    if (!child) {
        const DWORD error = ::GetLastError();
        Failure(L"Create monitor control", error);
        return false;
    }
    return true;
}

bool MonitorWindow::Impl::CreateControls() {
    theme.Init(::GetDpiForWindow(hwnd));
    font = CreateUiFont(theme.ScalePx(kFluentBodyFontSize));
    if (!font) {
        Failure(L"Create monitor font", ::GetLastError());
        return false;
    }
    if (!Add(TOOLBARCLASSNAMEW, L"", TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | TBSTYLE_WRAPABLE |
            CCS_TOP | CCS_NODIVIDER | WS_TABSTOP, Toolbar) ||
        !Add(L"STATIC", Text(L"调整详情区高度", L"Resize details pane"), SS_NOTIFY | WS_TABSTOP, DetailsSplitter) ||
        !Add(L"STATIC", Text(L"进程", L"Process"), SS_LEFT, ProcessLabel) ||
        !Add(L"STATIC", L"PID", SS_LEFT, PidLabel) ||
        !Add(L"STATIC", Text(L"级别", L"Level"), SS_LEFT, LevelLabel) ||
        !Add(L"STATIC", Text(L"分类", L"Category"), SS_LEFT, CategoryLabel) ||
        !Add(L"STATIC", Text(L"消息", L"Message"), SS_LEFT, SearchLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Process) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Pid) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Level) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Category) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Search) ||
        !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS |
            WS_BORDER | WS_TABSTOP, List) ||
        !Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
            WS_HSCROLL | WS_BORDER | WS_TABSTOP, Details) ||
        !Add(L"STATIC", L"", SS_LEFT, Status)) return false;
    command_ui.InitializeToolbar(Child(Toolbar));
    ::SendMessageW(Child(Details), EM_SETLIMITTEXT, kDetailsTextLimitChars, 0);
    if (!::SetWindowSubclass(Child(DetailsSplitter), SplitterProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(details splitter)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    ListView_SetExtendedListViewStyle(Child(List), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    if (!::SetWindowSubclass(Child(List), ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(list header)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    std::array<const wchar_t*, 7> columns{
        Text(L"时间 (UTC)", L"Time (UTC)"), Text(L"进程", L"Process"), L"PID", L"TID",
        Text(L"级别", L"Level"), Text(L"分类 / 阶段", L"Category / Stage"), Text(L"消息", L"Message")};
    const HRESULT columns_hr = InsertListViewColumns(Child(List), columns, kLogColumnWidths, theme.dpi);
    if (FAILED(columns_hr)) {
        Failure(L"ListView_InsertColumn", gsl::narrow_cast<ULONG>(columns_hr));
        return false;
    }
    if (!CreateMetricControls()) return false;
    ready = true;
    UpdateIcons();
    ApplyLanguage();
    UpdateTheme();
    if (!::SetTimer(hwnd, kMonitorRefreshTimerId, kMonitorRefreshIntervalMs, nullptr)) {
        Failure(L"SetTimer", ::GetLastError());
        return false;
    }
    return true;
}

void MonitorWindow::Impl::Layout() noexcept {
    if (!ready) return;
    RECT rect{};
    ::GetClientRect(hwnd, &rect);
    const auto s = [this](int value) noexcept { return theme.ScalePx(value); };
    const int width = rect.right;
    const auto move = [&](ControlId id, int x, int y, int w, int h) noexcept {
        ::MoveWindow(Child(id), x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    int filter_top = command_ui.LayoutToolbar(Child(Toolbar), width, theme) + s(kWindowMargin);
    int filter_left = s(kWindowMargin);
    const auto place_filter = [&](ControlId label, ControlId input, int label_width, int input_width,
                                  bool fill_row = false) noexcept {
        const int label_px = s(label_width);
        const int input_px = s(input_width);
        if (filter_left > s(kWindowMargin) &&
            filter_left + label_px + s(kFilterControlGap) + input_px > width - s(kWindowMargin)) {
            filter_left = s(kWindowMargin);
            filter_top += s(kFilterRowStride);
        }
        move(label, filter_left, filter_top + s(kFilterControlGap), label_px, s(kFilterLabelHeight));
        const int input_left = filter_left + label_px + s(kFilterControlGap);
        move(input, input_left, filter_top,
             fill_row ? width - s(kWindowMargin) - input_left : input_px,
             s(fill_row ? kSearchHeight : kFilterDropdownHeight));
        filter_left = input_left + input_px + s(kFilterGroupGap);
    };
    for (const auto& filter_size : kFilterSizes) {
        ::ShowWindow(Child(filter_size.label), metrics_view ? SW_HIDE : SW_SHOW);
        ::ShowWindow(Child(filter_size.input), metrics_view ? SW_HIDE : SW_SHOW);
        if (!metrics_view) place_filter(filter_size.label, filter_size.input, filter_size.label_width,
            filter_size.input_width, filter_size.fill_row);
    }
    for (const auto& filter_size : kMetricFilterSizes) {
        ::ShowWindow(Child(filter_size.label), metrics_view ? SW_SHOW : SW_HIDE);
        ::ShowWindow(Child(filter_size.input), metrics_view ? SW_SHOW : SW_HIDE);
        if (metrics_view) place_filter(filter_size.label, filter_size.input, filter_size.label_width,
            filter_size.input_width, filter_size.fill_row);
    }
    ::ShowWindow(Child(List), metrics_view ? SW_HIDE : SW_SHOW);
    ::ShowWindow(Child(MetricList), metrics_view ? SW_SHOW : SW_HIDE);
    ::ShowWindow(Child(MetricSummaryStatus), metrics_view ? SW_SHOW : SW_HIDE);
    int top = filter_top + s(kFilterSectionHeight);
    if (metrics_view) {
        move(MetricSummaryStatus, s(kWindowMargin), top, width - s(2 * kWindowMargin), s(kMetricSummaryHeight));
        top += s(kMetricSummaryHeight);
    }
    const int detail_limit_px = std::max(s(kMonitorMinimumDetailHeight),
        gsl::narrow_cast<int>(rect.bottom) - top - s(kMonitorDetailsFooterSpace) - s(kMinimumListHeight));
    detail_limit = std::max(kMonitorMinimumDetailHeight, gsl::narrow_cast<int>(
        gsl::narrow_cast<int64_t>(detail_limit_px) * kMonitorLayoutReferenceDpi / theme.dpi));
    const int detail = std::min(
        s(std::clamp(detail_height, kMonitorMinimumDetailHeight, detail_limit)), detail_limit_px);
    const int list_bottom = rect.bottom - detail - s(kMonitorDetailsFooterSpace);
    move(List, s(kWindowMargin), top, width - s(2 * kWindowMargin), list_bottom - top);
    move(MetricList, s(kWindowMargin), top, width - s(2 * kWindowMargin), list_bottom - top);
    move(DetailsSplitter, s(kWindowMargin), list_bottom, width - s(2 * kWindowMargin), s(kSplitterHeight));
    move(Details, s(kWindowMargin), list_bottom + s(kSplitterHeight), width - s(2 * kWindowMargin), detail);
    move(Status, s(kWindowMargin), rect.bottom - s(kStatusBottomOffset), width - s(2 * kWindowMargin), s(kStatusHeight));
}

void MonitorWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(Process), Child(Pid), Child(Search),
        Child(Details), Child(Level), Child(Category)};
    if (!ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls)) {
        diagnostics.Record(3, L"SetWindowTheme(control/header)", ERROR_GEN_FAILURE);
    }
    const std::array metric_controls{Child(MetricList), Child(MetricInstanceFilter), Child(MetricKindFilter),
        Child(MetricOutcomeFilter), Child(MetricModeFilter), Child(MetricWindowFilter)};
    if (!ApplyToolWindowListAndControlsTheme(hwnd, Child(MetricList), theme, metric_controls))
        diagnostics.Record(3, L"SetWindowTheme(metric control/header)", ERROR_GEN_FAILURE);
    command_ui.ApplyTheme(hwnd);
}

void MonitorWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = FormatMonitorWindowTitle(ui_language);
    ::SetWindowTextW(hwnd, title.c_str());
    for (const auto& [id, text] : std::array{
        std::pair{ProcessLabel, Text(L"进程", L"Process")},
        std::pair{LevelLabel, Text(L"级别", L"Level")}, std::pair{CategoryLabel, Text(L"分类", L"Category")},
        std::pair{SearchLabel, Text(L"消息", L"Message")},
        std::pair{DetailsSplitter, Text(L"调整详情区高度", L"Resize details pane")}}) {
        ::SetWindowTextW(Child(id), text);
    }
    const auto replace = [&](ControlId id, std::span<const wchar_t* const> items) {
        const HRESULT result = ReplaceComboBoxItems(Child(id), items);
        if (FAILED(result)) throw std::system_error(gsl::narrow_cast<int>(result),
            std::system_category(), "ReplaceComboBoxItems");
    };
    const std::array levels{LevelText(0), LevelText(1), LevelText(2), LevelText(3), LevelText(4), LevelText(5)};
    replace(Level, levels);
    const std::array categories{
        CategoryText(0),
        CategoryText(std::to_underlying(log::Keyword::General)),
        CategoryText(std::to_underlying(log::Keyword::Lifecycle)),
        CategoryText(std::to_underlying(log::Keyword::Input)),
        CategoryText(std::to_underlying(log::Keyword::Composition)),
        CategoryText(std::to_underlying(log::Keyword::Dictionary)),
        CategoryText(std::to_underlying(log::Keyword::Sync)),
        CategoryText(std::to_underlying(log::Keyword::UI)),
        CategoryText(std::to_underlying(log::Keyword::Config)),
        CategoryText(std::to_underlying(log::Keyword::Metrics))};
    replace(Category, categories);
    RefreshProcessChoices(true);
    const std::array<const wchar_t*, 7> columns{
        Text(L"时间 (UTC)", L"Time (UTC)"), Text(L"进程", L"Process"), L"PID", L"TID",
        Text(L"级别", L"Level"), Text(L"分类 / 阶段", L"Category / Stage"), Text(L"消息", L"Message")};
    for (size_t i = 0; i < columns.size(); ++i) {
        std::wstring text(gsl::at(columns, i));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = text.data();
        ListView_SetColumn(Child(List), gsl::narrow_cast<int>(i), &column);
    }
    ApplyMetricLanguage();
    if (metrics_view) RebuildMetricRows();
    UpdateCommands();
    Layout();
    UpdateTheme();
    UpdateStatus();
    UpdateDetails();
    ::InvalidateRect(Child(List), nullptr, FALSE);
}

void MonitorWindow::Impl::UpdateDpi(UINT dpi, const RECT& proposed) noexcept {
    if (!ready || updating_display) return;
    updating_display = true;
    const auto restore = gsl::finally([this]() noexcept { updating_display = false; });
    const UINT previous = theme.dpi;
    if (dpi != previous) {
        theme.Init(dpi);
        auto next_font = CreateUiFont(theme.ScalePx(kFluentBodyFontSize));
        if (next_font) {
            ::EnumChildWindows(hwnd, [](HWND child, LPARAM handle) noexcept -> BOOL {
                ::SendMessageW(child, WM_SETFONT, handle, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(next_font.Get()));
            font = std::move(next_font);
        } else {
            diagnostics.Record(2, L"Recreate DPI fonts", ::GetLastError());
        }
        for (size_t i = 0; i < kLogColumnWidths.size(); ++i) {
            const int column = gsl::narrow_cast<int>(i);
            ListView_SetColumnWidth(Child(List), column, ::MulDiv(ListView_GetColumnWidth(Child(List), column), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (size_t i = 0; i < kMetricTableWidths.size(); ++i) {
            const int column = gsl::narrow_cast<int>(i);
            ListView_SetColumnWidth(Child(MetricList), column, ::MulDiv(ListView_GetColumnWidth(Child(MetricList), column),
                gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {Process, Pid, Level, Category, MetricInstanceFilter, MetricKindFilter,
                 MetricOutcomeFilter, MetricModeFilter, MetricWindowFilter}) {
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), theme.ScalePx(kFluentComboItemHeight));
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, 0, theme.ScalePx(kFluentComboItemHeight));
        }
        UpdateIcons();
    }
    const POINT center{proposed.left + (proposed.right - proposed.left) / 2, proposed.top + (proposed.bottom - proposed.top) / 2};
    const auto monitor = QueryMonitorForPoint(center, hwnd);
    const RECT rect = ClampWindowRectToWorkArea(proposed, monitor.work_area);
    if (!::SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE)) {
        diagnostics.Record(2, L"Move monitor window to work area", ::GetLastError());
    }
    Layout();
    UpdateTheme();
}

void MonitorWindow::Impl::RecoverDisplay() noexcept {
    if (!ready || updating_display || ::IsIconic(hwnd) || ::IsZoomed(hwnd)) return;
    RECT rect{};
    if (!::GetWindowRect(hwnd, &rect)) {
        diagnostics.Record(2, L"GetWindowRect(display change)", ::GetLastError());
        return;
    }
    const POINT center{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
    UpdateDpi(QueryMonitorForPoint(center, hwnd).dpi, rect);
}

} // namespace qiqi::monitor
