// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/support.h"
#include "ime/engine/engine.h"

#include <windows.h>
#include <array>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace qiqi::test {

void TestCandidateCommit() {
    WubiEngine engine;
    QIQI_TEST_CHECK(engine.CommitCandidate(0).action == KeyActionType::PassThrough);
    engine.SetChineseMode(false);
    QIQI_TEST_CHECK(engine.CommitCandidate(0).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.State() == EngineState::English);
    engine.SetChineseMode(true);

    for (const bool full_width : {false, true}) {
        engine.SetFullWidthMode(full_width);
        engine.ProcessKey('Z');
        for (const auto vk : {'H', 'E', 'L', 'L', 'O'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
        const auto digit = engine.ProcessKey('1');
        QIQI_TEST_CHECK(digit.action == KeyActionType::UpdateState && digit.composition_text == L"zhello1");
        QIQI_TEST_CHECK(engine.CommitCandidate(1).action == KeyActionType::UpdateState);
        QIQI_TEST_CHECK(engine.RawCode() == L"zhello1" && engine.IsTempEnglishMode());
        const auto committed = engine.CommitCandidate(0);
        QIQI_TEST_CHECK(committed.action == KeyActionType::Commit && committed.committed_text == L"hello1");
        QIQI_TEST_CHECK(committed.composition_text.empty() && committed.page_candidates.empty());
        QIQI_TEST_CHECK(engine.State() == EngineState::Idle && engine.RawCode().empty());
        QIQI_TEST_CHECK(engine.IsFullWidthMode() == full_width);
        QIQI_TEST_CHECK(engine.CommitCandidate(0).action == KeyActionType::PassThrough);

        engine.ProcessKey('Z');
        const auto guide = engine.CommitCandidate(0);
        QIQI_TEST_CHECK(guide.action == KeyActionType::Commit && guide.committed_text == L"z");
    }
    engine.SetFullWidthMode(false);

    for (int i = 1; i <= 7; ++i) engine.AddEntry(L"mnop", L"candidate" + std::to_wstring(i));
    for (const auto vk : {'M', 'N', 'O', 'P'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
    engine.ProcessKey(VK_NEXT);
    QIQI_TEST_CHECK(engine.CurrentPage() == 1 && engine.CurrentPageCandidates().size() == 2);
    const auto selected = engine.SelectedInPage();
    QIQI_TEST_CHECK(engine.CommitCandidate(2).action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.RawCode() == L"mnop" && engine.CurrentPage() == 1 && engine.SelectedInPage() == selected);
    const auto committed = engine.CommitCandidate(1);
    QIQI_TEST_CHECK(committed.action == KeyActionType::Commit && committed.committed_text == L"candidate7");
    QIQI_TEST_CHECK(engine.State() == EngineState::Idle);

    engine.AddEntry(L"qrst", L"phrase1");
    engine.AddEntry(L"qrst", L"phrase2");
    engine.SetDynamicPhraseFreq(true);
    for (const auto vk : {'Q', 'R', 'S', 'T'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
    QIQI_TEST_CHECK(engine.CommitCandidate(1).committed_text == L"phrase2");
    QIQI_TEST_CHECK(engine.LookupExact(L"qrst").front() == L"phrase2");
}

void TestFirstLevelShortCodes() {
    WubiEngine engine;
    // 'a' -> 工, Space commits
    auto r1 = engine.ProcessKey('A');
    QIQI_TEST_CHECK(r1.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r1.composition_text == L"a");
    QIQI_TEST_CHECK(!r1.page_candidates.empty() && gsl::at(r1.page_candidates, 0) == L"工");

    auto r2 = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(r2.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(r2.committed_text == L"工");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // 'cf' -> 对
    engine.ProcessKey('C');
    auto r_cf = engine.ProcessKey('F');
    QIQI_TEST_CHECK(!r_cf.page_candidates.empty() && gsl::at(r_cf.page_candidates, 0) == L"对");
    engine.Reset();
}

void TestFourCodeUniqueAutoCommit() {
    WubiEngine engine;
    // "wqvb" -> "你好" (unique 4-code exact match -> auto commits on 4th key!)
    QIQI_TEST_CHECK(engine.ProcessKey('W').action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.ProcessKey('Q').action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.ProcessKey('V').action == KeyActionType::UpdateState);
    auto r4 = engine.ProcessKey('B');
    QIQI_TEST_CHECK(r4.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(r4.committed_text == L"你好");
    QIQI_TEST_CHECK(!engine.IsComposing());
}

void TestFifthKeyPushAutoCommit() {
    WubiEngine engine;
    // Add two words sharing 4-code "abcd" so 4th key doesn't auto-commit
    engine.AddEntry(L"abcd", L"甲");
    engine.AddEntry(L"abcd", L"乙");

    engine.ProcessKey('A');
    engine.ProcessKey('B');
    engine.ProcessKey('C');
    auto r4 = engine.ProcessKey('D');
    QIQI_TEST_CHECK(r4.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r4.page_candidates.size() >= 2);

    // Typing 5th key 'Q' ("我") should auto-commit "甲" and start composing "q"
    auto r5 = engine.ProcessKey('Q');
    QIQI_TEST_CHECK(r5.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(r5.committed_text == L"甲");
    QIQI_TEST_CHECK(r5.composition_text == L"q");
    QIQI_TEST_CHECK(engine.IsComposing());

    // Also test 5th key when the 4-code has NO matching candidates at all:
    engine.Reset();
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    auto r_no_cand = engine.ProcessKey('X'); // "yyyx" has no candidates
    QIQI_TEST_CHECK(r_no_cand.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r_no_cand.page_candidates.empty());

    auto r5_raw = engine.ProcessKey('A');
    QIQI_TEST_CHECK(r5_raw.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(r5_raw.committed_text == L"yyyx");
    QIQI_TEST_CHECK(r5_raw.composition_text == L"a");

    // Test disabling auto_commit_4code and push_commit_5th via ApplyConfig
    QiQiConfig custom_cfg{};
    custom_cfg.page_size = 7;
    custom_cfg.auto_commit_4code = false;
    custom_cfg.push_commit_5th = false;
    engine.ApplyConfig(custom_cfg);
    QIQI_TEST_CHECK(engine.PageSize() == 7);
    QIQI_TEST_CHECK(!engine.AutoCommit4Code());
    QIQI_TEST_CHECK(!engine.PushCommit5th());

    // With auto_commit_4code=false, "wqvb" ("你好") stays in Composing4 instead of auto-committing
    engine.Reset();
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    engine.ProcessKey('V');
    auto r4_no_auto = engine.ProcessKey('B');
    QIQI_TEST_CHECK(r4_no_auto.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing4);

    // With push_commit_5th=false, 5th key does not push-commit
    auto r5_no_push = engine.ProcessKey('A');
    QIQI_TEST_CHECK(r5_no_push.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.RawCode() == L"wqvb");
}
} // namespace qiqi::test
