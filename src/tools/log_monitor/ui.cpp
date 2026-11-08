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

void MonitorWindow::Impl::ReportCommandError(void* context, const char* operation, ULONG error) noexcept {
    auto* self = static_cast<Impl*>(context);
    std::array<wchar_t, 128> text{};
    size_t count = 0;
    for (const char ch : std::string_view(operation)) {
        if (count == text.size() - 1) break;
        gsl::at(text, count++) = gsl::narrow_cast<wchar_t>(ch);
    }
    self->diagnostics.Record(2, std::wstring_view(text.data(), count), error);
}

const wchar_t* MonitorWindow::Impl::CommandText(ControlId id) const noexcept {
    switch (id) {
    case Start: return collector.Running() ? Text(L"停止采集", L"Stop capture") : Text(L"开始采集", L"Start capture");
    case Pause: return paused ? Text(L"恢复自动滚动", L"Resume auto-scroll") : Text(L"暂停自动滚动", L"Pause auto-scroll");
    case Clear: return Text(L"清空", L"Clear");
    case Copy: return Text(L"复制", L"Copy");
    case Export: return Text(L"导出...", L"Export...");
    case Source: return diagnostic_view ? Text(L"输入法日志", L"IME events") : Text(L"工具诊断", L"Diagnostics");
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
    case Source: icon = diagnostic_view ? ToolIcon::Events : ToolIcon::Diagnostics; break;
    default: break;
    }
    const bool checked = (id == Pause && paused) || (id == Source && diagnostic_view) ||
        (id >= LanguageAuto && id <= LanguageEn &&
            std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(ui_language));
    const auto* selected_event = id == FilterActivity ? FirstSelectedEvent() : nullptr;
    const bool enabled = id == FilterActivity ? selected_event && selected_event->activity_id != GUID_NULL
        : (id != ClearActivity || filter.activity_id.has_value());
    return {gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), icon, enabled, checked};
}

void MonitorWindow::Impl::UpdateCommands() {
    const ToolCommand separator{};
    const std::array file{UiCommand(Start), UiCommand(Pause), separator, UiCommand(Export), separator, UiCommand(Exit)};
    const std::array edit{UiCommand(Copy), UiCommand(SelectAll), separator, UiCommand(Clear)};
    const std::array view{UiCommand(Source), separator, UiCommand(FilterActivity), UiCommand(ClearActivity)};
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
        UiCommand(Copy), UiCommand(Pause), UiCommand(Source)};
    command_ui.SetToolbar(Child(Toolbar), buttons);
    command_state.reset();
    RefreshCommandState();
}

void MonitorWindow::Impl::RefreshCommandState() {
    const auto* event = FirstSelectedEvent();
    const std::array state{collector.Running(), paused, diagnostic_view,
        event && event->activity_id != GUID_NULL, filter.activity_id.has_value()};
    if (command_state == state) return;
    for (const auto id : {Start, Pause, Source, FilterActivity, ClearActivity}) command_ui.UpdateCommand(hwnd, UiCommand(id));
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
    font = CreateUiFont(theme.ScalePx(14));
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
    ::SendMessageW(Child(Details), EM_SETLIMITTEXT, 1024 * 1024, 0);
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
    constexpr std::array widths{180, 140, 72, 72, 80, 145, 420};
    const HRESULT columns_hr = InsertListViewColumns(Child(List), columns, widths, theme.dpi);
    if (FAILED(columns_hr)) {
        Failure(L"ListView_InsertColumn", gsl::narrow_cast<ULONG>(columns_hr));
        return false;
    }
    ready = true;
    UpdateIcons();
    ApplyLanguage();
    UpdateTheme();
    if (!::SetTimer(hwnd, 1, 100, nullptr)) {
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
    int filter_top = command_ui.LayoutToolbar(Child(Toolbar), width, theme) + s(12);
    int filter_left = s(12);
    const auto place_filter = [&](ControlId label, ControlId input, int label_width, int input_width,
                                  bool fill_row = false) noexcept {
        const int label_px = s(label_width);
        const int input_px = s(input_width);
        if (filter_left > s(12) && filter_left + label_px + s(4) + input_px > width - s(12)) {
            filter_left = s(12);
            filter_top += s(38);
        }
        move(label, filter_left, filter_top + s(4), label_px, s(24));
        const int input_left = filter_left + label_px + s(4);
        move(input, input_left, filter_top,
             fill_row ? width - s(12) - input_left : input_px, s(fill_row ? 28 : 240));
        filter_left = input_left + input_px + s(10);
    };
    place_filter(ProcessLabel, Process, 60, 150);
    place_filter(PidLabel, Pid, 28, 82);
    place_filter(LevelLabel, Level, 44, 110);
    place_filter(CategoryLabel, Category, 72, 126);
    place_filter(SearchLabel, Search, 68, 200, true);
    const int top = filter_top + s(42);
    const int detail_limit_px = std::max(s(60), gsl::narrow_cast<int>(rect.bottom) - top - s(50) - s(60));
    detail_limit = std::max(60, gsl::narrow_cast<int>(
        gsl::narrow_cast<int64_t>(detail_limit_px) * 96 / theme.dpi));
    const int detail = std::min(s(std::clamp(detail_height, 60, detail_limit)), detail_limit_px);
    const int list_bottom = rect.bottom - detail - s(50);
    move(List, s(12), top, width - s(24), list_bottom - top);
    move(DetailsSplitter, s(12), list_bottom, width - s(24), s(8));
    move(Details, s(12), list_bottom + s(8), width - s(24), detail);
    move(Status, s(12), rect.bottom - s(30), width - s(24), s(24));
}

void MonitorWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(Process), Child(Pid), Child(Search),
        Child(Details), Child(Level), Child(Category)};
    if (!ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls)) {
        diagnostics.Record(3, L"SetWindowTheme(control/header)", ERROR_GEN_FAILURE);
    }
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
        CategoryText(std::to_underlying(log::Keyword::Config))};
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
        auto next_font = CreateUiFont(theme.ScalePx(14));
        if (next_font) {
            ::EnumChildWindows(hwnd, [](HWND child, LPARAM handle) noexcept -> BOOL {
                ::SendMessageW(child, WM_SETFONT, handle, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(next_font.Get()));
            font = std::move(next_font);
        } else {
            diagnostics.Record(2, L"Recreate DPI fonts", ::GetLastError());
        }
        for (int i = 0; i < 7; ++i) {
            ListView_SetColumnWidth(Child(List), i, ::MulDiv(ListView_GetColumnWidth(Child(List), i), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {Process, Pid, Level, Category}) {
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), theme.ScalePx(22));
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, 0, theme.ScalePx(22));
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
