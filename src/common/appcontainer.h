// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <span>
#include <string>
#include <vector>

namespace qiqi {

class DerivedCapabilitySids final {
public:
    explicit DerivedCapabilitySids(
        const wchar_t* name) noexcept;
    ~DerivedCapabilitySids() noexcept;
    DerivedCapabilitySids(
        const DerivedCapabilitySids&) =
        delete;
    DerivedCapabilitySids& operator=(
        const DerivedCapabilitySids&) =
        delete;

    HRESULT Build() noexcept;
    std::span<PSID> Capabilities() noexcept;
    std::span<const PSID>
        Capabilities() const noexcept;
    bool Contains(PSID candidate)
        const noexcept;

private:
    const wchar_t* name_ = nullptr;
    PSID* groups_ = nullptr;
    PSID* capabilities_ = nullptr;
    DWORD group_count_ = 0;
    DWORD capability_count_ = 0;
};

struct AppContainerCapabilityState {
    bool is_app_container = false;
    bool has_lpac_com = false;
    bool has_registry_read = false;
};

HRESULT QueryCurrentProcessAppContainerCapabilities(
    AppContainerCapabilityState& output) noexcept;

HRESULT DiscoverWindowsSearchPackageSids(
    std::vector<std::wstring>& output) noexcept;

} // namespace qiqi
