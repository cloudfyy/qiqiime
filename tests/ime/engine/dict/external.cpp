// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dict/test.h"

namespace qiqi::test::dict_cases {

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

} // namespace qiqi::test::dict_cases
