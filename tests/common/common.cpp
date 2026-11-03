#include "common/test_support.h"
#include "common/binary_codec.h"
#include "common/com_object.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/etw_registration.h"
#include "common/etw_write.h"
#include "common/scoped_handle.h"
#include "common/sound.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <thread>

namespace qiqi::test {

namespace {

std::atomic<ULONG> registration_error{ERROR_SUCCESS};
std::atomic<unsigned int> registration_calls{0};
std::atomic<unsigned int> unregistration_calls{0};
std::atomic<bool> block_registration{false};
std::atomic<bool> registration_entered{false};
std::atomic<bool> release_registration{false};

ULONG RegisterTestProvider() noexcept {
    ++registration_calls;
    if (block_registration) {
        registration_entered.store(true);
        registration_entered.notify_all();
        release_registration.wait(false);
    }
    return registration_error.load();
}

void UnregisterTestProvider() noexcept { ++unregistration_calls; }

ULONG write_error = ERROR_SUCCESS;
unsigned int write_calls = 0;
unsigned int write_reports = 0;
std::array<char, 128> write_report{};

#pragma warning(suppress : 26461) // Match EventWriteTransfer's SDK callback signature.
ULONG WINAPI WriteTestEvent(
    REGHANDLE provider, PCEVENT_DESCRIPTOR descriptor, LPCGUID activity, LPCGUID related,
    ULONG count, PEVENT_DATA_DESCRIPTOR data) {
    QIQI_TEST_CHECK(provider == 17 && descriptor && descriptor->Level == 4);
    QIQI_TEST_CHECK(!activity && !related && count == 1 && data && data->Size == 3);
    ++write_calls;
    ::SetLastError(ERROR_INVALID_DATA);
    return write_error;
}

void WINAPI CollectWriteFailure(LPCSTR message) {
    ++write_reports;
    QIQI_TEST_CHECK(strcpy_s(write_report.data(), write_report.size(), message) == 0);
    ::SetLastError(ERROR_ACCESS_DENIED);
}

void TestEtwWriteDiagnostics() {
    const DWORD saved_error = ::GetLastError();
    const auto restore_error = gsl::finally([saved_error]() noexcept { ::SetLastError(saved_error); });
    EVENT_DESCRIPTOR descriptor{};
    descriptor.Level = 4;
    EVENT_DATA_DESCRIPTOR data{};
    constexpr std::array<char, 3> payload{'a', 'b', 'c'};
    ::EventDataDescCreate(&data, payload.data(), gsl::narrow_cast<ULONG>(payload.size()));
    write_calls = 0;
    write_reports = 0;
    for (const ULONG error : {ERROR_SUCCESS, ERROR_NOT_ENOUGH_MEMORY, ERROR_MORE_DATA, ERROR_ACCESS_DENIED}) {
        write_error = error;
        ::SetLastError(ERROR_FILE_NOT_FOUND);
        QIQI_TEST_CHECK(log::detail::WriteEtwEvent(
            17, &descriptor, nullptr, nullptr, 1, &data, WriteTestEvent, CollectWriteFailure) == error);
        QIQI_TEST_CHECK(::GetLastError() == ERROR_FILE_NOT_FOUND);
        if (error != ERROR_SUCCESS) {
            QIQI_TEST_CHECK(std::string_view(write_report.data()) ==
                std::format("QiQiIME: EventWriteTransfer failed (error={})\n", error));
        } else {
            QIQI_TEST_CHECK(write_reports == 0);
        }
    }
    QIQI_TEST_CHECK(write_calls == 4 && write_reports == 3);
}

void TestEtwRegistrationLifetime() {
    registration_calls = 0;
    unregistration_calls = 0;
    registration_error = ERROR_ACCESS_DENIED;
    {
        log::detail::EtwRegistration registration(RegisterTestProvider, UnregisterTestProvider);
        QIQI_TEST_CHECK(!registration.IsRegistered());
        registration.Shutdown();
        QIQI_TEST_CHECK(registration_calls == 0 && unregistration_calls == 0);
        QIQI_TEST_CHECK(registration.EnsureRegistered() == ERROR_ACCESS_DENIED);
        QIQI_TEST_CHECK(!registration.IsRegistered());
        registration.Shutdown();
        QIQI_TEST_CHECK(unregistration_calls == 0);

        registration_error = ERROR_SUCCESS;
        block_registration = true;
        registration_entered = false;
        release_registration = false;
        std::jthread first([&]() {
            QIQI_TEST_CHECK(registration.EnsureRegistered() == ERROR_SUCCESS);
        });
        registration_entered.wait(false);
        QIQI_TEST_CHECK(!registration.IsRegistered());
        std::vector<std::jthread> callers;
        for (int i = 0; i < 8; ++i) {
            callers.emplace_back([&]() {
                QIQI_TEST_CHECK(registration.EnsureRegistered() == ERROR_SUCCESS);
                QIQI_TEST_CHECK(registration.IsRegistered());
            });
        }
        release_registration.store(true);
        release_registration.notify_all();
        first.join();
        callers.clear();
        block_registration = false;
        QIQI_TEST_CHECK(registration_calls == 2 && registration.IsRegistered());
        registration.Shutdown();
        registration.Shutdown();
        QIQI_TEST_CHECK(unregistration_calls == 1 && !registration.IsRegistered());
        QIQI_TEST_CHECK(registration.EnsureRegistered() == ERROR_SUCCESS);
    }
    QIQI_TEST_CHECK(registration_calls == 3 && unregistration_calls == 2);
}

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 8, 4});
    static_assert(version::SemanticVersion{1, 0, 0} > version::kCurrentVersion);
    QIQI_TEST_CHECK(!version::kVersion.empty());
    QIQI_TEST_CHECK(!version::kVersionW.empty());
    QIQI_TEST_CHECK(!version::kFullVersion.empty());
    QIQI_TEST_CHECK(!version::kFullVersionW.empty());
    QIQI_TEST_CHECK(!version::kGitCommit.empty());
    QIQI_TEST_CHECK(!version::kGitCommitW.empty());
    QIQI_TEST_CHECK(version::kArch == (sizeof(void*) == 8 ? "x64" : "x86"));
    QIQI_TEST_CHECK(version::kArchW == (sizeof(void*) == 8 ? L"x64" : L"x86"));
    QIQI_TEST_CHECK(version::kFullVersion.starts_with(version::kVersion));
}

void TestEtwLogger() {
    log::Shutdown();
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    log::Shutdown();
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    log::SetLevel(log::Level::debug);
    QIQI_LOG_DEBUG("Logging does not initialize the provider");
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    QIQI_TEST_CHECK(log::EnsureInitialized());
    QIQI_TEST_CHECK(log::IsEtwProviderRegistered() && log::GetLevel() == log::Level::debug);

    // 2. Test WideToUtf8, Utf8ToWide, and GetModuleFilePath
    QIQI_TEST_CHECK(WideToUtf8(L"").empty());
    QIQI_TEST_CHECK(WideToUtf8(L"五笔ABC") == "五笔ABC");
    QIQI_TEST_CHECK(Utf8ToWide("").empty());
    QIQI_TEST_CHECK(Utf8ToWide("五笔ABC") == L"五笔ABC");
    QIQI_TEST_CHECK(!GetModuleFilePath(nullptr).empty());
    QIQI_TEST_CHECK(GetModuleFilePath(reinterpret_cast<void*>(0x12345678)).empty());
    {
        const ScopedClipboard lock_clip(nullptr);
        QIQI_TEST_CHECK(lock_clip.IsOpen());
        const ScopedWindow other_owner(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        // When another window tries to open the clipboard on a background thread while locked, OpenClipboard fails:
        std::jthread worker([hwnd = other_owner.Get()]() {
            QIQI_TEST_CHECK(CopyTextToClipboard(hwnd, L"locked") != ERROR_SUCCESS);
        });
    }
    QIQI_TEST_CHECK(LoadAppIconOrBrandFallback(nullptr, 24, 1, false).IsValid());
    QIQI_TEST_CHECK(LoadAppIconOrBrandFallback(::GetModuleHandleW(nullptr), 24, 9999, true).IsValid());

    // 3. Test Pure Lock-Free ETW Logger (InitLogger, SetLevel, GetLevel, IsLogSinkActive, WriteLogMessage)
    log::InitLogger(log::Level::info);
    QIQI_TEST_CHECK(log::GetLevel() == log::Level::info);
    std::ignore = log::IsLogSinkActive();
    log::SetLevel(log::Level::trace);
    QIQI_TEST_CHECK(log::GetLevel() == log::Level::trace);

    QIQI_LOG_TRACE("Trace message");
    QIQI_LOG_DEBUG("Debug message");
    QIQI_LOG_INFO("Unit test log entry: {}", WideToUtf8(L"五笔日志测试"));
    QIQI_LOG_WARN("Warn message");
    QIQI_LOG_ERROR("Error message");
    QIQI_LOG_CRITICAL("Critical macro message");
    log::WriteLogMessage(log::Level::trace, "Direct trace");
    log::WriteLogMessage(log::Level::debug, "Direct debug");
    log::WriteLogMessage(log::Level::info, "Direct info");
    log::WriteLogMessage(log::Level::warn, "Direct warn");
    log::WriteLogMessage(log::Level::err, "Direct err");
    log::WriteLogMessage(log::Level::critical, "Critical message");

    static_assert(log::NormalizeKeyword(log::Keyword::General) == log::Keyword::General);
    static_assert(log::NormalizeKeyword(log::Keyword::All) == log::Keyword::General);
    static_assert(log::NormalizeOpcode(log::Opcode::Start) == log::Opcode::Start);
    static_assert(log::NormalizeOpcode(static_cast<log::Opcode>(99)) == log::Opcode::Info);
    static_assert(log::KeywordTag(log::Keyword::Composition) == "composition");
    static_assert(log::KeywordTag(log::Keyword::All) == "general");
    static_assert(log::KeywordNameW(std::to_underlying(log::Keyword::Sync)) == L"Sync");
    static_assert(log::KeywordNameW(0) == L"General");
    static_assert(log::OpcodeTag(log::Opcode::Stop) == "stop");
    static_assert(log::OpcodeNameW(std::to_underlying(log::Opcode::Start)) == L"Start");
    static_assert(log::OpcodeNameW(99) == L"Info");
    const uint8_t runtime_invalid_opcode = ::GetCurrentProcessId() != 0 ? uint8_t{99} : uint8_t{0};
    const uint8_t runtime_stop_opcode =
        ::GetCurrentProcessId() != 0 ? std::to_underlying(log::Opcode::Stop) : uint8_t{0};
    QIQI_TEST_CHECK(log::NormalizeOpcode(static_cast<log::Opcode>(runtime_invalid_opcode)) == log::Opcode::Info);
    QIQI_TEST_CHECK(log::OpcodeNameW(runtime_stop_opcode) == L"Stop");
    log::ReportLoggingFailure();

    GUID ambient_before = GUID_NULL;
    QIQI_TEST_CHECK(::EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, &ambient_before) == ERROR_SUCCESS);
    ::SetLastError(ERROR_NOT_READY);
    const GUID act_a = log::CreateActivityId();
    QIQI_TEST_CHECK(::GetLastError() == ERROR_NOT_READY);
    const GUID act_b = log::CreateActivityId();
    GUID ambient_after = GUID_NULL;
    QIQI_TEST_CHECK(::EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, &ambient_after) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(act_a != GUID_NULL && act_b != GUID_NULL && act_a != act_b);
    QIQI_TEST_CHECK(ambient_before == ambient_after);

    for (const log::Keyword kw : {
             log::Keyword::General,
             log::Keyword::Lifecycle,
             log::Keyword::Input,
             log::Keyword::Composition,
             log::Keyword::Dictionary,
             log::Keyword::Sync,
             log::Keyword::UI,
             log::Keyword::Config}) {
        QIQI_LOG_CAT_TRACE(kw, "Category trace {}", log::KeywordTag(kw));
        QIQI_LOG_CAT_DEBUG(kw, "Category debug {}", log::KeywordTag(kw));
        QIQI_LOG_CAT_INFO(kw, "Category info {}", log::KeywordTag(kw));
        QIQI_LOG_CAT_WARN(kw, "Category warn {}", log::KeywordTag(kw));
        QIQI_LOG_CAT_ERROR(kw, "Category error {}", log::KeywordTag(kw));
        QIQI_LOG_ACT_START(log::Level::debug, kw, &act_a, &act_b, "Activity start {}", log::KeywordTag(kw));
        QIQI_LOG_ACT_INFO(log::Level::info, kw, &act_a, "Activity info {}", log::KeywordTag(kw));
        QIQI_LOG_ACT_STOP(log::Level::warn, kw, &act_a, "Activity stop {}", log::KeywordTag(kw));
    }

    log::SetLevel(log::Level::off);
    QIQI_TEST_CHECK(!log::ShouldLog(log::Level::critical, log::Keyword::Lifecycle) &&
                    !log::ShouldLog(log::Level::info, log::Keyword::Sync) &&
                    !log::ShouldLog(log::Level::trace) && !log::ShouldLog(log::Level::off));
    ::SetLastError(ERROR_ACCESS_DENIED);
    QIQI_TEST_CHECK(log::CheckHResult(S_OK, "successful operation"));
    QIQI_TEST_CHECK(log::CheckHResult(S_FALSE, "optional operation"));
    QIQI_TEST_CHECK(log::CheckHResult(TF_S_ASYNC, "queued edit session"));
    QIQI_TEST_CHECK(!log::CheckHResult(
        E_ACCESSDENIED,
        "failed operation",
        log::Level::warn,
        log::LogContext{log::Keyword::Composition, log::Opcode::Info, &act_a, &act_b}));
    QIQI_TEST_CHECK(::GetLastError() == ERROR_ACCESS_DENIED);
    QIQI_LOG_INFO("Suppressed when level is off");
    log::WriteLogMessage(log::Level::off, "Suppressed off write");
    log::SetLevel(log::Level::info);
    QIQI_TEST_CHECK(!log::ShouldLog(log::Level::off));
    ::SetLastError(ERROR_SHARING_VIOLATION);
    QIQI_TEST_CHECK(!log::CheckHResult(E_FAIL, "failed operation", log::Level::err));
    QIQI_TEST_CHECK(::GetLastError() == ERROR_SHARING_VIOLATION);

    // 4. Test GetAppDataDirectory fallback when LOCALAPPDATA is unset and explicit reinitialization.
    {
        const auto empty_file = std::filesystem::temp_directory_path() /
            (L"qiqi_empty_read_" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        const auto remove_empty = gsl::finally([&empty_file]() noexcept {
            std::error_code ec;
            std::filesystem::remove(empty_file, ec);
        });
        QIQI_TEST_CHECK(WriteFileAtomicallyResult(empty_file, {}) == S_OK);
        std::string empty_content = "non-empty";
        QIQI_TEST_CHECK(ReadFileToUtf8String(empty_file, empty_content) && empty_content.empty());
        constexpr uint8_t dummy_byte = 0;
        if constexpr (sizeof(size_t) > sizeof(DWORD)) {
            const std::span<const uint8_t> oversize_span(&dummy_byte, static_cast<size_t>(MAXDWORD) + 1);
            QIQI_TEST_CHECK(WriteFileAtomicallyResult(empty_file, oversize_span) == E_INVALIDARG);
        }
        QIQI_TEST_CHECK(WriteFileAtomicallyResult(empty_file / L"nested.txt", std::span(&dummy_byte, 1)) != S_OK);
        QIQI_TEST_CHECK(GetSyncChannelDirectoryForFile({}).empty());
        // Test BinarySpanReader out-of-bounds branches (ReadLeNumber, ReadBytes, ReadWString)
        constexpr std::array<uint8_t, 2> short_buf{1, 2};
        codec::BinarySpanReader num_reader(short_buf);
        QIQI_TEST_CHECK(num_reader.ReadLe32() == 0 && !num_reader.Valid());
        codec::BinarySpanReader bytes_reader(short_buf);
        QIQI_TEST_CHECK(bytes_reader.ReadBytes(5).empty() && !bytes_reader.Valid());
        std::vector<uint8_t> bad_wstr;
        codec::AppendLe32(bad_wstr, 10);
        codec::BinarySpanReader wstr_reader(bad_wstr);
        QIQI_TEST_CHECK(wstr_reader.ReadWString(4).empty() && !wstr_reader.Valid());
    }
    {
        wchar_t* orig_localappdata = nullptr;
        size_t orig_len = 0;
        std::ignore = _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
        const std::unique_ptr<wchar_t, decltype(&std::free)> orig_localappdata_owner(
            orig_localappdata, &std::free);
        const auto restore_env = gsl::finally([&orig_localappdata_owner]() noexcept {
            if (orig_localappdata_owner) {
                _wputenv_s(L"LOCALAPPDATA", orig_localappdata_owner.get());
            }
        });
        _wputenv_s(L"LOCALAPPDATA", L"");
        QIQI_TEST_CHECK(!GetAppDataDirectory().empty());
    }
    log::Shutdown();
    log::WriteLogMessage(log::Level::info, "Direct writing does not reinitialize after Shutdown");
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    log::Shutdown();
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    log::SetLevel(log::Level::off);
    QIQI_TEST_CHECK(log::EnsureInitialized());
    QIQI_TEST_CHECK(log::IsEtwProviderRegistered() && log::GetLevel() == log::Level::off);
    log::InitLogger();
}

void TestIniConfiguration() {
    // 1. Test ResolveBundledDataFile fallback across <dll_dir>/<name>, <dll_dir>/../data/<name>, and <dll_dir>/../<name>
    {
        const auto base_dir = std::filesystem::temp_directory_path() / L"QiQiIME_BundledLayoutTest";
        const auto x64_dir = base_dir / L"x64";
        const auto shared_data_dir = base_dir / L"data";
        std::error_code layout_ec;
        std::filesystem::remove_all(base_dir, layout_ec);
        std::filesystem::create_directories(x64_dir, layout_ec);
        std::filesystem::create_directories(shared_data_dir, layout_ec);

        QIQI_TEST_CHECK(ResolveBundledDataFile({}, L"wubi86_jidian.txt") == std::filesystem::path(L"wubi86_jidian.txt"));
        const std::array<uint8_t, 4> dummy_bytes{'a', ' ', 'x', '\n'};
        // When file exists in ../data/, ResolveBundledDataFile(x64_dir, ...) resolves to ../data/
        const auto shared_dict = shared_data_dir / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(shared_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == shared_dict);

        // When file exists in ../, ResolveBundledDataFile(x64_dir, ...) resolves to ../
        const auto parent_dict = base_dir / L"wubi86_haifeng.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(parent_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_haifeng.txt") == parent_dict);

        // When file exists directly in dll_dir, direct path takes precedence
        const auto local_dict = x64_dir / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(local_dict, dummy_bytes));
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == local_dict);

        std::filesystem::remove_all(base_dir, layout_ec);
    }

    // 2. Test pure HKCU registry configuration & user_dict binary mirroring (using isolated test subkey)
    constexpr const wchar_t* kTestRegSubKey = L"Software\\QiQiIME_UnitTest";
    ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
    SetConfigRegistrySubKeyOverrideForTest(kTestRegSubKey);
    const auto restore_reg = gsl::finally([]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
    });

    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == kTestRegSubKey);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == 0);
    QiQiConfig reg_out{};
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out));
    QIQI_TEST_CHECK(!SaveConfigToRegistry(reg_out, L""));
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out, nullptr, L""));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(L"") == 0);

    // First LoadConfig initializes default values in HKCU\<subkey>
    const QiQiConfig discovered = LoadConfig(nullptr);
    QIQI_TEST_CHECK(discovered.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(discovered.enable_jidian_extra);
    QIQI_TEST_CHECK(discovered.page_size == 5);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() >= 1);

    // Save custom config with all fields modified and verify round-trip via LoadConfigFromRegistry & LoadConfig
    QiQiConfig custom_reg_cfg{};
    custom_reg_cfg.primary_dict = PrimaryDict::Haifeng;
    custom_reg_cfg.enable_jidian_extra = false;
    custom_reg_cfg.user_dict_path = L"custom_words.txt";
    custom_reg_cfg.page_size = 8;
    custom_reg_cfg.auto_commit_4code = false;
    custom_reg_cfg.push_commit_5th = false;
    custom_reg_cfg.semicolon_quote_quick_select = false;
    custom_reg_cfg.show_code_hint = false;
    custom_reg_cfg.enable_z_temp_english = false;
    custom_reg_cfg.dynamic_phrase_freq = true;
    custom_reg_cfg.default_chinese_mode = false;
    custom_reg_cfg.default_full_width = true;
    custom_reg_cfg.default_chinese_punct = false;
    custom_reg_cfg.shift_toggle_mode = false;
    custom_reg_cfg.show_status_bar = true;
    custom_reg_cfg.beep_on_empty_4code = false;
    custom_reg_cfg.beep_on_dup_4code = true;
    custom_reg_cfg.clear_on_empty_4code = true;
    custom_reg_cfg.enable_key_sound = true;
    custom_reg_cfg.up_down_page_turn = true;
    custom_reg_cfg.ui_language = UiLanguage::EnUs;
    custom_reg_cfg.underline_style = CompositionUnderlineStyle::Squiggle;
    custom_reg_cfg.bold_underline = true;
    custom_reg_cfg.log_level = log::Level::debug;

    QIQI_TEST_CHECK(SaveConfig(custom_reg_cfg));
    uint64_t seq_after_save = 0;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(reg_out, &seq_after_save));
    QIQI_TEST_CHECK(seq_after_save >= 2);
    QIQI_TEST_CHECK(reg_out.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!reg_out.enable_jidian_extra);
    QIQI_TEST_CHECK(reg_out.user_dict_path == L"custom_words.txt");
    QIQI_TEST_CHECK(reg_out.page_size == 8);
    QIQI_TEST_CHECK(!reg_out.auto_commit_4code);
    QIQI_TEST_CHECK(!reg_out.push_commit_5th);
    QIQI_TEST_CHECK(!reg_out.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!reg_out.show_code_hint);
    QIQI_TEST_CHECK(!reg_out.enable_z_temp_english);
    QIQI_TEST_CHECK(reg_out.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!reg_out.default_chinese_mode);
    QIQI_TEST_CHECK(reg_out.default_full_width);
    QIQI_TEST_CHECK(!reg_out.default_chinese_punct);
    QIQI_TEST_CHECK(!reg_out.shift_toggle_mode);
    QIQI_TEST_CHECK(reg_out.show_status_bar);
    QIQI_TEST_CHECK(!reg_out.beep_on_empty_4code);
    QIQI_TEST_CHECK(reg_out.beep_on_dup_4code);
    QIQI_TEST_CHECK(reg_out.clear_on_empty_4code);
    QIQI_TEST_CHECK(reg_out.enable_key_sound);
    QIQI_TEST_CHECK(reg_out.up_down_page_turn);
    QIQI_TEST_CHECK(reg_out.ui_language == UiLanguage::EnUs);
    QIQI_TEST_CHECK(reg_out.underline_style == CompositionUnderlineStyle::Squiggle);
    QIQI_TEST_CHECK(reg_out.bold_underline);
    QIQI_TEST_CHECK(reg_out.log_level == log::Level::debug);

    const QiQiConfig reloaded = LoadConfig(nullptr);
    QIQI_TEST_CHECK(reloaded.page_size == 8 && reloaded.primary_dict == PrimaryDict::Haifeng);

    // Verify user_dict binary registry mirror round-trip and edge cases
    std::vector<uint8_t> loaded_dict_bytes;
    QIQI_TEST_CHECK(!LoadUserDictBinaryFromRegistry(loaded_dict_bytes));
    QIQI_TEST_CHECK(!SaveUserDictBinaryToRegistry({}));
    QIQI_TEST_CHECK(!SaveUserDictBinaryToRegistry(std::array<uint8_t, 4>{1, 2, 3, 4}, L""));
    QIQI_TEST_CHECK(!LoadUserDictBinaryFromRegistry(loaded_dict_bytes, L""));
    const std::array<uint8_t, 8> sample_qdict{0x51, 0x44, 0x43, 0x54, 1, 2, 3, 4};
    QIQI_TEST_CHECK(SaveUserDictBinaryToRegistry(sample_qdict));
    QIQI_TEST_CHECK(LoadUserDictBinaryFromRegistry(loaded_dict_bytes));
    QIQI_TEST_CHECK(loaded_dict_bytes.size() == sample_qdict.size());
}

void TestScopedHandleTemplate() {
    ScopedKernelHandle h1(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(h1.IsValid());
    QIQI_TEST_CHECK(static_cast<bool>(h1));

    // Move construction
    ScopedKernelHandle h2(std::move(h1));
    QIQI_TEST_CHECK(!h1.IsValid());
    QIQI_TEST_CHECK(h2.IsValid());

    // Move assignment
    ScopedKernelHandle h3;
    h3 = std::move(h2);
    QIQI_TEST_CHECK(!h2.IsValid());
    QIQI_TEST_CHECK(h3.IsValid());

    h3.Reset();
    QIQI_TEST_CHECK(!h3.IsValid());

    // Test ScopedKernelTransaction RAII (rollback-on-destruction vs explicit Commit)
    {
        constexpr const wchar_t* kTxnTestSubKey = L"Software\\QiQiIME_TxnUnitTest";
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTxnTestSubKey);
        const auto cleanup_txn_key = gsl::finally([]() noexcept {
            ::RegDeleteTreeW(HKEY_CURRENT_USER, kTxnTestSubKey);
        });

        const ScopedKernelTransaction invalid_txn(INVALID_HANDLE_VALUE);
        QIQI_TEST_CHECK(!invalid_txn && !invalid_txn.IsValid() && !invalid_txn.IsCommitted());
        QIQI_TEST_CHECK(invalid_txn.Error() == ERROR_INVALID_HANDLE);

        // 1) Uncommitted ScopedKernelTransaction automatically rolls back on scope exit and preserves GetLastError()
        ::SetLastError(ERROR_FILE_NOT_FOUND);
        {
            ScopedKernelTransaction txn;
            QIQI_TEST_CHECK(txn.IsValid() && !txn.IsCommitted() && txn.Error() == ERROR_SUCCESS);
            ScopedKernelTransaction moved_txn(std::move(txn));
            QIQI_TEST_CHECK(!txn.IsValid() && txn.Error() == ERROR_INVALID_HANDLE &&
                            moved_txn.IsValid() && moved_txn.Error() == ERROR_SUCCESS);
            ScopedKernelTransaction assigned_txn(INVALID_HANDLE_VALUE);
            assigned_txn = std::move(moved_txn);
            QIQI_TEST_CHECK(!moved_txn.IsValid() && moved_txn.Error() == ERROR_INVALID_HANDLE &&
                            assigned_txn.IsValid() && assigned_txn.Error() == ERROR_SUCCESS);

            ScopedRegKey key;
            DWORD disp = 0;
            QIQI_TEST_CHECK(::RegCreateKeyTransactedW(
                                HKEY_CURRENT_USER,
                                kTxnTestSubKey,
                                0,
                                nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | KEY_WRITE,
                                nullptr,
                                key.Put(),
                                &disp,
                                assigned_txn.Get(),
                                nullptr) == ERROR_SUCCESS);
            constexpr DWORD val = 123;
            QIQI_TEST_CHECK(::RegSetValueExW(
                                key.Get(),
                                L"TestValue",
                                0,
                                REG_DWORD,
                                reinterpret_cast<const BYTE*>(&val),
                                sizeof(val)) == ERROR_SUCCESS);
            ::SetLastError(ERROR_SHARING_VIOLATION);
        }
        QIQI_TEST_CHECK(::GetLastError() == ERROR_SHARING_VIOLATION);
        ScopedRegKey verify_rolled_back;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
                            HKEY_CURRENT_USER, kTxnTestSubKey, 0, KEY_READ, verify_rolled_back.Put()) !=
                        ERROR_SUCCESS);

        // 2) Committed ScopedKernelTransaction persists changes
        {
            ScopedKernelTransaction txn;
            QIQI_TEST_CHECK(txn.IsValid());
            ScopedRegKey key;
            DWORD disp = 0;
            QIQI_TEST_CHECK(::RegCreateKeyTransactedW(
                                HKEY_CURRENT_USER,
                                kTxnTestSubKey,
                                0,
                                nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | KEY_WRITE,
                                nullptr,
                                key.Put(),
                                &disp,
                                txn.Get(),
                                nullptr) == ERROR_SUCCESS);
            constexpr DWORD val = 456;
            QIQI_TEST_CHECK(::RegSetValueExW(
                                key.Get(),
                                L"TestValue",
                                0,
                                REG_DWORD,
                                reinterpret_cast<const BYTE*>(&val),
                                sizeof(val)) == ERROR_SUCCESS);
            key.Reset();
            QIQI_TEST_CHECK(txn.Commit() && txn.IsCommitted() && txn.Error() == ERROR_SUCCESS);
            QIQI_TEST_CHECK(!txn.Commit() && txn.Error() == ERROR_INVALID_STATE);
        }
        ScopedRegKey verify_committed;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
                            HKEY_CURRENT_USER, kTxnTestSubKey, 0, KEY_READ, verify_committed.Put()) ==
                        ERROR_SUCCESS);
    }

    // Test ScopedGlobalLock<wchar_t>, ScopedClipboard, ScopedLastErrorRestore, MakeInt4Variant, and CopyToBstr
    ::SetLastError(ERROR_SUCCESS);
    const ScopedGlobalLock<wchar_t> null_lock(nullptr);
    QIQI_TEST_CHECK(!null_lock && null_lock.Get() == nullptr && null_lock.Error() == ERROR_INVALID_HANDLE);

    const HGLOBAL hmem = ::GlobalAlloc(GMEM_MOVEABLE, 16 * sizeof(wchar_t));
    QIQI_TEST_CHECK(hmem != nullptr);
    if (auto* raw_ptr = static_cast<wchar_t*>(::GlobalLock(hmem))) {
        wcscpy_s(raw_ptr, 16, L"奇奇五笔");
        ::GlobalUnlock(hmem);
    }
    {
        const ScopedGlobalLock<wchar_t> lock(hmem);
        QIQI_TEST_CHECK(lock && lock.Error() == ERROR_SUCCESS && std::wstring(lock.Get()) == L"奇奇五笔");
        ::SetLastError(ERROR_ALREADY_EXISTS);
    }
    QIQI_TEST_CHECK(::GetLastError() == ERROR_ALREADY_EXISTS);
    ::GlobalFree(hmem);

    {
        const ScopedClipboard clip{};
        if (clip.IsOpen()) {
            QIQI_TEST_CHECK(clip.Error() == ERROR_SUCCESS);
        } else {
            QIQI_TEST_CHECK(clip.Error() != ERROR_SUCCESS);
        }
        std::ignore = static_cast<bool>(clip);
        ::SetLastError(ERROR_PATH_NOT_FOUND);
    }
    QIQI_TEST_CHECK(::GetLastError() == ERROR_PATH_NOT_FOUND);

    const VARIANT v42 = MakeInt4Variant(42);
    QIQI_TEST_CHECK(v42.vt == VT_I4 && v42.lVal == 42);

    QIQI_TEST_CHECK(CopyToBstr(L"test", nullptr) == E_INVALIDARG);
    ScopedBstr bstr_out;
    QIQI_TEST_CHECK(SUCCEEDED(CopyToBstr(L"奇奇", bstr_out.Put())) &&
                    bstr_out.IsValid() &&
                    std::wstring(bstr_out.Get()) == L"奇奇");

    // Test CreateUiFont and WriteFileAtomically (including directory error branch)
    {
        const ScopedGdiHandle<HFONT> ui_font = CreateUiFont(16, FW_MEDIUM);
        QIQI_TEST_CHECK(ui_font.IsValid());

        const auto atomic_path = std::filesystem::temp_directory_path() / L"qiqiime_atomic_test.bin";
        const std::array<uint8_t, 4> payload{1, 2, 3, 4};
        QIQI_TEST_CHECK(WriteFileAtomically(atomic_path, payload));
        std::string read_back;
        QIQI_TEST_CHECK(ReadFileToUtf8String(atomic_path, read_back) && read_back.size() == 4);
        std::error_code rm_ec;
        std::filesystem::remove(atomic_path, rm_ec);
        QIQI_TEST_CHECK(!WriteFileAtomically(std::filesystem::temp_directory_path(), payload));
    }

    // Test ScopedSaveDC (both null HDC and valid memory HDC state restoration)
    {
        const ScopedSaveDC null_save(nullptr);
        QIQI_TEST_CHECK(!null_save);
    }
    {
        const ScopedMemoryDC mem_dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(mem_dc.IsValid());
        ::SetTextColor(mem_dc.Get(), RGB(12, 34, 56));
        ::SetBkMode(mem_dc.Get(), OPAQUE);
        {
            const ScopedSaveDC save_dc(mem_dc.Get());
            QIQI_TEST_CHECK(static_cast<bool>(save_dc));
            ::SetTextColor(mem_dc.Get(), RGB(200, 100, 50));
            ::SetBkMode(mem_dc.Get(), TRANSPARENT);
            QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(200, 100, 50));
            QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == TRANSPARENT);
        }
        QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(12, 34, 56));
        QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == OPAQUE);
    }

        // Test BindOrGetWindowUserData, ClientPointFromLParam, and PaintWindowClient on a temporary window
    {
        const ScopedWindow test_wnd(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 100, 200, 50, 40, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(test_wnd.IsValid());
        int payload = 12345;
        CREATESTRUCTW cs{};
        cs.lpCreateParams = &payload;
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_NCCREATE, 0) == nullptr);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(
                            test_wnd.Get(), WM_NCCREATE, reinterpret_cast<LPARAM>(&cs)) == &payload);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_PAINT, 0) == &payload);

        const POINT client_pt = ClientPointFromLParam(test_wnd.Get(), MAKELPARAM(15, 25), false);
        QIQI_TEST_CHECK(client_pt.x == 15 && client_pt.y == 25);
        const POINT nc_null_pt = ClientPointFromLParam(nullptr, MAKELPARAM(115, 225), true);
        QIQI_TEST_CHECK(nc_null_pt.x == 115 && nc_null_pt.y == 225);
        POINT screen_pt{10, 20};
        ::ClientToScreen(test_wnd.Get(), &screen_pt);
        const POINT nc_converted = ClientPointFromLParam(
            test_wnd.Get(), MAKELPARAM(screen_pt.x, screen_pt.y), true);
        QIQI_TEST_CHECK(nc_converted.x == 10 && nc_converted.y == 20);

        bool painted = false;
        QIQI_TEST_CHECK(PaintWindowClient(test_wnd.Get(), [&painted](HDC hdc, const RECT& rc) {
            painted = (hdc != nullptr && rc.right == 50 && rc.bottom == 40);
        }) == 0);
        QIQI_TEST_CHECK(painted);
    }
}

void TestI18nLocalization() {
    // 1. Direct language resolution
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::ZhCn) == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::EnUs) == UiLanguage::EnUs);
    const UiLanguage auto_resolved = ResolveUiLanguage(UiLanguage::Auto);
    QIQI_TEST_CHECK(auto_resolved == UiLanguage::ZhCn || auto_resolved == UiLanguage::EnUs);

    // 2. Bilingual string tables verification
    const UiStrings& zh = GetUiStrings(UiLanguage::ZhCn);
    const UiStrings& en = GetUiStrings(UiLanguage::EnUs);
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_header_prefix) == L"五笔 [");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_header_prefix) == L"Wubi [");
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_empty) == L"(无候选)");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_empty) == L"(No candidates)");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_logo) == L"五笔");
    QIQI_TEST_CHECK(std::wstring_view(en.status_logo) == L"Wubi");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_mode_cn) == L"中");
    QIQI_TEST_CHECK(std::wstring_view(en.status_mode_cn) == L"CN");
    QIQI_TEST_CHECK(std::wstring_view(zh.dlg_btn_ok) == L"确定");
    QIQI_TEST_CHECK(std::wstring_view(en.dlg_btn_ok) == L"OK");

    // 3. Test sound synthesizer and PlayImeSound (both live WAV synthesis and muted test mode)
    ResetLastPlayedSoundForTest();
    PlayImeSound(SoundEffectKind::None);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::None);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    PlayImeSound(SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    PlayImeSound(SoundEffectKind::DupCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);

    SetMuteAudioOutputForTest(true);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    SetMuteAudioOutputForTest(false);
}

} // namespace

void RunCommonTests() {
    TestEtwWriteDiagnostics();
    TestEtwRegistrationLifetime();
    TestEtwLogger();
    TestVersionMechanism();
    TestIniConfiguration();
    TestScopedHandleTemplate();
    TestI18nLocalization();
}

} // namespace qiqi::test
