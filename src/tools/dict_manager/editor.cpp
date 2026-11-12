// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dict_manager/editor.h"

#include <algorithm>
#include <gsl/gsl>

namespace qiqi::dict_manager {

EditorMode DictEditor::Mode() const noexcept {
    if (std::holds_alternative<EditorDraft>(state_)) return EditorMode::NewDraft;
    if (std::holds_alternative<ExistingDraft>(state_)) return EditorMode::ExistingDraft;
    return EditorMode::Browsing;
}

const EditorDraft* DictEditor::Draft() const noexcept {
    if (const auto* draft = std::get_if<EditorDraft>(&state_)) return draft;
    if (const auto* existing = std::get_if<ExistingDraft>(&state_)) return &existing->draft;
    return nullptr;
}

EditorDraft* DictEditor::Draft() noexcept {
    if (auto* draft = std::get_if<EditorDraft>(&state_)) return draft;
    if (auto* existing = std::get_if<ExistingDraft>(&state_)) return &existing->draft;
    return nullptr;
}

std::wstring_view DictEditor::Word() const noexcept {
    const auto* draft = Draft();
    return draft ? std::wstring_view(draft->word) : std::wstring_view{};
}

std::wstring_view DictEditor::Code() const noexcept {
    const auto* draft = Draft();
    return draft ? std::wstring_view(draft->code) : std::wstring_view{};
}

const DictRecord* DictEditor::Original() const noexcept {
    const auto* existing = std::get_if<ExistingDraft>(&state_);
    return existing ? &existing->original : nullptr;
}

bool DictEditor::Dirty() const noexcept {
    if (!Active()) return false;
    const auto* original = Original();
    return original ? Word() != original->word || Code() != original->code
                    : !Word().empty() || !Code().empty();
}

bool DictEditor::CanExecute(EditorCommand command) const noexcept {
    switch (command) {
        case EditorCommand::NewWord:
        case EditorCommand::ChangeRecords: return !Active();
        case EditorCommand::Save:
        case EditorCommand::Cancel:
        case EditorCommand::AutoCode: return Active();
        default: return false;
    }
}

bool DictEditor::WantsAutomaticCode() const noexcept {
    const auto* draft = std::get_if<EditorDraft>(&state_);
    return draft && !draft->manual_code;
}

const DictRecord* DictEditor::FindOriginal(std::span<const DictRecord> records) const noexcept {
    const auto* original = Original();
    if (!original) return nullptr;
    const auto found = std::ranges::find_if(records, [original](const DictRecord& record) {
        return record.source == original->source && record.code == original->code && record.word == original->word;
    });
    return found == records.end() ? nullptr : &*found;
}

bool DictEditor::BeginNew() noexcept {
    if (!CanExecute(EditorCommand::NewWord)) return false;
    state_.emplace<EditorDraft>();
    return true;
}

void DictEditor::BeginExisting(const DictRecord& record) {
    ExistingDraft next{record, {record.word, record.code, false}};
    state_ = std::move(next);
}

void DictEditor::Cancel() noexcept {
    state_.emplace<std::monostate>();
}

void DictEditor::ChangeWord(std::wstring word) noexcept {
    auto* draft = Draft();
    Expects(draft != nullptr);
    draft->word = std::move(word);
}

void DictEditor::ChangeCode(std::wstring code, CodeChange change) noexcept {
    auto* draft = Draft();
    Expects(draft != nullptr);
    draft->code = std::move(code);
    if (change == CodeChange::Manual) draft->manual_code = true;
}

EditorSaveAction DictEditor::PrepareSave(std::span<const DictRecord> records) const noexcept {
    if (!CanExecute(EditorCommand::Save)) return EditorSaveAction::Inactive;
    const auto* original = Original();
    if (original && !Dirty()) return EditorSaveAction::Unchanged;
    if (original && !FindOriginal(records)) return EditorSaveAction::MissingOriginal;
    return original && original->source == DictEntrySource::UserDict
        ? EditorSaveAction::ReplaceUser : EditorSaveAction::AddUser;
}

void DictEditor::CompleteSave(bool saved) noexcept {
    if (saved) Cancel();
}

} // namespace qiqi::dict_manager
