// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/RAII.h"
#include "ime/comp/tests.h"
#include "ime/tsf/test_trace.h"
#include <windows.h>
#include <objbase.h>
#include <gsl/gsl>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <filesystem>
#include <fcntl.h>
#include <io.h>

namespace qiqi::test {
void RunTsfCandidateLayoutTests();
void RunRestrictedInputContractTests();
void RunRestrictedInputDiagnostics();
int RunFocusMetricsEtwTest();
void RunFocusEnvironmentIsolationTest();
int RunFocusBenchmark(const std::filesystem::path& dll, std::wstring_view mode);
}

namespace {

struct DiagnosticStreamFailure final {};

class ThrowingDiagnosticBuffer final : public std::streambuf {
public:
    unsigned int writes = 0;
protected:
    int_type overflow(int_type) override {
        ++writes;
        throw DiagnosticStreamFailure{};
    }
};

struct DiagnosticDescriptorTraits {
    using HandleType = int;
    static constexpr int Invalid() noexcept { return -1; }
    static constexpr bool IsValid(int descriptor) noexcept { return descriptor >= 0; }
    static void Close(int descriptor) noexcept { QIQI_TEST_CHECK_CLEANUP(::_close(descriptor) == 0); }
};

class DiagnosticStderrCapture final {
public:
    explicit DiagnosticStderrCapture(FILE* stream)
        : stream_(stream), descriptor_(::_fileno(stream)), previous_(::_dup(descriptor_)) {
        QIQI_TEST_CHECK(previous_ && std::fflush(stream_) == 0);
        std::array<int, 2> pipe{};
        QIQI_TEST_CHECK(::_pipe(pipe.data(), 4096, _O_BINARY | _O_NOINHERIT) == 0);
        read_.Reset(pipe.front());
        const ScopedDescriptor write(pipe.back());
        QIQI_TEST_CHECK(::_dup2(write.Get(), descriptor_) == 0);
    }
    ~DiagnosticStderrCapture() noexcept { Restore(); }
    DiagnosticStderrCapture(const DiagnosticStderrCapture&) = delete;
    DiagnosticStderrCapture& operator=(const DiagnosticStderrCapture&) = delete;
    DiagnosticStderrCapture(DiagnosticStderrCapture&&) = delete;
    DiagnosticStderrCapture& operator=(DiagnosticStderrCapture&&) = delete;

    std::string Read() {
        Restore();
        std::string output;
        std::array<char, 1024> buffer{};
        for (;;) {
            const int count = ::_read(read_.Get(), buffer.data(), gsl::narrow_cast<unsigned int>(buffer.size()));
            QIQI_TEST_CHECK(count >= 0);
            if (count == 0) return output;
            output.append(buffer.data(), gsl::narrow_cast<size_t>(count));
        }
    }
private:
    using ScopedDescriptor = qiqi::ScopedHandle<DiagnosticDescriptorTraits>;
    void Restore() noexcept {
        if (!previous_) return;
        QIQI_TEST_CHECK_CLEANUP(std::fflush(stream_) == 0);
        QIQI_TEST_CHECK_CLEANUP(::_dup2(previous_.Get(), descriptor_) == 0);
        previous_.Reset();
    }
    FILE* const stream_;
    const int descriptor_;
    ScopedDescriptor previous_;
    ScopedDescriptor read_;
};

void TestTraceFailureReporting() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    struct Case {
        bool cleanup = false;
        bool retained = false;
        bool retained_badbit = false;
        bool retained_throws = false;
        bool stderr_failure = false;
        bool stderr_throws = false;
    };
    constexpr std::array cases{
        Case{},
        Case{.retained = true},
        Case{.retained = true, .retained_badbit = true},
        Case{.retained = true, .retained_throws = true},
        Case{.cleanup = true, .retained = true, .retained_throws = true},
        Case{.cleanup = true, .retained = true, .stderr_failure = true, .stderr_throws = true},
        Case{.cleanup = true, .retained = true, .retained_throws = true,
             .stderr_failure = true, .stderr_throws = true},
        Case{.cleanup = true, .retained = true, .stderr_failure = true},
        Case{.cleanup = true, .retained = true, .retained_badbit = true}
    };
    for (const auto& test : cases) {
        std::ostringstream output;
        std::ostringstream retained;
        ThrowingDiagnosticBuffer stderr_buffer;
        ThrowingDiagnosticBuffer retained_buffer;
        std::ostream throwing_retained(&retained_buffer);
        throwing_retained.exceptions(std::ios::badbit);
        if (test.retained_badbit) retained.setstate(std::ios::badbit);
        bool threw = false;
        std::string fallback_output;
        {
            DiagnosticStderrCapture capture(stderr);
            {
                auto* const previous_log = qiqi::test::retained_failure_log;
                const auto previous_exceptions = std::cerr.exceptions();
                auto* const previous_buffer = std::cerr.rdbuf(test.stderr_failure
                    ? static_cast<std::streambuf*>(&stderr_buffer) : output.rdbuf());
                const auto restore = gsl::finally([&]() noexcept {
                    qiqi::test::retained_failure_log = previous_log;
                    try {
                        std::cerr.exceptions(std::ios::goodbit);
                        std::cerr.rdbuf(previous_buffer);
                        std::cerr.exceptions(previous_exceptions);
                    } catch (const std::ios_base::failure&) {
                        std::fputs("[FAIL] Restoring diagnostic stderr stream failed.\n", stderr);
                        std::fflush(stderr);
                        std::abort();
                    }
                });
                std::cerr.exceptions(test.stderr_throws ? std::ios::badbit : std::ios::goodbit);
                qiqi::test::retained_failure_log = !test.retained ? nullptr :
                    test.retained_throws ? &throwing_retained : &retained;
                try {
                    if (test.cleanup) {
                        qiqi::test::ReportTestCleanupFailure("trace-test", 7, "expression");
                    } else {
                        qiqi::test::ReportTestFailure("trace-test", 7, "expression");
                    }
                } catch (const DiagnosticStreamFailure&) {
                    threw = true;
                }
            }
            fallback_output = capture.Read();
        }
        QIQI_TEST_CHECK(threw == (!test.cleanup && test.retained_throws));
        QIQI_TEST_CHECK((fallback_output.find("[FAIL] trace-test:7: expression") != std::string::npos) ==
                        test.stderr_failure);
        if (!test.stderr_failure) {
            QIQI_TEST_CHECK(output.str().find("[FAIL] trace-test:7: expression") != std::string::npos);
        } else {
            QIQI_TEST_CHECK(stderr_buffer.writes != 0);
        }
        if (test.retained && !test.retained_badbit && !test.retained_throws) {
            QIQI_TEST_CHECK(retained.str().find("[FAIL] trace-test:7: expression, shift=") == 0 &&
                            retained.str().find(", ctrl=") != std::string::npos &&
                            retained.str().find(", alt=") != std::string::npos &&
                            retained.str().find(", caps=") != std::string::npos);
        }
        if (test.retained_badbit || (test.cleanup && test.retained_throws)) {
            const auto& diagnostic = test.stderr_failure ? fallback_output : output.str();
            QIQI_TEST_CHECK(diagnostic.find("Writing retained test failure log failed.") != std::string::npos);
        }
        if (test.retained_throws) QIQI_TEST_CHECK(retained_buffer.writes != 0);
    }
}

void RunCompositionComponentTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    using qiqi::test::RunTsfTestStage;
    RunTsfTestStage("comp.range", qiqi::test::RunTsfCompositionRangeTests);
    RunTsfTestStage("comp.start", qiqi::test::RunTsfCompositionStartTests);
    RunTsfTestStage("comp.finish", qiqi::test::RunTsfCompositionFinishTests);
    RunTsfTestStage("comp.update", qiqi::test::RunTsfCompositionUpdateTests);
    RunTsfTestStage("comp.state", qiqi::test::RunTsfCompositionStateTests);
    RunTsfTestStage("comp.target", qiqi::test::RunTsfCompositionTargetTests);
    RunTsfTestStage("comp.presentation", qiqi::test::RunTsfCompositionPresentationTests);
    RunTsfTestStage("comp.layout", qiqi::test::RunTsfCompositionLayoutTests);
}

void TestTraceCaseTiming() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    std::ostringstream output;
    std::ostringstream retained;
    {
        auto* const previous_log = qiqi::test::retained_failure_log;
        auto* const previous_buffer = std::cerr.rdbuf(output.rdbuf());
        const auto restore = gsl::finally([&]() noexcept {
            qiqi::test::retained_failure_log = previous_log;
            std::cerr.rdbuf(previous_buffer);
        });
        qiqi::test::retained_failure_log = &retained;
        {
            const qiqi::test::TsfTestCaseTrace outer("timing.outer");
            const qiqi::test::TsfTestCaseTrace inner("timing.inner");
        }
        try {
            const qiqi::test::TsfTestCaseTrace unwind("timing.unwind");
            throw std::runtime_error("test trace unwinding");
        } catch (const std::runtime_error&) {
            QIQI_TEST_CHECK(retained.str().find("[CASE-END] timing.unwind") != std::string::npos);
        }
        qiqi::test::retained_failure_log = nullptr;
        const qiqi::test::TsfTestCaseTrace disabled("timing.disabled");
    }
    const auto text = retained.str();
    QIQI_TEST_CHECK(text == output.str());
    QIQI_TEST_CHECK(text.find("[CASE-BEGIN] timing.outer") < text.find("[CASE-BEGIN] timing.inner"));
    QIQI_TEST_CHECK(text.find("[CASE-END] timing.inner") < text.find("[CASE-END] timing.outer"));
    QIQI_TEST_CHECK(text.find(", unwinding=1") != std::string::npos);
    QIQI_TEST_CHECK(text.find("[CASE-END] timing.unwind") < text.find(", unwinding=1"));
    QIQI_TEST_CHECK(text.find(", elapsed_ms=") != std::string::npos);
    QIQI_TEST_CHECK(text.find("timing.disabled") == std::string::npos);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    QIQI_TEST_CHECK(argv != nullptr);
    const std::span<wchar_t* const> args(argv, gsl::narrow_cast<size_t>(argc));
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) {
            ::CoUninitialize();
        }
    });

    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--restricted-input-diagnostics") {
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        qiqi::test::RunRestrictedInputContractTests();
        qiqi::test::RunRestrictedInputDiagnostics();
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--tsf-focus-diagnostics") {
        qiqi::test::RunTsfFocusDiagnosticsScenario();
        return 0;
    }
    if (args.size() == 4 && std::wstring_view(gsl::at(args, 1)) == L"--etw-host") {
        qiqi::test::RunTsfEtwHostScenario(gsl::at(args, 2), gsl::at(args, 3));
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--etw-integration") {
        return qiqi::test::RunTsfEtwIntegrationTests();
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--focus-metrics-etw") {
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        return qiqi::test::RunFocusMetricsEtwTest();
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--focus-environment-test") {
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        qiqi::test::RunFocusEnvironmentIsolationTest();
        return 0;
    }
    if (args.size() == 4 && std::wstring_view(gsl::at(args, 1)) == L"--focus-benchmark") {
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        return qiqi::test::RunFocusBenchmark(gsl::at(args, 2), gsl::at(args, 3));
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--test-hang-diagnostics-probe") {
        const qiqi::test::TsfTestTrace trace("hang-probe");
        qiqi::test::RunTsfTestStage("watchdog.completion-probe", [] {
            const qiqi::test::TsfTestCaseTrace completed("watchdog.completed-probe");
            ::Sleep(5000);
        });
        qiqi::test::RunTsfTestStage("watchdog.hang-probe", [] { ::Sleep(INFINITE); });
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--candidate-layout-tests") {
        const qiqi::test::TsfTestTrace trace("candidate-layout");
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        qiqi::test::RunTsfCandidateLayoutTests();
        std::cout << "[PASS] QiQi candidate layout tests succeeded.\n";
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--candidate-ui-tests") {
        const qiqi::test::TsfTestTrace trace("candidate-ui");
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        qiqi::test::RunCandidateUITests();
        std::cout << "[PASS] QiQi candidate UI tests succeeded.\n";
        return 0;
    }
    if (args.size() == 2 && std::wstring_view(gsl::at(args, 1)) == L"--composition-lifecycle-tests") {
        const qiqi::test::TsfTestTrace trace("composition-lifecycle");
        qiqi::test::RunTsfTestStage("trace.failure-reporting", TestTraceFailureReporting);
        TestTraceCaseTiming();
        QIQI_TEST_CHECK(SUCCEEDED(co_hr));
        RunCompositionComponentTests();
        qiqi::test::RunTsfServiceTests();
        std::cout << "[PASS] QiQi composition components and service lifecycle tests succeeded.\n";
        return 0;
    }

    const qiqi::test::TsfTestTrace trace("tsf-ui");
    qiqi::test::RunTsfTestStage("trace.failure-reporting", TestTraceFailureReporting);
    TestTraceCaseTiming();
    using qiqi::test::RunTsfTestStage;
    RunTsfTestStage("ui.langbar", qiqi::test::RunLangBarMenuTests);
    RunTsfTestStage("ui.candidate", qiqi::test::RunCandidateUITests);
    RunTsfTestStage("ui.statusbar", qiqi::test::RunStatusBarUITests);
    RunTsfTestStage("tsf.com", qiqi::test::RunTsfComTests);
    RunCompositionComponentTests();
    RunTsfTestStage("tsf.restricted-input", qiqi::test::RunRestrictedInputContractTests);
    qiqi::test::RunTsfServiceTests();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
