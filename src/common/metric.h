#pragma once

#include <windows.h>
#include <cguid.h>
#include <gsl/gsl>
#include <cstdint>
#include <optional>

namespace qiqi::metrics {

inline constexpr uint16_t kSchemaVersion = 1;

enum class Metric : uint8_t {
    FocusTotal = 1, FocusReload, FocusPosition, KeyCallback, CandidateQuery, CandidateUpdate, Commit, Cleanup
};
enum class Phase : uint8_t { Begin, End, Complete };
enum class Outcome : uint8_t { Unspecified, Success, Failure, Cancelled, Stale, Incomplete, Exception, Unhandled, Rejected };
enum class ErrorDomain : uint8_t { None, HResult, Win32 };
enum class Mode : uint8_t { Unspecified, Popup, Uiless, FocusGained, FocusLost };
enum class Source : uint8_t { Unspecified, ThreadManager, KeyEvent };

struct Event {
    uint16_t schema_version = kSchemaVersion;
    GUID instance_id{};
    GUID activity_id{};
    GUID related_activity_id{};
    uint64_t capture_generation = 0;
    uint64_t service_id = 0;
    uint64_t operation_id = 0;
    uint64_t parent_operation_id = 0;
    Metric metric = Metric::FocusTotal;
    Phase phase = Phase::Complete;
    Outcome outcome = Outcome::Incomplete;
    std::optional<uint64_t> duration_ns;
    ErrorDomain error_domain = ErrorDomain::None;
    uint32_t error_code = 0;
    Mode mode = Mode::Unspecified;
    Source source = Source::Unspecified;
};

enum class ValidationError : uint8_t { None, Version, Identity, Generation, Enumeration, Duration, Error };
ValidationError Validate(const Event& event) noexcept;

struct ClockReading {
    uint64_t ticks = 0;
    uint64_t frequency = 0;
};

HRESULT ReadClock(ClockReading& reading) noexcept;
// Integer conversion is checked; failed conversion leaves duration_ns unchanged.
HRESULT ElapsedNanoseconds(ClockReading start, ClockReading end, uint64_t& duration_ns) noexcept;

struct InstanceIdentity {
    GUID id{};
    HRESULT error = S_OK;
};

InstanceIdentity ModuleInstance() noexcept;
uint64_t NextServiceId() noexcept;
uint64_t NextOperationId() noexcept;

enum class FailureKind : uint8_t { Protocol, Clock, Identity, Write };
struct DiagnosticCounts {
    uint64_t protocol = 0;
    uint64_t clock = 0;
    uint64_t identity = 0;
    uint64_t write = 0;
    uint64_t interrupted = 0;
};
DiagnosticCounts Diagnostics() noexcept;
void RecordFailure(FailureKind kind, HRESULT error) noexcept;
void RecordInterrupted() noexcept;

struct CaptureState {
    bool enabled = false;
    uint64_t generation = 0;
};
CaptureState ReadCaptureState() noexcept;
void Emit(const Event& event) noexcept;

struct ScopeApi {
    gsl::not_null<decltype(&ReadCaptureState)> capture = &ReadCaptureState;
    gsl::not_null<decltype(&ReadClock)> clock = &ReadClock;
    gsl::not_null<decltype(&ModuleInstance)> instance = &ModuleInstance;
    gsl::not_null<decltype(&NextOperationId)> operation = &NextOperationId;
    gsl::not_null<decltype(&Emit)> emit = &Emit;
};

class Scope final {
public:
    Scope(Metric metric, uint64_t service_id, Mode mode = Mode::Unspecified,
        Source source = Source::Unspecified, uint64_t parent_operation_id = 0, ScopeApi api = {}) noexcept;
    ~Scope() noexcept;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
    void SetOutcome(Outcome outcome, ErrorDomain domain = ErrorDomain::None, uint32_t code = 0) noexcept;
    void SetActivity(GUID activity, GUID related = GUID_NULL) noexcept;
    bool Active() const noexcept { return active_; }
    uint64_t OperationId() const noexcept { return active_ ? event_.operation_id : 0; }
private:
    bool CaptureUnchanged() const noexcept;
    ScopeApi api_;
    Event event_;
    ClockReading start_;
    int exceptions_ = 0;
    bool active_ = false;
};

} // namespace qiqi::metrics
