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

void TestCandidateAccessibility() {
    const TsfTestCaseTrace trace(__func__);
    auto ui = MakeComObject<CandidateUI>(::GetModuleHandleW(nullptr));
    ui->SetCandidates(L"ab", {L"一(~ab)", L"二(cd)"}, 5, 0, 0);
    ui->MoveWindowTo(RECT{100, 100, 102, 124});
    std::vector<EVENTID> events;
    ui->SetAccessibilityEventObserverForTest([&](EVENTID event) {
        events.push_back(event);
    });
    std::vector<std::pair<EVENTID, bool>> dispatches;
    ui->SetAccessibilityDispatchObserverForTest(
        [&](EVENTID event, bool is_root) { dispatches.emplace_back(event, is_root); });
    unsigned int structure_changes = 0;
    ui->SetAccessibilityStructureObserverForTest([&] { ++structure_changes; });
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK && ui->WindowHandle());
    const std::vector<EVENTID> expected_open{
        UIA_MenuOpenedEventId, UIA_SelectionItem_ElementSelectedEventId};
    QIQI_TEST_CHECK(events == expected_open);
    QIQI_TEST_CHECK(dispatches == (std::vector<std::pair<EVENTID, bool>>{
        {UIA_MenuOpenedEventId, true},
        {UIA_SelectionItem_ElementSelectedEventId, false}}));

    ComPtr<IRawElementProviderSimple> simple;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_PPV_ARGS(&simple)) == S_OK && simple);
    VARIANT value;
    ::VariantInit(&value);
    QIQI_TEST_CHECK(simple->GetPropertyValue(UIA_AutomationIdPropertyId, &value) == S_OK &&
                    value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"IME_Candidate_Window");
    std::ignore = ::VariantClear(&value);

    ComPtr<IRawElementProviderFragment> root;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_PPV_ARGS(&root)) == S_OK && root);
    ComPtr<IRawElementProviderFragment> first;
    QIQI_TEST_CHECK(root->Navigate(NavigateDirection_FirstChild, first.GetAddressOf()) == S_OK &&
                    first);
    ComPtr<IRawElementProviderSimple> first_simple;
    QIQI_TEST_CHECK(first.As(&first_simple) == S_OK && first_simple);
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(UIA_NamePropertyId, &value) == S_OK &&
                    value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"一");
    std::ignore = ::VariantClear(&value);
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(UIA_HelpTextPropertyId, &value) == S_OK &&
                    value.vt == VT_EMPTY);
    std::ignore = ::VariantClear(&value);
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(
                        UIA_SelectionItemIsSelectedPropertyId, &value) == S_OK &&
                    value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE);
    std::ignore = ::VariantClear(&value);
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(
                        UIA_IsOffscreenPropertyId, &value) == S_OK &&
                    value.vt == VT_BOOL && value.boolVal == VARIANT_FALSE);
    std::ignore = ::VariantClear(&value);

    ComPtr<ISelectionItemProvider> first_selection;
    IUnknown* pattern = nullptr;
    QIQI_TEST_CHECK(first_simple->GetPatternProvider(
                        UIA_SelectionItemPatternId, &pattern) == S_OK && pattern);
    QIQI_TEST_CHECK(pattern->QueryInterface(IID_PPV_ARGS(&first_selection)) == S_OK &&
                    first_selection);
    ComPtr<IRawElementProviderSimple> selection_container;
    QIQI_TEST_CHECK(first_selection->get_SelectionContainer(
                        selection_container.GetAddressOf()) == S_OK && selection_container);
    ComPtr<IUnknown> container_pattern;
    QIQI_TEST_CHECK(selection_container->GetPatternProvider(
                        UIA_SelectionPatternId, container_pattern.GetAddressOf()) == S_OK &&
                    container_pattern);
    ComPtr<ISelectionProvider> selection_provider;
    QIQI_TEST_CHECK(container_pattern.As(&selection_provider) == S_OK &&
                    selection_provider);
    const auto check_automation_selection = [&](std::wstring_view expected_name) {
        SAFEARRAY* selected_elements = nullptr;
        QIQI_TEST_CHECK(selection_provider->GetSelection(&selected_elements) == S_OK &&
                        selected_elements);
        const auto destroy_selected_elements = gsl::finally([&]() noexcept {
            if (selected_elements) {
                QIQI_TEST_CHECK_CLEANUP(::SafeArrayDestroy(selected_elements) == S_OK);
                selected_elements = nullptr;
            }
        });
        VARTYPE element_type = VT_EMPTY;
        LONG lower_bound = 0;
        LONG upper_bound = -1;
        QIQI_TEST_CHECK(::SafeArrayGetVartype(selected_elements, &element_type) == S_OK &&
                        element_type == VT_UNKNOWN);
        QIQI_TEST_CHECK(::SafeArrayGetLBound(selected_elements, 1, &lower_bound) == S_OK &&
                        lower_bound == 0);
        QIQI_TEST_CHECK(::SafeArrayGetUBound(selected_elements, 1, &upper_bound) == S_OK &&
                        upper_bound == 0);
        LONG index = lower_bound;
        ComPtr<IUnknown> selected_item;
        QIQI_TEST_CHECK(::SafeArrayGetElement(
                            selected_elements, &index, selected_item.GetAddressOf()) == S_OK &&
                        selected_item);
        ComPtr<IRawElementProviderSimple> selected_simple;
        QIQI_TEST_CHECK(selected_item.As(&selected_simple) == S_OK && selected_simple);
        VARIANT selected_name;
        ::VariantInit(&selected_name);
        QIQI_TEST_CHECK(selected_simple->GetPropertyValue(
                            UIA_NamePropertyId, &selected_name) == S_OK &&
                        selected_name.vt == VT_BSTR &&
                        std::wstring(selected_name.bstrVal) == expected_name);
        std::ignore = ::VariantClear(&selected_name);
    };
    check_automation_selection(L"一");
    pattern->Release();

    ComPtr<IRawElementProviderFragment> second;
    QIQI_TEST_CHECK(first->Navigate(NavigateDirection_NextSibling, second.GetAddressOf()) == S_OK &&
                    second);
    ComPtr<IRawElementProviderSimple> second_simple;
    QIQI_TEST_CHECK(second.As(&second_simple) == S_OK);
    QIQI_TEST_CHECK(second_simple->GetPropertyValue(UIA_NamePropertyId, &value) == S_OK &&
                    value.vt == VT_BSTR && std::wstring(value.bstrVal) == L"二");
    std::ignore = ::VariantClear(&value);

    size_t selected = SIZE_MAX;
    ui->SetSelectCallback([&](size_t index) { selected = index; });
    ComPtr<ISelectionItemProvider> selection;
    IUnknown* selection_pattern = nullptr;
    QIQI_TEST_CHECK(second_simple->GetPatternProvider(
                        UIA_SelectionItemPatternId, &selection_pattern) == S_OK &&
                    selection_pattern);
    QIQI_TEST_CHECK(selection_pattern->QueryInterface(IID_PPV_ARGS(&selection)) == S_OK &&
                    selection);
    selection_pattern->Release();
    QIQI_TEST_CHECK(selection->Select() == S_OK && selected == 1);

    events.clear();
    structure_changes = 0;
    ui->SetCandidates(L"ab", {L"甲(~ab)", L"二(cd)"}, 5, 0, 1);
    QIQI_TEST_CHECK(events.size() == 1 &&
                    events.front() == UIA_SelectionItem_ElementSelectedEventId);
    QIQI_TEST_CHECK(structure_changes == 1);
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(
                        UIA_SelectionItemIsSelectedPropertyId, &value) ==
                    UIA_E_ELEMENTNOTAVAILABLE);
    std::ignore = ::VariantClear(&value);
    check_automation_selection(L"二");
    first.Reset();
    first_simple.Reset();
    QIQI_TEST_CHECK(root->Navigate(
                        NavigateDirection_FirstChild,
                        first.GetAddressOf()) == S_OK &&
                    first);
    QIQI_TEST_CHECK(first.As(&first_simple) == S_OK && first_simple);

    SAFEARRAY* runtime_id = nullptr;
    QIQI_TEST_CHECK(first->GetRuntimeId(&runtime_id) == S_OK && runtime_id);
    QIQI_TEST_CHECK(::SafeArrayDestroy(runtime_id) == S_OK);
    const RECT first_rect = ui->CandidateRect(0);
    POINT first_center{
        (first_rect.left + first_rect.right) / 2,
        (first_rect.top + first_rect.bottom) / 2
    };
    QIQI_TEST_CHECK(::ClientToScreen(ui->WindowHandle(), &first_center));
    ComPtr<IRawElementProviderFragmentRoot> fragment_root;
    QIQI_TEST_CHECK(ui->QueryInterface(IID_PPV_ARGS(&fragment_root)) == S_OK);
    ComPtr<IRawElementProviderFragment> hit;
    QIQI_TEST_CHECK(fragment_root->ElementProviderFromPoint(
                        first_center.x, first_center.y, hit.GetAddressOf()) == S_OK && hit);

    ComPtr<IUIAutomation> automation;
    QIQI_TEST_CHECK(
        ::CoCreateInstance(
            CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation)) == S_OK &&
        automation);
    ComPtr<IUIAutomationElement> automation_element;
    QIQI_TEST_CHECK(
        automation->ElementFromHandle(
            ui->WindowHandle(), automation_element.GetAddressOf()) == S_OK &&
        automation_element);

    events.clear();
    dispatches.clear();
    QIQI_TEST_CHECK(ui->Show(FALSE) == S_OK);
    QIQI_TEST_CHECK(events.size() == 1 && events.front() == UIA_MenuClosedEventId);
    QIQI_TEST_CHECK(dispatches == (std::vector<std::pair<EVENTID, bool>>{
        {UIA_MenuClosedEventId, true}}));
    QIQI_TEST_CHECK(first_simple->GetPropertyValue(
                        UIA_IsOffscreenPropertyId, &value) == S_OK &&
                    value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE);
    std::ignore = ::VariantClear(&value);

    events.clear();
    ui->SetPresentationEnabled(false);
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_FALSE && events.empty());
    ui->SetPresentationEnabled(true);
    QIQI_TEST_CHECK(ui->Show(TRUE) == S_OK);
    events.clear();
    QIQI_TEST_CHECK(ui->DestroyPopupWindow());
    QIQI_TEST_CHECK(events.size() == 1 && events.front() == UIA_MenuClosedEventId);
}

} // namespace qiqi::test::candidate_cases
