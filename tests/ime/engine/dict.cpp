#include "common/test_support.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "ime/engine/engine.h"

#include <algorithm>
#include <chrono>
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

void RefreshQdictChecksum(std::vector<uint8_t>& buffer) {
    DictBinaryHeader header{};
    ReadBinaryValue(buffer, 0, header);
    header.payload_checksum = codec::ComputeFnv1a32(
        std::span<const uint8_t>(buffer).subspan(sizeof(DictBinaryHeader)));
    WriteBinaryValue(buffer, 0, header);
}

std::vector<uint8_t> MakeSingleEntryQdictBuffer() {
    DictBinaryHeader header{};
    header.code_count = 1;
    header.word_count = 1;
    header.char_count = 1;
    const DictCodeIndex code_index{PackWubiCode(L"aaaa"), 0, 1};
    const DictWordSlice word_slice{0, 1};
    constexpr wchar_t word = L'词';

    std::vector<uint8_t> buffer(sizeof(header) + sizeof(code_index) + sizeof(word_slice) + sizeof(word));
    size_t offset = 0;
    WriteBinaryValue(buffer, offset, header);
    offset += sizeof(header);
    WriteBinaryValue(buffer, offset, code_index);
    offset += sizeof(code_index);
    WriteBinaryValue(buffer, offset, word_slice);
    offset += sizeof(word_slice);
    WriteBinaryValue(buffer, offset, word);
    RefreshQdictChecksum(buffer);
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
    RefreshQdictChecksum(buffer);

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

    // Outdated .qdict version (v1) and corrupted payload checksum must be rejected
    auto v1_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader v1_header{};
    std::memcpy(&v1_header, v1_buffer.data(), sizeof(v1_header));
    v1_header.version = 1u;
    std::memcpy(v1_buffer.data(), &v1_header, sizeof(v1_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(v1_buffer)) == nullptr);

    auto bad_checksum_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader bad_checksum_header{};
    std::memcpy(&bad_checksum_header, bad_checksum_buffer.data(), sizeof(bad_checksum_header));
    bad_checksum_header.payload_checksum ^= 1u;
    std::memcpy(bad_checksum_buffer.data(), &bad_checksum_header, sizeof(bad_checksum_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(bad_checksum_buffer)) == nullptr);

    auto trailing_bytes_buffer = MakeSingleEntryQdictBuffer();
    trailing_bytes_buffer.push_back(0xAA);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(trailing_bytes_buffer)) == nullptr);
    WubiMappedDict::InvalidateProcessCacheForPath({});

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
    RefreshQdictChecksum(invalid_index_buffer);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_index_buffer)) == nullptr);

    auto invalid_code_buffer = MakeSingleEntryQdictBuffer();
    DictCodeIndex invalid_code{};
    ReadBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    invalid_code.packed_code = 0xFF000000u;
    WriteBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    RefreshQdictChecksum(invalid_code_buffer);
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
    RefreshQdictChecksum(invalid_slice_offset_buffer);
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
    RefreshQdictChecksum(invalid_slice_length_buffer);
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
            auto cf_cands = jidian_engine.LookupCandidates(L"cf");
            QIQI_TEST_CHECK(!cf_cands.empty() && gsl::at(cf_cands, 0) == L"对");
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
        if (std::filesystem::exists(main_dict)) {
            WubiMappedDict::ClearProcessCache();
            QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0);
            const auto first_mapped = WubiMappedDict::LoadFromTextOrCache(main_dict);
            const auto second_mapped = WubiMappedDict::LoadFromTextOrCache(main_dict);
            QIQI_TEST_CHECK(first_mapped && first_mapped == second_mapped);
            QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 1);
            const std::weak_ptr<const WubiMappedDict> weak_mapped = first_mapped;

            WubiEngine active_peer;
            QIQI_TEST_CHECK(active_peer.LoadDictionaryFile(main_dict));
            QIQI_TEST_CHECK(active_peer.MappedDictionaries().size() == 1 &&
                            active_peer.MappedDictionaries().front() == first_mapped);

            // Clearing the process cache removes the static map entry without dangling active shared_ptr holders.
            WubiMappedDict::ClearProcessCache();
            QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0 && !weak_mapped.expired());
            const auto peer_lookup = active_peer.LookupExact(L"aaaa");
            QIQI_TEST_CHECK(peer_lookup.size() >= 2 && gsl::at(peer_lookup, 0) == L"工");
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
    std::filesystem::remove_all(GetSyncChannelDirectoryForFile(temp_user_dict), ec);
}

void TestCrossProcessDictAndPhraseFreqSync() {
    const auto base_dir = std::filesystem::temp_directory_path() /
        (L"qiqiime_task40_sync_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(base_dir, ec);
    std::filesystem::create_directories(base_dir, ec);
    const auto cleanup = gsl::finally([&base_dir]() noexcept {
        std::error_code rm_ec;
        std::filesystem::remove_all(base_dir, rm_ec);
    });

    const auto user_dict = base_dir / L"user_dict.txt";
    const auto freq_file = base_dir / L"phrase_freq.txt";

    WubiEngine writer;
    WubiEngine reader;
    writer.SetUserDictPath(user_dict);
    reader.SetUserDictPath(user_dict);
    writer.SetPhraseFreqPath(freq_file);
    reader.SetPhraseFreqPath(freq_file);
    writer.SetDynamicPhraseFreq(true);
    reader.SetDynamicPhraseFreq(true);

    // 1. Controlled dictionary Add / Modify / Remove -> incremental sync in reader
    QIQI_TEST_CHECK(writer.AddUserPhrase(L"中国好", user_dict));
    QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 1 && reader.UserDictIncrementalSyncCount() == 0);
    auto r_klvb = reader.LookupExact(L"klvb");
    QIQI_TEST_CHECK(!r_klvb.empty() && r_klvb.front() == L"中国好");

    QIQI_TEST_CHECK(writer.AddUserEntry(L"abcd", L"词条甲", user_dict));
    QIQI_TEST_CHECK(writer.AddUserEntry(L"abcd", L"词条乙", user_dict));
    QIQI_TEST_CHECK(writer.ModifyUserEntry(L"abcd", L"词条甲", L"词条甲改", user_dict));
    QIQI_TEST_CHECK(writer.RemoveUserEntry(L"klvb", L"中国好", user_dict));

    // Composing boundary defers synchronization so active candidates never jump mid-input
    reader.ProcessKey('A');
    QIQI_TEST_CHECK(reader.IsComposing());
    QIQI_TEST_CHECK(reader.SynchronizeUserDictionary() == S_FALSE);
    QIQI_TEST_CHECK(!reader.SynchronizeCrossProcessChanges());
    QIQI_TEST_CHECK(reader.LookupExact(L"abcd").empty());

    // NewInput boundary applies all 4 incremental records without a full snapshot reload
    reader.Reset();
    QIQI_TEST_CHECK(reader.SynchronizeCrossProcessChanges());
    QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 1 && reader.UserDictIncrementalSyncCount() == 4);
    const auto r_abcd = reader.LookupExact(L"abcd");
    QIQI_TEST_CHECK(r_abcd.size() == 2 && gsl::at(r_abcd, 0) == L"词条甲改" && gsl::at(r_abcd, 1) == L"词条乙");
    QIQI_TEST_CHECK(reader.LookupExact(L"klvb").empty());

    // 2. External editor modification: small bounded diff (<= 16 ops) syncs incrementally
    {
        std::ofstream ofs(user_dict, std::ios::binary | std::ios::app);
        ofs << "abcd 外部新增一\nabce 外部新增二\n";
    }
    std::filesystem::last_write_time(
        user_dict,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(2),
        ec);
    QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 1 && reader.UserDictIncrementalSyncCount() == 6);
    const auto r_abcd_ext = reader.LookupExact(L"abcd");
    QIQI_TEST_CHECK(r_abcd_ext.size() == 3 && gsl::at(r_abcd_ext, 2) == L"外部新增一");
    const auto r_abce_ext = reader.LookupExact(L"abce");
    QIQI_TEST_CHECK(r_abce_ext.size() == 1 && gsl::at(r_abce_ext, 0) == L"外部新增二");

    {
        const auto version = reader.UserDictSyncVersion();
        const auto modified = std::filesystem::last_write_time(user_dict, ec);
        QIQI_TEST_CHECK(!ec);
        std::filesystem::last_write_time(user_dict, modified + std::chrono::seconds(1), ec);
        QIQI_TEST_CHECK(!ec);
        QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(reader.UserDictSyncVersion() == version);
        QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 1 && reader.UserDictIncrementalSyncCount() == 6);
        QIQI_TEST_CHECK(reader.LookupExact(L"abcd") == r_abcd_ext && reader.LookupExact(L"abce") == r_abce_ext);
        QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(reader.UserDictSyncVersion() == version);
        QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 1 && reader.UserDictIncrementalSyncCount() == 6);
    }

    // 3. External editor large replacement (> 16 ops) rotates baseline and triggers full snapshot reload
    {
        std::ofstream ofs(user_dict, std::ios::binary | std::ios::trunc);
        for (int i = 0; i < 20; ++i) {
            ofs << "xyza 批量替换词" << i << "\n";
        }
    }
    std::filesystem::last_write_time(
        user_dict,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5),
        ec);
    QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(reader.UserDictSnapshotSyncCount() == 2);
    QIQI_TEST_CHECK(reader.LookupExact(L"abcd").empty());
    QIQI_TEST_CHECK(reader.LookupExact(L"xyza").size() == 20);

    // 4. Phrase frequency binary format (QPF1) & coordinated cross-process incremental reorder sync
    writer.AddEntry(L"qrst", L"单");
    writer.AddEntry(L"qrst", L"短语一");
    writer.AddEntry(L"qrst", L"短语二");
    writer.AddEntry(L"qrst", L"短语三");
    reader.AddEntry(L"qrst", L"单");
    reader.AddEntry(L"qrst", L"短语一");
    reader.AddEntry(L"qrst", L"短语二");
    reader.AddEntry(L"qrst", L"短语三");

    // Writer selects "短语二" (index 2 on page) -> promotes "短语二" ahead of "短语一"
    for (const uint32_t vk : {'Q', 'R', 'S', 'T'}) writer.ProcessKey(vk);
    QIQI_TEST_CHECK(writer.CommitCandidate(2).committed_text == L"短语二");

    // Verify binary QPF1 magic header on disk
    std::string raw_freq;
    QIQI_TEST_CHECK(ReadFileToUtf8String(freq_file, raw_freq) && raw_freq.size() >= 20);
    QIQI_TEST_CHECK(raw_freq.substr(0, 4) == "QPF1");

    // Reader initial sync loads snapshot, then subsequent writer promotion syncs incrementally via Operation::Reorder
    QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(reader.PhraseFreqSnapshotSyncCount() == 1);
    auto r_qrst = reader.LookupExact(L"qrst");
    QIQI_TEST_CHECK(r_qrst.size() == 4 && gsl::at(r_qrst, 0) == L"单" && gsl::at(r_qrst, 1) == L"短语二");

    // Writer promotes "短语三" -> reader receives incremental Operation::Reorder without snapshot reload
    for (const uint32_t vk : {'Q', 'R', 'S', 'T'}) writer.ProcessKey(vk);
    QIQI_TEST_CHECK(writer.CommitCandidate(3).committed_text == L"短语三");
    QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(reader.PhraseFreqSnapshotSyncCount() == 1 && reader.PhraseFreqIncrementalSyncCount() == 1);
    r_qrst = reader.LookupExact(L"qrst");
    QIQI_TEST_CHECK(r_qrst.size() == 4 && gsl::at(r_qrst, 0) == L"单" &&
                    gsl::at(r_qrst, 1) == L"短语三" && gsl::at(r_qrst, 2) == L"短语二" &&
                    gsl::at(r_qrst, 3) == L"短语一");

    // Reader now promotes "短语一" without prior explicit sync after writer's change -> coordinated write preserves both!
    for (const uint32_t vk : {'Q', 'R', 'S', 'T'}) reader.ProcessKey(vk);
    QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::Composing) == S_FALSE);
    QIQI_TEST_CHECK(reader.CommitCandidate(3).committed_text == L"短语一");
    QIQI_TEST_CHECK(writer.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    const auto w_qrst = writer.LookupExact(L"qrst");
    QIQI_TEST_CHECK(w_qrst.size() == 4 && gsl::at(w_qrst, 0) == L"单" &&
                    gsl::at(w_qrst, 1) == L"短语一" && gsl::at(w_qrst, 2) == L"短语三" &&
                    gsl::at(w_qrst, 3) == L"短语二");

    // 5. Direct SavePhraseFrequencies, legacy single-character filtering, in-memory entry mutations, and file deletion cleanup
    QIQI_TEST_CHECK(!writer.SavePhraseFrequencies({}));
    QIQI_TEST_CHECK(writer.SavePhraseFrequencies(freq_file));
    {
        std::ofstream legacy_freq(freq_file, std::ios::binary | std::ios::trunc);
        legacy_freq << "aaaa 工\nqrst 短语二 短语一\n";
    }
    QIQI_TEST_CHECK(reader.LoadPhraseFrequencies(freq_file));

    WubiEngine in_mem;
    QIQI_TEST_CHECK(!in_mem.RemoveEntry(L"", L"词") && !in_mem.RemoveEntry(L"abcd", L""));
    QIQI_TEST_CHECK(!in_mem.ModifyEntry(L"", L"旧", L"新") && !in_mem.ModifyEntry(L"abcd", L"", L"新"));
    QIQI_TEST_CHECK(in_mem.AddUserEntry(L"abcd", L"内存一"));
    QIQI_TEST_CHECK(in_mem.AddUserEntry(L"abcd", L"内存二"));
    QIQI_TEST_CHECK(in_mem.ModifyUserEntry(L"abcd", L"内存一", L"内存一")); // no-op same word
    QIQI_TEST_CHECK(in_mem.ModifyUserEntry(L"abcd", L"内存一", L"内存二")); // target already exists -> deduplicates
    QIQI_TEST_CHECK(in_mem.LookupExact(L"abcd").size() == 1);
    QIQI_TEST_CHECK(in_mem.ModifyUserEntry(L"aaaa", L"工", L"工改")); // modifies entry from builtin/mapped layer
    QIQI_TEST_CHECK(in_mem.RemoveUserEntry(L"abcd", L"内存二"));
    QIQI_TEST_CHECK(in_mem.LookupExact(L"abcd").empty());

    std::filesystem::remove(user_dict, ec);
    std::filesystem::remove(freq_file, ec);
    QIQI_TEST_CHECK(writer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(writer.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(!writer.LoadPhraseFrequencies(freq_file));

    // 6. Manual candidate pinning (^word), system word blocklist (!word), and composing hotkeys (Ctrl+1..9 / Ctrl+Delete / Ctrl+Shift+1..9)
    WubiEngine pin_writer;
    WubiEngine pin_reader;
    pin_writer.SetUserDictPath(user_dict);
    pin_reader.SetUserDictPath(user_dict);
    pin_writer.SetPhraseFreqPath(freq_file);
    pin_reader.SetPhraseFreqPath(freq_file);
    pin_writer.SetAutoCommit4Code(false);
    pin_writer.SetDynamicPhraseFreq(true);
    pin_reader.SetDynamicPhraseFreq(true);

    QIQI_TEST_CHECK(pin_writer.AddUserEntry(L"abcd", L"普通词一"));
    QIQI_TEST_CHECK(pin_writer.AddUserEntry(L"abcd", L"普通词二"));
    QIQI_TEST_CHECK(pin_writer.AddUserEntry(L"abcd", L"置顶候选"));
    // Enter composing for "abcd" and press Ctrl+'3' to pin the 3rd candidate ("置顶候选") to top
    for (const uint32_t vk : {'A', 'B', 'C', 'D'}) pin_writer.ProcessKey(vk);
    QIQI_TEST_CHECK(pin_writer.ShouldEatKey('3', false, true, false));
    const auto pin_res = pin_writer.ProcessKey('3', false, true, false);
    QIQI_TEST_CHECK(pin_res.action == KeyActionType::UpdateState &&
                    !pin_res.page_candidates.empty() &&
                    pin_res.page_candidates.front() == L"置顶候选");
    pin_writer.Reset();

    // Even when dynamic phrase frequency promotes "普通词二", manually pinned "置顶候选" stays at index 0!
    QIQI_TEST_CHECK(pin_writer.PromoteUserPhraseFrequency(L"abcd", L"普通词二", freq_file));
    const auto after_pin = pin_writer.LookupExact(L"abcd");
    QIQI_TEST_CHECK(after_pin.size() == 3 &&
                    gsl::at(after_pin, 0) == L"置顶候选" &&
                    gsl::at(after_pin, 1) == L"普通词二");

    // Verify peer instance receives ^word pin via incremental sync and compiled .qdict excludes !blocked words
    QIQI_TEST_CHECK(pin_reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(pin_reader.LookupExact(L"abcd").front() == L"置顶候选");

    // Unpin restores order prior to manual pin (where "普通词二" was promoted by dynamic phrase freq)
    QIQI_TEST_CHECK(pin_writer.UnpinUserEntry(L"abcd", L"置顶候选"));
    QIQI_TEST_CHECK(pin_reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(pin_reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(pin_reader.LookupExact(L"abcd").front() == L"普通词二");
    pin_writer.SetDynamicPhraseFreq(false);
    pin_reader.SetDynamicPhraseFreq(false);
    QIQI_TEST_CHECK(pin_writer.LookupExact(L"abcd").front() == L"普通词一");
    QIQI_TEST_CHECK(pin_reader.LookupExact(L"abcd").front() == L"普通词一");

    // Composing Ctrl+Shift+'2' deletes 2nd candidate ("普通词二"), and Ctrl+Delete on builtin "aaaa" ("工") blocks it
    for (const uint32_t vk : {'A', 'B', 'C', 'D'}) pin_writer.ProcessKey(vk);
    QIQI_TEST_CHECK(pin_writer.ShouldEatKey('2', true, true, false));
    std::ignore = pin_writer.ProcessKey('2', true, true, false);
    pin_writer.Reset();
    QIQI_TEST_CHECK(pin_writer.LookupExact(L"abcd").size() == 2);

    pin_writer.ProcessKey('A');
    QIQI_TEST_CHECK(pin_writer.ShouldEatKey(VK_DELETE, false, true, false));
    std::ignore = pin_writer.ProcessKey(VK_DELETE, false, true, false);
    pin_writer.Reset();
    QIQI_TEST_CHECK(pin_writer.LookupExact(L"a").empty());
    QIQI_TEST_CHECK(pin_reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(pin_reader.LookupExact(L"a").empty());

    // Unblock restores builtin "工" for code "a"
    QIQI_TEST_CHECK(pin_writer.UnblockSystemEntry(L"a", L"工"));
    QIQI_TEST_CHECK(pin_reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(!pin_reader.LookupExact(L"a").empty() && pin_reader.LookupExact(L"a").front() == L"工");
}

void TestSyncFailureAtomicity() {
    using Entries = std::map<uint32_t, std::vector<std::wstring>>;
    const auto base = std::filesystem::temp_directory_path() /
        (L"qiqiime_sync_atomicity_" + std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(std::filesystem::create_directory(base));
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove_all(base, error);
        QIQI_TEST_CHECK(!error);
    });
    const auto payload = [](std::wstring_view word, std::wstring_view replacement = {}) {
        std::vector<uint8_t> bytes;
        codec::AppendLe32(bytes, PackWubiCode(L"abcd"));
        codec::AppendWString(bytes, word);
        if (!replacement.empty()) codec::AppendWString(bytes, replacement);
        return bytes;
    };
    const auto publish = [](const sync::Channel& channel, sync::Operation operation,
                            std::span<const uint8_t> bytes) {
        sync::Batch batch;
        QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
        sync::Version committed;
        QIQI_TEST_CHECK(channel.Publish(batch.version, operation, bytes, []() { return S_OK; }, committed) == S_OK);
    };

    for (const bool dictionary : {true, false}) {
        for (int failure = 0; failure < (dictionary ? 3 : 4); ++failure) {
            const bool unsupported = failure == 1;
            const auto directory = base / (dictionary ? L"dict" : L"freq") /
                (unsupported ? L"operation" : failure == 2 ? L"code" : failure == 3 ? L"word" : L"payload");
            QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
            const auto path = directory / L"source.txt";
            WubiEngine writer;
            WubiEngine reader;
            if (dictionary) {
                QIQI_TEST_CHECK(writer.AddUserEntry(L"abcd", L"original", path));
                reader.SetUserDictPath(path);
                QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
            } else {
                QIQI_TEST_CHECK(writer.PromoteUserPhraseFrequency(L"abcd", L"original"));
                QIQI_TEST_CHECK(writer.SavePhraseFrequencies(path));
                reader.SetPhraseFreqPath(path);
                QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
            }
            reader.AddEntry(L"abcd", L"victim");
            reader.PinEntry(L"abcd", L"pinned");
            reader.BlockEntry(L"abcd", L"hidden");
            if (dictionary) QIQI_TEST_CHECK(reader.PromoteUserPhraseFrequency(L"abcd", L"victim"));
            const Entries words = reader.UserDictionaryEntries();
            const Entries pins = reader.PinnedEntries();
            const Entries blocks = reader.BlockedEntries();
            const Entries promotions = reader.PromotedPhrases();
            const auto dict_version = reader.UserDictSyncVersion();
            const auto freq_version = reader.PhraseFreqSyncVersion();
            const auto snapshots = dictionary ? reader.UserDictSnapshotSyncCount() : reader.PhraseFreqSnapshotSyncCount();
            const auto increments = dictionary ? reader.UserDictIncrementalSyncCount() : reader.PhraseFreqIncrementalSyncCount();
            const sync::Channel channel(GetSyncChannelDirectoryForFile(path),
                {dictionary ? sync::DataType::Dictionary : sync::DataType::PhraseFrequency,
                 dictionary ? kGuidSyncSourceDictionary : kGuidSyncSourcePhraseFrequency});
            publish(channel, dictionary ? sync::Operation::Add : sync::Operation::Reorder, payload(L"added"));
            if (dictionary) {
                publish(channel, sync::Operation::Add, payload(L"^newpin"));
                publish(channel, sync::Operation::Add, payload(L"!victim"));
                publish(channel, sync::Operation::Modify, payload(L"original", L"modified"));
                publish(channel, sync::Operation::Remove, payload(L"^pinned"));
                publish(channel, sync::Operation::Remove, payload(L"!hidden"));
            } else {
                publish(channel, sync::Operation::Remove, payload(L"original"));
            }
            auto invalid = failure == 0 ? std::vector<uint8_t>{} : payload(L"unsupported");
            if (failure == 2) WriteBinaryValue(invalid, 0, uint32_t{0x01ff0000});
            if (failure == 3) invalid = payload(L"x");
            publish(channel, unsupported ?
                (dictionary ? sync::Operation::Reorder : sync::Operation::Add) :
                failure == 3 ? sync::Operation::Reorder : sync::Operation::Remove,
                invalid);
            for (int attempt = 0; attempt < 2; ++attempt) {
                const HRESULT hr = dictionary ?
                    reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) :
                    reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
                QIQI_TEST_CHECK(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
                QIQI_TEST_CHECK(reader.UserDictionaryEntries() == words && reader.PinnedEntries() == pins);
                QIQI_TEST_CHECK(reader.BlockedEntries() == blocks && reader.PromotedPhrases() == promotions);
                QIQI_TEST_CHECK(reader.UserDictSyncVersion() == dict_version &&
                                reader.PhraseFreqSyncVersion() == freq_version);
                QIQI_TEST_CHECK((dictionary ? reader.UserDictSnapshotSyncCount() :
                    reader.PhraseFreqSnapshotSyncCount()) == snapshots);
                QIQI_TEST_CHECK((dictionary ? reader.UserDictIncrementalSyncCount() :
                    reader.PhraseFreqIncrementalSyncCount()) == increments);
            }
        }
    }

    for (const bool dictionary : {true, false}) {
        const auto directory = base / (dictionary ? L"dict-snapshot" : L"freq-snapshot");
        QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
        const auto path = directory / L"source.txt";
        WubiEngine writer;
        WubiEngine reader;
        if (dictionary) {
            QIQI_TEST_CHECK(writer.AddUserEntry(L"abcd", L"original", path));
            reader.SetUserDictPath(path);
            QIQI_TEST_CHECK(reader.LoadDictionaryFile(path));
            QIQI_TEST_CHECK(reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        } else {
            QIQI_TEST_CHECK(writer.PromoteUserPhraseFrequency(L"abcd", L"original"));
            QIQI_TEST_CHECK(writer.SavePhraseFrequencies(path));
            reader.SetPhraseFreqPath(path);
            QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
        }
        reader.PinEntry(L"abcd", L"pinned");
        reader.BlockEntry(L"abcd", L"hidden");
        if (dictionary) QIQI_TEST_CHECK(reader.PromoteUserPhraseFrequency(L"abcd", L"original"));
        const auto lookup = reader.LookupExact(L"abcd");
        const Entries words = reader.UserDictionaryEntries();
        const Entries pins = reader.PinnedEntries();
        const Entries blocks = reader.BlockedEntries();
        const Entries promotions = reader.PromotedPhrases();
        const auto dict_version = reader.UserDictSyncVersion();
        const auto freq_version = reader.PhraseFreqSyncVersion();
        const auto channel_dir = GetSyncChannelDirectoryForFile(path);
        const auto snapshot = channel_dir / (dictionary ? L"snapshot.txt" : L"snapshot.qfreq");
        std::string original;
        QIQI_TEST_CHECK(ReadFileToUtf8String(snapshot, original));
        const sync::Channel channel(channel_dir,
            {dictionary ? sync::DataType::Dictionary : sync::DataType::PhraseFrequency,
             dictionary ? kGuidSyncSourceDictionary : kGuidSyncSourcePhraseFrequency});
        sync::Batch batch;
        QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
        sync::Version unused;
        QIQI_TEST_CHECK(channel.Publish(batch.version, sync::Operation::Add, {},
            []() { return S_FALSE; }, unused) == S_FALSE);
        {
            const ScopedFileHandle locked(::CreateFileW(snapshot.c_str(), GENERIC_READ, 0,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QIQI_TEST_CHECK(locked);
            const ScopedFileHandle source_locked(::CreateFileW(path.c_str(), GENERIC_READ, 0,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QIQI_TEST_CHECK(source_locked);
            const HRESULT hr = dictionary ?
                reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) :
                reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
            QIQI_TEST_CHECK(FAILED(hr));
            QIQI_TEST_CHECK(reader.UserDictionaryEntries() == words && reader.PinnedEntries() == pins);
            QIQI_TEST_CHECK(reader.BlockedEntries() == blocks && reader.PromotedPhrases() == promotions);
            QIQI_TEST_CHECK(reader.LookupExact(L"abcd") == lookup);
            QIQI_TEST_CHECK(reader.UserDictSyncVersion() == dict_version &&
                            reader.PhraseFreqSyncVersion() == freq_version);
            QIQI_TEST_CHECK((dictionary ? reader.UserDictSnapshotSyncCount() :
                reader.PhraseFreqSnapshotSyncCount()) == 1);
        }
        if (!dictionary) {
            constexpr std::array<uint8_t, 3> invalid{1, 2, 3};
            QIQI_TEST_CHECK(WriteFileAtomicallyResult(snapshot, invalid) == S_OK);
            QIQI_TEST_CHECK(reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) ==
                            HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            QIQI_TEST_CHECK(reader.UserDictionaryEntries() == words && reader.PinnedEntries() == pins);
            QIQI_TEST_CHECK(reader.BlockedEntries() == blocks && reader.PromotedPhrases() == promotions);
            QIQI_TEST_CHECK(reader.PhraseFreqSyncVersion() == freq_version &&
                            reader.PhraseFreqSnapshotSyncCount() == 1);
            QIQI_TEST_CHECK(WriteFileAtomicallyResult(snapshot, codec::AsByteSpan(original)) == S_OK);
        }
        const HRESULT recovered = dictionary ?
            reader.SynchronizeUserDictionary(sync::InputBoundary::NewInput) :
            reader.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput);
        QIQI_TEST_CHECK(recovered == S_OK);
        QIQI_TEST_CHECK((dictionary ? reader.UserDictSnapshotSyncCount() :
            reader.PhraseFreqSnapshotSyncCount()) == 2);
        QIQI_TEST_CHECK((dictionary ? reader.UserDictSyncVersion() : reader.PhraseFreqSyncVersion()) !=
                        (dictionary ? dict_version : freq_version));
    }
}

void TestPhraseFrequencySaveFailures() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"qiqiime_freq_save_failures_" + std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        QIQI_TEST_CHECK(!error);
    });
    const auto path = directory / L"frequency.qfreq";
    const auto channel = GetSyncChannelDirectoryForFile(path);
    const auto snapshot = channel / L"snapshot.qfreq";
    WubiEngine engine;
    QIQI_TEST_CHECK(engine.PromoteUserPhraseFrequency(L"abcd", L"original"));
    QIQI_TEST_CHECK(engine.SavePhraseFrequencies(path));
    for (const bool block_channel : {false, true}) {
        std::string before;
        QIQI_TEST_CHECK(ReadFileToUtf8String(snapshot, before));
        QIQI_TEST_CHECK(engine.PromoteUserPhraseFrequency(L"abcd",
            block_channel ? L"channel-failure" : L"snapshot-failure"));
        {
            const auto target = block_channel ? channel / L"writer.lock" : snapshot;
            const ScopedFileHandle blocker(::CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QIQI_TEST_CHECK(blocker);
            QIQI_TEST_CHECK(!engine.SavePhraseFrequencies(path));
            std::string raw;
            QIQI_TEST_CHECK(ReadFileToUtf8String(snapshot, raw) && raw == before);
            QIQI_TEST_CHECK(ReadFileToUtf8String(path, raw) && raw != before);
        }
        QIQI_TEST_CHECK(engine.SavePhraseFrequencies(path));
        std::string source_bytes;
        std::string snapshot_bytes;
        QIQI_TEST_CHECK(ReadFileToUtf8String(path, source_bytes) &&
                        ReadFileToUtf8String(snapshot, snapshot_bytes) && source_bytes == snapshot_bytes);
        std::map<uint32_t, std::vector<std::wstring>> loaded;
        QIQI_TEST_CHECK(WubiMappedDict::DecodeBinaryBufferToEntries(codec::AsByteSpan(snapshot_bytes), loaded,
            codec::kPhraseFreqBinaryMagic, codec::kPhraseFreqBinaryVer));
        QIQI_TEST_CHECK(loaded == engine.PromotedPhrases());
    }
}

} // namespace

void RunEngineDictTests() {
    TestPhraseFrequencySaveFailures();
    TestSyncFailureAtomicity();
    TestBinaryDictBufferValidation();
    TestAddEntryAndLookupEdgeCases();
    TestExternalDictFile();
    TestCrossProcessDictAndPhraseFreqSync();
}

} // namespace qiqi::test
