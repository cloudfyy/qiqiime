#include "common/test_support.h"
#include "Ime/engine/engine.h"

#include <windows.h>
#include <string>

namespace qiqi::test {

namespace {

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
    QIQI_TEST_CHECK(engine.ShouldEatKey('B', true, false, false));  // Shift+B while composing commits candidate + 'B'
    QIQI_TEST_CHECK(!engine.ShouldEatKey('2', true, false, false)); // Shift+2 has no punctuation mapping

    // Verify Shift+B while composing 'a' ("工") commits "工B" and returns to Idle
    auto shift_b_res = engine.ProcessKey('B', true);
    QIQI_TEST_CHECK(shift_b_res.action == KeyActionType::Commit);
    QIQI_TEST_CHECK(shift_b_res.committed_text == L"工B");
    QIQI_TEST_CHECK(!engine.IsComposing());

    // Verify Numpad candidate selection (VK_NUMPAD1) and Numpad operator/0 protection while composing
    engine.ProcessKey('A');
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_NUMPAD1, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_NUMPAD0, false, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_DECIMAL, false, false, false));
    QIQI_TEST_CHECK(engine.ProcessKey(VK_NUMPAD0, false).action == KeyActionType::UpdateState);
    auto numpad_sel = engine.ProcessKey(VK_NUMPAD1, false);
    QIQI_TEST_CHECK(numpad_sel.action == KeyActionType::Commit && numpad_sel.committed_text == L"工");

    // Start composing again to test ToggleChineseMode reset
    engine.ProcessKey('A');
    QIQI_TEST_CHECK(engine.IsComposing());

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
        {VK_OEM_3, false, L"·"},
        {VK_OEM_3, true, L"～"},
        {VK_OEM_4, false, L"【"},
        {VK_OEM_4, true, L"｛"},
        {VK_OEM_5, false, L"、"},
        {VK_OEM_6, false, L"】"},
        {VK_OEM_6, true, L"｝"},
        {VK_OEM_MINUS, true, L"——"},
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

    // Smart paired double quotes (“ ... ”) and single quotes (‘ ... ’)
    auto dq1 = engine.ProcessKey(VK_OEM_7, true);
    auto dq2 = engine.ProcessKey(VK_OEM_7, true);
    QIQI_TEST_CHECK(dq1.action == KeyActionType::Commit && dq1.committed_text == L"“");
    QIQI_TEST_CHECK(dq2.action == KeyActionType::Commit && dq2.committed_text == L"”");

    auto sq1 = engine.ProcessKey(VK_OEM_7, false);
    auto sq2 = engine.ProcessKey(VK_OEM_7, false);
    QIQI_TEST_CHECK(sq1.action == KeyActionType::Commit && sq1.committed_text == L"‘");
    QIQI_TEST_CHECK(sq2.action == KeyActionType::Commit && sq2.committed_text == L"’");

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

    // Toggle Chinese punctuation off (Ctrl+. behavior): punctuation keys should PassThrough in half-width mode
    QIQI_TEST_CHECK(engine.IsChinesePunctuation());
    engine.ToggleChinesePunctuation();
    QIQI_TEST_CHECK(!engine.IsChinesePunctuation());
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_OEM_PERIOD, false, false, false));
    QIQI_TEST_CHECK(engine.ProcessKey(VK_OEM_PERIOD, false).action == KeyActionType::PassThrough);
    engine.ToggleChinesePunctuation();
    QIQI_TEST_CHECK(engine.IsChinesePunctuation());
}

void TestFullWidthMode() {
    static_assert(AsciiToFullWidth(' ') == L'\u3000');
    static_assert(AsciiToFullWidth('\n') == L'\n');
    static_assert(MapFullWidthChar(VK_SPACE, false) == L'\u3000');
    static_assert(MapFullWidthChar('A', true) == L'Ａ');
    static_assert(MapFullWidthChar('1', false) == L'１');
    static_assert(MapFullWidthChar('2', true) == L'＠');

    // Runtime verification of AsciiToFullWidth and all OEM keys in MapFullWidthChar
    QIQI_TEST_CHECK(AsciiToFullWidth(' ') == L'\u3000');
    QIQI_TEST_CHECK(AsciiToFullWidth('\n') == L'\n');
    for (uint32_t vk : {VK_OEM_1, VK_OEM_PLUS, VK_OEM_COMMA, VK_OEM_MINUS, VK_OEM_PERIOD,
                        VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_5, VK_OEM_6, VK_OEM_7}) {
        QIQI_TEST_CHECK(MapFullWidthChar(vk, false).has_value());
        QIQI_TEST_CHECK(MapFullWidthChar(vk, true).has_value());
    }
    QIQI_TEST_CHECK(!MapFullWidthChar(VK_F1, false).has_value());

    WubiEngine engine;
    engine.SetFullWidthMode(true);
    QIQI_TEST_CHECK(engine.IsFullWidthMode());
    engine.SetChinesePunctuation(false);
    QIQI_TEST_CHECK(!engine.IsChinesePunctuation());
    engine.SetChinesePunctuation(true);
    engine.SetAutoCommit4Code(true);
    engine.SetPushCommit5th(true);

    // In Chinese Idle + FullWidth mode, Space, digits, and Shift+A produce full-width characters
    auto fw_space = engine.ProcessKey(VK_SPACE, false);
    QIQI_TEST_CHECK(fw_space.action == KeyActionType::Commit && fw_space.committed_text == L"　");

    auto fw_digit = engine.ProcessKey('1', false);
    QIQI_TEST_CHECK(fw_digit.action == KeyActionType::Commit && fw_digit.committed_text == L"１");

    auto fw_numpad = engine.ProcessKey(VK_NUMPAD5, false);
    QIQI_TEST_CHECK(fw_numpad.action == KeyActionType::Commit && fw_numpad.committed_text == L"５");
    auto fw_numpad_dec = engine.ProcessKey(VK_DECIMAL, false);
    QIQI_TEST_CHECK(fw_numpad_dec.action == KeyActionType::Commit && fw_numpad_dec.committed_text == L"．");

    auto fw_upper = engine.ProcessKey('A', true);
    QIQI_TEST_CHECK(fw_upper.action == KeyActionType::Commit && fw_upper.committed_text == L"Ａ");

    // While composing 'a' ("工"), typing Shift+2 ('@', which is not a Chinese punctuation key)
    // in FullWidth mode commits current candidate + full-width '＠'
    engine.ProcessKey('A', false);
    auto fw_comp_sym = engine.ProcessKey('2', true);
    QIQI_TEST_CHECK(fw_comp_sym.action == KeyActionType::Commit && fw_comp_sym.committed_text == L"工＠");

    // In English + FullWidth mode, lowercase 'a' also produces full-width 'ａ'
    engine.SetChineseMode(false);
    QIQI_TEST_CHECK(engine.ShouldEatKey('A', false, false, false));
    auto fw_lower = engine.ProcessKey('A', false);
    QIQI_TEST_CHECK(fw_lower.action == KeyActionType::Commit && fw_lower.committed_text == L"ａ");

    engine.ToggleFullWidthMode();
    QIQI_TEST_CHECK(!engine.IsFullWidthMode());

    // Test 'z'-guided temporary English / URL / identifier input mode in Chinese Idle state
    engine.SetChineseMode(true);
    QIQI_TEST_CHECK(engine.EnableZTempEnglish());
    QIQI_TEST_CHECK(!engine.IsTempEnglishMode());

    auto z_start = engine.ProcessKey('Z');
    QIQI_TEST_CHECK(z_start.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.State() == EngineState::TempEnglish);
    QIQI_TEST_CHECK(engine.IsTempEnglishMode() && engine.IsComposing());
    QIQI_TEST_CHECK(engine.RawCode() == L"z");
    QIQI_TEST_CHECK(engine.ShouldEatKey('G', true, false, false));
    QIQI_TEST_CHECK(engine.ShouldEatKey(VK_OEM_PERIOD, false, false, false));
    QIQI_TEST_CHECK(!engine.ShouldEatKey('C', false, true, false)); // Ctrl+C not eaten
    QIQI_TEST_CHECK(!engine.ShouldEatKey(VK_F1, false, false, false));
    QIQI_TEST_CHECK(engine.ProcessKey('C', false, true, false).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_F1).action == KeyActionType::PassThrough);

    // Type "Git.1" inside temporary English mode, test Backspace, then commit with Space
    engine.ProcessKey('G', true);
    engine.ProcessKey('I', false);
    engine.ProcessKey('T', false);
    engine.ProcessKey(VK_OEM_PERIOD, false);
    engine.ProcessKey('1', false);
    QIQI_TEST_CHECK(engine.RawCode() == L"zGit.1");
    engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(engine.RawCode() == L"zGit.");
    engine.ProcessKey('C', false);
    auto z_commit = engine.ProcessKey(VK_SPACE);
    QIQI_TEST_CHECK(z_commit.action == KeyActionType::Commit && z_commit.committed_text == L"Git.c");
    QIQI_TEST_CHECK(!engine.IsTempEnglishMode() && !engine.IsComposing());

    // Test Backspace on 1-char temp English returns to "z" ("(英文模式)"), and 2nd Backspace cancels composition
    engine.ProcessKey('Z');
    engine.ProcessKey('A');
    auto z_back_to_root = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(z_back_to_root.action == KeyActionType::UpdateState &&
                    engine.IsTempEnglishMode() &&
                    engine.RawCode() == L"z" &&
                    !engine.AllDisplayCandidates().empty() &&
                    engine.AllDisplayCandidates().front() == L"(英文模式)");
    auto z_back_cancel = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(z_back_cancel.action == KeyActionType::Cancel && !engine.IsTempEnglishMode());

    // Pressing Enter immediately after 'Z' commits literal "z"; pressing Enter after typing commits payload
    engine.ProcessKey('Z');
    auto z_only_enter = engine.ProcessKey(VK_RETURN);
    QIQI_TEST_CHECK(z_only_enter.action == KeyActionType::Commit && z_only_enter.committed_text == L"z");

    engine.ProcessKey('Z');
    engine.ProcessKey('O', true);
    engine.ProcessKey('K', false);
    auto z_enter_commit = engine.ProcessKey(VK_RETURN);
    QIQI_TEST_CHECK(z_enter_commit.action == KeyActionType::Commit && z_enter_commit.committed_text == L"Ok");

    engine.ProcessKey('Z');
    engine.ProcessKey('A');
    auto z_esc_cancel = engine.ProcessKey(VK_ESCAPE);
    QIQI_TEST_CHECK(z_esc_cancel.action == KeyActionType::Cancel && !engine.IsTempEnglishMode());

    // When enable_z_temp_english is disabled, 'Z' in Idle enters normal wildcard composition
    engine.SetEnableZTempEnglish(false);
    QIQI_TEST_CHECK(!engine.EnableZTempEnglish());
    engine.ProcessKey('Z');
    QIQI_TEST_CHECK(!engine.IsTempEnglishMode() && engine.IsComposing());
    engine.Reset();
    engine.SetEnableZTempEnglish(true);
}

} // namespace

void RunEngineModesPunctTests() {
    TestModeSwitchingAndShouldEatKey();
    TestChinesePunctuation();
    TestFullWidthMode();
}

} // namespace qiqi::test
