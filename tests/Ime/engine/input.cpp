#include "common/test_support.h"
#include "Ime/engine/engine.h"

#include <windows.h>
#include <string>

namespace qiqi::test {

namespace {

void TestCompileTimeFsmTransitions() {
    // Exhaustive compile-time checks across all states and key events
    static_assert(ClassifyKeyEvent('A', false, false, false) == KeyEventKind::CodeChar);
    static_assert(ClassifyKeyEvent('A', true, false, false) == KeyEventKind::UpperChar);
    static_assert(ClassifyKeyEvent('A', false, true, false) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_SPACE, false, false, false) == KeyEventKind::Space);
    static_assert(ClassifyKeyEvent(VK_RETURN, false, false, false) == KeyEventKind::Enter);
    static_assert(ClassifyKeyEvent(VK_BACK, false, false, false) == KeyEventKind::Backspace);
    static_assert(ClassifyKeyEvent(VK_ESCAPE, false, false, false) == KeyEventKind::Escape);
    static_assert(ClassifyKeyEvent('1', false, false, false) == KeyEventKind::DigitSelect);
    static_assert(ClassifyKeyEvent('1', true, false, false) == KeyEventKind::ChinesePunct);
    static_assert(ClassifyKeyEvent(VK_UP, false, false, false) == KeyEventKind::CandidateUp);
    static_assert(ClassifyKeyEvent(VK_DOWN, false, false, false) == KeyEventKind::CandidateDown);
    static_assert(ClassifyKeyEvent(VK_OEM_MINUS, false, false, false) == KeyEventKind::PageUp);
    static_assert(ClassifyKeyEvent(VK_OEM_PLUS, false, false, false) == KeyEventKind::PageDown);

    // CapsLock compile-time FSM checks
    static_assert(ClassifyKeyEvent('A', false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent('Z', false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_OEM_PERIOD, false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent('A', false, false, false, true, false, true, true, EngineState::Idle, true) == KeyEventKind::PrintableAscii);
    static_assert(ClassifyKeyEvent('A', false, false, false, true, false, true, false, EngineState::Composing1, true) == KeyEventKind::UpperChar);
    static_assert(ClassifyKeyEvent('A', true, false, false, true, false, true, false, EngineState::Composing1, true) == KeyEventKind::CodeChar);
    static_assert(ClassifyKeyEvent(VK_CAPITAL, false, false, false, true, false, true, false, EngineState::Idle, false) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_CAPITAL, false, false, false, true, false, true, false, EngineState::Composing2, false) == KeyEventKind::CapsLockToggle);
    static_assert(LookupTransition(EngineState::Composing2, KeyEventKind::CapsLockToggle).op == TransitionOp::CommitRawCode);
    static_assert(!LookupTransition(EngineState::Composing2, KeyEventKind::CapsLockToggle).eat_key);

    // Compile-time checks for shared pagination helpers
    static_assert(ComputeTotalPages(0, 5) == 0);
    static_assert(ComputeTotalPages(5, 0) == 0);
    static_assert(ComputeTotalPages(7, 5) == 2);
    static_assert(ComputePageRange(7, 5, 0) == std::pair<size_t, size_t>{0, 5});
    static_assert(ComputePageRange(7, 5, 1) == std::pair<size_t, size_t>{5, 7});
    static_assert(ComputePageRange(7, 5, 2) == std::pair<size_t, size_t>{0, 0});
    static_assert(!LookupTransition(EngineState::Count, KeyEventKind::CodeChar).eat_key);
    static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::Count).eat_key);
    QIQI_TEST_CHECK(!LookupTransition(EngineState::Count, KeyEventKind::CodeChar).eat_key);
    QIQI_TEST_CHECK(!LookupTransition(EngineState::Idle, KeyEventKind::Count).eat_key);
    for (size_t s = 0; s < static_cast<size_t>(EngineState::Count); ++s) {
        for (size_t e = 0; e < static_cast<size_t>(KeyEventKind::Count); ++e) {
            const auto st = static_cast<EngineState>(s);
            const auto ev = static_cast<KeyEventKind>(e);
            const auto expected = ComputeTransitionRule(st, ev);
            const auto actual = LookupTransition(st, ev);
            QIQI_TEST_CHECK(actual.op == expected.op);
            QIQI_TEST_CHECK(actual.next_state == expected.next_state);
            QIQI_TEST_CHECK(actual.eat_key == expected.eat_key);
        }
    }

    // Runtime verification of explicit EngineState transitions
    WubiEngine engine;
    QIQI_TEST_CHECK(engine.State() == EngineState::Idle);
    engine.ProcessKey('A');
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing1);
    engine.ProcessKey('B');
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing2);
    engine.ProcessKey('C');
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing3);
    engine.ProcessKey('Z');
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing4);
    engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(engine.State() == EngineState::Composing3);
    engine.SetChineseMode(false);
    QIQI_TEST_CHECK(engine.State() == EngineState::English);
    engine.SetChineseMode(true);
    QIQI_TEST_CHECK(engine.State() == EngineState::Idle);

    // Runtime CapsLock behavior in WubiEngine:
    // 1) In Idle state with CapsLock ON, letters and punctuation pass through (or commit FullWidth if enabled)
    QIQI_TEST_CHECK(!engine.ShouldEatKey('A', false, false, false, true));
    QIQI_TEST_CHECK(!engine.ShouldEatKey('Z', false, false, false, true));
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_OEM_PERIOD, false, false, false, true));
    engine.SetFullWidthMode(true);
    QIQI_TEST_CHECK(engine.ShouldEatKey('A', false, false, false, true));
    const auto fw_caps = engine.ProcessKey('A', false, false, false, true);
    QIQI_TEST_CHECK(fw_caps.action == KeyActionType::Commit && fw_caps.committed_text == L"Ａ");
    engine.SetFullWidthMode(false);

    // 2) Pressing VK_CAPITAL mid-composition commits raw code and returns to Idle without eating the OS key
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    QIQI_TEST_CHECK(engine.IsComposing() && engine.RawCode() == L"wq");
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_CAPITAL, false, false, false, false));
    const auto caps_commit = engine.ProcessKey(VK_CAPITAL, false, false, false, false);
    QIQI_TEST_CHECK(caps_commit.action == KeyActionType::Commit && caps_commit.committed_text == L"wq");
    QIQI_TEST_CHECK(!engine.IsComposing() && engine.State() == EngineState::Idle);

    // 3) Pressing VK_CAPITAL in TempEnglish mode commits the temporary English string; Shift+Letter with CapsLock ON appends lowercase
    engine.ProcessKey('Z');
    engine.ProcessKey('A', false, false, false, true);
    engine.ProcessKey('B', true, false, false, true);
    QIQI_TEST_CHECK(engine.IsTempEnglishMode() && engine.RawCode() == L"zAb");
    const auto temp_caps_commit = engine.ProcessKey(VK_CAPITAL, false, false, false, true);
    QIQI_TEST_CHECK(temp_caps_commit.action == KeyActionType::Commit && temp_caps_commit.committed_text == L"Ab");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // 4) While composing in Full-Width mode, pressing a letter with CapsLock ON commits selected candidate + full-width uppercase letter
    engine.SetFullWidthMode(true);
    engine.ProcessKey('A');
    const auto comp_fw_caps = engine.ProcessKey('B', false, false, false, true);
    QIQI_TEST_CHECK(comp_fw_caps.action == KeyActionType::Commit && comp_fw_caps.committed_text == L"工Ｂ");
    // Shift+Letter while composing with CapsLock ON acts as lowercase Wubi code ('b'), not uppercase
    engine.SetFullWidthMode(false);
    engine.ProcessKey('A');
    const auto comp_shift_caps = engine.ProcessKey('B', true, false, false, true);
    QIQI_TEST_CHECK(comp_shift_caps.action == KeyActionType::UpdateState && comp_shift_caps.composition_text == L"ab");
    engine.Reset();
}

void TestFirstLevelShortCodes() {
    WubiEngine engine;
    // 'a' -> 工, Space commits
    auto r1 = engine.ProcessKey('A');
    QIQI_TEST_CHECK(r1.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r1.composition_text == L"a");
    QIQI_TEST_CHECK(!r1.page_candidates.empty() && r1.page_candidates[0] == L"工");

    auto r2 = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(r2.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(r2.committed_text == L"工");
    QIQI_TEST_CHECK(!engine.IsComposing());
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

void TestWildcardAndBackspaceAndCancel() {
    WubiEngine engine;
    // "wqz" wildcard query should match "你" (wqi / wqiy) and show code hints in AllDisplayCandidates()
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    // Verify progressive suffix code hints (~code) during 1..3-code input (before 'z')
    QIQI_TEST_CHECK(engine.ShowCodeHint());
    const auto& disp_wq = engine.AllDisplayCandidates();
    QIQI_TEST_CHECK(!disp_wq.empty());
    bool found_suffix_hint = false;
    for (const auto& label : disp_wq) {
        if (label.find(L"(~vb)") != std::wstring::npos || label.find(L"(~") != std::wstring::npos) {
            found_suffix_hint = true;
            break;
        }
    }
    QIQI_TEST_CHECK(found_suffix_hint);

    // When show_code_hint is disabled, AllDisplayCandidates() matches AllCandidates() without (~suffix)
    engine.SetShowCodeHint(false);
    QIQI_TEST_CHECK(!engine.ShowCodeHint());
    engine.Reset();
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    QIQI_TEST_CHECK(engine.AllDisplayCandidates() == engine.AllCandidates());
    engine.SetShowCodeHint(true);

    auto rz = engine.ProcessKey('Z');
    QIQI_TEST_CHECK(rz.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(!rz.page_candidates.empty());
    QIQI_TEST_CHECK(!engine.AllDisplayCandidates().empty());
    QIQI_TEST_CHECK(engine.AllDisplayCandidates().front().find(L'(') != std::wstring::npos);

    // Backspace removes 'z' -> "wq"
    auto rb = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(rb.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(rb.composition_text == L"wq");

    // Backspace removes 'q' -> "w"
    auto rb2 = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(rb2.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(rb2.composition_text == L"w");

    // Backspace on last character cancels composition completely
    auto rb3 = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(rb3.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Start again and Escape cancels composition
    engine.ProcessKey('W');
    auto resc = engine.ProcessKey(VK_ESCAPE);
    QIQI_TEST_CHECK(resc.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(!engine.IsComposing());
}

void TestNavigationSelectionAndReturnCommit() {
    WubiEngine engine;
    // Create 7 candidates for code "mnop" (2 pages with page_size = 5)
    for (int i = 1; i <= 7; ++i) {
        engine.AddEntry(L"mnop", L"候" + std::to_wstring(i));
    }

    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    auto r = engine.ProcessKey('P');
    QIQI_TEST_CHECK(r.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r.total_pages == 2);
    QIQI_TEST_CHECK(r.current_page == 0);
    QIQI_TEST_CHECK(r.selected_index == 0);
    QIQI_TEST_CHECK(r.page_candidates.size() == 5);

    // VK_UP at index 0 stays at 0
    QIQI_TEST_CHECK(engine.ProcessKey(VK_UP).selected_index == 0);

    // VK_DOWN moves selection down
    QIQI_TEST_CHECK(engine.ProcessKey(VK_DOWN).selected_index == 1);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_DOWN).selected_index == 2);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_UP).selected_index == 1);

    // Page navigation: next page via VK_NEXT and VK_OEM_PLUS, prev page via VK_PRIOR and VK_OEM_MINUS
    auto next_page = engine.ProcessKey(VK_NEXT);
    QIQI_TEST_CHECK(next_page.current_page == 1);
    QIQI_TEST_CHECK(next_page.selected_index == 0);
    QIQI_TEST_CHECK(next_page.page_candidates.size() == 2);

    // Next page at last page stays on last page
    QIQI_TEST_CHECK(engine.ProcessKey(VK_OEM_PLUS).current_page == 1);

    // Prev page via VK_OEM_MINUS
    auto prev_page = engine.ProcessKey(VK_OEM_MINUS);
    QIQI_TEST_CHECK(prev_page.current_page == 0);
    // Prev page at page 0 stays on page 0
    QIQI_TEST_CHECK(engine.ProcessKey(VK_PRIOR).current_page == 0);

    // Go to page 2 (index 1), where only 2 candidates exist; pressing '9' is out of range
    engine.ProcessKey(VK_OEM_PLUS);
    auto out_of_range = engine.ProcessKey('9');
    QIQI_TEST_CHECK(out_of_range.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.IsComposing());

    // Pressing '2' commits the 2nd candidate on page 2 ("候7")
    auto num_commit = engine.ProcessKey('2');
    QIQI_TEST_CHECK(num_commit.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(num_commit.committed_text == L"候7");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Test ';' (VK_OEM_1 -> 2nd candidate) and ''' (VK_OEM_7 -> 3rd candidate) quick-select
    QIQI_TEST_CHECK(engine.SemicolonQuoteQuickSelect());
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    engine.ProcessKey('P');
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_1, false, false, false));
    auto semi_commit = engine.ProcessKey(VK_OEM_1, false);
    QIQI_TEST_CHECK(semi_commit.action == KeyActionType::Commit && semi_commit.committed_text == L"候2");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Even when Chinese punctuation is disabled, ';' and ''' still quick-select while composing
    engine.SetChinesePunctuation(false);
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    engine.ProcessKey('P');
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_7, false, false, false));
    auto quote_commit = engine.ProcessKey(VK_OEM_7, false);
    QIQI_TEST_CHECK(quote_commit.action == KeyActionType::Commit && quote_commit.committed_text == L"候3");
    engine.SetChinesePunctuation(true);

    // When page has only 2 candidates (page 2 of "mnop"), pressing ''' (3rd candidate) stays in Composing4 without committing
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    engine.ProcessKey('P');
    engine.ProcessKey(VK_NEXT);
    auto quote_out_of_range = engine.ProcessKey(VK_OEM_7, false);
    QIQI_TEST_CHECK(quote_out_of_range.action == KeyActionType::UpdateState && engine.IsComposing());
    // Shift + ';' (':') while composing still commits selected candidate + '：'
    auto colon_commit = engine.ProcessKey(VK_OEM_1, true);
    QIQI_TEST_CHECK(colon_commit.action == KeyActionType::Commit && colon_commit.committed_text == L"候6：");

    // When semicolon_quote_quick_select is disabled, ';' while composing commits 1st candidate + '；'
    engine.SetSemicolonQuoteQuickSelect(false);
    QIQI_TEST_CHECK(!engine.SemicolonQuoteQuickSelect());
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    engine.ProcessKey('P');
    auto semi_punct_commit = engine.ProcessKey(VK_OEM_1, false);
    QIQI_TEST_CHECK(semi_punct_commit.action == KeyActionType::Commit && semi_punct_commit.committed_text == L"候1；");
    engine.SetSemicolonQuoteQuickSelect(true);

    // Test optional dynamic phrase frequency adjustment (single characters fixed, phrases promoted)
    QIQI_TEST_CHECK(!engine.DynamicPhraseFreq());
    engine.AddEntry(L"qrst", L"单");
    engine.AddEntry(L"qrst", L"词组甲");
    engine.AddEntry(L"qrst", L"词组乙");
    engine.AddEntry(L"qrst", L"字");

    // With dynamic_phrase_freq = false, selecting "词组乙" (3rd candidate via ''') does not reorder
    engine.ProcessKey('Q');
    engine.ProcessKey('R');
    engine.ProcessKey('S');
    engine.ProcessKey('T');
    auto commit_fixed = engine.ProcessKey(VK_OEM_7, false);
    QIQI_TEST_CHECK(commit_fixed.action == KeyActionType::Commit && commit_fixed.committed_text == L"词组乙");
    auto cands_before = engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(cands_before.size() == 4 &&
                    cands_before[0] == L"单" &&
                    cands_before[1] == L"词组甲" &&
                    cands_before[2] == L"词组乙" &&
                    cands_before[3] == L"字");

    // Enable dynamic_phrase_freq: selecting "词组乙" promotes it to the 1st phrase slot (index 1),
    // while single characters "单" (index 0) and "字" (index 3) remain fixed!
    engine.SetDynamicPhraseFreq(true);
    QIQI_TEST_CHECK(engine.DynamicPhraseFreq());
    engine.ProcessKey('Q');
    engine.ProcessKey('R');
    engine.ProcessKey('S');
    engine.ProcessKey('T');
    auto commit_prom = engine.ProcessKey(VK_OEM_7, false);
    QIQI_TEST_CHECK(commit_prom.action == KeyActionType::Commit && commit_prom.committed_text == L"词组乙");
    auto cands_after = engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(cands_after.size() == 4 &&
                    cands_after[0] == L"单" &&
                    cands_after[1] == L"词组乙" &&
                    cands_after[2] == L"词组甲" &&
                    cands_after[3] == L"字");

    // Selecting single character "字" (4th candidate via '4') does NOT move single characters
    engine.ProcessKey('Q');
    engine.ProcessKey('R');
    engine.ProcessKey('S');
    engine.ProcessKey('T');
    auto commit_single = engine.ProcessKey('4', false);
    QIQI_TEST_CHECK(commit_single.action == KeyActionType::Commit && commit_single.committed_text == L"字");
    auto cands_single_fixed = engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(cands_single_fixed[0] == L"单" && cands_single_fixed[3] == L"字");

    // Selecting "词组甲" via VK_DOWN + VK_SPACE (CommitSelected) promotes "词组甲" back ahead of "词组乙" without duplication
    const auto temp_freq_path = std::filesystem::temp_directory_path() / L"qiqiime_test_phrase_freq.txt";
    std::error_code freq_ec;
    std::filesystem::remove(temp_freq_path, freq_ec);
    engine.SetPhraseFreqPath(temp_freq_path);
    QIQI_TEST_CHECK(engine.PhraseFreqPath() == temp_freq_path);
    QIQI_TEST_CHECK(!engine.LoadPhraseFrequencies(temp_freq_path));

    engine.ProcessKey('Q');
    engine.ProcessKey('R');
    engine.ProcessKey('S');
    engine.ProcessKey('T');
    engine.ProcessKey(VK_DOWN);
    engine.ProcessKey(VK_DOWN); // index 2 is now "词组甲"
    auto commit_via_space = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(commit_via_space.action == KeyActionType::Commit && commit_via_space.committed_text == L"词组甲");
    auto cands_repromoted = engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(cands_repromoted.size() == 4 &&
                    cands_repromoted[0] == L"单" &&
                    cands_repromoted[1] == L"词组甲" &&
                    cands_repromoted[2] == L"词组乙" &&
                    cands_repromoted[3] == L"字");

    // Verify dynamic phrase promotions were persisted to temp_freq_path and can be restored into a fresh WubiEngine
    QIQI_TEST_CHECK(std::filesystem::exists(temp_freq_path));
    WubiEngine restored_freq_engine;
    restored_freq_engine.AddEntry(L"qrst", L"单");
    restored_freq_engine.AddEntry(L"qrst", L"词组甲");
    restored_freq_engine.AddEntry(L"qrst", L"词组乙");
    restored_freq_engine.AddEntry(L"qrst", L"字");
    restored_freq_engine.SetDynamicPhraseFreq(true);
    QIQI_TEST_CHECK(restored_freq_engine.LoadPhraseFrequencies(temp_freq_path));
    auto restored_cands = restored_freq_engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(restored_cands.size() == 4 &&
                    restored_cands[0] == L"单" &&
                    restored_cands[1] == L"词组甲" &&
                    restored_cands[2] == L"词组乙" &&
                    restored_cands[3] == L"字");
    engine.SetPhraseFreqPath({});
    std::filesystem::remove(temp_freq_path, freq_ec);
    engine.SetDynamicPhraseFreq(false);

    // Test VK_RETURN committing raw ASCII code
    engine.ProcessKey('A');
    engine.ProcessKey('B');
    auto ret_commit = engine.ProcessKey(VK_RETURN);
    QIQI_TEST_CHECK(ret_commit.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(ret_commit.committed_text == L"ab");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Test VK_SPACE when no candidate exists (commits raw code) and 4-code empty beep / typing sound flags
    QIQI_TEST_CHECK(engine.BeepOnEmpty4Code());
    QIQI_TEST_CHECK(!engine.ClearOnEmpty4Code());
    QIQI_TEST_CHECK(!engine.EnableKeySound());
    engine.SetEnableKeySound(true);
    QIQI_TEST_CHECK(engine.EnableKeySound());

    auto k1 = engine.ProcessKey('Y');
    QIQI_TEST_CHECK(k1.trigger_key_sound && !k1.empty_4code_warning);
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    auto k4_empty = engine.ProcessKey('Y'); // "yyyy" is an empty 4-code in builtin dict (only "yyyy" if not added; use "xxxx")
    engine.Reset();
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    auto k4_xx = engine.ProcessKey('Y'); // "xxxy" has no match
    QIQI_TEST_CHECK(k4_xx.empty_4code_warning);
    QIQI_TEST_CHECK(k4_xx.trigger_key_sound);
    QIQI_TEST_CHECK(k4_xx.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.IsComposing() && engine.RawCode() == L"xxxy");

    // Disable beep_on_empty_4code and enable_key_sound
    engine.SetBeepOnEmpty4Code(false);
    engine.SetEnableKeySound(false);
    engine.Reset();
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    auto k4_silent = engine.ProcessKey('Y');
    QIQI_TEST_CHECK(!k4_silent.empty_4code_warning);
    QIQI_TEST_CHECK(!k4_silent.trigger_key_sound);
    auto sp_raw = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(sp_raw.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(sp_raw.committed_text == L"xxxy");

    // Enable clear_on_empty_4code: 4th code with no match auto-clears composition (Cancel) and still respects beep_on_empty_4code
    engine.SetClearOnEmpty4Code(true);
    engine.SetBeepOnEmpty4Code(true);
    QIQI_TEST_CHECK(engine.ClearOnEmpty4Code());
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    auto k4_clear = engine.ProcessKey('Y');
    QIQI_TEST_CHECK(k4_clear.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(k4_clear.empty_4code_warning);
    QIQI_TEST_CHECK(!engine.IsComposing() && engine.RawCode().empty());

    // Also verify Space on 1..3-code empty composition cancels instead of committing raw code when clear_on_empty_4code is true
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('Y'); // "xxy" has no match
    QIQI_TEST_CHECK(engine.IsComposing() && engine.AllCandidates().empty());
    auto sp_clear = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(sp_clear.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(sp_clear.committed_text.empty());
    QIQI_TEST_CHECK(!engine.IsComposing() && engine.RawCode().empty());

    // Verify clear_on_empty_4code with beep_on_empty_4code disabled clears silently
    engine.SetBeepOnEmpty4Code(false);
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    auto k4_clear_silent = engine.ProcessKey('Y');
    QIQI_TEST_CHECK(k4_clear_silent.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(!k4_clear_silent.empty_4code_warning);
    QIQI_TEST_CHECK(!engine.IsComposing() && engine.RawCode().empty());

    // Verify Enter (VK_RETURN) still commits raw code even when candidates are empty and clear_on_empty_4code is true
    engine.ProcessKey('X');
    engine.ProcessKey('X');
    engine.ProcessKey('Y');
    auto enter_raw = engine.ProcessKey(VK_RETURN);
    QIQI_TEST_CHECK(enter_raw.action == KeyActionType::Commit && enter_raw.committed_text == L"xxy");

    // Verify non-empty 4-code still commits or stays composing normally when clear_on_empty_4code is true
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    engine.ProcessKey('V');
    auto k4_valid = engine.ProcessKey('B'); // "wqvb" -> "你好"
    QIQI_TEST_CHECK(k4_valid.action == KeyActionType::Commit && k4_valid.committed_text == L"你好");

    // Test beep_on_dup_4code: when 4-code has multiple exact matches ("mnop"), dup_4code_warning is set only when enabled
    QIQI_TEST_CHECK(!engine.BeepOnDup4Code());
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    auto r4_no_dup_beep = engine.ProcessKey('P');
    QIQI_TEST_CHECK(!r4_no_dup_beep.dup_4code_warning);
    engine.Reset();

    engine.SetBeepOnDup4Code(true);
    QIQI_TEST_CHECK(engine.BeepOnDup4Code());
    engine.ProcessKey('M');
    engine.ProcessKey('N');
    engine.ProcessKey('O');
    auto r4_dup_beep = engine.ProcessKey('P');
    QIQI_TEST_CHECK(r4_dup_beep.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(r4_dup_beep.dup_4code_warning);
    QIQI_TEST_CHECK(!r4_dup_beep.empty_4code_warning);
    engine.SetBeepOnDup4Code(false);
    engine.Reset();

    // Verify ApplyConfig propagates clear_on_empty_4code and all other engine fields
    QiQiConfig custom_cfg{};
    custom_cfg.page_size = 8;
    custom_cfg.auto_commit_4code = false;
    custom_cfg.push_commit_5th = false;
    custom_cfg.semicolon_quote_quick_select = false;
    custom_cfg.beep_on_empty_4code = false;
    custom_cfg.beep_on_dup_4code = true;
    custom_cfg.clear_on_empty_4code = true;
    custom_cfg.enable_key_sound = true;
    engine.ApplyConfig(custom_cfg);
    QIQI_TEST_CHECK(engine.PageSize() == 8);
    QIQI_TEST_CHECK(!engine.AutoCommit4Code());
    QIQI_TEST_CHECK(!engine.PushCommit5th());
    QIQI_TEST_CHECK(!engine.SemicolonQuoteQuickSelect());
    QIQI_TEST_CHECK(!engine.BeepOnEmpty4Code());
    QIQI_TEST_CHECK(engine.BeepOnDup4Code());
    QIQI_TEST_CHECK(engine.ClearOnEmpty4Code());
    QIQI_TEST_CHECK(engine.EnableKeySound());
    engine.SetClearOnEmpty4Code(false);
    engine.SetBeepOnDup4Code(false);
    engine.SetSemicolonQuoteQuickSelect(true);
    engine.SetAutoCommit4Code(true);
    engine.SetPushCommit5th(true);
    engine.SetEnableKeySound(false);
}

} // namespace

void RunEngineInputTests() {
    TestCompileTimeFsmTransitions();
    TestFirstLevelShortCodes();
    TestFourCodeUniqueAutoCommit();
    TestFifthKeyPushAutoCommit();
    TestWildcardAndBackspaceAndCancel();
    TestNavigationSelectionAndReturnCommit();
}

} // namespace qiqi::test
