#include "tools/dict_manager/internal.h"
#include "common/about_dialog.h"
#include "common/file_util.h"
#include "common/theme.h"
#include "common/tool_window.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <format>
#include <system_error>
#include <uxtheme.h>

namespace qiqi::dict_manager {
using enum ControlId;

void DictManagerWindow::Impl::Failure(std::wstring_view operation, ULONG error) {
    QIQI_LOG_CAT_ERROR(
        log::Keyword::Dictionary,
        "DictManager failure: op='{}', error=0x{:08X}",
        WideToUtf8(operation),
        error);
    const auto message = std::format(L"{}\r\n{}={} (0x{:08X})", operation, Text(L"错误码", L"Error"), error, error);
    ::MessageBoxW(hwnd, message.c_str(), Text(L"奇奇五笔词库管理", L"QiQiIME Dictionary Manager"), MB_OK | MB_ICONERROR);
}

void DictManagerWindow::Impl::UpdateIcons() {
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

LRESULT CALLBACK DictManagerWindow::Impl::ListProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, ListProcedure, subclass);
    } else if (message == WM_KEYDOWN && self && self->ready) {
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (wparam == 'C' || wparam == VK_INSERT) {
                self->CopySelection();
                return 0;
            }
            if (wparam == 'A') {
                self->SelectAllRows();
                return 0;
            }
        } else if (wparam == VK_DELETE) {
            self->ExecuteDeleteSelected();
            return 0;
        }
    } else if (message == WM_NOTIFY && self) {
        if (const auto drawn = TryCustomDrawDarkListViewHeader(window, lparam, self->font.Get(), self->theme)) {
            return *drawn;
        }
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

bool DictManagerWindow::Impl::CreateControls() {
    theme.Init(::GetDpiForWindow(hwnd));
    font = CreateUiFont(theme.ScalePx(14));
    if (!font) {
        Failure(L"CreateUiFont", ::GetLastError());
        return false;
    }
    constexpr DWORD button = BS_OWNERDRAW | WS_TABSTOP;
    if (!Add(L"STATIC", L"", SS_LEFT, LanguageLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, Language) ||
        !Add(L"BUTTON", Text(L"刷新", L"Reload"), button, Reload) ||
        !Add(L"BUTTON", Text(L"复制", L"Copy"), button, Copy) ||
        !Add(L"BUTTON", Text(L"导入词库", L"Import"), button, Import) ||
        !Add(L"BUTTON", Text(L"导出快照", L"Export"), button, Export) ||
        !Add(L"BUTTON", Text(L"清空词频", L"Clear Freq"), button, ClearFreq) ||
        !Add(L"BUTTON", Text(L"关于", L"About"), button, About) ||
        !Add(L"STATIC", Text(L"来源", L"Source"), SS_LEFT, SourceLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, SourceCombo) ||
        !Add(L"STATIC", Text(L"编码", L"Code"), SS_LEFT, SearchCodeLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, SearchCodeEdit) ||
        !Add(L"STATIC", Text(L"词条/反查", L"Word/Lookup"), SS_LEFT, SearchWordLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, SearchWordEdit) ||
        !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, List) ||
        !Add(L"STATIC", Text(L"词条:", L"Word:"), SS_LEFT, EditWordLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditWordInput) ||
        !Add(L"STATIC", Text(L"编码:", L"Code:"), SS_LEFT, EditCodeLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditCodeInput) ||
        !Add(L"BUTTON", Text(L"自动取码", L"Auto Code"), button, AutoCodeBtn) ||
        !Add(L"BUTTON", Text(L"新增/保存用户词", L"Add / Save User Word"), button, AddUpdateBtn) ||
        !Add(L"BUTTON", Text(L"删除选中", L"Delete Selected"), button, DeleteBtn) ||
        !Add(L"BUTTON", Text(L"词频置顶", L"Promote Freq"), button, PromoteBtn) ||
        !Add(L"STATIC", L"", SS_LEFT, Status)) {
        return false;
    }

    ListView_SetExtendedListViewStyle(Child(List), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    if (!::SetWindowSubclass(Child(List), ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(list)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    std::array<const wchar_t*, 4> columns{
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank")};
    constexpr std::array widths{120, 320, 180, 96};
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
    RebuildRows();
    UpdateStatus();
    if (!::SetTimer(hwnd, 1, 500, nullptr)) {
        Failure(L"SetTimer", ::GetLastError());
        return false;
    }
    return true;
}

void DictManagerWindow::Impl::Layout() noexcept {
    if (!ready) return;
    RECT rect{};
    ::GetClientRect(hwnd, &rect);
    const auto s = [this](int value) noexcept { return theme.ScalePx(value); };
    const int width = rect.right;
    const auto move = [&](ControlId id, int x, int y, int w, int h) noexcept {
        ::MoveWindow(Child(id), x, y, std::max(1, w), std::max(1, h), TRUE);
    };

    constexpr std::array buttons{Reload, Copy, Import, Export, ClearFreq, About};
    constexpr int button_count = gsl::narrow_cast<int>(buttons.size());
    for (int i = 0; i < button_count; ++i) {
        move(gsl::at(buttons, i), s(12 + i * 108), s(12), s(100), s(32));
    }
    move(LanguageLabel, width - s(296), s(18), s(76), s(24));
    move(Language, width - s(212), s(12), s(200), s(200));

    const int filter_top = s(54);
    move(SourceLabel, s(12), filter_top + s(4), s(44), s(24));
    move(SourceCombo, s(60), filter_top, s(165), s(220));
    move(SearchCodeLabel, s(238), filter_top + s(4), s(44), s(24));
    move(SearchCodeEdit, s(286), filter_top, s(110), s(28));
    move(SearchWordLabel, s(410), filter_top + s(4), s(82), s(24));
    move(SearchWordEdit, s(496), filter_top, width - s(508), s(28));

    const int list_top = filter_top + s(40);
    const int edit_bar_top = rect.bottom - s(76);
    move(List, s(12), list_top, width - s(24), std::max(s(80), edit_bar_top - list_top - s(8)));

    move(EditWordLabel, s(12), edit_bar_top + s(6), s(44), s(24));
    move(EditWordInput, s(58), edit_bar_top + s(2), s(200), s(28));
    move(EditCodeLabel, s(270), edit_bar_top + s(6), s(44), s(24));
    move(EditCodeInput, s(316), edit_bar_top + s(2), s(90), s(28));
    move(AutoCodeBtn, s(416), edit_bar_top, s(96), s(32));
    move(AddUpdateBtn, s(520), edit_bar_top, s(144), s(32));
    move(DeleteBtn, s(672), edit_bar_top, s(104), s(32));
    move(PromoteBtn, s(784), edit_bar_top, s(104), s(32));

    move(Status, s(12), rect.bottom - s(32), width - s(24), s(24));
}

void DictManagerWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(Language), Child(SourceCombo),
        Child(SearchCodeEdit), Child(SearchWordEdit),
        Child(EditWordInput), Child(EditCodeInput)};
    std::ignore = ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls);
}

void DictManagerWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = std::format(
        L"{} v{} ({})",
        Text(L"奇奇五笔词库与词频管理", L"QiQiIME Dictionary & Frequency Manager"),
        version::kVersionW,
        version::kArchW);
    ::SetWindowTextW(hwnd, title.c_str());

    for (const auto& [id, text] : std::array{
             std::pair{Reload, Text(L"刷新", L"Reload")},
             std::pair{Copy, Text(L"复制", L"Copy")},
             std::pair{Import, Text(L"导入词库", L"Import")},
             std::pair{Export, Text(L"导出快照", L"Export")},
             std::pair{ClearFreq, Text(L"清空词频", L"Clear Freq")},
             std::pair{About, Text(L"关于", L"About")},
             std::pair{LanguageLabel, Text(L"语言", L"Language")},
             std::pair{SourceLabel, Text(L"来源", L"Source")},
             std::pair{SearchCodeLabel, Text(L"编码", L"Code")},
             std::pair{SearchWordLabel, Text(L"词条/反查", L"Word/Lookup")},
             std::pair{EditWordLabel, Text(L"词条:", L"Word:")},
             std::pair{EditCodeLabel, Text(L"编码:", L"Code:")},
             std::pair{AutoCodeBtn, Text(L"自动取码", L"Auto Code")},
             std::pair{AddUpdateBtn, Text(L"新增/保存用户词", L"Add / Save User Word")},
             std::pair{DeleteBtn, Text(L"删除选中", L"Delete Selected")},
             std::pair{PromoteBtn, Text(L"词频置顶", L"Promote Freq")}}) {
        ::SetWindowTextW(Child(id), text);
    }

    const std::array sources{
        SourceText(DictEntrySource::All),
        SourceText(DictEntrySource::UserDict),
        SourceText(DictEntrySource::PhraseFreq),
        SourceText(DictEntrySource::PrimaryDict),
        SourceText(DictEntrySource::ExtraDict)};
    std::ignore = ReplaceComboBoxItems(Child(SourceCombo), sources);

    const auto& strings = GetUiStrings(ui_language);
    const std::array languages{strings.dlg_lang_auto, strings.dlg_lang_zh, strings.dlg_lang_en};
    std::ignore = ReplaceComboBoxItems(Child(Language), languages);

    const std::array<const wchar_t*, 4> columns{
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank")};
    for (size_t i = 0; i < columns.size(); ++i) {
        std::wstring text(gsl::at(columns, i));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = text.data();
        ListView_SetColumn(Child(List), gsl::narrow_cast<int>(i), &column);
    }
    UpdateStatus();
    ::InvalidateRect(Child(List), nullptr, FALSE);
}

void DictManagerWindow::Impl::UpdateDpi(UINT dpi, const RECT& proposed) {
    if (!ready) return;
    const UINT previous = theme.dpi;
    if (dpi != previous) {
        theme.Init(dpi);
        if (auto next_font = CreateUiFont(theme.ScalePx(14))) {
            ::EnumChildWindows(
                hwnd,
                [](HWND child, LPARAM handle) noexcept -> BOOL {
                    ::SendMessageW(child, WM_SETFONT, handle, TRUE);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(next_font.Get()));
            font = std::move(next_font);
        }
        for (int i = 0; i < 4; ++i) {
            ListView_SetColumnWidth(
                Child(List),
                i,
                ::MulDiv(ListView_GetColumnWidth(Child(List), i), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {SourceCombo, Language}) {
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), theme.ScalePx(22));
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, 0, theme.ScalePx(22));
        }
        UpdateIcons();
    }
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

void DictManagerWindow::Impl::RecoverDisplay() {
    if (!ready || ::IsIconic(hwnd) || ::IsZoomed(hwnd)) return;
    RECT rect{};
    if (!::GetWindowRect(hwnd, &rect)) return;
    const POINT center{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
    UpdateDpi(QueryMonitorForPoint(center, hwnd).dpi, rect);
}

void DictManagerWindow::Impl::Refilter() {
    const LRESULT src_sel = ::SendMessageW(Child(SourceCombo), CB_GETCURSEL, 0, 0);
    filter.source = SourceFromComboIndex(src_sel);
    filter.code_query = ReadWindowTextString(Child(SearchCodeEdit));
    filter.word_query = ReadWindowTextString(Child(SearchWordEdit));
    RebuildRows();
    UpdateStatus();
}

void DictManagerWindow::Impl::RebuildRows() {
    updating_rows = true;
    const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
    std::vector<uint64_t> next;
    next.reserve(model.Records().size());
    for (const auto& rec : model.Records()) {
        if (filter.Matches(rec)) {
            next.push_back(rec.id);
        }
    }
    rows.swap(next);
    const HWND list = Child(List);
    ListView_SetItemCountEx(list, gsl::narrow_cast<int>(rows.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    selected_ids.clear();
    selected_id = 0;
    ::InvalidateRect(list, nullptr, FALSE);
}

void DictManagerWindow::Impl::SyncSelectionFromList() {
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
        selected_id = gsl::at(rows, gsl::narrow_cast<size_t>(focused));
    } else {
        selected_id = selected_ids.empty() ? 0 : selected_ids.back();
    }
    SyncEditorFromSelection();
}

void DictManagerWindow::Impl::SelectAllRows() {
    const HWND list = Child(List);
    if (rows.empty()) return;
    {
        updating_rows = true;
        const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    }
    SyncSelectionFromList();
}

void DictManagerWindow::Impl::SyncEditorFromSelection() {
    const auto* rec = model.FindById(selected_id);
    if (!rec) return;
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditWordInput), rec->word.c_str());
    ::SetWindowTextW(Child(EditCodeInput), rec->code.c_str());
}

void DictManagerWindow::Impl::Tick() {
    if (model.PollExternalChanges()) {
        RebuildRows();
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::UpdateStatus() {
    auto status = std::format(
        L"{}: {} / {} | {}: {} | {}: {} | {}: {}",
        Text(L"当前显示", L"Visible"),
        rows.size(),
        model.Records().size(),
        Text(L"用户词条", L"User words"),
        model.UserEntryCount(),
        Text(L"动态词频", L"Phrase freq"),
        model.PhraseFreqCount(),
        Text(L"系统词库", L"System dict"),
        model.SystemEntryCount());
    if (!last_message.empty()) {
        status = last_message + L" | " + status;
    }
    ::SetWindowTextW(Child(Status), status.c_str());
}

std::wstring DictManagerWindow::Impl::Cell(const DictRecord& record, int column) const {
    switch (column) {
    case 0: return record.code;
    case 1: return record.word;
    case 2: return SourceText(record.source);
    default: return std::to_wstring(record.rank_in_code);
    }
}

void DictManagerWindow::Impl::Notify(NMHDR& header) {
    if (header.idFrom != gsl::narrow_cast<UINT_PTR>(std::to_underlying(List))) return;
    if (header.code == LVN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMLVDISPINFOW*>(&header);
        if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 &&
            gsl::narrow_cast<size_t>(info.item.iItem) < rows.size()) {
            if (const auto* rec = model.FindById(gsl::at(rows, gsl::narrow_cast<size_t>(info.item.iItem)))) {
                const auto text = Cell(*rec, info.item.iSubItem);
                if (info.item.pszText && info.item.cchTextMax > 0) {
                    wcsncpy_s(
                        info.item.pszText,
                        gsl::narrow_cast<size_t>(info.item.cchTextMax),
                        text.c_str(),
                        _TRUNCATE);
                }
            }
        }
    } else if (header.code == LVN_ITEMCHANGED && !updating_rows) {
        const auto& change = *reinterpret_cast<const NMLISTVIEW*>(&header);
        if ((change.uChanged & LVIF_STATE) && ((change.uOldState ^ change.uNewState) & LVIS_SELECTED)) {
            SyncSelectionFromList();
        }
    } else if (header.code == LVN_ODSTATECHANGED && !updating_rows) {
        SyncSelectionFromList();
    }
}

std::vector<DictRecord> DictManagerWindow::Impl::CollectSelectedRecords() const {
    std::vector<DictRecord> result;
    const HWND list = Child(List);
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < rows.size()) {
            if (const auto* rec = model.FindById(gsl::at(rows, gsl::narrow_cast<size_t>(index)))) {
                result.push_back(*rec);
            }
        }
    }
    if (result.empty() && selected_id != 0) {
        if (const auto* rec = model.FindById(selected_id)) {
            result.push_back(*rec);
        }
    }
    return result;
}

void DictManagerWindow::Impl::AutoFillCodeFromWord() {
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    const std::wstring auto_code = model.ComputeAutoCode(word);
    if (!auto_code.empty()) {
        ::SetWindowTextW(Child(EditCodeInput), auto_code.c_str());
    }
}

void DictManagerWindow::Impl::ExecuteAddOrUpdate() {
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    std::wstring code = ReadWindowTextString(Child(EditCodeInput));
    if (code.empty() && !word.empty()) {
        code = model.ComputeAutoCode(word);
        if (!code.empty()) {
            ::SetWindowTextW(Child(EditCodeInput), code.c_str());
        }
    }
    const auto* selected_rec = model.FindById(selected_id);
    bool ok = false;
    if (selected_rec && selected_rec->source == DictEntrySource::UserDict &&
        selected_rec->code == code && selected_rec->word != word) {
        ok = model.ModifyUserEntry(code, selected_rec->word, word);
    } else {
        ok = model.AddOrUpdateUserEntry(code, word);
    }
    if (!ok) {
        last_message = Text(L"保存词条失败（请检查五笔编码 a-z 及词条非空）", L"Failed to save entry (check a-z code and word)");
    } else {
        last_message = Text(L"已保存用户词条并同步", L"Saved user entry and synchronized");
        RebuildRows();
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteDeleteSelected() {
    const auto selected = CollectSelectedRecords();
    if (selected.empty()) return;
    size_t removed_count = 0;
    for (const auto& rec : selected) {
        if (rec.source == DictEntrySource::UserDict) {
            if (model.RemoveUserEntry(rec.code, rec.word)) ++removed_count;
        } else if (rec.source == DictEntrySource::PhraseFreq) {
            if (model.RemovePhraseFrequency(rec.code, rec.word)) ++removed_count;
        }
    }
    if (removed_count > 0) {
        last_message = std::format(L"{}: {}", Text(L"已删除并同步记录数", L"Deleted & synced records"), removed_count);
        RebuildRows();
    } else {
        last_message = Text(L"仅用户词库与动态词频支持删除", L"Only user dictionary and phrase frequency entries can be deleted");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePromoteSelected() {
    std::wstring word = ReadWindowTextString(Child(EditWordInput));
    std::wstring code = ReadWindowTextString(Child(EditCodeInput));
    if (const auto* selected_rec = model.FindById(selected_id)) {
        if (word.empty()) word = selected_rec->word;
        if (code.empty()) code = selected_rec->code;
    }
    if (model.PromotePhraseFrequency(code, word)) {
        last_message = Text(L"已置顶动态词频并同步", L"Promoted phrase frequency and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"词频置顶仅支持双字及以上词组", L"Phrase frequency promotion requires a multi-character phrase");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteClearFrequencies() {
    if (model.ClearAllPhraseFrequencies()) {
        last_message = Text(L"已清空全部动态词频记录", L"Cleared all dynamic phrase frequencies");
        RebuildRows();
    } else {
        last_message = Text(L"清空动态词频失败", L"Failed to clear phrase frequencies");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::CopySelection() {
    const auto items = CollectSelectedRecords();
    if (items.empty()) return;
    const auto text = FormatRecordsForClipboard(items);
    const ULONG error = CopyTextToClipboard(hwnd, text);
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"复制到剪贴板失败", L"Copy to clipboard failed"), error);
    } else {
        last_message = std::format(L"{}: {}", Text(L"已复制词条数", L"Copied entries"), items.size());
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::ShowListContextMenu(POINT screen_pt) {
    screen_pt = ResolveListViewContextMenuPoint(Child(List), screen_pt, theme);
    const ScopedMenu menu(::CreatePopupMenu());
    if (!menu) return;
    const auto selected = CollectSelectedRecords();
    const bool has_sel = !selected.empty();
    const bool can_delete = has_sel &&
        (selected.front().source == DictEntrySource::UserDict || selected.front().source == DictEntrySource::PhraseFreq);
    ::AppendMenuW(menu.Get(), MF_STRING | (has_sel ? MF_ENABLED : MF_GRAYED), 1,
        Text(L"复制选中词条\tCtrl+C", L"Copy selected entries\tCtrl+C"));
    ::AppendMenuW(menu.Get(), MF_STRING | (!rows.empty() ? MF_ENABLED : MF_GRAYED), 2,
        Text(L"全选\tCtrl+A", L"Select all\tCtrl+A"));
    ::AppendMenuW(menu.Get(), MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu.Get(), MF_STRING | (has_sel ? MF_ENABLED : MF_GRAYED), 3,
        Text(L"将选中词组置顶词频", L"Promote phrase frequency"));
    ::AppendMenuW(menu.Get(), MF_STRING | (can_delete ? MF_ENABLED : MF_GRAYED), 4,
        Text(L"删除选中项\tDel", L"Delete selected\tDel"));
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    if (cmd == 1) CopySelection();
    else if (cmd == 2) SelectAllRows();
    else if (cmd == 3) ExecutePromoteSelected();
    else if (cmd == 4) ExecuteDeleteSelected();
}

void DictManagerWindow::Impl::ImportUserDictDialog() {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"导入用户词库文件 (UTF-8)", L"Import User Dictionary File (UTF-8)");
    dialog.lpstrFilter = Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&dialog)) return;
    size_t imported = 0;
    if (model.ImportUserEntriesFromFile(path.data(), imported)) {
        last_message = std::format(L"{}: {}", Text(L"成功导入词条数", L"Imported entries"), imported);
        RebuildRows();
    } else {
        last_message = Text(L"导入失败或无有效词条", L"Import failed or no valid entries");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExportSnapshotDialog() {
    std::array<wchar_t, 32768> path{};
    wcscpy_s(path.data(), path.size(), L"qiqi_user_dict_export.txt");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"导出当前筛选词条 (UTF-8)", L"Export Filtered Entries (UTF-8)");
    dialog.lpstrFilter = Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&dialog)) return;
    std::vector<DictRecord> snapshot;
    snapshot.reserve(rows.size());
    for (const uint64_t id : rows) {
        if (const auto* rec = model.FindById(id)) {
            snapshot.push_back(*rec);
        }
    }
    if (model.ExportRecordsToFile(path.data(), snapshot)) {
        last_message = std::format(L"{}: {}", Text(L"已导出词条数", L"Exported entries"), snapshot.size());
    } else {
        last_message = Text(L"导出文件失败", L"Failed to export file");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::Command(int id, int notification) {
    switch (gsl::narrow_cast<ControlId>(id)) {
    case Reload:
        model.ReloadAll();
        last_message = Text(L"已重新加载词库与词频", L"Reloaded dictionaries and phrase frequencies");
        RebuildRows();
        UpdateStatus();
        break;
    case Copy:
        CopySelection();
        break;
    case Import:
        ImportUserDictDialog();
        break;
    case Export:
        ExportSnapshotDialog();
        break;
    case ClearFreq:
        ExecuteClearFrequencies();
        break;
    case About:
        std::ignore = ShowAboutDialog(instance, hwnd, ui_language, true);
        break;
    case SourceCombo:
        if (!updating_filters && notification == CBN_SELCHANGE) {
            Refilter();
        }
        break;
    case SearchCodeEdit:
    case SearchWordEdit:
        if (!updating_filters && notification == EN_CHANGE) {
            Refilter();
        }
        break;
    case EditWordInput:
        if (!updating_editor && notification == EN_CHANGE) {
            AutoFillCodeFromWord();
        }
        break;
    case AutoCodeBtn:
        AutoFillCodeFromWord();
        break;
    case AddUpdateBtn:
        ExecuteAddOrUpdate();
        break;
    case DeleteBtn:
        ExecuteDeleteSelected();
        break;
    case PromoteBtn:
        ExecutePromoteSelected();
        break;
    case Language:
        if (notification == CBN_SELCHANGE) {
            const LRESULT choice = ::SendMessageW(Child(Language), CB_GETCURSEL, 0, 0);
            if (choice >= 0 && choice <= 2) {
                ui_language = gsl::narrow_cast<UiLanguage>(choice);
                ApplyLanguage();
            }
        }
        break;
    default:
        break;
    }
}

LRESULT CALLBACK DictManagerWindow::Impl::Procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = BindOrGetWindowUserData<Impl>(window, message, lparam);
    if (!self || message == WM_NCCREATE) return ::DefWindowProcW(window, message, wparam, lparam);
    try {
        if (const auto handled = HandleFluentToolWindowMessage(
                window, message, wparam, lparam, self->theme,
                gsl::narrow_cast<UINT>(std::to_underlying(AddUpdateBtn)), 920, 560)) {
            return *handled;
        }
        switch (message) {
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_TIMER:
            if (self->ready) self->Tick();
            return 0;
        case WM_COMMAND:
            if (self->ready) self->Command(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_NOTIFY:
            if (self->ready && lparam) self->Notify(*reinterpret_cast<NMHDR*>(lparam));
            return 0;
        case WM_CONTEXTMENU:
            if (self->ready && reinterpret_cast<HWND>(wparam) == self->Child(List)) {
                self->ShowListContextMenu(PointFromLParam(lparam));
                return 0;
            }
            break;
        case WM_DPICHANGED:
            if (lparam) self->UpdateDpi(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE:
            self->RecoverDisplay();
            return 0;
        case WM_SETTINGCHANGE:
            if (wparam == SPI_SETWORKAREA) self->RecoverDisplay();
            if (self->ready && self->ui_language == UiLanguage::Auto) self->ApplyLanguage();
            [[fallthrough]];
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            self->UpdateTheme();
            return 0;
        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            ::KillTimer(window, 1);
            ::PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (self->owner->window_.Get() == window) std::ignore = self->owner->window_.Detach();
            self->hwnd = nullptr;
            self->ready = false;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    } catch (const std::exception&) {
        ::SetWindowTextW(
            self->Child(Status),
            self->Text(L"操作发生异常，请重试。", L"Operation failed with an exception."));
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

#pragma warning(suppress : 26455)
DictManagerWindow::DictManagerWindow() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}
DictManagerWindow::~DictManagerWindow() noexcept = default;

bool DictManagerWindow::Create(
    HINSTANCE instance,
    const std::filesystem::path& user_dict_path,
    const std::filesystem::path& phrase_freq_path) {
    if (window_) return true;
    std::ignore = log::EnsureInitialized();
    impl_->instance = instance;
    const std::filesystem::path module_path = GetModuleFilePath(instance);
    impl_->model.InitializePaths(
        module_path.empty() ? std::filesystem::path{} : module_path.parent_path(),
        user_dict_path,
        phrase_freq_path);

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
    const auto title = std::format(
        L"{} v{} ({})",
        impl_->Text(L"奇奇五笔词库与词频管理", L"QiQiIME Dictionary & Frequency Manager"),
        version::kVersionW,
        version::kArchW);
    const auto monitor = QueryActiveMonitor(nullptr);
    const auto rect = ComputeCenteredToolWindowRect(monitor, 1020, 680, monitor.dpi);
    window_.Reset(::CreateWindowExW(
        0,
        kWindowClass.data(),
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        impl_.get()));
    if (!window_) {
        impl_->Failure(L"CreateWindowEx", ::GetLastError());
        return false;
    }
    impl_->hwnd = window_.Get();
    if (!impl_->CreateControls()) return false;
    impl_->UpdateDpi(impl_->theme.dpi, ComputeCenteredToolWindowRect(monitor, 1020, 680, impl_->theme.dpi));
    impl_->Layout();
    return true;
}

int RunDictManager(HINSTANCE instance, int show) {
    DictManagerWindow window;
    if (!window.Create(instance)) return 1;
    return RunToolWindowMessageLoop(window.Handle(), show);
}

} // namespace qiqi::dict_manager
