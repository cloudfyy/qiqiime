// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/flowfixture.h"

namespace qiqi::test::service_cases {

#pragma warning(suppress : 26455) // Fixture setup intentionally reports construction failures.
ServiceFlowTestFixture::ServiceFlowTestFixture()
    : suffix_(std::to_wstring(::GetCurrentProcessId()) + L"_" +
              std::to_wstring(::GetTickCount64())),
      registry_subkey_(L"Software\\QiQiIME_TsfUnitTest_" + suffix_),
      previous_registry_subkey_(GetActiveConfigRegistrySubKey()),
      appdata_directory_(std::filesystem::temp_directory_path() /
                         (L"QiQiIME_TsfTestAppData_" + suffix_)),
      previous_audio_mute_(::qiqi::detail::mute_audio_output_for_test.load(std::memory_order_relaxed)),
      previous_sound_(GetLastPlayedSoundForTest()) {
    wchar_t* previous_appdata = nullptr;
    size_t previous_appdata_length = 0;
    QIQI_TEST_CHECK(_wdupenv_s(
        &previous_appdata, &previous_appdata_length, L"LOCALAPPDATA") == 0);
    previous_appdata_.reset(previous_appdata);
    QIQI_TEST_CHECK(std::filesystem::create_directory(appdata_directory_));
    SetConfigRegistrySubKeyOverrideForTest(registry_subkey_.c_str());
    QIQI_TEST_CHECK(_wputenv_s(
        L"LOCALAPPDATA", appdata_directory_.c_str()) == 0);

    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard_state_.data()));
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard_state_.data()));
    SetMuteAudioOutputForTest(true);
    ResetLastPlayedSoundForTest();
}

ServiceFlowTestFixture::~ServiceFlowTestFixture() noexcept {
    QIQI_TEST_CHECK_CLEANUP(::SetKeyboardState(original_keyboard_state_.data()));
    SetMuteAudioOutputForTest(previous_audio_mute_);
    ::qiqi::detail::last_played_sound.store(previous_sound_, std::memory_order_relaxed);

    SetConfigRegistrySubKeyOverrideForTest(previous_registry_subkey_);
    const auto status = ::RegDeleteTreeW(
        HKEY_CURRENT_USER, registry_subkey_.c_str());
    QIQI_TEST_CHECK_CLEANUP(
        status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
    QIQI_TEST_CHECK_CLEANUP(_wputenv_s(
        L"LOCALAPPDATA",
        previous_appdata_ ? previous_appdata_.get() : L"") == 0);
    try {
        std::error_code cleanup_error;
        std::filesystem::remove_all(
            appdata_directory_,
            cleanup_error);
        QIQI_TEST_CHECK_CLEANUP(
            !cleanup_error);
    } catch (...) {
        QIQI_TEST_CHECK_CLEANUP(
            false &&
            "Service flow fixture cleanup threw");
    }
}

const std::filesystem::path&
ServiceFlowTestFixture::AppDataDirectory() const noexcept {
    return appdata_directory_;
}

const std::array<BYTE, 256>&
ServiceFlowTestFixture::CleanKeyboardState() const noexcept {
    return clean_keyboard_state_;
}

} // namespace qiqi::test::service_cases
