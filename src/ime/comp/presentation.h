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

// Query the live manager at the original boundary; the service keeps the UI address stable.
inline HRESULT RetireCompositionCandidateUiElement(
    CandidateUI& candidate_ui, DWORD element_id, ITfUIElementMgr* manager,
    std::string_view stage) {
    if (element_id == TF_INVALID_UIELEMENTID || !manager) return S_OK;
    const HRESULT hr = manager->EndUIElement(element_id);
    if (!log::CheckHResult(hr, stage)) {
        candidate_ui.QueueUiElementRetirement(element_id, manager);
    }
    return hr;
}

template <typename GetManager, typename IsCurrent>
void PresentCompositionCandidates(
    CandidateUI& candidate_ui, const DWORD& activate_flags,
    const KeyProcessResult& snapshot, ITfDocumentMgr* document,
    const EditSessionEpoch& attempted_epoch,
    uint64_t service_id, const GUID& activity, GetManager get_manager, IsCurrent is_current) {
    const bool had_pending_retirements = candidate_ui.HasPendingUiElementRetirements();
    if (had_pending_retirements &&
        FAILED(candidate_ui.RetryUiElementRetirements())) {
        std::ignore = candidate_ui.Show(FALSE);
        return;
    }
    if (had_pending_retirements &&
        candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID) {
        candidate_ui.ClearUiElementDocument();
    }
    if (!is_current()) return;
    const bool force_uiless = (activate_flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0;
    Microsoft::WRL::ComPtr<ITfUIElementMgr> ui_mgr;
    const HRESULT manager_hr = get_manager()->QueryInterface(IID_PPV_ARGS(&ui_mgr));
    const bool manager_ok = log::CheckHResult(
        manager_hr, "BeginOrUpdateCandidateUI::QueryInterface(ITfUIElementMgr)", log::Level::debug);
    if (!is_current()) {
        if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID) {
            candidate_ui.ClearUiElementDocument();
        }
        return;
    }
    if (!manager_ok || !ui_mgr) {
        QIQI_LOG_ACT_INFO(
            log::Level::warn,
            log::Keyword::UI,
            &activity,
            "BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; presentation hidden without negotiation");
        std::ignore = candidate_ui.Show(FALSE);
        const DWORD existing_id = candidate_ui.UiElementId();
        const auto existing_manager = candidate_ui.UiElementManager();
        if (existing_id != TF_INVALID_UIELEMENTID) {
            if (existing_manager) {
                candidate_ui.SetUiElementId(TF_INVALID_UIELEMENTID);
                RetireCompositionCandidateUiElement(
                    candidate_ui, existing_id, existing_manager.Get(),
                    "ITfUIElementMgr::EndUIElement(manager-query-failure)");
            }
        }
        if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID &&
            !candidate_ui.HasPendingUiElementRetirements()) {
            candidate_ui.ClearUiElementDocument();
        }
        return;
    }
    if (candidate_ui.UiElementId() != TF_INVALID_UIELEMENTID &&
        !candidate_ui.UiElementManager()) {
        candidate_ui.SetUiElementId(candidate_ui.UiElementId(), ui_mgr.Get());
    }
    const bool manager_changed =
        candidate_ui.UiElementId() != TF_INVALID_UIELEMENTID &&
        candidate_ui.UiElementManager() &&
        candidate_ui.UiElementManager().Get() != ui_mgr.Get();
    if (manager_changed) {
        const DWORD old_element_id = candidate_ui.UiElementId();
        const auto old_manager = candidate_ui.UiElementManager();
        candidate_ui.SetUiElementId(TF_INVALID_UIELEMENTID);
        std::ignore = candidate_ui.Show(FALSE);
        if (old_element_id != TF_INVALID_UIELEMENTID && old_manager) {
            if (FAILED(RetireCompositionCandidateUiElement(
                    candidate_ui, old_element_id, old_manager.Get(),
                    "ITfUIElementMgr::EndUIElement(manager-change)"))) {
                return;
            }
        }
    }
    candidate_ui.SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index,
        document);
    if (!is_current()) return;
    const bool begin_registration =
        candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID;
    const bool owns_registration = begin_registration &&
        !candidate_ui.IsUiElementRegistrationInProgress() &&
        candidate_ui.TryBeginUiElementRegistration();
    if (candidate_ui.IsUiElementRegistrationInProgress() && !owns_registration) {
        return;
    }
    const auto finish_registration = gsl::finally([&candidate_ui, owns_registration]() noexcept {
        if (owns_registration) candidate_ui.EndUiElementRegistration();
    });

    const auto publish_update = [&](DWORD element_id, std::string_view stage) noexcept {
        DWORD updated_flags = 0;
        if (!log::CheckHResult(candidate_ui.GetUpdatedFlags(&updated_flags),
                              "CandidateUI::GetUpdatedFlags") || updated_flags == 0) {
            return;
        }
        const uint64_t revision = candidate_ui.UiElementUpdateRevision();
        const HRESULT update_hr = ui_mgr->UpdateUIElement(element_id);
        if (log::CheckHResult(
                update_hr, stage) &&
            candidate_ui.UiElementId() == element_id) {
            candidate_ui.AcknowledgeUiElementUpdate(revision);
        }
    };

    if (begin_registration) {
        candidate_ui.PrepareUiElementRegistrationUpdate();
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        const uint64_t element_revision = candidate_ui.UiElementIdRevision();
        const HRESULT begin_hr = ui_mgr->BeginUIElement(&candidate_ui, &show_own_ui, &element_id);
        const bool begin_ok = log::CheckHResult(begin_hr, "BeginOrUpdateCandidateUI::BeginUIElement");
        if (!is_current()) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "TsfLifecycle(service={}, stage=ui-begin-stale, activation={}, context={}, element_id={}, begin_ok={})",
                service_id, attempted_epoch.activation, attempted_epoch.context, element_id, begin_ok);
            if (element_id != TF_INVALID_UIELEMENTID) {
                std::ignore = RetireCompositionCandidateUiElement(
                    candidate_ui, element_id, ui_mgr.Get(),
                    "StaleCandidateUI::EndUIElement");
            }
            if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID &&
                !candidate_ui.HasPendingUiElementRetirements()) {
                candidate_ui.ClearUiElementDocument();
            }
            return;
        }
        const bool valid_id = element_id != TF_INVALID_UIELEMENTID;
        if (candidate_ui.UiElementIdRevision() != element_revision) {
            if (valid_id) {
                std::ignore = RetireCompositionCandidateUiElement(
                    candidate_ui, element_id, ui_mgr.Get(),
                    "ReentrantCandidateUI::EndUIElement");
            }
            return;
        }
        if (begin_ok && valid_id) {
            candidate_ui.SetUiElementId(element_id, ui_mgr.Get());
            const bool show_popup = show_own_ui != FALSE;
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={}, force_uiless={})",
                element_id, show_own_ui != FALSE, show_popup, force_uiless);
            std::ignore = candidate_ui.Show(show_popup ? TRUE : FALSE);
            if (!is_current()) return;
            if (!show_popup) {
                publish_update(element_id, "ITfUIElementMgr::UpdateUIElement(initial-uiless)");
            } else {
                candidate_ui.AcknowledgeUiElementUpdate(
                    candidate_ui.UiElementUpdateRevision());
            }
        } else {
            if (valid_id) {
                std::ignore = RetireCompositionCandidateUiElement(
                    candidate_ui, element_id, ui_mgr.Get(),
                    "FailedBeginCandidateUI::EndUIElement");
            }
            const HRESULT diagnostic_hr = begin_ok ? E_INVALIDARG : begin_hr;
            QIQI_LOG_ACT_INFO(
                log::Level::warn,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: negotiation failed (hr=0x{:08x}, element_id={}); presentation hidden",
                gsl::narrow_cast<unsigned int>(diagnostic_hr), element_id);
            std::ignore = candidate_ui.Show(FALSE);
            if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID &&
                !candidate_ui.HasPendingUiElementRetirements()) {
                candidate_ui.ClearUiElementDocument();
            }
        }
    } else {
        publish_update(
            candidate_ui.UiElementId(), "ITfUIElementMgr::UpdateUIElement(subsequent)");
    }
}

inline HRESULT EndCompositionCandidatePresentation(
    CandidateUI& candidate_ui,
    const Microsoft::WRL::ComPtr<ITfThreadMgr>& thread_mgr, uint64_t service_id, const GUID& activity) {
    std::ignore = candidate_ui.Show(FALSE);
    if (candidate_ui.HasPendingUiElementRetirements()) {
        const HRESULT retry_hr = candidate_ui.RetryUiElementRetirements();
        if (FAILED(retry_hr)) return retry_hr;
        if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID) {
            candidate_ui.ClearUiElementDocument();
        }
    }
    const DWORD element_id = candidate_ui.UiElementId();
    const auto element_manager = candidate_ui.UiElementManager();
    ComPtr<ITfUIElementMgr> manager = element_manager;
    if (!manager && thread_mgr) {
        const HRESULT manager_hr = thread_mgr.As(&manager);
        if (!log::CheckHResult(
                manager_hr, "EndCandidateUI::QueryInterface(ITfUIElementMgr)",
                log::Level::debug)) {
            return manager_hr;
        }
    }
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &activity,
        "TsfLifecycle(service={}, stage=ui-end, element_id={}, has_manager={})",
        service_id, element_id, manager != nullptr);
    if (element_id != TF_INVALID_UIELEMENTID) {
        if (!manager) return E_NOINTERFACE;
        candidate_ui.SetUiElementId(TF_INVALID_UIELEMENTID);
        const HRESULT end_hr = RetireCompositionCandidateUiElement(
            candidate_ui, element_id, manager.Get(),
            "ITfUIElementMgr::EndUIElement");
        if (FAILED(end_hr)) return end_hr;
    }
    if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID &&
        !candidate_ui.HasPendingUiElementRetirements()) {
        candidate_ui.ClearUiElementDocument();
    }
    return S_OK;
}

} // namespace qiqi
