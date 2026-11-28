// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/syncutil.h"

namespace qiqi::test::changesync {
template <typename Action>
void WithAppContainerProfile(const Action& action) {
    const auto name = L"QiQiSyncTest." + GuidText();
    PSID sid = nullptr;
    const HRESULT profile = ::CreateAppContainerProfile(name.c_str(), name.c_str(), L"QiQi sync test",
                                                        nullptr, 0, &sid);
    if (FAILED(profile)) std::cerr << "CreateAppContainerProfile hr=" << std::hex << profile << "\n";
    QIQI_TEST_CHECK(SUCCEEDED(profile));
    const auto cleanup = gsl::finally([&name, sid]() noexcept {
        ::FreeSid(sid);
        const HRESULT hr = ::DeleteAppContainerProfile(name.c_str());
        if (FAILED(hr)) {
            std::fprintf(stderr, "Test AppContainer profile cleanup failed (pid=%lu, hr=0x%08lx)\n",
                         ::GetCurrentProcessId(), gsl::narrow_cast<unsigned long>(hr));
            std::fflush(stderr);
            std::abort();
        }
    });
    SECURITY_CAPABILITIES capabilities{};
    capabilities.AppContainerSid = sid;
    action(capabilities);
}

void RequireAppContainerToken() {
    ScopedKernelHandle token;
    QIQI_TEST_CHECK(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put()));
    DWORD container = 0;
    DWORD size = 0;
    QIQI_TEST_CHECK(::GetTokenInformation(
        token.Get(), TokenIsAppContainer, &container, sizeof(container), &size) && container);
}

struct ChangeHandleTraits : FileHandleTraits {
    static void Close(HANDLE value) noexcept { ::FindCloseChangeNotification(value); }
};

void ProbeReader(const std::filesystem::path& directory, const wchar_t* registry_subkey, HANDLE ready) {
    const TestStage stage(__func__);
    RequireAppContainerToken();
    {
        WubiEngine practice;
        KeyProcessResult update;
        const auto denied = directory / L"denied.idx";
        const auto allowed = directory / L"full.idx";
        QIQI_TEST_CHECK(practice.SetCandidateFilters(false, false, update, denied) == S_OK);
        QIQI_TEST_CHECK(practice.SetCandidateFilters(false, true, update, denied) == E_ACCESSDENIED);
        QIQI_TEST_CHECK(std::ranges::contains(practice.LookupCandidates(L"vb"), L"好"));
        const ScopedFileHandle writer(::CreateFileW(allowed.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(!writer && ::GetLastError() == ERROR_ACCESS_DENIED);
        QIQI_TEST_CHECK(practice.SetCandidateFilters(false, true, update, allowed) == S_OK);
        QIQI_TEST_CHECK(practice.LookupCandidates(L"vb").empty());
        QIQI_TEST_CHECK(practice.LookupCandidates(L"vbg") == std::vector<std::wstring>{L"好"});
    }
    {
        WubiEngine pinyin;
        const auto denied = directory / L"denied-pinyin.idx";
        const auto allowed = directory / L"pinyin.idx";
        QIQI_TEST_CHECK(pinyin.SetZPinyinLookup(false, denied) == S_OK);
        QIQI_TEST_CHECK(pinyin.SetZPinyinLookup(true, denied) == E_ACCESSDENIED);
        QIQI_TEST_CHECK(pinyin.ProcessKey('Z').action == KeyActionType::UpdateState && pinyin.IsTempEnglishMode());
        pinyin.Reset();
        const ScopedFileHandle writer(::CreateFileW(allowed.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(!writer && ::GetLastError() == ERROR_ACCESS_DENIED);
        QIQI_TEST_CHECK(pinyin.SetZPinyinLookup(true, allowed) == S_OK);
        for (const UINT key : {'Z', 'N', 'I'}) {
            QIQI_TEST_CHECK(pinyin.ProcessKey(key).action == KeyActionType::UpdateState);
        }
        QIQI_TEST_CHECK(pinyin.ProcessKey(VK_SPACE).committed_text == L"你");
    }
    SetConfigRegistrySubKeyOverrideForTest(registry_subkey);
    QIQI_TEST_CHECK(::SetEnvironmentVariableW(L"LOCALAPPDATA", L"C:\\QiQiContainerPrivate"));
    std::filesystem::path discovered;
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(discovered) == S_OK);
    QIQI_TEST_CHECK(discovered == GetSyncChannelDirectoryForFile(directory / L"user_dict.txt"));
    QIQI_TEST_CHECK(SaveUserDictSyncDirectory(discovered) == E_ACCESSDENIED);
    const auto path = directory / L"lock";
    const ScopedFileHandle reader(::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(reader);
    OVERLAPPED position{};
    QIQI_TEST_CHECK(::LockFileEx(reader.Get(), LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &position));
    QIQI_TEST_CHECK(::UnlockFileEx(reader.Get(), 0, 1, 0, &position));
    const ScopedFileHandle writer(::CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(!writer && ::GetLastError() == ERROR_ACCESS_DENIED);
    const ScopedHandle<ChangeHandleTraits> notification(::FindFirstChangeNotificationW(
        directory.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE));
    QIQI_TEST_CHECK(notification);
    Channel channel(directory, kSource);
    QIQI_TEST_CHECK(channel.Watch() == S_OK);
    Batch initial;
    QIQI_TEST_CHECK(channel.Read({}, initial) == S_OK);
    Version applied = initial.version;
    Version denied;
    bool called = false;
    QIQI_TEST_CHECK(channel.Publish(applied, Operation::Add, kPayload,
        [&]() { called = true; return S_OK; }, denied) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(!called && denied == Version{});
    QIQI_TEST_CHECK(Channel::Create(directory / L"forbidden", kSource) == E_ACCESSDENIED);
    const auto user_dict = directory / L"user_dict.txt";
    const auto freq_file = directory / L"phrase_freq.qfreq";
    WubiEngine container_engine;
    container_engine.SetUserDictPath(GetDefaultUserDictPath());
    container_engine.SetPhraseFreqPath(freq_file);
    container_engine.SetDynamicPhraseFreq(true);
    std::string inaccessible_source;
    QIQI_TEST_CHECK(!ReadFileToUtf8String(user_dict, inaccessible_source));
    const auto missing_subkey = std::wstring(registry_subkey) + L"_missing";
    SetConfigRegistrySubKeyOverrideForTest(missing_subkey.c_str());
    const HRESULT missing_path_hr = container_engine.InitializeUserDictionarySharing();
    QIQI_TEST_CHECK(FAILED(missing_path_hr));
    QIQI_TEST_CHECK(container_engine.SynchronizeUserDictionary(InputBoundary::NewInput) == missing_path_hr);
    SetConfigRegistrySubKeyOverrideForTest(registry_subkey);
    for (const auto& [name, expected] : std::array{
        std::pair{L"MissingSnapshot", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)},
        std::pair{L"DeniedSnapshot", E_ACCESSDENIED},
        std::pair{L"MissingJournal", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)}}) {
        const auto failure_subkey = std::wstring(registry_subkey) + L"\\" + name;
        SetConfigRegistrySubKeyOverrideForTest(failure_subkey.c_str());
        WubiEngine failed_reader;
        QIQI_TEST_CHECK(failed_reader.InitializeUserDictionarySharing() == expected);
        QIQI_TEST_CHECK(failed_reader.UserDictSyncVersion() == Version{});
        QIQI_TEST_CHECK(failed_reader.UserDictionaryEntries().empty());
    }
    SetConfigRegistrySubKeyOverrideForTest(registry_subkey);
    QIQI_TEST_CHECK(container_engine.InitializeUserDictionarySharing() == S_OK);
    QIQI_TEST_CHECK(container_engine.SynchronizeCrossProcessChanges(InputBoundary::NewInput));
    QIQI_TEST_CHECK(container_engine.UserDictSnapshotSyncCount() == 1);
    QIQI_TEST_CHECK(container_engine.PhraseFreqSnapshotSyncCount() == 1);
    const auto init_qrst = container_engine.LookupExact(L"qrst");
    QIQI_TEST_CHECK(init_qrst.size() == 2 && gsl::at(init_qrst, 0) == L"短语一" && gsl::at(init_qrst, 1) == L"短语二");
    QIQI_TEST_CHECK(!container_engine.AddUserEntry(L"abcd", L"禁止写入", user_dict));
    QIQI_TEST_CHECK(!container_engine.RemoveUserEntry(L"qrst", L"短语一", user_dict));
    QIQI_TEST_CHECK(!container_engine.ModifyUserEntry(L"qrst", L"短语一", L"禁止修改", user_dict));
    QIQI_TEST_CHECK(!container_engine.SavePhraseFrequencies(freq_file));
    SendReady(ready);
    const auto deadline = ::GetTickCount64() + kTimeout;
    for (;;) {
        const auto now = ::GetTickCount64();
        if (now >= deadline) break;
        QIQI_TEST_CHECK(::WaitForSingleObject(notification.Get(),
            gsl::narrow_cast<DWORD>(deadline - now)) == WAIT_OBJECT_0);
        QIQI_TEST_CHECK(::FindNextChangeNotification(notification.Get()));
        if (::GetFileAttributesW((directory / L"published").c_str()) != INVALID_FILE_ATTRIBUTES) {
            HRESULT hr = E_PENDING;
            bool received = false;
            while (::GetTickCount64() < deadline) {
                hr = channel.Synchronize(applied, InputBoundary::NewInput,
                    []() { QIQI_TEST_CHECK(false); return E_FAIL; },
                    [&](std::span<const Change> changes) {
                        std::string contents;
                        QIQI_TEST_CHECK(ReadFileToUtf8String(directory / L"published", contents) && contents == "delta");
                        QIQI_TEST_CHECK(changes.size() == 1);
                        QIQI_TEST_CHECK(changes.front().operation == Operation::Add);
                        QIQI_TEST_CHECK(std::ranges::equal(changes.front().payload, kPayload));
                        received = true;
                        return S_OK;
                    });
                if (hr != HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION)) break;
                ::SwitchToThread();
            }
            QIQI_TEST_CHECK(hr == S_OK && received && applied.sequence == 1);
            QIQI_TEST_CHECK(channel.Rearm() == S_OK);
            QIQI_TEST_CHECK(container_engine.SynchronizeCrossProcessChanges(InputBoundary::NewInput));
            QIQI_TEST_CHECK(container_engine.UserDictIncrementalSyncCount() == 1);
            QIQI_TEST_CHECK(container_engine.PhraseFreqIncrementalSyncCount() == 1);
            const auto synced_abcd = container_engine.LookupExact(L"abcd");
            QIQI_TEST_CHECK(synced_abcd.size() == 1 && gsl::at(synced_abcd, 0) == L"容器增量词");
            const auto synced_qrst = container_engine.LookupExact(L"qrst");
            QIQI_TEST_CHECK(synced_qrst.size() == 2 && gsl::at(synced_qrst, 0) == L"短语二" &&
                            gsl::at(synced_qrst, 1) == L"短语一");
            SendReady(ready);
            while (::GetTickCount64() < deadline) {
                if (::GetFileAttributesW((directory / L"switched").c_str()) != INVALID_FILE_ATTRIBUTES) {
                    QIQI_TEST_CHECK(container_engine.InitializeUserDictionarySharing() == S_OK);
                    QIQI_TEST_CHECK(container_engine.UserDictSnapshotSyncCount() == 2);
                    QIQI_TEST_CHECK(container_engine.LookupExact(L"abcd").empty());
                    const auto switched = container_engine.LookupExact(L"qwer");
                    QIQI_TEST_CHECK(switched.size() == 1 && switched.front() == L"切换路径词");
                    SendReady(ready);
                    while (::GetTickCount64() < deadline) {
                        if (::GetFileAttributesW((directory / L"cleared").c_str()) != INVALID_FILE_ATTRIBUTES) {
                            QIQI_TEST_CHECK(container_engine.InitializeUserDictionarySharing() == S_OK);
                            QIQI_TEST_CHECK(container_engine.LookupExact(L"qwer").empty());
                            return;
                        }
                        ::Sleep(10);
                    }
                    QIQI_TEST_CHECK(false);
                }
                ::Sleep(10);
            }
            QIQI_TEST_CHECK(false);
            return;
        }
    }
    QIQI_TEST_CHECK(false);
}

void HoldReader(const std::filesystem::path& directory, std::wstring_view mode,
                HANDLE release, HANDLE ready) {
    const TestStage stage(__func__);
    RequireAppContainerToken();
    QIQI_TEST_CHECK(mode == L"deny-write" || mode == L"shared-lock" || mode == L"deny-delete");
    const auto path = directory / (mode == L"deny-delete" ? L"journal.bin" : L"writer.lock");
    const DWORD sharing = mode == L"deny-write" ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE;
    const ScopedFileHandle reader(::CreateFileW(path.c_str(), GENERIC_READ, sharing, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(reader);
    if (mode == L"shared-lock") {
        OVERLAPPED position{};
        QIQI_TEST_CHECK(::LockFileEx(reader.Get(), LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &position));
    }
    SendReady(ready);
    QIQI_TEST_CHECK(::WaitForSingleObject(release, kTimeout) == WAIT_OBJECT_0);
}

void TestAppContainerTransport(const std::filesystem::path& peer) {
    const TestStage stage(__func__);
    Fixture fixture(L"appcontainer");
    const auto registry_subkey = L"Software\\QiQiIME_SharedPath_" + GuidText();
    SetConfigRegistrySubKeyOverrideForTest(registry_subkey.c_str());
    const auto registry_cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        const LSTATUS error = ::RegDeleteTreeW(HKEY_CURRENT_USER, registry_subkey.c_str());
        if (error != ERROR_SUCCESS) {
            std::fprintf(stderr, "Test registry cleanup failed (pid=%lu, error=%ld)\n",
                ::GetCurrentProcessId(), error);
            std::fflush(stderr);
            std::abort();
        }
    });
    PSECURITY_DESCRIPTOR raw = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;FA;;;OW)", SDDL_REVISION_1, &raw, nullptr));
    const ScopedLocalAlloc private_acl(raw);
    QIQI_TEST_CHECK(::SetFileSecurityW(fixture.directory.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
    QIQI_TEST_CHECK(Channel::Create(fixture.directory, kSource) == S_OK);
    const auto lock_path = fixture.directory / L"lock";
    const ScopedFileHandle lock(::CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(lock);
    const auto user_dict = fixture.directory / L"user_dict.txt";
    const auto freq_file = fixture.directory / L"phrase_freq.qfreq";
    WubiEngine host_engine;
    host_engine.SetUserDictPath(user_dict);
    host_engine.SetPhraseFreqPath(freq_file);
    host_engine.SetDynamicPhraseFreq(true);
    QIQI_TEST_CHECK(host_engine.AddUserEntry(L"qrst", L"短语一", user_dict));
    QIQI_TEST_CHECK(host_engine.AddUserEntry(L"qrst", L"短语二", user_dict));
    QIQI_TEST_CHECK(host_engine.InitializeUserDictionarySharing() == S_OK);
    QIQI_TEST_CHECK(::SetFileSecurityW(user_dict.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
    const auto full_index = GetModuleFilePath(nullptr).parent_path().parent_path() / L"data" / L"full.idx";
    const auto denied_index = fixture.directory / L"denied.idx";
    std::string index_bytes;
    QIQI_TEST_CHECK(ReadFileToUtf8StringResult(full_index, index_bytes) == S_OK);
    QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"full.idx", codec::AsByteSpan(index_bytes)));
    QIQI_TEST_CHECK(WriteFileAtomically(denied_index, codec::AsByteSpan(index_bytes)));
    QIQI_TEST_CHECK(::SetFileSecurityW(denied_index.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
    const auto denied_pinyin = fixture.directory / L"denied-pinyin.idx";
    const auto pinyin_index = full_index.parent_path() / L"pinyin.idx";
    QIQI_TEST_CHECK(ReadFileToUtf8StringResult(pinyin_index, index_bytes) == S_OK);
    QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"pinyin.idx", codec::AsByteSpan(index_bytes)));
    QIQI_TEST_CHECK(WriteFileAtomically(denied_pinyin, codec::AsByteSpan(index_bytes)));
    QIQI_TEST_CHECK(::SetFileSecurityW(denied_pinyin.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
    for (const auto* name : {L"MissingSnapshot", L"DeniedSnapshot", L"MissingJournal"}) {
        const auto source_path = fixture.directory / (std::wstring(name) + L".txt");
        const auto channel_directory = GetSyncChannelDirectoryForFile(source_path);
        WubiEngine failure_writer;
        failure_writer.SetUserDictPath(source_path);
        QIQI_TEST_CHECK(failure_writer.AddUserEntry(L"qwer", L"失败路径词"));
        if (std::wstring_view(name) == L"MissingSnapshot") {
            QIQI_TEST_CHECK(std::filesystem::remove(channel_directory / L"snapshot.txt"));
        } else if (std::wstring_view(name) == L"DeniedSnapshot") {
            QIQI_TEST_CHECK(::SetFileSecurityW((channel_directory / L"snapshot.txt").c_str(),
                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, raw));
        } else {
            QIQI_TEST_CHECK(std::filesystem::remove(channel_directory / L"journal.bin"));
        }
        const auto failure_subkey = registry_subkey + L"\\" + name;
        QIQI_TEST_CHECK(SaveUserDictSyncDirectory(channel_directory, failure_subkey.c_str()) == S_OK);
    }
    for (const uint32_t vk : {'Q', 'R', 'S', 'T'}) host_engine.ProcessKey(vk);
    QIQI_TEST_CHECK(host_engine.CommitCandidate(0).committed_text == L"短语一");

    const auto executable = fixture.directory / L"reader.exe";
    std::filesystem::copy_file(peer.empty() ? GetModuleFilePath(nullptr) : peer, executable);
    WithAppContainerProfile([&](SECURITY_CAPABILITIES& capabilities) {
        Child child(executable, L"--probe \"" + fixture.directory.wstring() + L"\" \"" +
            registry_subkey + L"\"", &capabilities);
        child.AwaitReady();
        QIQI_TEST_CHECK(host_engine.AddUserEntry(L"abcd", L"容器增量词", user_dict));
        for (const uint32_t vk : {'Q', 'R', 'S', 'T'}) host_engine.ProcessKey(vk);
        QIQI_TEST_CHECK(host_engine.CommitCandidate(1).committed_text == L"短语二");
        Channel channel(fixture.directory, kSource);
        Batch batch;
        QIQI_TEST_CHECK(channel.Read({}, batch) == S_OK);
        Version committed;
        QIQI_TEST_CHECK(channel.Publish(batch.version, Operation::Add, kPayload, [&]() {
            return WriteFileAtomicallyResult(fixture.directory / L"published", kPayload);
        }, committed) == S_OK);
        child.AwaitReady();
        const auto alternate = fixture.directory / L"alternate.txt";
        WubiEngine alternate_engine;
        alternate_engine.SetUserDictPath(alternate);
        QIQI_TEST_CHECK(alternate_engine.AddUserEntry(L"qwer", L"切换路径词"));
        QIQI_TEST_CHECK(alternate_engine.InitializeUserDictionarySharing() == S_OK);
        QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"switched", kPayload));
        child.AwaitReady();
        QIQI_TEST_CHECK(std::filesystem::remove(alternate));
        QIQI_TEST_CHECK(alternate_engine.SynchronizeUserDictionary(InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(alternate_engine.LookupExact(L"qwer").empty());
        QIQI_TEST_CHECK(WriteFileAtomically(fixture.directory / L"cleared", kPayload));
        child.Join();
    });
}

void TestReaderInterferenceCharacterization(const std::filesystem::path& peer) {
    const TestStage stage(__func__);
    WithAppContainerProfile([&](SECURITY_CAPABILITIES& capabilities) {
        for (const std::wstring_view mode : {L"deny-write", L"shared-lock", L"deny-delete"}) {
            Fixture fixture(mode);
            Channel channel(fixture.directory, kSource);
            const Version before = Current(channel);
            const auto source = fixture.directory / L"published";
            constexpr std::array<uint8_t, 3> original{'o', 'l', 'd'};
            QIQI_TEST_CHECK(WriteFileAtomicallyResult(source, original) == S_OK);
            std::string journal_before;
            QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"journal.bin", journal_before));
            const auto executable = fixture.directory / L"reader.exe";
            std::filesystem::copy_file(peer.empty() ? GetModuleFilePath(nullptr) : peer, executable);
            SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
            const ScopedKernelHandle release(::CreateEventW(&attributes, TRUE, FALSE, nullptr));
            QIQI_TEST_CHECK(release);
            const auto arguments = L"--hold-reader \"" + fixture.directory.wstring() + L"\" " +
                std::wstring(mode) + L" " + std::to_wstring(reinterpret_cast<uintptr_t>(release.Get()));
            Child child(executable, arguments, &capabilities, release.Get());
            child.AwaitReady();

            const HRESULT expected = mode == L"shared-lock" ? HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) :
                HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION);
            QIQI_TEST_CHECK(Channel::Create(fixture.directory, kSource) ==
                (mode == L"deny-delete" ? S_OK : expected));
            size_t writes = 0;
            const auto write_source = [&]() {
                ++writes;
                return WriteFileAtomicallyResult(source, kPayload);
            };
            for (int attempt = 0; attempt < 2; ++attempt) {
                Version committed;
                const HRESULT blocked = channel.Publish(before, Operation::Add, kPayload, write_source, committed);
                if (mode == L"deny-delete") {
                    QIQI_TEST_CHECK(blocked == E_ACCESSDENIED ||
                                    blocked == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
                } else {
                    QIQI_TEST_CHECK(blocked == expected);
                }
                QIQI_TEST_CHECK(writes == 0 && committed == Version{});
                QIQI_TEST_CHECK(Current(channel) == before);
                std::string text;
                QIQI_TEST_CHECK(ReadFileToUtf8String(source, text) && text == "old");
                QIQI_TEST_CHECK(ReadFileToUtf8String(fixture.directory / L"journal.bin", text) &&
                                text == journal_before);
                QIQI_TEST_CHECK(!std::filesystem::exists((fixture.directory / L"journal.bin").wstring() +
                    L".tmp." + std::to_wstring(::GetCurrentProcessId())));
            }

            QIQI_TEST_CHECK(::SetEvent(release.Get()));
            child.Join();
            QIQI_TEST_CHECK(Channel::Create(fixture.directory, kSource) == S_OK);
            QIQI_TEST_CHECK(Current(channel) == before);
            Version committed;
            QIQI_TEST_CHECK(channel.Publish(before, Operation::Add, kPayload, write_source, committed) == S_OK);
            QIQI_TEST_CHECK(writes == 1 && committed.baseline == before.baseline &&
                            committed.sequence == before.sequence + 1);
            std::string text;
            QIQI_TEST_CHECK(ReadFileToUtf8String(source, text) && text == "delta");
            Version applied = before;
            size_t deltas = 0;
            const auto apply_delta = [&](std::span<const Change> changes) {
                QIQI_TEST_CHECK(changes.size() == 1 && changes.front().sequence == committed.sequence);
                QIQI_TEST_CHECK(changes.front().operation == Operation::Add &&
                                std::ranges::equal(changes.front().payload, kPayload));
                ++deltas;
                return S_OK;
            };
            for (int attempt = 0; attempt < 2; ++attempt) {
                QIQI_TEST_CHECK(channel.Synchronize(applied, InputBoundary::NewInput,
                    []() { return E_UNEXPECTED; }, apply_delta) == S_OK);
                QIQI_TEST_CHECK(applied == committed && deltas == 1);
            }
        }
    });
}
} // namespace qiqi::test::changesync
