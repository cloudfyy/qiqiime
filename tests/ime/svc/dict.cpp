// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/test.h"

namespace qiqi::test::service_cases {

void TestProcessDictCacheLifetime() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto dict_path = std::filesystem::temp_directory_path() /
                           std::format(L"qiqi_process_cache_test_{}.txt", ::GetCurrentProcessId());
    std::filesystem::path qdict_path = dict_path;
    qdict_path.replace_extension(L".qdict");
    const auto cleanup_files = gsl::finally([&]() noexcept {
        WubiMappedDict::ClearProcessCache();
        std::error_code ec;
        std::filesystem::remove(dict_path, ec);
        std::filesystem::remove(qdict_path, ec);
        std::filesystem::remove_all(GetSyncChannelDirectoryForFile(dict_path), ec);
    });
    {
        std::ofstream out(dict_path, std::ios::binary | std::ios::trunc);
        out << "xvwq 共享词一 共享词二\n";
        out.close();
        QIQI_TEST_CHECK(out.good());
    }

    const QiQiConfig saved_config = LoadConfig(g_dll_instance);
    auto config = saved_config;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    config.user_dict_path = dict_path.wstring();
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.show_status_bar = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config));
    });

    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0);

    auto service_a = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_a = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 1);
    auto cached_dict = WubiMappedDict::LoadFromTextOrCache(dict_path);
    QIQI_TEST_CHECK(cached_dict != nullptr);
    const std::weak_ptr<const WubiMappedDict> weak_dict = cached_dict;

    auto service_b = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_b = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service_a->Activate(manager_a.Get(), 1) == S_OK);
    std::filesystem::path published_directory;
    QIQI_TEST_CHECK(LoadUserDictSyncDirectory(published_directory) == S_OK &&
        published_directory == GetSyncChannelDirectoryForFile(dict_path));
    QIQI_TEST_CHECK(service_b->Activate(manager_b.Get(), 2) == S_OK);

    // Deactivating service_a releases its own engine references, but preserves the process-wide dictionary cache
    // so service_b and subsequent reactivations do not re-map the dictionary file from disk.
    QIQI_TEST_CHECK(service_a->Deactivate() == S_OK);
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 1 && !weak_dict.expired());
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(dict_path) == cached_dict);

    // service_b continues querying candidates from the shared dictionary after service_a deactivates.
    std::array<BYTE, 256> clean_kb{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_kb.data()));
    auto context_b = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_b->OnKeyDown(context_b.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_b->OnKeyDown(context_b.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_b->range_->text_ == L"共享词一");

    // Reactivating a service with a non-BuiltinOnly primary dictionary reuses the existing in-process cache entry.
    config.primary_dict = PrimaryDict::Jidian;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    auto service_c = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_c = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service_c->Activate(manager_c.Get(), 3) == S_OK);
    QIQI_TEST_CHECK(service_c->Deactivate() == S_OK);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(dict_path) == cached_dict);
    QIQI_TEST_CHECK(service_c->Activate(manager_c.Get(), 3) == S_OK);
    auto context_c = MakeComObject<MockTfContext>();
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_c->OnKeyDown(context_c.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_c->OnKeyDown(context_c.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_c->range_->text_ == L"共享词一");

    // BuiltinOnly service with user_dict_path also reloads external user dictionary upon reactivation.
    config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service_a->Activate(manager_a.Get(), 1) == S_OK);
    auto context_a = MakeComObject<MockTfContext>();
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_a->OnKeyDown(context_a.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_a->OnKeyDown(context_a.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_a->range_->text_ == L"共享词一");
    QIQI_TEST_CHECK(service_a->Deactivate() == S_OK);

    // Module-detach cleanup (or explicit ClearProcessCache) releases the process cache; once all services release
    // their external dictionaries, the underlying WubiMappedDict object is destroyed.
    cached_dict.reset();
    QIQI_TEST_CHECK(!weak_dict.expired());
    QIQI_TEST_CHECK(service_b->Deactivate() == S_OK);
    QIQI_TEST_CHECK(service_c->Deactivate() == S_OK);
    service_a.Reset();
    service_b.Reset();
    service_c.Reset();
    QIQI_TEST_CHECK(!weak_dict.expired() && WubiMappedDict::ProcessCacheSize() > 0);
    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0 && weak_dict.expired());
}

void TestDictionarySharingAndLegacyRegistryIgnored() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const std::wstring subkey = L"Software\\QiQiIME_DictFallback_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    QIQI_TEST_CHECK(!std::filesystem::exists(GetDefaultUserDictPath()));
    const std::map<uint32_t, std::vector<std::wstring>> entries{
        {PackWubiCode(L"qwer"), {L"镜像回退词"}}
    };
    std::vector<uint8_t> bytes;
    WubiMappedDict::CompileEntriesToBinaryBuffer(entries, bytes);
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, key.Put(), nullptr) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::RegSetValueExW(key.Get(), L"UserDictQdict", 0, REG_BINARY,
            bytes.data(), gsl::narrow_cast<DWORD>(bytes.size())) == ERROR_SUCCESS);
    }
    for (const bool configured_path : {false, true}) {
        QiQiConfig config;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.enable_jidian_extra = true;
        config.auto_commit_4code = false;
        config.user_dict_path = configured_path ?
            (GetDefaultUserDictPath().parent_path() / L"missing-explicit.txt").wstring() : L"";
        QIQI_TEST_CHECK(SaveConfig(config));
        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        for (const UINT key : std::array<UINT, 4>{'Q', 'W', 'E', 'R'}) {
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        }
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(context->range_->text_ != L"镜像回退词");
        std::filesystem::path directory;
        QIQI_TEST_CHECK(LoadUserDictSyncDirectory(directory) == S_OK && directory.empty());
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    }

    // Verify QiQiTextService custom primary dictionary loading and hot-reload retention on invalid path
    {
        const auto custom_path = std::filesystem::temp_directory_path() /
            (L"qiqi_tsf_custom_primary_" + std::to_wstring(::GetCurrentProcessId()) + L".txt");
        auto custom_qdict = custom_path;
        custom_qdict.replace_extension(L".qdict");
        const auto cleanup_custom = gsl::finally([&]() noexcept {
            WubiMappedDict::ClearProcessCache();
            std::error_code ec;
            std::filesystem::remove(custom_path, ec);
            std::filesystem::remove(custom_qdict, ec);
        });
        {
            std::ofstream out(custom_path, std::ios::binary | std::ios::trunc);
            out << "xcvb 服务自定义词\n";
        }
        QiQiConfig config;
        config.primary_dict = PrimaryDict::Custom;
        config.primary_dict_path = custom_path.wstring();
        config.auto_commit_4code = false;
        config.user_dict_path = (GetDefaultUserDictPath().parent_path() / L"missing-explicit.txt").wstring();
        QIQI_TEST_CHECK(SaveConfig(config));

        auto service = MakeComObject<QiQiTextService>(g_dll_instance);
        auto manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        auto context = MakeComObject<MockTfContext>();
        BOOL eaten = FALSE;
        for (const UINT key : std::array<UINT, 4>{'X', 'C', 'V', 'B'}) {
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        }
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(context->range_->text_ == L"服务自定义词");

        // Hot-reload with missing custom path retains previously loaded custom dictionary
        config.primary_dict_path = (custom_path.parent_path() / L"missing_custom_primary.txt").wstring();
        QIQI_TEST_CHECK(SaveConfig(config));
        QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
        context->range_->text_.clear();
        for (const UINT key : std::array<UINT, 4>{'X', 'C', 'V', 'B'}) {
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), key, 0, &eaten) == S_OK && eaten);
        }
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(context->range_->text_ == L"服务自定义词");
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    }
}

} // namespace qiqi::test::service_cases
