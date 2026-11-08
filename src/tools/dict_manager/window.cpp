#include "tools/dict_manager/internal.h"
#include "common/about_dialog.h"
#include "common/dialog_util.h"
#include "common/file_util.h"
#include "common/theme.h"
#include "common/window.h"
#include "common/version.h"
#include "common/win32_ui_util.h"
#include "tools/dictionary_validation.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <format>
#include <stdexcept>
#include <span>
#include <system_error>
#include <uxtheme.h>
#include <utility>

namespace qiqi::dict_manager {
using enum ControlId;

namespace {
constexpr std::array toolbar_commands{
    Reload, NewWord, Copy, Import, Export, Pin, Delete, Promote};

[[noreturn]] void ThrowUiError(const char* operation, DWORD error) {
    QIQI_LOG_ERROR("DictManager {} failed (error={})", operation, error);
    throw std::system_error(error, std::system_category(), operation);
}

}

std::wstring FormatBackupRestoreMessage(const BackupRestoreResult& result, UiLanguage language) {
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    const auto text = [english](const wchar_t* zh, const wchar_t* en) noexcept { return english ? en : zh; };
    if (result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK) {
        return std::format(L"{} ({}: {}, {}: {})",
            text(L"已恢复备份", L"Backup restored"),
            text(L"词库规则", L"Dict rules"), result.summary.user_entries_applied,
            text(L"词组排序", L"Phrase ordering"), result.summary.phrase_freq_applied);
    }
    const wchar_t* stage = L"";
    switch (result.stage) {
    case BackupRestoreStage::Read: stage = text(L"读取", L"read"); break;
    case BackupRestoreStage::Validate: stage = text(L"校验", L"validation"); break;
    case BackupRestoreStage::Prepare: stage = text(L"准备", L"preparation"); break;
    case BackupRestoreStage::UserDictionary: stage = text(L"词库规则", L"dictionary"); break;
    case BackupRestoreStage::PhraseFrequency: stage = text(L"词组排序", L"phrase ordering"); break;
    case BackupRestoreStage::Config: stage = text(L"配置", L"configuration"); break;
    case BackupRestoreStage::Refresh: stage = text(L"刷新", L"refresh"); break;
    case BackupRestoreStage::Complete: break;
    }
    std::wstring message = std::format(L"{} ({}; {}: {}, {}: {}; 0x{:08X})",
        result.status == BackupRestoreStatus::Partial ?
            text(L"备份恢复未完成；可能已应用部分更改", L"Backup restore incomplete; changes may already have been applied") :
            text(L"恢复备份失败", L"Backup restore failed"),
        stage, text(L"成功操作", L"Succeeded operations"), result.operations_succeeded,
        text(L"失败操作", L"Failed operations"), result.operations_failed,
        gsl::narrow_cast<uint32_t>(result.error));
    if (FAILED(result.refresh_error)) {
        message += std::format(L" {} (0x{:08X})",
            text(L"刷新失败；当前显示可能不完整", L"Refresh failed; the displayed view may be incomplete"),
            gsl::narrow_cast<uint32_t>(result.refresh_error));
    }
    return message;
}

void DictManagerWindow::Impl::ReportCommandError(void*, const char* operation, ULONG error) noexcept {
    QIQI_LOG_ERROR("DictManager UI operation '{}' failed (error=0x{:08X})", operation, error);
}

ToolCommand DictManagerWindow::Impl::UiCommand(ControlId id) const noexcept {
    ToolIcon icon = ToolIcon::None;
    switch (id) {
    case NewWord: icon = ToolIcon::New; break;
    case Reload: icon = ToolIcon::Reload; break;
    case Copy: icon = ToolIcon::Copy; break;
    case Import: icon = ToolIcon::Import; break;
    case Export: icon = ToolIcon::Save; break;
    case SaveWord: icon = ToolIcon::Save; break;
    case Pin: icon = ToolIcon::Pin; break;
    case Delete: icon = ToolIcon::Delete; break;
    case Promote: icon = ToolIcon::Promote; break;
    default: break;
    }
    return {id == Separator ? 0u : gsl::narrow_cast<UINT>(std::to_underlying(id)), CommandText(id), icon, true,
        id >= LanguageAuto && id <= LanguageEn &&
            std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(ui_language)};
}

const wchar_t* DictManagerWindow::Impl::CommandText(ControlId id) const noexcept {
    switch (id) {
    case Reload: return Text(L"刷新", L"Reload");
    case Copy: return Text(L"复制", L"Copy");
    case Import: return Text(L"导入词库...", L"Import...");
    case Export: return Text(L"导出快照...", L"Export...");
    case SelectPrimaryDict: return Text(L"选择自定义主词库...", L"Select Custom Primary Dict...");
    case RestoreDefaultPrimaryDict: return Text(L"恢复默认主词库 (极点 86)", L"Restore Default Primary Dict (Jidian 86)");
    case Backup: return Text(L"备份全部...", L"Backup...");
    case Restore: return Text(L"恢复备份...", L"Restore...");
    case ClearFreq: return Text(L"重置词组排序", L"Reset Phrase Ordering");
    case About: return Text(L"关于...", L"About...");
    case AutoCode: return Text(L"自动取码", L"Auto Code");
    case NewWord: return Text(L"新增词条", L"New Word");
    case CancelEdit: return Text(L"取消编辑\tEsc", L"Cancel Edit\tEsc");
    case SaveWord: return Text(L"保存用户词", L"Save Word");
    case Pin: return Text(L"置顶 / 取消置顶", L"Pin / Unpin");
    case Delete: return Text(L"删除 / 屏蔽 / 恢复", L"Delete / Block / Restore");
    case Promote: return Text(L"词组置前", L"Prioritize Phrase");
    case SelectAll: return Text(L"全选", L"Select All");
    case LanguageAuto: return GetUiStrings(ui_language).dlg_lang_auto;
    case LanguageZh: return GetUiStrings(ui_language).dlg_lang_zh;
    case LanguageEn: return GetUiStrings(ui_language).dlg_lang_en;
    case Exit: return Text(L"退出", L"Exit");
    default: return L"";
    }
}

const wchar_t* DictManagerWindow::Impl::MenuHeading(size_t index) const noexcept {
    switch (index) {
    case 0: return Text(L"文件(&F)", L"&File");
    case 1: return Text(L"编辑(&E)", L"&Edit");
    case 2: return Text(L"语言(&L)", L"&Language");
    default: return Text(L"帮助(&H)", L"&Help");
    }
}

void DictManagerWindow::Impl::UpdateCommands() {
    const std::array file{UiCommand(Reload), UiCommand(Separator), UiCommand(SelectPrimaryDict),
        UiCommand(RestoreDefaultPrimaryDict), UiCommand(Separator), UiCommand(Import), UiCommand(Export),
        UiCommand(Separator), UiCommand(Backup), UiCommand(Restore), UiCommand(Separator), UiCommand(Exit)};
    const std::array edit{UiCommand(NewWord), UiCommand(SaveWord), UiCommand(CancelEdit), UiCommand(AutoCode),
        UiCommand(Separator), UiCommand(Copy), UiCommand(SelectAll), UiCommand(Pin), UiCommand(Delete),
        UiCommand(Promote), UiCommand(Separator), UiCommand(ClearFreq)};
    const std::array language{UiCommand(LanguageAuto), UiCommand(LanguageZh), UiCommand(LanguageEn)};
    const std::array help{UiCommand(About)};
    const std::array menus{
        ToolMenu{MenuHeading(0), L'f', file}, ToolMenu{MenuHeading(1), L'e', edit},
        ToolMenu{MenuHeading(2), L'l', language, true}, ToolMenu{MenuHeading(3), L'h', help}};
    command_ui.SetMenu(hwnd, menus);
    std::array<ToolCommand, toolbar_commands.size()> buttons{};
    std::ranges::transform(toolbar_commands, buttons.begin(), [this](ControlId id) { return UiCommand(id); });
    command_ui.SetToolbar(Child(Toolbar), buttons);
    const std::array save{UiCommand(SaveWord)};
    command_ui.SetToolbar(Child(InlineSave), save);
    UpdateCommandState();
}

void DictManagerWindow::Impl::Failure(std::wstring_view operation, ULONG error) {
    QIQI_LOG_CAT_ERROR(
        log::Keyword::Dictionary,
        "DictManager failure: op='{}', error=0x{:08X}",
        WideToUtf8(operation),
        error);
    const auto message = std::format(L"{}\r\n{}={} (0x{:08X})", operation, Text(L"错误码", L"Error"), error, error);
    ::MessageBoxW(hwnd, message.c_str(), Text(L"奇奇五笔词库管理", L"QiQiIME Dictionary Manager"), MB_OK | MB_ICONERROR);
}

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

LRESULT CALLBACK DictManagerWindow::Impl::ListProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, ListProcedure, subclass);
    } else if (message == WM_KEYDOWN && self && self->ready) {
        try {
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
                self->Command(std::to_underlying(Delete), 0);
                return 0;
            } else if (wparam == VK_F2) {
                self->BeginEdit(ListView_GetNextItem(window, -1, LVNI_FOCUSED), std::to_underlying(ColumnId::Word));
                return 0;
            } else if (wparam == VK_ESCAPE && self->editor.Active()) {
                self->CancelEditing();
                return 0;
            }
        } catch (const std::exception&) {
            QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary, "DictManager ListProcedure shortcut failed with an exception");
            return 0;
        }
    } else if (message == WM_NOTIFY && self) {
        if (const auto drawn = TryCustomDrawDarkListViewHeader(window, lparam, self->font.Get(), self->theme)) {
            return *drawn;
        }
    }
    int activate_edit_row = -1;
    bool edit_mouse_handled = false;
    if (self && self->ready) {
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
            LVHITTESTINFO hit{};
            hit.pt = PointFromLParam(lparam);
            const int row = ListView_SubItemHitTest(window, &hit);
            const bool edit_icon = row >= 0 && hit.iSubItem == std::to_underlying(ColumnId::Edit);
            if (message == WM_LBUTTONDOWN) {
                self->pressed_edit_row = edit_icon ? row : -1;
                if (edit_icon) {
                    ::SetFocus(window);
                    ListView_SetItemState(window, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_SetItemState(window, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ::SetCapture(window);
                    if (::GetCapture() != window) {
                        self->pressed_edit_row = -1;
                        ReportCommandError(self, "SetCapture(edit icon)", ERROR_SUCCESS);
                    }
                    edit_mouse_handled = true;
                }
            } else {
                const int pressed = std::exchange(self->pressed_edit_row, -1);
                if (pressed >= 0) {
                    if (::GetCapture() == window && !::ReleaseCapture()) {
                        ReportCommandError(self, "ReleaseCapture(edit icon)", ::GetLastError());
                    }
                    if (edit_icon && row == pressed) activate_edit_row = row;
                    edit_mouse_handled = true;
                }
            }
        } else if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE) {
            self->pressed_edit_row = -1;
            if (message == WM_CANCELMODE && ::GetCapture() == window && !::ReleaseCapture()) {
                ReportCommandError(self, "ReleaseCapture(cancel edit icon)", ::GetLastError());
            }
        }
    }
    const LRESULT result = edit_mouse_handled ? 0 : ::DefSubclassProc(window, message, wparam, lparam);
    if (self && self->ready && activate_edit_row >= 0) {
        try {
            self->BeginEdit(activate_edit_row, std::to_underlying(ColumnId::Code));
        } catch (const std::exception&) {
            QIQI_LOG_ERROR("DictManager edit icon operation failed with an exception");
            ::SetWindowTextW(self->Child(Status), self->Text(L"编辑操作失败，请重试。", L"Edit operation failed; please retry."));
        }
    }
    if (self && self->ready && (message == WM_HSCROLL || message == WM_VSCROLL ||
            message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_SIZE ||
            message == WM_NOTIFY || message == LVM_SCROLL || message == LVM_ENSUREVISIBLE)) {
        self->PositionEditor();
    }
    return result;
}

LRESULT CALLBACK DictManagerWindow::Impl::EditorProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, EditorProcedure, subclass);
    } else if (self) {
        if (message == WM_IME_STARTCOMPOSITION) self->ime_composing = true;
        if (message == WM_IME_ENDCOMPOSITION) self->ime_composing = false;
        if (message == WM_GETDLGCODE && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            return ::DefSubclassProc(window, message, wparam, lparam) | DLGC_WANTMESSAGE;
        }
        if (message == WM_CHAR && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) return 0;
        if (message == WM_KEYDOWN && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            try {
                if (wparam == VK_RETURN) self->ExecuteAddOrUpdate();
                else self->CancelEditing();
            } catch (const std::exception&) {
                QIQI_LOG_ERROR("DictManager inline editor keyboard operation failed");
                ::SetWindowTextW(self->Child(Status), self->Text(L"编辑操作失败，请重试。", L"Edit operation failed; please retry."));
            }
            return 0;
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
    const HRESULT columns_hr = InsertListViewColumns(Child(List), columns, widths, theme.dpi);
    if (FAILED(columns_hr)) {
        Failure(L"ListView_InsertColumn", gsl::narrow_cast<ULONG>(columns_hr));
        return false;
    }

    ready = true;
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

    const int filter_top = command_ui.LayoutToolbar(Child(Toolbar), width, theme) + s(12);
    move(SourceLabel, s(12), filter_top + s(4), s(56), s(24));
    move(SourceCombo, s(72), filter_top, s(165), s(220));
    move(SearchLabel, s(250), filter_top + s(4), s(64), s(24));
    move(SearchEdit, s(318), filter_top, width - s(330), s(28));

    const int list_top = filter_top + s(40);
    move(List, s(12), list_top, width - s(24), std::max(s(80), gsl::narrow_cast<int>(rect.bottom) - s(40) - list_top));

    move(Status, s(12), rect.bottom - s(32), width - s(24), s(24));
    PositionEditor();
}

void DictManagerWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(SourceCombo),
        Child(SearchEdit),
        Child(EditWordInput), Child(EditCodeInput)};
    std::ignore = ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls);
    command_ui.ApplyTheme(hwnd);
}

void DictManagerWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = std::format(
        L"{} v{} ({})",
        Text(L"奇奇五笔词库与词频管理", L"QiQiIME Dictionary & Frequency Manager"),
        version::kVersionW,
        version::kArchW);
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
        for (int i = 0; i < std::to_underlying(ColumnId::Count); ++i) {
            ListView_SetColumnWidth(
                Child(List),
                i,
                ::MulDiv(ListView_GetColumnWidth(Child(List), i), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {SourceCombo}) {
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), theme.ScalePx(22));
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, 0, theme.ScalePx(22));
        }
        UpdateCommands();
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

bool DictManagerWindow::Impl::CanExecute(ControlId id) const noexcept {
    switch (id) {
        case NewWord: return editor.CanExecute(EditorCommand::NewWord);
        case SaveWord: return editor.CanExecute(EditorCommand::Save);
        case CancelEdit: return editor.CanExecute(EditorCommand::Cancel);
        case AutoCode: return editor.CanExecute(EditorCommand::AutoCode);
        case Reload:
        case SelectPrimaryDict:
        case RestoreDefaultPrimaryDict:
        case Import:
        case Restore:
        case Pin:
        case Delete:
        case Promote:
        case ClearFreq: return editor.CanExecute(EditorCommand::ChangeRecords);
        default: return true;
    }
}

void DictManagerWindow::Impl::UpdateCommandState() noexcept {
    for (const auto id : {Reload, NewWord, SelectPrimaryDict, RestoreDefaultPrimaryDict, Import, Restore, Pin, Delete, Promote, ClearFreq, SaveWord, CancelEdit, AutoCode}) {
        const bool enabled = CanExecute(id);
        ::EnableMenuItem(::GetMenu(hwnd), std::to_underlying(id), MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
        ::SendMessageW(Child(Toolbar), TB_ENABLEBUTTON, std::to_underlying(id), MAKELONG(enabled, 0));
    }
    ::SendMessageW(Child(InlineSave), TB_ENABLEBUTTON, std::to_underlying(SaveWord), MAKELONG(CanExecute(SaveWord), 0));
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
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& cell = gsl::at(cells, i);
        ::SetWindowPos(Child(gsl::at(controls, i)), nullptr,
            cell.left + 1, cell.top + 1, std::max(1L, cell.right - cell.left - 2),
            std::max(1L, cell.bottom - cell.top - 2), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    const auto& action = cells.back();
    const int size = std::max(1, std::min({theme.ScalePx(26),
        gsl::narrow_cast<int>(action.right - action.left - 2),
        gsl::narrow_cast<int>(action.bottom - action.top - 2)}));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONWIDTH, 0, MAKELPARAM(size, size));
    ::SetWindowPos(Child(InlineSave), nullptr, action.left + (action.right - action.left - size) / 2,
        action.top, size, action.bottom - action.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
                const int size = std::max(1, std::min({theme.ScalePx(16),
                    gsl::narrow_cast<int>(cell.right - cell.left) - theme.ScalePx(4),
                    gsl::narrow_cast<int>(cell.bottom - cell.top) - theme.ScalePx(4)}));
                command_ui.DrawIcon(draw.nmcd.hdc, cell, ToolIcon::Edit, size,
                    std::max(1, theme.ScalePx(1)), ::GetTextColor(draw.nmcd.hdc));
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

void DictManagerWindow::Impl::AutoFillCodeFromWord() {
    if (!editor.CanExecute(EditorCommand::AutoCode)) return;
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    const std::wstring auto_code = model.ComputeAutoCode(word);
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditCodeInput), auto_code.c_str());
    UpdateEditorState();
}

void DictManagerWindow::Impl::ExecuteAddOrUpdate() {
    if (!editor.CanExecute(EditorCommand::Save) || ime_composing) return;
    UpdateEditorState();
    if (editor.PrepareSave(model.Records()) == EditorSaveAction::Unchanged) {
        CancelEditing();
        last_message = Text(L"内容未修改，无需保存。", L"No changes to save.");
        UpdateStatus();
        return;
    }
    if (editor.Code().empty() && !editor.Word().empty() && editor.WantsAutomaticCode()) {
        AutoFillCodeFromWord();
    }
    if (model.PollExternalChanges()) RebuildRows();
    const auto action = editor.PrepareSave(model.Records());
    if (action == EditorSaveAction::MissingOriginal) {
        last_message = Text(L"原词条已被其他进程修改或删除；草稿已保留，请取消后重新编辑。",
            L"The original entry changed or was removed externally; draft preserved. Cancel and edit again.");
        UpdateStatus();
        return;
    }
    HRESULT save_hr = E_UNEXPECTED;
    if (action == EditorSaveAction::ReplaceUser) {
        const auto* original = editor.Original();
        save_hr = model.ReplaceUserEntryResult(original->code, original->word, editor.Code(), editor.Word());
    } else {
        save_hr = model.AddOrUpdateUserEntryResult(editor.Code(), editor.Word());
    }
    const bool ok = log::CheckHResult(save_hr, "DictManager::SaveWord");
    editor.CompleteSave(ok);
    if (!ok) {
        const bool permission_denied =
            save_hr == E_ACCESSDENIED || save_hr == HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD) ||
            save_hr == HRESULT_FROM_WIN32(ERROR_WRITE_PROTECT);
        last_message = std::format(L"{} (HRESULT=0x{:08X})",
            permission_denied
                ? Text(L"保存失败：访问被拒绝或写保护，草稿已保留。请检查词库及同步目录权限、只读属性和文件占用，或改用当前用户可写的词库目录。",
                       L"Save failed: access denied or write-protected. Draft preserved. Check dictionary/sync permissions, read-only attributes and file locks, or use a writable user directory.")
                : Text(L"保存失败，草稿已保留；请检查编码、词条及外部更改。",
                       L"Save failed; draft preserved. Check code, word and external changes."),
            gsl::narrow_cast<unsigned int>(save_hr));
    } else {
        editing_row = -1;
        last_message = Text(L"已保存用户词条并同步", L"Saved user entry and synchronized");
        RebuildRows();
        UpdateCommandState();
        ::SetFocus(Child(List));
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePinSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    const bool pinned = record->source == DictEntrySource::Pinned;
    if (pinned ? model.UnpinUserEntry(record->code, record->word) : model.PinUserEntry(record->code, record->word)) {
        last_message = pinned ? Text(L"已取消固定置顶并同步", L"Unpinned entry and synchronized")
            : Text(L"已固定置顶并同步", L"Pinned entry to top and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"固定置顶失败（请检查五笔编码与词条）", L"Failed to pin entry");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteDeleteSelected() {
    const auto selected = CollectSelectedRecords();
    if (selected.empty()) return;
    size_t changed_count = 0;
    for (const auto& rec : selected) {
        switch (rec.source) {
        case DictEntrySource::UserDict:
            if (model.RemoveUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Pinned:
            if (model.UnpinUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Blocked:
            if (model.UnblockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PhraseFreq:
            if (model.RemovePhraseFrequency(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PrimaryDict:
        case DictEntrySource::ExtraDict:
            if (model.BlockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        default:
            break;
        }
    }
    if (changed_count > 0) {
        last_message = std::format(
            L"{}: {}",
            Text(L"已处理（删除/屏蔽/恢复）记录数", L"Processed (deleted/blocked/restored) records"),
            changed_count);
        RebuildRows();
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePromoteSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    if (model.PromotePhraseFrequency(record->code, record->word)) {
        last_message = Text(L"已将词组置前并同步", L"Prioritized phrase and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"词组置前仅支持双字及以上词组", L"Prioritizing requires a multi-character phrase");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteClearFrequencies() {
    if (model.ClearAllPhraseFrequencies()) {
        last_message = Text(L"已重置全部词组排序", L"Reset all phrase ordering");
        RebuildRows();
    } else {
        last_message = Text(L"重置词组排序失败", L"Failed to reset phrase ordering");
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
    if (!menu) ThrowUiError("CreatePopupMenu", ::GetLastError());
    const auto selected = CollectSelectedRecords();
    const bool has_sel = !selected.empty();
    for (const auto id : {Copy, SelectAll, Pin, Promote, Delete}) {
        std::wstring text(CommandText(id));
        if (id == Copy) text += L"\tCtrl+C";
        if (id == SelectAll) text += L"\tCtrl+A";
        if (id == Delete) text += L"\tDel";
        const bool enabled = (id == SelectAll ? !rows.empty() : has_sel) && CanExecute(id);
        if (!::AppendMenuW(menu.Get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED),
                std::to_underlying(id), text.c_str())) {
            ThrowUiError("AppendMenuW", ::GetLastError());
        }
    }
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    if (cmd) Command(cmd, 0);
}

void DictManagerWindow::Impl::SelectPrimaryDictDialog() {
    std::filesystem::path selected;
    if (!dialog_detail::PromptForPrimaryDictionaryFile(
            hwnd, ui_language, model.ActivePrimaryDictPath().wstring(), selected)) {
        return;
    }
    const auto validation = WubiMappedDict::ValidateAndLoadPrimaryDictionary(selected);
    if (validation && !dictionary_ui::ConfirmPartialDictionary(hwnd, ui_language, validation)) {
        return;
    }
    if (SUCCEEDED(model.SelectValidatedCustomPrimaryDictionary(selected, validation))) {
        last_message = std::format(
            L"{}: {}",
            Text(L"已切换自定义主词库", L"Switched custom primary dictionary"),
            selected.filename().wstring());
        RebuildRows();
        UpdateStatus();
    } else {
        last_message = Text(
            L"所选主词库无效或无法读取，已保留当前词库",
            L"Selected primary dictionary is invalid or unreadable; kept current dictionary");
        UpdateStatus();
        ::MessageBoxW(
            hwnd,
            GetUiStrings(ui_language).dlg_invalid_primary_dict,
            Text(L"奇奇五笔词库管理", L"QiQiIME Dictionary Manager"),
            MB_OK | MB_ICONWARNING);
    }
}

void DictManagerWindow::Impl::ExecuteRestoreDefaultPrimaryDict() {
    if (model.RestoreDefaultPrimaryDictionary()) {
        last_message = Text(
            L"已恢复默认主词库（极点五笔 86）",
            L"Restored default primary dictionary (Jidian Wubi 86)");
        RebuildRows();
    } else {
        last_message = Text(
            L"恢复默认主词库失败",
            L"Failed to restore default primary dictionary");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ImportUserDictDialog() {
    std::array<wchar_t, 32768> path{};
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"导入用户词库文件 (UTF-8)", L"Import User Dictionary File (UTF-8)"),
            Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Open)) return;
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
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"导出当前筛选词条 (UTF-8)", L"Export Filtered Entries (UTF-8)"),
            Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Save, L"txt")) return;
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

void DictManagerWindow::Impl::BackupBundleDialog() {
    std::array<wchar_t, 32768> path{};
    wcscpy_s(path.data(), path.size(), L"qiqiime_backup.qbak");
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"备份配置、用户词库与词组排序 (.qbak)", L"Backup Config, User Dict & Phrase Ordering (.qbak)"),
            Text(L"奇奇五笔备份 (*.qbak)\0*.qbak\0所有文件\0*.*\0",
                L"QiQiIME Backup (*.qbak)\0*.qbak\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Save, L"qbak")) return;
    if (model.CreateBackupBundle(path.data())) {
        last_message = Text(L"已导出完整备份文件 (.qbak)", L"Exported full backup bundle (.qbak)");
    } else {
        last_message = Text(L"创建备份文件失败", L"Failed to create backup bundle");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::RestoreBundleDialog() {
    std::array<wchar_t, 32768> path{};
    if (!dialog_detail::PromptForFile(hwnd,
            Text(L"选择要恢复的奇奇五笔备份 (.qbak)", L"Select QiQiIME Backup to Restore (.qbak)"),
            Text(L"奇奇五笔备份 (*.qbak)\0*.qbak\0所有文件\0*.*\0",
                L"QiQiIME Backup (*.qbak)\0*.qbak\0All files\0*.*\0"),
            path, dialog_detail::FileDialogMode::Open)) return;

    const int choice = ::MessageBoxW(
        hwnd,
        Text(
            L"请选择恢复方式：\r\n"
            L"• 点击「是 (Y)」：合并模式（保留现有用户词库与词组排序，合并备份项）\r\n"
            L"• 点击「否 (N)」：覆盖模式（用备份完全替换现有用户词库与词组排序）\r\n"
            L"• 点击「取消」：中止恢复",
            L"Choose restore conflict mode:\r\n"
            L"• Yes: Merge (keep existing user words & phrase ordering, merge backup items)\r\n"
            L"• No: Replace (overwrite existing user dictionary & phrase ordering with backup)\r\n"
            L"• Cancel: Abort restore"),
        Text(L"恢复配置与词库备份", L"Restore Config & Dictionary Backup"),
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (choice == IDCANCEL) return;

    const BackupConflictMode mode = (choice == IDNO) ? BackupConflictMode::Replace : BackupConflictMode::Merge;
    const auto result = model.RestoreBackupBundle(path.data(), mode);
    last_message = FormatBackupRestoreMessage(result, ui_language);
    if (result.stage != BackupRestoreStage::Read && result.stage != BackupRestoreStage::Validate) {
        RebuildRows();
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::Command(int id, int notification) {
    if (!CanExecute(gsl::narrow_cast<ControlId>(id))) {
        if (editor.Active()) {
            last_message = Text(L"请先保存当前编辑，或按 Esc 取消。", L"Save the current edit or press Esc to cancel first.");
            UpdateStatus();
        }
        return;
    }
    switch (gsl::narrow_cast<ControlId>(id)) {
    case NewWord:
        BeginNew();
        break;
    case CancelEdit:
        CancelEditing();
        break;
    case Reload:
        model.ReloadAll();
        last_message = Text(L"已重新加载词库与词组排序", L"Reloaded dictionaries and phrase ordering");
        RebuildRows();
        UpdateStatus();
        break;
    case Copy:
        CopySelection();
        break;
    case SelectAll:
        SelectAllRows();
        break;
    case Exit:
        ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    case SelectPrimaryDict:
        SelectPrimaryDictDialog();
        break;
    case RestoreDefaultPrimaryDict:
        ExecuteRestoreDefaultPrimaryDict();
        break;
    case Import:
        ImportUserDictDialog();
        break;
    case Export:
        ExportSnapshotDialog();
        break;
    case Backup:
        BackupBundleDialog();
        break;
    case Restore:
        RestoreBundleDialog();
        break;
    case ClearFreq:
        ExecuteClearFrequencies();
        break;
    case About:
        std::ignore = ShowAboutDialog(instance, hwnd, ui_language, true);
        break;
    case SourceCombo:
        if (editor.Active() && notification == CBN_SETFOCUS) CancelEditing(false);
        if (!editor.Active() && !updating_filters && notification == CBN_SELCHANGE) {
            Refilter();
        }
        break;
    case SearchEdit:
        if (editor.Active() && notification == EN_SETFOCUS) CancelEditing(false);
        if (!editor.Active() && !updating_filters && notification == EN_CHANGE) {
            Refilter();
        }
        break;
    case EditWordInput:
        if (editor.Active() && !updating_editor && notification == EN_CHANGE) {
            if (editor.WantsAutomaticCode()) AutoFillCodeFromWord();
            else UpdateEditorState();
        }
        break;
    case EditCodeInput:
        if (editor.Active() && !updating_editor && notification == EN_CHANGE) {
            UpdateEditorState(CodeChange::Manual);
        }
        break;
    case AutoCode:
        AutoFillCodeFromWord();
        break;
    case SaveWord:
        ExecuteAddOrUpdate();
        break;
    case Pin:
        ExecutePinSelected();
        break;
    case Delete:
        ExecuteDeleteSelected();
        break;
    case Promote:
        ExecutePromoteSelected();
        break;
    case LanguageAuto:
    case LanguageZh:
    case LanguageEn:
        ui_language = gsl::narrow_cast<UiLanguage>(id - std::to_underlying(LanguageAuto));
        ApplyLanguage();
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
        if (const auto handled = self->command_ui.HandleMessage(window, message, wparam, lparam, self->font.Get(), self->theme)) {
            return *handled;
        }
        if (const auto handled = HandleFluentToolWindowMessage(
                window, message, wparam, lparam, self->theme,
                0, 920, 560)) {
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
            if (self->ready && lparam) return self->Notify(*reinterpret_cast<NMHDR*>(lparam));
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
        QIQI_LOG_ERROR("DictManager window operation failed with an exception");
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
    if (!dialog_detail::RegisterDialogWindowClass(instance, kWindowClass.data(), &Impl::Procedure,
            0, ::LoadIconW(instance, MAKEINTRESOURCEW(1)))) {
        impl_->Failure(L"RegisterClassEx", ::GetLastError());
        return false;
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
