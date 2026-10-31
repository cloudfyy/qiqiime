#pragma once
#include <windows.h>
#include <ctffunc.h>
#include <gsl/gsl>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>

#include "common/com_object.h"
#include "common/config.h"
#include "common/guids.h"
#include "Ime/engine/engine.h"
#include "Ime/ui/candidate_ui.h"
#include "Ime/ui/langbar_button.h"
#include "Ime/ui/status_bar.h"
#include <memory>

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
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) noexcept override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) noexcept override;
    STDMETHODIMP OnPopContext(ITfContext* pic) noexcept override;

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
    bool InitThreadMgrEventSink();
    bool InitKeyEventSink();
    void UninitKeyEventSink();
    void InitDisplayAttributeGuidAtom();
    void InitFunctionProvider();
    void UninitFunctionProvider();
    void InitLangBarItem();
    void UninitLangBarItem();
    void InitCompartmentSinks();
    void UninitCompartmentSinks();
    void NotifyGlobalConfigChanged();
    void SaveGlobalStatusBarPosition(POINT pos);
    void SyncStatusBarPositionFromGlobalCompartment();
    void SyncEngineFromSystemCompartments(REFGUID changed_guid);
    void UpdateUiIndicators();
    void HandleLangBarMenuAction(LangBarMenuId action_id);
    void HandleFocusChanged(bool has_focus);
    void PersistAndSyncQuickConfigChange(bool reload_dicts);
    void LoadExternalDictionaryNextToDll();

    void ResetCompositionAndUI();
    bool IsContextKeyboardDisabled(ITfContext* context) const noexcept;
    void ToggleModeAndCancelComposition(ITfContext* context);
    void SyncTsfCompartments();
    void ApplyDynamicEngineSettings() noexcept;
    void ReloadConfigIfModified();
    void ApplyEngineResult(ITfContext* context, const KeyProcessResult& result);
    void SyncCompositionUIInSession(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, const KeyProcessResult& snapshot);
    void StartOrUpdateCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text);
    void CommitAndEndCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text);
    void CancelCompositionInSession(TfEditCookie ec, ITfContext* context);
    void UpdateCandidateWindowPositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context);
    static void SetRangeTextAndCollapseCaret(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text);

    void BeginOrUpdateCandidateUI(const KeyProcessResult& snapshot);
    void EndCandidateUI();

    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD activate_flags_ = 0;
    ScopedTfSinkConnection thread_mgr_sink_;
    ScopedTfSinkConnection text_layout_sink_;
    ScopedTfCompartment global_config_comp_;
    ScopedTfCompartment global_pos_comp_;
    ScopedTfCompartment openclose_comp_;
    ScopedTfCompartment convmode_comp_;
    TfGuidAtom display_attribute_atom_ = TF_INVALID_GUIDATOM;
    bool syncing_compartments_ = false;
    bool has_valid_caret_pos_ = false;

    ComPtr<ITfComposition> composition_;
    ComPtr<ITfContext> active_context_;
    ComPtr<CandidateUI> candidate_ui_;
    ComPtr<LangBarButton> langbar_btn_;
    std::unique_ptr<StatusBarUI> status_bar_;
    QiQiConfig config_{};
    uint64_t config_registry_sequence_ = 0;
    WubiEngine engine_;
    bool shift_tap_candidate_ = false;
    ULONGLONG shift_press_tick_ = 0;
};

// Global DLL instance and active COM object counter
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

} // namespace qiqi
