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

void TestCodeCompletionInput() {
    WubiEngine engine;
    engine.AddEntry(L"xxy", L"简码");
    engine.AddEntry(L"xxya", L"全码");
    const auto type = [&](std::wstring_view code) {
        engine.Reset();
        for (const wchar_t ch : code) {
            const auto key = gsl::narrow_cast<uint32_t>(ch - L'a' + L'A');
            QIQI_TEST_CHECK(engine.ShouldEatKey(key, false, false, false));
            QIQI_TEST_CHECK(engine.ProcessKey(key).action != KeyActionType::PassThrough);
        }
    };
    for (const bool enabled : {true, false}) {
        for (const bool hints : {true, false}) {
            QiQiConfig config;
            config.enable_code_completion = enabled;
            config.show_code_hint = hints;
            engine.ApplyConfig(config);
            type(L"xxy");
            QIQI_TEST_CHECK(engine.State() == EngineState::Composing3);
            QIQI_TEST_CHECK(engine.AllCandidates().size() == (enabled ? 2u : 1u));
            if (!enabled || !hints) {
                QIQI_TEST_CHECK(engine.AllDisplayCandidates() == engine.AllCandidates());
            }
            QIQI_TEST_CHECK(engine.ShouldEatKey(VK_SPACE, false, false, false));
            QIQI_TEST_CHECK(engine.ProcessKey(VK_SPACE).committed_text == L"简码");
            type(L"xxy");
            QIQI_TEST_CHECK(engine.ProcessKey('1').committed_text == L"简码");
            type(L"xxy");
            QIQI_TEST_CHECK(engine.CommitCandidate(0).committed_text == L"简码");
            type(L"xxy");
            QIQI_TEST_CHECK(engine.ProcessKey('A').committed_text == L"全码");
            QIQI_TEST_CHECK(engine.State() == EngineState::Idle);

            type(L"xxy");
            QIQI_TEST_CHECK(engine.ProcessKey(VK_BACK).composition_text == L"xx");
            QIQI_TEST_CHECK(engine.AllCandidates().empty() == !enabled);
            QIQI_TEST_CHECK(engine.ProcessKey(VK_ESCAPE).action == KeyActionType::Cancel);

            type(L"zhello");
            QIQI_TEST_CHECK(engine.IsTempEnglishMode());
            QIQI_TEST_CHECK(engine.ProcessKey(VK_SPACE).committed_text == L"hello");
        }
    }
    engine.SetEnableCodeCompletion(false);
    for (const bool clear : {false, true}) {
        engine.SetClearOnEmpty4Code(clear);
        type(L"xx");
        QIQI_TEST_CHECK(engine.AllCandidates().empty());
        QIQI_TEST_CHECK(engine.ShouldEatKey('1', false, false, false));
        QIQI_TEST_CHECK(engine.ProcessKey('1').action == KeyActionType::UpdateState);
        QIQI_TEST_CHECK(engine.CommitCandidate(0).action == KeyActionType::UpdateState);
        QIQI_TEST_CHECK(engine.ShouldEatKey(VK_SPACE, false, false, false));
        const auto space = engine.ProcessKey(VK_SPACE);
        QIQI_TEST_CHECK(space.action == (clear ? KeyActionType::Cancel : KeyActionType::Commit));
        QIQI_TEST_CHECK(space.committed_text == (clear ? L"" : L"xx"));
        QIQI_TEST_CHECK(engine.State() == EngineState::Idle);
        type(L"xx");
        QIQI_TEST_CHECK(engine.ShouldEatKey(VK_RETURN, false, false, false));
        QIQI_TEST_CHECK(engine.ProcessKey(VK_RETURN).committed_text == L"xx");
    }
}

void TestSingleCharacterInput() {
    WubiEngine engine;
    engine.AddEntry(L"xxy", L"甲");
    engine.AddEntry(L"xxy", L"词组");
    engine.AddEntry(L"xxya", L"\U00020000");
    engine.PinEntry(L"xxya", L"置顶词");
    const auto type = [&](std::string_view keys) {
        engine.Reset();
        for (const unsigned char key : keys) {
            QIQI_TEST_CHECK(engine.ProcessKey(key).action == KeyActionType::UpdateState);
        }
    };
    type("XXYA");
    QIQI_TEST_CHECK(engine.AllCandidates().size() == 2 && engine.AllCandidates().front() == L"置顶词");
    const auto filtered = engine.SetSingleCharacterMode(true);
    QIQI_TEST_CHECK(filtered.action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(filtered.composition_text == L"xxya" && engine.State() == EngineState::Composing4);
    QIQI_TEST_CHECK(filtered.all_candidates == std::vector<std::wstring>{L"\U00020000"});
    QIQI_TEST_CHECK(engine.CommitCandidate(1).action == KeyActionType::UpdateState);
    QIQI_TEST_CHECK(engine.CommitCandidate(0).committed_text == L"\U00020000");

    type("XXY");
    QIQI_TEST_CHECK(engine.ProcessKey('A').committed_text == L"\U00020000");
    const auto restored = engine.SetSingleCharacterMode(false);
    QIQI_TEST_CHECK(restored.action == KeyActionType::PassThrough);
    type("XXYA");
    QIQI_TEST_CHECK(engine.AllCandidates().size() == 2 && engine.AllCandidates().front() == L"置顶词");

    QiQiConfig config;
    config.single_character_mode = true;
    config.page_size = 1;
    config.space_enter_mode = WubiSpaceEnterMode::EnterSelectSpaceClear;
    engine.Reset();
    engine.ApplyConfig(config);
    type("XXY");
    QIQI_TEST_CHECK(engine.TotalPages() == 2);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_NEXT).current_page == 1);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_RETURN).committed_text == L"\U00020000");
    type("ZX");
    QIQI_TEST_CHECK(engine.ProcessKey(VK_RETURN).committed_text == L"x");
    QIQI_TEST_CHECK(engine.ProcessKey(VK_OEM_PERIOD).committed_text == L"。");
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
} // namespace qiqi::test
