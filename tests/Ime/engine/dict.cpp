#include "common/test_support.h"
#include "common/file_util.h"
#include "common/logger.h"
#include "Ime/engine/engine.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace qiqi::test {

namespace {

template <typename T>
void WriteBinaryValue(std::vector<uint8_t>& buffer, size_t offset, const T& value) {
    const std::span<uint8_t> bytes(buffer);
    std::memcpy(bytes.subspan(offset, sizeof(value)).data(), &value, sizeof(value));
}

template <typename T>
void ReadBinaryValue(const std::vector<uint8_t>& buffer, size_t offset, T& value) {
    const std::span<const uint8_t> bytes(buffer);
    std::memcpy(&value, bytes.subspan(offset, sizeof(value)).data(), sizeof(value));
}

std::vector<uint8_t> MakeSingleEntryQdictBuffer() {
    DictBinaryHeader header{};
    header.code_count = 1;
    header.word_count = 1;
    header.char_count = 1;
    const DictCodeIndex code_index{PackWubiCode(L"aaaa"), 0, 1};
    const DictWordSlice word_slice{0, 1};
    const wchar_t word = L'词';

    std::vector<uint8_t> buffer(sizeof(header) + sizeof(code_index) + sizeof(word_slice) + sizeof(word));
    size_t offset = 0;
    WriteBinaryValue(buffer, offset, header);
    offset += sizeof(header);
    WriteBinaryValue(buffer, offset, code_index);
    offset += sizeof(code_index);
    WriteBinaryValue(buffer, offset, word_slice);
    offset += sizeof(word_slice);
    WriteBinaryValue(buffer, offset, word);
    return buffer;
}

bool WriteInvalidIndexQdictCache(
    const std::filesystem::path& source_path,
    const std::filesystem::path& cache_path) {
    std::error_code ec;
    const uint64_t source_size = std::filesystem::file_size(source_path, ec);
    if (ec) {
        return false;
    }
    const uint64_t source_mtime = gsl::narrow_cast<uint64_t>(
        std::filesystem::last_write_time(source_path, ec).time_since_epoch().count());
    if (ec) {
        return false;
    }

    auto buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    header.source_size = source_size;
    header.source_mtime = source_mtime;
    std::memcpy(buffer.data(), &header, sizeof(header));

    DictCodeIndex code_index{};
    ReadBinaryValue(buffer, sizeof(DictBinaryHeader), code_index);
    code_index.first_word_idx = UINT32_MAX;
    WriteBinaryValue(buffer, sizeof(DictBinaryHeader), code_index);

    std::ofstream cache_file(cache_path, std::ios::binary | std::ios::trunc);
    if (!cache_file) {
        return false;
    }
    cache_file.write(
        reinterpret_cast<const char*>(buffer.data()),
        gsl::narrow_cast<std::streamsize>(buffer.size()));
    return cache_file.good();
}

void TestBinaryDictBufferValidation() {
    auto valid_buffer = MakeSingleEntryQdictBuffer();
    const auto valid_dict = WubiMappedDict::LoadFromBinaryBuffer(std::move(valid_buffer));
    QIQI_TEST_CHECK(valid_dict != nullptr && valid_dict->IsValid());
    std::vector<std::wstring> words;
    valid_dict->LookupExact(PackWubiCode(L"aaaa"), words);
    QIQI_TEST_CHECK(words.size() == 1 && words.front() == L"词");

    auto invalid_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_count_header{};
    std::memcpy(&invalid_count_header, invalid_count_buffer.data(), sizeof(invalid_count_header));
    invalid_count_header.code_count = UINT32_MAX;
    std::memcpy(invalid_count_buffer.data(), &invalid_count_header, sizeof(invalid_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_count_buffer)) == nullptr);

    auto invalid_word_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_word_count_header{};
    std::memcpy(&invalid_word_count_header, invalid_word_count_buffer.data(), sizeof(invalid_word_count_header));
    invalid_word_count_header.word_count = UINT32_MAX;
    std::memcpy(invalid_word_count_buffer.data(), &invalid_word_count_header, sizeof(invalid_word_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_word_count_buffer)) == nullptr);

    auto invalid_char_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_char_count_header{};
    std::memcpy(&invalid_char_count_header, invalid_char_count_buffer.data(), sizeof(invalid_char_count_header));
    invalid_char_count_header.char_count = UINT32_MAX;
    std::memcpy(invalid_char_count_buffer.data(), &invalid_char_count_header, sizeof(invalid_char_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_char_count_buffer)) == nullptr);

    auto invalid_index_buffer = MakeSingleEntryQdictBuffer();
    DictCodeIndex invalid_index{};
    ReadBinaryValue(invalid_index_buffer, sizeof(DictBinaryHeader), invalid_index);
    invalid_index.first_word_idx = UINT32_MAX;
    WriteBinaryValue(invalid_index_buffer, sizeof(DictBinaryHeader), invalid_index);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_index_buffer)) == nullptr);

    auto invalid_code_buffer = MakeSingleEntryQdictBuffer();
    DictCodeIndex invalid_code{};
    ReadBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    invalid_code.packed_code = 0xFF000000u;
    WriteBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_code_buffer)) == nullptr);

    auto invalid_slice_offset_buffer = MakeSingleEntryQdictBuffer();
    DictWordSlice invalid_slice_offset{};
    ReadBinaryValue(
        invalid_slice_offset_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_offset);
    invalid_slice_offset.char_offset = UINT32_MAX;
    WriteBinaryValue(
        invalid_slice_offset_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_offset);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_slice_offset_buffer)) == nullptr);

    auto invalid_slice_length_buffer = MakeSingleEntryQdictBuffer();
    DictWordSlice invalid_slice_length{};
    ReadBinaryValue(
        invalid_slice_length_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_length);
    invalid_slice_length.char_length = UINT32_MAX;
    WriteBinaryValue(
        invalid_slice_length_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_length);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_slice_length_buffer)) == nullptr);
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

    // Compile-time checks for 4-byte packed Wubi code and prefix bounds
    static_assert(PackWubiCode(L"") == 0);
    static_assert(PackWubiCode(L"abcde") == 0);
    static_assert(PackWubiCode(L"a") < PackWubiCode(L"aa"));
    static_assert(PackWubiCode(L"aa") < PackWubiCode(L"ab"));
    static_assert(PackWubiCode(L"ab") < PackWubiCode(L"b"));
    static_assert(PackWubiCode(L"WQVB") == PackWubiCode(L"wqvb"));
    static_assert(PackWubiCode(L"wqvb") > PackWubiCode(L"wq"));
    static_assert(PackWubiCode(L"wqvb") <= PackWubiPrefixUpperBound(L"wq"));
    static_assert(MatchesWildcardPattern(PackWubiCode(L"wqvb"), L"wqz"));
    static_assert(!MatchesWildcardPattern(PackWubiCode(L"wq"), L"wqz"));
    QIQI_TEST_CHECK(!MatchesWildcardPattern(PackWubiCode(L"wqvb"), L""));
    QIQI_TEST_CHECK(!MatchesWildcardPattern(PackWubiCode(L"wqvb"), L"abcde"));
    QIQI_TEST_CHECK(MatchesWildcardPattern(PackWubiCode(L"wqvb"), L"WQZ"));

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
    QIQI_TEST_CHECK(exact.size() == 1 && gsl::at(exact, 0) == L"测试词");

    // Prefix lookup where prefix itself also has an exact match (covers exact-skip in prefix loop)
    engine.AddEntry(L"xyz", L"三码词");
    auto short_prefix_cands = engine.LookupCandidates(L"xyz");
    QIQI_TEST_CHECK(short_prefix_cands.size() >= 2 && gsl::at(short_prefix_cands, 0) == L"三码词");

    // Prefix lookup at the lexicographical tail of the dictionary (reaches dict_.end() without break)
    engine.AddEntry(L"yyyx", L"尾词");
    auto tail_cands = engine.LookupCandidates(L"yyy");
    QIQI_TEST_CHECK(!tail_cands.empty() && gsl::at(tail_cands, 0) == L"尾词");

    // Prefix and wildcard 50-candidate cap, plus 50+ exact match cap
    for (int i = 0; i < 60; ++i) {
        std::wstring word = L"词" + std::to_wstring(i);
        engine.AddEntry(L"xyza", word);
    }
    auto exact_cap = engine.LookupCandidates(L"xyza");
    QIQI_TEST_CHECK(exact_cap.size() == 50);
    auto prefix_cands = engine.LookupCandidates(L"xyz");
    QIQI_TEST_CHECK(prefix_cands.size() == 50);
    auto wildcard_cands = engine.LookupCandidates(L"xzz");
    QIQI_TEST_CHECK(wildcard_cands.size() == 50);
}

void TestExternalDictFile() {
    WubiEngine engine;

    // 1. Non-existent file returns false
    QIQI_TEST_CHECK(!engine.LoadDictionaryFile("non_existent_wubi_table_12345.txt"));

    // 2. Temporary UTF-8 dictionary file with BOM, comments, blank lines, QiQi format, and Rime YAML format
    const auto temp_dict = std::filesystem::temp_directory_path() / L"qiqiime_test_dict.txt";
    std::filesystem::path adjacent_qdict = temp_dict;
    adjacent_qdict.replace_extension(L".qdict");
    {
        std::ofstream ofs(temp_dict, std::ios::binary);
        ofs << "\xEF\xBB\xBF# Comment line with BOM\n"
            << "---\n"
            << "name: test_rime_dict\n"
            << "...\n"
            << "\n"
            << "   \n"
            << "# Another comment\n"
            << "zzzz 测一 测二\n"
            << "瑞梅词\tzzzy\t100\n"
            << "超长码词\tabcde\t50\n";
    }
    // Pre-create a full-size cache with matching source metadata and an invalid index entry.
    WubiMappedDict::ClearProcessCache();
    const auto fallback_cache = GetAppDataDirectory() / L"cache" / L"qiqiime_test_dict.qdict";
    std::error_code cache_ec;
    std::filesystem::remove(fallback_cache, cache_ec);
    QIQI_TEST_CHECK(WriteInvalidIndexQdictCache(temp_dict, adjacent_qdict));
    QIQI_TEST_CHECK(engine.LoadDictionaryFile(temp_dict));
    const auto rebuilt_zzzz = engine.LookupExact(L"zzzz");
    QIQI_TEST_CHECK(rebuilt_zzzz.size() == 2 && gsl::at(rebuilt_zzzz, 0) == L"测一" &&
                    gsl::at(rebuilt_zzzz, 1) == L"测二");
    // Loading the same file a second time hits the in-process & disk .qdict cache
    QIQI_TEST_CHECK(engine.LoadDictionaryFile(temp_dict));
    QIQI_TEST_CHECK(engine.MappedDictionaryCount() >= 1);
    auto zzzz = engine.LookupExact(L"zzzz");
    QIQI_TEST_CHECK(zzzz.size() == 2 && gsl::at(zzzz, 0) == L"测一" && gsl::at(zzzz, 1) == L"测二");
    auto zzzy = engine.LookupExact(L"zzzy");
    QIQI_TEST_CHECK(zzzy.size() == 1 && gsl::at(zzzy, 0) == L"瑞梅词");

    // Prefix & wildcard lookup on mapped dictionary (including progressive suffix hints via ProcessKey)
    auto zz_prefix = engine.LookupCandidates(L"zzz");
    QIQI_TEST_CHECK(zz_prefix.size() >= 3);
    engine.ProcessKey('T');
    engine.ProcessKey('U');
    engine.ProcessKey('G');
    engine.Reset();

    // Verify least-privilege sharing on mapped .qdict:
    // Read-only sharing (GENERIC_READ | FILE_SHARE_READ) is allowed, while write access (GENERIC_WRITE) is denied.
    {
        const ScopedFileHandle ro_check(::CreateFileW(
            adjacent_qdict.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        QIQI_TEST_CHECK(ro_check.IsValid());

        const ScopedFileHandle rw_check(::CreateFileW(
            adjacent_qdict.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));
        QIQI_TEST_CHECK(!rw_check.IsValid());
    }

    // Verify concurrent hot-replacement while another engine/UWP instance holds the old .qdict mapped:
    // 1) `engine` keeps `adjacent_qdict` mapped via FILE_SHARE_READ | FILE_SHARE_DELETE
    // 2) `.txt` is modified on disk with a new entry ("zzzz 新词一 新词二 新词三")
    // 3) `engine.HasModifiedMappedDictionary()` detects the source change
    // 4) A second `updater_engine` atomically replaces `adjacent_qdict` via MoveFileExW while `engine` is still mapped!
    // 5) `engine` continues reading its old view safely until it reloads and switches to the new view.
    {
        QIQI_TEST_CHECK(!engine.HasModifiedMappedDictionary());
        {
            std::ofstream ofs(temp_dict, std::ios::binary | std::ios::trunc);
            ofs << "zzzz 新词一 新词二 新词三\n"
                << "瑞梅词\tzzzy\t100\n";
        }
        std::error_code time_ec;
        std::filesystem::last_write_time(
            temp_dict,
            std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10),
            time_ec);
        QIQI_TEST_CHECK(engine.HasModifiedMappedDictionary());

        WubiEngine updater_engine;
        QIQI_TEST_CHECK(updater_engine.LoadDictionaryFile(temp_dict));
        auto updated_zzzz = updater_engine.LookupExact(L"zzzz");
        QIQI_TEST_CHECK(updated_zzzz.size() == 3 && gsl::at(updated_zzzz, 0) == L"新词一");

        // Old engine still safely reads its existing mapped view ("测一", "测二") without STATUS_IN_PAGE_ERROR
        auto old_zzzz = engine.LookupExact(L"zzzz");
        QIQI_TEST_CHECK(old_zzzz.size() == 2 && gsl::at(old_zzzz, 0) == L"测一");

        // Once old engine reloads, it picks up the newly mapped .qdict ("新词一", "新词二", "新词三")
        engine.ClearExternalDictionaries();
        QIQI_TEST_CHECK(engine.LoadDictionaryFile(temp_dict));
        QIQI_TEST_CHECK(!engine.HasModifiedMappedDictionary());
        auto reloaded_zzzz = engine.LookupExact(L"zzzz");
        QIQI_TEST_CHECK(reloaded_zzzz.size() == 3 && gsl::at(reloaded_zzzz, 0) == L"新词一");
    }

    // Test CompileTextFileToQdict prebuilding .qdict and loading when .txt is absent or read-only
    {
        const auto prebuilt_qdict = std::filesystem::temp_directory_path() / L"qiqiime_prebuilt_only.qdict";
        const auto missing_txt = std::filesystem::temp_directory_path() / L"qiqiime_prebuilt_only.txt";
        std::error_code pre_ec;
        std::filesystem::remove(prebuilt_qdict, pre_ec);
        std::filesystem::remove(missing_txt, pre_ec);

        QIQI_TEST_CHECK(!WubiMappedDict::CompileTextFileToQdict(missing_txt, prebuilt_qdict));
        QIQI_TEST_CHECK(WubiMappedDict::CompileTextFileToQdict(temp_dict, prebuilt_qdict));
        QIQI_TEST_CHECK(std::filesystem::exists(prebuilt_qdict));

        WubiEngine prebuilt_engine;
        QIQI_TEST_CHECK(prebuilt_engine.LoadDictionaryFile(missing_txt));
        // Second LoadDictionaryFile with missing_txt hits the in-process cache for qdict-only path
        QIQI_TEST_CHECK(prebuilt_engine.LoadDictionaryFile(missing_txt));
        auto pre_zzzz = prebuilt_engine.LookupExact(L"zzzz");
        QIQI_TEST_CHECK(pre_zzzz.size() == 3 && gsl::at(pre_zzzz, 0) == L"新词一");
        prebuilt_engine.ClearExternalDictionaries();
        WubiMappedDict::ClearProcessCache();
        std::filesystem::remove(prebuilt_qdict, pre_ec);

        // Corrupted .qdict when .txt does not exist returns false
        const auto corrupt_only_qdict = std::filesystem::temp_directory_path() / L"qiqiime_corrupt_only.qdict";
        const auto corrupt_only_txt = std::filesystem::temp_directory_path() / L"qiqiime_corrupt_only.txt";
        {
            std::ofstream bad_ofs(corrupt_only_qdict, std::ios::binary);
            bad_ofs << "bad";
        }
        WubiEngine corrupt_engine;
        QIQI_TEST_CHECK(!corrupt_engine.LoadDictionaryFile(corrupt_only_txt));
        std::filesystem::remove(corrupt_only_qdict, pre_ec);
    }

    std::error_code ec;
    std::filesystem::remove(temp_dict, ec);
    std::filesystem::remove(adjacent_qdict, ec);
    std::filesystem::remove(GetAppDataDirectory() / L"cache" / L"qiqiime_test_dict.qdict", ec);

    // 3. Load bundled dictionaries (wubi86_jidian.txt, wubi86_jidian_extra.txt, wubi86_haifeng.txt)
    const std::filesystem::path exe_path = GetModuleFilePath(nullptr);
    if (!exe_path.empty()) {
        const std::filesystem::path exe_dir = exe_path.parent_path();
        const std::filesystem::path main_dict = exe_dir / L"wubi86_jidian.txt";
        const std::filesystem::path extra_dict = exe_dir / L"wubi86_jidian_extra.txt";
        const std::filesystem::path haifeng_dict = exe_dir / L"wubi86_haifeng.txt";
        if (std::filesystem::exists(main_dict)) {
            WubiEngine jidian_engine;
            QIQI_TEST_CHECK(jidian_engine.LoadDictionaryFile(main_dict));
            auto gong = jidian_engine.LookupExact(L"aaaa");
            QIQI_TEST_CHECK(gong.size() >= 2 && gsl::at(gong, 0) == L"工");
            // Exercise mapped prefix and wildcard 50-result cap on full dictionary
            auto wq_prefix = jidian_engine.LookupCandidates(L"w");
            QIQI_TEST_CHECK(wq_prefix.size() == 50);
            auto w_wild = jidian_engine.LookupCandidates(L"wzz");
            QIQI_TEST_CHECK(w_wild.size() == 50);
        }
        if (std::filesystem::exists(extra_dict)) {
            QIQI_TEST_CHECK(engine.LoadDictionaryFile(extra_dict));
            auto ke1 = engine.LookupExact(L"tugg");
            QIQI_TEST_CHECK(std::find(ke1.begin(), ke1.end(), L"科一") != ke1.end());
            // Exercise mapped LookupPrefix with progressive suffix hint (~g) on "tug"
            engine.ProcessKey('T');
            engine.ProcessKey('U');
            engine.ProcessKey('G');
            bool found_mapped_hint = false;
            for (const auto& lbl : engine.AllDisplayCandidates()) {
                if (lbl == L"科一(~g)") {
                    found_mapped_hint = true;
                    break;
                }
            }
            QIQI_TEST_CHECK(found_mapped_hint);
            engine.Reset();
        }
        if (std::filesystem::exists(haifeng_dict)) {
            WubiEngine haifeng_engine;
            QIQI_TEST_CHECK(haifeng_engine.LoadDictionaryFile(haifeng_dict));
            // Verify Unicode CJK Extension B character (U+20000 '𠀀' -> ghab) in Haifeng Wubi
            auto cjkb = haifeng_engine.LookupExact(L"ghab");
            QIQI_TEST_CHECK(!cjkb.empty() && gsl::at(cjkb, 0) == L"𠀀");
            // Verify FindLongestCodeForWord and EncodePhraseWubi86 on UTF-16 surrogate pair CJK Ext-B char
            QIQI_TEST_CHECK(haifeng_engine.LookupCharacterCode(L"𠀀") == L"ghab");
            QIQI_TEST_CHECK(haifeng_engine.EncodePhraseWubi86(L"𠀀好") == L"ghvb");
        }
    }

    // 4. Test Wubi 86 automatic phrase encoding (2+2, 1+1+2, 1+1+1+last) and persistent AddUserPhrase
    WubiEngine phrase_engine;
    QIQI_TEST_CHECK(!GetDefaultUserDictPath().empty());
    QIQI_TEST_CHECK(phrase_engine.LookupCharacterCode(L"").empty());
    QIQI_TEST_CHECK(phrase_engine.LookupCharacterCode(L"国") == L"lgyi");
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"国").empty());        // < 2 chars -> empty
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"国?").empty());       // unknown char -> empty
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"中国") == L"khlg");   // 2 chars (2+2): khl + lgyi -> khlg
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"中国好") == L"klvb"); // 3 chars (1+1+2): k + l + vb -> klvb
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"中国你好") == L"klwv"); // 4 chars (1+1+1+last)
    QIQI_TEST_CHECK(phrase_engine.EncodePhraseWubi86(L"五笔中国好") == L"gtkv"); // 5 chars (1+1+1+last)

    const auto temp_user_dict = std::filesystem::temp_directory_path() / L"qiqiime_user_phrases_test.txt";
    std::filesystem::remove(temp_user_dict, ec);
    QIQI_TEST_CHECK(!phrase_engine.AddUserPhrase(L"单"));
    QIQI_TEST_CHECK(phrase_engine.AddUserPhrase(L"中国好")); // in-memory only
    auto mem_lookup = phrase_engine.LookupExact(L"klvb");
    QIQI_TEST_CHECK(!mem_lookup.empty() && mem_lookup.front() == L"中国好");

    QIQI_TEST_CHECK(phrase_engine.AddUserPhrase(L"五笔中国好", temp_user_dict));
    WubiEngine reloaded_engine;
    QIQI_TEST_CHECK(reloaded_engine.LoadDictionaryFile(temp_user_dict));
    auto persisted_lookup = reloaded_engine.LookupExact(L"gtkv");
    QIQI_TEST_CHECK(!persisted_lookup.empty() && persisted_lookup.front() == L"五笔中国好");

    std::filesystem::path temp_user_qdict = temp_user_dict;
    temp_user_qdict.replace_extension(L".qdict");
    QIQI_TEST_CHECK(std::filesystem::exists(temp_user_qdict));

    // Verify LoadDictionaryFromBinaryBuffer with valid and invalid .qdict bytes
    std::string qdict_raw;
    QIQI_TEST_CHECK(ReadFileToUtf8String(temp_user_qdict, qdict_raw));
    std::vector<uint8_t> qdict_vec(qdict_raw.size());
    std::memcpy(qdict_vec.data(), qdict_raw.data(), qdict_raw.size());
    WubiEngine mem_buf_engine;
    QIQI_TEST_CHECK(!mem_buf_engine.LoadDictionaryFromBinaryBuffer({1, 2, 3}));
    QIQI_TEST_CHECK(mem_buf_engine.LoadDictionaryFromBinaryBuffer(std::move(qdict_vec)));
    auto buf_lookup = mem_buf_engine.LookupExact(L"gtkv");
    QIQI_TEST_CHECK(!buf_lookup.empty() && buf_lookup.front() == L"五笔中国好");

    std::filesystem::remove(temp_user_dict, ec);
    std::filesystem::remove(temp_user_qdict, ec);
}

} // namespace

void RunEngineDictTests() {
    TestBinaryDictBufferValidation();
    TestAddEntryAndLookupEdgeCases();
    TestExternalDictFile();
}

} // namespace qiqi::test
