#include "tools/log_monitor/internal.h"
#include "common/theme.h"
#include "common/tool_window.h"
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

void MonitorWindow::Impl::UpdateIcons() {
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
    constexpr DWORD button = BS_OWNERDRAW | WS_TABSTOP;
    if (!Add(L"STATIC", L"", SS_LEFT, LanguageLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Language) ||
        !Add(L"BUTTON", Text(L"开始采集", L"Start"), button, Start) ||
        !Add(L"BUTTON", Text(L"暂停滚动", L"Pause scroll"), button, Pause) ||
        !Add(L"BUTTON", Text(L"清空", L"Clear"), button, Clear) ||
        !Add(L"BUTTON", Text(L"复制", L"Copy"), button, Copy) ||
        !Add(L"BUTTON", Text(L"导出", L"Export"), button, Export) ||
        !Add(L"BUTTON", Text(L"工具诊断", L"Diagnostics"), button, Source) ||
        !Add(L"BUTTON", Text(L"关于", L"About"), button, About) ||
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
    ::SendMessageW(Child(Details), EM_SETLIMITTEXT, 1024 * 1024, 0);
    if (!::SetWindowSubclass(Child(DetailsSplitter), SplitterProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(details splitter)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    if (!theme.d2d_target || !theme.fmt_btn) {
        diagnostics.Record(3, L"Direct2D/DirectWrite unavailable; using native button fallback", ERROR_NOT_SUPPORTED);
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
    for (size_t i = 0; i < columns.size(); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        std::wstring text(gsl::at(columns, i));
        column.pszText = text.data();
        column.cx = theme.ScalePx(gsl::at(widths, i));
        if (ListView_InsertColumn(Child(List), gsl::narrow_cast<int>(i), &column) < 0) {
            Failure(L"ListView_InsertColumn", ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
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
    constexpr std::array buttons{Start, Pause, Clear, Copy, Export, Source, About};
    constexpr int button_count = gsl::narrow_cast<int>(buttons.size());
    const int toolbar_columns = width >= s(1060) ? button_count : (width >= s(720) ? 4 : 3);
    const int toolbar_rows = (button_count + toolbar_columns - 1) / toolbar_columns;
    for (int index = 0; index < button_count; ++index) {
        move(gsl::at(buttons, index), s(12 + (index % toolbar_columns) * 108),
            s(12 + (index / toolbar_columns) * 40), s(100), s(32));
    }
    int filter_top = s(12 + toolbar_rows * 40);
    if (width >= s(760)) {
        move(LanguageLabel, width - s(296), s(18), s(76), s(24));
        move(Language, width - s(212), s(12), s(200), s(200));
    } else {
        move(LanguageLabel, s(12), filter_top + s(4), s(76), s(24));
        move(Language, s(92), filter_top, width - s(104), s(200));
        filter_top += s(38);
    }
    move(ProcessLabel, s(12), filter_top + s(4), s(48), s(24));
    move(Process, s(62), filter_top, s(150), s(240));
    move(PidLabel, s(222), filter_top + s(4), s(28), s(24));
    move(Pid, s(254), filter_top, s(82), s(240));
    move(LevelLabel, s(346), filter_top + s(4), s(44), s(24));
    move(Level, s(394), filter_top, s(110), s(240));
    move(CategoryLabel, s(514), filter_top + s(4), s(52), s(24));
    move(Category, s(570), filter_top, s(126), s(240));
    if (width < s(980)) {
        filter_top += s(38);
        move(SearchLabel, s(12), filter_top + s(4), s(58), s(24));
        move(Search, s(74), filter_top, width - s(86), s(28));
    } else {
        move(SearchLabel, s(706), filter_top + s(4), s(58), s(24));
        move(Search, s(768), filter_top, width - s(780), s(28));
    }
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
        Child(Details), Child(Language), Child(Level), Child(Category)};
    if (!ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls)) {
        diagnostics.Record(3, L"SetWindowTheme(control/header)", ERROR_GEN_FAILURE);
    }
}

void MonitorWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = std::format(L"{} v{} ({})", Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), version::kVersionW, version::kArchW);
    ::SetWindowTextW(hwnd, title.c_str());
    for (const auto& [id, text] : std::array{
        std::pair{Clear, Text(L"清空", L"Clear")}, std::pair{Copy, Text(L"复制", L"Copy")},
        std::pair{Export, Text(L"导出", L"Export")},
        std::pair{About, Text(L"关于", L"About")}, std::pair{ProcessLabel, Text(L"进程", L"Process")},
        std::pair{LevelLabel, Text(L"级别", L"Level")}, std::pair{CategoryLabel, Text(L"分类", L"Category")},
        std::pair{SearchLabel, Text(L"消息", L"Message")},
        std::pair{DetailsSplitter, Text(L"调整详情区高度", L"Resize details pane")},
        std::pair{LanguageLabel, Text(L"语言", L"Language")},
        std::pair{Pause, paused ? Text(L"恢复滚动", L"Resume scroll") : Text(L"暂停滚动", L"Pause scroll")},
        std::pair{Source, diagnostic_view ? Text(L"输入法日志", L"IME events") : Text(L"工具诊断", L"Diagnostics")}}) {
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
    const auto& strings = GetUiStrings(ui_language);
    const std::array languages{strings.dlg_lang_auto, strings.dlg_lang_zh, strings.dlg_lang_en};
    replace(Language, languages);
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
    UpdateStatus();
    UpdateDetails();
    ::InvalidateRect(Child(List), nullptr, FALSE);
}

void MonitorWindow::Impl::UpdateDpi(UINT dpi, const RECT& proposed) {
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
        for (const auto id : {Process, Pid, Level, Category, Language}) {
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

void MonitorWindow::Impl::RecoverDisplay() {
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
