// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/dictcodec.h"
#include "tools/settings/internal.h"
#include "tools/settings/transfer.h"

#include <filesystem>

namespace qiqi::test {
namespace {

using namespace settings_detail;

struct PreviewExpectation {
    HWND owner;
    std::wstring body;
    bool accept;
    bool seen = false;
    bool change_external = false;
};
thread_local PreviewExpectation* preview_expectation = nullptr;
thread_local const wchar_t* transfer_error_text = nullptr;
thread_local bool transfer_error_seen = false;

template <typename Action>
void WithTransferError(const wchar_t* expected, Action action) {
    const wchar_t* previous = std::exchange(transfer_error_text, expected);
    const auto restore = gsl::finally([previous]() noexcept { transfer_error_text = previous; });
    transfer_error_seen = false;
    const HHOOK hook = ::SetWindowsHookExW(WH_CBT, [](int code, WPARAM wparam, LPARAM lparam) noexcept -> LRESULT {
        if (code == HCBT_ACTIVATE) {
            const HWND box = reinterpret_cast<HWND>(wparam);
            std::array<wchar_t, 32> name{};
            ::GetClassNameW(box, name.data(), gsl::narrow_cast<int>(name.size()));
            if (std::wstring_view(name.data()) == L"#32770") {
                const auto actual = ControlText(::GetDlgItem(box, 0xFFFF));
                if (actual != transfer_error_text)
                    std::wcerr << L"Expected transfer error:\n" << transfer_error_text <<
                        L"\nActual transfer error:\n" << actual << std::endl;
                QIQI_TEST_CHECK(actual == transfer_error_text);
                transfer_error_seen = true;
                ::SendMessageW(box, WM_COMMAND, IDOK, 0);
                return 1;
            }
        }
        return ::CallNextHookEx(nullptr, code, wparam, lparam);
    }, nullptr, ::GetCurrentThreadId());
    QIQI_TEST_CHECK(hook != nullptr);
    const auto unhook = gsl::finally([hook]() noexcept { QIQI_TEST_CHECK(::UnhookWindowsHookEx(hook)); });
    action();
    QIQI_TEST_CHECK(transfer_error_seen);
}

template <typename Action>
void WithPreview(HWND owner, const QiQiConfig& before, const QiQiConfig& after,
    UiLanguage language, bool accept, Action action, bool change_external = false) {
    PreviewExpectation expectation{owner, FormatSettingsConfigChanges(before, after, language), accept,
        false, change_external};
    auto* previous = std::exchange(preview_expectation, &expectation);
    const auto restore = gsl::finally([previous]() noexcept { preview_expectation = previous; });
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND window = FindThreadWindowByClass(kSettingsImportPreviewWndClass);
        if (!window) return;
        ::KillTimer(nullptr, id);
        const auto* expected = preview_expectation;
        QIQI_TEST_CHECK(expected && ::GetWindow(window, GW_OWNER) == expected->owner);
        QIQI_TEST_CHECK(!::IsWindowEnabled(expected->owner));
        const HWND body = ::GetDlgItem(window, kIdConfigChanges);
        QIQI_TEST_CHECK(ControlText(body) == expected->body);
        const auto style = ::GetWindowLongPtrW(body, GWL_STYLE);
        QIQI_TEST_CHECK((style & ES_READONLY) != 0 && (style & WS_VSCROLL) != 0);
        QIQI_TEST_CHECK(::GetDlgItem(window, IDOK) && ::GetDlgItem(window, IDCANCEL));
        ::SendMessageW(window, WM_THEMECHANGED, 0, 0);
        RECT rect{};
        QIQI_TEST_CHECK(::GetWindowRect(window, &rect));
        const auto metrics = QueryActiveMonitor(expected->owner);
        ::SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(metrics.dpi, metrics.dpi),
            reinterpret_cast<LPARAM>(&rect));
        RECT client{};
        QIQI_TEST_CHECK(::GetClientRect(window, &client));
        for (const int control : {kIdConfigChanges, IDOK, IDCANCEL}) {
            RECT bounds{};
            QIQI_TEST_CHECK(::GetWindowRect(::GetDlgItem(window, control), &bounds));
            ::MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            QIQI_TEST_CHECK(bounds.left >= 0 && bounds.top >= 0 &&
                bounds.right <= client.right && bounds.bottom <= client.bottom);
        }
        if (expected->change_external) {
            QiQiConfig external;
            QIQI_TEST_CHECK(LoadConfigFromRegistry(external));
            external.beep_on_dup_4code = true;
            external.default_full_width = true;
            QIQI_TEST_CHECK(SaveConfig(external));
            ::SendMessageW(expected->owner, kSettingsExternalConfigChangedMessage, 0, 0);
        }
        QIQI_TEST_CHECK(ImportSettingsConfiguration(expected->owner, L"unused.json") == E_ABORT);
        preview_expectation->seen = true;
        if (expected->accept) ::SendMessageW(window, WM_COMMAND, IDOK, 0);
        else ::SendMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto stop_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    action();
    QIQI_TEST_CHECK(expectation.seen && ::IsWindowEnabled(owner));
}

#pragma warning(suppress : 26461) // Win32's callback type requires a mutable descriptor.
BOOL WINAPI CancelFileDialog(OPENFILENAMEW* dialog) {
    QIQI_TEST_CHECK(dialog && dialog->lpstrDefExt && std::wstring_view(dialog->lpstrDefExt) == L"json");
    QIQI_TEST_CHECK((dialog->Flags & OFN_NOCHANGEDIR) != 0);
    return FALSE;
}
DWORD WINAPI NoDialogError() noexcept { return ERROR_SUCCESS; }

void TestTransferFlow() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"qiqi-config-transfer-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
            std::to_wstring(::GetTickCount64()));
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    const auto input = directory / L"input.json";
    const auto output = directory / L"output.json";
    const auto large = directory / L"large.json";
    const std::wstring subkey = L"Software\\QiQiIME_ConfigTransferTest_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        const LSTATUS error = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND);
        for (const auto& path : {input, output, large}) {
            std::error_code removed;
            std::filesystem::remove(path, removed);
            QIQI_TEST_CHECK(!removed);
        }
        QIQI_TEST_CHECK(std::filesystem::remove(directory));
    });
    constexpr std::string_view partial =
        R"({"format":"qiqiime-config","version":1,"settings":{"page_size":7,"enable_code_completion":false,"default_chinese_mode":false}})";
    constexpr std::string_view language_only =
        R"({"format":"qiqiime-config","version":1,"settings":{"ui_language":"en-US"}})";
    QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(language_only)));
    for (const bool import : {true, false}) {
        QiQiConfig config;
        config.ui_language = UiLanguage::ZhCn;
        QIQI_TEST_CHECK(SaveConfig(config));
        const auto sequence = QueryRegistryConfigSequence();
        const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, false));
        QIQI_TEST_CHECK(window);
        const auto expected = import ? UiLanguage::EnUs : QiQiConfig{}.ui_language;
        if (import) {
            auto draft = config;
            draft.ui_language = expected;
            WithPreview(window.Get(), config, draft, config.ui_language, true, [&]() {
                QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), input) == S_OK);
            });
        } else {
            ::SendMessageW(window.Get(), WM_COMMAND, kIdBtnRestoreDefaults, 0);
        }
        for (const auto message : {WM_THEMECHANGED, WM_SETTINGCHANGE}) {
            ::SendMessageW(window.Get(), message, 0, 0);
            QiQiConfig draft = config;
            ReadSettingsFromWindowControls(window.Get(), draft);
            QIQI_TEST_CHECK(draft.ui_language == expected && config.ui_language == UiLanguage::ZhCn);
        }
        ::SendMessageW(window.Get(), WM_COMMAND, IDCANCEL, 0);
        QIQI_TEST_CHECK(config.ui_language == UiLanguage::ZhCn);
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence && LoadConfig().ui_language == UiLanguage::ZhCn);
    }
    QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(partial)));
    for (const UiLanguage language : {UiLanguage::ZhCn, UiLanguage::EnUs}) {
        for (const bool confirm : {false, true}) {
            for (const bool save : {false, true}) {
                QiQiConfig config;
                config.ui_language = language;
                config.user_dict_path = L"preserved-user.txt";
                config.primary_dict_path = L"preserved-primary.txt";
                QIQI_TEST_CHECK(SaveConfig(config));
                const uint64_t sequence = QueryRegistryConfigSequence();
                const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true));
                QIQI_TEST_CHECK(window);
                QIQI_TEST_CHECK(ControlText(::GetDlgItem(window.Get(), kIdBtnImportConfig)) ==
                    GetUiStrings(language).dlg_btn_import_config);
                QIQI_TEST_CHECK(ControlText(::GetDlgItem(window.Get(), kIdBtnExportConfig)) ==
                    GetUiStrings(language).dlg_btn_export_config);
                ::SendDlgItemMessageW(window.Get(), kIdComboPageSize, CB_SETCURSEL, 5, 0);
                QiQiConfig current = config;
                ReadSettingsFromWindowControls(window.Get(), current);
                QIQI_TEST_CHECK(current.page_size == 6);
                QiQiConfig imported = current;
                imported.page_size = 7;
                imported.enable_code_completion = false;
                imported.default_chinese_mode = false;
                const auto summary = FormatSettingsConfigChanges(current, imported, language);
                QIQI_TEST_CHECK(summary.find(L"(page_size)") != std::wstring::npos);
                QIQI_TEST_CHECK(summary.find(L"6 -> 7") != std::wstring::npos);
                QIQI_TEST_CHECK(summary.find(L"(enable_code_completion)") != std::wstring::npos);
                QIQI_TEST_CHECK(summary.find(L"true -> false") != std::wstring::npos);
                QIQI_TEST_CHECK(summary.find(L"user_dict_path") == std::wstring::npos);
                WithPreview(window.Get(), current, imported, language, confirm, [&]() {
                    QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), input) == (confirm ? S_OK : S_FALSE));
                });
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence && config.page_size == 5);
                QIQI_TEST_CHECK(config.enable_code_completion && config.default_chinese_mode);
                QiQiConfig edits = config;
                ReadSettingsFromWindowControls(window.Get(), edits);
                QIQI_TEST_CHECK(edits.page_size == (confirm ? 7u : 6u));
                QIQI_TEST_CHECK(edits.enable_code_completion == !confirm);
                QIQI_TEST_CHECK(edits.default_chinese_mode == !confirm);
                QIQI_TEST_CHECK(ExportSettingsConfiguration(window.Get(), output) == S_OK);
                std::string exported;
                QIQI_TEST_CHECK(ReadFileToUtf8String(output, exported));
                ConfigJsonError error;
                QiQiConfig round_trip;
                QIQI_TEST_CHECK(ParseConfigJson(exported, config, round_trip, error) == S_OK);
                ForEachConfigField([&](const auto& field) { QIQI_TEST_CHECK(field.Value(round_trip) == field.Value(edits)); });
                QIQI_TEST_CHECK(exported.find("path") == std::string::npos);
                const dialog_detail::FileDialogApi cancelled{CancelFileDialog, CancelFileDialog, NoDialogError};
                PromptSettingsConfigTransfer(window.Get(), true, cancelled);
                PromptSettingsConfigTransfer(window.Get(), false, cancelled);
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence);
                if (confirm && save) {
                    SetConfigRegistrySubKeyOverrideForTest(L"");
                    const auto message = FormatConfigJsonError({}, HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER), language);
                    WithTransferError(message.c_str(), [&]() {
                        ::SendMessageW(window.Get(), WM_COMMAND, IDOK, 0);
                    });
                    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
                    QIQI_TEST_CHECK(config.page_size == 5 && QueryRegistryConfigSequence() == sequence);
                    const auto* context = reinterpret_cast<const DialogContext*>(
                        ::GetWindowLongPtrW(window.Get(), GWLP_USERDATA));
                    QIQI_TEST_CHECK(context && !context->done && context->imported_config);
                }
                ::SendMessageW(window.Get(), WM_COMMAND, save ? IDOK : IDCANCEL, 0);
                QiQiConfig stored;
                QIQI_TEST_CHECK(LoadConfigFromRegistry(stored));
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence + (save ? 1 : 0));
                QIQI_TEST_CHECK(stored.page_size == (save ? edits.page_size : 5u));
                QIQI_TEST_CHECK(stored.default_chinese_mode == !(confirm && save));
                QIQI_TEST_CHECK(stored.enable_code_completion == !(confirm && save));
                QIQI_TEST_CHECK(stored.user_dict_path == config.user_dict_path &&
                    stored.primary_dict_path == config.primary_dict_path);
            }
        }
    }
    {
        QiQiConfig config;
        config.ui_language = UiLanguage::EnUs;
        const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
        QIQI_TEST_CHECK(window);
        auto imported = config;
        imported.primary_dict = PrimaryDict::Haifeng;
        ConfigJsonError error;
        std::string json;
        QIQI_TEST_CHECK(SerializeConfigJson(imported, json, error) == S_OK);
        QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(json)));
        WithPreview(window.Get(), config, imported, config.ui_language, true, [&]() {
            QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), input) == S_OK);
        });
        QiQiConfig edits = config;
        ReadSettingsFromWindowControls(window.Get(), edits);
        QIQI_TEST_CHECK(edits.primary_dict == PrimaryDict::Haifeng && edits.enable_jidian_extra);
        ::SendMessageW(window.Get(), WM_COMMAND, kIdRadioBuiltin, 0);
        ::SendMessageW(window.Get(), WM_COMMAND, kIdRadioHaifeng, 0);
        ReadSettingsFromWindowControls(window.Get(), edits);
        QIQI_TEST_CHECK(edits.primary_dict == PrimaryDict::Haifeng && !edits.enable_jidian_extra);
        QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(partial)));
    }
    {
        const auto original_monitor = std::exchange(detail::MonitorMetricsOverrideStorage(), std::nullopt);
        const auto restore_monitor = gsl::finally([&]() noexcept {
            SetMonitorMetricsOverrideForTest(original_monitor);
        });
        for (const UINT dpi : {96u, 192u}) {
            SetMonitorMetricsOverrideForTest(MonitorMetrics{RECT{0, 0, 800, 600}, dpi});
            QiQiConfig config;
            config.ui_language = UiLanguage::EnUs;
            config.easy_reading = true;
            config.ui_text_scale = UiTextScale::Percent200;
            const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
            QIQI_TEST_CHECK(window);
            auto imported = config;
            ForEachConfigField([&](const auto& field) {
                using T = std::remove_cvref_t<decltype(config.*(field.member))>;
                if constexpr (std::is_same_v<T, bool>) {
                    if (field.backup_key != "easy_reading") imported.*(field.member) = !(config.*(field.member));
                }
            });
            ConfigJsonError error;
            std::string json;
            QIQI_TEST_CHECK(SerializeConfigJson(imported, json, error) == S_OK);
            QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(json)));
            WithPreview(window.Get(), config, imported, config.ui_language, false, [&]() {
                QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), input) == S_FALSE);
            });
        }
        QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(partial)));
    }
    {
        QiQiConfig config;
        config.ui_language = UiLanguage::EnUs;
        QIQI_TEST_CHECK(SaveConfig(config));
        const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true));
        QIQI_TEST_CHECK(window);
        auto imported = config;
        imported.page_size = 7;
        imported.enable_code_completion = false;
        imported.default_chinese_mode = false;
        WithPreview(window.Get(), config, imported, config.ui_language, true, [&]() {
            QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), input) == S_OK);
        }, true);
        ::SendMessageW(window.Get(), kSettingsExternalConfigChangedMessage, 0, 0);
        QiQiConfig merged = config;
        ReadSettingsFromWindowControls(window.Get(), merged);
        QIQI_TEST_CHECK(merged.page_size == 7 && !merged.default_chinese_mode);
        QIQI_TEST_CHECK(merged.beep_on_dup_4code && merged.default_full_width);
        ::SendMessageW(window.Get(), WM_COMMAND, IDOK, 0);
        QiQiConfig saved;
        QIQI_TEST_CHECK(LoadConfigFromRegistry(saved));
        QIQI_TEST_CHECK(saved.page_size == 7 && !saved.default_chinese_mode && saved.default_full_width);
    }
    {
        QiQiConfig config;
        config.ui_language = UiLanguage::EnUs;
        QIQI_TEST_CHECK(SaveConfig(config));
        const auto sequence = QueryRegistryConfigSequence();
        const ScopedWindow window(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config, true));
        QIQI_TEST_CHECK(window);
        constexpr std::string_view invalid =
            R"({"format":"qiqiime-config","version":1,"settings":{"page_size":7,"unknown":true}})";
        QIQI_TEST_CHECK(WriteFileAtomically(input, codec::AsByteSpan(invalid)));
        ConfigJsonError error;
        QiQiConfig candidate;
        const HRESULT parse = ParseConfigJson(invalid, config, candidate, error);
        const auto message = FormatConfigJsonError(error, parse, config.ui_language);
        WithTransferError(message.c_str(), [&]() {
            QIQI_TEST_CHECK(FAILED(ImportSettingsConfiguration(window.Get(), input)));
        });
        QiQiConfig actual = config;
        ReadSettingsFromWindowControls(window.Get(), actual);
        QIQI_TEST_CHECK(actual.page_size == 5 && QueryRegistryConfigSequence() == sequence);
        const std::string oversized(kMaxConfigJsonBytes + 1, ' ');
        QIQI_TEST_CHECK(WriteFileAtomically(large, codec::AsByteSpan(oversized)));
        std::string unchanged = "unchanged";
        QIQI_TEST_CHECK(ReadFileToUtf8StringResult(large, unchanged, kMaxConfigJsonBytes) ==
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        QIQI_TEST_CHECK(unchanged == "unchanged");
        const auto large_message = FormatConfigJsonError({ConfigJsonIssue::TooLarge},
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE), config.ui_language);
        WithTransferError(large_message.c_str(), [&]() {
            QIQI_TEST_CHECK(ImportSettingsConfiguration(window.Get(), large) == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        });
        QIQI_TEST_CHECK(WriteFileAtomically(output, codec::AsByteSpan("preserve")));
        {
            const ScopedFileHandle lock(::CreateFileW(output.c_str(), GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QIQI_TEST_CHECK(lock.IsValid());
            const auto write_message = FormatConfigJsonError({}, HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
                config.ui_language);
            WithTransferError(write_message.c_str(), [&]() {
                QIQI_TEST_CHECK(ExportSettingsConfiguration(window.Get(), output) ==
                    HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
            });
        }
        std::string preserved;
        QIQI_TEST_CHECK(ReadFileToUtf8String(output, preserved) && preserved == "preserve");
        QIQI_TEST_CHECK(QueryRegistryConfigSequence() == sequence);
    }
}

} // namespace

void RunSettingsTransferTests() { TestTransferFlow(); }

} // namespace qiqi::test
