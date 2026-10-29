#include "engine/wubi_engine.h"
#include "common/logger.h"
#include "common/version.h"
#include <windows.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#define QIQI_TEST_CHECK(expr)                                                                   \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";     \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

using namespace qiqi;

namespace {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 1, 0});
    static_assert(version::SemanticVersion{1, 0, 0} > version::kCurrentVersion);
    QIQI_TEST_CHECK(!version::kVersion.empty());
    QIQI_TEST_CHECK(!version::kVersionW.empty());
    QIQI_TEST_CHECK(!version::kFullVersion.empty());
    QIQI_TEST_CHECK(!version::kFullVersionW.empty());
    QIQI_TEST_CHECK(!version::kGitCommit.empty());
    QIQI_TEST_CHECK(!version::kGitCommitW.empty());
    QIQI_TEST_CHECK(version::kFullVersion.starts_with(version::kVersion));
}

void TestSpdlogLogger() {
    // 1. Test lazy initialization of GetLogger() before InitLogger() is ever called
    auto default_logger = log::GetLogger();
    QIQI_TEST_CHECK(default_logger != nullptr);
    QIQI_LOG_DEBUG("Lazy default logger test");

    // 2. Test WideToUtf8 with empty and non-empty strings
    QIQI_TEST_CHECK(log::WideToUtf8(L"").empty());
    QIQI_TEST_CHECK(log::WideToUtf8(L"五笔ABC") == "五笔ABC");

    // 3. Test custom file logger, SetLevel, and Flush
    const auto temp_log = std::filesystem::temp_directory_path() / L"qiqiime_unit_test.log";
    std::error_code ec;
    std::filesystem::remove(temp_log, ec);

    log::InitLogger(temp_log, spdlog::level::info);
    log::SetLevel(spdlog::level::trace);
    QIQI_LOG_TRACE("Trace message");
    QIQI_LOG_DEBUG("Debug message");
    QIQI_LOG_INFO("Unit test log entry: {}", log::WideToUtf8(L"五笔日志测试"));
    QIQI_LOG_WARN("Warn message");
    QIQI_LOG_ERROR("Error message");
    log::Flush();

    QIQI_TEST_CHECK(std::filesystem::exists(temp_log));
    {
        std::ifstream ifs(temp_log);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        QIQI_TEST_CHECK(content.find("Unit test log entry:") != std::string::npos);
        QIQI_TEST_CHECK(content.find("Trace message") != std::string::npos);
    }

    // Re-initialize default logger to release temp_log handle
    log::InitLogger();
    std::filesystem::remove(temp_log, ec);
}

void TestAddEntryAndLookupEdgeCases() {
    WubiEngine engine;
    QIQI_TEST_CHECK(!engine.IsComposing());
    QIQI_TEST_CHECK(engine.RawCode().empty());
    QIQI_TEST_CHECK(engine.AllCandidates().empty());
    QIQI_TEST_CHECK(engine.CurrentPageCandidates().empty());
    QIQI_TEST_CHECK(engine.TotalPages() == 0);
    QIQI_TEST_CHECK(engine.CurrentPage() == 0);
    QIQI_TEST_CHECK(engine.SelectedInPage() == 0);
    QIQI_TEST_CHECK(engine.PageSize() == WubiEngine::kDefaultPageSize);

    // Empty lookup
    QIQI_TEST_CHECK(engine.LookupCandidates(L"").empty());
    QIQI_TEST_CHECK(engine.LookupExact(L"zzzz").empty());

    // Invalid AddEntry arguments should be ignored safely
    engine.AddEntry(L"", L"空码");
    engine.AddEntry(L"a", L"");
    engine.AddEntry(L"abcde", L"超长码");
    engine.AddEntry(L"ab1", L"含数字");
    engine.AddEntry(L"ab@", L"含符号1");
    engine.AddEntry(L"ab{", L"含符号2");
    QIQI_TEST_CHECK(engine.LookupExact(L"abcde").empty());
    QIQI_TEST_CHECK(engine.LookupExact(L"ab1").empty());

    // Uppercase normalization & duplicate suppression
    engine.AddEntry(L"XYZW", L"测试词");
    engine.AddEntry(L"xyzw", L"测试词");
    auto exact = engine.LookupExact(L"xyzw");
    QIQI_TEST_CHECK(exact.size() == 1 && exact[0] == L"测试词");

    // Prefix and wildcard 50-candidate cap
    for (int i = 0; i < 60; ++i) {
        std::wstring word = L"词" + std::to_wstring(i);
        engine.AddEntry(L"xyza", word);
    }
    auto prefix_cands = engine.LookupCandidates(L"xyz");
    QIQI_TEST_CHECK(prefix_cands.size() == 50);
    auto wildcard_cands = engine.LookupCandidates(L"xzz");
    QIQI_TEST_CHECK(wildcard_cands.size() == 50);
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

void TestExternalDictFile() {
    WubiEngine engine;

    // 1. Non-existent file returns false
    QIQI_TEST_CHECK(!engine.LoadDictionaryFile("non_existent_wubi_table_12345.txt"));

    // 2. Temporary UTF-8 dictionary file with BOM, comments, blank lines, and multiple words
    const auto temp_dict = std::filesystem::temp_directory_path() / L"qiqiime_test_dict.txt";
    {
        std::ofstream ofs(temp_dict, std::ios::binary);
        ofs << "\xEF\xBB\xBF# Comment line with BOM\n"
            << "\n"
            << "   \n"
            << "# Another comment\n"
            << "zzzz 测一 测二\n";
    }
    QIQI_TEST_CHECK(engine.LoadDictionaryFile(temp_dict));
    auto zzzz = engine.LookupExact(L"zzzz");
    QIQI_TEST_CHECK(zzzz.size() == 2 && zzzz[0] == L"测一" && zzzz[1] == L"测二");

    std::error_code ec;
    std::filesystem::remove(temp_dict, ec);

    // 3. Load bundled wubi86.txt if present in current or executable directory
    if (std::filesystem::exists("wubi86.txt")) {
        QIQI_TEST_CHECK(engine.LoadDictionaryFile("wubi86.txt"));
    }
}

} // namespace

int main() {
    TestSpdlogLogger();
    TestVersionMechanism();
    TestAddEntryAndLookupEdgeCases();
    TestFirstLevelShortCodes();
    TestFourCodeUniqueAutoCommit();
    TestFifthKeyPushAutoCommit();
    TestWildcardAndBackspaceAndCancel();
    TestNavigationSelectionAndReturnCommit();
    TestModeSwitchingAndShouldEatKey();
    TestChinesePunctuation();
    TestExternalDictFile();
    std::cout << "[PASS] All QiQi Wubi 86 engine tests succeeded.\n";
    return 0;
}
