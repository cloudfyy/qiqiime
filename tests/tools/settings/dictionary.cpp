// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"
#include "common/support.h"
#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "ime/engine/dict.h"
#include "tools/settings/internal.h"
#include "tools/settings/window.h"
#include "common/dictui.h"

#include <windows.h>
#include <array>
#include <string>
#include <tuple>
#include <utility>

namespace qiqi::test {

namespace {

thread_local bool unexpected_dictionary_picker_seen = false;
void TestSettingsDictionaryDependencies() {
    using namespace settings_detail;
    QiQiConfig config;
    config.ui_language = UiLanguage::EnUs;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = true;
    const ScopedWindow dialog(CreateSettingsWindow(::GetModuleHandleW(nullptr), nullptr, &config));
    QIQI_TEST_CHECK(dialog);
    unexpected_dictionary_picker_seen = false;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 30, [](HWND, UINT, UINT_PTR, DWORD) noexcept {
        if (const HWND picker = FindThreadWindowByClass(L"#32770")) {
            unexpected_dictionary_picker_seen = true;
            ::SendMessageW(picker, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cleanup = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    const auto click = [&](int id) {
        ::SendMessageW(dialog.Get(), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    };
    const HWND browse = ::GetDlgItem(dialog.Get(), kIdBtnBrowsePrimaryDict);
    const HWND extra = ::GetDlgItem(dialog.Get(), kIdChkExtraDict);
    QIQI_TEST_CHECK(!::IsWindowEnabled(browse) && !::IsWindowEnabled(extra));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
    std::array<wchar_t, 128> title{};
    ::GetWindowTextW(dialog.Get(), title.data(), gsl::narrow_cast<int>(title.size()));
    QIQI_TEST_CHECK(std::wstring(title.data()) == GetUiStrings(UiLanguage::EnUs).dlg_title);
    QIQI_TEST_CHECK(config.enable_jidian_extra);
    click(kIdBtnBrowsePrimaryDict);
    QIQI_TEST_CHECK(!unexpected_dictionary_picker_seen);
    click(kIdRadioCustom);
    QIQI_TEST_CHECK(!unexpected_dictionary_picker_seen);
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdRadioCustom) == BST_CHECKED);
    QIQI_TEST_CHECK(::IsWindowEnabled(browse) && !::IsWindowEnabled(extra));
    QiQiConfig edited;
    ReadSettingsFromWindowControls(dialog.Get(), edited);
    QIQI_TEST_CHECK(edited.primary_dict == PrimaryDict::Custom && edited.primary_dict_path.empty());
    WithDismissedMessage(GetUiStrings(UiLanguage::EnUs).dlg_invalid_primary_dict, [&] {
        click(IDOK);
    });
    const auto* context = reinterpret_cast<const DialogContext*>(
        ::GetWindowLongPtrW(dialog.Get(), GWLP_USERDATA));
    QIQI_TEST_CHECK(context && !context->done && !context->accepted);
    QIQI_TEST_CHECK(config.primary_dict == PrimaryDict::BuiltinOnly);
    for (const int id : {kIdRadioHaifeng, kIdRadioBuiltin, kIdRadioCustom}) {
        click(kIdRadioJidian);
        QIQI_TEST_CHECK(::IsWindowEnabled(extra));
        QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
        ::CheckDlgButton(dialog.Get(), kIdChkExtraDict, BST_CHECKED);
        click(id);
        QIQI_TEST_CHECK(::IsWindowEnabled(browse) == (id == kIdRadioCustom));
        QIQI_TEST_CHECK(!::IsWindowEnabled(extra));
        QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
        ReadSettingsFromWindowControls(dialog.Get(), edited);
        QIQI_TEST_CHECK(!edited.enable_jidian_extra);
    }
    click(kIdRadioJidian);
    QIQI_TEST_CHECK(::IsWindowEnabled(extra));
    QIQI_TEST_CHECK(::IsDlgButtonChecked(dialog.Get(), kIdChkExtraDict) == BST_UNCHECKED);
}

int WINAPI FailDictionaryDialog(HWND, LPCWSTR, LPCWSTR, UINT) noexcept {
    ::SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return 0;
}

void TestDictionaryConfirmationText() {
    std::map<uint32_t, std::vector<std::wstring>> entries;
    entries[PackWubiCode(L"aaaa")] = {L"词"};
    std::vector<uint8_t> binary;
    WubiMappedDict::CompileEntriesToBinaryBuffer(entries, binary);
    PrimaryDictValidationResult validation{
        PrimaryDictValidationStatus::Valid, ERROR_SUCCESS,
        WubiMappedDict::LoadFromBinaryBuffer(std::move(binary)),
        {1, 10, {{2, DictionaryLineError::MissingWord}}}
    };
    const auto zh = dictionary_ui::ValidationDetails(
        validation.diagnostics, UiLanguage::ZhCn);
    const auto en = dictionary_ui::ValidationDetails(
        validation.diagnostics, UiLanguage::EnUs);
    QIQI_TEST_CHECK(zh.find(L"有效行：1。无效行：10。") != std::wstring::npos &&
        zh.find(L"第 2 行：缺少词语") != std::wstring::npos);
    QIQI_TEST_CHECK(en.find(L"Valid rows: 1. Invalid rows: 10.") != std::wstring::npos &&
        en.find(L"Line 2: Missing word") != std::wstring::npos);
    QIQI_TEST_CHECK(!dictionary_ui::ConfirmPartialDictionary(
        nullptr, UiLanguage::ZhCn, static_cast<bool>(validation),
        validation.diagnostics, &FailDictionaryDialog));
}

} // namespace

void RunSettingsDictionaryTests() {
    TestDictionaryConfirmationText();
    TestSettingsDictionaryDependencies();
}

} // namespace qiqi::test
