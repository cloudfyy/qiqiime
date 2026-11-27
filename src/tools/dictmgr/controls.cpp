// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"
#include "common/uiutil.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;
namespace layout = dict_manager_layout;

void DictManagerWindow::Impl::UpdateIcons() noexcept {
    std::ignore = UpdateWindowBrandIcons(hwnd, instance, theme, small_icon, big_icon);
}

bool DictManagerWindow::Impl::Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id) {
    const auto* child = CreateFluentChildControl(
        hwnd, instance, font.Get(), &theme, cls, text, style, 0, 0, 10, 10, std::to_underlying(id));
    if (!child) {
        const DWORD error = ::GetLastError();
        Failure(L"CreateFluentChildControl", error);
        return false;
    }
    return true;
}

bool DictManagerWindow::Impl::CreateControls(DisplayPreferences preferences) {
    if (!UpdateToolWindowDisplay(nullptr, theme, font, preferences, ::GetDpiForWindow(hwnd), ui_language)) {
        Failure(L"CreateUiFont", ERROR_FUNCTION_FAILED);
        return false;
    }
    if (!Add(TOOLBARCLASSNAMEW, L"", TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_WRAPABLE |
                 TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER | WS_TABSTOP, Toolbar) ||
        !Add(L"STATIC", Text(L"来源", L"Source"), SS_LEFT, SourceLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, SourceCombo) ||
        !Add(L"STATIC", Text(L"搜索", L"Search"), SS_LEFT, SearchLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, SearchEdit) ||
        !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS |
                 WS_BORDER | WS_TABSTOP | WS_CLIPSIBLINGS, List) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditWordInput) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditCodeInput) ||
        !Add(TOOLBARCLASSNAMEW, L"", TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NORESIZE |
                 CCS_NOPARENTALIGN | CCS_NODIVIDER | WS_TABSTOP, InlineSave) ||
        !Add(L"STATIC", L"", SS_LEFT, Status)) {
        return false;
    }
    command_ui.InitializeToolbar(Child(Toolbar));
    command_ui.InitializeToolbar(Child(InlineSave), true);
    for (const auto id : {EditWordInput, EditCodeInput, InlineSave}) {
        ::ShowWindow(Child(id), SW_HIDE);
        if (!::SetWindowSubclass(Child(id), EditorProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
            QIQI_LOG_ERROR("DictManager inline editor subclass installation failed");
            return false;
        }
    }

    ListView_SetExtendedListViewStyle(Child(List), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    if (!::SetWindowSubclass(Child(List), ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(list)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    const std::array columns{
        Text(L"编辑", L"Edit"),
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank"), Text(L"操作", L"Action")};
    constexpr std::array widths{40, 120, 320, 180, 76, 52};
    const HRESULT columns_hr = InsertListViewColumns(Child(List), columns, widths, theme);
    if (FAILED(columns_hr)) {
        Failure(L"ListView_InsertColumn", gsl::narrow_cast<ULONG>(columns_hr));
        return false;
    }

    ready = true;
    ::SendMessageW(ListView_GetHeader(Child(List)), WM_SETFONT, reinterpret_cast<WPARAM>(font.Get()), FALSE);
    UpdateDisplayTargets();
    UpdateIcons();
    ApplyLanguage();
    const LRESULT source_selection = ::SendMessageW(
        Child(SourceCombo), CB_SETCURSEL, std::to_underlying(DictEntrySource::UserDict), 0);
    if (source_selection == CB_ERR) {
        Failure(L"CB_SETCURSEL(default source)", gsl::narrow_cast<ULONG>(source_selection));
        return false;
    }
    filter.source = SourceFromComboIndex(source_selection);
    UpdateTheme();
    RebuildRows();
    UpdateStatus();
    if (!::SetTimer(hwnd, kRefreshTimerId, kRefreshIntervalMs, nullptr)) {
        Failure(L"SetTimer", ::GetLastError());
        return false;
    }
    return true;
}

void DictManagerWindow::Impl::Layout() noexcept {
    if (!ready || updating_layout) return;
    updating_layout = true;
    const auto restore = gsl::finally([this]() noexcept { updating_layout = false; });
    RECT rect{};
    ::GetClientRect(hwnd, &rect);
    const auto s = [this](int value) noexcept { return theme.ScalePx(value); };
    const int width = rect.right;
    const auto move = [&](ControlId id, int x, int y, int w, int h) noexcept {
        viewport.Move(Child(id), x, y, std::max(1, w), std::max(1, h));
    };

    const int margin = s(layout::kWindowMargin);
    const int gap = s(theme.display_preferences.easy_reading
        ? layout::kComfortControlGap : layout::kControlGap);
    const int available = std::max(1, width - 2 * margin);
    const int toolbar_height = command_ui.LayoutToolbar(Child(Toolbar), width, theme);
    const int filter_top = toolbar_height + margin;
    const int label_height = s(layout::kLabelHeight);
    const int source_label_width = s(layout::kSourceLabelWidth);
    const int search_label_width = s(layout::kSearchLabelWidth);
    const int source_width = s(layout::kSourceComboWidth);
    const int input_height = FluentControlHeight(theme, layout::kSearchEditHeight);
    RECT combo{};
    ::GetWindowRect(Child(SourceCombo), &combo);
    const int row_height = std::max({input_height, label_height,
        gsl::narrow_cast<int>(combo.bottom - combo.top)});
    int source_y = filter_top;
    const bool stack_source = available < source_label_width + gap + source_width;
    const int source_label_y = source_y + (stack_source ? 0 : (row_height - label_height) / 2);
    if (stack_source) source_y += label_height + gap;
    const int source_x = stack_source ? margin : margin + source_label_width + gap;
    const int source_bottom = source_y + row_height;
    const int search_x = source_x + source_width + s(layout::kFilterGroupGap);
    const bool wrap_search = stack_source ||
        width - margin - search_x < search_label_width + gap + s(layout::kSearchMinimumWidth);
    int search_y = wrap_search ? source_bottom + gap : filter_top;
    const int search_left = wrap_search ? margin : search_x;
    const bool stack_search = width - margin - search_left <
        search_label_width + gap + s(layout::kSearchMinimumWidth);
    const int search_label_y = search_y + (stack_search ? 0 : (row_height - label_height) / 2);
    if (stack_search) search_y += label_height + gap;
    const int input_left = stack_search ? margin : search_left + search_label_width + gap;

    const int list_top = std::max(source_bottom, search_y + row_height) + gap;
    const int content_height = std::max(gsl::narrow_cast<int>(rect.bottom),
        list_top + s(layout::kListMinimumHeight + layout::kListBottomInset));
    viewport.Configure(hwnd, content_height, rect.bottom);
    move(Toolbar, 0, 0, width, toolbar_height);
    move(SourceLabel, margin, source_label_y, std::min(source_label_width, available), label_height);
    move(SourceCombo, source_x, source_y,
        std::min(source_width, std::max(1, width - margin - source_x)), s(layout::kComboDropDownHeight));
    move(SearchLabel, search_left, search_label_y, std::min(search_label_width, available), label_height);
    move(SearchEdit, input_left, search_y, width - margin - input_left, input_height);
    move(List, s(layout::kWindowMargin), list_top, width - s(2 * layout::kWindowMargin),
         content_height - s(layout::kListBottomInset) - list_top);

    move(Status, s(layout::kWindowMargin), content_height - s(layout::kStatusBottomOffset),
         width - s(2 * layout::kWindowMargin), s(layout::kStatusHeight));
    PositionEditor();
}

void DictManagerWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(SourceCombo),
        Child(SearchEdit),
        Child(EditWordInput), Child(EditCodeInput)};
    std::ignore = ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls);
    command_ui.ApplyTheme(hwnd, theme);
}

bool DictManagerWindow::Impl::UpdateDisplay(DisplayPreferences preferences, UINT dpi, UiLanguage language) noexcept {
    const std::array lists{Child(List)};
    if (!UpdateToolWindowDisplay(hwnd, theme, font, preferences, dpi, language, lists)) return false;
    UpdateDisplayTargets();
    UpdateIcons();
    Layout();
    UpdateTheme();
    return true;
}

void DictManagerWindow::Impl::RefreshDisplay(const QiQiConfig& config, bool update_language_menu) {
    if (!ready) return;
    const auto preferences = NormalizeDisplayPreferences(GetDisplayPreferences(config));
    const auto language = ResolveUiLanguage(language_override.value_or(config.ui_language));
    const bool language_changed = language != ui_language;
    if (language_changed && !update_language_menu && command_ui.MenuActive()) return;
    if ((language_changed || preferences != theme.display_preferences) &&
        !UpdateDisplay(preferences, theme.dpi, language)) return;
    if (language_changed || update_language_menu) {
        ui_language = language;
        ApplyLanguage();
    }
}

void DictManagerWindow::Impl::UpdateDisplayTargets() noexcept {
    std::ignore = SetFluentListRowHeight(Child(List), theme, row_images);
    SetFluentComboHeights(Child(SourceCombo), theme);
    if (theme.display_preferences.easy_reading) {
        const int target = theme.ScaleDipPx(kComfortMinimumTargetDip);
        for (const auto column : {ColumnId::Edit, ColumnId::Code, ColumnId::Word, ColumnId::Action}) {
            const int index = std::to_underlying(column);
            if (ListView_GetColumnWidth(Child(List), index) < target) {
                ListView_SetColumnWidth(Child(List), index, target);
            }
        }
    }
}

void DictManagerWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = FormatWindowTitle(ui_language);
    ::SetWindowTextW(hwnd, title.c_str());

    UpdateCommands();
    for (const auto& [id, text] : std::array{
             std::pair{SourceLabel, Text(L"来源", L"Source")},
             std::pair{SearchLabel, Text(L"搜索", L"Search")}}) {
        ::SetWindowTextW(Child(id), text);
    }
    if (!::SendMessageW(Child(SearchEdit), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
            Text(L"五笔编码或词条（纯字母按编码搜索）", L"Wubi code or word (letters search codes)")))) {
        QIQI_LOG_WARN("DictManager search cue banner could not be set");
    }
    for (const auto& [id, cue] : std::array{
             std::pair{EditCodeInput, Text(L"编码", L"Code")}, std::pair{EditWordInput, Text(L"词条", L"Word")}}) {
        if (!::SendMessageW(Child(id), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue))) {
            QIQI_LOG_WARN("DictManager inline editor cue banner could not be set");
        }
    }

    const std::array sources{
        SourceText(DictEntrySource::All),
        SourceText(DictEntrySource::UserDict),
        SourceText(DictEntrySource::Pinned),
        SourceText(DictEntrySource::Blocked),
        SourceText(DictEntrySource::PhraseFreq),
        SourceText(DictEntrySource::PrimaryDict),
        SourceText(DictEntrySource::ExtraDict)};
    const HRESULT source_result = ReplaceComboBoxItems(Child(SourceCombo), sources);
    if (FAILED(source_result)) {
        ThrowUiError("ReplaceComboBoxItems(source)", gsl::narrow_cast<DWORD>(source_result));
    }

    const std::array columns{
        Text(L"编辑", L"Edit"),
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank"), Text(L"操作", L"Action")};
    for (size_t i = 0; i < columns.size(); ++i) {
        std::wstring text(gsl::at(columns, i));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = text.data();
        ListView_SetColumn(Child(List), gsl::narrow_cast<int>(i), &column);
    }
    UpdateStatus();
    Layout();
    UpdateTheme();
    ::InvalidateRect(Child(List), nullptr, FALSE);
}

void DictManagerWindow::Impl::UpdateDpi(UINT dpi, const RECT& proposed) noexcept {
    if (!ready) return;
    std::ignore = UpdateDisplay(theme.display_preferences, dpi, ui_language);
    const POINT center{
        proposed.left + (proposed.right - proposed.left) / 2,
        proposed.top + (proposed.bottom - proposed.top) / 2};
    const auto monitor = QueryMonitorForPoint(center, hwnd);
    const RECT rect = ClampWindowRectToWorkArea(proposed, monitor.work_area);
    ::SetWindowPos(
        hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    UpdateTheme();
}

void DictManagerWindow::Impl::RecoverDisplay() noexcept {
    if (!ready || ::IsIconic(hwnd) || ::IsZoomed(hwnd)) return;
    RECT rect{};
    if (!::GetWindowRect(hwnd, &rect)) return;
    const POINT center{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
    UpdateDpi(QueryMonitorForPoint(center, hwnd).dpi, rect);
}

void DictManagerWindow::Impl::UpdateEditorState(CodeChange change) {
    const bool dirty = editor.Dirty();
    editor.ChangeWord(ReadWindowTextString(Child(EditWordInput)));
    editor.ChangeCode(ReadWindowTextString(Child(EditCodeInput)), change);
    if (editor.Dirty() == dirty) return;
    ListView_RedrawItems(Child(List), editing_row, editing_row);
    UpdateStatus();
}

void DictManagerWindow::Impl::PositionEditor() noexcept {
    const auto hide = [&]() noexcept {
        for (const auto id : {EditCodeInput, EditWordInput, InlineSave}) ::ShowWindow(Child(id), SW_HIDE);
    };
    if (!editor.Active() || editing_row < 0) {
        hide();
        return;
    }
    const HWND list = Child(List);
    RECT client{}, header{};
    ::GetClientRect(list, &client);
    ::GetWindowRect(ListView_GetHeader(list), &header);
    ::MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&header), 2);
    std::array<RECT, 3> cells{};
    constexpr std::array columns{ColumnId::Code, ColumnId::Word, ColumnId::Action};
    for (size_t i = 0; i < cells.size(); ++i) {
        auto& cell = gsl::at(cells, i);
        const int column = std::to_underlying(gsl::at(columns, i));
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        if (!ListView_GetSubItemRect(list, editing_row, column, LVIR_BOUNDS, &cell) ||
            cell.top < header.bottom || cell.bottom > client.bottom || cell.left < 0 || cell.right > client.right) {
            hide();
            return;
        }
        ::MapWindowPoints(list, hwnd, reinterpret_cast<POINT*>(&cell), 2);
    }
    constexpr std::array controls{EditCodeInput, EditWordInput};
    const int inset = theme.display_preferences.easy_reading ? 0 : layout::kEditorCellInsetPx;
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& cell = gsl::at(cells, i);
        ::SetWindowPos(Child(gsl::at(controls, i)), nullptr,
            cell.left + inset, cell.top + inset,
            std::max(1L, cell.right - cell.left - 2 * inset),
            std::max(1L, cell.bottom - cell.top - 2 * inset), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    const auto& action = cells.back();
    const int size = std::max(1, std::min({FluentControlHeight(theme, layout::kInlineSaveButtonSize),
        gsl::narrow_cast<int>(action.right - action.left - 2 * inset),
        gsl::narrow_cast<int>(action.bottom - action.top - 2 * inset)}));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONWIDTH, 0, MAKELPARAM(size, size));
    ::SetWindowPos(Child(InlineSave), nullptr, action.left + (action.right - action.left - size) / 2,
        action.top, size, action.bottom - action.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

} // namespace qiqi::dict_manager
