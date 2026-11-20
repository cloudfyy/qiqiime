// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace qiqi {

inline constexpr size_t kMaxZipArchiveEntries = 16;
inline constexpr size_t kMaxZipEntryBytes = 32 * 1024 * 1024;
inline constexpr size_t kMaxZipArchiveBytes = 64 * 1024 * 1024;

struct ZipArchiveEntry {
    std::string name;
    std::vector<uint8_t> data;
};

// Flat portable ASCII names, Deflate compression, and bounded in-memory extraction.
HRESULT BuildZipArchive(
    std::span<const ZipArchiveEntry> entries, std::vector<uint8_t>& output);
HRESULT ReadZipArchive(
    std::span<const uint8_t> bytes, std::vector<ZipArchiveEntry>& entries);

HRESULT WriteZipArchiveFileResult(
    const std::filesystem::path& path, std::span<const ZipArchiveEntry> entries);
HRESULT ReadZipArchiveFileResult(
    const std::filesystem::path& path, std::vector<ZipArchiveEntry>& entries);

} // namespace qiqi
