#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "mocks.h"
#include "common/config.h"
#include "common/file_util.h"
#include "ime/comp/access_policy.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/tip_service.h"

#include <initguid.h>
#include <inputscope.h>
#include <array>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <vector>

namespace qiqi::test {
namespace {

class ScopeOwner final : public ComObjectBase<ScopeOwner, ITfContextOwner, ITfInputScope> {
public:
    STDMETHODIMP GetACPFromPoint(const POINT*, DWORD, LONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(LONG, LONG, RECT*, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetScreenExt(RECT*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetWnd(HWND* window) noexcept override {
        if (!window) return E_INVALIDARG;
        *window = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetStatus(TF_STATUS* status) noexcept override {
        if (!status) return E_INVALIDARG;
        *status = {};
        status->dwDynamicFlags = read_only ? TF_SD_READONLY : 0;
        return S_OK;
    }
    STDMETHODIMP GetAttribute(REFGUID guid, VARIANT* value) noexcept override {
        if (!value) return E_INVALIDARG;
        ::VariantInit(value);
        if (has_scope && IsEqualGUID(guid, kGuidPropInputScope)) {
            value->vt = VT_UNKNOWN;
            ITfInputScope* input_scope = this;
            value->punkVal = input_scope;
            AddRef();
        }
        return S_OK;
    }
    STDMETHODIMP GetInputScopes(InputScope** scopes, UINT* count) noexcept override {
        if (!scopes || !count) return E_INVALIDARG;
        *scopes = nullptr;
        *count = 0;
        auto* const result = static_cast<InputScope*>(::CoTaskMemAlloc(sizeof(scope)));
        if (!result) return E_OUTOFMEMORY;
        *result = scope;
        *scopes = result;
        *count = 1;
        return S_OK;
    }
    STDMETHODIMP GetPhrase(BSTR** phrases, UINT* count) noexcept override {
        if (!phrases || !count) return E_INVALIDARG;
        *phrases = nullptr;
        *count = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetRegularExpression(BSTR* value) noexcept override { return NoString(value); }
    STDMETHODIMP GetSRGS(BSTR* value) noexcept override { return NoString(value); }
    STDMETHODIMP GetXML(BSTR* value) noexcept override { return NoString(value); }

    InputScope scope = IS_DEFAULT;
    bool has_scope = true;
    bool read_only = false;
private:
    static HRESULT NoString(BSTR* value) noexcept {
        if (!value) return E_INVALIDARG;
        *value = nullptr;
        return E_NOTIMPL;
    }
};

class MockInputScopeProperty final : public ComObjectBase<MockInputScopeProperty, ITfReadOnlyProperty, ITfInputScope> {
public:
    STDMETHODIMP GetType(GUID* guid) noexcept override {
        if (!guid) return E_INVALIDARG;
        *guid = kGuidPropInputScope;
        return S_OK;
    }
    STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges** ppEnum, ITfRange*) noexcept override {
        if (ppEnum) *ppEnum = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetValue(TfEditCookie, ITfRange*, VARIANT* pvarValue) override {
        if (!pvarValue) return E_INVALIDARG;
        ::VariantInit(pvarValue);
        if (get_value_callback) get_value_callback();
        if (get_value_result != S_OK) {
            return get_value_result;
        }
        if (value_vt == VT_EMPTY) {
            return S_OK;
        }
        if (value_vt != VT_UNKNOWN) {
            pvarValue->vt = value_vt;
            pvarValue->lVal = 1;
            return S_OK;
        }
        if (return_null_unknown) {
            pvarValue->vt = VT_UNKNOWN;
            pvarValue->punkVal = nullptr;
            return S_OK;
        }
        if (fail_qi_input_scope) {
            auto dummy = MakeComObject<MockTfRange>();
            pvarValue->vt = VT_UNKNOWN;
            pvarValue->punkVal = dummy.Detach();
            return S_OK;
        }
        pvarValue->vt = VT_UNKNOWN;
        ITfInputScope* sc = this;
        pvarValue->punkVal = sc;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP GetContext(ITfContext** ppCtx) noexcept override {
        if (ppCtx) *ppCtx = nullptr;
        return E_NOTIMPL;
    }

    // ITfInputScope
    STDMETHODIMP GetInputScopes(InputScope** pprgInputScopes, UINT* pcCount) noexcept override {
        if (!pprgInputScopes || !pcCount) return E_INVALIDARG;
        *pprgInputScopes = nullptr;
        *pcCount = 0;
        if (FAILED(get_scopes_result)) {
            return get_scopes_result;
        }
        if (inconsistent_null_buffer_with_count) {
            *pcCount = 2;
            return S_OK;
        }
        if (scopes.empty()) {
            return S_OK;
        }
        const size_t bytes = scopes.size() * sizeof(InputScope);
        auto* const buf = static_cast<InputScope*>(::CoTaskMemAlloc(bytes));
        if (!buf) return E_OUTOFMEMORY;
        const std::span<InputScope> out_span(buf, scopes.size());
        std::copy(scopes.begin(), scopes.end(), out_span.begin());
        *pprgInputScopes = buf;
        *pcCount = gsl::narrow_cast<UINT>(scopes.size());
        return get_scopes_result;
    }
    STDMETHODIMP GetPhrase(BSTR** ppbstrPhrases, UINT* pcCount) noexcept override {
        if (ppbstrPhrases) *ppbstrPhrases = nullptr;
        if (pcCount) *pcCount = 0;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetRegularExpression(BSTR* pbstrRegExp) noexcept override {
        if (pbstrRegExp) *pbstrRegExp = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetSRGS(BSTR* pbstrSRGS) noexcept override {
        if (pbstrSRGS) *pbstrSRGS = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetXML(BSTR* pbstrXML) noexcept override {
        if (pbstrXML) *pbstrXML = nullptr;
        return E_NOTIMPL;
    }

    std::vector<InputScope> scopes{IS_DEFAULT};
    HRESULT get_value_result = S_OK;
    VARTYPE value_vt = VT_UNKNOWN;
    bool return_null_unknown = false;
    bool fail_qi_input_scope = false;
    HRESULT get_scopes_result = S_OK;
    bool inconsistent_null_buffer_with_count = false;
    std::function<void()> get_value_callback;
};

void TestNativeContextContracts() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    ComPtr<ITfThreadMgrEx> manager;
    QIQI_TEST_CHECK(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&manager)) == S_OK);
    TfClientId client = TF_CLIENTID_NULL;
    QIQI_TEST_CHECK(manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(manager->Deactivate() == S_OK);
    });

    struct ScopeCase { InputScope scope; bool has_scope; };
    constexpr std::array<ScopeCase, 11> cases{{
        {IS_DEFAULT, true}, {IS_PASSWORD, true}, {IS_PRIVATE, true},
        {IS_NUMERIC_PASSWORD, true}, {IS_NUMERIC_PIN, true}, {IS_ALPHANUMERIC_PIN, true},
        {IS_ALPHANUMERIC_PIN_SET, true}, {IS_NUMBER, true},
        {IS_EMAIL_SMTPEMAILADDRESS, true}, {IS_URL, true}, {IS_DEFAULT, false}
    }};
    for (const auto& [scope, has_scope] : cases) {
        for (const bool read_only : {false, true}) {
            const auto owner = MakeComObject<ScopeOwner>();
            owner->scope = scope;
            owner->has_scope = has_scope;
            owner->read_only = read_only;
            ComPtr<ITfDocumentMgr> document;
            QIQI_TEST_CHECK(manager->CreateDocumentMgr(&document) == S_OK);
            ComPtr<ITfContext> context;
            TfEditCookie cookie = 0;
            QIQI_TEST_CHECK(document->CreateContext(client, 0, static_cast<ITfContextOwner*>(owner.Get()),
                                                    &context, &cookie) == S_OK);
            ScopedTfSinkConnection owner_sink;
            QIQI_TEST_CHECK(owner_sink.Advise(context.Get(), IID_ITfContextOwner,
                                              static_cast<ITfContextOwner*>(owner.Get())));
            QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
            const auto pop = gsl::finally([&]() noexcept {
                QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
            });
            TF_STATUS status{};
            QIQI_TEST_CHECK(context->GetStatus(&status) == S_OK);
            QIQI_TEST_CHECK(((status.dwDynamicFlags & TF_SD_READONLY) != 0) == read_only);

            const auto session = MakeComObject<LambdaEditSession>([&](TfEditCookie ec) -> HRESULT {
                const auto scope_res = QueryContextInputScopeInSession(
                    ec, gsl::not_null<ITfContext*>(context.Get()), [] { return true; });
                if (!has_scope) {
                    QIQI_TEST_CHECK(scope_res.classification == InputScopeClassification::Unknown);
                    return S_OK;
                }
                if (IsPasswordOrPinInputScope(scope)) {
                    QIQI_TEST_CHECK(scope_res.classification == InputScopeClassification::PasswordOrPin);
                } else if (scope == IS_PRIVATE) {
                    QIQI_TEST_CHECK(scope_res.classification == InputScopeClassification::Private);
                } else {
                    QIQI_TEST_CHECK(scope_res.classification == InputScopeClassification::Allowed);
                }
                const ContextFacts fast = ReadFastContextFacts(
                    context.Get(), scope_res.classification, [] { return true; }, false);
                const AccessPolicy pol = EvaluatePolicy(fast);
                if (read_only || IsPasswordOrPinInputScope(scope)) {
                    QIQI_TEST_CHECK(!pol.can_input && !pol.can_learn && pol.should_reset_active);
                } else if (scope == IS_PRIVATE) {
                    QIQI_TEST_CHECK(pol.can_input && !pol.can_learn && !pol.should_reset_active);
                } else {
                    QIQI_TEST_CHECK(pol.can_input && pol.can_learn && !pol.should_reset_active);
                }
                return S_OK;
            });
            HRESULT result = E_FAIL;
            QIQI_TEST_CHECK(context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READ, &result) == S_OK);
            QIQI_TEST_CHECK(result == S_OK);

            ComPtr<ITfCompartmentMgr> compartments;
            QIQI_TEST_CHECK(context.As(&compartments) == S_OK);
            for (const auto& guid : {GUID_COMPARTMENT_KEYBOARD_DISABLED, GUID_COMPARTMENT_EMPTYCONTEXT}) {
                ComPtr<ITfCompartment> compartment;
                QIQI_TEST_CHECK(compartments->GetCompartment(guid, &compartment) == S_OK);
                VARIANT value = MakeInt4Variant(0);
                const auto clear = gsl::finally([&]() noexcept {
                    QIQI_TEST_CHECK(::VariantClear(&value) == S_OK);
                });
                QIQI_TEST_CHECK(compartment->GetValue(&value) == S_FALSE && value.vt == VT_EMPTY);
                QIQI_TEST_CHECK(WriteCompartmentInt4(compartment.Get(), client, 1) == S_OK);
                QIQI_TEST_CHECK(compartment->GetValue(&value) == S_OK && value.vt == VT_I4 && value.lVal == 1);
                QIQI_TEST_CHECK(::VariantClear(&value) == S_OK);
                QIQI_TEST_CHECK(WriteCompartmentInt4(compartment.Get(), client, 0) == S_OK);
            }
        }
    }
    std::cout << "[PASS] Native TSF: 10 scopes + absent scope x writable/read-only; empty and explicit context compartments.\n";
}

void TestNativeEditStyles() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const ScopedWindow parent(::CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                                               0, 0, 200, 80, nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(parent);
    for (const DWORD style : {DWORD{0}, DWORD{ES_PASSWORD}, DWORD{ES_READONLY}, DWORD{ES_NUMBER}}) {
        const ScopedWindow edit(::CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style,
                                                 0, 0, 100, 24, parent.Get(), nullptr, g_dll_instance, nullptr));
        QIQI_TEST_CHECK(edit);
        const auto initial = InspectConfirmedCurrentThreadEditWindow(edit.Get());
        QIQI_TEST_CHECK(initial.is_native_edit);
        QIQI_TEST_CHECK(initial.is_password == ((style & ES_PASSWORD) != 0));
        QIQI_TEST_CHECK(initial.is_readonly == ((style & ES_READONLY) != 0));

        QIQI_TEST_CHECK(::SendMessageW(edit.Get(), EM_SETREADONLY, TRUE, 0) != 0);
        QIQI_TEST_CHECK(InspectConfirmedCurrentThreadEditWindow(edit.Get()).is_readonly);
        QIQI_TEST_CHECK(::SendMessageW(edit.Get(), EM_SETREADONLY, FALSE, 0) != 0);
        QIQI_TEST_CHECK(!InspectConfirmedCurrentThreadEditWindow(edit.Get()).is_readonly);
        ::SendMessageW(edit.Get(), EM_SETPASSWORDCHAR, L'*', 0);
        QIQI_TEST_CHECK(InspectConfirmedCurrentThreadEditWindow(edit.Get()).is_password);
        ::SendMessageW(edit.Get(), EM_SETPASSWORDCHAR, 0, 0);
        QIQI_TEST_CHECK(!InspectConfirmedCurrentThreadEditWindow(edit.Get()).is_password);
    }
    std::cout << "[PASS] Native EDIT: ordinary/password/read-only/number styles and dynamic changes; no text read.\n";
}

void TestUnifiedAccessPolicyAndEdgeCases() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    // 1. Pure policy and multi-scope classification checks
    {
        constexpr std::array<InputScope, 2> mixed_pwd{{IS_DEFAULT, IS_NUMERIC_PIN}};
        QIQI_TEST_CHECK(ClassifyInputScopeSpan(mixed_pwd) == InputScopeClassification::PasswordOrPin);
        constexpr std::array<InputScope, 2> mixed_priv{{IS_URL, IS_PRIVATE}};
        QIQI_TEST_CHECK(ClassifyInputScopeSpan(mixed_priv) == InputScopeClassification::Private);
        constexpr std::array<InputScope, 3> ordinary{{IS_NUMBER, IS_EMAIL_SMTPEMAILADDRESS, IS_URL}};
        QIQI_TEST_CHECK(ClassifyInputScopeSpan(ordinary) == InputScopeClassification::Allowed);
        QIQI_TEST_CHECK(ClassifyInputScopeSpan({}) == InputScopeClassification::Unknown);

        // Read-only + Private must still be Blocked (cannot write)
        ContextFacts ro_priv{.has_context = true, .is_readonly = true, .scope_state = InputScopeClassification::Private};
        const AccessPolicy ro_priv_pol = EvaluatePolicy(ro_priv);
        QIQI_TEST_CHECK(!ro_priv_pol.can_input && !ro_priv_pol.can_learn && ro_priv_pol.should_reset_active);

        // Pending scope: do not eat key, do not learn, do not destructively reset
        ContextFacts pending{.has_context = true, .scope_state = InputScopeClassification::Pending};
        const AccessPolicy pending_pol = EvaluatePolicy(pending);
        QIQI_TEST_CHECK(!pending_pol.can_input && !pending_pol.can_learn && !pending_pol.should_reset_active);
    }

    // 2. Scope query edge cases: multiple scopes, empty array, inconsistent count/null, bad VT, QI failure, GetValue failure
    {
        auto ctx = MakeComObject<MockTfContext>();
        ctx->get_start_result_ = S_OK;
        auto prop = MakeComObject<MockInputScopeProperty>();
        ctx->app_property_ = prop;
        ctx->get_app_property_result_ = S_OK;

        // Multi-scope with password second
        prop->scopes = {IS_DEFAULT, IS_ALPHANUMERIC_PIN};
        auto res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::PasswordOrPin);

        // Empty array (count == 0)
        prop->scopes.clear();
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::Unknown);

        // Inconsistent count > 0 with null buffer -> QueryError
        prop->inconsistent_null_buffer_with_count = true;
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::QueryError && res.hr == E_UNEXPECTED);
        prop->inconsistent_null_buffer_with_count = false;

        // Wrong VARIANT type -> QueryError
        prop->value_vt = VT_I4;
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::QueryError && res.hr == DISP_E_TYPEMISMATCH);
        prop->value_vt = VT_UNKNOWN;

        // Null punkVal -> QueryError
        prop->return_null_unknown = true;
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::QueryError);
        prop->return_null_unknown = false;

        // QI(ITfInputScope) failure -> QueryError
        prop->fail_qi_input_scope = true;
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::QueryError);
        prop->fail_qi_input_scope = false;

        // Runtime access denied -> QueryError
        prop->get_value_result = E_ACCESSDENIED;
        res = QueryContextInputScopeInSession(1, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
        QIQI_TEST_CHECK(res.classification == InputScopeClassification::QueryError && res.hr == E_ACCESSDENIED);
        prop->get_value_result = S_OK;
    }

    // 3. Service-level unified gating: queued write after disable, Shift, PreservedKeys, Candidate UI, thread-mgr compartment isolation, StatusSink
    {
        const auto manager = MakeComObject<MockTfThreadMgr>();
        const auto document = MakeComObject<MockTfDocumentMgr>();
        const auto queue = std::make_shared<MockEditSessionQueue>();
        const auto context = document->CreateTestContext(queue);
        context->allow_status_sink_ = true;
        context->get_start_result_ = S_OK;
        const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
        });

        // Thread manager compartment with same GUID must NOT disable a normal context
        ComPtr<ITfCompartment> tm_disabled;
        QIQI_TEST_CHECK(manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &tm_disabled) == S_OK);
        QIQI_TEST_CHECK(WriteCompartmentInt4(tm_disabled.Get(), 1, 1) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(WriteCompartmentInt4(tm_disabled.Get(), 1, 0) == S_OK);

        // Queued edit session before context becomes disabled must be blocked when session runs
        ComPtr<ITfCompartment> disabled;
        QIQI_TEST_CHECK(context->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        QIQI_TEST_CHECK(WriteCompartmentInt4(disabled.Get(), 1, 1) == S_OK);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context->start_composition_calls_ == 0);

        // All 4 key entry points + Shift + all 4 PreservedKeys must pass through when disabled
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        const auto mode_before = manager->convmode_compartment_->last_val_;
        QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(manager->convmode_compartment_->last_val_ == mode_before);

        for (const GUID* pk_guid : {&kGuidPreservedKeyToggleMode, &kGuidPreservedKeyToggleFullWidth,
                                    &kGuidPreservedKeyTogglePunctuation, &kGuidPreservedKeyAddUserWord}) {
            QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), *pk_guid, &eaten) == S_OK && !eaten);
        }

        // Unset compartment (S_FALSE) allows input; compartment error (E_ACCESSDENIED) blocks & resets
        QIQI_TEST_CHECK(WriteCompartmentInt4(disabled.Get(), 1, 0) == S_OK);
        context->disabled_compartment_->get_value_result_ = E_ACCESSDENIED;
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
        context->disabled_compartment_->get_value_result_ = S_FALSE;
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        context->disabled_compartment_->get_value_result_ = S_OK;

        // Composing -> dynamic read-only via ITfStatusSink -> cleans up active composition
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(context->status_sink_ != nullptr);
        context->status_read_only_ = true;
        QIQI_TEST_CHECK(context->status_sink_->OnStatusChange(context.Get(), TF_SD_READONLY) == S_OK);
        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
        context->status_read_only_ = false;
        while (queue->PendingCount() > 0) {
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        }
    }

    // 4. Private scope: normal input & commit allowed, but phrase promotion, pin/delete, and AddUserWord are suppressed
    {
        const auto subkey = std::format(L"Software\\QiQiIME_RestrictedPrivate_{}", ::GetCurrentProcessId());
        ScopedRegKey reg_key;
        DWORD disposition = 0;
        QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
                                         KEY_ALL_ACCESS, nullptr, reg_key.Put(), &disposition) == ERROR_SUCCESS);
        SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
        const auto clean_reg = gsl::finally([&]() noexcept {
            SetConfigRegistrySubKeyOverrideForTest(nullptr);
            reg_key.Reset();
            std::ignore = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        });

        const auto temp_dir = std::filesystem::temp_directory_path() /
                              std::format(L"QiQiIME_PrivateTest_{}", ::GetCurrentProcessId());
        std::error_code fs_ec;
        std::filesystem::create_directories(temp_dir, fs_ec);
        const auto clean_dir = gsl::finally([&]() noexcept {
            std::error_code rm_ec;
            std::filesystem::remove_all(temp_dir, rm_ec);
        });

        QiQiConfig cfg{};
        cfg.primary_dict = PrimaryDict::BuiltinOnly;
        cfg.enable_jidian_extra = false;
        cfg.dynamic_phrase_freq = true;
        cfg.auto_commit_4code = false;
        QIQI_TEST_CHECK(SaveConfigToRegistry(cfg));

        const auto manager = MakeComObject<MockTfThreadMgr>();
        const auto document = MakeComObject<MockTfDocumentMgr>();
        const auto context = document->CreateTestContext();
        context->get_start_result_ = S_OK;
        const auto scope_prop = MakeComObject<MockInputScopeProperty>();
        scope_prop->scopes = {IS_PRIVATE};
        context->app_property_ = scope_prop;
        context->get_app_property_result_ = S_OK;

        const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        const auto deactivate = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        });

        BOOL eaten = FALSE;
        // First key in IS_PRIVATE context eats and starts composition normally
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(context->start_composition_calls_ == 1 && context->range_->text_ == L"a");

        // AddUserWord preserved key is rejected in Private scope without touching clipboard
        QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyAddUserWord, &eaten) == S_OK && !eaten);

        // Space commits normally in Private scope
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(!context->range_->text_.empty());

        // Engine-level direct check: ProcessKey with allow_learn=false does not promote phrase or pin/delete
        WubiEngine engine;
        engine.ApplyConfig(cfg);
        engine.SetUserDictPath(temp_dir / L"user_dict.txt");
        engine.SetPhraseFreqPath(temp_dir / L"phrase_freq.qfreq");
        engine.AddEntry(L"aaaa", L"测试词一");
        engine.AddEntry(L"aaaa", L"测试词二");

        std::ignore = engine.ProcessKey('A', false, false, false, false, false, false);
        std::ignore = engine.ProcessKey('A', false, false, false, false, false, false);
        std::ignore = engine.ProcessKey('A', false, false, false, false, false, false);
        std::ignore = engine.ProcessKey('A', false, false, false, false, false, false);
        // Attempt Ctrl+2 pin and Ctrl+Shift+2 delete with allow_learn=false
        std::ignore = engine.ProcessKey('2', false, true, false, false, false, false);
        QIQI_TEST_CHECK(engine.PinnedEntries().empty());
        std::ignore = engine.ProcessKey('2', true, true, false, false, false, false);
        QIQI_TEST_CHECK(engine.BlockedEntries().empty());
        // Select 2nd candidate with allow_learn=false -> committed, but PromotedPhrases stays empty
        const auto commit_res = engine.ProcessKey('2', false, false, false, false, false, false);
        QIQI_TEST_CHECK(commit_res.action == KeyActionType::Commit && commit_res.committed_text == L"测试词二");
        QIQI_TEST_CHECK(engine.PromotedPhrases().empty());
        QIQI_TEST_CHECK(!std::filesystem::exists(temp_dir / L"phrase_freq.qfreq"));
    }
    std::cout << "[PASS] Unified access policy: fast/session checks, Shift/PreservedKeys, StatusSink, Private learning suppression.\n";
}

void ObserveCurrentService() {
    const auto manager = MakeComObject<MockTfThreadMgr>();
    const auto document = MakeComObject<MockTfDocumentMgr>();
    const auto queue = std::make_shared<MockEditSessionQueue>();
    const auto context = document->CreateTestContext(queue);
    const auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
    });
    ComPtr<ITfCompartment> disabled;
    QIQI_TEST_CHECK(context->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled) == S_OK);
    BOOL eaten = FALSE;
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(queue->PendingCount() == 1);
    QIQI_TEST_CHECK(WriteCompartmentInt4(disabled.Get(), 1, 1) == S_OK);
    QIQI_TEST_CHECK(queue->RunNext() == S_OK);
    QIQI_TEST_CHECK(context->start_composition_calls_ == 0);
    std::cout << "[OBSERVE] queued-write-after-disable: composition_started="
              << (context->start_composition_calls_ != 0) << '\n';
    const auto requests = context->request_edit_session_calls_;
    const auto ui_ends = manager->end_ui_calls_;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-test-keydown: eaten=" << eaten << '\n';
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-keydown: eaten=" << eaten
              << " cleanup_requested=" << (context->request_edit_session_calls_ != requests)
              << " candidate_ended=" << (manager->end_ui_calls_ != ui_ends) << '\n';
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);

    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-shift-test-keyup: eaten=" << eaten << '\n';
    const auto mode_before = manager->convmode_compartment_->last_val_;
    QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(manager->convmode_compartment_->last_val_ == mode_before);
    std::cout << "[OBSERVE] disabled-shift-keyup: eaten=" << eaten
              << " mode_changed=" << (manager->convmode_compartment_->last_val_ != mode_before) << '\n';
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] disabled-preserved-toggle: eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && !eaten);
    std::cout << "[OBSERVE] compartment-read-error=0x80070005: test_keydown_eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = S_FALSE;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
    std::cout << "[OBSERVE] compartment-unset=S_FALSE/VT_EMPTY: test_keydown_eaten=" << eaten << '\n';
}

} // namespace

void RunRestrictedInputContractTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects = g_com_object_count.load();
    TestNativeContextContracts();
    TestNativeEditStyles();
    TestUnifiedAccessPolicyAndEdgeCases();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects);
}

void RunRestrictedInputDiagnostics() {
    const auto subkey = std::format(L"Software\\QiQiIME_RestrictedInput_{}", ::GetCurrentProcessId());
    ScopedRegKey key;
    DWORD disposition = 0;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
                                     KEY_ALL_ACCESS, nullptr, key.Put(), &disposition) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(disposition == REG_CREATED_NEW_KEY);
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        key.Reset();
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    QiQiConfig config{};
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    std::array<BYTE, 256> original{};
    QIQI_TEST_CHECK(::GetKeyboardState(original.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original.data()));
    });
    std::array<BYTE, 256> clean{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean.data()));
    const auto objects = g_com_object_count.load();
    ObserveCurrentService();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects);
    std::cout << "[OBSERVE] Synthetic callbacks only; not browser, WPF, UWP or game acceptance.\n";
}

} // namespace qiqi::test
