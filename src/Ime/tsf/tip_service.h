#pragma once
#include <windows.h>
#include <ctffunc.h>
#include <gsl/gsl>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>

#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "Ime/engine/engine.h"
#include "Ime/ui/candidate_ui.h"
#include "Ime/ui/langbar_button.h"
#include "Ime/ui/statusbar.h"
#include <memory>
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
    struct EditSessionEpoch {
        uint64_t activation = 0;
        uint64_t context = 0;
    };
    struct CompositionCleanup {
        CompositionCleanup() = default;
        ~CompositionCleanup() noexcept;
        CompositionCleanup(const CompositionCleanup&) = delete;
        CompositionCleanup& operator=(const CompositionCleanup&) = delete;
        CompositionCleanup(CompositionCleanup&&) = delete;
        CompositionCleanup& operator=(CompositionCleanup&&) = delete;
        void LogResult(std::string_view stage, HRESULT hr) const noexcept;
        uint64_t service_id = 0;
        uint64_t id = 0; // Zero tracks an unpublished StartComposition result, not a scheduled cleanup.
        ComPtr<ITfContext> context;
        ComPtr<ITfComposition> composition;
        ComPtr<ITfRange> range;
        bool started = false;
        bool attribute_clear_started = false;
        bool finished = false;
        bool terminated = false;
        bool async_requested = false;
        bool context_removed = false;
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
    bool SyncStatusBarPositionFromGlobalCompartment() noexcept;
    void SyncEngineFromSystemCompartments(REFGUID changed_guid);
    void UpdateUiIndicators();
    void HandleLangBarMenuAction(LangBarMenuId action_id);
    void HandleFocusChanged(bool has_focus, FocusNotificationSource source);
    void LogShiftTap(std::string_view stage, uint32_t vk, const ITfContext* context,
                     bool eaten, std::string_view reason) const noexcept;
    bool PersistAndSyncQuickConfigChange(QiQiConfig updated_config, bool reload_dicts);
    void SyncSavedConfigChange(bool reload_dicts);
    void LoadExternalDictionaryNextToDll();

    void ResetCompositionAndUI(bool try_host_cleanup = true);
    void SetUiPresentationEnabled(bool enabled) noexcept;
    bool DestroyUIWindows() noexcept;
    void ScheduleCompositionCleanup();
    void ScheduleCompositionCleanup(
        ComPtr<ITfContext> context, ComPtr<ITfComposition> composition, TfClientId client_id);
    bool HasPendingCompositionCleanup(const ITfContext* context);
    void MarkCompositionContextRemoved(const ITfContext* context) noexcept;
    void ResetContextAndUI(bool try_host_cleanup = true);
    bool PrepareInputContext(ITfContext* context);
    bool IsActiveDocument(ITfDocumentMgr* document) const;
    bool ReconcileDocumentTarget(ITfDocumentMgr* document, std::string_view source);
    bool IsEditSessionCurrent(EditSessionEpoch epoch, const ITfContext* context) const noexcept;
    bool IsCompositionEditCurrent(
        EditSessionEpoch epoch, const ITfContext* context, const ITfComposition* composition) const noexcept;
    bool IsContextKeyboardDisabled(ITfContext* context) const noexcept;
    void ToggleModeAndCancelComposition(ITfContext* context);
    void SyncTsfCompartments();
    void ApplyDynamicEngineSettings() noexcept;
    void ReloadConfigIfModified(uint64_t focus_log_id = 0);
    void ApplyEngineResult(ITfContext* context, const KeyProcessResult& result);
    void SyncCompositionUIInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot);
    ComPtr<ITfComposition> StartOrUpdateCompositionInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text);
    void CommitAndEndCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text);
    void CancelCompositionInSession(TfEditCookie ec, const ITfContext* context);
    void UpdateCandidateWindowPositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context);
    void SetRangeTextAndCollapseCaret(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text);

    void BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot);
    void EndCandidateUI();

    const uint64_t service_log_id_;
    uint64_t next_cleanup_log_id_ = 0;
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD activate_flags_ = 0;
    bool is_active_ = false;
    bool deactivating_ = false;
    bool resetting_context_ = false;
    ComPtr<ITfContext> preparing_input_context_;
    EditSessionEpoch edit_session_epoch_;
    ScopedTfSinkConnection thread_mgr_sink_;
    ScopedTfSinkConnection text_layout_sink_;
    bool key_event_sink_registered_ = false;
    std::vector<RegisteredPreservedKey> registered_preserved_keys_;
    bool function_provider_registered_ = false;
    bool langbar_item_added_ = false;
    ScopedTfCompartment global_config_comp_;
    ScopedTfCompartment global_pos_comp_;
    ScopedTfCompartment openclose_comp_;
    ScopedTfCompartment convmode_comp_;
    TfGuidAtom display_attribute_atom_ = TF_INVALID_GUIDATOM;
    bool syncing_compartments_ = false;
    bool has_valid_caret_pos_ = false;
    bool has_logged_focus_state_ = false;
    bool last_logged_focus_state_ = false;
    uint64_t focus_revision_ = 0;

    ComPtr<ITfComposition> composition_;
    std::vector<std::weak_ptr<CompositionCleanup>> composition_cleanups_;
    ComPtr<ITfContext> active_context_;
    ComPtr<ITfDocumentMgr> active_document_;
    ComPtr<CandidateUI> candidate_ui_;
    ComPtr<LangBarButton> langbar_btn_;
    std::unique_ptr<StatusBarUI> status_bar_;
    QiQiConfig config_{};
    uint64_t config_registry_sequence_ = 0;
    WubiEngine engine_;
    bool shift_tap_candidate_ = false;
    ULONGLONG shift_press_tick_ = 0;
    uint64_t shift_log_sequence_ = 0;
};

// Global DLL instance and active COM object counter
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

} // namespace qiqi
