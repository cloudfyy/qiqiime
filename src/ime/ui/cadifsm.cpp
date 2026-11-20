// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/cadifsm.h"
#include "common/logger.h"
#include <gsl/gsl>
#include <new>
#include <tuple>

namespace qiqi {
namespace {

HRESULT EndUiElementNoThrow(
    gsl::not_null<ITfUIElementMgr*> manager, DWORD element_id) noexcept {
    try {
        return manager->EndUIElement(element_id);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_UNEXPECTED;
    }
}

} // namespace

bool CandidateUiElementLifecycle::Transition(
    detail::CandidateUiLifecycleEvent event) noexcept {
    const auto next = detail::LookupCandidateUiLifecycleTransition(
        state_, event);
    if (!next) return false;
    state_ = *next;
    ++generation_;
    return true;
}

DWORD CandidateUiElementLifecycle::ActiveId() const noexcept {
    return active_ ? active_->id : TF_INVALID_UIELEMENTID;
}

ITfUIElementMgr* CandidateUiElementLifecycle::ActiveManager() const noexcept {
    return active_ ? active_->manager.Get() : nullptr;
}

void CandidateUiElementLifecycle::BindActiveManager(ITfUIElementMgr* manager) noexcept {
    if (active_ && !active_->manager && manager) {
        active_->manager = manager;
        ++generation_;
    }
}

void CandidateUiElementLifecycle::SetActiveRegistrationForTest(
    DWORD id, ITfUIElementMgr* manager) noexcept {
    active_.reset();
    if (id != TF_INVALID_UIELEMENTID) {
        active_ = Registration{.id = id, .manager = manager};
        if (!Transition(
                detail::CandidateUiLifecycleEvent::TestRegistrationSet)) {
            ++generation_;
        }
        return;
    }
    if (state_ == CandidateUiLifecycleState::Active && !retiring_) {
        std::ignore = Transition(
            detail::CandidateUiLifecycleEvent::TestRegistrationCleared);
    } else {
        ++generation_;
    }
}

std::optional<uint64_t>
CandidateUiElementLifecycle::TryBeginRegistration() noexcept {
    if (active_ || retiring_ ||
        !Transition(detail::CandidateUiLifecycleEvent::BeginStarted)) {
        return std::nullopt;
    }
    return generation_;
}

bool CandidateUiElementLifecycle::IsBeginRegistrationCurrent(
    uint64_t token) const noexcept {
    return state_ == CandidateUiLifecycleState::Beginning &&
           generation_ == token && !active_ && !retiring_;
}

bool CandidateUiElementLifecycle::PublishRegistration(
    uint64_t token, DWORD id, ITfUIElementMgr* manager) noexcept {
    if (!IsBeginRegistrationCurrent(token) ||
        id == TF_INVALID_UIELEMENTID || !manager) {
        return false;
    }
    active_ = Registration{.id = id, .manager = manager};
    return Transition(detail::CandidateUiLifecycleEvent::BeginSucceeded);
}

void CandidateUiElementLifecycle::AbortBeginRegistration(uint64_t token) noexcept {
    if (IsBeginRegistrationCurrent(token)) {
        std::ignore = Transition(detail::CandidateUiLifecycleEvent::BeginAborted);
    }
}

HRESULT CandidateUiElementLifecycle::QueueRegistration(
    DWORD id, ITfUIElementMgr* manager) noexcept {
    if (id == TF_INVALID_UIELEMENTID) return E_INVALIDARG;
    if (!manager) return E_POINTER;
    if (retiring_) {
        return retiring_->id == id && retiring_->manager.Get() == manager
            ? S_FALSE
            : E_UNEXPECTED;
    }
    retiring_ = Registration{.id = id, .manager = manager};
    return S_OK;
}

HRESULT CandidateUiElementLifecycle::RetireRegistration(
    DWORD id, ITfUIElementMgr* manager, std::string_view operation) noexcept {
    const HRESULT queued = QueueRegistration(id, manager);
    if (queued == S_FALSE) {
        return RetryRetirements(operation);
    }
    if (FAILED(queued)) return queued;
    if (state_ != CandidateUiLifecycleState::Ending) {
        if (!Transition(detail::CandidateUiLifecycleEvent::RegistrationQueued)) {
            return E_UNEXPECTED;
        }
    } else {
        ++generation_;
    }
    return DriveRetirements(operation);
}

HRESULT CandidateUiElementLifecycle::RetireActiveRegistration(
    std::string_view operation) noexcept {
    if (state_ == CandidateUiLifecycleState::Retiring) {
        return DriveRetirements(operation);
    }
    if (!active_) return DriveRetirements(operation);
    if (state_ != CandidateUiLifecycleState::Active) return E_PENDING;
    if (!active_->manager) return E_NOINTERFACE;
    const HRESULT queued =
        QueueRegistration(active_->id, active_->manager.Get());
    if (FAILED(queued)) return queued;
    if (queued == S_FALSE) return E_UNEXPECTED;
    active_.reset();
    if (!Transition(detail::CandidateUiLifecycleEvent::RegistrationQueued)) {
        return E_UNEXPECTED;
    }
    return DriveRetirements(operation);
}

HRESULT CandidateUiElementLifecycle::RetryRetirements(
    std::string_view operation) noexcept {
    if (state_ == CandidateUiLifecycleState::Beginning ||
        state_ == CandidateUiLifecycleState::Updating ||
        state_ == CandidateUiLifecycleState::Ending) {
        return E_PENDING;
    }
    if (state_ != CandidateUiLifecycleState::Retiring) return S_OK;
    return DriveRetirements(operation);
}

HRESULT CandidateUiElementLifecycle::EndRegistrations(
    std::string_view operation) noexcept {
    if (state_ == CandidateUiLifecycleState::Beginning ||
        state_ == CandidateUiLifecycleState::Updating ||
        state_ == CandidateUiLifecycleState::Ending) {
        return E_PENDING;
    }
    HRESULT result = state_ == CandidateUiLifecycleState::Retiring
        ? DriveRetirements(operation)
        : RetireActiveRegistration(operation);
    if (FAILED(result) && state_ == CandidateUiLifecycleState::Retiring) {
        result = DriveRetirements(operation);
    }
    if (SUCCEEDED(result) &&
        state_ == CandidateUiLifecycleState::Active &&
        active_) {
        result = RetireActiveRegistration(operation);
        if (FAILED(result) &&
            state_ == CandidateUiLifecycleState::Retiring) {
            result = DriveRetirements(operation);
        }
    }
    return result;
}

HRESULT CandidateUiElementLifecycle::DriveRetirements(
    std::string_view operation) noexcept {
    if (!retiring_) return S_OK;
    if (state_ == CandidateUiLifecycleState::Ending) return E_PENDING;
    if (!Transition(detail::CandidateUiLifecycleEvent::RetirementStarted)) {
        return E_UNEXPECTED;
    }
    const DWORD retirement_id = retiring_->id;
    auto* const raw_retirement_manager = retiring_->manager.Get();
    if (!raw_retirement_manager) {
        std::ignore = Transition(
            detail::CandidateUiLifecycleEvent::RetirementFailed);
        return E_NOINTERFACE;
    }
    const HRESULT result = EndUiElementNoThrow(
        gsl::not_null<ITfUIElementMgr*>(raw_retirement_manager),
        retirement_id);
    if (!log::CheckHResult(result, operation)) {
        std::ignore = Transition(
            detail::CandidateUiLifecycleEvent::RetirementFailed);
        return result;
    }
    retiring_.reset();
    const auto succeeded = active_
        ? detail::CandidateUiLifecycleEvent::RetirementSucceededActive
        : detail::CandidateUiLifecycleEvent::RetirementSucceededIdle;
    if (!Transition(succeeded)) return E_UNEXPECTED;
    return S_OK;
}

std::optional<CandidateUiElementLifecycle::UpdateToken>
CandidateUiElementLifecycle::TryBeginUpdate() noexcept {
    if (!active_ ||
        !Transition(detail::CandidateUiLifecycleEvent::UpdateStarted)) {
        return std::nullopt;
    }
    return UpdateToken{
        .element_id = active_->id,
        .generation = generation_,
    };
}

bool CandidateUiElementLifecycle::IsUpdateCurrent(
    const UpdateToken& token) const noexcept {
    return state_ == CandidateUiLifecycleState::Updating &&
           generation_ == token.generation &&
           ActiveId() == token.element_id;
}

bool CandidateUiElementLifecycle::CompleteUpdate(
    const UpdateToken& token) noexcept {
    if (state_ != CandidateUiLifecycleState::Updating) return false;
    const bool current = IsUpdateCurrent(token);
    if (!Transition(detail::CandidateUiLifecycleEvent::UpdateCompleted)) {
        return false;
    }
    return current;
}

void CandidateUiElementLifecycle::MarkCandidateUpdate(
    DWORD changed_flags) noexcept {
    if (!active_) {
        updated_flags_ = kAllCandidateUpdateFlags;
    } else {
        updated_flags_ |= changed_flags;
    }
    if (updated_flags_ != 0) ++update_revision_;
}

void CandidateUiElementLifecycle::PrepareInitialUpdate() noexcept {
    if (updated_flags_ != kAllCandidateUpdateFlags) {
        updated_flags_ = kAllCandidateUpdateFlags;
        ++update_revision_;
    }
}

void CandidateUiElementLifecycle::AcknowledgeUpdate(
    uint64_t revision) noexcept {
    if (revision == update_revision_) updated_flags_ = 0;
}

void CandidateUiElementLifecycle::ClearDocumentManagerIfIdle() noexcept {
    if (state_ == CandidateUiLifecycleState::Idle &&
        !active_ && !retiring_) {
        document_manager_ = nullptr;
    }
}

} // namespace qiqi
