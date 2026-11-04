#pragma once

#include "tools/dict_manager/window.h"
#include "tools/dict_manager/model.h"
#include "common/d2d_controls.h"
#include "common/i18n.h"
#include "common/logger.h"

#include <commctrl.h>

namespace qiqi::dict_manager {

struct DictManagerWindow::Impl {
    DictManagerModel model;
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    ScopedIcon small_icon;
    ScopedIcon big_icon;
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
    bool updating_rows = false;
    bool updating_filters = false;
    bool updating_editor = false;

    HWND Child(ControlId id) const noexcept { return ::GetDlgItem(hwnd, std::to_underlying(id)); }
    const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const noexcept {
        return ResolveUiLanguage(ui_language) == UiLanguage::EnUs ? en : zh;
    }

    const wchar_t* SourceText(DictEntrySource src) const noexcept {
        switch (src) {
        case DictEntrySource::UserDict:    return Text(L"用户词库", L"User Dict");
        case DictEntrySource::Pinned:      return Text(L"手动置顶 (^)", L"Pinned (^)");
        case DictEntrySource::Blocked:     return Text(L"已屏蔽词 (!)", L"Blocked (!)");
        case DictEntrySource::PhraseFreq:  return Text(L"动态词频 (QPF1)", L"Phrase Freq (QPF1)");
        case DictEntrySource::PrimaryDict: return Text(L"主词库", L"Primary Dict");
        case DictEntrySource::ExtraDict:   return Text(L"扩展词库", L"Extra Dict");
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
    bool CreateControls();
    void Layout() noexcept;
    void UpdateTheme() noexcept;
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
    void Notify(NMHDR& header);
    std::vector<DictRecord> CollectSelectedRecords() const;
    void SyncEditorFromSelection() noexcept;
    void AutoFillCodeFromWord();
    void ExecuteAddOrUpdate();
    void ExecutePinSelected();
    void ExecuteDeleteSelected();
    void ExecutePromoteSelected();
    void ExecuteClearFrequencies();
    void CopySelection();
    void ShowListContextMenu(POINT screen_pt);
    void ImportUserDictDialog();
    void ExportSnapshotDialog();
    void BackupBundleDialog();
    void RestoreBundleDialog();
    void Command(int id, int notification);
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
};

} // namespace qiqi::dict_manager
