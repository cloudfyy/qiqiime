// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/logger.h"
#include "ime/comp/state.h"
#include "ime/engine/engine.h"
#include "ime/tsf/editsession.h"
#include "ime/ui/candobj.h"
#include <cstdint>
#include <tuple>
#include <wrl/client.h>

namespace qiqi {

template <typename Present>
void EnterCompositionCandidatePresentation(
    const CandidateUI* candidate_ui, const ITfThreadMgr* thread_mgr,
    const CompositionLifecycleState& state, Present present) {
    if (!candidate_ui || !thread_mgr || !state.Context() || !state.IsActive() || state.IsResettingContext()) {
        return;
    }
    const EditSessionTicket ticket{state.Epoch(), state.Context().Get()};
    const auto context = state.Context();
    present(ticket);
}

// qiqi-reentrancy: composition
template <typename UpdateComposition, typename IsCurrent, typename Position, typename Present>
void SyncCompositionPresentationInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot,
    UpdateComposition update_composition, IsCurrent is_current, Position position, Present present) {
    const CompositionEditGuard guard(
        update_composition(ec, context, snapshot.composition_text), std::ref(is_current));
    if (!guard.Current()) return;
    position(ec, context);
    if (!guard.Current()) return;
    present(snapshot);
}

template <typename GetManager, typename IsCurrent>
void PresentCompositionCandidates(
    CandidateUI& candidate_ui, const DWORD& activate_flags,
    const KeyProcessResult& snapshot, ITfDocumentMgr* document,
    const EditSessionEpoch& attempted_epoch,
    uint64_t service_id, const GUID& activity, GetManager get_manager, IsCurrent is_current) {
    auto& lifecycle = candidate_ui.UiElementLifecycle();
    const HRESULT retirement_hr = lifecycle.RetryRetirements(
        "ITfUIElementMgr::EndUIElement(pending-retirement)");
    if (retirement_hr == E_PENDING) return;
    if (FAILED(retirement_hr)) {
        std::ignore = candidate_ui.Show(FALSE);
        return;
    }
    lifecycle.ClearDocumentManagerIfIdle();
    if (!is_current()) return;
    const bool force_uiless = (activate_flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0;
    Microsoft::WRL::ComPtr<ITfUIElementMgr> ui_mgr;
    const HRESULT manager_hr = get_manager()->QueryInterface(IID_PPV_ARGS(&ui_mgr));
    const bool manager_ok = log::CheckHResult(
        manager_hr, "BeginOrUpdateCandidateUI::QueryInterface(ITfUIElementMgr)", log::Level::debug);
    if (!is_current()) {
        lifecycle.ClearDocumentManagerIfIdle();
        return;
    }
    if (!manager_ok || !ui_mgr) {
        QIQI_LOG_ACT_INFO(
            log::Level::warn,
            log::Keyword::UI,
            &activity,
            "BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; presentation hidden without negotiation");
        std::ignore = candidate_ui.Show(FALSE);
        if (lifecycle.HasActiveRegistration()) {
            const HRESULT end_hr = lifecycle.RetireActiveRegistration(
                "ITfUIElementMgr::EndUIElement(manager-query-failure)");
            std::ignore = log::CheckHResult(
                end_hr, "BeginOrUpdateCandidateUI::RetireUnavailableManager");
        }
        lifecycle.ClearDocumentManagerIfIdle();
        return;
    }
    if (lifecycle.HasActiveRegistration() && !lifecycle.ActiveManager()) {
        lifecycle.BindActiveManager(ui_mgr.Get());
    }
    const bool manager_changed =
        lifecycle.HasActiveRegistration() &&
        lifecycle.ActiveManager() &&
        lifecycle.ActiveManager() != ui_mgr.Get();
    if (manager_changed) {
        std::ignore = candidate_ui.Show(FALSE);
        const HRESULT end_hr = lifecycle.RetireActiveRegistration(
            "ITfUIElementMgr::EndUIElement(manager-change)");
        if (!log::CheckHResult(
                end_hr, "BeginOrUpdateCandidateUI::RetireManagerChanged")) {
            return;
        }
    }

    const auto publish_update = [&](
        const CandidateUiElementLifecycle::UpdateToken& token,
        std::string_view stage) noexcept {
        const DWORD updated_flags = lifecycle.UpdatedFlags();
        if (updated_flags == 0) {
            std::ignore = lifecycle.CompleteUpdate(token);
            return S_OK;
        }
        const uint64_t revision = lifecycle.UpdateRevision();
        const HRESULT update_hr = ui_mgr->UpdateUIElement(token.element_id);
        if (log::CheckHResult(update_hr, stage) &&
            lifecycle.IsUpdateCurrent(token)) {
            lifecycle.AcknowledgeUpdate(revision);
        }
        std::ignore = lifecycle.CompleteUpdate(token);
        return update_hr;
    };

    if (!lifecycle.HasActiveRegistration()) {
        const auto begin_token = lifecycle.TryBeginRegistration();
        if (!begin_token) return;
        const auto finish_begin = gsl::finally([&lifecycle, token = *begin_token]() noexcept {
            lifecycle.AbortBeginRegistration(token);
        });
        candidate_ui.ResetIntegrationState();
        candidate_ui.SetCandidates(
            snapshot.composition_text,
            snapshot.all_display_candidates,
            snapshot.page_size,
            snapshot.current_page,
            snapshot.selected_index,
            document, &snapshot.all_candidates);
        if (!is_current()) return;
        lifecycle.PrepareInitialUpdate();
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        const HRESULT begin_hr = ui_mgr->BeginUIElement(&candidate_ui, &show_own_ui, &element_id);
        const bool begin_ok = log::CheckHResult(begin_hr, "BeginOrUpdateCandidateUI::BeginUIElement");
        const bool valid_id = element_id != TF_INVALID_UIELEMENTID;
        if (!is_current()) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "TsfLifecycle(service={}, stage=ui-begin-stale, activation={}, context={}, element_id={}, begin_ok={})",
                service_id, attempted_epoch.activation, attempted_epoch.context, element_id, begin_ok);
            if (valid_id) {
                const HRESULT retire_hr = lifecycle.RetireRegistration(
                    element_id, ui_mgr.Get(),
                    "StaleCandidateUI::EndUIElement");
                std::ignore = log::CheckHResult(
                    retire_hr, "StaleCandidateUI::RetireRegistration");
            } else {
                lifecycle.AbortBeginRegistration(*begin_token);
            }
            lifecycle.ClearDocumentManagerIfIdle();
            return;
        }
        if (!lifecycle.IsBeginRegistrationCurrent(*begin_token)) {
            if (valid_id) {
                const HRESULT retire_hr = lifecycle.RetireRegistration(
                    element_id, ui_mgr.Get(),
                    "ReentrantCandidateUI::EndUIElement");
                std::ignore = log::CheckHResult(
                    retire_hr, "ReentrantCandidateUI::RetireRegistration");
            }
            return;
        }
        if (begin_ok && valid_id &&
            lifecycle.PublishRegistration(
                *begin_token, element_id, ui_mgr.Get())) {
            const bool show_popup = !force_uiless && show_own_ui != FALSE;
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={}, force_uiless={})",
                element_id, show_own_ui != FALSE, show_popup, force_uiless);
            std::ignore = candidate_ui.Show(show_popup ? TRUE : FALSE);
            if (!is_current()) return;
            if (!show_popup) {
                const auto update = lifecycle.TryBeginUpdate();
                if (update) {
                    const auto finish_update = gsl::finally(
                        [&lifecycle, token = *update]() noexcept {
                            std::ignore = lifecycle.CompleteUpdate(token);
                        });
                    std::ignore = publish_update(
                        *update,
                        "ITfUIElementMgr::UpdateUIElement(initial-uiless)");
                }
            } else {
                lifecycle.AcknowledgeUpdate(lifecycle.UpdateRevision());
            }
        } else {
            if (valid_id) {
                const HRESULT retire_hr = lifecycle.RetireRegistration(
                    element_id, ui_mgr.Get(),
                    "FailedBeginCandidateUI::EndUIElement");
                std::ignore = log::CheckHResult(
                    retire_hr, "FailedBeginCandidateUI::RetireRegistration");
            } else {
                lifecycle.AbortBeginRegistration(*begin_token);
            }
            const HRESULT diagnostic_hr = begin_ok ? E_INVALIDARG : begin_hr;
            QIQI_LOG_ACT_INFO(
                log::Level::warn,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: negotiation failed (hr=0x{:08x}, element_id={}); presentation hidden",
                gsl::narrow_cast<unsigned int>(diagnostic_hr), element_id);
            std::ignore = candidate_ui.Show(FALSE);
            lifecycle.ClearDocumentManagerIfIdle();
        }
        return;
    }

    const auto update = lifecycle.TryBeginUpdate();
    if (!update) return;
    const auto finish_update = gsl::finally(
        [&lifecycle, token = *update]() noexcept {
            std::ignore = lifecycle.CompleteUpdate(token);
        });
    candidate_ui.SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index,
        document, &snapshot.all_candidates);
    if (!is_current()) return;
    std::ignore = publish_update(
        *update, "ITfUIElementMgr::UpdateUIElement(subsequent)");
}

inline HRESULT EndCompositionCandidatePresentation(
    CandidateUI& candidate_ui,
    const Microsoft::WRL::ComPtr<ITfThreadMgr>& thread_mgr, uint64_t service_id, const GUID& activity) {
    std::ignore = candidate_ui.Show(FALSE);
    candidate_ui.ResetIntegrationState();
    auto& lifecycle = candidate_ui.UiElementLifecycle();
    const auto bind_active_manager = [&]() -> HRESULT {
        if (!lifecycle.HasActiveRegistration() ||
            lifecycle.ActiveManager()) {
            return S_OK;
        }
        if (!thread_mgr) return E_NOINTERFACE;
        ComPtr<ITfUIElementMgr> manager;
        const HRESULT manager_hr = thread_mgr.As(&manager);
        if (!log::CheckHResult(
                manager_hr, "EndCandidateUI::QueryInterface(ITfUIElementMgr)",
                log::Level::debug)) {
            return manager_hr;
        }
        if (!manager) return E_NOINTERFACE;
        lifecycle.BindActiveManager(manager.Get());
        return S_OK;
    };
    HRESULT end_hr = bind_active_manager();
    for (unsigned int attempt = 0;
         SUCCEEDED(end_hr) && attempt < 2; ++attempt) {
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &activity,
            "TsfLifecycle(service={}, stage=ui-end, attempt={}, state={}, element_id={}, has_manager={})",
            service_id, attempt + 1, static_cast<int>(lifecycle.State()),
            lifecycle.ActiveId(), lifecycle.ActiveManager() != nullptr);
        end_hr = lifecycle.EndRegistrations(
            "ITfUIElementMgr::EndUIElement");
        if (end_hr != E_NOINTERFACE ||
            !lifecycle.HasActiveRegistration() ||
            lifecycle.ActiveManager()) {
            break;
        }
        end_hr = bind_active_manager();
    }
    lifecycle.ClearDocumentManagerIfIdle();
    return end_hr;
}

} // namespace qiqi
