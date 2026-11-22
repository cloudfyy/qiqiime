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

void TestCompileTimeFsmTransitions() {
    // Exhaustive compile-time checks across all states and key events
    static_assert(ClassifyKeyEvent('A', false, false, false) == KeyEventKind::CodeChar);
    static_assert(ClassifyKeyEvent('A', true, false, false) == KeyEventKind::UpperChar);
    static_assert(ClassifyKeyEvent('A', false, true, false) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_SPACE, false, false, false) == KeyEventKind::Space);
    static_assert(ClassifyKeyEvent(VK_RETURN, false, false, false) == KeyEventKind::Enter);
    static_assert(ClassifyKeyEvent(VK_BACK, false, false, false) == KeyEventKind::Backspace);
    static_assert(ClassifyKeyEvent(VK_ESCAPE, false, false, false) == KeyEventKind::Escape);
    static_assert(ClassifyKeyEvent('1', false, false, false) == KeyEventKind::None);
    static_assert(
        ClassifyKeyEvent('1', false, false, false, true, false, true, false, EngineState::Composing1) ==
        KeyEventKind::DigitSelect);
    static_assert(ClassifyKeyEvent('1', true, false, false) == KeyEventKind::ChinesePunct);
    static_assert(ClassifyKeyEvent(VK_UP, false, false, false) == KeyEventKind::CandidatePrev);
    static_assert(
        ClassifyKeyEvent(VK_UP, false, false, false, true, false, true, false, EngineState::Composing1, false, true) ==
        KeyEventKind::PageUp);
    static_assert(ClassifyKeyEvent(VK_LEFT, false, false, false) == KeyEventKind::CandidatePrev);
    static_assert(
        ClassifyKeyEvent(VK_LEFT, false, false, false, true, false, true, false, EngineState::Composing1, false, true) ==
        KeyEventKind::CandidatePrev);
    static_assert(ClassifyKeyEvent(VK_DOWN, false, false, false) == KeyEventKind::CandidateNext);
    static_assert(
        ClassifyKeyEvent(VK_DOWN, false, false, false, true, false, true, false, EngineState::Composing1, false, true) ==
        KeyEventKind::PageDown);
    static_assert(ClassifyKeyEvent(VK_RIGHT, false, false, false) == KeyEventKind::CandidateNext);
    static_assert(
        ClassifyKeyEvent(VK_RIGHT, false, false, false, true, false, true, false, EngineState::Composing1, false, true) ==
        KeyEventKind::CandidateNext);
    static_assert(ClassifyKeyEvent(VK_OEM_MINUS, false, false, false) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_OEM_PLUS, false, false, false) == KeyEventKind::None);
    static_assert(
        ClassifyKeyEvent(VK_OEM_MINUS, false, false, false, true, false, true, false, EngineState::Composing1) ==
        KeyEventKind::PageUp);
    static_assert(
        ClassifyKeyEvent(VK_OEM_PLUS, false, false, false, true, false, true, false, EngineState::Composing1) ==
        KeyEventKind::PageDown);
    static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::CandidatePrev).eat_key);
    static_assert(!LookupTransition(EngineState::Idle, KeyEventKind::CandidateNext).eat_key);
    static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::CandidatePrev).eat_key);
    static_assert(LookupTransition(EngineState::Composing1, KeyEventKind::CandidateNext).eat_key);

    // CapsLock compile-time FSM checks
    static_assert(ClassifyKeyEvent('A', false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent('Z', false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    static_assert(ClassifyKeyEvent(VK_OEM_PERIOD, false, false, false, true, false, true, false, EngineState::Idle, true) == KeyEventKind::None);
    const uint32_t non_digit_vk = ::GetCurrentProcessId() != 0 ? uint32_t{'A'} : uint32_t{0};
    QIQI_TEST_CHECK(!ResolveCandidateSelectIndex(non_digit_vk, false, false).has_value());
    WubiEngine idle_engine;
    QIQI_TEST_CHECK(idle_engine.CommitCandidate(0).action == KeyActionType::PassThrough);
    QIQI_TEST_CHECK(idle_engine.SelectCandidate(0).action ==
                    KeyActionType::PassThrough);
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
    constexpr size_t max_items = (std::numeric_limits<size_t>::max)();
    static_assert(ComputeTotalPages(max_items, 2) == max_items / 2 + 1);
    static_assert(ComputeTotalPages(max_items, max_items) == 1);
    static_assert(ComputePageRange(max_items, 2, max_items / 2) ==
        std::pair<size_t, size_t>{max_items - 1, max_items});
    static_assert(ComputePageRange(7, max_items, 0) == std::pair<size_t, size_t>{0, 7});
    static_assert(ComputePageRange(7, 2, max_items / 2 + 1) ==
        std::pair<size_t, size_t>{0, 0});
    static_assert(ComputePageRange(7, 0, max_items) == std::pair<size_t, size_t>{0, 0});
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
    WubiEngine selection_engine;
    const std::map<uint32_t, std::vector<std::wstring>> entries{
        {PackWubiCode(L"wq"), {L"候选一", L"候选二", L"候选三"}}
    };
    std::vector<uint8_t> bytes;
    WubiMappedDict::CompileEntriesToBinaryBuffer(entries, bytes);
    QIQI_TEST_CHECK(
        selection_engine.LoadDictionaryFromBinaryBuffer(std::move(bytes)));
    selection_engine.SetPageSize(2);
    selection_engine.ProcessKey('W');
    selection_engine.ProcessKey('Q');
    QIQI_TEST_CHECK(selection_engine.AllCandidates().size() > 2);
    const auto selected = selection_engine.SelectCandidate(2);
    QIQI_TEST_CHECK(selected.action == KeyActionType::UpdateState &&
                    selected.current_page == 1 &&
                    selected.selected_index == 0);
    QIQI_TEST_CHECK(selection_engine.SelectCandidate(
                        selection_engine.AllCandidates().size()).action ==
                    KeyActionType::PassThrough);

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

void TestResetKeyPrediction() {
    static_assert(StateAfterCompositionReset(EngineState::English) == EngineState::English);
    static_assert(StateAfterCompositionReset(EngineState::Idle) == EngineState::Idle);
    static_assert(StateAfterCompositionReset(EngineState::Composing4) == EngineState::Idle);
    static_assert(StateAfterCompositionReset(EngineState::TempEnglish) == EngineState::Idle);
    constexpr std::array<std::pair<EngineState, std::wstring_view>, 7> seeds = {{
        {EngineState::English, L""},
        {EngineState::Idle, L""},
        {EngineState::Composing1, L"a"},
        {EngineState::Composing2, L"ab"},
        {EngineState::Composing3, L"abc"},
        {EngineState::Composing4, L"abcz"},
        {EngineState::TempEnglish, L"z"}
    }};
    for (const auto& entry : seeds) {
        const EngineState state = entry.first;
        const std::wstring_view seed = entry.second;
        for (const bool full_width : {false, true}) {
            QiQiConfig config{};
            config.default_chinese_mode = state != EngineState::English;
            config.default_full_width = full_width;
            config.auto_commit_4code = false;
            config.clear_on_empty_4code = false;
            config.enable_z_temp_english = true;
            config.semicolon_quote_quick_select = true;
            WubiEngine engine;
            engine.ApplyConfig(config);
            for (const wchar_t code : seed) engine.ProcessKey(gsl::narrow_cast<uint32_t>(code - L'a' + L'A'));
            QIQI_TEST_CHECK(engine.State() == state);
            const auto candidates = engine.AllCandidates();
            const auto page = engine.CurrentPage();
            const auto selected = engine.SelectedInPage();
            WubiEngine reset_engine;
            reset_engine.ApplyConfig(config);
            reset_engine.Reset();
            for (uint32_t modifiers = 0; modifiers < 16; ++modifiers) {
                const bool shift = (modifiers & 1) != 0;
                const bool ctrl = (modifiers & 2) != 0;
                const bool alt = (modifiers & 4) != 0;
                const bool caps = (modifiers & 8) != 0;
                for (uint32_t vk = 0; vk < 256; ++vk) {
                    QIQI_TEST_CHECK(engine.ShouldEatKeyAfterReset(vk, shift, ctrl, alt, caps) ==
                                    reset_engine.ShouldEatKey(vk, shift, ctrl, alt, caps));
                }
            }
            QIQI_TEST_CHECK(engine.State() == state && engine.RawCode() == seed && engine.AllCandidates() == candidates);
            QIQI_TEST_CHECK(engine.CurrentPage() == page && engine.SelectedInPage() == selected);
            engine.Reset();
            QIQI_TEST_CHECK(engine.State() == StateAfterCompositionReset(state) && engine.RawCode().empty());
            QIQI_TEST_CHECK(engine.AllCandidates().empty() && engine.CurrentPage() == 0 && engine.SelectedInPage() == 0);
            QIQI_TEST_CHECK(engine.IsFullWidthMode() == full_width && engine.IsChineseMode() == config.default_chinese_mode);
        }
    }
}

namespace {

void CheckCompositionSnapshot(const WubiEngine& engine, const KeyProcessResult& result) {
    QIQI_TEST_CHECK(result.composition_text == engine.RawCode());
    QIQI_TEST_CHECK(result.page_candidates == engine.CurrentPageCandidates());
    QIQI_TEST_CHECK(result.all_display_candidates == engine.AllDisplayCandidates());
    QIQI_TEST_CHECK(result.current_page == engine.CurrentPage());
    QIQI_TEST_CHECK(result.selected_index == engine.SelectedInPage());
    QIQI_TEST_CHECK(result.total_pages == engine.TotalPages());
}
} // namespace

void TestRuntimeFsmOutcomes() {
    struct FourCodeCase {
        std::wstring_view code;
        bool unique;
        bool empty;
        bool duplicate;
    };
    constexpr std::array<FourCodeCase, 4> cases = {{
        {L"abcd", true, false, false},
        {L"efgh", false, false, true},
        {L"abcz", false, false, false},
        {L"yyyx", false, true, false}
    }};
    constexpr std::array append_states = {
        EngineState::Composing1, EngineState::Composing2, EngineState::Composing3
    };
    for (const auto& test : cases) {
        for (const bool auto_commit : {false, true}) {
            for (const bool clear_empty : {false, true}) {
                for (const bool beep : {false, true}) {
                    for (const bool push : {false, true}) {
                        WubiEngine engine;
                        engine.AddEntry(L"abcd", L"unique");
                        engine.AddEntry(L"efgh", L"first");
                        engine.AddEntry(L"efgh", L"second");
                        engine.SetAutoCommit4Code(auto_commit);
                        engine.SetClearOnEmpty4Code(clear_empty);
                        engine.SetBeepOnEmpty4Code(beep);
                        engine.SetBeepOnDup4Code(beep);
                        engine.SetPushCommit5th(push);
                        engine.SetEnableKeySound(true);
                        engine.SetPageSize(7);
                        for (size_t i = 0; i < 3; ++i) {
                            const auto key = gsl::narrow_cast<uint32_t>(gsl::at(test.code, i) - L'a' + L'A');
                            const auto result = engine.ProcessKey(key);
                            QIQI_TEST_CHECK(engine.State() == gsl::at(append_states, i));
                            QIQI_TEST_CHECK(result.action == KeyActionType::UpdateState);
                            CheckCompositionSnapshot(engine, result);
                        }
                        QIQI_TEST_CHECK(engine.ShouldEatKey(
                            gsl::narrow_cast<uint32_t>(test.code.back() - L'a' + L'A'), false, false, false));
                        QIQI_TEST_CHECK(engine.State() == EngineState::Composing3 && engine.RawCode() == test.code.substr(0, 3));
                        const auto fourth = engine.ProcessKey(gsl::narrow_cast<uint32_t>(test.code.back() - L'a' + L'A'));
                        CheckCompositionSnapshot(engine, fourth);
                        QIQI_TEST_CHECK(fourth.page_size == 7 && fourth.trigger_key_sound);
                        QIQI_TEST_CHECK(fourth.empty_4code_warning == (test.empty && beep));
                        QIQI_TEST_CHECK(fourth.dup_4code_warning == (test.duplicate && beep));
                        if (test.unique && auto_commit) {
                            QIQI_TEST_CHECK(engine.State() == EngineState::Idle && engine.RawCode().empty());
                            QIQI_TEST_CHECK(fourth.action == KeyActionType::Commit && fourth.committed_text == L"unique");
                        } else if (test.empty && clear_empty) {
                            QIQI_TEST_CHECK(engine.State() == EngineState::Idle && engine.RawCode().empty());
                            QIQI_TEST_CHECK(fourth.action == KeyActionType::Cancel && fourth.committed_text.empty());
                        } else {
                            QIQI_TEST_CHECK(engine.State() == EngineState::Composing4 && engine.RawCode() == test.code);
                            QIQI_TEST_CHECK(fourth.action == KeyActionType::UpdateState && fourth.committed_text.empty());
                            const auto selected = engine.CurrentPageCandidates();
                            const std::wstring expected_commit = selected.empty() ? std::wstring(test.code) : selected.front();
                            const auto fifth = engine.ProcessKey('Q');
                            CheckCompositionSnapshot(engine, fifth);
                            QIQI_TEST_CHECK(fifth.trigger_key_sound && fifth.page_size == 7);
                            QIQI_TEST_CHECK(!fifth.empty_4code_warning && !fifth.dup_4code_warning);
                            if (push) {
                                QIQI_TEST_CHECK(engine.State() == EngineState::Composing1 && engine.RawCode() == L"q");
                                QIQI_TEST_CHECK(fifth.action == KeyActionType::Commit && fifth.committed_text == expected_commit);
                            } else {
                                QIQI_TEST_CHECK(engine.State() == EngineState::Composing4 && engine.RawCode() == test.code);
                                QIQI_TEST_CHECK(fifth.action == KeyActionType::UpdateState && fifth.committed_text.empty());
                            }
                        }
                    }
                }
            }
        }
    }
}

void TestRuntimeFsmCompletion() {
    WubiEngine engine;
    engine.SetAutoCommit4Code(false);
    engine.SetEnableKeySound(true);
    constexpr std::array composing_states = {
        EngineState::Composing1, EngineState::Composing2, EngineState::Composing3, EngineState::Composing4
    };
    const std::wstring_view code = L"abcd";
    for (size_t length = 1; length <= code.size(); ++length) {
        engine.Reset();
        for (const wchar_t ch : code.substr(0, length)) engine.ProcessKey(gsl::narrow_cast<uint32_t>(ch - L'a' + L'A'));
        const auto state = gsl::at(composing_states, length - 1);
        for (const uint32_t key : {VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_PRIOR, VK_NEXT, int{'9'}}) {
            QIQI_TEST_CHECK(engine.ShouldEatKey(key, false, false, false));
            const auto result = engine.ProcessKey(key);
            QIQI_TEST_CHECK(engine.State() == state && engine.RawCode() == code.substr(0, length));
            QIQI_TEST_CHECK(result.action == KeyActionType::UpdateState);
            CheckCompositionSnapshot(engine, result);
        }
        const auto ignored = engine.ProcessKey('A', false, true, false);
        QIQI_TEST_CHECK(ignored.action == KeyActionType::PassThrough && ignored.composition_text.empty());
        QIQI_TEST_CHECK(engine.State() == state && engine.RawCode() == code.substr(0, length));
        for (size_t remaining = length; remaining > 0; --remaining) {
            const auto result = engine.ProcessKey(VK_BACK);
            CheckCompositionSnapshot(engine, result);
            QIQI_TEST_CHECK(engine.RawCode() == code.substr(0, remaining - 1));
            if (remaining > 1) {
                QIQI_TEST_CHECK(engine.State() == gsl::at(composing_states, remaining - 2));
                QIQI_TEST_CHECK(result.action == KeyActionType::UpdateState && result.trigger_key_sound);
            } else {
                QIQI_TEST_CHECK(engine.State() == EngineState::Idle);
                QIQI_TEST_CHECK(result.action == KeyActionType::Cancel && !result.trigger_key_sound);
            }
        }
    }

    engine.SetPageSize(7);
    engine.ProcessKey('A');
    const auto cancel = engine.ProcessKey(VK_ESCAPE);
    QIQI_TEST_CHECK(engine.State() == EngineState::Idle && !cancel.trigger_key_sound && cancel.page_size == 5);
    CheckCompositionSnapshot(engine, cancel);
    engine.ProcessKey('Z');
    engine.ProcessKey('A');
    const auto digit = engine.ProcessKey('1');
    QIQI_TEST_CHECK(engine.State() == EngineState::TempEnglish && digit.composition_text == L"za1");
    CheckCompositionSnapshot(engine, digit);
    for (const auto raw : {L"za", L"z"}) {
        const auto result = engine.ProcessKey(VK_BACK);
        QIQI_TEST_CHECK(engine.State() == EngineState::TempEnglish && engine.RawCode() == raw);
        CheckCompositionSnapshot(engine, result);
    }
    const auto cancel_guide = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(engine.State() == EngineState::Idle && cancel_guide.action == KeyActionType::Cancel);
    QIQI_TEST_CHECK(cancel_guide.trigger_key_sound && cancel_guide.page_size == 7);
    CheckCompositionSnapshot(engine, cancel_guide);

    for (const bool chinese : {false, true}) {
        engine.SetChineseMode(chinese);
        engine.SetFullWidthMode(true);
        const auto result = engine.ProcessKey('A', false, false, false, true);
        QIQI_TEST_CHECK(engine.State() == (chinese ? EngineState::Idle : EngineState::English));
        QIQI_TEST_CHECK(result.action == KeyActionType::Commit && result.committed_text == L"\uFF21");
        CheckCompositionSnapshot(engine, result);
        engine.Reset();
        QIQI_TEST_CHECK(engine.State() == (chinese ? EngineState::Idle : EngineState::English));
    }
}
} // namespace qiqi::test
