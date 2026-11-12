// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestEditorState() {
    DictEditor editor;
    const auto check_commands = [&]() {
        QIQI_TEST_CHECK(editor.CanExecute(EditorCommand::NewWord) == !editor.Active());
        QIQI_TEST_CHECK(editor.CanExecute(EditorCommand::ChangeRecords) == !editor.Active());
        for (const auto command : {EditorCommand::Save, EditorCommand::Cancel, EditorCommand::AutoCode}) {
            QIQI_TEST_CHECK(editor.CanExecute(command) == editor.Active());
        }
    };
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::Browsing && !editor.Dirty());
    QIQI_TEST_CHECK(!editor.Original() && editor.Word().empty() && editor.Code().empty());
    QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::Inactive);
    QIQI_TEST_CHECK(editor.BeginNew());
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::NewDraft && !editor.Dirty());
    QIQI_TEST_CHECK(editor.WantsAutomaticCode() && editor.PrepareSave({}) == EditorSaveAction::AddUser);
    editor.ChangeWord(L"new");
    editor.ChangeCode(L"abcd", CodeChange::Automatic);
    QIQI_TEST_CHECK(editor.Dirty() && editor.WantsAutomaticCode());
    QIQI_TEST_CHECK(!editor.BeginNew() && editor.Word() == L"new");
    editor.ChangeCode(L"test", CodeChange::Manual);
    editor.ChangeWord(L"changed");
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode() && editor.Code() == L"test");
    editor.ChangeCode(L"wxyz", CodeChange::Automatic);
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode());
    editor.CompleteSave(false);
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::NewDraft && editor.Dirty());
    QIQI_TEST_CHECK(editor.Word() == L"changed" && editor.Code() == L"wxyz");
    editor.ChangeWord(L"");
    editor.ChangeCode(L"", CodeChange::Automatic);
    QIQI_TEST_CHECK(!editor.Dirty() && !editor.WantsAutomaticCode());
    editor.Cancel();
    check_commands();
    QIQI_TEST_CHECK(editor.BeginNew() && editor.WantsAutomaticCode());

    const DictRecord original{9, PackWubiCode(L"abcd"), L"abcd", L"original", DictEntrySource::UserDict, 1};
    editor.BeginExisting(original); // Selecting an existing row replaces the temporary new row.
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::ExistingDraft && !editor.Dirty());
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode() && editor.Original()->word == original.word);
    QIQI_TEST_CHECK(!editor.BeginNew() && editor.Original()->id == original.id);
    QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::Unchanged);
    std::vector<DictRecord> records{original};
    records.front().id = 42; // Refresh renumbers rows; identity is source/code/word, not the old row ID.
    QIQI_TEST_CHECK(editor.FindOriginal(records) == &records.front());
    editor.ChangeWord(L"updated");
    QIQI_TEST_CHECK(editor.Dirty() && editor.PrepareSave(records) == EditorSaveAction::ReplaceUser);
    editor.CompleteSave(false);
    QIQI_TEST_CHECK(editor.Word() == L"updated" && editor.Original()->word == L"original");
    for (const bool change_source : {false, true}) {
        auto removed = records;
        if (change_source) removed.front().source = DictEntrySource::Pinned;
        else removed.front().word = L"externally changed";
        QIQI_TEST_CHECK(!editor.FindOriginal(removed));
        QIQI_TEST_CHECK(editor.PrepareSave(removed) == EditorSaveAction::MissingOriginal);
        editor.CompleteSave(false);
        QIQI_TEST_CHECK(editor.Dirty() && editor.Word() == L"updated");
    }
    editor.ChangeWord(original.word);
    editor.ChangeCode(L"other", CodeChange::Manual);
    QIQI_TEST_CHECK(editor.Dirty());
    editor.ChangeCode(original.code, CodeChange::Manual);
    QIQI_TEST_CHECK(!editor.Dirty() && editor.PrepareSave(records) == EditorSaveAction::Unchanged);
    for (const auto source : {DictEntrySource::Pinned, DictEntrySource::Blocked, DictEntrySource::PhraseFreq,
             DictEntrySource::PrimaryDict, DictEntrySource::ExtraDict}) {
        auto record = original;
        record.source = source;
        editor.BeginExisting(record);
        editor.ChangeWord(L"user copy");
        const std::array current{record};
        QIQI_TEST_CHECK(editor.PrepareSave(current) == EditorSaveAction::AddUser);
        QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::MissingOriginal);
        QIQI_TEST_CHECK(editor.Original()->word == original.word);
    }
    editor.CompleteSave(true);
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::Browsing && !editor.Dirty() && !editor.Original());
    QIQI_TEST_CHECK(editor.Word().empty() && editor.Code().empty());
    editor.Cancel();
    QIQI_TEST_CHECK(editor.BeginNew() && editor.WantsAutomaticCode());
}

} // namespace qiqi::dict_manager::tests
