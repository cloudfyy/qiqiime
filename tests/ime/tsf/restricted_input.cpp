#include "common/test_support.h"
#include "mocks.h"
#include "common/config.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/tip_service.h"

#include <initguid.h>
#include <inputscope.h>
#include <array>
#include <format>
#include <iostream>
#include <memory>

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
        if (has_scope && IsEqualGUID(guid, GUID_PROP_INPUTSCOPE)) {
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

void TestNativeContextContracts() {
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
                ComPtr<ITfRange> range;
                QIQI_TEST_CHECK(context->GetStart(ec, &range) == S_OK);
                ComPtr<ITfReadOnlyProperty> property;
                const HRESULT property_hr = context->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
                if (!has_scope && property_hr == S_FALSE) return S_OK;
                QIQI_TEST_CHECK(property_hr == S_OK && property);
                VARIANT value = MakeInt4Variant(0);
                const auto clear = gsl::finally([&]() noexcept {
                    QIQI_TEST_CHECK(::VariantClear(&value) == S_OK);
                });
                const HRESULT value_hr = property->GetValue(ec, range.Get(), &value);
                if (!has_scope) {
                    std::cout << std::format("[OBSERVE] absent-owner-scope: hr=0x{:08x}, vt={}\n",
                                              gsl::narrow_cast<unsigned int>(value_hr), value.vt);
                    QIQI_TEST_CHECK((value_hr == S_OK || value_hr == S_FALSE) && value.vt == VT_EMPTY);
                    return S_OK;
                }
                if (value_hr != S_OK) {
                    std::cerr << std::format("[FAIL] Native input scope property: hr=0x{:08x}, vt={}\n",
                                              gsl::narrow_cast<unsigned int>(value_hr), value.vt);
                }
                QIQI_TEST_CHECK(value_hr == S_OK);
                QIQI_TEST_CHECK(value.vt == VT_UNKNOWN && value.punkVal);
                ComPtr<ITfInputScope> input_scope;
                QIQI_TEST_CHECK(value.punkVal->QueryInterface(IID_PPV_ARGS(&input_scope)) == S_OK);
                InputScope* scopes = nullptr;
                UINT count = 0;
                const auto free_scopes = gsl::finally([&]() noexcept { ::CoTaskMemFree(scopes); });
                QIQI_TEST_CHECK(input_scope->GetInputScopes(&scopes, &count) == S_OK);
                QIQI_TEST_CHECK(scopes && count == 1 && *scopes == scope);
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
    const ScopedWindow parent(::CreateWindowExW(0, L"STATIC", L"", WS_POPUP,
                                               0, 0, 200, 80, nullptr, nullptr, g_dll_instance, nullptr));
    QIQI_TEST_CHECK(parent);
    for (const DWORD style : {DWORD{0}, DWORD{ES_PASSWORD}, DWORD{ES_READONLY}, DWORD{ES_NUMBER}}) {
        const ScopedWindow edit(::CreateWindowExW(0, L"EDIT", L"", WS_CHILD | style,
                                                 0, 0, 100, 24, parent.Get(), nullptr, g_dll_instance, nullptr));
        QIQI_TEST_CHECK(edit);
        ::SetLastError(ERROR_SUCCESS);
        const LONG_PTR actual = ::GetWindowLongPtrW(edit.Get(), GWL_STYLE);
        QIQI_TEST_CHECK(actual != 0 || ::GetLastError() == ERROR_SUCCESS);
        QIQI_TEST_CHECK((actual & (ES_PASSWORD | ES_READONLY | ES_NUMBER)) == gsl::narrow_cast<LONG_PTR>(style));
        QIQI_TEST_CHECK(::SendMessageW(edit.Get(), EM_SETREADONLY, TRUE, 0) != 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(edit.Get(), GWL_STYLE) & ES_READONLY) != 0);
        QIQI_TEST_CHECK(::SendMessageW(edit.Get(), EM_SETREADONLY, FALSE, 0) != 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(edit.Get(), GWL_STYLE) & ES_READONLY) == 0);
        ::SendMessageW(edit.Get(), EM_SETPASSWORDCHAR, L'*', 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(edit.Get(), GWL_STYLE) & ES_PASSWORD) != 0);
        ::SendMessageW(edit.Get(), EM_SETPASSWORDCHAR, 0, 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(edit.Get(), GWL_STYLE) & ES_PASSWORD) == 0);
    }
    std::cout << "[PASS] Native EDIT: ordinary/password/read-only/number styles and dynamic changes; no text read.\n";
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
    std::cout << "[OBSERVE] queued-write-after-disable: composition_started="
              << (context->start_composition_calls_ != 0) << '\n';
    const auto requests = context->request_edit_session_calls_;
    const auto ui_ends = manager->end_ui_calls_;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'B', 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] disabled-test-keydown: eaten=" << eaten << '\n';
    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] disabled-keydown: eaten=" << eaten
              << " cleanup_requested=" << (context->request_edit_session_calls_ != requests)
              << " candidate_ended=" << (manager->end_ui_calls_ != ui_ends) << '\n';
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);

    QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK);
    QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] disabled-shift-test-keyup: eaten=" << eaten << '\n';
    const auto mode_before = manager->convmode_compartment_->last_val_;
    QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] disabled-shift-keyup: eaten=" << eaten
              << " mode_changed=" << (manager->convmode_compartment_->last_val_ != mode_before) << '\n';
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK);
    std::cout << "[OBSERVE] disabled-preserved-toggle: eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = E_ACCESSDENIED;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] compartment-read-error=0x80070005: test_keydown_eaten=" << eaten << '\n';
    context->disabled_compartment_->get_value_result_ = S_FALSE;
    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'A', 0, &eaten) == S_OK);
    std::cout << "[OBSERVE] compartment-unset=S_FALSE/VT_EMPTY: test_keydown_eaten=" << eaten << '\n';
}

} // namespace

void RunRestrictedInputContractTests() {
    const auto objects = g_com_object_count.load();
    TestNativeContextContracts();
    TestNativeEditStyles();
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
