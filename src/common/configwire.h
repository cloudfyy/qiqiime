// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"
#include "common/skin.h"
#include "common/trace.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace qiqi::config {

inline constexpr size_t kMaxConfigTransportBytes = 256 * 1024;
inline constexpr size_t kMaxConfigPathChars = 32 * 1024;

struct Snapshot {
    uint64_t revision = 0;
    HRESULT notification_result = S_OK;
    GUID commit_trace_id = GUID_NULL;
    GUID commit_activity_id = GUID_NULL;
    uint64_t commit_revision = 0;
    QiQiConfig value;
    std::filesystem::path user_dict_sync_directory;
    ImeSkinCatalog skin_catalog;
};

struct SaveRequest {
    uint64_t expected_revision = 0;
    QiQiConfig value;
};

struct SyncDirectoryRequest {
    uint64_t expected_revision = 0;
    std::filesystem::path directory;
};

HRESULT EncodeSnapshot(const Snapshot& snapshot, std::vector<uint8_t>& output) noexcept;
HRESULT DecodeSnapshot(std::span<const uint8_t> bytes, Snapshot& output) noexcept;
HRESULT EncodeSaveRequest(const SaveRequest& request, std::vector<uint8_t>& output) noexcept;
HRESULT DecodeSaveRequest(std::span<const uint8_t> bytes, SaveRequest& output) noexcept;
HRESULT EncodeSyncDirectoryRequest(
    const SyncDirectoryRequest& request, std::vector<uint8_t>& output) noexcept;
HRESULT DecodeSyncDirectoryRequest(
    std::span<const uint8_t> bytes, SyncDirectoryRequest& output) noexcept;

HRESULT AllocateRpcBuffer(
    std::span<const uint8_t> bytes,
    ULONG& output_size,
    byte*& output) noexcept;
HRESULT ReadRpcBuffer(
    ULONG size,
    const byte* bytes,
    std::vector<uint8_t>& output) noexcept;

} // namespace qiqi::config
