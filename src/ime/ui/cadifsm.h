// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <msctf.h>
#include <wrl/client.h>
#include <cstdint>
#include <optional>
#include <string_view>
#include "ime/fsm/table.h"

namespace qiqi {

enum class CandidateUiLifecycleState : uint8_t {
    Idle,
    Beginning,
    Active,
    Updating,
    Retiring,
    Ending,
    Count
};

namespace detail {
enum class CandidateUiLifecycleEvent : uint8_t {
    BeginStarted,
    BeginSucceeded,
    BeginAborted,
    UpdateStarted,
    UpdateCompleted,
    RegistrationQueued,
    RetirementStarted,
    RetirementFailed,
    RetirementSucceededActive,
    RetirementSucceededIdle,
    TestRegistrationSet,
    TestRegistrationCleared,
    Count
};

using CandidateUiLifecycleTransitions =
    FsmTable<CandidateUiLifecycleState, CandidateUiLifecycleEvent,
             CandidateUiLifecycleState>;

constexpr CandidateUiLifecycleTransitions BuildCandidateUiLifecycleTransitions() noexcept {
    CandidateUiLifecycleTransitions transitions =
        MakeFsmTable<CandidateUiLifecycleState, CandidateUiLifecycleEvent>(
            CandidateUiLifecycleState::Count);
    using State = CandidateUiLifecycleState;
    using Event = CandidateUiLifecycleEvent;
    SetFsmTransition(transitions, State::Idle, Event::BeginStarted, State::Beginning);
    SetFsmTransition(transitions, State::Idle, Event::RegistrationQueued, State::Retiring);
    SetFsmTransition(transitions, State::Idle, Event::TestRegistrationSet, State::Active);
    SetFsmTransition(transitions, State::Beginning, Event::BeginSucceeded, State::Active);
    SetFsmTransition(transitions, State::Beginning, Event::BeginAborted, State::Idle);
    SetFsmTransition(transitions, State::Beginning, Event::RegistrationQueued, State::Retiring);
    SetFsmTransition(transitions, State::Beginning, Event::TestRegistrationSet, State::Active);
    SetFsmTransition(transitions, State::Active, Event::UpdateStarted, State::Updating);
    SetFsmTransition(transitions, State::Active, Event::RegistrationQueued, State::Retiring);
    SetFsmTransition(transitions, State::Active, Event::TestRegistrationCleared, State::Idle);
    SetFsmTransition(transitions, State::Updating, Event::UpdateCompleted, State::Active);
    SetFsmTransition(transitions, State::Retiring, Event::RetirementStarted, State::Ending);
    SetFsmTransition(transitions, State::Ending, Event::RetirementFailed, State::Retiring);
    SetFsmTransition(transitions, State::Ending, Event::RetirementSucceededActive, State::Active);
    SetFsmTransition(transitions, State::Ending, Event::RetirementSucceededIdle, State::Idle);
    return transitions;
}

inline constexpr auto kCandidateUiLifecycleTransitions =
    BuildCandidateUiLifecycleTransitions();

constexpr std::optional<CandidateUiLifecycleState>
LookupCandidateUiLifecycleTransition(
    CandidateUiLifecycleState state,
    CandidateUiLifecycleEvent event) noexcept {
    const auto next = LookupFsmTransition(
        kCandidateUiLifecycleTransitions, state, event);
    if (!next || *next == CandidateUiLifecycleState::Count) {
        return std::nullopt;
    }
    return next;
}

static_assert(
    LookupCandidateUiLifecycleTransition(
        CandidateUiLifecycleState::Idle,
        CandidateUiLifecycleEvent::BeginStarted) ==
    CandidateUiLifecycleState::Beginning);
static_assert(
    !LookupCandidateUiLifecycleTransition(
        CandidateUiLifecycleState::Idle,
        CandidateUiLifecycleEvent::UpdateStarted));
static_assert(
    !LookupCandidateUiLifecycleTransition(
        CandidateUiLifecycleState::Count,
        CandidateUiLifecycleEvent::BeginStarted));
}

class CandidateUiElementLifecycle final {
public:
    static constexpr DWORD kAllCandidateUpdateFlags =
        TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_SELECTION |
        TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;

    struct UpdateToken {
        DWORD element_id = TF_INVALID_UIELEMENTID;
        uint64_t generation = 0;
    };

    CandidateUiElementLifecycle() = default;
    CandidateUiElementLifecycle(const CandidateUiElementLifecycle&) = delete;
    CandidateUiElementLifecycle& operator=(const CandidateUiElementLifecycle&) = delete;
    CandidateUiElementLifecycle(CandidateUiElementLifecycle&&) = delete;
    CandidateUiElementLifecycle& operator=(CandidateUiElementLifecycle&&) = delete;

    [[nodiscard]] CandidateUiLifecycleState State() const noexcept { return state_; }
    [[nodiscard]] bool HasActiveRegistration() const noexcept { return active_.has_value(); }
    [[nodiscard]] bool HasPendingRetirements() const noexcept { return retiring_.has_value(); }
    [[nodiscard]] DWORD ActiveId() const noexcept;
    [[nodiscard]] ITfUIElementMgr* ActiveManager() const noexcept;
    void BindActiveManager(ITfUIElementMgr* manager) noexcept;
    void SetActiveRegistrationForTest(
        DWORD id, ITfUIElementMgr* manager = nullptr) noexcept;

    [[nodiscard]] std::optional<uint64_t> TryBeginRegistration() noexcept;
    [[nodiscard]] bool IsBeginRegistrationCurrent(uint64_t token) const noexcept;
    bool PublishRegistration(
        uint64_t token, DWORD id, ITfUIElementMgr* manager) noexcept;
    void AbortBeginRegistration(uint64_t token) noexcept;
    HRESULT RetireRegistration(
        DWORD id, ITfUIElementMgr* manager, std::string_view operation) noexcept;
    HRESULT RetireActiveRegistration(std::string_view operation) noexcept;
    HRESULT RetryRetirements(std::string_view operation) noexcept;
    HRESULT EndRegistrations(std::string_view operation) noexcept;

    [[nodiscard]] std::optional<UpdateToken> TryBeginUpdate() noexcept;
    [[nodiscard]] bool IsUpdateCurrent(const UpdateToken& token) const noexcept;
    bool CompleteUpdate(const UpdateToken& token) noexcept;
    void MarkCandidateUpdate(DWORD changed_flags) noexcept;
    void PrepareInitialUpdate() noexcept;
    [[nodiscard]] DWORD UpdatedFlags() const noexcept { return updated_flags_; }
    [[nodiscard]] uint64_t UpdateRevision() const noexcept { return update_revision_; }
    void AcknowledgeUpdate(uint64_t revision) noexcept;

    [[nodiscard]] ITfDocumentMgr* DocumentManager() const noexcept {
        return document_manager_.Get();
    }
    void SetDocumentManager(ITfDocumentMgr* document_manager) noexcept {
        document_manager_ = document_manager;
    }
    void ClearDocumentManager() noexcept { document_manager_ = nullptr; }
    void ClearDocumentManagerIfIdle() noexcept;

private:
    struct Registration {
        DWORD id = TF_INVALID_UIELEMENTID;
        Microsoft::WRL::ComPtr<ITfUIElementMgr> manager;
    };

    bool Transition(detail::CandidateUiLifecycleEvent event) noexcept;
    HRESULT QueueRegistration(DWORD id, ITfUIElementMgr* manager) noexcept;
    HRESULT DriveRetirements(std::string_view operation) noexcept;

    CandidateUiLifecycleState state_ = CandidateUiLifecycleState::Idle;
    std::optional<Registration> active_;
    std::optional<Registration> retiring_;
    Microsoft::WRL::ComPtr<ITfDocumentMgr> document_manager_;
    uint64_t generation_ = 0;
    uint64_t update_revision_ = 0;
    DWORD updated_flags_ = kAllCandidateUpdateFlags;
};

} // namespace qiqi
