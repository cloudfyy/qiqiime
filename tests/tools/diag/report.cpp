// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

#include "tools/diag/diag.h"

#include "common/RAII.h"
#include "common/fileutil.h"
#include "common/support.h"

#include <gsl/gsl>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

using Json = nlohmann::ordered_json;

void TestConfiguredUiLanguage() {
    using namespace qiqi;
    const auto subkey = L"Software\\QiQiIME_Test_DiagLanguage_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(::GetTickCount64());
    const auto* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto restore = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK_CLEANUP(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });

    UiLanguage language = UiLanguage::EnUs;
    QIQI_TEST_CHECK(diag::ReadConfiguredUiLanguage(language) == S_FALSE && language == UiLanguage::Auto);
    ScopedRegKey key;
    DWORD disposition = 0;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, key.Put(), &disposition) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(disposition == REG_CREATED_NEW_KEY);
    for (const auto configured : {UiLanguage::ZhCn, UiLanguage::EnUs, UiLanguage::Auto}) {
        QIQI_TEST_CHECK(WriteRegistryDwordResult(key.Get(), kUiLanguageConfigField.registry_key,
            static_cast<DWORD>(configured)) == S_OK);
        QIQI_TEST_CHECK(diag::ReadConfiguredUiLanguage(language) == S_OK && language == configured);
    }
    QIQI_TEST_CHECK(WriteRegistryDwordResult(key.Get(), kUiLanguageConfigField.registry_key, 99) == S_OK);
    QIQI_TEST_CHECK(diag::ReadConfiguredUiLanguage(language) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA) &&
                    language == UiLanguage::Auto);
}

void TestSkippedSchema() {
    std::string text;
    QIQI_TEST_CHECK(qiqi::diag::BuildReport({}, true, text) == S_OK);
    const Json report = Json::parse(text);
    QIQI_TEST_CHECK(report.at("schema") == "qiqiime-diag");
    QIQI_TEST_CHECK(report.at("schema_version") == 1);
    const auto& stages = report.at("stages");
    QIQI_TEST_CHECK(stages.size() == 4);
    for (const auto& stage : stages) {
        QIQI_TEST_CHECK(stage.at("status") == "skipped");
    }
}

void TestSplitOutput() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
                                       (L"qiqi-diag-test-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    const auto cleanup = gsl::finally([&] {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    });
    std::string text;
    QIQI_TEST_CHECK(qiqi::diag::BuildReport({.machine = true}, false, text) == S_OK);
    QIQI_TEST_CHECK(qiqi::diag::WriteReportSet(root, text) == S_OK);
    QIQI_TEST_CHECK(std::filesystem::exists(root / L"summary.json"));
    QIQI_TEST_CHECK(std::filesystem::exists(root / L"machine.json"));
    QIQI_TEST_CHECK(!std::filesystem::exists(root / L"report.json"));
    QIQI_TEST_CHECK(!std::filesystem::exists(root / L"dcom.json"));
    for (const auto* skipped : {L"processes.json", L"ime.json", L"broker.json"}) {
        QIQI_TEST_CHECK(!std::filesystem::exists(root / skipped));
    }
    std::string summary_text;
    QIQI_TEST_CHECK(qiqi::ReadFileToUtf8String(root / L"summary.json", summary_text));
    const Json summary = Json::parse(summary_text);
    QIQI_TEST_CHECK(summary.at("stages").at(0).at("file") == "machine.json");
    std::string machine_text;
    QIQI_TEST_CHECK(qiqi::ReadFileToUtf8String(root / L"machine.json", machine_text));
    const Json machine = Json::parse(machine_text);
    QIQI_TEST_CHECK(machine.at("stage").at("display").at("monitors").is_array());
    const auto& operating_system = machine.at("stage").at("operating_system");
    QIQI_TEST_CHECK(operating_system.at("architecture") != "machine-0x0009");
    if (operating_system.value("build", 0U) >= 22000 &&
        operating_system.value("registry_product_name", std::string{}).starts_with("Windows 10")) {
        QIQI_TEST_CHECK(operating_system.value("product_name", std::string{}).starts_with("Windows 11"));
    }
    const auto& cpu = machine.at("stage").at("cpu");
    QIQI_TEST_CHECK(cpu.at("physical_core_count").get<uint64_t>() > 0);
    QIQI_TEST_CHECK(cpu.at("logical_processor_count").get<uint64_t>() >=
                    cpu.at("physical_core_count").get<uint64_t>());
    QIQI_TEST_CHECK(cpu.at("processor_group_count").get<uint64_t>() > 0);
    QIQI_TEST_CHECK(cpu.at("cache_total_bytes").at("l1_bytes").get<uint64_t>() > 0);
    QIQI_TEST_CHECK(machine.at("stage").at("fonts").at("chinese_font_faces").is_array());
    QIQI_TEST_CHECK(machine.at("stage").at("accessibility").at("screen_reader").is_boolean());
    QIQI_TEST_CHECK(machine.at("stage").at("accessibility").at("narrator_running").is_boolean());
    QIQI_TEST_CHECK(machine.at("stage").at("theme").is_object());
    QIQI_TEST_CHECK(qiqi::diag::WriteReportSet(root, text) == HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY));
}
