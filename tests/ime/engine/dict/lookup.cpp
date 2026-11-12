// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dict/test.h"

namespace qiqi::test::dict_cases {

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

void TestCustomPrimaryDictionaryAndFallback() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_custom_primary_dict_test_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        WubiMappedDict::ClearProcessCache();
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto bundled_jidian = temp_dir / L"wubi86_jidian.txt";
    const auto custom_txt = temp_dir / L"my_custom_primary.txt";
    const auto custom_qdict = temp_dir / L"standalone_custom.qdict";
    const auto empty_txt = temp_dir / L"empty_dict.txt";
    const auto invalid_txt = temp_dir / L"not_a_wubi_dict.txt";
    const auto missing_txt = temp_dir / L"does_not_exist.txt";

    constexpr std::string_view kBundledContent = "aaaa 极点默认词\n";
    constexpr std::string_view kCustomContent =
        "# Custom Wubi 86 primary dictionary\n"
        "aaaa 自定义首选词 自定义次选词\n"
        "bbbb 自定义专属词\n";
    QIQI_TEST_CHECK(WriteFileAtomically(bundled_jidian, codec::AsByteSpan(kBundledContent)));
    QIQI_TEST_CHECK(WriteFileAtomically(custom_txt, codec::AsByteSpan(kCustomContent)));
    QIQI_TEST_CHECK(WriteFileAtomically(empty_txt, codec::AsByteSpan(std::string_view("# comments only\n"))));
    QIQI_TEST_CHECK(WriteFileAtomically(invalid_txt, codec::AsByteSpan(std::string_view("123456789 987654321\n"))));
    QIQI_TEST_CHECK(WubiMappedDict::CompileTextFileToQdict(custom_txt, custom_qdict));

    // 1. ValidateAndLoadPrimaryDictionary failure and success cases
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary({}).status ==
                    PrimaryDictValidationStatus::EmptyPath);
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary(missing_txt).status ==
                    PrimaryDictValidationStatus::FileNotFound);
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary(empty_txt).status ==
                    PrimaryDictValidationStatus::InvalidFormatOrEmpty);
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary(invalid_txt).status ==
                    PrimaryDictValidationStatus::InvalidFormatOrEmpty);
    {
        const ScopedFileHandle locked(::CreateFileW(
            custom_txt.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked.IsValid());
        const auto locked_res = WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_txt);
        QIQI_TEST_CHECK(!locked_res &&
                        locked_res.status == PrimaryDictValidationStatus::AccessDeniedOrReadError);
    }

    const auto valid_txt_res = WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_txt);
    QIQI_TEST_CHECK(valid_txt_res && valid_txt_res.dictionary->CodeCount() == 2 &&
                    valid_txt_res.dictionary->WordCount() == 3);
    const auto valid_qdict_res = WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_qdict);
    QIQI_TEST_CHECK(valid_qdict_res && valid_qdict_res.dictionary->CodeCount() == 2 &&
                    valid_qdict_res.dictionary->WordCount() == 3);
    QIQI_TEST_CHECK(valid_txt_res.diagnostics.valid_lines == 2 &&
        valid_txt_res.diagnostics.invalid_lines == 0);
    QIQI_TEST_CHECK(valid_txt_res.dictionary->SourcePath() == custom_txt &&
        !valid_txt_res.dictionary->IsSourceModified());
    QIQI_TEST_CHECK(valid_qdict_res.dictionary->SourcePath() == custom_qdict &&
        !valid_qdict_res.dictionary->IsSourceModified());

    const auto partial_txt = temp_dir / L"partial.txt";
    constexpr std::string_view partial_content =
        "# comment\n"
        "aaaa 有效词\n"
        "abcde 12345\n"
        "bbbb\n"
        "反查词 cccc 100\n"
        "错误词 dddd invalid-weight\n";
    QIQI_TEST_CHECK(WriteFileAtomically(partial_txt, codec::AsByteSpan(partial_content)));
    const auto partial = WubiMappedDict::ValidateAndLoadPrimaryDictionary(partial_txt);
    QIQI_TEST_CHECK(partial && partial.diagnostics.valid_lines == 2 &&
        partial.diagnostics.invalid_lines == 3 && partial.dictionary->WordCount() == 2);
    QIQI_TEST_CHECK(partial.diagnostics.issues.at(0).line == 3 &&
        partial.diagnostics.issues.at(0).error == DictionaryLineError::InvalidCode);
    QIQI_TEST_CHECK(partial.diagnostics.issues.at(1).line == 4 &&
        partial.diagnostics.issues.at(1).error == DictionaryLineError::MissingWord);
    QIQI_TEST_CHECK(partial.diagnostics.issues.at(2).line == 6 &&
        partial.diagnostics.issues.at(2).error == DictionaryLineError::InvalidWeight);
    {
        std::map<uint32_t, std::vector<std::wstring>> parsed;
        DictionaryTextDiagnostics report;
        WubiMappedDict::ParseTextDictEntries(
            L"---\nname: test\n...\n词语 abcd 1.5\n", parsed, false, &report);
        QIQI_TEST_CHECK(report.valid_lines == 1 && report.invalid_lines == 0 && parsed.size() == 1);
        WubiMappedDict::ParseTextDictEntries(L"---\nname: test\n", parsed, false, &report);
        QIQI_TEST_CHECK(parsed.empty() && report.invalid_lines == 1 &&
            report.issues.at(0).error == DictionaryLineError::UnterminatedHeader);
        std::wstring controls = L"aaaa ";
        controls.push_back(L'\0');
        controls += L"词\n";
        WubiMappedDict::ParseTextDictEntries(controls, parsed, false, &report);
        QIQI_TEST_CHECK(parsed.empty() && report.invalid_lines == 1 &&
            report.issues.at(0).error == DictionaryLineError::InvalidCharacter);
        WubiMappedDict::ParseTextDictEntries(
            L"1 1\n2 2\n3 3\n4 4\n5 5\n6 6\n7 7\n8 8\n9 9\n", parsed, false, &report);
        QIQI_TEST_CHECK(report.invalid_lines == 9 && report.issues.size() == 8);
    }
    const auto utf8_txt = temp_dir / L"invalid_utf8.txt";
    const std::string bad_utf8 = std::string("aaaa ") + '\xC0' + '\xAF' + '\n';
    QIQI_TEST_CHECK(WriteFileAtomically(utf8_txt, codec::AsByteSpan(bad_utf8)));
    const auto utf8_validation = WubiMappedDict::ValidateAndLoadPrimaryDictionary(utf8_txt);
    QIQI_TEST_CHECK(!utf8_validation && utf8_validation.status == PrimaryDictValidationStatus::InvalidUtf8 &&
        utf8_validation.win32_error == ERROR_NO_UNICODE_TRANSLATION);
    QIQI_TEST_CHECK(!WubiMappedDict::LoadFromTextOrCache(utf8_txt));
    const auto bom_txt = temp_dir / L"bom.txt";
    const std::string bom_text = std::string("\xEF\xBB\xBF") + "aaaa 词语\r\n";
    QIQI_TEST_CHECK(WriteFileAtomically(bom_txt, codec::AsByteSpan(bom_text)));
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary(bom_txt));

    const auto cached_txt = temp_dir / L"cached.txt";
    const auto cached_bin = temp_dir / L"cached.qdict";
    QIQI_TEST_CHECK(WriteFileAtomically(cached_txt, codec::AsByteSpan(std::string_view("aaaa valid\n"))));
    QIQI_TEST_CHECK(WubiMappedDict::CompileTextFileToQdict(cached_txt, cached_bin));
    const auto original_time = std::filesystem::last_write_time(cached_txt);
    QIQI_TEST_CHECK(WriteFileAtomically(cached_txt, codec::AsByteSpan(std::string_view("1234 56789\n"))));
    std::filesystem::last_write_time(cached_txt, original_time);
    QIQI_TEST_CHECK(!WubiMappedDict::ValidateAndLoadPrimaryDictionary(cached_txt));
    QIQI_TEST_CHECK(std::filesystem::remove(cached_txt));
    QIQI_TEST_CHECK(WubiMappedDict::ValidateAndLoadPrimaryDictionary(cached_txt).status ==
        PrimaryDictValidationStatus::FileNotFound);
    const auto corrupt_bin = temp_dir / L"corrupt.qdict";
    QIQI_TEST_CHECK(WriteFileAtomically(corrupt_bin, codec::AsByteSpan(std::string_view("not a dictionary"))));
    QIQI_TEST_CHECK(!WubiMappedDict::ValidateAndLoadPrimaryDictionary(corrupt_bin));

    // 2. Priority verification: ^Pinned > PromotedPhrase > CustomPrimaryDict + UserDict, !Blocked excluded,
    //    and custom primary dictionary file on disk is never modified.
    WubiEngine engine;
    QiQiConfig cfg;
    cfg.primary_dict = PrimaryDict::Custom;
    cfg.primary_dict_path = custom_txt.wstring();
    cfg.dynamic_phrase_freq = true;
    engine.ApplyConfig(cfg);
    const auto load_summary = engine.LoadConfiguredPrimaryDictionaries(cfg, temp_dir, true);
    QIQI_TEST_CHECK(SUCCEEDED(load_summary.hr) && !load_summary.used_fallback && load_summary.loaded_count == 1);

    engine.AddEntry(L"aaaa", L"用户补充词");
    engine.PinEntry(L"aaaa", L"手动置顶词");
    engine.BlockEntry(L"aaaa", L"自定义次选词");
    const auto exact = engine.LookupExact(L"aaaa");
    QIQI_TEST_CHECK(exact.size() >= 3);
    QIQI_TEST_CHECK(exact.at(0) == L"手动置顶词");
    QIQI_TEST_CHECK(exact.at(1) == L"自定义首选词");
    QIQI_TEST_CHECK(exact.at(2) == L"用户补充词");
    QIQI_TEST_CHECK(std::ranges::find(exact, L"自定义次选词") == exact.end());

    std::string after_custom_bytes;
    QIQI_TEST_CHECK(ReadFileToUtf8String(custom_txt, after_custom_bytes) &&
                    after_custom_bytes == kCustomContent);

    // 3. Hot-reload failure retains previously active primary dictionary when retain_existing_on_custom_failure=true
    cfg.primary_dict_path = missing_txt.wstring();
    const auto retained_summary = engine.LoadConfiguredPrimaryDictionaries(cfg, temp_dir, true);
    QIQI_TEST_CHECK(FAILED(retained_summary.hr) && retained_summary.used_fallback &&
                    retained_summary.retained_previous);
    const auto still_custom = engine.LookupExact(L"bbbb");
    QIQI_TEST_CHECK(!still_custom.empty() && still_custom.front() == L"自定义专属词");

    // 4. Fresh load failure falls back to bundled Jidian primary dictionary when retain_existing_on_custom_failure=false
    const auto fallback_summary = engine.LoadConfiguredPrimaryDictionaries(cfg, temp_dir, false);
    QIQI_TEST_CHECK(FAILED(fallback_summary.hr) && fallback_summary.used_fallback &&
                    !fallback_summary.retained_previous);
    const auto bundled_fallback = engine.LookupExact(L"aaaa");
    QIQI_TEST_CHECK(std::ranges::find(bundled_fallback, L"极点默认词") != bundled_fallback.end());

    WubiEngine reload_engine;
    reload_engine.SetUserDictPath(temp_dir / L"isolated_user.txt");
    cfg.primary_dict_path = partial_txt.wstring();
    QIQI_TEST_CHECK(SUCCEEDED(reload_engine.LoadConfiguredPrimaryDictionaries(cfg, temp_dir, false).hr));
    QIQI_TEST_CHECK(WriteFileAtomically(partial_txt, codec::AsByteSpan(std::string_view("12345 67890\n"))));
    std::ignore = reload_engine.SynchronizeCrossProcessChanges(sync::InputBoundary::NewInput);
    QIQI_TEST_CHECK(!reload_engine.LookupExact(L"aaaa").empty());
    QIQI_TEST_CHECK(WriteFileAtomically(partial_txt, codec::AsByteSpan(std::string_view("eeee 更新后的词条\n"))));
    std::ignore = reload_engine.SynchronizeCrossProcessChanges(sync::InputBoundary::NewInput);
    QIQI_TEST_CHECK(!reload_engine.LookupExact(L"eeee").empty());
    cfg.primary_dict_path = missing_txt.wstring();
    const auto retained_reload = reload_engine.LoadConfiguredPrimaryDictionaries(cfg, temp_dir, true);
    QIQI_TEST_CHECK(retained_reload.retained_previous && !reload_engine.LookupExact(L"eeee").empty());
}

} // namespace qiqi::test::dict_cases
