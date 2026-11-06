#pragma once
#include "common/logger.h"
#include "ime/engine/engine.h"
#include "ime/tsf/edit_session.h"
#include "ime/ui/candidate_ui.h"
#include <cstdint>
#include <tuple>
#include <wrl/client.h>

namespace qiqi {

// Query the live manager at the original boundary; the service keeps the UI address stable.
template <typename GetManager, typename IsCurrent>
void PresentCompositionCandidates(
    CandidateUI& candidate_ui, const DWORD& activate_flags,
    const KeyProcessResult& snapshot, const EditSessionEpoch& attempted_epoch,
    uint64_t service_id, const GUID& activity, GetManager get_manager, IsCurrent is_current) {
    candidate_ui.SetCandidates(
        snapshot.composition_text,
        snapshot.all_display_candidates,
        snapshot.page_size,
        snapshot.current_page,
        snapshot.selected_index);
    if (!is_current()) return;

    Microsoft::WRL::ComPtr<ITfUIElementMgr> ui_mgr;
    const HRESULT manager_hr = get_manager()->QueryInterface(IID_PPV_ARGS(&ui_mgr));
    const bool manager_ok = log::CheckHResult(
        manager_hr, "BeginOrUpdateCandidateUI::QueryInterface(ITfUIElementMgr)", log::Level::debug);
    if (!is_current()) return;
    if (!manager_ok || !ui_mgr) {
        // Fallback if UIElementMgr unavailable
        QIQI_LOG_ACT_INFO(
            log::Level::debug,
            log::Keyword::UI,
            &activity,
            "BeginOrUpdateCandidateUI: ITfUIElementMgr unavailable; showing popup directly");
        candidate_ui.Show(TRUE);
        return;
    }

    if (candidate_ui.UiElementId() == TF_INVALID_UIELEMENTID) {
        BOOL show_own_ui = TRUE;
        DWORD element_id = TF_INVALID_UIELEMENTID;
        const HRESULT begin_hr = ui_mgr->BeginUIElement(&candidate_ui, &show_own_ui, &element_id);
        const bool begin_ok = log::CheckHResult(begin_hr, "BeginOrUpdateCandidateUI::BeginUIElement");
        if (!is_current()) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "TsfLifecycle(service={}, stage=ui-begin-stale, activation={}, context={}, element_id={}, begin_ok={})",
                service_id, attempted_epoch.activation, attempted_epoch.context, element_id, begin_ok);
            if (begin_ok && element_id != TF_INVALID_UIELEMENTID) {
                std::ignore = log::CheckHResult(ui_mgr->EndUIElement(element_id), "StaleCandidateUI::EndUIElement");
            }
            return;
        }
        if (begin_ok) {
            candidate_ui.SetUiElementId(element_id);
            // If host game/app returned FALSE in show_own_ui (UILess mode), do not show popup window
            const bool force_uiless = (activate_flags & TF_TMAE_UIELEMENTENABLEDONLY) != 0 && !show_own_ui;
            const bool show_popup = (show_own_ui && !force_uiless);
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: BeginUIElement(id={}, show_own_ui={}, show_popup={})",
                element_id, show_own_ui != FALSE, show_popup);
            candidate_ui.Show(show_popup ? TRUE : FALSE);
            if (!is_current()) return;
            std::ignore = log::CheckHResult(ui_mgr->UpdateUIElement(element_id), "ITfUIElementMgr::UpdateUIElement");
        } else {
            QIQI_LOG_ACT_INFO(
                log::Level::warn,
                log::Keyword::UI,
                &activity,
                "BeginOrUpdateCandidateUI: BeginUIElement failed; falling back to popup window");
            candidate_ui.Show(TRUE);
        }
    } else {
        std::ignore = log::CheckHResult(ui_mgr->UpdateUIElement(candidate_ui.UiElementId()),
                                       "ITfUIElementMgr::UpdateUIElement");
    }
}

inline void EndCompositionCandidatePresentation(
    CandidateUI& candidate_ui,
    const Microsoft::WRL::ComPtr<ITfThreadMgr>& thread_mgr, uint64_t service_id, const GUID& activity) {
    const DWORD element_id = candidate_ui.UiElementId();
    const auto manager = thread_mgr;
    candidate_ui.SetUiElementId(TF_INVALID_UIELEMENTID);
    candidate_ui.Show(FALSE);
    QIQI_LOG_ACT_INFO(
        log::Level::debug,
        log::Keyword::UI,
        &activity,
        "TsfLifecycle(service={}, stage=ui-end, element_id={}, has_manager={})",
        service_id, element_id, manager != nullptr);
    if (element_id != TF_INVALID_UIELEMENTID && manager) {
        Microsoft::WRL::ComPtr<ITfUIElementMgr> ui_mgr;
        if (log::CheckHResult(manager.As(&ui_mgr), "EndCandidateUI::QueryInterface(ITfUIElementMgr)") && ui_mgr) {
            std::ignore = log::CheckHResult(ui_mgr->EndUIElement(element_id),
                                           "ITfUIElementMgr::EndUIElement");
        }
    }
}

} // namespace qiqi
