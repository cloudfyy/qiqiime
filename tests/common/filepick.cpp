// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test::controltest {
namespace {

struct FileDialogTestState {
    bool selected = true;
    DWORD error = ERROR_SUCCESS;
    int opens = 0;
    int saves = 0;
    int errors = 0;
    OPENFILENAMEW observed{};
    bool initial_filename_correct = false;
};

FileDialogTestState file_dialog_test;

BOOL FakeFileDialog(OPENFILENAMEW* dialog, bool save) noexcept {
    if (save) ++file_dialog_test.saves;
    else ++file_dialog_test.opens;
    if (!dialog) {
        file_dialog_test.error = CDERR_INITIALIZATION;
        return FALSE;
    }
    file_dialog_test.observed = *dialog;
    file_dialog_test.initial_filename_correct = std::wcscmp(dialog->lpstrFile, L"before.txt") == 0;
    if (file_dialog_test.selected) wcscpy_s(dialog->lpstrFile, dialog->nMaxFile, L"chosen.txt");
    return file_dialog_test.selected ? TRUE : FALSE;
}

BOOL WINAPI FakeOpenFileDialog(OPENFILENAMEW* dialog) noexcept {
    return FakeFileDialog(dialog, false);
}

BOOL WINAPI FakeSaveFileDialog(OPENFILENAMEW* dialog) noexcept {
    return FakeFileDialog(dialog, true);
}

DWORD WINAPI FakeFileDialogError() noexcept {
    ++file_dialog_test.errors;
    return file_dialog_test.error;
}

} // namespace

void TestFileDialogs(HWND owner) {
    using namespace dialog_detail;
    std::array<wchar_t, 32> path{};
    constexpr wchar_t filter[] = L"Text\0*.txt\0";
    file_dialog_test = {};
    const FileDialogApi api{&FakeOpenFileDialog, &FakeSaveFileDialog, &FakeFileDialogError};
    for (const auto mode : {FileDialogMode::Open, FileDialogMode::Save}) {
        for (const auto expected : {FileDialogStatus::Selected, FileDialogStatus::Cancelled, FileDialogStatus::Failed}) {
            wcscpy_s(path.data(), path.size(), L"before.txt");
            file_dialog_test.selected = expected == FileDialogStatus::Selected;
            file_dialog_test.error = expected == FileDialogStatus::Failed ? FNERR_BUFFERTOOSMALL : ERROR_SUCCESS;
            const auto result = PromptForFile(owner, L"Pick", std::span(filter).data(), path, mode,
                mode == FileDialogMode::Save ? L"txt" : nullptr, api);
            const auto& dialog = file_dialog_test.observed;
            QIQI_TEST_CHECK(dialog.lStructSize == sizeof(dialog) && dialog.hwndOwner == owner);
            QIQI_TEST_CHECK(std::wstring_view(dialog.lpstrTitle) == L"Pick" && dialog.lpstrFilter == std::span(filter).data());
            QIQI_TEST_CHECK(dialog.lpstrFile == path.data() && dialog.nMaxFile == path.size());
            QIQI_TEST_CHECK(file_dialog_test.initial_filename_correct);
            const DWORD flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                (mode == FileDialogMode::Save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
            QIQI_TEST_CHECK(dialog.Flags == flags);
            if (mode == FileDialogMode::Save) QIQI_TEST_CHECK(std::wstring_view(dialog.lpstrDefExt) == L"txt");
            else QIQI_TEST_CHECK(dialog.lpstrDefExt == nullptr);
            QIQI_TEST_CHECK(result.status == expected && result.error == file_dialog_test.error);
            QIQI_TEST_CHECK(static_cast<bool>(result) == file_dialog_test.selected);
            QIQI_TEST_CHECK(std::wstring_view(path.data()) == (file_dialog_test.selected ? L"chosen.txt" : L"before.txt"));
        }
    }
    QIQI_TEST_CHECK(file_dialog_test.opens == 3 && file_dialog_test.saves == 3 && file_dialog_test.errors == 4);
    QIQI_TEST_CHECK(PromptForFile(owner, L"Pick", L"Text\0*.txt\0", {}, FileDialogMode::Open, nullptr, api).status
        == FileDialogStatus::Failed);
    path.fill(L'x');
    QIQI_TEST_CHECK(PromptForFile(owner, L"Pick", L"Text\0*.txt\0", path, FileDialogMode::Open, nullptr, api).status
        == FileDialogStatus::Failed);
    QIQI_TEST_CHECK(file_dialog_test.opens == 3 && file_dialog_test.errors == 4);
}
} // namespace qiqi::test::controltest
