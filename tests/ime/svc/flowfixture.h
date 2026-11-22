// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ime/svc/test.h"

#include <filesystem>

namespace qiqi::test::service_cases {

class ServiceFlowTestFixture final {
public:
    ServiceFlowTestFixture();
    ~ServiceFlowTestFixture() noexcept;
    ServiceFlowTestFixture(const ServiceFlowTestFixture&) = delete;
    ServiceFlowTestFixture& operator=(const ServiceFlowTestFixture&) = delete;
    ServiceFlowTestFixture(ServiceFlowTestFixture&&) = delete;
    ServiceFlowTestFixture& operator=(ServiceFlowTestFixture&&) = delete;

    const std::filesystem::path& AppDataDirectory() const noexcept;
    const std::array<BYTE, 256>& CleanKeyboardState() const noexcept;

private:
    std::wstring suffix_;
    std::wstring registry_subkey_;
    const wchar_t* previous_registry_subkey_;
    std::filesystem::path appdata_directory_;
    std::unique_ptr<wchar_t, decltype(&std::free)> previous_appdata_{nullptr, &std::free};
    std::array<BYTE, 256> original_keyboard_state_{};
    std::array<BYTE, 256> clean_keyboard_state_{};
    bool previous_audio_mute_;
    SoundEffectKind previous_sound_;
};

} // namespace qiqi::test::service_cases
