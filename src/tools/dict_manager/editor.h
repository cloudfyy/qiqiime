// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/dict_manager/model.h"
#include <variant>

namespace qiqi::dict_manager {

enum class EditorMode { Browsing, NewDraft, ExistingDraft };
enum class EditorCommand { NewWord, Save, Cancel, AutoCode, ChangeRecords };
enum class EditorSaveAction { Inactive, Unchanged, MissingOriginal, AddUser, ReplaceUser };
enum class CodeChange { Manual, Automatic };

struct EditorDraft {
    std::wstring word;
    std::wstring code;
    bool manual_code = false;
};

class DictEditor final {
public:
    EditorMode Mode() const noexcept;
    bool Active() const noexcept { return Mode() != EditorMode::Browsing; }
    bool Dirty() const noexcept;
    bool CanExecute(EditorCommand command) const noexcept;
    bool WantsAutomaticCode() const noexcept;
    const DictRecord* Original() const noexcept;
    const DictRecord* FindOriginal(std::span<const DictRecord> records) const noexcept;
    std::wstring_view Word() const noexcept;
    std::wstring_view Code() const noexcept;

    bool BeginNew() noexcept;
    void BeginExisting(const DictRecord& record);
    void Cancel() noexcept;
    void ChangeWord(std::wstring word) noexcept;
    void ChangeCode(std::wstring code, CodeChange change) noexcept;
    EditorSaveAction PrepareSave(std::span<const DictRecord> records) const noexcept;
    void CompleteSave(bool saved) noexcept;

private:
    struct ExistingDraft {
        DictRecord original;
        EditorDraft draft;
    };
    const EditorDraft* Draft() const noexcept;
    EditorDraft* Draft() noexcept;
    std::variant<std::monostate, EditorDraft, ExistingDraft> state_;
};

} // namespace qiqi::dict_manager
