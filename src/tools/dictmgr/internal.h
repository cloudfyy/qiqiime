// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/dictmgr/window.h"
#include "tools/dictmgr/model.h"
#include "tools/dictmgr/editor.h"
#include "common/controls.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/toolbar.h"
#include "common/window.h"

#include <commctrl.h>

namespace qiqi::dict_manager {

inline constexpr UINT_PTR kRefreshTimerId = 1;
inline constexpr UINT kRefreshIntervalMs = 500;

namespace dict_manager_layout {
inline constexpr SIZE kInitialWindowSize{1020, 680};
inline constexpr int kWindowMargin = 12;
inline constexpr int kControlGap = 4;
inline constexpr int kComfortControlGap = 8;
inline constexpr int kFilterGroupGap = 13;
inline constexpr int kSourceLabelWidth = 56;
inline constexpr int kSourceComboWidth = 165;
inline constexpr int kComboDropDownHeight = 220;
inline constexpr int kSearchLabelWidth = 64;
inline constexpr int kSearchMinimumWidth = 120;
inline constexpr int kMinimumWindowWidth = 920;
inline constexpr int kMinimumWindowHeight = 560;
inline constexpr int kLabelHeight = 24;
inline constexpr RECT kSourceLabel{kWindowMargin, 4, kWindowMargin + 56, 4 + kLabelHeight};
inline constexpr RECT kSourceCombo{
    kSourceLabel.right + kControlGap, 0, kSourceLabel.right + kControlGap + 165, 220};
inline constexpr RECT kSearchLabel{
    kSourceCombo.right + 13, kSourceLabel.top,
    kSourceCombo.right + 13 + 64, kSourceLabel.top + kLabelHeight};
inline constexpr int kSearchEditLeft = kSearchLabel.right + kControlGap;
inline constexpr int kSearchEditRightReserve = kSearchEditLeft + kWindowMargin;
inline constexpr int kSearchEditHeight = 28;
inline constexpr int kFilterSectionHeight = 40;
inline constexpr int kListMinimumHeight = 80;
inline constexpr int kListBottomInset = 40;
inline constexpr int kStatusBottomOffset = 32;
inline constexpr int kStatusHeight = 24;
inline constexpr int kEditorCellInsetPx = 1;
inline constexpr int kInlineSaveButtonSize = 26;
inline constexpr int kEditIconSize = 16;
inline constexpr int kEditIconPadding = 4;
inline constexpr int kEditIconStrokeWidth = 1;
} // namespace dict_manager_layout

struct DictManagerWindow::Impl {
    DictManagerModel model;
    DictEditor editor;
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    ScopedImageList row_images;
    FluentToolWindowViewport viewport;
    ScopedIcon small_icon;
    ScopedIcon big_icon;
    ToolCommandUi command_ui{ReportCommandError, this};
    UiLanguage ui_language = UiLanguage::Auto;
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    DictManagerWindow* owner = nullptr;
    std::vector<uint64_t> rows;
    std::vector<uint64_t> selected_ids;
    uint64_t selected_id = 0;
    DictFilter filter{};
    std::wstring last_message;
    bool ready = false;
    bool updating_layout = false;
    bool updating_rows = false;
    bool updating_filters = false;
    bool updating_editor = false;
    bool ime_composing = false;
    int editing_row = -1;
    int pressed_edit_row = -1;

    HWND Child(ControlId id) const noexcept { return GetChildControl(hwnd, id); }
    const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const noexcept {
        return LocalizedUiText(ui_language, zh, en);
    }

    const wchar_t* SourceText(DictEntrySource src) const noexcept {
        switch (src) {
        case DictEntrySource::UserDict:    return Text(L"用户词库", L"User Dict");
        case DictEntrySource::Pinned:      return Text(L"手动置顶", L"Pinned");
        case DictEntrySource::Blocked:     return Text(L"已屏蔽词", L"Blocked");
        case DictEntrySource::PhraseFreq:  return Text(L"词组排序", L"Phrase Ordering");
        case DictEntrySource::PrimaryDict: return Text(L"主词库", L"Primary Dict");
        case DictEntrySource::ExtraDict:   return Text(L"极点扩展词库", L"Jidian Extra Dict");
        default:                           return Text(L"全部来源", L"All Sources");
        }
    }

    static constexpr DictEntrySource SourceFromComboIndex(LRESULT index) noexcept {
        switch (index) {
        case 1: return DictEntrySource::UserDict;
        case 2: return DictEntrySource::Pinned;
        case 3: return DictEntrySource::Blocked;
        case 4: return DictEntrySource::PhraseFreq;
        case 5: return DictEntrySource::PrimaryDict;
        case 6: return DictEntrySource::ExtraDict;
        default: return DictEntrySource::All;
        }
    }

    void UpdateIcons() noexcept;
    void Failure(std::wstring_view operation, ULONG error);
    bool Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id);
    static LRESULT CALLBACK ListProcedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR context) noexcept;
    static LRESULT CALLBACK EditorProcedure(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR context) noexcept;
    bool CreateControls();
    void UpdateCommands();
    static void ReportCommandError(void*, const char* operation, ULONG error) noexcept;
    ToolCommand UiCommand(ControlId id) const noexcept;
    const wchar_t* CommandText(ControlId id) const noexcept;
    const wchar_t* MenuHeading(size_t index) const noexcept;
    void Layout() noexcept;
    void UpdateTheme() noexcept;
    bool UpdateDisplay(DisplayPreferences preferences, UINT dpi) noexcept;
    void UpdateDisplayTargets() noexcept;
    void ApplyLanguage();
    void UpdateDpi(UINT dpi, const RECT& proposed) noexcept;
    void RecoverDisplay() noexcept;
    void Refilter();
    void SyncSelectionFromList();
    void SelectAllRows();
    void RebuildRows();
    void Tick();
    void UpdateStatus();
    std::wstring Cell(const DictRecord& record, int column) const;
    LRESULT Notify(NMHDR& header);
    std::vector<DictRecord> CollectSelectedRecords() const;
    void BeginEdit(int row, int column);
    void BeginNew();
    void CancelEditing(bool focus_list = true);
    void UpdateEditorState(CodeChange change = CodeChange::Automatic);
    void PositionEditor() noexcept;
    void UpdateCommandState() noexcept;
    bool CanExecute(ControlId id) const noexcept;
    [[nodiscard]] const DictRecord* FindOriginal() const noexcept;
    void AutoFillCodeFromWord();
    void ExecuteAddOrUpdate();
    void ExecutePinSelected();
    void ExecuteDeleteSelected();
    void ExecutePromoteSelected();
    void ExecuteClearFrequencies();
    void CopySelection();
    void ShowListContextMenu(POINT screen_pt);
    void SelectPrimaryDictDialog();
    void ExecuteRestoreDefaultPrimaryDict();
    void ImportUserDictDialog();
    void ExportSnapshotDialog();
    void BackupBundleDialog();
    void RestoreBundleDialog();
    void Command(int id, int notification);
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
};

} // namespace qiqi::dict_manager
