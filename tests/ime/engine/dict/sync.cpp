// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dict/test.h"

namespace qiqi::test::dict_cases {

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
    const auto freq_file = base_dir / L"phrase_freq.qfreq";

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

} // namespace qiqi::test::dict_cases
