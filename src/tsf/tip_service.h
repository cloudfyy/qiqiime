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
#include "engine/wubi_engine.h"
#include "ui/candidate_ui.h"
#include "ui/langbar_button.h"
#include "ui/status_bar.h"
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
    void InitGlobalCompartmentSink();
    void UninitGlobalCompartmentSink();
    void NotifyGlobalConfigChanged();
    void HandleLangBarMenuAction(LangBarMenuId action_id);
    void HandleFocusChanged(bool has_focus);
    void PersistAndSyncQuickConfigChange(bool reload_dicts);
    void LoadExternalDictionaryNextToDll();

    void ResetCompositionAndUI();
    void ToggleModeAndCancelComposition(ITfContext* context);
    void SyncTsfCompartments();
    void ApplyDynamicEngineSettings() noexcept;
    void ReloadConfigIfModified();
    void ApplyEngineResult(ITfContext* context, const KeyProcessResult& result);
    void SyncCompositionUIInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text);
    void StartOrUpdateCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& text);
    void CommitAndEndCompositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context, const std::wstring& committed_text);
    void CancelCompositionInSession(TfEditCookie ec, ITfContext* context);
    void UpdateCandidateWindowPositionInSession(TfEditCookie ec, gsl::not_null<ITfContext*> context);
    static void SetRangeTextAndCollapseCaret(
        TfEditCookie ec, gsl::not_null<ITfContext*> context, gsl::not_null<ITfRange*> range, std::wstring_view text);

    void BeginOrUpdateCandidateUI();
    void EndCandidateUI();

    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD activate_flags_ = 0;
    ScopedTfSinkConnection thread_mgr_sink_;
    ScopedTfSinkConnection global_config_sink_;
    TfGuidAtom display_attribute_atom_ = TF_INVALID_GUIDATOM;

    ComPtr<ITfCompartment> global_config_comp_;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfContext> active_context_;
    ComPtr<CandidateUI> candidate_ui_;
    ComPtr<LangBarButton> langbar_btn_;
    std::unique_ptr<StatusBarUI> status_bar_;
    QiQiConfig config_{};
    std::filesystem::file_time_type config_last_write_time_{};
    WubiEngine engine_;
    bool shift_tap_candidate_ = false;
};

// Global DLL instance and active COM object counter
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

} // namespace qiqi
