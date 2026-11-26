// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.h"
#include "cleanup.h"
#include "wizard.h"
#include "common/RAII.h"
#include "common/support.h"

#include <msiquery.h>
#include <objbase.h>
#include <propidl.h>
#include <gsl/gsl>
#include <array>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>

void RunInstallerCleanupTests();

namespace {

std::wstring NewGuid() {
    GUID guid{};
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&guid)));
    std::array<wchar_t, 39> text{};
    QIQI_TEST_CHECK(::StringFromGUID2(guid, text.data(), gsl::narrow_cast<int>(text.size())) != 0);
    return text.data();
}

void CreateFixture(const std::filesystem::path& path) {
    const auto product_code = NewGuid();
    const auto package_code = NewGuid();
    PMSIHANDLE database;
    QIQI_TEST_CHECK(::MsiOpenDatabaseW(path.c_str(), MSIDBOPEN_CREATE, &database) == ERROR_SUCCESS);
    {
        PMSIHANDLE view;
        QIQI_TEST_CHECK(::MsiDatabaseOpenViewW(database,
            L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, "
            L"`Value` CHAR(0) LOCALIZABLE PRIMARY KEY `Property`)", &view) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiViewExecute(view, 0) == ERROR_SUCCESS);
    }
    {
        PMSIHANDLE view;
        QIQI_TEST_CHECK(::MsiDatabaseOpenViewW(database,
            L"INSERT INTO `Property` (`Property`, `Value`) VALUES (?, ?)", &view) == ERROR_SUCCESS);
        for (const auto& property : {
                std::pair{L"ProductCode", product_code.c_str()},
                std::pair{L"ProductName", L"QiQiIME Prompt Test Fixture"},
                std::pair{L"ProductVersion", L"1.0.0"},
                std::pair{L"ProductLanguage", L"1033"},
                std::pair{L"Manufacturer", L"QiQiIME Tests"}}) {
            PMSIHANDLE record = ::MsiCreateRecord(2);
            QIQI_TEST_CHECK(record);
            QIQI_TEST_CHECK(::MsiRecordSetStringW(record, 1, property.first) == ERROR_SUCCESS);
            QIQI_TEST_CHECK(::MsiRecordSetStringW(record, 2, property.second) == ERROR_SUCCESS);
            QIQI_TEST_CHECK(::MsiViewExecute(view, record) == ERROR_SUCCESS);
        }
    }
    {
        PMSIHANDLE summary;
        QIQI_TEST_CHECK(::MsiGetSummaryInformationW(database, nullptr, 5, &summary) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoSetPropertyW(summary, PID_CODEPAGE, VT_I2, 1252, nullptr, nullptr) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoSetPropertyW(summary, PIDSI_TEMPLATE, VT_LPSTR, 0, nullptr, L"Intel;1033") == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoSetPropertyW(summary, PIDSI_REVNUMBER, VT_LPSTR, 0, nullptr, package_code.c_str()) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoSetPropertyW(summary, PIDSI_PAGECOUNT, VT_I4, 200, nullptr, nullptr) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoSetPropertyW(summary, PIDSI_WORDCOUNT, VT_I4, 0, nullptr, nullptr) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::MsiSummaryInfoPersist(summary) == ERROR_SUCCESS);
    }
    QIQI_TEST_CHECK(::MsiDatabaseCommit(database) == ERROR_SUCCESS);
}

void SetProperty(MSIHANDLE install, const wchar_t* name, const wchar_t* value) {
    QIQI_TEST_CHECK(::MsiSetPropertyW(install, name, value) == ERROR_SUCCESS);
}

std::wstring GetProperty(MSIHANDLE install, const wchar_t* name) {
    std::array<wchar_t, 1024> value{};
    DWORD count = gsl::narrow_cast<DWORD>(value.size());
    QIQI_TEST_CHECK(::MsiGetPropertyW(install, name, value.data(), &count) == ERROR_SUCCESS);
    return std::wstring(value.data(), count);
}

struct UiState {
    unsigned int prompts = 0;
    unsigned int errors = 0;
};

int __stdcall HandleMessage(void* context, UINT message, MSIHANDLE record) noexcept {
    auto& state = *static_cast<UiState*>(context);
    const UINT type = message & 0xFF000000u;
    if (type == INSTALLMESSAGE_USER) {
        ++state.prompts;
        return IDOK;
    }
    if (type == INSTALLMESSAGE_INFO && ::MsiRecordGetFieldCount(record) == 2) {
        std::array<wchar_t, 256> format{};
        DWORD count = gsl::narrow_cast<DWORD>(format.size());
        if (::MsiRecordGetStringW(record, 0, format.data(), &count) == ERROR_SUCCESS &&
            std::wstring_view(format.data(), count).starts_with(L"QiQiIME installer:")) {
            ++state.errors;
        }
    }
    return IDOK;
}

void ResetUninstall(MSIHANDLE install) {
    SetProperty(install, L"Installed", L"1");
    SetProperty(install, L"REMOVE", L"ALL");
    SetProperty(install, L"UPGRADINGPRODUCTCODE", nullptr);
    SetProperty(install, L"UILevel", L"2");
    SetProperty(install, L"QIQI_REMOVE_USER_DATA", nullptr);
    SetProperty(install, L"QIQI_REMOVE_CONFIGURATION", nullptr);
    SetProperty(install, L"QIQI_REMOVE_DICTIONARIES", nullptr);
    SetProperty(install, L"QIQI_WIZARD_SHOWN", nullptr);
    SetProperty(install, L"QIQI_SYSTEM_RESTART_REQUIRED", nullptr);
    SetProperty(install, L"UserSID", L"S-1-5-21-111-222-333-1001");
    SetProperty(install, L"INSTALLFOLDER", L"C:\\QiQiIME synthetic program");
}

void TestChoicesAndSerialization(MSIHANDLE install, const UiState& ui) {
    using namespace qiqi::installer;
    for (const bool configuration : {false, true}) {
        for (const bool dictionaries : {false, true}) {
            ResetUninstall(install);
            SetProperty(install, L"QIQI_REMOVE_CONFIGURATION", configuration ? L"1" : L"0");
            SetProperty(install, L"QIQI_REMOVE_DICTIONARIES", dictionaries ? L"1" : L"0");
            QIQI_TEST_CHECK(PrepareUninstall(install) == ERROR_SUCCESS);
            CleanupRequest request;
            QIQI_TEST_CHECK(ParseCleanupRequest(GetProperty(install, L"ExecuteCleanup"), request) == S_OK);
            QIQI_TEST_CHECK(request.configuration == configuration && request.dictionaries == dictionaries);
            QIQI_TEST_CHECK(request.sid == L"S-1-5-21-111-222-333-1001");
            QIQI_TEST_CHECK(request.transaction == GetProperty(install, L"QiQiCleanupTransaction"));
        }
    }
    ResetUninstall(install);
    SetProperty(install, L"UserSID", L"S-1-5-18");
    const auto prompts = ui.prompts;
    QIQI_TEST_CHECK(PrepareUninstall(install) == ERROR_SUCCESS && ui.prompts == prompts);
    CleanupRequest request;
    QIQI_TEST_CHECK(ParseCleanupRequest(GetProperty(install, L"ExecuteCleanup"), request) == S_OK);
    QIQI_TEST_CHECK(request.sid == L"S-1-5-18" && !request.configuration && !request.dictionaries);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"", L"", L"", request) == S_OK);
    QIQI_TEST_CHECK(!request.configuration && !request.dictionaries);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"1", L"", L"", request) == S_OK);
    QIQI_TEST_CHECK(request.configuration && request.dictionaries);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"1", L"0", L"1", request) == S_OK);
    QIQI_TEST_CHECK(!request.configuration && request.dictionaries);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"0", L"1", L"0", request) == S_OK);
    QIQI_TEST_CHECK(request.configuration && !request.dictionaries);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"yes", L"", L"", request) == E_INVALIDARG);
    QIQI_TEST_CHECK(ParseCleanupChoices(L"", L"2", L"", request) == E_INVALIDARG);
    QIQI_TEST_CHECK(IsPendingProgramFile(L"\\??\\C:\\Program Files\\QiQiIME\\x64\\qiqiime.dll",
        L"C:\\Program Files\\QiQiIME\\"));
    QIQI_TEST_CHECK(IsPendingProgramFile(L"!\\??\\c:\\program files\\qiqiime\\data\\wubi86_jidian.qdict",
        L"C:\\Program Files\\QiQiIME"));
    QIQI_TEST_CHECK(!IsPendingProgramFile(L"\\??\\C:\\Program Files\\Other\\x64\\qiqiime.dll",
        L"C:\\Program Files\\QiQiIME"));
    QIQI_TEST_CHECK(!IsPendingProgramFile(L"\\??\\C:\\Program Files\\QiQiIME-backup\\x64\\qiqiime.dll",
        L"C:\\Program Files\\QiQiIME"));
    QIQI_TEST_CHECK(!IsPendingProgramFile(L"\\??\\C:\\Program Files\\QiQiIME\\x64\\other.dll",
        L"C:\\Program Files\\QiQiIME"));
    QIQI_TEST_CHECK(
        RequiresRestartForProgramLock(
            RmMainWindow,
            true,
            L"C:\\Users\\Test\\App\\app.exe",
            L"C:\\Windows") ==
        false);
    QIQI_TEST_CHECK(
        RequiresRestartForProgramLock(
            RmMainWindow,
            true,
            L"C:\\Windows\\SystemApps\\SearchHost.exe",
            L"C:\\Windows"));
    QIQI_TEST_CHECK(
        RequiresRestartForProgramLock(
            RmExplorer,
            true,
            L"C:\\Tools\\explorer.exe",
            L"C:\\Windows"));
    QIQI_TEST_CHECK(
        RequiresRestartForProgramLock(
            RmOtherWindow,
            false,
            L"C:\\Tools\\unknown.exe",
            L"C:\\Windows"));
    QIQI_TEST_CHECK(
        RequiresRestartForProgramLock(
            RmOtherWindow,
            true,
            L"C:\\WindowsBackup\\app.exe",
            L"C:\\Windows") ==
        false);
    QIQI_TEST_CHECK(
        !ShouldPromptUninstallResult(
            S_OK,
            false,
            0));
    QIQI_TEST_CHECK(
        !ShouldPromptUninstallResult(
            S_OK,
            true,
            0));
    QIQI_TEST_CHECK(
        ShouldPromptUninstallResult(
            S_OK,
            true,
            1));
    QIQI_TEST_CHECK(
        ShouldPromptUninstallResult(
            E_ACCESSDENIED,
            false,
            0));
    for (const auto* invalid : {L"", L"sid|guid|11", L"S-1-5-18|{00000000-0000-0000-0000-000000000001}|11",
                               L"S-1-5-21-111-222-333-1001|{00000000-0000-0000-0000-000000000001}|10|extra"}) {
        QIQI_TEST_CHECK(FAILED(ParseCleanupRequest(invalid, request)));
    }

    ResetUninstall(install);
    const auto prompts_before_choose = ui.prompts;
    QIQI_TEST_CHECK(ChooseUninstallData(install) == ERROR_SUCCESS && ui.prompts == prompts_before_choose);
    for (const auto& skip : {std::pair{L"UPGRADINGPRODUCTCODE", L"{00000000-0000-0000-0000-000000000001}"},
                            std::pair{L"Installed", L""}, std::pair{L"REMOVE", L""}}) {
        ResetUninstall(install);
        SetProperty(install, L"ExecuteCleanup", nullptr);
        SetProperty(install, skip.first, skip.second);
        QIQI_TEST_CHECK(PrepareUninstall(install) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(GetProperty(install, L"ExecuteCleanup").empty());
    }
    ResetUninstall(install);
    SetProperty(install, L"QIQI_REMOVE_DICTIONARIES", L"invalid");
    const auto errors = ui.errors;
    QIQI_TEST_CHECK(PrepareUninstall(install) == ERROR_INSTALL_FAILURE && ui.errors == errors + 1);
    SetProperty(install, L"CustomActionData", L"invalid");
    QIQI_TEST_CHECK(ExecuteCleanup(install) == ERROR_INSTALL_FAILURE);
}

struct DialogDriver {
    bool configuration;
    bool dictionaries;
    bool cancel;
    bool clicked = false;
    HWND owner = nullptr;
};

BOOL CALLBACK DriveDialog(HWND window, LPARAM parameter) {
    using namespace qiqi::installer;
    auto& driver = *reinterpret_cast<DialogDriver*>(parameter);
    if (!::GetDlgItem(window, kConfigurationControl) || !::GetDlgItem(window, kDictionariesControl)) return TRUE;
    QIQI_TEST_CHECK(::GetWindow(window, GW_OWNER) == driver.owner);
    if (driver.owner) QIQI_TEST_CHECK(!::IsWindowEnabled(driver.owner));
    QIQI_TEST_CHECK((::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    QIQI_TEST_CHECK((::GetWindowLongPtrW(window, GWL_STYLE) & DS_SETFOREGROUND) != 0);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(window, kConfigurationControl) == BST_UNCHECKED);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(window, kDictionariesControl) == BST_UNCHECKED);
    if (driver.configuration) ::SendDlgItemMessageW(window, kConfigurationControl, BM_CLICK, 0, 0);
    if (driver.dictionaries) ::SendDlgItemMessageW(window, kDictionariesControl, BM_CLICK, 0, 0);
    driver.clicked = true;
    ::SendMessageW(window, WM_COMMAND, driver.cancel ? IDCANCEL : IDOK, 0);
    return FALSE;
}

template<typename Show>
void WithDialogDriver(unsigned int choice, Show&& show, HWND owner = nullptr) {
    const DWORD thread = ::GetCurrentThreadId();
    std::atomic<bool> stop{false};
    const bool owner_enabled = !owner || ::IsWindowEnabled(owner) != FALSE;
    const std::jthread automation([&stop, thread, choice, owner]() {
        DialogDriver driver{(choice & 1u) != 0, (choice & 2u) != 0, choice == 4u, false, owner};
        for (unsigned int attempt = 0; attempt < 1000 && !stop.load() && !driver.clicked; ++attempt) {
            ::EnumThreadWindows(thread, DriveDialog, reinterpret_cast<LPARAM>(&driver));
            ::Sleep(10);
        }
        QIQI_TEST_CHECK(driver.clicked);
    });
    const DPI_AWARENESS_CONTEXT previous_dpi =
        ::GetThreadDpiAwarenessContext();
    show();
    QIQI_TEST_CHECK(
        ::AreDpiAwarenessContextsEqual(
            previous_dpi,
            ::GetThreadDpiAwarenessContext()) !=
        FALSE);
    stop.store(true);
    if (owner) QIQI_TEST_CHECK((::IsWindowEnabled(owner) != FALSE) == owner_enabled);
}

void TestWizard() {
    using namespace qiqi::installer;
    for (const bool chinese : {false, true}) {
        const WizardText text{
            chinese ? L"卸载向导测试" : L"Uninstall wizard test",
            chinese ? L"选择数据" : L"Select data",
            chinese ? L"删除配置" : L"Delete settings",
            chinese ? L"删除词典" : L"Delete dictionary data",
            chinese ? L"保留外部源词库和备份" : L"External source dictionaries and backups are kept.",
            chinese ? L"卸载" : L"Uninstall", chinese ? L"取消" : L"Cancel"};
        for (const unsigned int choice : {0u, 1u, 2u, 3u, 4u}) {
            bool configuration = false;
            bool dictionaries = false;
            bool accepted = false;
            WithDialogDriver(choice, [&]() {
                QIQI_TEST_CHECK(ShowDataChoices(text, configuration, dictionaries, accepted) == S_OK);
            });
            QIQI_TEST_CHECK(accepted == (choice != 4u));
            QIQI_TEST_CHECK(configuration == ((choice & 1u) != 0) && dictionaries == ((choice & 2u) != 0));
        }
    }
}

void TestWizardOwnership() {
    using namespace qiqi::installer;
    constexpr const wchar_t* product = L"QiQiIME synthetic installer ownership";
    WNDCLASSW type{};
    type.lpfnWndProc = ::DefWindowProcW;
    type.hInstance = ::GetModuleHandleW(nullptr);
    type.lpszClassName = L"MsiDialogCloseClass";
    const ATOM atom = ::RegisterClassW(&type);
    QIQI_TEST_CHECK(atom);
    const auto unregister = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(::UnregisterClassW(type.lpszClassName, type.hInstance));
    });
    const qiqi::ScopedWindow unrelated(::CreateWindowExW(0, type.lpszClassName,
        L"Another product", WS_POPUP | WS_VISIBLE, 0, 0, 100, 100, nullptr, nullptr, type.hInstance, nullptr));
    const std::wstring owner_title =
        std::wstring(product) +
        L" Uninstall Wizard";
    const qiqi::ScopedWindow owner(::CreateWindowExW(0, type.lpszClassName,
        owner_title.c_str(), WS_POPUP | WS_VISIBLE, 0, 0, 200, 150, nullptr, nullptr, type.hInstance, nullptr));
    QIQI_TEST_CHECK(unrelated && owner);
    QIQI_TEST_CHECK(FindInstallerOwner(product) == owner.Get());
    QIQI_TEST_CHECK(!FindInstallerOwner(L"Missing installer") && !FindInstallerOwner(L""));
    const WizardText text{L"Wizard ownership", L"Select data", L"Settings", L"Dictionaries",
        L"Back up first.", L"Start", L"Cancel"};
    for (const bool initially_enabled : {false, true}) {
        ::EnableWindow(owner.Get(), initially_enabled);
        for (const unsigned int choice : {0u, 4u}) {
            bool configuration = false, dictionaries = false, accepted = false;
            WithDialogDriver(choice, [&]() {
                QIQI_TEST_CHECK(ShowDataChoices(text, configuration, dictionaries, accepted,
                    FindInstallerOwner(product)) == S_OK);
            }, owner.Get());
            QIQI_TEST_CHECK(accepted == (choice != 4u) && ::IsWindowEnabled(unrelated.Get()));
        }
    }
    ::ShowWindow(owner.Get(), SW_HIDE);
    QIQI_TEST_CHECK(!FindInstallerOwner(product));
}

void SetWizardProperties(MSIHANDLE install, const wchar_t* title) {
    for (const auto* name : {L"QIQI_WIZARD_DESCRIPTION", L"QIQI_WIZARD_CONFIGURATION",
                            L"QIQI_WIZARD_DICTIONARIES", L"QIQI_WIZARD_WARNING",
                            L"QIQI_WIZARD_REMOVE", L"QIQI_WIZARD_CANCEL"}) {
        SetProperty(install, name, L"QiQiIME synthetic wizard test");
    }
    SetProperty(install, L"QIQI_WIZARD_TITLE", title);
}

void TestWizardAction(MSIHANDLE install) {
    for (const auto* level : {L"3", L"4", L"5"}) {
        for (const bool cancel : {false, true}) {
            ResetUninstall(install);
            SetProperty(install, L"UILevel", level);
            SetProperty(install, L"ExecuteCleanup", nullptr);
            SetWizardProperties(install, L"QiQiIME synthetic wizard test");
            const UINT expected = cancel ? ERROR_INSTALL_USEREXIT : ERROR_SUCCESS;
            WithDialogDriver(cancel ? 4u : 2u, [&]() {
                QIQI_TEST_CHECK(ChooseUninstallData(install) == expected);
            });
            QIQI_TEST_CHECK(GetProperty(install, L"ExecuteCleanup").empty());
            QIQI_TEST_CHECK(GetProperty(install, L"QIQI_WIZARD_SHOWN") == (cancel ? L"" : L"1"));
            QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_CONFIGURATION") == (cancel ? L"" : L"0"));
            QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_DICTIONARIES") == (cancel ? L"" : L"1"));
            if (!cancel) QIQI_TEST_CHECK(ChooseUninstallData(install) == ERROR_SUCCESS);
        }
    }
}

void TestExportedWizardAction(MSIHANDLE install) {
    std::array<wchar_t, 32768> executable{};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable.data(), gsl::narrow_cast<DWORD>(executable.size()));
    QIQI_TEST_CHECK(length != 0 && length < executable.size());
    const auto dll = std::filesystem::path(std::wstring_view(executable.data(), length)).parent_path() /
        L"QiQiIMEInstallerActions.dll";
    const qiqi::ScopedModule module(::LoadLibraryExW(dll.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
    QIQI_TEST_CHECK(module);
    for (const auto* name : {"ChooseUninstallData", "PrepareUninstall", "ExecuteCleanup",
                             "CompleteUninstall", "CheckInstallPendingCleanup"}) {
        QIQI_TEST_CHECK(::GetProcAddress(module.Get(), name) != nullptr);
    }
    using Choose = UINT (__stdcall*)(MSIHANDLE);
    const auto choose = reinterpret_cast<Choose>(::GetProcAddress(module.Get(), "ChooseUninstallData"));
    for (const auto* level : {L"3", L"5"}) {
        ResetUninstall(install);
        SetProperty(install, L"UILevel", level);
        const auto caption = L"QiQiIME fixture " + NewGuid();
        SetWizardProperties(install, caption.c_str());
        WithDialogDriver(1u, [&]() {
            QIQI_TEST_CHECK(choose(install) == ERROR_SUCCESS);
        });
        QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_CONFIGURATION") == L"1");
        QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_DICTIONARIES") == L"0");
    }
}

} // namespace

int wmain() {
    const HRESULT initialized = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    QIQI_TEST_CHECK(SUCCEEDED(initialized));
    const auto uninitialize = gsl::finally([]() noexcept { ::CoUninitialize(); });
    const auto directory = std::filesystem::temp_directory_path() / (L"qiqi-installer-prompt-" + NewGuid());
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    const auto remove_fixture = gsl::finally([&]() { std::filesystem::remove_all(directory); });
    const auto path = directory / L"fixture.msi";
    const auto log = directory / L"fixture.log";
    QIQI_TEST_CHECK(::MsiEnableLogW(INSTALLLOGMODE_FATALEXIT | INSTALLLOGMODE_ERROR |
        INSTALLLOGMODE_WARNING | INSTALLLOGMODE_INFO | INSTALLLOGMODE_VERBOSE, log.c_str(), INSTALLLOGATTRIBUTES_FLUSHEACHLINE)
        == ERROR_SUCCESS);
    const auto stop_log = gsl::finally([]() noexcept { ::MsiEnableLogW(0, nullptr, 0); });
    CreateFixture(path);

    const INSTALLUILEVEL previous = ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
    QIQI_TEST_CHECK(previous != INSTALLUILEVEL_NOCHANGE);
    const auto restore_ui = gsl::finally([previous]() noexcept { ::MsiSetInternalUI(previous, nullptr); });
    UiState ui;
    INSTALLUI_HANDLER_RECORD previous_handler = nullptr;
    QIQI_TEST_CHECK(::MsiSetExternalUIRecord(&HandleMessage,
        INSTALLLOGMODE_USER | INSTALLLOGMODE_INFO | INSTALLLOGMODE_ERROR | INSTALLLOGMODE_WARNING,
        &ui, &previous_handler) == ERROR_SUCCESS && !previous_handler);
    const auto restore_handler = gsl::finally([]() noexcept {
        QIQI_TEST_CHECK(::MsiSetExternalUIRecord(nullptr, 0, nullptr, nullptr) == ERROR_SUCCESS);
    });
    PMSIHANDLE install;
    QIQI_TEST_CHECK(::MsiOpenPackageExW(path.c_str(), MSIOPENPACKAGEFLAGS_IGNOREMACHINESTATE, &install) == ERROR_SUCCESS);
    TestChoicesAndSerialization(install, ui);
    const auto prompts = ui.prompts;
    QIQI_TEST_CHECK(ChooseUninstallData(0) == ERROR_INSTALL_FAILURE && ui.prompts == prompts);
    TestWizard();
    TestWizardOwnership();
    TestWizardAction(install);
    TestExportedWizardAction(install);
    RunInstallerCleanupTests();
    std::cout << "[PASS] Uninstall choices, wizard, isolated cleanup and restart gate.\n";
    return 0;
}
