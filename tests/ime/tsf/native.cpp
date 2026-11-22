// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scope.h"
#include "ime/tsf/trace.h"
#include "ime/tsf/editsession.h"

#include <array>
#include <format>
#include <iostream>

namespace qiqi::test::restricted_detail {
namespace {

void TestNativeContextContracts() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    ComPtr<ITfThreadMgrEx> manager;
    QIQI_TEST_CHECK(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&manager)) == S_OK);
    // Earlier native controls can already have activated this thread's TSF manager.
    TfClientId existing_client = TF_CLIENTID_NULL;
    const HRESULT existing_hr = manager->ActivateEx(&existing_client, TF_TMAE_NOACTIVATETIP);
    if (FAILED(existing_hr)) {
        const auto message = std::format("NativeContextContracts::Preactivate hr=0x{:08X}",
            gsl::narrow_cast<uint32_t>(existing_hr));
        ReportTestCleanupFailure(__FILE__, __LINE__, message.c_str());
    }
    QIQI_TEST_CHECK(SUCCEEDED(existing_hr) && existing_client != TF_CLIENTID_NULL);
    const auto deactivate_existing = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(manager->Deactivate() == S_OK);
    });
    TfClientId client = TF_CLIENTID_NULL;
    const HRESULT activation_hr = manager->ActivateEx(&client, TF_TMAE_NOACTIVATETIP);
    if (FAILED(activation_hr)) {
        DWORD active_flags = 0;
        const HRESULT flags_hr = manager->GetActiveFlags(&active_flags);
        const auto message = std::format(
            "NativeContextContracts::ActivateEx hr=0x{:08X}, client={}, requested_flags=0x{:08X}, "
            "active_flags_hr=0x{:08X}, active_flags=0x{:08X}",
            gsl::narrow_cast<uint32_t>(activation_hr), client, TF_TMAE_NOACTIVATETIP,
            gsl::narrow_cast<uint32_t>(flags_hr), active_flags);
        ReportTestCleanupFailure(__FILE__, __LINE__, message.c_str());
    }
    QIQI_TEST_CHECK(SUCCEEDED(activation_hr) && client != TF_CLIENTID_NULL);
    const auto deactivate = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(manager->Deactivate() == S_OK);
        DWORD active_flags = 0;
        QIQI_TEST_CHECK(manager->GetActiveFlags(&active_flags) == S_OK &&
                        (active_flags & TF_TMF_ACTIVATED) != 0);
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


} // namespace

void RunRestrictedInputNativeTests() {
    TestNativeContextContracts();
    TestNativeEditStyles();
}

} // namespace qiqi::test::restricted_detail
