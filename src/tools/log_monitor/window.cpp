#include "tools/log_monitor/window.h"
#include "tools/log_monitor/collector.h"
#include "common/d2d_controls.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <format>
#include <system_error>
#include <uxtheme.h>

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

struct MonitorWindow::Impl {
    Diagnostics diagnostics;
    EventQueue pending;
    Collector collector{pending, diagnostics};
    EventBuffer events;
    EventBuffer tools{512, 1024 * 1024};
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    ScopedIcon small_icon;
    ScopedIcon big_icon;
    UiLanguage ui_language = UiLanguage::Auto;
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    MonitorWindow* owner = nullptr;
    std::vector<uint64_t> rows;
    std::vector<uint64_t> selected_ids;
    Filter filter;
    std::wstring last_error;
    uint64_t selected = 0;
    unsigned int ticks = 0;
    bool paused = false;
    bool diagnostic_view = false;
    bool valid_filter = true;
    bool ready = false;
    bool updating_rows = false;
    bool updating_display = false;
    int detail_height = 130; // Logical pixels.

    EventBuffer& Current() noexcept { return diagnostic_view ? tools : events; }
    const EventBuffer& Current() const noexcept { return diagnostic_view ? tools : events; }
    HWND Child(ControlId id) const noexcept { return ::GetDlgItem(hwnd, std::to_underlying(id)); }
    const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const noexcept {
        return ResolveUiLanguage(ui_language) == UiLanguage::EnUs ? en : zh;
    }

    const wchar_t* LevelText(UCHAR level) const noexcept {
        switch (level) {
        case 1: return Text(L"严重", L"Critical");
        case 2: return Text(L"错误", L"Error");
        case 3: return Text(L"警告", L"Warning");
        case 4: return Text(L"信息", L"Info");
        case 5: return Text(L"详细", L"Verbose");
        default: return Text(L"全部", L"All");
        }
    }

    void UpdateIcons() {
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

    void Failure(std::wstring_view operation, ULONG error) {
        diagnostics.Record(2, operation, error);
        const auto message = std::format(L"{}\r\n{}={} (0x{:08X})", operation, Text(L"错误码", L"Error"), error, error);
        ::MessageBoxW(hwnd, message.c_str(), Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), MB_OK | MB_ICONERROR);
    }

    bool Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id) {
        const auto* child = CreateFluentChildControl(hwnd, instance, font.Get(), &theme,
            cls, text, style, 0, 0, 10, 10, std::to_underlying(id));
        if (!child) {
            const DWORD error = ::GetLastError();
            Failure(L"Create monitor control", error);
            return false;
        }
        return true;
    }

    static LRESULT CALLBACK ListProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
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

    bool CreateControls() {
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
            !Add(L"STATIC", Text(L"详情高度", L"Details"), SS_LEFT, DetailsSizeLabel) ||
            !Add(TRACKBAR_CLASSW, L"", TBS_NOTICKS | WS_TABSTOP, DetailsSize) ||
            !Add(L"STATIC", Text(L"进程", L"Process"), SS_LEFT, ProcessLabel) ||
            !Add(L"STATIC", L"PID", SS_LEFT, PidLabel) ||
            !Add(L"STATIC", Text(L"级别", L"Level"), SS_LEFT, LevelLabel) ||
            !Add(L"STATIC", Text(L"消息", L"Message"), SS_LEFT, SearchLabel) ||
            !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Process) ||
            !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Pid) ||
            !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Level) ||
            !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, Search) ||
            !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS |
                WS_BORDER | WS_TABSTOP, List) ||
            !Add(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                WS_HSCROLL | WS_BORDER | WS_TABSTOP, Details) ||
            !Add(L"STATIC", L"", SS_LEFT, Status)) return false;
        ::SendMessageW(Child(Details), EM_SETLIMITTEXT, 1024 * 1024, 0);
        ::SendMessageW(Child(DetailsSize), TBM_SETRANGE, TRUE, MAKELPARAM(60, 400));
        ::SendMessageW(Child(DetailsSize), TBM_SETPOS, TRUE, detail_height);
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

    void Layout() {
        if (!ready) return;
        RECT rect{};
        ::GetClientRect(hwnd, &rect);
        const auto s = [this](int value) { return theme.ScalePx(value); };
        const int width = rect.right;
        const auto move = [&](ControlId id, int x, int y, int w, int h) {
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
        move(DetailsSizeLabel, s(12), filter_top + s(4), s(90), s(24));
        move(DetailsSize, s(110), filter_top, width - s(122), s(30));
        filter_top += s(38);
        move(ProcessLabel, s(12), filter_top + s(4), s(55), s(24));
        move(Process, s(70), filter_top, s(170), s(28));
        move(PidLabel, s(254), filter_top + s(4), s(30), s(24));
        move(Pid, s(290), filter_top, s(90), s(28));
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
        const int detail = std::min(s(detail_height), std::max(s(60), (gsl::narrow_cast<int>(rect.bottom) - top) / 2));
        const int list_bottom = rect.bottom - detail - s(50);
        move(List, s(12), top, width - s(24), list_bottom - top);
        move(Details, s(12), list_bottom + s(8), width - s(24), detail);
        move(Status, s(12), rect.bottom - s(30), width - s(24), s(24));
    }

    void UpdateTheme() noexcept {
        if (!ready) return;
        theme.RefreshThemeBrushes();
        const bool contrast = IsHighContrastEnabled();
        const bool dark = !contrast && IsSystemDarkTheme();
        ApplySystemThemeToMenuAndWindow(hwnd, dark);
        for (const auto id : {List, Process, Pid, Search, Details, Language, Level, DetailsSize}) {
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

    void ApplyLanguage() {
        if (!ready) return;
        const auto title = std::format(L"{} v{} ({})", Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), version::kVersionW, version::kArchW);
        ::SetWindowTextW(hwnd, title.c_str());
        for (const auto& [id, text] : std::array{
            std::pair{Clear, Text(L"清空", L"Clear")}, std::pair{Copy, Text(L"复制", L"Copy")},
            std::pair{Export, Text(L"导出", L"Export")},
            std::pair{About, Text(L"关于", L"About")}, std::pair{ProcessLabel, Text(L"进程", L"Process")},
            std::pair{LevelLabel, Text(L"级别", L"Level")}, std::pair{SearchLabel, Text(L"消息", L"Message")},
            std::pair{DetailsSizeLabel, Text(L"详情高度", L"Details")}, std::pair{LanguageLabel, Text(L"语言", L"Language")},
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

    void UpdateDpi(UINT dpi, const RECT& proposed) {
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
            for (const auto id : {Level, Language}) {
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

    void RecoverDisplay() {
        if (!ready || updating_display || ::IsIconic(hwnd) || ::IsZoomed(hwnd)) return;
        RECT rect{};
        if (!::GetWindowRect(hwnd, &rect)) {
            diagnostics.Record(2, L"GetWindowRect(display change)", ::GetLastError());
            return;
        }
        const POINT center{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
        UpdateDpi(QueryMonitorForPoint(center, hwnd).dpi, rect);
    }

    void Refilter() {
        filter.process = ReadText(Child(Process));
        const auto pid = ReadText(Child(Pid));
        filter.pid = ParsePid(pid);
        valid_filter = pid.empty() || filter.pid.has_value();
        const LRESULT level = ::SendMessageW(Child(Level), CB_GETCURSEL, 0, 0);
        filter.level = level >= 0 ? gsl::narrow_cast<UCHAR>(level) : 0;
        filter.text = ReadText(Child(Search));
        RebuildRows();
    }

    void SyncSelectionFromList() {
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

    void SelectAllRows() {
        const HWND list = Child(List);
        if (rows.empty()) return;
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
        SyncSelectionFromList();
    }

    void RebuildRows() {
        updating_rows = true;
        const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
        std::vector<uint64_t> next;
        if (valid_filter) {
            next.reserve(Current().Events().size());
            for (const auto& event : Current().Events()) {
                if (filter.Matches(event)) next.push_back(event.id);
            }
        }
        rows.swap(next);
        const HWND list = Child(List);
        ListView_SetItemCountEx(list, gsl::narrow_cast<int>(rows.size()), LVSICF_NOSCROLL);
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        std::vector<uint64_t> retained;
        retained.reserve(selected_ids.size());
        int focus_index = -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            const uint64_t id = gsl::at(rows, i);
            if (std::find(selected_ids.begin(), selected_ids.end(), id) != selected_ids.end()) {
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

    void Tick() {
        bool changed = false;
        for (auto& event : pending.Take()) { events.Push(std::move(event)); changed = true; }
        for (auto& event : diagnostics.queue.Take()) {
            if (event.level <= 2) last_error = event.message.substr(0, 512);
            tools.Push(std::move(event));
            changed = true;
        }
        if (++ticks % 10 == 0) collector.PollLoss();
        if (changed) RebuildRows();
        UpdateStatus();
    }

    void UpdateStatus() {
        ::SetWindowTextW(Child(Start), collector.Running() ? Text(L"停止采集", L"Stop") : Text(L"开始采集", L"Start"));
        auto status = valid_filter ? std::format(
            L"{} | {}: {} / {} | {}: {} KiB | {}: {} | {}: {}/{} | {}: {} | {}: {}",
            collector.Running() ? Text(L"采集中", L"Capturing") : Text(L"已停止", L"Stopped"),
            Text(L"显示", L"Visible"), rows.size(), Current().Events().size(),
            Text(L"缓冲", L"Buffer"), Current().Bytes() / 1024,
            Text(L"本地淘汰", L"Local eviction"), diagnostic_view ?
                tools.Evicted() + diagnostics.queue.Evicted() : events.Evicted() + pending.Evicted(),
            Text(L"ETW 丢失(事件/缓冲)", L"ETW lost(events/buffers)"), collector.events_lost.load(), collector.buffers_lost.load(),
            Text(L"解码失败", L"Decode"), collector.decode_errors.load(), Text(L"内存异常", L"Memory"),
            collector.allocation_errors.load() + diagnostics.allocation_errors.load()) :
            Text(L"PID 无效：请输入 0 到 4294967295 的整数，或留空。", L"Invalid PID: enter 0..4294967295, or leave empty.");
        if (!last_error.empty()) status = std::wstring(Text(L"错误: ", L"ERROR: ")) + last_error + L" | " + status;
        ::SetWindowTextW(Child(Status), status.c_str());
    }

    std::wstring Cell(const Event& event, int column) const {
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

    void Notify(NMHDR& header) {
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
        } else if ((header.code == LVN_ITEMCHANGED || header.code == LVN_ODSTATECHANGED) && !updating_rows) {
            SyncSelectionFromList();
        }
    }

    std::vector<Event> CollectSelectedEvents() const {
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

    void UpdateDetails() {
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

    void CopySelection() {
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

    void ShowListContextMenu(POINT screen_pt) {
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
        const bool has_sel = !CollectSelectedEvents().empty();
        const bool has_rows = !rows.empty();
        ::AppendMenuW(menu.Get(), MF_STRING | (has_sel ? MF_ENABLED : MF_GRAYED), 1,
            Text(L"复制选中日志项\tCtrl+C", L"Copy selected entries\tCtrl+C"));
        ::AppendMenuW(menu.Get(), MF_STRING | (has_rows ? MF_ENABLED : MF_GRAYED), 2,
            Text(L"全选\tCtrl+A", L"Select all\tCtrl+A"));
        const BOOL cmd = ::TrackPopupMenu(
            menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
        if (cmd == 1) CopySelection();
        else if (cmd == 2) SelectAllRows();
    }

    void ExportSnapshot() {
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

    void Command(int id, int notification) {
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
        case Clear: Current().Clear(); selected = 0; selected_ids.clear(); RebuildRows(); break;
        case Copy: CopySelection(); break;
        case Export: ExportSnapshot(); break;
        case Source:
            diagnostic_view = !diagnostic_view;
            selected = 0;
            selected_ids.clear();
            ::SetWindowTextW(Child(Source), diagnostic_view ? Text(L"输入法日志", L"IME events") : Text(L"工具诊断", L"Diagnostics"));
            ::SetWindowTextW(Child(Process), L"");
            ::SetWindowTextW(Child(Pid), L"");
            ::SetWindowTextW(Child(Search), L"");
            ::SendMessageW(Child(Level), CB_SETCURSEL, 0, 0);
            Refilter();
            break;
        case About: {
            const auto text = std::format(L"{}\r\n{} ({})\r\n{}\r\n{}\r\n{}",
                Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), version::kFullVersionW, version::kArchW, version::kGitCommitW,
                L"QiQiIME.TraceLogging -> ETW / TDH",
                Text(L"应用侧关闭的日志无法恢复。Verbose 合并 Trace/Debug，Error 合并 Error/Critical。",
                     L"Application-filtered events cannot be recovered. Verbose merges Trace/Debug; Error merges Error/Critical."));
            MSGBOXPARAMSW params{};
            params.cbSize = sizeof(params);
            params.hwndOwner = hwnd;
            params.hInstance = instance;
            params.lpszText = text.c_str();
            params.lpszCaption = Text(L"关于", L"About");
            params.dwStyle = MB_OK | MB_USERICON;
            params.lpszIcon = MAKEINTRESOURCEW(1);
            ::MessageBoxIndirectW(&params);
            break;
        }
        case Process: case Pid: case Search:
            if (notification == EN_CHANGE) Refilter();
            break;
        case Level: if (notification == CBN_SELCHANGE) Refilter(); break;
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

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = BindOrGetWindowUserData<Impl>(window, message, lparam);
        if (!self || message == WM_NCCREATE) return ::DefWindowProcW(window, message, wparam, lparam);
        try {
            switch (message) {
            case WM_SIZE: self->Layout(); return 0;
            case WM_TIMER: if (self->ready) self->Tick(); return 0;
            case WM_COMMAND: if (self->ready) self->Command(LOWORD(wparam), HIWORD(wparam)); return 0;
            case WM_NOTIFY: if (self->ready && lparam) self->Notify(*reinterpret_cast<NMHDR*>(lparam)); return 0;
            case WM_CONTEXTMENU:
                if (self->ready && reinterpret_cast<HWND>(wparam) == self->Child(List)) {
                    self->ShowListContextMenu(PointFromLParam(lparam));
                    return 0;
                }
                break;
            case WM_HSCROLL:
                if (self->ready && reinterpret_cast<HWND>(lparam) == self->Child(DetailsSize)) {
                    self->detail_height = gsl::narrow_cast<int>(::SendMessageW(self->Child(DetailsSize), TBM_GETPOS, 0, 0));
                    self->Layout();
                }
                return 0;
            case WM_DRAWITEM:
                if (lparam) {
                    const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                    if (item.CtlType == ODT_COMBOBOX) {
                        DrawFluentComboBoxItem(item, self->theme);
                    } else if (item.CtlType == ODT_BUTTON) {
                        DrawFluentPushButton(item, self->theme, item.CtlID == gsl::narrow_cast<UINT>(std::to_underlying(Start)),
                            IsSystemDarkTheme() ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));
                    }
                    return TRUE;
                }
                break;
            case WM_MEASUREITEM:
                if (lparam && reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->CtlType == ODT_COMBOBOX) {
                    reinterpret_cast<MEASUREITEMSTRUCT*>(lparam)->itemHeight = gsl::narrow_cast<UINT>(self->theme.ScalePx(22));
                    return TRUE;
                }
                break;
            case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX:
                return reinterpret_cast<LRESULT>(ApplyFluentControlColors(reinterpret_cast<HDC>(wparam), self->theme, true));
            case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:
                return reinterpret_cast<LRESULT>(ApplyFluentControlColors(reinterpret_cast<HDC>(wparam), self->theme, false));
            case WM_ERASEBKGND: {
                RECT rect{};
                ::GetClientRect(window, &rect);
                const HBRUSH brush = IsHighContrastEnabled() ? ::GetSysColorBrush(COLOR_BTNFACE) : self->theme.surface_brush.Get();
                ::FillRect(reinterpret_cast<HDC>(wparam), &rect, brush ? brush : ::GetSysColorBrush(COLOR_BTNFACE));
                return 1;
            }
            case WM_GETMINMAXINFO: {
                auto& info = *reinterpret_cast<MINMAXINFO*>(lparam);
                const auto monitor = QueryActiveMonitor(window);
                info.ptMinTrackSize = {std::min<LONG>(self->theme.ScalePx(760), monitor.work_area.right - monitor.work_area.left),
                    std::min<LONG>(self->theme.ScalePx(560), monitor.work_area.bottom - monitor.work_area.top)};
                return 0;
            }
            case WM_DPICHANGED: {
                if (lparam) self->UpdateDpi(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
                return 0;
            }
            case WM_DISPLAYCHANGE:
                self->RecoverDisplay();
                return 0;
            case WM_SETTINGCHANGE:
                if (wparam == SPI_SETWORKAREA) self->RecoverDisplay();
                if (self->ready && self->ui_language == UiLanguage::Auto) self->ApplyLanguage();
                [[fallthrough]];
            case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: case WM_DWMCOLORIZATIONCOLORCHANGED:
                self->UpdateTheme();
                return 0;
            case WM_CLOSE:
                if (!self->collector.Stop()) { self->Tick(); return 0; }
                ::DestroyWindow(window);
                return 0;
            case WM_DESTROY: ::KillTimer(window, 1); ::PostQuitMessage(0); return 0;
            case WM_NCDESTROY:
                if (self->owner->window_.Get() == window) std::ignore = self->owner->window_.Detach();
                self->hwnd = nullptr;
                self->ready = false;
                ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
            default: break;
            }
        } catch (const std::system_error& error) {
            self->diagnostics.Record(2, L"GUI/export system error", gsl::narrow_cast<ULONG>(error.code().value()));
            ::SetWindowTextW(self->Child(Status), self->Text(L"操作失败，请查看工具诊断；内存日志仍保留。", L"Operation failed; see diagnostics. Buffered records retained."));
        } catch (const std::exception&) {
            self->diagnostics.Record(2, L"GUI operation exception", ERROR_UNHANDLED_EXCEPTION);
            ::SetWindowTextW(self->Child(Status), self->Text(L"操作失败，请查看工具诊断；内存日志仍保留。", L"Operation failed; see diagnostics. Buffered records retained."));
        }
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
};

#pragma warning(suppress : 26455) // Allocation failures reach the executable's startup error dialog.
MonitorWindow::MonitorWindow() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }
MonitorWindow::~MonitorWindow() noexcept = default;

bool MonitorWindow::Create(HINSTANCE instance) {
    if (window_) return true;
    impl_->instance = instance;
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!::InitCommonControlsEx(&controls)) {
        impl_->Failure(L"InitCommonControlsEx", ERROR_DLL_INIT_FAILED);
        return false;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = &Impl::Procedure;
    cls.lpszClassName = kWindowClass.data();
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!::RegisterClassExW(&cls)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            impl_->Failure(L"RegisterClassEx", error);
            return false;
        }
    }
    const auto title = std::format(L"{} v{} ({})",
        impl_->Text(L"奇奇日志监控", L"QiQiIME Log Monitor"), version::kVersionW, version::kArchW);
    const auto monitor = QueryActiveMonitor(nullptr);
    const auto initial_rect = [&](UINT dpi) {
        const LONG width = std::min<LONG>(ScalePxByDpi(1160, dpi), monitor.work_area.right - monitor.work_area.left);
        const LONG height = std::min<LONG>(ScalePxByDpi(780, dpi), monitor.work_area.bottom - monitor.work_area.top);
        const LONG left = monitor.work_area.left + (monitor.work_area.right - monitor.work_area.left - width) / 2;
        const LONG top = monitor.work_area.top + (monitor.work_area.bottom - monitor.work_area.top - height) / 2;
        return RECT{left, top, left + width, top + height};
    };
    const auto rect = initial_rect(monitor.dpi);
    window_.Reset(::CreateWindowExW(0, kWindowClass.data(), title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, instance, impl_.get()));
    if (!window_) {
        impl_->Failure(L"CreateWindowEx", ::GetLastError());
        return false;
    }
    impl_->hwnd = window_.Get();
    if (!impl_->CreateControls()) return false;
    impl_->UpdateDpi(impl_->theme.dpi, initial_rect(impl_->theme.dpi));
    impl_->Layout();
    impl_->Tick();
    return true;
}

int RunMonitor(HINSTANCE instance, int show) {
    MonitorWindow window;
    if (!window.Create(instance)) return 1;
    ::ShowWindow(window.Handle(), show);
    MSG message{};
    BOOL result = 0;
    while ((result = ::GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (!::IsDialogMessageW(window.Handle(), &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    return result == -1 ? 1 : gsl::narrow_cast<int>(message.wParam);
}
} // namespace qiqi::monitor
