// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"

#include <gsl/gsl>
#include <algorithm>
#include <format>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;
namespace layout = dict_manager_layout;

void DictManagerWindow::Impl::Refilter() {
    const LRESULT src_sel = ::SendMessageW(Child(SourceCombo), CB_GETCURSEL, 0, 0);
    filter.source = SourceFromComboIndex(src_sel);
    filter.SetQuery(ReadWindowTextString(Child(SearchEdit)));
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
    if (editor.Active()) {
        const auto* original = FindOriginal();
        const auto row = original ? std::ranges::find(next, original->id) : next.end();
        if (row != next.end()) {
            editing_row = gsl::narrow_cast<int>(row - next.begin());
        } else {
            editing_row = std::clamp(editing_row, 0, gsl::narrow_cast<int>(next.size()));
            next.insert(next.begin() + editing_row, 0);
        }
    }
    rows.swap(next);
    const HWND list = Child(List);
    ListView_SetItemCountEx(list, gsl::narrow_cast<int>(rows.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    selected_ids.clear();
    selected_id = 0;
    ::InvalidateRect(list, nullptr, FALSE);
    if (editor.Active()) {
        ListView_SetItemState(list, editing_row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    PositionEditor();
}

void DictManagerWindow::Impl::SyncSelectionFromList() {
    const HWND list = Child(List);
    auto selection = ReadOwnerDataListSelection(list, rows);
    selected_ids = std::move(selection.selected_ids);
    selected_id = selection.active_id;
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

const DictRecord* DictManagerWindow::Impl::FindOriginal() const noexcept {
    return editor.FindOriginal(model.Records());
}

void DictManagerWindow::Impl::BeginEdit(int row, int column) {
    if (editor.Active() && row == editing_row) {
        ListView_EnsureVisible(Child(List), editing_row, FALSE);
        PositionEditor();
        ::SetFocus(Child(column == std::to_underlying(ColumnId::Code) ? EditCodeInput : EditWordInput));
        return;
    }
    if (row < 0 || gsl::narrow_cast<size_t>(row) >= rows.size()) return;
    const auto* record = model.FindById(gsl::at(rows, gsl::narrow_cast<size_t>(row)));
    if (!record) return;
    const bool discard_new = editor.Mode() == EditorMode::NewDraft;
    const int previous_row = editing_row;
    editor.BeginExisting(*record);
    editing_row = row;
    ime_composing = false;
    if (discard_new) {
        RebuildRows();
    } else if (previous_row >= 0) {
        ListView_RedrawItems(Child(List), previous_row, previous_row);
    }
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditWordInput), record->word.c_str());
    ::SetWindowTextW(Child(EditCodeInput), record->code.c_str());
    UpdateCommandState();
    ListView_EnsureVisible(Child(List), editing_row, FALSE);
    PositionEditor();
    const HWND input = Child(column == std::to_underlying(ColumnId::Code) ? EditCodeInput : EditWordInput);
    ::SetFocus(input);
    ::SendMessageW(input, EM_SETSEL, 0, -1);
    last_message = record->source == DictEntrySource::UserDict
        ? Text(L"行内编辑：Enter 保存，Esc 取消。", L"Inline edit: Enter to save, Esc to cancel.")
        : Text(L"行内编辑：另存为用户词条；Enter 保存，Esc 取消。", L"Inline edit: save as a user word; Enter to save, Esc to cancel.");
    UpdateStatus();
}

void DictManagerWindow::Impl::BeginNew() {
    if (editor.Active()) {
        BeginEdit(editing_row, std::to_underlying(ColumnId::Word));
        return;
    }
    if (!editor.BeginNew()) return;
    editing_row = 0;
    {
        updating_editor = true;
        const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
        ::SetWindowTextW(Child(EditWordInput), L"");
        ::SetWindowTextW(Child(EditCodeInput), L"");
    }
    RebuildRows();
    UpdateCommandState();
    ListView_EnsureVisible(Child(List), editing_row, FALSE);
    PositionEditor();
    ::SetFocus(Child(EditWordInput));
    last_message = Text(L"新增词条：输入词条可自动取码；Enter 保存，Esc 取消。",
        L"New word: type a word for auto code; Enter to save, Esc to cancel.");
    UpdateStatus();
}

void DictManagerWindow::Impl::CancelEditing(bool focus_list) {
    if (!editor.Active()) return;
    editor.Cancel();
    editing_row = -1;
    ime_composing = false;
    RebuildRows();
    UpdateCommandState();
    if (focus_list) ::SetFocus(Child(List));
    last_message = Text(L"已取消编辑，未保存更改。", L"Edit cancelled; no changes saved.");
    UpdateStatus();
    Tick();
}

void DictManagerWindow::Impl::Tick() {
    const auto preferences = NormalizeDisplayPreferences(GetDisplayPreferences(LoadConfig()));
    if (preferences != theme.display_preferences) {
        std::ignore = UpdateDisplay(preferences, theme.dpi);
    }
    if (editor.Active()) return;
    if (model.PollExternalChanges()) {
        RebuildRows();
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::UpdateStatus() {
    auto status = std::format(
        L"{}: {} / {} | {}: {} | {}: {} | {}: {} | {}: {} | {}: {}",
        Text(L"当前显示", L"Visible"),
        rows.size() - (editor.Active() && std::ranges::find(rows, uint64_t{0}) != rows.end() ? 1 : 0),
        model.Records().size(),
        Text(L"用户词", L"User"),
        model.UserEntryCount(),
        Text(L"置顶", L"Pinned"),
        model.PinnedEntryCount(),
        Text(L"已屏蔽", L"Blocked"),
        model.BlockedEntryCount(),
        Text(L"词组排序", L"Phrase ordering"),
        model.PhraseFreqCount(),
        Text(L"系统词库", L"System dict"),
        model.SystemEntryCount());
    if (model.ActivePrimaryDict() == PrimaryDict::Custom && !model.ActivePrimaryDictPath().empty()) {
        status += std::format(
            L" | {}: {}",
            Text(L"自定义主词库", L"Custom dict"),
            model.ActivePrimaryDictPath().filename().wstring());
    }
    if (!last_message.empty()) {
        status = last_message + L" | " + status;
    }
    if (editor.Dirty()) {
        status = std::wstring(Text(L"* 未保存的更改", L"* Unsaved changes")) + L" | " + status;
    }
    ::SetWindowTextW(Child(Status), status.c_str());
}

std::wstring DictManagerWindow::Impl::Cell(const DictRecord& record, int column) const {
    switch (gsl::narrow_cast<ColumnId>(column)) {
    case ColumnId::Code: return record.code;
    case ColumnId::Word: return record.word;
    case ColumnId::Source: return SourceText(record.source);
    case ColumnId::Rank: return std::to_wstring(record.rank_in_code);
    default: return {};
    }
}

LRESULT DictManagerWindow::Impl::Notify(NMHDR& header) {
    if (header.idFrom != gsl::narrow_cast<UINT_PTR>(std::to_underlying(List))) return 0;
    if (header.code == NM_CUSTOMDRAW) {
        const auto& draw = *reinterpret_cast<const NMLVCUSTOMDRAW*>(&header);
        if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
        if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYSUBITEMDRAW;
        if (draw.iSubItem == std::to_underlying(ColumnId::Edit)) {
            if (draw.nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) return CDRF_NOTIFYPOSTPAINT;
            if (draw.nmcd.dwDrawStage == (CDDS_ITEMPOSTPAINT | CDDS_SUBITEM)) {
                RECT cell{};
                const HWND list = Child(List);
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
                if (!ListView_GetItemRect(list, gsl::narrow_cast<int>(draw.nmcd.dwItemSpec), &cell, LVIR_BOUNDS)) {
                    ReportCommandError(this, "ListView_GetItemRect(edit icon)", ERROR_NOT_FOUND);
                    return CDRF_DODEFAULT;
                }
                cell.right = cell.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit));
                const ScopedSaveDC saved(draw.nmcd.hdc);
                if (!saved) {
                    ReportCommandError(this, "SaveDC(edit icon)", ::GetLastError());
                    return CDRF_DODEFAULT;
                }
                const int clipped = ::IntersectClipRect(draw.nmcd.hdc, cell.left, cell.top, cell.right, cell.bottom);
                if (clipped == ERROR) {
                    ReportCommandError(this, "IntersectClipRect(edit icon)", ::GetLastError());
                    return CDRF_DODEFAULT;
                }
                if (clipped == NULLREGION) return CDRF_DODEFAULT;
                const int size = std::max(1, std::min({theme.ScalePx(layout::kEditIconSize),
                    gsl::narrow_cast<int>(cell.right - cell.left) - theme.ScalePx(layout::kEditIconPadding),
                    gsl::narrow_cast<int>(cell.bottom - cell.top) - theme.ScalePx(layout::kEditIconPadding)}));
                command_ui.DrawIcon(draw.nmcd.hdc, cell, ToolIcon::Edit, size,
                    std::max(1, theme.ScalePx(layout::kEditIconStrokeWidth)), ::GetTextColor(draw.nmcd.hdc));
            }
        }
        return CDRF_DODEFAULT;
    } else if (header.code == LVN_GETINFOTIPW) {
        auto& info = *reinterpret_cast<NMLVGETINFOTIPW*>(&header);
        if (info.iSubItem == std::to_underlying(ColumnId::Edit) && info.pszText && info.cchTextMax > 0) {
            wcsncpy_s(info.pszText, gsl::narrow_cast<size_t>(info.cchTextMax),
                Text(L"编辑词条（编码）", L"Edit entry (code)"), _TRUNCATE);
        }
    } else if (header.code == LVN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMLVDISPINFOW*>(&header);
        if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 &&
            gsl::narrow_cast<size_t>(info.item.iItem) < rows.size()) {
            const auto id = gsl::at(rows, gsl::narrow_cast<size_t>(info.item.iItem));
            const auto* rec = id == 0 && editor.Original() ? editor.Original() : model.FindById(id);
            {
                auto text = rec ? Cell(*rec, info.item.iSubItem)
                    : (info.item.iSubItem == std::to_underlying(ColumnId::Source)
                        ? Text(L"新增用户词", L"New User Word") : L"");
                if (editor.Dirty() && info.item.iItem == editing_row &&
                    info.item.iSubItem == std::to_underlying(ColumnId::Source)) {
                    text.insert(0, L"* ");
                }
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
    } else if (header.code == NM_DBLCLK) {
        const auto& click = *reinterpret_cast<const NMITEMACTIVATE*>(&header);
        LVHITTESTINFO hit{};
        hit.pt = click.ptAction;
        const int row = ListView_SubItemHitTest(Child(List), &hit);
        if (row >= 0) {
            if (hit.iSubItem == std::to_underlying(ColumnId::Code) ||
                hit.iSubItem == std::to_underlying(ColumnId::Word)) {
                BeginEdit(row, hit.iSubItem);
            }
        }
    } else if (header.code == LVN_ENDSCROLL) {
        PositionEditor();
    }
    return 0;
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

} // namespace qiqi::dict_manager
