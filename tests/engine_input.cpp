#include "common.h"
#include "engine/wubi_engine.h"

#include <windows.h>
#include <string>

namespace qiqi::test {

namespace {

void TestCompileTimeFsmTransitions() {
    // Exhaustive compile-time checks across all states and key events
    static_assert(ClassifyKeyEvent('A', false, false, false) == KeyEventKind::CodeChar);
    static_assert(ClassifyKeyEvent('A', true, false, false) == KeyEventKind::None);
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

    // Out-of-bounds state/event safety fallback
    static_assert(!LookupTransition(EngineState::Count, KeyEventKind::CodeChar).eat_key);
    static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::Count).eat_key);

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
}

void TestWildcardAndBackspaceAndCancel() {
    WubiEngine engine;
    // "wqz" wildcard query should match "你" (wqi / wqiy)
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    auto rz = engine.ProcessKey('Z');
    QIQI_TEST_CHECK(rz.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(!rz.page_candidates.empty());

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

    // Test VK_RETURN committing raw ASCII code
    engine.ProcessKey('A');
    engine.ProcessKey('B');
    auto ret_commit = engine.ProcessKey(VK_RETURN);
    QIQI_TEST_CHECK(ret_commit.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(ret_commit.committed_text == L"ab");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Test VK_SPACE when no candidate exists (commits raw code)
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    auto sp_raw = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(sp_raw.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(sp_raw.committed_text == L"yyy");
}

void TestModeSwitchingAndShouldEatKey() {
    WubiEngine engine;
    QIQI_TEST_CHECK(engine.IsChineseMode());

    // Idle state ShouldEatKey checks
    QIQI_TEST_CHECK(engine.ShouldEatKey('A', false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey('A', true, false, false));   // Shift+A is not eaten when idle
    QIQI_TEST_CHECK(!engine.ShouldEatKey('A', false, true, false));   // Ctrl+A not eaten
    QIQI_TEST_CHECK(!engine.ShouldEatKey('A', false, false, true));   // Alt+A not eaten
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_SPACE, false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_RETURN, false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey('1', false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey('1', true, false, false));    // Shift+1 -> '！'
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_COMMA, false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_F1, false, false, false));

    // Composing state ShouldEatKey checks
    engine.ProcessKey('A');
    QIQI_TEST_CHECK(engine.IsComposing());
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_SPACE, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_RETURN, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_ESCAPE, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_BACK, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_UP, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_DOWN, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_PRIOR, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_NEXT, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_MINUS, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_PLUS, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey('1', false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey('2', true, false, false)); // Shift+2 has no punctuation mapping

    // Toggle to English mode while composing -> resets composition and stops eating keys
    engine.ToggleChineseMode();
    QIQI_TEST_CHECK(!engine.IsChineseMode());
    QIQI_TEST_CHECK(!engine.IsComposing());
    QIQI_TEST_CHECK(!engine.ShouldEatKey('A', false, false, false));
    QIQI_TEST_CHECK(engine.ProcessKey('A').action == KeyActionType::PassThrough);

    // Toggle back to Chinese mode
    engine.ToggleChineseMode();
    QIQI_TEST_CHECK(engine.IsChineseMode());
    QIQI_TEST_CHECK(engine.ProcessKey('A', false, true, false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_F1).action == KeyActionType::PassThrough);
}

void TestChinesePunctuation() {
    WubiEngine engine;

    struct PunctCase {
        uint32_t vk;
        bool shift;
        const wchar_t* expected;
    };
    const PunctCase cases[] = {
        {VK_OEM_COMMA, false, L"，"},
        {VK_OEM_COMMA, true, L"《"},
        {VK_OEM_PERIOD, false, L"。"},
        {VK_OEM_PERIOD, true, L"》"},
        {VK_OEM_1, false, L"；"},
        {VK_OEM_1, true, L"："},
        {VK_OEM_2, false, L"、"},
        {VK_OEM_2, true, L"？"},
        {'1', true, L"！"},
        {'4', true, L"￥"},
        {'6', true, L"……"},
        {'9', true, L"（"},
        {'0', true, L"）"},
    };

    for (const auto& c : cases) {
        auto res = engine.ProcessKey(c.vk, c.shift);
        QIQI_TEST_CHECK(res.action == KeyActionType::Commit);
        QIQI_TEST_CHECK(res.committed_text == c.expected);
    }

    // Unshifted number keys when idle should PassThrough
    QIQI_TEST_CHECK(engine.ProcessKey('1', false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey('4', false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey('6', false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey('9', false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey('0', false).action == KeyActionType::PassThrough);

    // Punctuation while composing with candidate commits candidate + punctuation
    engine.ProcessKey('A'); // "工"
    auto comp_punct = engine.ProcessKey(VK_OEM_PERIOD, false);
    QIQI_TEST_CHECK(comp_punct.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(comp_punct.committed_text == L"工。");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Punctuation while composing with NO candidate commits raw_code + punctuation
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    engine.ProcessKey('Y');
    auto raw_punct = engine.ProcessKey(VK_OEM_COMMA, false);
    QIQI_TEST_CHECK(raw_punct.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(raw_punct.committed_text == L"yyy，");
    QIQI_TEST_CHECK(!engine.IsComposing());
}

} // namespace

void RunEngineInputTests() {
    TestCompileTimeFsmTransitions();
    TestFirstLevelShortCodes();
    TestFourCodeUniqueAutoCommit();
    TestFifthKeyPushAutoCommit();
    TestWildcardAndBackspaceAndCancel();
    TestNavigationSelectionAndReturnCommit();
    TestModeSwitchingAndShouldEatKey();
    TestChinesePunctuation();
}

} // namespace qiqi::test
