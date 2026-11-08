#include "common/metric.h"
#include "common/logger.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace qiqi::metrics {
namespace {
std::array<std::atomic<uint64_t>, 4> failures{};
std::atomic<uint64_t> interrupted{0};
std::atomic<uint64_t> service_sequence{0};
std::atomic<uint64_t> operation_sequence{0};
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000;

uint64_t FractionalNanoseconds(uint64_t remainder, uint64_t frequency) noexcept {
    if (remainder <= (std::numeric_limits<uint64_t>::max)() / kNanosecondsPerSecond) {
        return remainder * kNanosecondsPerSecond / frequency;
    }
    // Long division keeps every remainder below frequency without requiring x64-only 128-bit arithmetic.
    uint64_t quotient = 0;
    uint64_t rest = 0;
    for (int bit = gsl::narrow_cast<int>(std::bit_width(kNanosecondsPerSecond)) - 1; bit >= 0; --bit) {
        quotient *= 2;
        if (rest >= frequency - rest) {
            rest -= frequency - rest;
            ++quotient;
        } else {
            rest *= 2;
        }
        if ((kNanosecondsPerSecond >> bit) & 1) {
            if (rest >= frequency - remainder) {
                rest -= frequency - remainder;
                ++quotient;
            } else {
                rest += remainder;
            }
        }
    }
    return quotient;
}

uint64_t AllocateId(std::atomic<uint64_t>& sequence) noexcept {
    auto previous = sequence.load(std::memory_order_relaxed);
    while (previous != (std::numeric_limits<uint64_t>::max)()) {
        if (sequence.compare_exchange_weak(previous, previous + 1, std::memory_order_relaxed)) return previous + 1;
    }
    RecordFailure(FailureKind::Identity, HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    return 0;
}

} // namespace

ValidationError Validate(const Event& event) noexcept {
    if (event.schema_version != kSchemaVersion) return ValidationError::Version;
    if (event.instance_id == GUID_NULL || event.service_id == 0 || event.operation_id == 0 ||
        event.parent_operation_id == event.operation_id) return ValidationError::Identity;
    if (event.capture_generation == 0) return ValidationError::Generation;
    if (event.metric < Metric::FocusTotal || event.metric > Metric::Cleanup ||
        event.phase > Phase::Complete || event.outcome > Outcome::Rejected ||
        event.error_domain > ErrorDomain::Win32 || event.mode > Mode::FocusLost ||
        event.source > Source::KeyEvent) return ValidationError::Enumeration;
    if (event.phase == Phase::Begin) {
        if (event.duration_ns || event.outcome != Outcome::Unspecified) return ValidationError::Duration;
        if (event.error_domain != ErrorDomain::None || event.error_code != 0) return ValidationError::Error;
    } else if (!event.duration_ns || event.outcome == Outcome::Unspecified) {
        return ValidationError::Duration;
    }
    if ((event.error_domain == ErrorDomain::None && event.error_code != 0) ||
        (event.error_domain != ErrorDomain::None && event.error_code == 0) ||
        (event.outcome == Outcome::Success && (event.error_domain != ErrorDomain::None || event.error_code != 0))) {
        return ValidationError::Error;
    }
    return ValidationError::None;
}

HRESULT ReadClock(ClockReading& reading) noexcept {
    // These APIs do not document an extended error for a failed query.
    LARGE_INTEGER frequency{};
    if (!::QueryPerformanceFrequency(&frequency)) return E_FAIL;
    LARGE_INTEGER ticks{};
    if (!::QueryPerformanceCounter(&ticks)) return E_FAIL;
    if (frequency.QuadPart <= 0 || ticks.QuadPart < 0) return E_UNEXPECTED;
    reading = {gsl::narrow_cast<uint64_t>(ticks.QuadPart), gsl::narrow_cast<uint64_t>(frequency.QuadPart)};
    return S_OK;
}

HRESULT ElapsedNanoseconds(ClockReading start, ClockReading end, uint64_t& duration_ns) noexcept {
    if (start.frequency == 0 || start.frequency != end.frequency || end.ticks < start.ticks) return E_UNEXPECTED;
    constexpr uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
    const uint64_t elapsed = end.ticks - start.ticks;
    const uint64_t seconds = elapsed / start.frequency;
    const uint64_t remainder = elapsed % start.frequency;
    if (seconds > maximum / kNanosecondsPerSecond) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
    const uint64_t fractional = FractionalNanoseconds(remainder, start.frequency);
    const uint64_t whole = seconds * kNanosecondsPerSecond;
    if (whole > maximum - fractional) return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    duration_ns = whole + fractional;
    return S_OK;
}

InstanceIdentity ModuleInstance() noexcept {
    static const InstanceIdentity identity = []() noexcept {
        GUID id{};
        const ULONG error = log::CreateActivityIdResult(id);
        return InstanceIdentity{id, error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) :
            (id != GUID_NULL ? S_OK : E_UNEXPECTED)};
    }();
    return identity;
}

uint64_t NextServiceId() noexcept { return AllocateId(service_sequence); }
uint64_t NextOperationId() noexcept { return AllocateId(operation_sequence); }

DiagnosticCounts Diagnostics() noexcept {
    return {gsl::at(failures, 0).load(), gsl::at(failures, 1).load(),
        gsl::at(failures, 2).load(), gsl::at(failures, 3).load(), interrupted.load()};
}

void RecordFailure(FailureKind kind, HRESULT error) noexcept {
    const DWORD saved_error = ::GetLastError();
    const auto restore = gsl::finally([saved_error]() noexcept { ::SetLastError(saved_error); });
    ++gsl::at(failures, std::to_underlying(kind));
    std::array<char, 128> message{};
    std::snprintf(message.data(), message.size(), "QiQiIME: metric failure (kind=%u, hr=0x%08lX)\n",
        gsl::narrow_cast<unsigned>(std::to_underlying(kind)), gsl::narrow_cast<unsigned long>(error));
    ::OutputDebugStringA(message.data());
}

void RecordInterrupted() noexcept { ++interrupted; }

CaptureState ReadCaptureState() noexcept { return {log::MetricsEnabled(), log::MetricsCaptureGeneration()}; }
void Emit(const Event& event) noexcept { log::WriteMetric(event); }

Scope::Scope(Metric metric, uint64_t service_id, Mode mode, Source source,
    uint64_t parent_operation_id, ScopeApi api) noexcept : api_(api) {
#ifdef QIQI_METRICS_BENCHMARK_BASELINE
    constexpr CaptureState capture{};
#else
    const CaptureState capture = api_.capture();
#endif
    if (!capture.enabled) return;
    const DWORD saved_error = ::GetLastError();
    const auto restore = gsl::finally([saved_error]() noexcept { ::SetLastError(saved_error); });
    const auto identity = api_.instance();
    if (FAILED(identity.error)) {
        RecordFailure(FailureKind::Identity, identity.error);
        return;
    }
    event_.instance_id = identity.id;
    event_.capture_generation = capture.generation;
    event_.service_id = service_id;
    event_.operation_id = api_.operation();
    event_.parent_operation_id = parent_operation_id;
    event_.metric = metric;
    event_.mode = mode;
    event_.source = source;
    event_.duration_ns = 0;
    if (Validate(event_) != ValidationError::None) {
        RecordFailure(FailureKind::Protocol, E_INVALIDARG);
        return;
    }
    const HRESULT error = api_.clock(start_);
    if (FAILED(error) || start_.frequency == 0) {
        RecordFailure(FailureKind::Clock, FAILED(error) ? error : E_UNEXPECTED);
        return;
    }
    exceptions_ = std::uncaught_exceptions();
    active_ = true;
}

Scope::~Scope() noexcept {
    if (!active_) return;
    const DWORD saved_error = ::GetLastError();
    const auto restore = gsl::finally([saved_error]() noexcept { ::SetLastError(saved_error); });
    if (!CaptureUnchanged()) {
        RecordInterrupted();
        return;
    }
    ClockReading end{};
    const HRESULT read_error = api_.clock(end);
    uint64_t duration = 0;
    const HRESULT error = FAILED(read_error) ? read_error : ElapsedNanoseconds(start_, end, duration);
    if (FAILED(error)) {
        RecordFailure(FailureKind::Clock, error);
        return;
    }
    if (!CaptureUnchanged()) {
        RecordInterrupted();
        return;
    }
    event_.duration_ns = duration;
    if (std::uncaught_exceptions() > exceptions_) {
        event_.outcome = Outcome::Exception;
    }
    api_.emit(event_);
}

bool Scope::CaptureUnchanged() const noexcept {
    const CaptureState capture = api_.capture();
    return capture.enabled && capture.generation == event_.capture_generation;
}

void Scope::SetOutcome(Outcome outcome, ErrorDomain domain, uint32_t code) noexcept {
    if (!active_) return;
    event_.outcome = outcome;
    event_.error_domain = domain;
    event_.error_code = code;
    if (Validate(event_) != ValidationError::None) {
        RecordFailure(FailureKind::Protocol, E_INVALIDARG);
        active_ = false;
    }
}

void Scope::SetActivity(GUID activity, GUID related) noexcept {
    if (!active_) return;
    event_.activity_id = activity;
    event_.related_activity_id = related;
}

} // namespace qiqi::metrics
