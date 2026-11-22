// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/skin.h"
#include <optional>

namespace qiqi::test::candidate_cases {

void TestCandidateLightDismissEvents();
void TestCandidateTeardownReentry();
void TestCandidateAutomationTeardown();
void TestCandidateStableRepainting();
void TestCandidateFontPreferences();
void TestCandidateUI();
void TestCandidateLayouts(const std::optional<qiqi::ImeSkinSnapshot>& skin_snapshot = std::nullopt);
void TestCandidateHostOwnership();
void TestCandidateDisplayPreferences(
    const std::optional<qiqi::ImeSkinSnapshot>& skin_snapshot = std::nullopt);
void TestCandidateConstrainedViewport(
    const std::optional<qiqi::ImeSkinSnapshot>& skin_snapshot = std::nullopt);
void TestCandidateSkinRepainting();
void TestCandidateSkinStates();
void TestCandidateAccessibility();
void TestCandidatePagingAccessibility();
void TestCandidatePagingAccessibilityContracts();
void TestCandidateUilessContract();

} // namespace qiqi::test::candidate_cases
