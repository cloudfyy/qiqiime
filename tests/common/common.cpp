#include "common.h"
#include "common/com_object.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/sound.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace qiqi::test {

namespace {

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 8, 4});
    static_assert(version::SemanticVersion{1, 0, 0} > version::kCurrentVersion);
    QIQI_TEST_CHECK(!version::kVersion.empty());
    QIQI_TEST_CHECK(!version::kVersionW.empty());
    QIQI_TEST_CHECK(!version::kFullVersion.empty());
    QIQI_TEST_CHECK(!version::kFullVersionW.empty());
    QIQI_TEST_CHECK(!version::kGitCommit.empty());
    QIQI_TEST_CHECK(!version::kGitCommitW.empty());
    QIQI_TEST_CHECK(version::kArch == (sizeof(void*) == 8 ? "x64" : "x86"));
    QIQI_TEST_CHECK(version::kArchW == (sizeof(void*) == 8 ? L"x64" : L"x86"));
    QIQI_TEST_CHECK(version::kFullVersion.starts_with(version::kVersion));
}

void TestEtwLogger() {
    // 1. Test lazy initialization & Windows ETW TraceLogging provider registration before InitLogger() is ever called
    QIQI_TEST_CHECK(log::IsEtwProviderRegistered());
    QIQI_LOG_DEBUG("Lazy default logger test");

    // 2. Test WideToUtf8, Utf8ToWide, and GetModuleFilePath
    QIQI_TEST_CHECK(WideToUtf8(L"").empty());
    QIQI_TEST_CHECK(WideToUtf8(L"五笔ABC") == "五笔ABC");
    QIQI_TEST_CHECK(Utf8ToWide("").empty());
    QIQI_TEST_CHECK(Utf8ToWide("五笔ABC") == L"五笔ABC");
    QIQI_TEST_CHECK(!GetModuleFilePath(nullptr).empty());
    QIQI_TEST_CHECK(GetModuleFilePath(reinterpret_cast<void*>(0x12345678)).empty());

    // 3. Test Pure Lock-Free ETW Logger (InitLogger, SetLevel, GetLevel, IsLogSinkActive, WriteLogMessage)
    log::InitLogger(log::Level::info);
    QIQI_TEST_CHECK(log::GetLevel() == log::Level::info);
    (void)log::IsLogSinkActive();
    log::SetLevel(log::Level::trace);
    QIQI_TEST_CHECK(log::GetLevel() == log::Level::trace);

    QIQI_LOG_TRACE("Trace message");
    QIQI_LOG_DEBUG("Debug message");
    QIQI_LOG_INFO("Unit test log entry: {}", WideToUtf8(L"五笔日志测试"));
    QIQI_LOG_WARN("Warn message");
    QIQI_LOG_ERROR("Error message");
    QIQI_LOG_CRITICAL("Critical macro message");
    log::WriteLogMessage(log::Level::trace, "Direct trace");
    log::WriteLogMessage(log::Level::debug, "Direct debug");
    log::WriteLogMessage(log::Level::info, "Direct info");
    log::WriteLogMessage(log::Level::warn, "Direct warn");
    log::WriteLogMessage(log::Level::err, "Direct err");
    log::WriteLogMessage(log::Level::critical, "Critical message");

    log::SetLevel(log::Level::off);
    QIQI_LOG_INFO("Suppressed when level is off");
    log::WriteLogMessage(log::Level::off, "Suppressed off write");
    log::SetLevel(log::Level::info);

    // 4. Test GetAppDataDirectory fallback when LOCALAPPDATA is unset, plus Shutdown & lazy re-Init
    {
        wchar_t* orig_localappdata = nullptr;
        size_t orig_len = 0;
        _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
        const auto restore_env = gsl::finally([orig_localappdata]() noexcept {
            if (orig_localappdata) {
                _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
                free(orig_localappdata);
            }
        });
        _wputenv_s(L"LOCALAPPDATA", L"");
        QIQI_TEST_CHECK(!GetAppDataDirectory().empty());
    }
    log::Shutdown();
    // WriteLogMessage after Shutdown exercises lazy EnsureRegistered inside WriteLogMessage
    log::WriteLogMessage(log::Level::info, "Lazy re-init after Shutdown");
    log::Shutdown();
    QIQI_TEST_CHECK(log::IsEtwProviderRegistered());
    log::InitLogger();
}

void TestIniConfiguration() {
    // 1. Test ParseConfigFromUtf8 with BOM, comments, inline comments, and all sections
    constexpr std::string_view kIniContent =
        "\xEF\xBB\xBF# Header comment\n"
        "; Semicolon comment\n"
        "[dictionary]\n"
        "primary = haifeng # inline comment\n"
        "enable_jidian_extra = false\n"
        "user_dict = custom_words.txt\n"
        "\n"
        "[engine]\n"
        "page_size = 7\n"
        "auto_commit_4code = 0\n"
        "push_commit_5th = no\n"
        "default_chinese_mode = off\n"
        "default_full_width = true\n"
        "default_chinese_punct = false\n"
        "shift_toggle_mode = false\n"
        "semicolon_quote_quick_select = false\n"
        "show_code_hint = false\n"
        "enable_z_temp_english = false\n"
        "dynamic_phrase_freq = true\n"
        "clear_on_empty_4code = true\n"
        "\n"
        "[log]\n"
        "level = debug\n";

    const QiQiConfig cfg = ParseConfigFromUtf8(kIniContent);
    QIQI_TEST_CHECK(cfg.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(!cfg.enable_jidian_extra);
    QIQI_TEST_CHECK(cfg.user_dict_path == L"custom_words.txt");
    QIQI_TEST_CHECK(cfg.page_size == 7);
    QIQI_TEST_CHECK(!cfg.auto_commit_4code);
    QIQI_TEST_CHECK(!cfg.push_commit_5th);
    QIQI_TEST_CHECK(!cfg.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(!cfg.show_code_hint);
    QIQI_TEST_CHECK(!cfg.enable_z_temp_english);
    QIQI_TEST_CHECK(cfg.dynamic_phrase_freq);
    QIQI_TEST_CHECK(!cfg.default_chinese_mode);
    QIQI_TEST_CHECK(cfg.default_full_width);
    QIQI_TEST_CHECK(!cfg.default_chinese_punct);
    QIQI_TEST_CHECK(!cfg.shift_toggle_mode);
    QIQI_TEST_CHECK(cfg.clear_on_empty_4code);
    QIQI_TEST_CHECK(cfg.log_level == log::Level::debug);

    // Test SaveConfigFile & SerializeConfigToUtf8 round-trip
    const auto roundtrip_path = std::filesystem::temp_directory_path() / L"qiqiime_roundtrip.ini";
    std::error_code rt_ec;
    QIQI_TEST_CHECK(SaveConfigFile(roundtrip_path, cfg));
    QiQiConfig roundtrip_cfg{};
    QIQI_TEST_CHECK(LoadConfigFile(roundtrip_path, roundtrip_cfg));
    QIQI_TEST_CHECK(roundtrip_cfg.primary_dict == PrimaryDict::Haifeng);
    QIQI_TEST_CHECK(roundtrip_cfg.default_full_width);
    QIQI_TEST_CHECK(!roundtrip_cfg.default_chinese_punct);
    QIQI_TEST_CHECK(!roundtrip_cfg.semicolon_quote_quick_select);
    QIQI_TEST_CHECK(roundtrip_cfg.clear_on_empty_4code);
    std::filesystem::remove(roundtrip_path, rt_ec);

    // 2. Test alternative enum/boolean values, uppercase keys, malformed lines, and all log levels
    const QiQiConfig cfg2 = ParseConfigFromUtf8(
        "[DICTIONARY]\n"
        "PRIMARY = builtin\n"
        "ENABLE_JIDIAN_EXTRA = yes\n"
        "malformed_line_without_equals\n"
        "[ENGINE]\n"
        "AUTO_COMMIT_4CODE = on\n"
        "PUSH_COMMIT_5TH = invalid_bool\n"
        "BEEP_ON_EMPTY_4CODE = false\n"
        "BEEP_ON_DUP_4CODE = true\n"
        "CLEAR_ON_EMPTY_4CODE = true\n"
        "ENABLE_KEY_SOUND = true\n"
        "UI_LANGUAGE = en-US\n"
        "[LOG]\n"
        "LEVEL = trace\n"
        "LOG_KEYSTROKES = true\n");
    QIQI_TEST_CHECK(cfg2.primary_dict == PrimaryDict::BuiltinOnly);
    QIQI_TEST_CHECK(cfg2.enable_jidian_extra);
    QIQI_TEST_CHECK(cfg2.auto_commit_4code);
    QIQI_TEST_CHECK(cfg2.push_commit_5th);
    QIQI_TEST_CHECK(!cfg2.beep_on_empty_4code);
    QIQI_TEST_CHECK(cfg2.beep_on_dup_4code);
    QIQI_TEST_CHECK(cfg2.clear_on_empty_4code);
    QIQI_TEST_CHECK(cfg2.enable_key_sound);
    QIQI_TEST_CHECK(cfg2.ui_language == UiLanguage::EnUs);
    QIQI_TEST_CHECK(cfg2.log_level == log::Level::trace);
    QIQI_TEST_CHECK(cfg2.log_keystrokes);

    const QiQiConfig cfg_zh = ParseConfigFromUtf8("[engine]\nlanguage = zh-CN\n");
    QIQI_TEST_CHECK(cfg_zh.ui_language == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(!SerializeConfigToUtf8(cfg_zh).empty());
    const QiQiConfig cfg_auto = ParseConfigFromUtf8("[engine]\nui_language = unknown_lang\n");
    QIQI_TEST_CHECK(cfg_auto.ui_language == UiLanguage::Auto);

    for (const auto& [lvl_str, lvl_enum] : {
             std::pair{"info", log::Level::info},
             std::pair{"warn", log::Level::warn},
             std::pair{"error", log::Level::err},
             std::pair{"critical", log::Level::critical},
             std::pair{"off", log::Level::off},
             std::pair{"unknown_lvl", log::Level::info}}) {
        QiQiConfig lcfg = ParseConfigFromUtf8(std::string("[log]\nlevel = ") + lvl_str + "\n[dictionary]\nprimary = unknown_dict\n");
        QIQI_TEST_CHECK(lcfg.log_level == lvl_enum);
        QIQI_TEST_CHECK(lcfg.primary_dict == PrimaryDict::Jidian);
        const std::string serialized = SerializeConfigToUtf8(lcfg);
        QIQI_TEST_CHECK(!serialized.empty());
    }
    QIQI_TEST_CHECK(!SerializeConfigToUtf8(cfg2).empty());

    // SaveConfigFile failure when target is a directory
    QIQI_TEST_CHECK(!SaveConfigFile(std::filesystem::temp_directory_path(), cfg));

    // Test ResolveBundledDataFile fallback across <dll_dir>/<name>, <dll_dir>/../data/<name>, and <dll_dir>/../<name>
    {
        const auto base_dir = std::filesystem::temp_directory_path() / L"QiQiIME_BundledLayoutTest";
        const auto x64_dir = base_dir / L"x64";
        const auto shared_data_dir = base_dir / L"data";
        std::error_code layout_ec;
        std::filesystem::remove_all(base_dir, layout_ec);
        std::filesystem::create_directories(x64_dir, layout_ec);
        std::filesystem::create_directories(shared_data_dir, layout_ec);

        QIQI_TEST_CHECK(ResolveBundledDataFile({}, L"config.ini") == std::filesystem::path(L"config.ini"));
        // When file exists in ../data/, ResolveBundledDataFile(x64_dir, ...) resolves to ../data/
        const auto shared_dict = shared_data_dir / L"wubi86_jidian.txt";
        SaveConfigFile(shared_dict, cfg);
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == shared_dict);

        // When file exists in ../, ResolveBundledDataFile(x64_dir, ...) resolves to ../
        const auto parent_cfg = base_dir / L"config.ini";
        SaveConfigFile(parent_cfg, cfg);
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"config.ini") == parent_cfg);

        // When file exists directly in dll_dir, direct path takes precedence
        const auto local_dict = x64_dir / L"wubi86_jidian.txt";
        SaveConfigFile(local_dict, cfg);
        QIQI_TEST_CHECK(ResolveBundledDataFile(x64_dir, L"wubi86_jidian.txt") == local_dict);

        std::filesystem::remove_all(base_dir, layout_ec);
    }

    // 3. Test EnsureDefaultConfigFile and LoadConfigFile
    const auto temp_cfg_path = std::filesystem::temp_directory_path() / L"qiqiime_test_config.ini";
    std::error_code ec;
    std::filesystem::remove(temp_cfg_path, ec);

    QIQI_TEST_CHECK(!LoadConfigFile("non_existent_config_99999.ini", const_cast<QiQiConfig&>(cfg)));
    QIQI_TEST_CHECK(EnsureDefaultConfigFile(temp_cfg_path));
    // Calling EnsureDefaultConfigFile again when file exists returns true without overwriting
    QIQI_TEST_CHECK(EnsureDefaultConfigFile(temp_cfg_path));

    QiQiConfig loaded_default{};
    QIQI_TEST_CHECK(LoadConfigFile(temp_cfg_path, loaded_default));
    QIQI_TEST_CHECK(loaded_default.primary_dict == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(loaded_default.enable_jidian_extra);
    QIQI_TEST_CHECK(loaded_default.page_size == 5);
    QIQI_TEST_CHECK(loaded_default.auto_commit_4code);
    QIQI_TEST_CHECK(loaded_default.push_commit_5th);
    QIQI_TEST_CHECK(!loaded_default.clear_on_empty_4code);
    QIQI_TEST_CHECK(loaded_default.default_chinese_mode);
    QIQI_TEST_CHECK(loaded_default.shift_toggle_mode);
    QIQI_TEST_CHECK(loaded_default.log_level == log::Level::info);

    std::filesystem::remove(temp_cfg_path, ec);

    // 4. Test LoadConfig discovery and HKCU registry config/user-dict mirroring (using isolated test subkey)
    constexpr const wchar_t* kTestRegSubKey = L"Software\\QiQiIME_UnitTest";
    ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
    SetConfigRegistrySubKeyOverrideForTest(kTestRegSubKey);

    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    const auto temp_appdata = std::filesystem::temp_directory_path() / L"QiQiIME_TestAppData";
    std::filesystem::remove_all(temp_appdata, ec);
    const auto restore_appdata = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTestRegSubKey);
        if (orig_localappdata) {
            _wputenv_s(L"LOCALAPPDATA", orig_localappdata);
            free(orig_localappdata);
        }
        std::error_code clean_ec;
        std::filesystem::remove_all(temp_appdata, clean_ec);
    });
    _wputenv_s(L"LOCALAPPDATA", temp_appdata.wstring().c_str());

    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == kTestRegSubKey);
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == 0);
    QiQiConfig reg_out{};
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out));
    QIQI_TEST_CHECK(!SaveConfigToRegistry(cfg, L""));
    QIQI_TEST_CHECK(!LoadConfigFromRegistry(reg_out, nullptr, L""));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence(L"") == 0);

    const QiQiConfig discovered = LoadConfig(nullptr);
    QIQI_TEST_CHECK(discovered.page_size >= 1 && discovered.page_size <= 9);
    QIQI_TEST_CHECK(std::filesystem::exists(GetDefaultUserConfigPath()));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() >= 1);

    // Verify LoadConfig falls back to HKCU registry mirror when %LOCALAPPDATA%\QiQiIME\config.ini is absent (UWP scenario)
    QiQiConfig custom_reg_cfg{};
    custom_reg_cfg.page_size = 8;
    custom_reg_cfg.primary_dict = PrimaryDict::Haifeng;
    custom_reg_cfg.ui_language = UiLanguage::EnUs;
    QIQI_TEST_CHECK(SaveConfigToRegistry(custom_reg_cfg));
    uint64_t seq_after_save = 0;
    QIQI_TEST_CHECK(LoadConfigFromRegistry(reg_out, &seq_after_save));
    QIQI_TEST_CHECK(seq_after_save >= 2);
    QIQI_TEST_CHECK(reg_out.page_size == 8 &&
                    reg_out.primary_dict == PrimaryDict::Haifeng &&
                    reg_out.ui_language == UiLanguage::EnUs);

    std::filesystem::remove_all(temp_appdata, ec);
    const QiQiConfig uwp_loaded = LoadConfig(nullptr);
    QIQI_TEST_CHECK(uwp_loaded.page_size == 8 && uwp_loaded.primary_dict == PrimaryDict::Haifeng);

    // Verify user_dict binary registry mirror round-trip and edge cases
    std::vector<uint8_t> loaded_dict_bytes;
    QIQI_TEST_CHECK(!LoadUserDictBinaryFromRegistry(loaded_dict_bytes));
    QIQI_TEST_CHECK(!SaveUserDictBinaryToRegistry({}));
    QIQI_TEST_CHECK(!SaveUserDictBinaryToRegistry(std::array<uint8_t, 4>{1, 2, 3, 4}, L""));
    QIQI_TEST_CHECK(!LoadUserDictBinaryFromRegistry(loaded_dict_bytes, L""));
    const std::array<uint8_t, 8> sample_qdict{0x51, 0x44, 0x43, 0x54, 1, 2, 3, 4};
    QIQI_TEST_CHECK(SaveUserDictBinaryToRegistry(sample_qdict));
    QIQI_TEST_CHECK(LoadUserDictBinaryFromRegistry(loaded_dict_bytes));
    QIQI_TEST_CHECK(loaded_dict_bytes.size() == sample_qdict.size());
}

void TestScopedHandleTemplate() {
    ScopedKernelHandle h1(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(h1.IsValid());
    QIQI_TEST_CHECK(static_cast<bool>(h1));

    // Move construction
    ScopedKernelHandle h2(std::move(h1));
    QIQI_TEST_CHECK(!h1.IsValid());
    QIQI_TEST_CHECK(h2.IsValid());

    // Move assignment
    ScopedKernelHandle h3;
    h3 = std::move(h2);
    QIQI_TEST_CHECK(!h2.IsValid());
    QIQI_TEST_CHECK(h3.IsValid());

    h3.Reset();
    QIQI_TEST_CHECK(!h3.IsValid());

    // Test ScopedGlobalLock<wchar_t>, ScopedClipboard, MakeInt4Variant, and CopyToBstr
    const ScopedGlobalLock<wchar_t> null_lock(nullptr);
    QIQI_TEST_CHECK(!null_lock && null_lock.Get() == nullptr);

    const HGLOBAL hmem = ::GlobalAlloc(GMEM_MOVEABLE, 16 * sizeof(wchar_t));
    QIQI_TEST_CHECK(hmem != nullptr);
    if (auto* raw_ptr = static_cast<wchar_t*>(::GlobalLock(hmem))) {
        wcscpy_s(raw_ptr, 16, L"奇奇五笔");
        ::GlobalUnlock(hmem);
    }
    {
        const ScopedGlobalLock<wchar_t> lock(hmem);
        QIQI_TEST_CHECK(lock && std::wstring(lock.Get()) == L"奇奇五笔");
    }
    ::GlobalFree(hmem);

    {
        const ScopedClipboard clip{};
        (void)clip.IsOpen();
        (void)static_cast<bool>(clip);
    }

    const VARIANT v42 = MakeInt4Variant(42);
    QIQI_TEST_CHECK(v42.vt == VT_I4 && v42.lVal == 42);

    QIQI_TEST_CHECK(CopyToBstr(L"test", nullptr) == E_INVALIDARG);
    ScopedBstr bstr_out;
    QIQI_TEST_CHECK(SUCCEEDED(CopyToBstr(L"奇奇", bstr_out.Put())) &&
                    bstr_out.IsValid() &&
                    std::wstring(bstr_out.Get()) == L"奇奇");

    // Test CreateUiFont and WriteFileAtomically (including directory error branch)
    {
        const ScopedGdiHandle<HFONT> ui_font = CreateUiFont(16, FW_MEDIUM);
        QIQI_TEST_CHECK(ui_font.IsValid());

        const auto atomic_path = std::filesystem::temp_directory_path() / L"qiqiime_atomic_test.bin";
        const std::array<uint8_t, 4> payload{1, 2, 3, 4};
        QIQI_TEST_CHECK(WriteFileAtomically(atomic_path, payload));
        std::string read_back;
        QIQI_TEST_CHECK(ReadFileToUtf8String(atomic_path, read_back) && read_back.size() == 4);
        std::error_code rm_ec;
        std::filesystem::remove(atomic_path, rm_ec);
        QIQI_TEST_CHECK(!WriteFileAtomically(std::filesystem::temp_directory_path(), payload));
    }

    // Test ScopedSaveDC (both null HDC and valid memory HDC state restoration)
    {
        const ScopedSaveDC null_save(nullptr);
        QIQI_TEST_CHECK(!null_save);
    }
    {
        const ScopedMemoryDC mem_dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(mem_dc.IsValid());
        ::SetTextColor(mem_dc.Get(), RGB(12, 34, 56));
        ::SetBkMode(mem_dc.Get(), OPAQUE);
        {
            const ScopedSaveDC save_dc(mem_dc.Get());
            QIQI_TEST_CHECK(static_cast<bool>(save_dc));
            ::SetTextColor(mem_dc.Get(), RGB(200, 100, 50));
            ::SetBkMode(mem_dc.Get(), TRANSPARENT);
            QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(200, 100, 50));
            QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == TRANSPARENT);
        }
        QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(12, 34, 56));
        QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == OPAQUE);
    }

        // Test BindOrGetWindowUserData, ClientPointFromLParam, and PaintWindowClient on a temporary window
    {
        const ScopedWindow test_wnd(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 100, 200, 50, 40, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(test_wnd.IsValid());
        int payload = 12345;
        CREATESTRUCTW cs{};
        cs.lpCreateParams = &payload;
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_NCCREATE, 0) == nullptr);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(
                            test_wnd.Get(), WM_NCCREATE, reinterpret_cast<LPARAM>(&cs)) == &payload);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_PAINT, 0) == &payload);

        const POINT client_pt = ClientPointFromLParam(test_wnd.Get(), MAKELPARAM(15, 25), false);
        QIQI_TEST_CHECK(client_pt.x == 15 && client_pt.y == 25);
        const POINT nc_null_pt = ClientPointFromLParam(nullptr, MAKELPARAM(115, 225), true);
        QIQI_TEST_CHECK(nc_null_pt.x == 115 && nc_null_pt.y == 225);
        POINT screen_pt{10, 20};
        ::ClientToScreen(test_wnd.Get(), &screen_pt);
        const POINT nc_converted = ClientPointFromLParam(
            test_wnd.Get(), MAKELPARAM(screen_pt.x, screen_pt.y), true);
        QIQI_TEST_CHECK(nc_converted.x == 10 && nc_converted.y == 20);

        bool painted = false;
        QIQI_TEST_CHECK(PaintWindowClient(test_wnd.Get(), [&painted](HDC hdc, const RECT& rc) {
            painted = (hdc != nullptr && rc.right == 50 && rc.bottom == 40);
        }) == 0);
        QIQI_TEST_CHECK(painted);
    }
}

void TestI18nLocalization() {
    // 1. Direct language resolution
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::ZhCn) == UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ResolveUiLanguage(UiLanguage::EnUs) == UiLanguage::EnUs);
    const UiLanguage auto_resolved = ResolveUiLanguage(UiLanguage::Auto);
    QIQI_TEST_CHECK(auto_resolved == UiLanguage::ZhCn || auto_resolved == UiLanguage::EnUs);

    // 2. Bilingual string tables verification
    const UiStrings& zh = GetUiStrings(UiLanguage::ZhCn);
    const UiStrings& en = GetUiStrings(UiLanguage::EnUs);
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_header_prefix) == L"五笔 [");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_header_prefix) == L"Wubi [");
    QIQI_TEST_CHECK(std::wstring_view(zh.candidate_empty) == L"(无候选)");
    QIQI_TEST_CHECK(std::wstring_view(en.candidate_empty) == L"(No candidates)");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_logo) == L"五笔");
    QIQI_TEST_CHECK(std::wstring_view(en.status_logo) == L"Wubi");
    QIQI_TEST_CHECK(std::wstring_view(zh.status_mode_cn) == L"中");
    QIQI_TEST_CHECK(std::wstring_view(en.status_mode_cn) == L"CN");
    QIQI_TEST_CHECK(std::wstring_view(zh.dlg_btn_ok) == L"确定");
    QIQI_TEST_CHECK(std::wstring_view(en.dlg_btn_ok) == L"OK");

    // 3. Config INI aliases & round-trip serialization for all UiLanguage values
    for (const auto& [alias, expected] : {
             std::pair{"auto", UiLanguage::Auto},
             std::pair{"system", UiLanguage::Auto},
             std::pair{"default", UiLanguage::Auto},
             std::pair{"zh-CN", UiLanguage::ZhCn},
             std::pair{"zh_cn", UiLanguage::ZhCn},
             std::pair{"zh", UiLanguage::ZhCn},
             std::pair{"cn", UiLanguage::ZhCn},
             std::pair{"chinese", UiLanguage::ZhCn},
             std::pair{"en-US", UiLanguage::EnUs},
             std::pair{"en_us", UiLanguage::EnUs},
             std::pair{"en", UiLanguage::EnUs},
             std::pair{"english", UiLanguage::EnUs}}) {
        const QiQiConfig parsed = ParseConfigFromUtf8(std::string("[engine]\nui_language = ") + alias + "\n");
        QIQI_TEST_CHECK(parsed.ui_language == expected);
        const std::string serialized = SerializeConfigToUtf8(parsed);
        const QiQiConfig roundtrip = ParseConfigFromUtf8(serialized);
        QIQI_TEST_CHECK(roundtrip.ui_language == expected);
    }

    // 4. Test sound synthesizer and PlayImeSound (both live WAV synthesis and muted test mode)
    ResetLastPlayedSoundForTest();
    PlayImeSound(SoundEffectKind::None);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::None);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    PlayImeSound(SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    PlayImeSound(SoundEffectKind::DupCodeBeep);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);

    SetMuteAudioOutputForTest(true);
    PlayImeSound(SoundEffectKind::KeyClick);
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
    SetMuteAudioOutputForTest(false);
}

} // namespace

void RunCommonTests() {
    TestEtwLogger();
    TestVersionMechanism();
    TestIniConfiguration();
    TestScopedHandleTemplate();
    TestI18nLocalization();
}

} // namespace qiqi::test
