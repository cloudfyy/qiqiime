#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <atomic>

#include "wubi_engine.h"
#include "candidate_ui.h"

namespace qiqi {

using Microsoft::WRL::ComPtr;

class QiQiTextService final
    : public ITfTextInputProcessorEx,
      public ITfThreadMgrEventSink,
      public ITfKeyEventSink,
      public ITfCompositionSink {
public:
    QiQiTextService();
    ~QiQiTextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor
    STDMETHODIMP Activate(ITfThreadMgr* ptim, TfClientId tid) override;
    STDMETHODIMP Deactivate() override;

    // ITfTextInputProcessorEx
    STDMETHODIMP ActivateEx(ITfThreadMgr* ptim, TfClientId tid, DWORD dwFlags) override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* pdim) override;
    STDMETHODIMP OnSetFocus(ITfDocumentMgr* pdimFocus, ITfDocumentMgr* pdimPrevFocus) override;
    STDMETHODIMP OnPushContext(ITfContext* pic) override;
    STDMETHODIMP OnPopContext(ITfContext* pic) override;

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL fForeground) override;
    STDMETHODIMP OnTestKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyDown(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnKeyUp(ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) override;

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ecWrite, ITfComposition* pComposition) override;

private:
    bool InitThreadMgrEventSink();
    void UninitThreadMgrEventSink();
    bool InitKeyEventSink();
    void UninitKeyEventSink();
    void LoadExternalDictionaryNextToDll();

    void ApplyEngineResult(ITfContext* context, const KeyProcessResult& result);
    void StartOrUpdateCompositionInSession(TfEditCookie ec, ITfContext* context, const std::wstring& text);
    void CommitAndEndCompositionInSession(TfEditCookie ec, ITfContext* context, const std::wstring& committed_text);
    void CancelCompositionInSession(TfEditCookie ec, ITfContext* context);
    void UpdateCandidateWindowPositionInSession(TfEditCookie ec, ITfContext* context);

    void BeginOrUpdateCandidateUI();
    void EndCandidateUI();

    std::atomic<ULONG> ref_count_{1};
    ComPtr<ITfThreadMgr> thread_mgr_;
    TfClientId client_id_ = TF_CLIENTID_NULL;
    DWORD activate_flags_ = 0;
    DWORD thread_mgr_event_cookie_ = TF_INVALID_COOKIE;

    ComPtr<ITfComposition> composition_;
    CandidateUI* candidate_ui_ = nullptr; // Ref-counted COM object
    WubiEngine engine_;
    bool shift_tap_candidate_ = false;
};

// Global DLL instance and active COM object counter
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

} // namespace qiqi
