#include "common/test_support.h"
#include "common/config.h"
#include "common/d2d_controls.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/win32_ui_util.h"
#include "ime/engine/engine.h"
#include "tools/dict_manager/model.h"
#include "tools/dict_manager/window.h"

#include <commctrl.h>
#include <winver.h>
#include <filesystem>
#include <fstream>
#include <span>

namespace {
using namespace qiqi;
using namespace qiqi::dict_manager;
using enum qiqi::dict_manager::ControlId;

HWND Child(const DictManagerWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

void Click(const DictManagerWindow& window, ControlId id) noexcept {
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(id), BN_CLICKED), 0);
}

std::wstring ControlText(HWND hwnd) {
    std::wstring text(gsl::narrow_cast<size_t>(::GetWindowTextLengthW(hwnd)) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, text.data(), gsl::narrow_cast<int>(text.size()));
    text.resize(gsl::narrow_cast<size_t>(copied));
    return text;
}

void DrainQuitMessages() noexcept {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

void TestVersionResource() {
    auto path = GetModuleFilePath(nullptr);
    QIQI_TEST_CHECK(!path.empty());
    path.replace_filename(L"QiQiIMEDictManager.exe");

    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), nullptr);
    QIQI_TEST_CHECK(size > 0);
    std::vector<std::byte> data(size);
    QIQI_TEST_CHECK(::GetFileVersionInfoW(path.c_str(), 0, size, data.data()));

    void* value = nullptr;
    UINT length = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(data.data(), L"\\", &value, &length));
    QIQI_TEST_CHECK(value != nullptr && length >= sizeof(VS_FIXEDFILEINFO));
    const auto* info = static_cast<const VS_FIXEDFILEINFO*>(value);
    QIQI_TEST_CHECK(info->dwSignature == VS_FFI_SIGNATURE);
    QIQI_TEST_CHECK(info->dwFileType == VFT_APP);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionMS) == version::kMajor);
    QIQI_TEST_CHECK(LOWORD(info->dwFileVersionMS) == version::kMinor);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionLS) == version::kPatch);

    void* text = nullptr;
    UINT count = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(data.data(), L"\\StringFileInfo\\080404b0\\OriginalFilename", &text, &count));
    QIQI_TEST_CHECK(text != nullptr && std::wstring_view(static_cast<const wchar_t*>(text)) == L"QiQiIMEDictManager.exe");

    const ScopedModule resource(::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    QIQI_TEST_CHECK(resource.IsValid());
    const HRSRC group = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    QIQI_TEST_CHECK(group != nullptr);
    const HGLOBAL loaded = ::LoadResource(resource.Get(), group);
    QIQI_TEST_CHECK(loaded != nullptr);
    const std::span<const BYTE> bytes(
        static_cast<const BYTE*>(::LockResource(loaded)), ::SizeofResource(resource.Get(), group));
    QIQI_TEST_CHECK(bytes.size() >= 6);
    WORD icon_count = 0;
    std::memcpy(&icon_count, bytes.subspan(4, 2).data(), 2);
    QIQI_TEST_CHECK(icon_count == 10 && bytes.size() >= 6 + size_t{icon_count} * 14);
    bool has_256_frame = false;
    for (size_t i = 0; i < icon_count; ++i) {
        const auto entry = bytes.subspan(6 + i * 14, 14);
        if (entry.front() == 0 && gsl::at(entry, 1) == 0) has_256_frame = true;
    }
    QIQI_TEST_CHECK(has_256_frame);
}

void TestModelAndCrossProcessSync() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_mgr_test_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.txt";
    const auto import_file = temp_dir / L"import.txt";
    const auto export_file = temp_dir / L"export.txt";

    DictManagerModel model;
    const auto module_dir = GetModuleFilePath(nullptr).parent_path();
    model.InitializePaths(module_dir, user_dict, freq_file);
    QIQI_TEST_CHECK(model.UserEntryCount() == 0);
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);

    // Auto-compute Wubi 86 phrase and single-char codes
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中华人民共和国") == L"kwwl");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中国") == L"khlg");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"工") == L"aaaa");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"   ").empty());

    // Add, modify, and remove user entries while an active WubiEngine instance syncs incrementally
    WubiEngine peer_engine;
    peer_engine.SetUserDictPath(user_dict);
    peer_engine.SetPhraseFreqPath(freq_file);
    peer_engine.SetDynamicPhraseFreq(true);

    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词一"));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 2);

    QIQI_TEST_CHECK(model.ModifyUserEntry(L"abcd", L"管理词一", L"管理词改"));
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    const auto after_mod = peer_engine.LookupExact(L"abcd");
    QIQI_TEST_CHECK(after_mod.size() == 2 && gsl::at(after_mod, 0) == L"管理词改");

    QIQI_TEST_CHECK(model.RemoveUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 1);

    // Phrase frequency promotion, removal, and clearing synced to peer WubiEngine
    QIQI_TEST_CHECK(!model.PromotePhraseFrequency(L"abcd", L"单"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"管理词改"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).front() == L"高频词组");

    QIQI_TEST_CHECK(model.RemovePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).size() == 1);

    QIQI_TEST_CHECK(model.ClearAllPhraseFrequencies());
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().empty());

    // PollExternalChanges when peer_engine adds an entry and promotes a phrase
    QIQI_TEST_CHECK(!model.PollExternalChanges());
    QIQI_TEST_CHECK(peer_engine.AddUserEntry(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(peer_engine.PromoteUserPhraseFrequency(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(model.PollExternalChanges());
    QIQI_TEST_CHECK(model.UserEntryCount() == 2 && model.PhraseFreqCount() == 1);

    // Import and export
    {
        std::ofstream ofs(import_file, std::ios::binary | std::ios::trunc);
        ofs << "qwer 导入词一 导入词二\n";
    }
    size_t imported = 0;
    QIQI_TEST_CHECK(model.ImportUserEntriesFromFile(import_file, imported) && imported == 2);
    QIQI_TEST_CHECK(model.ExportRecordsToFile(export_file, model.Records()));
    std::string exported_utf8;
    QIQI_TEST_CHECK(ReadFileToUtf8String(export_file, exported_utf8) && !exported_utf8.empty());

    // Filter matching and clipboard formatting
    DictFilter filter{};
    filter.source = DictEntrySource::UserDict;
    filter.code_query = L"QW";
    filter.word_query = L"导入";
    size_t matched = 0;
    for (const auto& rec : model.Records()) {
        if (filter.Matches(rec)) ++matched;
    }
    QIQI_TEST_CHECK(matched == 2);
    QIQI_TEST_CHECK(!FormatRecordsForClipboard(model.Records()).empty());

    // Backup & Restore (.qbak bundle with FNV-1a checksum validation, Merge vs Replace conflict modes)
    const std::wstring test_reg_subkey =
        L"Software\\QiQiIME_DictMgrBackupTest_" + std::to_wstring(::GetCurrentProcessId());
    SetConfigRegistrySubKeyOverrideForTest(test_reg_subkey.c_str());
    const auto restore_reg = gsl::finally([&test_reg_subkey]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, test_reg_subkey.c_str());
    });

    QiQiConfig custom_cfg = LoadConfig();
    custom_cfg.page_size = 7;
    custom_cfg.dynamic_phrase_freq = true;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));

    QIQI_TEST_CHECK(model.PinUserEntry(L"abcd", L"备份置顶词"));
    QIQI_TEST_CHECK(model.BlockSystemEntry(L"aaaa", L"工"));
    const auto backup_file = temp_dir / L"test_backup.qbak";
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup_file));

    // Corrupting the backup file payload must be rejected by checksum validation
    std::string raw_backup;
    QIQI_TEST_CHECK(ReadFileToUtf8String(backup_file, raw_backup));
    const auto corrupt_file = temp_dir / L"corrupt.qbak";
    {
        std::string corrupted = raw_backup + "\nabcd 被篡改词\n";
        QIQI_TEST_CHECK(WriteFileAtomically(corrupt_file, codec::AsByteSpan(corrupted)));
    }
    BackupRestoreSummary summary{};
    QIQI_TEST_CHECK(!model.RestoreBackupBundle(corrupt_file, BackupConflictMode::Replace, summary));

    // Mutate current state and test Replace mode (overwrites config, user_dict, and phrase_freq)
    custom_cfg.page_size = 3;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"临时未备份词"));
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Replace, summary));
    QIQI_TEST_CHECK(summary.config_restored && summary.user_entries_applied >= 5 && summary.phrase_freq_applied == 1);
    QIQI_TEST_CHECK(LoadConfig().page_size == 7);
    QIQI_TEST_CHECK(model.PinnedEntryCount() == 1 && model.BlockedEntryCount() == 1);

    // Test Merge mode (preserves newly added entry while re-applying backup entries)
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"mnop", L"合并保留词"));
    const size_t before_merge_count = model.UserEntryCount();
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Merge, summary));
    QIQI_TEST_CHECK(model.UserEntryCount() == before_merge_count);
}

void TestGuiWindow() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_mgr_gui_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        DrainQuitMessages();
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.txt";

    DictManagerWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
    QIQI_TEST_CHECK(::IsWindow(window.Handle()));

    // Type a phrase into EditWordInput -> verify AutoCode fills EditCodeInput with "kwwl"
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(EditWordInput), EN_CHANGE), 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");

    // Click Add / Save User Word -> verify list gets 1 row
    Click(window, AddUpdateBtn);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) >= 1);

    // Select first row, promote phrase frequency, and filter by PhraseFreq source (combo index 4)
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, PromoteBtn);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);

    // Pin entry via PinBtn and verify Pinned source filter (combo index 2)
    Click(window, PinBtn);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, PinBtn); // Unpin when already pinned
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    // Switch language between EnUs and ZhCn
    ::SendMessageW(Child(window, Language), CB_SETCURSEL, std::to_underlying(UiLanguage::EnUs), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Language), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ControlText(Child(window, Reload)) == L"Reload");
    ::SendMessageW(Child(window, Language), CB_SETCURSEL, std::to_underlying(UiLanguage::ZhCn), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Language), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ControlText(Child(window, Reload)) == L"刷新");

    // Clear frequencies, switch back to UserDict (combo index 1), delete entry
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    Click(window, ClearFreq);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, DeleteBtn);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    ::SendMessageW(window.Handle(), WM_CLOSE, 0, 0);
    DrainQuitMessages();
}

} // namespace

int main() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    TestVersionResource();
    TestModelAndCrossProcessSync();
    TestGuiWindow();
    std::cout << "[PASS] QiQiDictManagerTest\n";
    return 0;
}
