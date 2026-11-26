// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/configwire.h"

#include <gsl/gsl>
#include <bit>
#include <functional>
#include <optional>

namespace qiqi::config {

enum class ConfigReadRoute : uint8_t {
    Broker,
    AppContainerRegistry,
    BuiltinDefaults,
};

constexpr ConfigReadRoute SelectConfigReadRoute(
    bool app_container,
    bool has_lpac_com,
    bool has_registry_read) noexcept {
    if (!app_container ||
        has_lpac_com) {
        return ConfigReadRoute::Broker;
    }
    return has_registry_read
        ? ConfigReadRoute::AppContainerRegistry
        : ConfigReadRoute::BuiltinDefaults;
}

constexpr std::string_view
ConfigReadRouteName(
    ConfigReadRoute route) noexcept {
    switch (route) {
    case ConfigReadRoute::Broker:
        return "broker";
    case ConfigReadRoute::
        AppContainerRegistry:
        return "registry";
    case ConfigReadRoute::
        BuiltinDefaults:
        return "builtin";
    }
    return "unknown";
}

using AsyncCompletion =
    std::function<void(
        HRESULT,
        const Snapshot&)>;

static_assert(sizeof(HRESULT) == sizeof(uint32_t));

constexpr WPARAM EncodeAsyncResult(
    HRESULT result) noexcept {
    return gsl::narrow_cast<WPARAM>(
        std::bit_cast<uint32_t>(result));
}

constexpr HRESULT DecodeAsyncResult(
    WPARAM value) noexcept {
    return std::bit_cast<HRESULT>(
        gsl::narrow_cast<uint32_t>(
            value));
}

HRESULT QueryProcessConfigReadRoute(
    ConfigReadRoute& route) noexcept;
void SetConfigReadRouteOverrideForTest(
    std::optional<ConfigReadRoute> route) noexcept;

uint64_t CachedRevision() noexcept;
HRESULT CopyCachedSnapshot(
    Snapshot& output) noexcept;
HRESULT ReadSnapshotFromRegistry(
    Snapshot& output) noexcept;

HRESULT ReadSnapshot(
    Snapshot& output) noexcept;
HRESULT ReadSnapshot(
    const trace::Context& trace,
    Snapshot& output) noexcept;
HRESULT SaveSnapshot(
    const SaveRequest& request,
    Snapshot& output) noexcept;
HRESULT SaveSnapshot(
    const SaveRequest& request,
    const trace::Context& trace,
    Snapshot& output) noexcept;
HRESULT ResetSnapshot(
    uint64_t expected_revision,
    Snapshot& output) noexcept;
HRESULT ResetSnapshot(
    uint64_t expected_revision,
    const trace::Context& trace,
    Snapshot& output) noexcept;
HRESULT PublishSyncDirectory(
    const SyncDirectoryRequest& request,
    Snapshot& output) noexcept;
HRESULT PublishSyncDirectory(
    const SyncDirectoryRequest& request,
    const trace::Context& trace,
    Snapshot& output) noexcept;

HRESULT ReadSnapshotAsync(
    AsyncCompletion completion) noexcept;
HRESULT ReadSnapshotAsync(
    const trace::Context& trace,
    AsyncCompletion completion) noexcept;
HRESULT SaveSnapshotAsync(
    SaveRequest request,
    AsyncCompletion completion) noexcept;
HRESULT SaveSnapshotAsync(
    SaveRequest request,
    const trace::Context& trace,
    AsyncCompletion completion) noexcept;
HRESULT ResetSnapshotAsync(
    uint64_t expected_revision,
    AsyncCompletion completion) noexcept;
HRESULT ResetSnapshotAsync(
    uint64_t expected_revision,
    const trace::Context& trace,
    AsyncCompletion completion) noexcept;
HRESULT PublishSyncDirectoryAsync(
    SyncDirectoryRequest request,
    AsyncCompletion completion) noexcept;
HRESULT PublishSyncDirectoryAsync(
    SyncDirectoryRequest request,
    const trace::Context& trace,
    AsyncCompletion completion) noexcept;

} // namespace qiqi::config
