// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/controls.h"
#include "ime/comp/presentation.h"
#include "ime/tsf/mocks.h"
#include "ime/ui/candobj.h"

#include <windows.h>
#include <commctrl.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ime/ui/candidate.h"

namespace qiqi::test::candidate_cases {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring ReadAutomationString(
    gsl::not_null<IRawElementProviderSimple*> provider, PROPERTYID property_id) {
    VARIANT value;
    ::VariantInit(&value);
    const auto clear = gsl::finally([&]() noexcept {
        std::ignore = ::VariantClear(&value);
    });
    QIQI_TEST_CHECK(provider->GetPropertyValue(property_id, &value) == S_OK);
    QIQI_TEST_CHECK(value.vt == VT_BSTR && value.bstrVal);
    return std::wstring(value.bstrVal);
}

bool ReadAutomationBool(
    gsl::not_null<IRawElementProviderSimple*> provider, PROPERTYID property_id) {
    VARIANT value;
    ::VariantInit(&value);
    const auto clear = gsl::finally([&]() noexcept {
        std::ignore = ::VariantClear(&value);
    });
    QIQI_TEST_CHECK(provider->GetPropertyValue(property_id, &value) == S_OK);
    QIQI_TEST_CHECK(value.vt == VT_BOOL);
    return value.boolVal == VARIANT_TRUE;
}

ComPtr<IRawElementProviderSimple> AsAutomationSimple(
    gsl::not_null<IRawElementProviderFragment*> fragment) {
    ComPtr<IRawElementProviderSimple> simple;
    QIQI_TEST_CHECK(fragment->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK &&
                    simple);
    return simple;
}

std::wstring ReadAutomationId(gsl::not_null<IRawElementProviderFragment*> fragment) {
    return ReadAutomationString(
        AsAutomationSimple(fragment).Get(), UIA_AutomationIdPropertyId);
}

std::wstring ReadAutomationName(gsl::not_null<IRawElementProviderFragment*> fragment) {
    return ReadAutomationString(
        AsAutomationSimple(fragment).Get(), UIA_NamePropertyId);
}

long ReadAutomationRuntimeChild(gsl::not_null<IRawElementProviderFragment*> fragment) {
    SAFEARRAY* runtime_id = nullptr;
    QIQI_TEST_CHECK(fragment->GetRuntimeId(&runtime_id) == S_OK && runtime_id);
    const auto destroy_runtime_id = gsl::finally([&]() noexcept {
        if (runtime_id) {
            QIQI_TEST_CHECK_CLEANUP(::SafeArrayDestroy(runtime_id) == S_OK);
            runtime_id = nullptr;
        }
    });
    LONG index = 2;
    long child_id = 0;
    QIQI_TEST_CHECK(::SafeArrayGetElement(
                        runtime_id, &index, &child_id) == S_OK);
    return child_id;
}

} // namespace

void TestCandidatePagingAccessibilityContracts() {
    const TsfTestCaseTrace trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetUiLanguage(UiLanguage::EnUs);
    ui->SetCandidates(
        L"ab", {L"一", L"二", L"三", L"四", L"五", L"六"}, 5, 0, 0);
    ui->MoveWindowTo(RECT{100, 100, 102, 124});
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());

    ComPtr<IRawElementProviderFragment> root_fragment;
    ComPtr<IRawElementProviderSimple> root_simple;
    ComPtr<IRawElementProviderFragmentRoot> root;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_PPV_ARGS(&root_fragment)) == S_OK &&
                    root_fragment);
    QIQI_TEST_CHECK(root_fragment.As(&root_simple) == S_OK && root_simple);
    QIQI_TEST_CHECK(root_fragment.As(&root) == S_OK && root);

    SAFEARRAY* root_runtime_id = nullptr;
    QIQI_TEST_CHECK(root_fragment->GetRuntimeId(&root_runtime_id) == S_OK &&
                    root_runtime_id);
    QIQI_TEST_CHECK(::SafeArrayDestroy(root_runtime_id) == S_OK);
    UiaRect root_bounds{};
    QIQI_TEST_CHECK(root_fragment->get_BoundingRectangle(&root_bounds) == S_OK &&
                    root_bounds.width > 0 && root_bounds.height > 0);
    ComPtr<IRawElementProviderFragmentRoot> own_root;
    QIQI_TEST_CHECK(root_fragment->get_FragmentRoot(
                        own_root.GetAddressOf()) == S_OK &&
                    own_root);
    QIQI_TEST_CHECK(root_fragment->SetFocus() == S_OK);
    QIQI_TEST_CHECK(root->GetFocus(nullptr) == E_INVALIDARG);
    ComPtr<IRawElementProviderFragment> focused;
    QIQI_TEST_CHECK(root->GetFocus(focused.GetAddressOf()) == S_OK && focused);
    QIQI_TEST_CHECK(ReadAutomationName(focused.Get()) == L"一");

    BOOL can_select_multiple = TRUE;
    BOOL selection_required = FALSE;
    ComPtr<ISelectionProvider> selection_provider;
    QIQI_TEST_CHECK(ui.As(&selection_provider) == S_OK && selection_provider);
    QIQI_TEST_CHECK(selection_provider->GetSelection(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(selection_provider->get_CanSelectMultiple(
                        &can_select_multiple) == S_OK &&
                    can_select_multiple == FALSE);
    QIQI_TEST_CHECK(selection_provider->get_CanSelectMultiple(nullptr) ==
                    E_INVALIDARG);
    QIQI_TEST_CHECK(selection_provider->get_IsSelectionRequired(
                        &selection_required) == S_OK &&
                    selection_required == TRUE);
    QIQI_TEST_CHECK(selection_provider->get_IsSelectionRequired(nullptr) ==
                    E_INVALIDARG);

    SAFEARRAY* selected_elements = nullptr;
    QIQI_TEST_CHECK(selection_provider->GetSelection(
                        &selected_elements) == S_OK &&
                    selected_elements);
    LONG selected_lower = -1;
    LONG selected_upper = -2;
    QIQI_TEST_CHECK(::SafeArrayGetLBound(
                        selected_elements, 1, &selected_lower) == S_OK &&
                    selected_lower == 0);
    QIQI_TEST_CHECK(::SafeArrayGetUBound(
                        selected_elements, 1, &selected_upper) == S_OK &&
                    selected_upper == 0);
    QIQI_TEST_CHECK(::SafeArrayDestroy(selected_elements) == S_OK);

    RECT window_rect{};
    QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &window_rect));
    ComPtr<IRawElementProviderFragment> hit_root;
    QIQI_TEST_CHECK(root->ElementProviderFromPoint(
                        window_rect.left + 2.0,
                        window_rect.top + 2.0,
                        hit_root.GetAddressOf()) == S_OK &&
                    hit_root);
    QIQI_TEST_CHECK(ReadAutomationId(hit_root.Get()) ==
                    L"IME_Candidate_Window");

    ComPtr<IRawElementProviderFragment> previous;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        previous.GetAddressOf()) == S_OK &&
                    previous);
    ComPtr<IRawElementProviderSimple> previous_simple =
        AsAutomationSimple(previous.Get());
    ProviderOptions provider_options{};
    QIQI_TEST_CHECK(previous_simple->get_ProviderOptions(
                        &provider_options) == S_OK);
    QIQI_TEST_CHECK((provider_options & ProviderOptions_ServerSideProvider) != 0);
    QIQI_TEST_CHECK((provider_options & ~ProviderOptions_ServerSideProvider) == 0);
    QIQI_TEST_CHECK(previous_simple->GetPropertyValue(
                        UIA_NamePropertyId, nullptr) == E_INVALIDARG);
    ComPtr<IRawElementProviderSimple> host_provider;
    QIQI_TEST_CHECK(previous_simple->get_HostRawElementProvider(
                        host_provider.GetAddressOf()) == S_OK &&
                    !host_provider);
    SAFEARRAY* roots = nullptr;
    QIQI_TEST_CHECK(previous->GetEmbeddedFragmentRoots(&roots) == S_OK &&
                    roots == nullptr);
    QIQI_TEST_CHECK(ReadAutomationRuntimeChild(previous.Get()) ==
                    0x40000000L);
    UiaRect previous_bounds{};
    QIQI_TEST_CHECK(previous->get_BoundingRectangle(&previous_bounds) == S_OK &&
                    previous_bounds.width > 0 &&
                    previous_bounds.height > 0);
    QIQI_TEST_CHECK(!ReadAutomationBool(
        previous_simple.Get(), UIA_IsOffscreenPropertyId));
    ComPtr<IRawElementProviderFragmentRoot> previous_root;
    QIQI_TEST_CHECK(previous->get_FragmentRoot(
                        previous_root.GetAddressOf()) == S_OK &&
                    previous_root);
    ComPtr<IRawElementProviderFragment> parent;
    QIQI_TEST_CHECK(previous->Navigate(
                        NavigateDirection_Parent, parent.GetAddressOf()) == S_OK &&
                    parent);
    QIQI_TEST_CHECK(ReadAutomationId(parent.Get()) ==
                    L"IME_Candidate_Window");
    QIQI_TEST_CHECK(previous->SetFocus() == S_OK);
    ComPtr<IUnknown> unsupported_pattern;
    QIQI_TEST_CHECK(previous_simple->GetPatternProvider(
                        UIA_SelectionItemPatternId,
                        unsupported_pattern.GetAddressOf()) == S_OK &&
                    !unsupported_pattern);
    ComPtr<IUnknown> previous_pattern;
    QIQI_TEST_CHECK(previous_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        previous_pattern.GetAddressOf()) == S_OK &&
                    previous_pattern);
    ComPtr<IInvokeProvider> previous_invoke;
    QIQI_TEST_CHECK(previous_pattern.As(&previous_invoke) == S_OK &&
                    previous_invoke);
    QIQI_TEST_CHECK(previous_invoke->Invoke() ==
                    UIA_E_ELEMENTNOTENABLED);

    ComPtr<IRawElementProviderFragment> next;
    QIQI_TEST_CHECK(previous->Navigate(
                        NavigateDirection_NextSibling, next.GetAddressOf()) == S_OK &&
                    next);
    ComPtr<IRawElementProviderSimple> next_simple =
        AsAutomationSimple(next.Get());
    QIQI_TEST_CHECK(!ReadAutomationBool(
        next_simple.Get(), UIA_IsEnabledPropertyId));
    ComPtr<IUnknown> next_pattern;
    QIQI_TEST_CHECK(next_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        next_pattern.GetAddressOf()) == S_OK &&
                    next_pattern);
    ComPtr<IInvokeProvider> next_invoke;
    QIQI_TEST_CHECK(next_pattern.As(&next_invoke) == S_OK && next_invoke);
    ui->SetPageTurnCallback([](bool) {
        throw std::runtime_error("page callback test");
    });
    QIQI_TEST_CHECK(next_invoke->Invoke() == E_FAIL);
    ui->SetPageTurnCallback([](bool) { throw std::bad_alloc(); });
    QIQI_TEST_CHECK(next_invoke->Invoke() == E_OUTOFMEMORY);
    int page_turn_direction = 0;
    ui->SetPageTurnCallback([&](bool next_page) {
        page_turn_direction = next_page ? 1 : -1;
    });
    QIQI_TEST_CHECK(next_invoke->Invoke() == S_OK &&
                    page_turn_direction == 1);

    ComPtr<IRawElementProviderFragment> first_candidate;
    QIQI_TEST_CHECK(next->Navigate(
                        NavigateDirection_NextSibling,
                        first_candidate.GetAddressOf()) == S_OK &&
                    first_candidate);
    ComPtr<IRawElementProviderSimple> first_candidate_simple =
        AsAutomationSimple(first_candidate.Get());
    VARIANT candidate_control_type;
    ::VariantInit(&candidate_control_type);
    QIQI_TEST_CHECK(first_candidate_simple->GetPropertyValue(
                        UIA_ControlTypePropertyId,
                        &candidate_control_type) == S_OK &&
                    candidate_control_type.vt == VT_I4 &&
                    candidate_control_type.lVal == UIA_ListItemControlTypeId);
    std::ignore = ::VariantClear(&candidate_control_type);
    QIQI_TEST_CHECK(ReadAutomationBool(
                        first_candidate_simple.Get(),
                        UIA_IsEnabledPropertyId) &&
                    !ReadAutomationBool(
                        first_candidate_simple.Get(),
                        UIA_IsOffscreenPropertyId));
    UiaRect candidate_bounds{};
    QIQI_TEST_CHECK(first_candidate->get_BoundingRectangle(
                        &candidate_bounds) == S_OK &&
                    candidate_bounds.width > 0 &&
                    candidate_bounds.height > 0);
    ComPtr<IUnknown> candidate_unsupported_pattern;
    QIQI_TEST_CHECK(first_candidate_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        candidate_unsupported_pattern.GetAddressOf()) == S_OK &&
                    !candidate_unsupported_pattern);
    ComPtr<ISelectionItemProvider> first_selection;
    QIQI_TEST_CHECK(first_candidate_simple.As(&first_selection) == S_OK &&
                    first_selection);
    BOOL is_selected = FALSE;
    QIQI_TEST_CHECK(first_selection->get_IsSelected(&is_selected) == S_OK &&
                    is_selected == TRUE);
    QIQI_TEST_CHECK(first_selection->get_IsSelected(nullptr) == E_INVALIDARG);
    ComPtr<IRawElementProviderSimple> selection_container;
    QIQI_TEST_CHECK(first_selection->get_SelectionContainer(
                        selection_container.GetAddressOf()) == S_OK &&
                    selection_container);
    QIQI_TEST_CHECK(ReadAutomationString(
                        selection_container.Get(),
                        UIA_AutomationIdPropertyId) ==
                    L"IME_Candidate_Window");
    QIQI_TEST_CHECK(first_selection->AddToSelection() == E_NOTIMPL);
    QIQI_TEST_CHECK(first_selection->RemoveFromSelection() == E_NOTIMPL);
    size_t selected_index = SIZE_MAX;
    ui->SetSelectCallback(
        [&](size_t index) { selected_index = index; });
    QIQI_TEST_CHECK(first_candidate->SetFocus() == S_OK &&
                    selected_index == 0);
    ui->SetSelectCallback(
        [](size_t) { throw std::runtime_error("selection callback test"); });
    QIQI_TEST_CHECK(first_selection->Select() == E_FAIL);
    ui->SetSelectCallback([](size_t) { throw std::bad_alloc(); });
    QIQI_TEST_CHECK(first_selection->Select() == E_OUTOFMEMORY);

    ui->SetCandidates(
        L"ab", {L"一", L"二"}, 5, 0, 0);
    QIQI_TEST_CHECK(next_invoke->Invoke() ==
                    UIA_E_ELEMENTNOTAVAILABLE);
    VARIANT stale_value;
    ::VariantInit(&stale_value);
    QIQI_TEST_CHECK(next_simple->GetPropertyValue(
                        UIA_IsOffscreenPropertyId, &stale_value) ==
                    UIA_E_ELEMENTNOTAVAILABLE);
    QIQI_TEST_CHECK(first_candidate_simple->GetPropertyValue(
                        UIA_NamePropertyId, &stale_value) ==
                    UIA_E_ELEMENTNOTAVAILABLE);
    std::ignore = ::VariantClear(&stale_value);

    std::vector<PROPERTYID> changed_properties;
    ui->SetAccessibilityPropertyObserverForTest(
        [&](PROPERTYID property_id) {
            changed_properties.push_back(property_id);
        });
    ui->SetCandidates(L"ab", {}, 5, 0, 0);
    VARIANT item_status;
    ::VariantInit(&item_status);
    QIQI_TEST_CHECK(root_simple->GetPropertyValue(
                        UIA_ItemStatusPropertyId, &item_status) == S_OK &&
                    item_status.vt == VT_EMPTY);
    std::ignore = ::VariantClear(&item_status);
    QIQI_TEST_CHECK(std::ranges::contains(
        changed_properties, UIA_ItemStatusPropertyId));
    QIQI_TEST_CHECK(selection_provider->get_IsSelectionRequired(
                        &selection_required) == S_OK &&
                    selection_required == FALSE);
    SAFEARRAY* empty_selection = nullptr;
    QIQI_TEST_CHECK(selection_provider->GetSelection(
                        &empty_selection) == S_OK &&
                    empty_selection);
    LONG empty_lower = 0;
    LONG empty_upper = -1;
    QIQI_TEST_CHECK(::SafeArrayGetLBound(
                        empty_selection, 1, &empty_lower) == S_OK &&
                    empty_lower == 0);
    QIQI_TEST_CHECK(::SafeArrayGetUBound(
                        empty_selection, 1, &empty_upper) == S_OK &&
                    empty_upper == -1);
    QIQI_TEST_CHECK(::SafeArrayDestroy(empty_selection) == S_OK);
    ComPtr<IRawElementProviderFragment> no_focus;
    QIQI_TEST_CHECK(root->GetFocus(no_focus.GetAddressOf()) == S_OK &&
                    !no_focus);
    ComPtr<IRawElementProviderFragment> no_first_child;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        no_first_child.GetAddressOf()) == S_OK &&
                    !no_first_child);
}

} // namespace qiqi::test::candidate_cases
