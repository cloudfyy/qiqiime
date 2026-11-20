// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"
#include "common/etw.h"
#include "common/cleanup.h"
#include "common/cfgjson.h"
#include "common/zip.h"
#include "tools/dictmgr/backup.h"
#include <exception>
#include <optional>
#include <stdexcept>

#undef QIQI_TEST_CHECK
#define QIQI_TEST_CHECK(expr) do { \
    if (!(expr)) { \
        ::qiqi::test::ReportTestCleanupFailure(__FILE__, __LINE__, #expr); \
        throw std::runtime_error(std::format("{}:{}: {}", __FILE__, __LINE__, #expr)); \
    } \
} while (false)

namespace qiqi::dict_manager::tests {

namespace {
void PrintRestoreFixtureTrace(std::span<const qiqi::test::LogEvent> events) {
    for (const auto& event : events) {
        std::cerr << "[RESTORE-TRACE] " << event.message << '\n';
    }
    std::cerr << std::flush;
}
} // namespace

std::string BackupManifest(bool has_custom = false) {
    std::string manifest =
        "{\"format\":\"qiqiime-backup\",\"version\":1,\"files\":{"
        "\"config\":\"config.json\","
        "\"user_dictionary\":\"user-dictionary.txt\","
        "\"phrase_order\":\"phrase-order.txt\"";
    if (has_custom) {
        manifest +=
            ",\"custom_primary_dictionary\":\"custom-primary.txt\"";
    }
    manifest += "}}\n";
    return manifest;
}

std::string ConfigJson(std::string_view settings) {
    return "{\"format\":\"qiqiime-config\",\"version\":1,\"settings\":{" +
        std::string(settings) + "}}\n";
}

void WriteRestoreBundle(
    const std::filesystem::path& path,
    std::string_view config,
    std::string_view user_dictionary = {},
    std::string_view phrase_order = {},
    std::optional<std::string_view> custom_primary = std::nullopt,
    std::string_view manifest = {}) {
    const auto bytes = [](std::string_view value) {
        return std::vector<uint8_t>(value.begin(), value.end());
    };
    const std::string manifest_text = manifest.empty()
        ? BackupManifest(custom_primary.has_value())
        : std::string(manifest);
    std::vector<ZipArchiveEntry> entries{
        {std::string(detail::kBackupManifestEntry),
         bytes(manifest_text)},
        {std::string(detail::kBackupConfigEntry), bytes(config)},
        {std::string(detail::kBackupUserDictionaryEntry), bytes(user_dictionary)},
        {std::string(detail::kBackupPhraseOrderEntry), bytes(phrase_order)}};
    if (custom_primary) {
        entries.push_back({
            std::string(detail::kBackupCustomPrimaryTextEntry),
            bytes(*custom_primary)});
    }
    QIQI_TEST_CHECK(SUCCEEDED(WriteZipArchiveFileResult(path, entries)));
}

namespace {
void RunBackupRestoreResults() {
    const auto directory = std::filesystem::current_path() /
        (L"qiqi_restore_results_" + std::to_wstring(::GetCurrentProcessId()));
    const std::wstring subkey = L"Software\\QiQiIME_RestoreResults_" + std::to_wstring(::GetCurrentProcessId());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        if (std::uncaught_exceptions() != 0) {
            std::fprintf(stderr, "[FIXTURE-RETAINED] pid=%lu, directory=\"%ls\"\n",
                ::GetCurrentProcessId(), directory.c_str());
            std::fflush(stderr);
            return;
        }
        const auto ec = qiqi::test::RemoveDirectoryWithSharingRetry(directory, 2000, ::Sleep);
        if (ec) {
            std::fprintf(stderr, "[FIXTURE-CLEANUP-FAIL] error=%d, directory=\"%ls\"\n",
                ec.value(), directory.c_str());
            std::fflush(stderr);
            QIQI_TEST_CHECK_CLEANUP(!ec);
        }
    });
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto user = directory / L"user.txt";
    const auto frequency = directory / L"frequency.txt";
    const auto backup = directory / L"restore.zip";
    DictManagerModel model;
    size_t reset_count = 0;
    const auto reset = [&]() {
        ++reset_count;
        const auto ec = qiqi::test::RemoveDirectoryWithSharingRetry(directory, 2000, ::Sleep);
        if (ec) {
            std::cerr << std::format("[FIXTURE-FAIL] stage=reset-directory, reset={}, error={}, directory=\"{}\"\n",
                reset_count, ec.value(), WideToUtf8(directory.native())) << std::flush;
        }
        QIQI_TEST_CHECK(!ec);
        QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
        QiQiConfig config;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.page_size = 3;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths({}, user, frequency);
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"原有词条"));
        const HRESULT frequency_hr = model.PromotePhraseFrequencyResult(L"zzzz", L"原有词组");
        if (FAILED(frequency_hr)) {
            std::cerr << std::format(
                "[FIXTURE-FAIL] stage=initial-phrase-promotion, reset={}, pid={}, hr=0x{:08X}, directory=\"{}\"\n",
                reset_count, ::GetCurrentProcessId(), gsl::narrow_cast<uint32_t>(frequency_hr),
                WideToUtf8(directory.native())) << std::flush;
        }
        QIQI_TEST_CHECK(frequency_hr == S_OK);
    };
    const auto has_word = [&](DictEntrySource source, std::wstring_view word) {
        return std::ranges::any_of(model.Records(), [=](const DictRecord& record) {
            return record.source == source && record.word == word;
        });
    };
    const auto lock_file = [](const std::filesystem::path& path) {
        return ScopedFileHandle(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    };
    const auto read_file = [](const std::filesystem::path& path) {
        std::string bytes;
        QIQI_TEST_CHECK(ReadFileToUtf8String(path, bytes));
        return bytes;
    };
    const auto sharing_failure = [](HRESULT error) {
        return error == E_ACCESSDENIED || error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
    };
    const std::string full_config = ConfigJson(R"("page_size":7)");
    const std::string full_user = "abcd 新增词条\n";
    const std::string full_phrase = "abcd 新增词组\n";

    reset();
    for (const bool maximum : {false, true}) {
        QiQiConfig expected = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Assign(expected, maximum ? field.maximum : field.minimum));
        });
        expected.primary_dict = PrimaryDict::Jidian;
        expected.user_dict_path = L"not-in-backup.txt";
        QIQI_TEST_CHECK(SaveConfig(expected));
        QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
        std::vector<ZipArchiveEntry> entries;
        QIQI_TEST_CHECK(SUCCEEDED(ReadZipArchiveFileResult(backup, entries)));
        QIQI_TEST_CHECK(entries.size() == 4);
        const auto config_entry = std::ranges::find_if(
            entries, [](const ZipArchiveEntry& entry) {
                return entry.name == detail::kBackupConfigEntry;
            });
        QIQI_TEST_CHECK(config_entry != entries.end());
        const std::string config_bytes(
            config_entry->data.begin(), config_entry->data.end());
        QIQI_TEST_CHECK(!config_bytes.contains("user_dict_path") &&
            !config_bytes.contains("not-in-backup.txt") &&
            !config_bytes.contains("log_level"));
        ConfigJsonError config_error;
        QiQiConfig archived{};
        QIQI_TEST_CHECK(SUCCEEDED(ParseConfigJson(
            config_bytes, QiQiConfig{}, archived, config_error)));
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(archived) == field.Value(expected));
        });
        QIQI_TEST_CHECK(SaveConfig(QiQiConfig{}));
        QIQI_TEST_CHECK(model.RestoreBackupBundle(backup, BackupConflictMode::Merge).status == BackupRestoreStatus::Success);
        const auto restored = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(restored) == field.Value(expected));
        });
        QIQI_TEST_CHECK(restored.user_dict_path.empty());
    }
    const QiQiConfig before_invalid = LoadConfig();
    const uint64_t sequence_before_invalid = QueryRegistryConfigSequence();
    for (const auto* invalid_config : {
             R"({"format":"qiqiime-config","version":1,"settings":{"page_size":0}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"future_numeric_key":123}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"log_level":6}})",
             R"({"format":"qiqiime-config","version":1,"settings":{"page_size":7,"page_size":8}})"}) {
        WriteRestoreBundle(backup, invalid_config);
        const auto result =
            model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed &&
            result.stage == BackupRestoreStage::Validate &&
            QueryRegistryConfigSequence() == sequence_before_invalid);
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(LoadConfig()) ==
                field.Value(before_invalid));
        });
    }

    reset();
    QIQI_TEST_CHECK(model.RemoveUserEntry(L"zzzz", L"原有词条"));
    QIQI_TEST_CHECK(model.ClearAllPhraseFrequencies());
    const auto custom_source = directory / L"custom-source.txt";
    QIQI_TEST_CHECK(WriteFileAtomically(
        custom_source,
        codec::AsByteSpan(std::string_view("abcd 自定义主词库\n"))));
    QIQI_TEST_CHECK(model.SelectCustomPrimaryDictionary(custom_source));
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    std::vector<ZipArchiveEntry> custom_archive;
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(backup, custom_archive)));
    QIQI_TEST_CHECK(custom_archive.size() == 5);
    const auto archived_custom = std::ranges::find_if(
        custom_archive, [](const ZipArchiveEntry& entry) {
                return entry.name == detail::kBackupCustomPrimaryTextEntry;
        });
    QIQI_TEST_CHECK(archived_custom != custom_archive.end());
    for (const auto& entry : custom_archive) {
        const std::string_view bytes(
                reinterpret_cast<const char*>(entry.data.data()),
                entry.data.size());
        QIQI_TEST_CHECK(!bytes.contains(WideToUtf8(custom_source.wstring())));
    }

    QiQiConfig builtin_config = LoadConfig();
    builtin_config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    QIQI_TEST_CHECK(std::filesystem::remove(custom_source));
    const auto custom_result =
        model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
    QIQI_TEST_CHECK(custom_result.status == BackupRestoreStatus::Success &&
        custom_result.stage == BackupRestoreStage::Complete);
    QIQI_TEST_CHECK(custom_result.summary.custom_primary_dict_restored);
    QIQI_TEST_CHECK(model.ActivePrimaryDict() == PrimaryDict::Custom);
    const auto restored_custom_path =
        directory / L"qiqiime-restored-primary.txt";
    QIQI_TEST_CHECK(model.ActivePrimaryDictPath() ==
                        restored_custom_path);
    QIQI_TEST_CHECK(std::filesystem::exists(restored_custom_path));
    QIQI_TEST_CHECK(std::ranges::any_of(
        model.Records(), [](const DictRecord& record) {
                return record.source == DictEntrySource::PrimaryDict &&
                    record.word == L"自定义主词库";
        }));
    QIQI_TEST_CHECK(LoadConfig().primary_dict_path ==
                        restored_custom_path.wstring());

    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    {
        QIQI_TEST_CHECK(WriteFileAtomically(
                restored_custom_path,
                codec::AsByteSpan(std::string_view("abcd 旧自定义词库\n"))));
        const auto locked = lock_file(restored_custom_path);
        QIQI_TEST_CHECK(locked);
        const auto denied =
                model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(
                denied.status == BackupRestoreStatus::Failed &&
                denied.stage == BackupRestoreStage::CustomDictionary &&
                sharing_failure(denied.error));
        QIQI_TEST_CHECK(!denied.summary.custom_primary_dict_restored &&
                !denied.effects_possible);
        QIQI_TEST_CHECK(
                LoadConfig().primary_dict == PrimaryDict::BuiltinOnly);
    }

    QIQI_TEST_CHECK(WriteFileAtomically(
        custom_source,
        codec::AsByteSpan(std::string_view("abcd 二进制主词库\n"))));
    const auto custom_binary = directory / L"custom-source.qdict";
    QIQI_TEST_CHECK(WubiMappedDict::CompileTextFileToQdict(
        custom_source, custom_binary));
    QIQI_TEST_CHECK(model.SelectCustomPrimaryDictionary(custom_binary));
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    custom_archive.clear();
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(backup, custom_archive)));
    QIQI_TEST_CHECK(std::ranges::any_of(
        custom_archive, [](const ZipArchiveEntry& entry) {
                return entry.name ==
                    detail::kBackupCustomPrimaryBinaryEntry;
        }));
    QIQI_TEST_CHECK(SaveConfig(builtin_config));
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);
    QIQI_TEST_CHECK(std::filesystem::remove(custom_binary));
    const auto binary_result =
        model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
    QIQI_TEST_CHECK(binary_result.status == BackupRestoreStatus::Success &&
        binary_result.summary.custom_primary_dict_restored);
    QIQI_TEST_CHECK(model.ActivePrimaryDict() == PrimaryDict::Custom);
    const auto restored_custom_binary =
        directory / L"qiqiime-restored-primary.qdict";
    QIQI_TEST_CHECK(model.ActivePrimaryDictPath() ==
                        restored_custom_binary);
    QIQI_TEST_CHECK(std::ranges::any_of(
        model.Records(), [](const DictRecord& record) {
                return record.source == DictEntrySource::PrimaryDict &&
                    record.word == L"二进制主词库";
        }));

    reset();
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
    {
        const auto locked_backup = lock_file(backup);
        QIQI_TEST_CHECK(locked_backup);
        const HRESULT create_result =
                model.CreateBackupBundleResult(backup);
        QIQI_TEST_CHECK(
                create_result == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
                create_result == E_ACCESSDENIED);
    }

    // Malformed later input must not apply earlier configuration or dictionary entries.
    reset();
    const auto old_user = read_file(user);
    const auto old_frequency = read_file(frequency);
    const auto old_sequence = QueryRegistryConfigSequence();
    const auto missing = model.RestoreBackupBundle(directory / L"missing.zip", BackupConflictMode::Replace);
    QIQI_TEST_CHECK(missing.status == BackupRestoreStatus::Failed && missing.stage == BackupRestoreStage::Read);
    const std::array malformed{
        std::tuple{full_config, full_user + "not_a_code 损坏词组\n", full_phrase, std::string{}},
        std::tuple{ConfigJson(R"("page_size":0)"), full_user, full_phrase, std::string{}},
        std::tuple{full_config, full_user, full_phrase,
                   std::string(R"({"format":"qiqiime-backup","version":1,"files":{"config":"config.json","user_dictionary":"user-dictionary.txt","phrase_order":"phrase-order.txt","unexpected":"x"}})")},
        std::tuple{full_config, std::string("abcd ^\n"), full_phrase, std::string{}},
        std::tuple{full_config, full_user, std::string("abcd 单\n"), std::string{}},
        std::tuple{full_config, std::string("abcd ") + "\xFF\n", full_phrase, std::string{}}};
    for (const auto& [config, user_data, phrase_data, manifest] : malformed) {
        WriteRestoreBundle(
            backup, config, user_data, phrase_data,
            std::nullopt, manifest);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && FAILED(result.error));
        QIQI_TEST_CHECK(result.refresh_error == S_FALSE && !result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3 && QueryRegistryConfigSequence() == old_sequence);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条"));
    }

    QIQI_TEST_CHECK(WriteFileAtomically(
        backup,
        codec::AsByteSpan(std::string_view(
            "# QIQIIME_BACKUP_V1\n# checksum_fnv1a64=0000000000000000\n"))));
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    std::string valid_archive;
    QIQI_TEST_CHECK(ReadFileToUtf8String(backup, valid_archive));
    const size_t user_payload = valid_archive.find(full_user);
    QIQI_TEST_CHECK(user_payload != std::string::npos);
    const std::string good_archive = valid_archive;
    gsl::at(valid_archive, user_payload) = 'z';
    QIQI_TEST_CHECK(WriteFileAtomically(
        backup, codec::AsByteSpan(valid_archive)));
    const auto bad_crc =
        model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
    QIQI_TEST_CHECK(bad_crc.status == BackupRestoreStatus::Failed &&
        bad_crc.stage == BackupRestoreStage::Validate &&
        bad_crc.error == HRESULT_FROM_WIN32(ERROR_CRC));
    QIQI_TEST_CHECK(read_file(user) == old_user &&
                    read_file(frequency) == old_frequency);

    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    std::vector<ZipArchiveEntry> unexpected_entries;
    QIQI_TEST_CHECK(SUCCEEDED(
        ReadZipArchiveFileResult(backup, unexpected_entries)));
    unexpected_entries.push_back({
        "unexpected.txt",
        std::vector<uint8_t>{'x'}});
    QIQI_TEST_CHECK(SUCCEEDED(
        WriteZipArchiveFileResult(backup, unexpected_entries)));
    const auto unexpected =
        model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
    QIQI_TEST_CHECK(unexpected.status == BackupRestoreStatus::Failed &&
        unexpected.stage == BackupRestoreStage::Validate);

    std::string unsafe_archive = good_archive;
    size_t unsafe_name = unsafe_archive.find("config.json");
    QIQI_TEST_CHECK(unsafe_name != std::string::npos);
    while (unsafe_name != std::string::npos) {
        unsafe_archive.replace(unsafe_name, 11, "unsafe/xx!!");
        unsafe_name = unsafe_archive.find("config.json", unsafe_name + 11);
    }
    QIQI_TEST_CHECK(WriteFileAtomically(
        backup, codec::AsByteSpan(unsafe_archive)));
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    std::string gapped_archive = good_archive;
    const auto eocd = gapped_archive.rfind(
        std::string("\x50\x4b\x05\x06", 4));
    QIQI_TEST_CHECK(eocd != std::string::npos);
    auto read_u32 = [&gapped_archive](size_t offset) {
        uint32_t value = 0;
        for (size_t index = 0; index < 4; ++index) {
            value |= gsl::narrow_cast<uint32_t>(
                gsl::narrow_cast<uint8_t>(
                    gsl::at(gapped_archive, offset + index)))
                << (index * 8);
        }
        return value;
    };
    const uint32_t directory_offset = read_u32(eocd + 16);
    gapped_archive.insert(
        gsl::narrow_cast<size_t>(directory_offset), 1, '\0');
    const size_t shifted_eocd = eocd + 1;
    for (size_t index = 0; index < 4; ++index) {
        gsl::at(gapped_archive, shifted_eocd + 16 + index) =
            static_cast<char>(
                ((directory_offset + 1) >> (index * 8)) & 0xffu);
    }
    QIQI_TEST_CHECK(WriteFileAtomically(
        backup, codec::AsByteSpan(gapped_archive)));
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    std::string unsupported_archive = good_archive;
    const auto local_header = unsupported_archive.find(
        std::string("\x50\x4b\x03\x04", 4));
    QIQI_TEST_CHECK(local_header != std::string::npos);
    gsl::at(unsupported_archive, local_header + 8) =
        static_cast<char>(8);
    const auto central_header = unsupported_archive.find(
        std::string("\x50\x4b\x01\x02", 4));
    QIQI_TEST_CHECK(central_header != std::string::npos);
    gsl::at(unsupported_archive, central_header + 10) =
        static_cast<char>(8);
    QIQI_TEST_CHECK(WriteFileAtomically(
        backup, codec::AsByteSpan(unsupported_archive)));
    QIQI_TEST_CHECK(
        model.RestoreBackupBundle(
            backup, BackupConflictMode::Replace).stage ==
        BackupRestoreStage::Validate);

    std::vector<uint8_t> rejected_archive;
    QIQI_TEST_CHECK(FAILED(BuildZipArchive(
        std::array{ZipArchiveEntry{
            "unsafe/name.txt", std::vector<uint8_t>{'x'}}},
        rejected_archive)));
    QIQI_TEST_CHECK(FAILED(BuildZipArchive(
        std::array{
            ZipArchiveEntry{"duplicate.txt", std::vector<uint8_t>{'x'}},
            ZipArchiveEntry{"duplicate.txt", std::vector<uint8_t>{'y'}}},
        rejected_archive)));

    // Actual registry write denial, without changing the process user's real configuration.
    WriteRestoreBundle(backup, full_config, {}, {});
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
            READ_CONTROL | WRITE_DAC, key.Put()) == ERROR_SUCCESS);
        DWORD size = 0;
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, nullptr, &size) ==
            ERROR_INSUFFICIENT_BUFFER);
        std::vector<uint8_t> original(size);
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION,
            original.data(), &size) == ERROR_SUCCESS);
        const auto restore_acl = gsl::finally([&]() noexcept {
            ::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, original.data());
        });
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;KW;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
        const ScopedLocalAlloc acl(descriptor);
        QIQI_TEST_CHECK(::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, descriptor) == ERROR_SUCCESS);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) &&
            !result.summary.config_restored);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
        WriteRestoreBundle(backup, full_config, full_user, full_phrase);
        const auto partial = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(partial.status == BackupRestoreStatus::Partial && partial.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(partial.operations_succeeded == 2 && partial.operations_failed == 1);
        QIQI_TEST_CHECK(partial.summary.user_entries_applied == 1 && partial.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(!partial.summary.config_restored && LoadConfig().page_size == 3);
    }
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == old_sequence);

    // Replace must stop when the first deletion fails, preserving the original files and config.
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    // Later preparation failures report already committed destructive work, never full success.
    {
        auto cache = user;
        cache.replace_extension(L".qdict");
        QIQI_TEST_CHECK(WriteFileAtomically(cache, codec::AsByteSpan(std::string_view("cache"))));
        const auto locked = lock_file(cache);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) == old_frequency && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto channel = GetSyncChannelDirectoryForFile(user);
        const auto journal = channel / L"journal.bin";
        // Make the refresh failure independent of channel-directory enumeration order:
        // the locked journal cannot be removed, and neither source nor snapshot can reload it.
        QIQI_TEST_CHECK(std::filesystem::remove(channel / L"snapshot.txt"));
        const auto locked = lock_file(journal);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded > 0 && result.operations_failed == 2);
        QIQI_TEST_CHECK(FAILED(result.refresh_error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && !result.summary.config_restored);
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Best-effort application keeps honest per-item counts and skips config on partial failure.
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary && sharing_failure(result.error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 1 && result.summary.phrase_freq_failed == 0);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        BackupRestoreSummary summary;
        QIQI_TEST_CHECK(!model.RestoreBackupBundle(backup, BackupConflictMode::Merge, summary));
        QIQI_TEST_CHECK(summary.user_entries_failed == 1 && summary.phrase_freq_applied == 1);
    }
    reset();
    WriteRestoreBundle(backup, ConfigJson(""), "abcd 新增词条\n");
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.user_entries_failed == 1);
    }
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(sharing_failure(result.error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 1 && result.summary.user_entries_failed == 0);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    reset();
    WriteRestoreBundle(backup, ConfigJson(""), {}, "abcd 新增词组\n");
    {
        const auto locked = lock_file(GetSyncChannelDirectoryForFile(frequency) / L"writer.lock");
        QIQI_TEST_CHECK(locked);
        const auto count = model.PhraseFreqCount();
        QIQI_TEST_CHECK(model.PromotePhraseFrequencyResult(L" ABCD ", L" 新增词组 ") ==
            HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(model.PhraseFreqCount() == count);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"0x80070020"));
    }

    for (const std::string_view user_data :
         {"abcd 新增词条\n", "abcd ^置顶词条\n", "abcd !屏蔽词条\n"}) {
        reset();
        WriteRestoreBundle(backup, ConfigJson(""), user_data);
        const auto locked = lock_file(GetSyncChannelDirectoryForFile(user) / L"writer.lock");
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"0x80070020"));
    }

    // Missing optional sources remain a valid, empty view and need not be created by Merge.
    {
        DictManagerModel optional;
        const auto missing_user = directory / L"optional_user.txt";
        const auto missing_frequency = directory / L"optional_frequency.txt";
        optional.InitializePaths({}, missing_user, missing_frequency);
        QIQI_TEST_CHECK(optional.ReloadAllResult() == S_OK);
        WriteRestoreBundle(backup, ConfigJson(""));
        const auto result = optional.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 0);
        QIQI_TEST_CHECK(optional.UserEntryCount() == 0 && optional.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(!std::filesystem::exists(missing_user) && !std::filesystem::exists(missing_frequency));
    }

    // A final refresh failure cannot certify a successful restore or a complete displayed view.
    reset();
    WriteRestoreBundle(backup, full_config);
    {
        const ScopedFileHandle locked(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Replace must stop after an unsuccessful intermediate reload, before applying staged items.
    reset();
    {
        const auto module = directory / L"module";
        QIQI_TEST_CHECK(std::filesystem::create_directories(module));
        auto config = LoadConfig();
        config.primary_dict = PrimaryDict::Jidian;
        config.enable_jidian_extra = false;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths(module, user, frequency); // The optional system file is not present yet.
        const auto dictionary = module / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(dictionary, codec::AsByteSpan(std::string_view("abcd 系统词组\n"))));
        const ScopedFileHandle locked(::CreateFileW(dictionary.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        WriteRestoreBundle(backup, full_config, full_user, full_phrase);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.operations_failed == 1 && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
    }

    // Preserve the apply failure independently of refresh failure; discard speculative promotions.
    reset();
    WriteRestoreBundle(backup, ConfigJson(""), {}, "abcd 新增词组\n");
    {
        const ScopedFileHandle unreadable_user(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        const auto unwritable_frequency = lock_file(frequency);
        QIQI_TEST_CHECK(unreadable_user && unwritable_frequency);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency && sharing_failure(result.error));
        QIQI_TEST_CHECK(result.refresh_error == E_FAIL && result.operations_failed == 2);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
        const auto message = FormatBackupRestoreMessage(result, UiLanguage::EnUs);
        QIQI_TEST_CHECK(message.contains(L"phrase ordering") && message.contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Clear can fail after its source write when the required synchronization snapshot is locked.
    reset();
    WriteRestoreBundle(backup, full_config, full_user, full_phrase);
    {
        const auto snapshot = GetSyncChannelDirectoryForFile(frequency) / L"snapshot.qfreq";
        const auto old_snapshot = read_file(snapshot);
        const auto locked = lock_file(snapshot);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Prepare && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.refresh_error == S_OK && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) != old_frequency && model.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(read_file(snapshot) == old_snapshot);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    for (const auto mode : {BackupConflictMode::Merge, BackupConflictMode::Replace}) {
        reset();
        WriteRestoreBundle(
            backup, full_config,
            full_user + "abcd ^置顶词条\nabcd !屏蔽词条\n",
            full_phrase);
        const auto result = model.RestoreBackupBundle(backup, mode);
        if (result.status != BackupRestoreStatus::Success || result.stage != BackupRestoreStage::Complete) {
            std::wcerr << L"Backup restore fixture mode="
                       << (mode == BackupConflictMode::Merge ? L"merge" : L"replace")
                       << L": " << FormatBackupRestoreMessage(result, UiLanguage::EnUs) << std::endl;
        }
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.stage == BackupRestoreStage::Complete);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.error == S_OK && result.operations_failed == 0);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 3 && result.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Pinned, L"置顶词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Blocked, L"屏蔽词条"));
    }

    // The UI's pure formatter cannot label a partial/failed result as successful.
    BackupRestoreResult message_result;
    message_result.operations_failed = 1;
    for (const auto stage : {BackupRestoreStage::Read, BackupRestoreStage::Validate,
             BackupRestoreStage::Prepare, BackupRestoreStage::UserDictionary,
             BackupRestoreStage::PhraseFrequency, BackupRestoreStage::CustomDictionary,
             BackupRestoreStage::Config, BackupRestoreStage::Refresh}) {
        message_result.stage = stage;
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            message_result.status = BackupRestoreStatus::Failed;
            const auto failed = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(failed.contains(language == UiLanguage::EnUs ? L"restore failed" : L"恢复备份失败"));
            message_result.status = BackupRestoreStatus::Partial;
            const auto partial = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(partial.contains(language == UiLanguage::EnUs ? L"restore incomplete" : L"恢复未完成"));
            QIQI_TEST_CHECK(!partial.contains(language == UiLanguage::EnUs ? L"Backup restored" : L"已恢复备份"));
            message_result.refresh_error = E_FAIL;
            const auto incomplete_view = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(incomplete_view.contains(language == UiLanguage::EnUs ?
                L"view may be incomplete" : L"当前显示可能不完整"));
            message_result.refresh_error = S_FALSE;
        }
    }
    message_result.status = BackupRestoreStatus::Success;
    message_result.refresh_error = E_FAIL;
    QIQI_TEST_CHECK(!FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    message_result.refresh_error = S_OK;
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::ZhCn).contains(L"已恢复备份"));
}
} // namespace

void TestBackupRestoreResults() {
    std::exception_ptr failure;
    const auto events = qiqi::test::CaptureLogEvents([&](TRACEHANDLE) {
        try {
            RunBackupRestoreResults();
        } catch (const std::exception&) {
            failure = std::current_exception();
        }
    });
    if (failure) {
        PrintRestoreFixtureTrace(std::span(events).last(std::min<size_t>(80, events.size())));
        std::rethrow_exception(failure);
    }
}

} // namespace qiqi::dict_manager::tests
