// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.h"
#include "common/test_support.h"

#include <msiquery.h>
#include <objbase.h>
#include <propidl.h>
#include <gsl/gsl>
#include <array>
#include <filesystem>
#include <string>

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

constexpr const wchar_t* kPromptText = L"Delete synthetic test data? No keeps the data.";

struct UiState {
    MSIHANDLE install = 0;
    std::wstring_view expected_text = kPromptText;
    int response = IDNO;
    unsigned int prompts = 0;
    unsigned int errors = 0;
    int error_code = 0;
};

int __stdcall HandleMessage(void* context, UINT message, MSIHANDLE record) {
    auto& state = *static_cast<UiState*>(context);
    const UINT type = message & 0xFF000000u;
    if (type == INSTALLMESSAGE_USER) {
        QIQI_TEST_CHECK((message & MB_TYPEMASK) == MB_YESNOCANCEL);
        QIQI_TEST_CHECK((message & MB_DEFMASK) == MB_DEFBUTTON2);
        QIQI_TEST_CHECK((message & MB_ICONMASK) == MB_ICONWARNING);
        std::array<wchar_t, 256> text{};
        DWORD count = gsl::narrow_cast<DWORD>(text.size());
        QIQI_TEST_CHECK(::MsiFormatRecordW(state.install, record, text.data(), &count) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(std::wstring_view(text.data(), count) == state.expected_text);
        ++state.prompts;
        return state.response;
    }
    if (type == INSTALLMESSAGE_INFO && ::MsiRecordGetFieldCount(record) == 2) {
        std::array<wchar_t, 256> format{};
        DWORD count = gsl::narrow_cast<DWORD>(format.size());
        if (::MsiRecordGetStringW(record, 0, format.data(), &count) == ERROR_SUCCESS &&
            std::wstring_view(format.data(), count).starts_with(L"QiQiIME uninstall prompt:")) {
            ++state.errors;
            state.error_code = ::MsiRecordGetInteger(record, 2);
        }
    }
    return IDOK;
}

void ResetUninstall(MSIHANDLE install, const wchar_t* prompt = kPromptText) {
    SetProperty(install, L"Installed", L"1");
    SetProperty(install, L"REMOVE", L"ALL");
    SetProperty(install, L"UPGRADINGPRODUCTCODE", nullptr);
    SetProperty(install, L"UILevel", L"3");
    SetProperty(install, L"QIQI_REMOVE_USER_DATA", nullptr);
    SetProperty(install, L"QIQI_REMOVE_USER_DATA_PROMPT", prompt);
}

void TestPrompt(MSIHANDLE install, UiState& ui) {
    for (const auto& language : {std::pair{L"1033", kPromptText},
                                std::pair{L"2052", L"是否删除测试数据？默认选择“否”，保留数据。\r\n取消将停止卸载。"}}) {
        ui.expected_text = language.second;
        for (const int response : {IDNO, IDYES, IDCANCEL, -1}) {
            ResetUninstall(install, language.second);
            SetProperty(install, L"ProductLanguage", language.first);
            ui.response = response;
            const auto prompts = ui.prompts;
            const auto errors = ui.errors;
            const UINT result = PromptRemoveUserData(install);
            const UINT expected = response == IDCANCEL ? ERROR_INSTALL_USEREXIT :
                (response == -1 ? ERROR_INSTALL_FAILURE : ERROR_SUCCESS);
            QIQI_TEST_CHECK(result == expected && ui.prompts == prompts + 1);
            QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_USER_DATA") == (response == IDYES ? L"1" : L""));
            QIQI_TEST_CHECK(ui.errors == errors + (response == -1 ? 1u : 0u));
            if (response == -1) QIQI_TEST_CHECK(ui.error_code == -1);
        }
    }
    ui.expected_text = kPromptText;
    ResetUninstall(install);
    SetProperty(install, L"UILevel", L"4");
    ui.response = IDNO;
    const auto prompts = ui.prompts;
    QIQI_TEST_CHECK(PromptRemoveUserData(install) == ERROR_SUCCESS && ui.prompts == prompts + 1);
    QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_USER_DATA").empty());

    for (const auto& skip : {
            std::pair{L"UILevel", L"2"}, std::pair{L"UILevel", L"5"},
            std::pair{L"UPGRADINGPRODUCTCODE", L"{00000000-0000-0000-0000-000000000001}"},
            std::pair{L"Installed", L""}, std::pair{L"REMOVE", L""},
            std::pair{L"QIQI_REMOVE_USER_DATA", L"0"}, std::pair{L"QIQI_REMOVE_USER_DATA", L"1"}}) {
        ResetUninstall(install);
        SetProperty(install, skip.first, skip.second);
        const auto before = GetProperty(install, L"QIQI_REMOVE_USER_DATA");
        const auto count = ui.prompts;
        QIQI_TEST_CHECK(PromptRemoveUserData(install) == ERROR_SUCCESS && ui.prompts == count);
        QIQI_TEST_CHECK(GetProperty(install, L"QIQI_REMOVE_USER_DATA") == before);
    }
    ResetUninstall(install);
    SetProperty(install, L"QIQI_REMOVE_USER_DATA_PROMPT", nullptr);
    const auto before = ui.prompts;
    const auto errors = ui.errors;
    QIQI_TEST_CHECK(PromptRemoveUserData(install) == ERROR_INSTALL_FAILURE && ui.prompts == before);
    QIQI_TEST_CHECK(ui.errors == errors + 1 && GetProperty(install, L"QIQI_REMOVE_USER_DATA").empty());
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
    ui.install = install;
    TestPrompt(install, ui);
    const auto prompts = ui.prompts;
    QIQI_TEST_CHECK(PromptRemoveUserData(0) == ERROR_INSTALL_FAILURE && ui.prompts == prompts);
    std::cout << "[PASS] Uninstall basic/reduced prompt, default retention, cancellation and guards.\n";
    return 0;
}
