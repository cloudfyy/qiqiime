// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candidate.h"
#include "ime/tsf/trace.h"
#include "common/support.h"
#include "common/guids.h"

namespace qiqi::test {

void RunCandidateSkinTests() {
    const auto& skin = GetBuiltinImeSkins().front();
    const std::optional<ImeSkinSnapshot> snapshot = ImeSkinSnapshot{skin.id, skin.palettes};
    candidate_cases::TestCandidateFontPreferences();
    candidate_cases::TestCandidateSkinRepainting();
    candidate_cases::TestCandidateSkinStates();
    candidate_cases::TestCandidateDisplayPreferences(snapshot);
    candidate_cases::TestCandidateLayouts(snapshot);
    candidate_cases::TestCandidateConstrainedViewport(snapshot);
}

void RunCandidateUITests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects_before = g_com_object_count.load();
    candidate_cases::TestCandidateLightDismissEvents();
    candidate_cases::TestCandidateTeardownReentry();
    candidate_cases::TestCandidateAutomationTeardown();
    candidate_cases::TestCandidateStableRepainting();
    candidate_cases::TestCandidateFontPreferences();
    candidate_cases::TestCandidateUI();
    candidate_cases::TestCandidateLayouts();
    candidate_cases::TestCandidateHostOwnership();
    candidate_cases::TestCandidateDisplayPreferences();
    candidate_cases::TestCandidateConstrainedViewport();
    candidate_cases::TestCandidateAccessibility();
    candidate_cases::TestCandidatePagingAccessibility();
    candidate_cases::TestCandidatePagingAccessibilityContracts();
    candidate_cases::TestCandidateUilessContract();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
