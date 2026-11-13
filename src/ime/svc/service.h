// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <commctrl.h>
#include <ctffunc.h>
#include <gsl/gsl>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>

#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "ime/engine/engine.h"
#include "ime/comp/access_policy.h"
#include "ime/comp/cleanup.h"
#include "ime/comp/edit.h"
#include "ime/comp/state.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/shift_tap.h"
#include "ime/ui/candidate_ui.h"
#include "ime/ui/langbar_button.h"
#include "ime/ui/statusbar.h"
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace qiqi {

using Microsoft::WRL::ComPtr;

class QiQiTextService final
    : public ComObjectBase<
          QiQiTextService,
          ITfTextInputProcessorEx,
          ITfThreadMgrEventSink,
          ITfKeyEventSink,
          ITfCompositionSink,
          ITfTextLayoutSink,
          ITfTextEditSink,
          ITfStatusSink,
          ITfCompartmentEventSink,
          ITfFunctionProvider,
          ITfFnConfigure,
          ITfDisplayAttributeProvider> {
public:
    explicit QiQiTextService(HINSTANCE instance);
    ~QiQiTextService() noexcept;

#pragma warning(push)
#pragma warning(disable : 26456)
    QiQiTextService(const QiQiTextService&) = delete;
    QiQiTextService& operator=(const QiQiTextService&) = delete;
    QiQiTextService(QiQiTextService&&) = delete;
    QiQiTextService& operator=(QiQiTextService&&) = delete;
#pragma warning(pop)

    bool QueryAdditionalInterfaces(REFIID riid, void** ppvObj) noexcept {
        return MatchBaseInterfaces<ITfTextInputProcessorEx, ITfTextInputProcessor>(riid, ppvObj) ||
               MatchBaseInterfaces<ITfFnConfigure, ITfFunction>(riid, ppvObj);
    }

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) noexcept override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) noexcept override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) noexcept override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

    // ITfTextLayoutSink
    STDMETHODIMP OnLayoutChange(ITfContext* pic, TfLayoutCode lcode, ITfContextView* pView) override;

    STDMETHODIMP OnEndEdit(ITfContext* context, TfEditCookie cookie, ITfEditRecord* record) override;

    // ITfStatusSink
    STDMETHODIMP OnStatusChange(ITfContext* pic, DWORD dwFlags) override;

    // ITfCompartmentEventSink
    STDMETHODIMP OnChange(REFGUID rguid) override;

    // ITfFunctionProvider
    STDMETHODIMP GetType(GUID* pguid) noexcept override;
    STDMETHODIMP GetDescription(BSTR* pbstrDesc) noexcept override;
    STDMETHODIMP GetFunction(REFGUID rguid, REFIID riid, IUnknown** ppunk) noexcept override;

    // ITfFnConfigure (also inherits ITfFunction::GetDisplayName)
    STDMETHODIMP GetDisplayName(BSTR* pbstrName) noexcept override;
    STDMETHODIMP Show(HWND hwndParent, LANGID langid, REFGUID rguidProfile) override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo** ppEnum) noexcept override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo** ppInfo) noexcept override;

private:
    enum class FocusNotificationSource { ThreadManager, KeyEvent };
    struct KeyEvent {
        uint32_t vk = 0;
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
        bool caps = false;
        LPARAM flags = 0;
    };
    struct ReloadResult {
        bool succeeded = true;
        HRESULT error = S_OK;
        void Fail(HRESULT hr = S_OK) noexcept {
            succeeded = false;
            if (FAILED(hr) && SUCCEEDED(error)) error = hr;
        }
        void Observe(HRESULT hr) noexcept {
            if (FAILED(hr)) Fail(hr);
        }
    };
    struct RegisteredPreservedKey {
        GUID guid{};
        TF_PRESERVEDKEY key{};
    };

    bool InitThreadMgrEventSink() noexcept;
    bool InitKeyEventSink();
    void UninitKeyEventSink();
    void RegisterCompositionDisplayAttributeAtom();
    void InitFunctionProvider();
    void UninitFunctionProvider();
    void InitLangBarItem();
    void UninitLangBarItem();
    void InitCompartmentSinks();
    void UninitCompartmentSinks() noexcept;
    bool NotifyGlobalConfigChanged() noexcept;
    void SaveGlobalStatusBarPosition(POINT pos) noexcept;
    HRESULT SyncStatusBarPositionFromGlobalCompartment(bool* changed = nullptr) noexcept;
    void SyncEngineFromSystemCompartments(REFGUID changed_guid);
    void UpdateUiIndicators();
    void HandleLangBarMenuAction(LangBarMenuId action_id);
    void HandleFocusChanged(bool has_focus, FocusNotificationSource source);
    static KeyEvent ReadKeyEvent(WPARAM key, LPARAM flags) noexcept;
    void HandleTestKeyDown(ITfContext* context, const KeyEvent& key, gsl::not_null<BOOL*> eaten) noexcept;
    void HandleKeyDown(ITfContext* context, const KeyEvent& key, gsl::not_null<BOOL*> eaten);
    void HandleTestKeyUp(ITfContext* context, uint32_t vk, gsl::not_null<BOOL*> eaten) noexcept;
    void HandleKeyUp(ITfContext* context, uint32_t vk, gsl::not_null<BOOL*> eaten);
    void HandlePreservedKey(ITfContext* context, REFGUID guid, gsl::not_null<BOOL*> eaten);
    void LogShiftTap(std::string_view stage, uint32_t vk, const ITfContext* context,
                     bool eaten, std::string_view reason) const noexcept;
    void TrackShiftTapKeyDown(std::string_view stage, uint32_t vk, ITfContext* context,
                             LPARAM flags) noexcept;
    bool HandleShiftTapKeyUp(std::string_view stage, uint32_t vk, const ITfContext* context,
                             bool test_only) noexcept;
    bool PersistAndSyncQuickConfigChange(QiQiConfig updated_config, bool reload_dicts);
    void SyncSavedConfigChange(bool reload_dicts);
    ReloadResult LoadExternalDictionaryNextToDll();

    void ResetCompositionAndUI(bool try_host_cleanup = true);
    void SetUiPresentationEnabled(bool enabled) noexcept;
    bool DestroyUIWindows() noexcept;
    void ScheduleCompositionCleanup();
    void ResetContextAndUI(bool try_host_cleanup = true);
    bool PrepareInputContext(ITfContext* context);
    bool ReconcileDocumentTarget(ITfDocumentMgr* document, std::string_view source);
    bool IsEditSessionCurrent(EditSessionTicket ticket) const noexcept;
    bool IsCompositionEditCurrent(
        EditSessionTicket ticket, const ITfComposition* composition) const noexcept;
    AccessPolicy EvaluateContextAccess(ITfContext* context, bool enforce_reset = true);
    AccessPolicy EvaluateContextAccessInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, EditSessionTicket ticket, bool enforce_reset = true);
    bool CanQueryContextAccess(ITfContext* context) const noexcept;
    ContextFacts ReadContextAccessFacts(gsl::not_null<ITfContext*> context, bool is_active_target) const noexcept;
    void EnforceContextAccessPolicy(const AccessPolicy& policy, bool is_active_target);
    void ConnectActiveContextAccessSinks(ITfContext* context);
    void DisconnectActiveContextAccessSinks() noexcept;
    void PrefetchActiveContextInputScope(ITfContext* context);
    void HandleActiveContextAccessChanged();
    bool IsPasswordObservationCurrent(uint64_t revision) const noexcept;
    void ObserveFocusedPasswordContext();
    void ObservePasswordDocument(ITfDocumentMgr* document);
    void ObservePasswordContext(ITfContext* context, ITfDocumentMgr* document);
    void DisconnectPasswordContext() noexcept;
    void RefreshPasswordContext();
    void UpdatePasswordModeInSession(TfEditCookie cookie, ITfContext* context, uint64_t revision);
    void UpdatePasswordMode(bool password);
    static LRESULT CALLBACK PasswordWindowSubclassProc(
        HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data);
    bool IsContextKeyboardDisabled(ITfContext* context) const noexcept;
    void SetChineseModeAndFinishComposition(ITfContext* context, bool enabled);
    void ToggleModeAndFinishComposition(ITfContext* context);
    void SyncTsfCompartments();
    void ApplyDynamicEngineSettings() noexcept;
    ReloadResult ReloadConfigIfModified(uint64_t focus_log_id = 0);
    void ApplyEngineResult(ITfContext* context, const KeyProcessResult& result);
    HRESULT ApplyCompositionEditInSession(TfEditCookie ec, const CompositionEditTask& task);
    void SyncCompositionUIInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot);
    ComPtr<ITfComposition> StartOrUpdateCompositionInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text);
    void CommitAndEndCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text);
    void CancelCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context);
    void UpdateCandidateWindowPositionInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, ComPtr<ITfContextView> view = {});

    void BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot);
    void EndCandidateUI();

    const uint64_t service_log_id_;
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD activate_flags_ = 0;
    bool activating_ = false;
    bool activation_cancelled_ = false;
    bool deactivating_ = false;
    ComPtr<ITfContext> preparing_input_context_;
    ScopedTfSinkConnection thread_mgr_sink_;
    ScopedTfSinkConnection text_layout_sink_;
    ScopedTfSinkConnection context_status_sink_;
    ComPtr<ITfContext> password_context_;
    ITfDocumentMgr* password_document_ = nullptr; // Identity only; valid while TSF owns the focused document.
    std::unique_ptr<ScopedTfSinkConnection> password_edit_sink_;
    HWND password_window_ = nullptr;
    uint64_t password_context_revision_ = 0;
    std::optional<bool> password_previous_chinese_;
    bool key_event_sink_registered_ = false;
    std::vector<RegisteredPreservedKey> registered_preserved_keys_;
    bool function_provider_registered_ = false;
    bool langbar_item_added_ = false;
    ScopedTfCompartment global_config_comp_;
    ScopedTfCompartment global_pos_comp_;
    ScopedTfCompartment openclose_comp_;
    ScopedTfCompartment convmode_comp_;
    ScopedTfCompartment context_disabled_comp_;
    ScopedTfCompartment context_empty_comp_;
    ContextAccessTracker context_access_;
    TfGuidAtom display_attribute_atom_ = TF_INVALID_GUIDATOM;
    bool syncing_compartments_ = false;
    bool has_valid_caret_pos_ = false;
    bool has_logged_focus_state_ = false;
    bool last_logged_focus_state_ = false;
    uint64_t focus_revision_ = 0;
    GUID current_lifecycle_activity_ = GUID_NULL;

    CompositionLifecycleState composition_lifecycle_;
    ComPtr<CandidateUI> candidate_ui_;
    ComPtr<LangBarButton> langbar_btn_;
    std::unique_ptr<StatusBarUI> status_bar_;
    QiQiConfig config_{};
    uint64_t config_registry_sequence_ = 0;
    WubiEngine engine_;
    ShiftTapTracker shift_tap_;
};

// Global DLL instance and active COM object counter
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

} // namespace qiqi
