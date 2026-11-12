// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

namespace qiqi::test::common_cases {

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
    {
        std::wstring decoded = L"unchanged";
        QIQI_TEST_CHECK(Utf8ToWideResult("五笔ABC", decoded) == S_OK && decoded == L"五笔ABC");
        const std::string embedded{"a\0b", 3};
        QIQI_TEST_CHECK(Utf8ToWideResult(embedded, decoded) == S_OK);
        QIQI_TEST_CHECK(decoded == std::wstring(L"a\0b", 3));
        QIQI_TEST_CHECK(Utf8ToWideResult("\xEF\xBB\xBFtext", decoded) == S_OK);
        QIQI_TEST_CHECK(decoded == L"\uFEFFtext");
        const std::string malformed{"\xC3\x28", 2};
        decoded = L"unchanged";
        QIQI_TEST_CHECK(Utf8ToWideResult(malformed, decoded) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        QIQI_TEST_CHECK(decoded == L"unchanged");
        QIQI_TEST_CHECK(Utf8ToWideResult(malformed, decoded, Utf8ErrorPolicy::Replace) == S_OK);
        QIQI_TEST_CHECK(decoded == L"\uFFFD(" && Utf8ToWide(malformed) == decoded);
        QIQI_TEST_CHECK(Utf8ToWideResult("", decoded) == S_OK && decoded.empty());
    }
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
    static_assert(log::NormalizeKeyword(log::Keyword::Metrics) == log::Keyword::Metrics);
    static_assert(log::KeywordTag(log::Keyword::Metrics) == "metrics");
    static_assert(log::KeywordNameW(std::to_underlying(log::Keyword::Metrics)) == L"Metrics");
    static_assert((std::to_underlying(log::Keyword::Logs) & std::to_underlying(log::Keyword::Metrics)) == 0);
    static_assert((std::to_underlying(log::Keyword::All) & std::to_underlying(log::Keyword::Metrics)) != 0);
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
            const std::span<const uint8_t> oversize_span(
                &dummy_byte, gsl::narrow_cast<size_t>(uint64_t{MAXDWORD} + 1));
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

} // namespace qiqi::test::common_cases
