// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

namespace qiqi::test {

void TestCompileTimeFsmTransitions();
void TestResetKeyPrediction();
void TestRuntimeFsmOutcomes();
void TestRuntimeFsmCompletion();
void TestCandidateCommit();
void TestFirstLevelShortCodes();
void TestFourCodeUniqueAutoCommit();
void TestFifthKeyPushAutoCommit();
void TestCodeCompletionInput();
void TestWildcardAndBackspaceAndCancel();
void TestNavigationSelectionAndReturnCommit();

void RunEngineInputTests() {
    TestCompileTimeFsmTransitions();
    TestResetKeyPrediction();
    TestRuntimeFsmOutcomes();
    TestRuntimeFsmCompletion();
    TestCandidateCommit();
    TestFirstLevelShortCodes();
    TestFourCodeUniqueAutoCommit();
    TestFifthKeyPushAutoCommit();
    TestCodeCompletionInput();
    TestWildcardAndBackspaceAndCancel();
    TestNavigationSelectionAndReturnCommit();
}

} // namespace qiqi::test
