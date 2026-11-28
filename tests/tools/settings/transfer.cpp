// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/backup.h"
#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/support.h"
#include "common/zip.h"
#include "ime/engine/backup.h"
#include "ime/engine/engine.h"
#include "tools/settings/internal.h"
#include "tools/settings/transfer.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace qiqi::test {
namespace {

using namespace settings_detail;

thread_local bool backup_message_seen = false;
thread_local std::wstring backup_message;

template <typename Action>
std::wstring WithBackupMessage(Action action) {
    backup_message.clear();
    backup_message_seen = false;
    const HHOOK hook = ::SetWindowsHookExW(
        WH_CBT,
        [](int code, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
            if (code == HCBT_ACTIVATE) {
                const HWND box = reinterpret_cast<HWND>(wparam);
                std::array<wchar_t, 32> name{};
                ::GetClassNameW(
                    box, name.data(), gsl::narrow_cast<int>(name.size()));
                if (std::wstring_view(name.data()) == L"#32770") {
                    backup_message = ControlText(::GetDlgItem(box, 0xFFFF));
                    backup_message_seen = true;
                    ::SendMessageW(box, WM_COMMAND, IDOK, 0);
                    return 1;
                }
            }
            return ::CallNextHookEx(nullptr, code, wparam, lparam);
        },
        nullptr, ::GetCurrentThreadId());
    QIQI_TEST_CHECK(hook != nullptr);
    const auto unhook = gsl::finally([hook]() noexcept {
        QIQI_TEST_CHECK(::UnhookWindowsHookEx(hook));
    });
    action();
    QIQI_TEST_CHECK(backup_message_seen);
    return backup_message;
}

std::optional<BackupConflictMode> ChooseMerge(HWND, UiLanguage) noexcept {
    return BackupConflictMode::Merge;
}

std::optional<BackupConflictMode> ChooseReplace(HWND, UiLanguage) noexcept {
    return BackupConflictMode::Replace;
}

#pragma warning(suppress : 26461)
BOOL WINAPI CancelFileDialog(OPENFILENAMEW* dialog) {
    QIQI_TEST_CHECK(dialog && dialog->lpstrDefExt &&
        std::wstring_view(dialog->lpstrDefExt) == L"zip");
    QIQI_TEST_CHECK((dialog->Flags & OFN_NOCHANGEDIR) != 0);
    return FALSE;
}

DWORD WINAPI NoDialogError() noexcept { return ERROR_SUCCESS; }

void TestSettingsZipBackupFlow() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"qiqi-settings-zip-" + std::to_wstring(::GetCurrentProcessId()) +
         L"-" + std::to_wstring(::GetTickCount64()));
    const auto module = directory / L"module";
    const auto local_app_data = directory / L"appdata";
    QIQI_TEST_CHECK(std::filesystem::create_directories(module));
    QIQI_TEST_CHECK(std::filesystem::create_directories(local_app_data));

    wchar_t* original_app_data = nullptr;
    size_t original_size = 0;
    QIQI_TEST_CHECK(_wdupenv_s(
        &original_app_data, &original_size, L"LOCALAPPDATA") == 0);
    const std::unique_ptr<wchar_t, decltype(&std::free)> restore_app_data(
        original_app_data, &std::free);
    const std::wstring subkey =
        L"Software\\QiQiIME_SettingsZipBackup_" +
        std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    QIQI_TEST_CHECK(_wputenv_s(L"LOCALAPPDATA", local_app_data.c_str()) == 0);
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
        const LSTATUS error =
            ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK_CLEANUP(
            error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND);
        QIQI_TEST_CHECK_CLEANUP(_wputenv_s(
            L"LOCALAPPDATA",
            restore_app_data ? restore_app_data.get() : L"") == 0);
        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        QIQI_TEST_CHECK_CLEANUP(!cleanup_error);
    });

    const auto user = directory / L"user.txt";
    const auto phrase = GetDefaultPhraseFreqPath();
    const auto custom = directory / L"custom.txt";
    const auto backup = directory / L"backup.zip";
    const auto exported = directory / L"settings-export.zip";
    const auto invalid = directory / L"invalid.zip";
    const auto extra = directory / L"extra.zip";

    QiQiConfig archived;
    archived.primary_dict = PrimaryDict::Custom;
    archived.primary_dict_path = custom.wstring();
    archived.user_dict_path = user.wstring();
    archived.page_size = 7;
    archived.ui_theme = UiTheme::Dark;
    archived.enable_code_completion = false;
    archived.single_character_mode = true;
    archived.full_code_practice = true;
    archived.phrase_char_selection = true;
    archived.enable_z_pinyin_lookup = true;
    archived.commit_on_english_switch = false;
    QIQI_TEST_CHECK(WriteFileAtomically(
        custom, codec::AsByteSpan(std::string_view("abcd 自定义主词条\n"))));
    QIQI_TEST_CHECK(SaveConfig(archived));

    WubiEngine source;
    source.SetDynamicPhraseFreq(true);
    source.SetUserDictPath(user);
    source.SetPhraseFreqPath(phrase);
    QIQI_TEST_CHECK(SUCCEEDED(source.AddUserEntryResult(
        L"abcd", L"备份词条", user)));
    QIQI_TEST_CHECK(SUCCEEDED(source.PinUserEntryResult(
        L"efgh", L"备份置顶", user)));
    QIQI_TEST_CHECK(SUCCEEDED(source.BlockSystemEntryResult(
        L"ijkl", L"备份屏蔽", user)));
    QIQI_TEST_CHECK(SUCCEEDED(source.PromoteUserPhraseFrequencyResult(
        L"abcd", L"备份词条", phrase)));

    CompleteBackup complete(module);
    QIQI_TEST_CHECK(SUCCEEDED(complete.CreateFileResult(backup)));
    std::vector<ZipArchiveEntry> entries;
    QIQI_TEST_CHECK(SUCCEEDED(ReadZipArchiveFileResult(backup, entries)));
    QIQI_TEST_CHECK(entries.size() == 5);
    QIQI_TEST_CHECK(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.name == kBackupManifestEntry;
    }));
    QIQI_TEST_CHECK(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.name == kBackupConfigEntry;
    }));
    QIQI_TEST_CHECK(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.name == kBackupUserDictionaryEntry;
    }));
    QIQI_TEST_CHECK(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.name == kBackupPhraseOrderEntry;
    }));
    QIQI_TEST_CHECK(std::ranges::any_of(entries, [](const auto& entry) {
        return entry.name == kBackupCustomPrimaryTextEntry;
    }));

    QiQiConfig current = archived;
    current.primary_dict = PrimaryDict::BuiltinOnly;
    current.primary_dict_path.clear();
    current.user_dict_path = user.wstring();
    current.page_size = 5;
    current.ui_theme = UiTheme::Light;
    current.enable_code_completion = true;
    current.single_character_mode = false;
    current.full_code_practice = false;
    current.phrase_char_selection = false;
    current.enable_z_pinyin_lookup = false;
    QIQI_TEST_CHECK(SaveConfig(current));
    QIQI_TEST_CHECK(source.AddUserEntryResult(L"mnop", L"本机保留词", user) == S_OK);

    const ScopedWindow window(CreateSettingsWindow(
        ::GetModuleHandleW(nullptr), nullptr, &current, true));
    QIQI_TEST_CHECK(window);
    QIQI_TEST_CHECK(ControlText(
        ::GetDlgItem(window.Get(), kIdBtnImportConfig)) ==
        GetUiStrings(current.ui_language).dlg_btn_restore_backup);
    QIQI_TEST_CHECK(ControlText(
        ::GetDlgItem(window.Get(), kIdBtnExportConfig)) ==
        GetUiStrings(current.ui_language).dlg_btn_create_backup);

    const auto export_message = WithBackupMessage([&] {
        QIQI_TEST_CHECK(ExportSettingsBackup(window.Get(), exported) == S_OK);
    });
    QIQI_TEST_CHECK(export_message.contains(L"ZIP"));
    std::vector<ZipArchiveEntry> exported_entries;
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(exported, exported_entries)));
    QIQI_TEST_CHECK(exported_entries.size() == 4);
    QIQI_TEST_CHECK(std::ranges::any_of(
        exported_entries, [](const auto& entry) {
            return entry.name == kBackupUserDictionaryEntry;
        }));
    QIQI_TEST_CHECK(std::ranges::any_of(
        exported_entries, [](const auto& entry) {
            return entry.name == kBackupPhraseOrderEntry;
        }));

    QIQI_TEST_CHECK(::SendDlgItemMessageW(
        window.Get(), kIdComboPageSize, CB_SETCURSEL, 8, 0) == 8);
    const auto unchanged_sequence = QueryRegistryConfigSequence();
    const auto unchanged_message = WithBackupMessage([&] {
        QIQI_TEST_CHECK(RestoreSettingsBackup(
            window.Get(), exported, ChooseMerge) == S_OK);
    });
    QIQI_TEST_CHECK(!unchanged_message.empty());
    QiQiConfig unchanged_visible;
    ReadSettingsFromWindowControls(window.Get(), unchanged_visible);
    QIQI_TEST_CHECK(unchanged_visible.page_size == current.page_size);
    QIQI_TEST_CHECK(
        QueryRegistryConfigSequence() == unchanged_sequence);

    const uint64_t before_restore = QueryRegistryConfigSequence();
    const auto restore_message = WithBackupMessage([&] {
        QIQI_TEST_CHECK(RestoreSettingsBackup(
            window.Get(), backup, ChooseMerge) == S_OK);
    });
    QIQI_TEST_CHECK(restore_message.contains(
        ResolveUiLanguage(current.ui_language) == UiLanguage::ZhCn
            ? L"已恢复备份"
            : L"Backup restored"));
    QiQiConfig restored;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(restored));
    QIQI_TEST_CHECK(restored.primary_dict == PrimaryDict::Custom &&
        restored.page_size == 7 && restored.ui_theme == UiTheme::Dark &&
        !restored.enable_code_completion && restored.single_character_mode && restored.full_code_practice &&
        restored.phrase_char_selection && restored.enable_z_pinyin_lookup && !restored.commit_on_english_switch);
    QiQiConfig visible;
    ReadSettingsFromWindowControls(window.Get(), visible);
    QIQI_TEST_CHECK(visible.page_size == restored.page_size &&
        visible.ui_theme == restored.ui_theme && visible.single_character_mode && visible.full_code_practice &&
        visible.phrase_char_selection && visible.enable_z_pinyin_lookup);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() > before_restore);

    WubiEngine loaded;
    loaded.SetUserDictPath(user);
    QIQI_TEST_CHECK(SUCCEEDED(loaded.SynchronizeUserDictionary()));
    QIQI_TEST_CHECK(std::ranges::any_of(
        loaded.UserDictionaryEntries(), [](const auto& item) {
            return std::ranges::contains(item.second, L"备份词条");
        }));
    QIQI_TEST_CHECK(std::ranges::any_of(
        loaded.UserDictionaryEntries(), [](const auto& item) {
            return std::ranges::contains(item.second, L"本机保留词");
        }));
    QIQI_TEST_CHECK(std::ranges::any_of(
        loaded.PinnedEntries(), [](const auto& item) {
            return std::ranges::contains(item.second, L"备份置顶");
        }));
    QIQI_TEST_CHECK(std::ranges::any_of(
        loaded.BlockedEntries(), [](const auto& item) {
            return std::ranges::contains(item.second, L"备份屏蔽");
        }));
    loaded.SetPhraseFreqPath(phrase);
    QIQI_TEST_CHECK(loaded.LoadPhraseFrequencies(phrase));
    QIQI_TEST_CHECK(std::ranges::any_of(
        loaded.PromotedPhrases(), [](const auto& item) {
            return std::ranges::contains(item.second, L"备份词条");
        }));

    QIQI_TEST_CHECK(source.AddUserEntryResult(
        L"qrst", L"覆盖时删除词", user) == S_OK);
    HRESULT replace_result = E_UNEXPECTED;
    const auto replace_message = WithBackupMessage([&] {
        replace_result =
            RestoreSettingsBackup(window.Get(), backup, ChooseReplace);
    });
    if (FAILED(replace_result)) {
        std::cerr << std::format(
            "[SETTINGS-RESTORE-FAIL] replace_hr=0x{:08X}, message={}\n",
            gsl::narrow_cast<uint32_t>(replace_result),
            WideToUtf8(replace_message));
    }
    QIQI_TEST_CHECK(SUCCEEDED(replace_result));
    QIQI_TEST_CHECK(!replace_message.empty());
    QIQI_TEST_CHECK(SUCCEEDED(loaded.SynchronizeUserDictionary()));
    QIQI_TEST_CHECK(!std::ranges::any_of(
        loaded.UserDictionaryEntries(), [](const auto& item) {
            return std::ranges::contains(item.second, L"覆盖时删除词");
        }));

    QIQI_TEST_CHECK(WriteFileAtomically(
        invalid,
        codec::AsByteSpan(std::string_view(
            R"({"format":"qiqiime-config","version":1,"settings":{}})"))));
    const auto invalid_sequence = QueryRegistryConfigSequence();
    const auto invalid_message = WithBackupMessage([&] {
        QIQI_TEST_CHECK(FAILED(RestoreSettingsBackup(
            window.Get(), invalid, ChooseMerge)));
    });
    QIQI_TEST_CHECK(invalid_message.contains(L"0x"));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == invalid_sequence);

    entries.push_back({"unexpected.bin", std::vector<uint8_t>{0x01}});
    QIQI_TEST_CHECK(SUCCEEDED(
        WriteZipArchiveFileResult(extra, entries)));
    const auto extra_message = WithBackupMessage([&] {
        QIQI_TEST_CHECK(RestoreSettingsBackup(
            window.Get(), extra, ChooseMerge) ==
            HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    });
    QIQI_TEST_CHECK(extra_message.contains(L"0x"));

    const auto locked_output = directory / L"locked.zip";
    QIQI_TEST_CHECK(WriteFileAtomically(
        locked_output, codec::AsByteSpan(std::string_view("preserve"))));
    {
        const ScopedFileHandle lock(::CreateFileW(
            locked_output.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(lock);
        const auto write_message = WithBackupMessage([&] {
            QIQI_TEST_CHECK(ExportSettingsBackup(
                window.Get(), locked_output) ==
                HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
        });
        QIQI_TEST_CHECK(write_message.contains(L"0x"));
    }
    std::string preserved;
    QIQI_TEST_CHECK(ReadFileToUtf8String(locked_output, preserved) &&
        preserved == "preserve");

    const dialog_detail::FileDialogApi cancelled{
        CancelFileDialog, CancelFileDialog, NoDialogError};
    PromptSettingsBackupTransfer(window.Get(), true, cancelled);
    PromptSettingsBackupTransfer(window.Get(), false, cancelled);

    ::SendMessageW(window.Get(), WM_COMMAND, IDCANCEL, 0);
    QiQiConfig still_restored;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(still_restored) &&
        still_restored.page_size == 7 &&
        still_restored.primary_dict == PrimaryDict::Custom);
}

} // namespace

void RunSettingsTransferTests() {
    TestSettingsZipBackupFlow();
}

} // namespace qiqi::test
