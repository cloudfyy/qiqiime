// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scope.h"
#include "ime/tsf/trace.h"
#include "common/config.h"

#include <array>
#include <format>
#include <iostream>

namespace qiqi::test {

void RunRestrictedInputContractTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto objects = g_com_object_count.load();
    restricted_detail::RunRestrictedInputNativeTests();
    restricted_detail::RunRestrictedInputPolicyTests();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects);
}

void RunRestrictedInputDiagnostics() {
    const auto subkey = std::format(L"Software\\QiQiIME_RestrictedInput_{}", ::GetCurrentProcessId());
    ScopedRegKey key;
    DWORD disposition = 0;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
                                     KEY_ALL_ACCESS, nullptr, key.Put(), &disposition) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(disposition == REG_CREATED_NEW_KEY);
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        key.Reset();
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
    });
    QiQiConfig config{};
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    std::array<BYTE, 256> original{};
    QIQI_TEST_CHECK(::GetKeyboardState(original.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original.data()));
    });
    std::array<BYTE, 256> clean{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean.data()));
    const auto objects = g_com_object_count.load();
    restricted_detail::ObserveCurrentService();
    QIQI_TEST_CHECK(g_com_object_count.load() == objects);
    std::cout << "[OBSERVE] Synthetic callbacks only; not browser, WPF, UWP or game acceptance.\n";
}

} // namespace qiqi::test
