#include "common/test_support.h"
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

namespace qiqi::test {
void RunRestrictedInputContractTests();
void RunRestrictedInputDiagnostics();
int RunFocusMetricsEtwTest();
int RunFocusBenchmark(const std::filesystem::path& dll, std::wstring_view mode);
}

namespace {

void TestTraceFailureReporting() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    for (const int destination : {0, 1, 2}) {
        std::ostringstream output;
        std::ostringstream retained;
        if (destination == 2) retained.setstate(std::ios::badbit);
        {
            auto* const previous_log = qiqi::test::retained_failure_log;
            auto* const previous_buffer = std::cerr.rdbuf(output.rdbuf());
            const auto restore = gsl::finally([&]() noexcept {
                qiqi::test::retained_failure_log = previous_log;
                std::cerr.rdbuf(previous_buffer);
            });
            qiqi::test::retained_failure_log = destination == 0 ? nullptr : &retained;
            qiqi::test::ReportTestFailure("trace-test", 7, "expression");
        }
        QIQI_TEST_CHECK(output.str().find("[FAIL] trace-test:7: expression") != std::string::npos);
        if (destination == 1) {
            QIQI_TEST_CHECK(retained.str().find("[FAIL] trace-test:7: expression, shift=") == 0 &&
                            retained.str().find(", ctrl=") != std::string::npos &&
                            retained.str().find(", alt=") != std::string::npos &&
                            retained.str().find(", caps=") != std::string::npos);
        }
        if (destination == 2) {
            QIQI_TEST_CHECK(output.str().find("Writing retained test failure log failed.") != std::string::npos);
        }
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
