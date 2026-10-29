#include "common.h"
#include "common/logger.h"
#include "engine/wubi_engine.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qiqi::test {

namespace {

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

    // Prefix lookup where prefix itself also has an exact match (covers exact-skip in prefix loop)
    engine.AddEntry(L"xyz", L"三码词");
    auto short_prefix_cands = engine.LookupCandidates(L"xyz");
    QIQI_TEST_CHECK(short_prefix_cands.size() >= 2 && short_prefix_cands[0] == L"三码词");

    // Prefix lookup at the lexicographical tail of the dictionary (reaches dict_.end() without break)
    engine.AddEntry(L"yyyx", L"尾词");
    auto tail_cands = engine.LookupCandidates(L"yyy");
    QIQI_TEST_CHECK(!tail_cands.empty() && tail_cands[0] == L"尾词");

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

    // 3. Load bundled wubi86.txt next to executable or in current directory
    std::filesystem::path dict_next_to_exe = log::GetModuleFilePath(nullptr);
    if (!dict_next_to_exe.empty()) {
        dict_next_to_exe.replace_filename(L"wubi86.txt");
        if (std::filesystem::exists(dict_next_to_exe)) {
            QIQI_TEST_CHECK(engine.LoadDictionaryFile(dict_next_to_exe));
        }
    }
    if (std::filesystem::exists("wubi86.txt")) {
        QIQI_TEST_CHECK(engine.LoadDictionaryFile("wubi86.txt"));
    }
}

} // namespace

void RunEngineDictTests() {
    TestAddEntryAndLookupEdgeCases();
    TestExternalDictFile();
}

} // namespace qiqi::test
