#include "tools/log_monitor/internal.h"
#include "common/theme.h"
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
    const auto load = [&](int size) {
        return ScopedIcon(static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1),
            IMAGE_ICON, theme.ScalePx(size), theme.ScalePx(size), LR_DEFAULTCOLOR)));
    };
    auto loaded_small = load(16);
    auto loaded_big = load(48);
    if (!loaded_small || !loaded_big) {
        diagnostics.Record(2, L"Load DPI-scaled brand icons", ::GetLastError());
        return;
    }
    ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(loaded_small.Get()));
    ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(loaded_big.Get()));
    small_icon = std::move(loaded_small);
    big_icon = std::move(loaded_big);
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
        !Add(L"STATIC", Text(L"消息", L"Message"), SS_LEFT, SearchLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Process) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Pid) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Level) ||
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
    std::array<const wchar_t*, 6> columns{
        Text(L"时间 (UTC)", L"Time (UTC)"), Text(L"进程", L"Process"), L"PID", L"TID",
        Text(L"级别", L"Level"), Text(L"消息", L"Message")};
    constexpr std::array widths{190, 160, 80, 80, 90, 500};
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
    move(ProcessLabel, s(12), filter_top + s(4), s(55), s(24));
    move(Process, s(70), filter_top, s(170), s(240));
    move(PidLabel, s(254), filter_top + s(4), s(30), s(24));
    move(Pid, s(290), filter_top, s(90), s(240));
    move(LevelLabel, s(392), filter_top + s(4), s(50), s(24));
    move(Level, s(448), filter_top, s(150), s(240));
    if (width < s(940)) {
        filter_top += s(38);
        move(SearchLabel, s(12), filter_top + s(4), s(65), s(24));
        move(Search, s(80), filter_top, width - s(92), s(28));
    } else {
        move(SearchLabel, s(610), filter_top + s(4), s(65), s(24));
        move(Search, s(680), filter_top, width - s(692), s(28));
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
    theme.RefreshThemeBrushes();
    const bool contrast = IsHighContrastEnabled();
    const bool dark = !contrast && IsSystemDarkTheme();
    ApplySystemThemeToMenuAndWindow(hwnd, dark);
    for (const auto id : {List, Process, Pid, Search, Details, Language, Level}) {
        const HRESULT result = ::SetWindowTheme(Child(id), contrast ? nullptr : (dark ? L"DarkMode_Explorer" : L"Explorer"), nullptr);
        if (FAILED(result)) diagnostics.Record(3, L"SetWindowTheme(control)", gsl::narrow_cast<ULONG>(result));
    }
    const HRESULT header = ::SetWindowTheme(ListView_GetHeader(Child(List)),
        contrast ? nullptr : (dark ? L"DarkMode_ItemsView" : L"ItemsView"), nullptr);
    if (FAILED(header)) diagnostics.Record(3, L"SetWindowTheme(header)", gsl::narrow_cast<ULONG>(header));
    const COLORREF background = contrast ? ::GetSysColor(COLOR_WINDOW) :
        (dark ? RGB(43, 46, 52) : RGB(255, 255, 255));
    const COLORREF foreground = contrast ? ::GetSysColor(COLOR_WINDOWTEXT) :
        (dark ? RGB(238, 240, 245) : RGB(28, 32, 40));
    ListView_SetBkColor(Child(List), background);
    ListView_SetTextBkColor(Child(List), background);
    ListView_SetTextColor(Child(List), foreground);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void MonitorWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = std::format(L"{} v{} ({})", Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), version::kVersionW, version::kArchW);
    ::SetWindowTextW(hwnd, title.c_str());
    for (const auto& [id, text] : std::array{
        std::pair{Clear, Text(L"清空", L"Clear")}, std::pair{Copy, Text(L"复制", L"Copy")},
        std::pair{Export, Text(L"导出", L"Export")},
        std::pair{About, Text(L"关于", L"About")}, std::pair{ProcessLabel, Text(L"进程", L"Process")},
        std::pair{LevelLabel, Text(L"级别", L"Level")}, std::pair{SearchLabel, Text(L"消息", L"Message")},
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
    const auto& strings = GetUiStrings(ui_language);
    const std::array languages{strings.dlg_lang_auto, strings.dlg_lang_zh, strings.dlg_lang_en};
    replace(Language, languages);
    RefreshProcessChoices(true);
    const std::array<const wchar_t*, 6> columns{
        Text(L"时间 (UTC)", L"Time (UTC)"), Text(L"进程", L"Process"), L"PID", L"TID",
        Text(L"级别", L"Level"), Text(L"消息", L"Message")};
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
        for (int i = 0; i < 6; ++i) {
            ListView_SetColumnWidth(Child(List), i, ::MulDiv(ListView_GetColumnWidth(Child(List), i), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {Process, Pid, Level, Language}) {
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
