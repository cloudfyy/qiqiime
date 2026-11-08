#include "common/test_support.h"
#include "common/metrics.h"
#include "common/logger.h"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace qiqi::test {
namespace {
using namespace metrics;
constexpr GUID kInstance{1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};

Event ValidEvent() noexcept {
    Event event;
    event.instance_id = kInstance;
    event.capture_generation = 1;
    event.service_id = 1;
    event.operation_id = 2;
    event.duration_ns = 0;
    event.outcome = Outcome::Success;
    return event;
}

void TestProtocol() {
    const auto valid = ValidEvent();
    QIQI_TEST_CHECK(Validate(valid) == ValidationError::None);
    auto event = valid;
    event.schema_version = 2;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Version);
    for (int field = 0; field < 4; ++field) {
        event = valid;
        if (field == 0) event.instance_id = GUID_NULL;
        if (field == 1) event.service_id = 0;
        if (field == 2) event.operation_id = 0;
        if (field == 3) event.parent_operation_id = event.operation_id;
        QIQI_TEST_CHECK(Validate(event) == ValidationError::Identity);
    }
    event = valid;
    event.capture_generation = 0;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Generation);
    for (int field = 0; field < 6; ++field) {
        event = valid;
        if (field == 0) event.metric = gsl::narrow_cast<Metric>(255);
        if (field == 1) event.phase = gsl::narrow_cast<Phase>(255);
        if (field == 2) event.outcome = gsl::narrow_cast<Outcome>(255);
        if (field == 3) event.error_domain = gsl::narrow_cast<ErrorDomain>(255);
        if (field == 4) event.mode = gsl::narrow_cast<Mode>(255);
        if (field == 5) event.source = gsl::narrow_cast<Source>(255);
        QIQI_TEST_CHECK(Validate(event) == ValidationError::Enumeration);
    }
    event = valid;
    event.duration_ns.reset();
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Duration);
    event.phase = Phase::Begin;
    event.outcome = Outcome::Unspecified;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::None);
    event.duration_ns = 0;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Duration);
    event = valid;
    event.outcome = Outcome::Unspecified;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Duration);
    event = valid;
    event.outcome = Outcome::Failure;
    event.error_domain = ErrorDomain::HResult;
    event.error_code = gsl::narrow_cast<uint32_t>(E_ACCESSDENIED);
    QIQI_TEST_CHECK(Validate(event) == ValidationError::None);
    event.error_domain = ErrorDomain::None;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Error);
    event.error_domain = ErrorDomain::Win32;
    event.error_code = 0;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Error);
    event = valid;
    event.error_domain = ErrorDomain::Win32;
    event.error_code = ERROR_ACCESS_DENIED;
    QIQI_TEST_CHECK(Validate(event) == ValidationError::Error);
}

void TestClock() {
    uint64_t duration = 99;
    QIQI_TEST_CHECK(ElapsedNanoseconds({10, 10'000'000}, {511, 10'000'000}, duration) == S_OK && duration == 50'100);
    QIQI_TEST_CHECK(ElapsedNanoseconds({10, 3}, {12, 3}, duration) == S_OK && duration == 666'666'666);
    QIQI_TEST_CHECK(ElapsedNanoseconds({10, 3}, {10, 3}, duration) == S_OK && duration == 0);
    duration = 99;
    QIQI_TEST_CHECK(ElapsedNanoseconds({10, 3}, {9, 3}, duration) == E_UNEXPECTED && duration == 99);
    QIQI_TEST_CHECK(ElapsedNanoseconds({10, 3}, {12, 4}, duration) == E_UNEXPECTED && duration == 99);
    QIQI_TEST_CHECK(ElapsedNanoseconds({0, 0}, {0, 0}, duration) == E_UNEXPECTED && duration == 99);
    constexpr uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
    QIQI_TEST_CHECK(ElapsedNanoseconds({0, 1}, {maximum, 1}, duration) ==
        HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) && duration == 99);
    QIQI_TEST_CHECK(ElapsedNanoseconds({0, maximum}, {maximum - 1, maximum}, duration) == S_OK &&
        duration == 999'999'999);
    QIQI_TEST_CHECK(ElapsedNanoseconds({0, maximum}, {maximum / 2, maximum}, duration) == S_OK &&
        duration == 499'999'999);
    ClockReading first{}, second{};
    QIQI_TEST_CHECK(ReadClock(first) == S_OK && ReadClock(second) == S_OK);
    QIQI_TEST_CHECK(ElapsedNanoseconds(first, second, duration) == S_OK);
}

struct ScopeState {
    CaptureState capture{true, 1};
    int clocks = 0;
    int identities = 0;
    int operations = 0;
    int writes = 0;
    HRESULT clock_error = S_OK;
    HRESULT identity_error = S_OK;
    bool change_generation_on_end = false;
    ClockReading first{10, 10'000'000};
    ClockReading last{511, 10'000'000};
    Event event;
};
ScopeState state;
CaptureState FakeCapture() noexcept { return state.capture; }
HRESULT FakeClock(ClockReading& reading) noexcept {
    reading = ++state.clocks == 1 ? state.first : state.last;
    if (state.clocks == 2 && state.change_generation_on_end) ++state.capture.generation;
    ::SetLastError(ERROR_INVALID_DATA);
    return state.clock_error;
}
InstanceIdentity FakeInstance() noexcept {
    ++state.identities;
    return {kInstance, state.identity_error};
}
uint64_t FakeOperation() noexcept { ++state.operations; return 2; }
void FakeEmit(const Event& event) noexcept { ++state.writes; state.event = event; }
const ScopeApi api{&FakeCapture, &FakeClock, &FakeInstance, &FakeOperation, &FakeEmit};

void TestScopes() {
    static_assert(!std::is_copy_constructible_v<Scope> && !std::is_move_constructible_v<Scope>);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered() && !log::MetricsEnabled());
    {
        const Scope disabled(Metric::FocusTotal, 1);
        QIQI_TEST_CHECK(!disabled.Active() && !log::IsEtwProviderRegistered());
    }
    state = {};
    state.capture.enabled = false;
    {
        Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
        QIQI_TEST_CHECK(!scope.Active() && scope.OperationId() == 0);
        scope.SetOutcome(Outcome::Success);
    }
    QIQI_TEST_CHECK(state.clocks == 0 && state.identities == 0 && state.operations == 0 && state.writes == 0);
    for (const auto outcome : {Outcome::Success, Outcome::Failure, Outcome::Cancelled, Outcome::Stale, Outcome::Incomplete}) {
        state = {};
        ::SetLastError(ERROR_NOT_READY);
        {
            Scope scope(Metric::FocusTotal, 1, Mode::FocusGained, Source::ThreadManager, 7, api);
            QIQI_TEST_CHECK(scope.Active() && scope.OperationId() == 2 && ::GetLastError() == ERROR_NOT_READY);
            if (outcome != Outcome::Incomplete) {
                scope.SetOutcome(outcome, outcome == Outcome::Failure ? ErrorDomain::HResult : ErrorDomain::None,
                    outcome == Outcome::Failure ? gsl::narrow_cast<uint32_t>(E_ACCESSDENIED) : 0);
            }
            scope.SetActivity(kInstance, log::kProviderId);
        }
        QIQI_TEST_CHECK(::GetLastError() == ERROR_NOT_READY && state.writes == 1 && state.clocks == 2);
        QIQI_TEST_CHECK(state.event.outcome == outcome && state.event.duration_ns == 50'100);
        QIQI_TEST_CHECK(state.event.parent_operation_id == 7 && state.event.source == Source::ThreadManager);
        QIQI_TEST_CHECK(state.event.activity_id == kInstance && state.event.related_activity_id == log::kProviderId);
        QIQI_TEST_CHECK(Validate(state.event) == ValidationError::None);
    }
    for (const auto prior : {Outcome::Success, Outcome::Failure}) {
        state = {};
        try {
            Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
            scope.SetOutcome(prior, prior == Outcome::Failure ? ErrorDomain::HResult : ErrorDomain::None,
                prior == Outcome::Failure ? gsl::narrow_cast<uint32_t>(E_ACCESSDENIED) : 0);
            throw std::runtime_error("test exception");
        } catch (const std::runtime_error&) {
            QIQI_TEST_CHECK(state.writes == 1 && state.event.outcome == Outcome::Exception);
            QIQI_TEST_CHECK(state.event.error_code ==
                (prior == Outcome::Failure ? gsl::narrow_cast<uint32_t>(E_ACCESSDENIED) : 0));
        }
    }
    for (const bool restart : {false, true}) {
        state = {};
        const auto before = Diagnostics().interrupted;
        {
            const Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
            if (restart) ++state.capture.generation;
            else state.capture.enabled = false;
        }
        QIQI_TEST_CHECK(state.clocks == 1 && state.writes == 0 && Diagnostics().interrupted == before + 1);
    }
    for (const bool at_start : {false, true}) {
        state = {};
        const auto before = Diagnostics().clock;
        if (at_start) state.clock_error = E_ACCESSDENIED;
        {
            const Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
            state.clock_error = E_ACCESSDENIED;
        }
        state = {};
        state.change_generation_on_end = true;
        const auto interrupted_before = Diagnostics().interrupted;
        {
            const Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
        }
        QIQI_TEST_CHECK(state.clocks == 2 && state.writes == 0 && Diagnostics().interrupted == interrupted_before + 1);
        QIQI_TEST_CHECK(state.writes == 0 && Diagnostics().clock == before + 1);
    }
    state = {};
    state.last.ticks = state.first.ticks - 1;
    const auto clock_before = Diagnostics().clock;
    {
        const Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
    }
    QIQI_TEST_CHECK(state.writes == 0 && Diagnostics().clock == clock_before + 1);
    state = {};
    const auto protocol_before = Diagnostics().protocol;
    {
        Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
        scope.SetOutcome(gsl::narrow_cast<Outcome>(255));
    }
    QIQI_TEST_CHECK(state.writes == 0 && Diagnostics().protocol == protocol_before + 1);
    state = {};
    state.identity_error = E_ACCESSDENIED;
    const auto identity_before = Diagnostics().identity;
    {
        const Scope scope(Metric::FocusTotal, 1, Mode::Unspecified, Source::Unspecified, 0, api);
    }
    QIQI_TEST_CHECK(state.clocks == 0 && state.writes == 0 && Diagnostics().identity == identity_before + 1);
    QIQI_TEST_CHECK(NextServiceId() != NextServiceId() && NextOperationId() != NextOperationId());
}
} // namespace

void RunMetricsTests() {
    TestProtocol();
    TestClock();
    TestScopes();
}
int RunMetricsEtwTest();
} // namespace qiqi::test

int main(int argc, char** argv) {
    const std::span args(argv, gsl::narrow_cast<size_t>(argc));
    if (argc == 2 && std::string_view(gsl::at(args, 1)) == "--etw") return qiqi::test::RunMetricsEtwTest();
    if (argc != 1) {
        std::cerr << "Usage: qiqi_metrics_test [--etw]\n";
        return 2;
    }
    qiqi::test::RunMetricsTests();
    std::cout << "[PASS] Typed metrics, clocks and scope tests.\n";
    return 0;
}
