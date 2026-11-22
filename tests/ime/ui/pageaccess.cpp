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

void TestCandidatePagingAccessibility() {
    const TsfTestCaseTrace trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetUiLanguage(UiLanguage::EnUs);
    const std::vector<std::wstring> candidates{
        L"一", L"二", L"三", L"四", L"五",
        L"六", L"七", L"八", L"九", L"十",
        L"甲", L"乙"
    };
    int page_turn_direction = 0;
    ui->SetPageTurnCallback([&](bool next_page) {
        page_turn_direction = next_page ? 1 : -1;
    });
    ui->SetCandidates(L"ab", candidates, 5, 0, 0);
    ui->MoveWindowTo(RECT{100, 100, 102, 124});
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());

    std::vector<PROPERTYID> changed_properties;
    ui->SetAccessibilityPropertyObserverForTest(
        [&](PROPERTYID property_id) {
            changed_properties.push_back(property_id);
        });
    unsigned int structure_changes = 0;
    ui->SetAccessibilityStructureObserverForTest(
        [&] { ++structure_changes; });
    std::vector<EVENTID> events;
    ui->SetAccessibilityEventObserverForTest(
        [&](EVENTID event) { events.push_back(event); });

    ComPtr<IRawElementProviderFragment> root_fragment;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_PPV_ARGS(&root_fragment)) == S_OK &&
                    root_fragment);
    ComPtr<IRawElementProviderSimple> root_simple;
    QIQI_TEST_CHECK(root_fragment.As(&root_simple) == S_OK && root_simple);
    QIQI_TEST_CHECK(ReadAutomationString(
                        root_simple.Get(), UIA_ItemStatusPropertyId) ==
                    L"Page 1 of 3");

    ComPtr<IRawElementProviderFragment> previous;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        previous.GetAddressOf()) == S_OK &&
                    previous);
    QIQI_TEST_CHECK(ReadAutomationId(previous.Get()) ==
                    L"IME_Candidate_PreviousPage");
    QIQI_TEST_CHECK(ReadAutomationName(previous.Get()) == L"Previous");
    ComPtr<IRawElementProviderSimple> previous_simple;
    QIQI_TEST_CHECK(previous.As(&previous_simple) == S_OK && previous_simple);
    VARIANT value;
    ::VariantInit(&value);
    QIQI_TEST_CHECK(previous_simple->GetPropertyValue(
                        UIA_ControlTypePropertyId, &value) == S_OK &&
                    value.vt == VT_I4 &&
                    value.lVal == UIA_ButtonControlTypeId);
    std::ignore = ::VariantClear(&value);
    QIQI_TEST_CHECK(!ReadAutomationBool(
        previous_simple.Get(), UIA_IsEnabledPropertyId));
    ComPtr<IUnknown> previous_pattern;
    QIQI_TEST_CHECK(previous_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        previous_pattern.GetAddressOf()) == S_OK &&
                    previous_pattern);
    ComPtr<IInvokeProvider> previous_invoke;
    QIQI_TEST_CHECK(previous_pattern.As(&previous_invoke) == S_OK &&
                    previous_invoke);
    QIQI_TEST_CHECK(previous_invoke->Invoke() ==
                        UIA_E_ELEMENTNOTENABLED &&
                    page_turn_direction == 0);
    ComPtr<ISelectionItemProvider> previous_selection_item;
    QIQI_TEST_CHECK(previous_simple->QueryInterface(
                        IID_PPV_ARGS(&previous_selection_item)) == E_NOINTERFACE &&
                    !previous_selection_item);

    ComPtr<IRawElementProviderFragment> next;
    QIQI_TEST_CHECK(previous->Navigate(
                        NavigateDirection_NextSibling, next.GetAddressOf()) == S_OK &&
                    next);
    QIQI_TEST_CHECK(ReadAutomationId(next.Get()) == L"IME_Candidate_NextPage");
    QIQI_TEST_CHECK(ReadAutomationName(next.Get()) == L"Next");
    ComPtr<IRawElementProviderSimple> next_simple;
    QIQI_TEST_CHECK(next.As(&next_simple) == S_OK && next_simple);
    QIQI_TEST_CHECK(ReadAutomationBool(
        next_simple.Get(), UIA_IsEnabledPropertyId));
    ComPtr<IUnknown> next_pattern;
    QIQI_TEST_CHECK(next_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        next_pattern.GetAddressOf()) == S_OK &&
                    next_pattern);
    ComPtr<IInvokeProvider> next_invoke;
    QIQI_TEST_CHECK(next_pattern.As(&next_invoke) == S_OK && next_invoke);
    QIQI_TEST_CHECK(next_invoke->Invoke() == S_OK &&
                    page_turn_direction == 1);

    ComPtr<IRawElementProviderFragment> first_candidate;
    QIQI_TEST_CHECK(next->Navigate(
                        NavigateDirection_NextSibling,
                        first_candidate.GetAddressOf()) == S_OK &&
                    first_candidate);
    QIQI_TEST_CHECK(ReadAutomationName(first_candidate.Get()) == L"一");
    QIQI_TEST_CHECK(ReadAutomationRuntimeChild(first_candidate.Get()) == 0);
    ComPtr<IRawElementProviderFragment> before_first_candidate;
    QIQI_TEST_CHECK(first_candidate->Navigate(
                        NavigateDirection_PreviousSibling,
                        before_first_candidate.GetAddressOf()) == S_OK &&
                    before_first_candidate);
    QIQI_TEST_CHECK(ReadAutomationId(before_first_candidate.Get()) ==
                    L"IME_Candidate_NextPage");

    ComPtr<IRawElementProviderFragment> last_candidate;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_LastChild,
                        last_candidate.GetAddressOf()) == S_OK &&
                    last_candidate);
    QIQI_TEST_CHECK(ReadAutomationName(last_candidate.Get()) == L"五");

    const auto hit_test = [&](const RECT& rect) {
        POINT center{
            (rect.left + rect.right) / 2,
            (rect.top + rect.bottom) / 2
        };
        QIQI_TEST_CHECK(::ClientToScreen(ui->WindowHandle(), &center));
        ComPtr<IRawElementProviderFragment> hit;
        ComPtr<IRawElementProviderFragmentRoot> root;
        QIQI_TEST_CHECK(ui.As(&root) == S_OK && root);
        QIQI_TEST_CHECK(root->ElementProviderFromPoint(
                            center.x, center.y, hit.GetAddressOf()) == S_OK &&
                        hit);
        return hit;
    };
    QIQI_TEST_CHECK(ReadAutomationId(
                        hit_test(ui->PrevPageButtonRect()).Get()) ==
                    L"IME_Candidate_PreviousPage");
    QIQI_TEST_CHECK(ReadAutomationId(
                        hit_test(ui->NextPageButtonRect()).Get()) ==
                    L"IME_Candidate_NextPage");

    structure_changes = 0;
    ui->SetUiLanguage(UiLanguage::ZhCn);
    QIQI_TEST_CHECK(structure_changes == 1);
    QIQI_TEST_CHECK(ReadAutomationName(previous.Get()) == L"上一页");
    QIQI_TEST_CHECK(ReadAutomationName(next.Get()) == L"下一页");
    QIQI_TEST_CHECK(ReadAutomationString(
                        root_simple.Get(), UIA_ItemStatusPropertyId) ==
                    L"第 1 / 3 页");
    ui->SetUiLanguage(UiLanguage::EnUs);
    QIQI_TEST_CHECK(structure_changes == 2);

    page_turn_direction = 0;
    changed_properties.clear();
    events.clear();
    structure_changes = 0;
    ui->SetCandidates(L"ab", candidates, 5, 1, 0);
    QIQI_TEST_CHECK(structure_changes == 1);
    QIQI_TEST_CHECK(events == std::vector<EVENTID>{
        UIA_SelectionItem_ElementSelectedEventId});
    QIQI_TEST_CHECK(std::ranges::contains(
                        changed_properties, UIA_ItemStatusPropertyId) &&
                    std::ranges::contains(
                        changed_properties, UIA_IsEnabledPropertyId));
    QIQI_TEST_CHECK(ReadAutomationString(
                        root_simple.Get(), UIA_ItemStatusPropertyId) ==
                    L"Page 2 of 3");
    ComPtr<IRawElementProviderFragment> middle_previous;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        middle_previous.GetAddressOf()) == S_OK &&
                    middle_previous);
    ComPtr<IRawElementProviderSimple> middle_previous_simple;
    QIQI_TEST_CHECK(middle_previous.As(&middle_previous_simple) == S_OK &&
                    middle_previous_simple);
    QIQI_TEST_CHECK(ReadAutomationBool(
        middle_previous_simple.Get(), UIA_IsEnabledPropertyId));
    ComPtr<IRawElementProviderFragment> middle_next;
    QIQI_TEST_CHECK(middle_previous->Navigate(
                        NavigateDirection_NextSibling,
                        middle_next.GetAddressOf()) == S_OK &&
                    middle_next);
    ComPtr<IRawElementProviderFragment> middle_first_candidate;
    QIQI_TEST_CHECK(middle_next->Navigate(
                        NavigateDirection_NextSibling,
                        middle_first_candidate.GetAddressOf()) == S_OK &&
                    middle_first_candidate);
    QIQI_TEST_CHECK(ReadAutomationRuntimeChild(
                        middle_first_candidate.Get()) == 5);
    ComPtr<IUnknown> middle_previous_pattern;
    QIQI_TEST_CHECK(middle_previous_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        middle_previous_pattern.GetAddressOf()) == S_OK);
    ComPtr<IInvokeProvider> middle_previous_invoke;
    QIQI_TEST_CHECK(middle_previous_pattern.As(&middle_previous_invoke) ==
                        S_OK &&
                    middle_previous_invoke);
    QIQI_TEST_CHECK(middle_previous_invoke->Invoke() == S_OK &&
                    page_turn_direction == -1);

    changed_properties.clear();
    events.clear();
    structure_changes = 0;
    ui->SetCandidates(L"ab", candidates, 5, 2, 0);
    QIQI_TEST_CHECK(structure_changes == 1 &&
                    ReadAutomationString(
                        root_simple.Get(), UIA_ItemStatusPropertyId) ==
                    L"Page 3 of 3");
    ComPtr<IRawElementProviderFragment> last_page_previous;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        last_page_previous.GetAddressOf()) == S_OK &&
                    last_page_previous);
    ComPtr<IRawElementProviderFragment> last_page_next;
    QIQI_TEST_CHECK(last_page_previous->Navigate(
                        NavigateDirection_NextSibling,
                        last_page_next.GetAddressOf()) == S_OK &&
                    last_page_next);
    ComPtr<IRawElementProviderSimple> last_page_next_simple;
    QIQI_TEST_CHECK(last_page_next.As(&last_page_next_simple) == S_OK &&
                    last_page_next_simple);
    QIQI_TEST_CHECK(!ReadAutomationBool(
        last_page_next_simple.Get(), UIA_IsEnabledPropertyId));
    ComPtr<IUnknown> last_page_next_pattern;
    QIQI_TEST_CHECK(last_page_next_simple->GetPatternProvider(
                        UIA_InvokePatternId,
                        last_page_next_pattern.GetAddressOf()) == S_OK);
    ComPtr<IInvokeProvider> last_page_next_invoke;
    QIQI_TEST_CHECK(last_page_next_pattern.As(&last_page_next_invoke) == S_OK &&
                    last_page_next_invoke);
    QIQI_TEST_CHECK(last_page_next_invoke->Invoke() ==
                    UIA_E_ELEMENTNOTENABLED);

    ui->SetCandidates(L"ab", {L"一", L"二"}, 5, 0, 0);
    QIQI_TEST_CHECK(ReadAutomationString(
                        root_simple.Get(), UIA_ItemStatusPropertyId) ==
                    L"Page 1 of 1");
    ComPtr<IRawElementProviderFragment> single_page_first;
    QIQI_TEST_CHECK(root_fragment->Navigate(
                        NavigateDirection_FirstChild,
                        single_page_first.GetAddressOf()) == S_OK &&
                    single_page_first);
    QIQI_TEST_CHECK(ReadAutomationName(single_page_first.Get()) == L"一");
    ComPtr<IRawElementProviderSimple> single_page_first_simple;
    QIQI_TEST_CHECK(single_page_first.As(&single_page_first_simple) == S_OK &&
                    single_page_first_simple);
    ComPtr<ISelectionItemProvider> selection_item;
    QIQI_TEST_CHECK(single_page_first_simple->QueryInterface(
                        IID_PPV_ARGS(&selection_item)) == S_OK &&
                    selection_item);
}

} // namespace qiqi::test::candidate_cases
