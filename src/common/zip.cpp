// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/zip.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <array>
#include <algorithm>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace qiqi {
namespace {

constexpr uint32_t kLocalFileHeaderSignature = 0x04034B50;
constexpr uint32_t kCentralDirectorySignature = 0x02014B50;
constexpr uint32_t kEndOfCentralDirectorySignature = 0x06054B50;
constexpr uint16_t kUtf8FileNames = 0x0800;
constexpr uint16_t kStoredMethod = 0;
constexpr uint16_t kMadeByDosVersion20 = 20;
constexpr uint16_t kDosDate1980Jan1 = 0x0021;

constexpr std::array<uint32_t, 256> MakeCrc32Table() noexcept {
    std::array<uint32_t, 256> table{};
    for (size_t index = 0; index < table.size(); ++index) {
        uint32_t value = gsl::narrow_cast<uint32_t>(index);
        for (unsigned int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) != 0
                ? (value >> 1u) ^ 0xedb88320u
                : value >> 1u;
        }
        gsl::at(table, index) = value;
    }
    return table;
}

constexpr auto kCrc32Table = MakeCrc32Table();

constexpr uint32_t Crc32(std::span<const uint8_t> bytes) noexcept {
    uint32_t crc = 0xffffffffu;
    for (const uint8_t byte : bytes) {
        crc = gsl::at(
            kCrc32Table,
            gsl::narrow_cast<size_t>((crc ^ byte) & 0xffu)) ^
            (crc >> 8u);
    }
    return crc ^ 0xffffffffu;
}

inline constexpr std::array<uint8_t, 9> kCrc32Check =
    {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
static_assert(Crc32(kCrc32Check) == 0xcbf43926u);

void AppendLittleEndian(
    std::vector<uint8_t>& bytes, uint64_t value, size_t width) {
    for (size_t index = 0; index < width; ++index) {
        bytes.push_back(gsl::narrow_cast<uint8_t>((value >> (index * 8)) & 0xffu));
    }
}

bool ReadLittleEndian(
    std::span<const uint8_t> bytes, size_t offset, size_t width, uint64_t& value) noexcept {
    if (width > sizeof(value) || offset > bytes.size() ||
        width > bytes.size() - offset) {
        return false;
    }
    value = 0;
    for (size_t index = 0; index < width; ++index) {
        value |= gsl::narrow_cast<uint64_t>(gsl::at(bytes, offset + index))
            << (index * 8);
    }
    return true;
}

bool ReadU16(std::span<const uint8_t> bytes, size_t offset, uint16_t& value) noexcept {
    uint64_t wide = 0;
    if (!ReadLittleEndian(bytes, offset, 2, wide)) return false;
    value = gsl::narrow_cast<uint16_t>(wide);
    return true;
}

bool ReadU32(std::span<const uint8_t> bytes, size_t offset, uint32_t& value) noexcept {
    uint64_t wide = 0;
    if (!ReadLittleEndian(bytes, offset, 4, wide)) return false;
    value = gsl::narrow_cast<uint32_t>(wide);
    return true;
}

bool IsPortableZipName(std::string_view name) noexcept {
    if (name.empty() || name.size() > std::numeric_limits<uint16_t>::max()) {
        return false;
    }
    for (const char ch : name) {
        if (ch <= 0x20 || ch >= 0x7f ||
            ch == '/' || ch == '\\' || ch == ':') {
            return false;
        }
    }
    return name != "." && name != "..";
}

HRESULT RejectZip(std::string_view reason, DWORD error = ERROR_INVALID_DATA) noexcept {
    QIQI_LOG_WARN("ZIP archive rejected (reason={})", reason);
    return HRESULT_FROM_WIN32(error);
}

bool FindEndOfCentralDirectory(
    std::span<const uint8_t> bytes, size_t& offset) noexcept {
    constexpr size_t kMinimumEndSize = 22;
    constexpr size_t kMaximumCommentSize = 65535;
    if (bytes.size() < kMinimumEndSize) return false;
    const size_t minimum = bytes.size() - std::min(
        bytes.size(), kMinimumEndSize + kMaximumCommentSize);
    for (size_t candidate = bytes.size() - kMinimumEndSize;
         candidate >= minimum;
         --candidate) {
        uint32_t signature = 0;
        uint16_t comment_size = 0;
        if (ReadU32(bytes, candidate, signature) &&
            signature == kEndOfCentralDirectorySignature &&
            ReadU16(bytes, candidate + 20, comment_size) &&
            candidate + kMinimumEndSize +
                gsl::narrow_cast<size_t>(comment_size) == bytes.size()) {
            offset = candidate;
            return true;
        }
        if (candidate == minimum) break;
    }
    return false;
}

} // namespace

HRESULT BuildZipArchive(
    std::span<const ZipArchiveEntry> entries, std::vector<uint8_t>& output) {
    try {
        if (entries.size() > kMaxZipArchiveEntries ||
            entries.size() > std::numeric_limits<uint16_t>::max()) {
            return HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
        }

        struct DirectoryRecord {
            std::string name;
            uint32_t crc = 0;
            uint32_t size = 0;
            uint32_t offset = 0;
        };
        std::vector<DirectoryRecord> records;
        records.reserve(entries.size());
        std::vector<uint8_t> bytes;
        size_t total_data = 0;
        std::set<std::string> names;
        for (const auto& entry : entries) {
            if (!IsPortableZipName(entry.name) || entry.data.size() > kMaxZipEntryBytes ||
                entry.data.size() > std::numeric_limits<uint32_t>::max()) {
                return E_INVALIDARG;
            }
            if (!names.emplace(entry.name).second) return HRESULT_FROM_WIN32(ERROR_DUP_NAME);
            if (entry.data.size() > kMaxZipArchiveBytes - total_data) {
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }
            total_data += entry.data.size();
            if (bytes.size() > std::numeric_limits<uint32_t>::max()) {
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }
            const auto local_offset = gsl::narrow_cast<uint32_t>(bytes.size());
            const auto size = gsl::narrow_cast<uint32_t>(entry.data.size());
            const auto crc = Crc32(entry.data);

            AppendLittleEndian(bytes, kLocalFileHeaderSignature, 4);
            AppendLittleEndian(bytes, kMadeByDosVersion20, 2);
            AppendLittleEndian(bytes, kUtf8FileNames, 2);
            AppendLittleEndian(bytes, kStoredMethod, 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, kDosDate1980Jan1, 2);
            AppendLittleEndian(bytes, crc, 4);
            AppendLittleEndian(bytes, size, 4);
            AppendLittleEndian(bytes, size, 4);
            AppendLittleEndian(bytes, entry.name.size(), 2);
            AppendLittleEndian(bytes, 0, 2);
            bytes.insert(bytes.end(), entry.name.begin(), entry.name.end());
            bytes.insert(bytes.end(), entry.data.begin(), entry.data.end());
            if (bytes.size() > kMaxZipArchiveBytes) {
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }
            records.push_back({entry.name, crc, size, local_offset});
        }

        const size_t directory_offset = bytes.size();
        for (const auto& record : records) {
            AppendLittleEndian(bytes, kCentralDirectorySignature, 4);
            AppendLittleEndian(bytes, kMadeByDosVersion20, 2);
            AppendLittleEndian(bytes, kMadeByDosVersion20, 2);
            AppendLittleEndian(bytes, kUtf8FileNames, 2);
            AppendLittleEndian(bytes, kStoredMethod, 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, kDosDate1980Jan1, 2);
            AppendLittleEndian(bytes, record.crc, 4);
            AppendLittleEndian(bytes, record.size, 4);
            AppendLittleEndian(bytes, record.size, 4);
            AppendLittleEndian(bytes, record.name.size(), 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, 0, 2);
            AppendLittleEndian(bytes, 0, 4);
            AppendLittleEndian(bytes, record.offset, 4);
            bytes.insert(bytes.end(), record.name.begin(), record.name.end());
        }
        const size_t directory_size = bytes.size() - directory_offset;
        if (directory_offset > std::numeric_limits<uint32_t>::max() ||
            directory_size > std::numeric_limits<uint32_t>::max() ||
            bytes.size() + 22 > kMaxZipArchiveBytes) {
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        AppendLittleEndian(bytes, kEndOfCentralDirectorySignature, 4);
        AppendLittleEndian(bytes, 0, 2);
        AppendLittleEndian(bytes, 0, 2);
        AppendLittleEndian(bytes, entries.size(), 2);
        AppendLittleEndian(bytes, entries.size(), 2);
        AppendLittleEndian(bytes, directory_size, 4);
        AppendLittleEndian(bytes, directory_offset, 4);
        AppendLittleEndian(bytes, 0, 2);

        output = std::move(bytes);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("ZIP build failed with an allocation error");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("ZIP build failed with an unexpected exception");
        return E_UNEXPECTED;
    }
}

HRESULT ReadZipArchive(
    std::span<const uint8_t> bytes, std::vector<ZipArchiveEntry>& entries) {
    try {
        if (bytes.empty() || bytes.size() > kMaxZipArchiveBytes) {
            return RejectZip("invalid-size", ERROR_FILE_TOO_LARGE);
        }
        size_t eocd = 0;
        if (!FindEndOfCentralDirectory(bytes, eocd)) {
            return RejectZip("end-of-central-directory");
        }
        uint16_t disk = 0;
        uint16_t directory_disk = 0;
        uint16_t disk_entries = 0;
        uint16_t total_entries = 0;
        uint16_t comment_size = 0;
        uint32_t directory_size = 0;
        uint32_t directory_offset = 0;
        if (!ReadU16(bytes, eocd + 4, disk) ||
            !ReadU16(bytes, eocd + 6, directory_disk) ||
            !ReadU16(bytes, eocd + 8, disk_entries) ||
            !ReadU16(bytes, eocd + 10, total_entries) ||
            !ReadU32(bytes, eocd + 12, directory_size) ||
            !ReadU32(bytes, eocd + 16, directory_offset) ||
            !ReadU16(bytes, eocd + 20, comment_size) ||
            disk != 0 || directory_disk != 0 || disk_entries != total_entries ||
            comment_size != 0 || disk_entries == 0xffff ||
            total_entries > kMaxZipArchiveEntries) {
            return RejectZip("directory-metadata");
        }
        if (directory_offset > eocd ||
            directory_size > eocd - directory_offset ||
            directory_offset + directory_size != eocd) {
            return RejectZip("directory-range");
        }

        std::vector<ZipArchiveEntry> parsed;
        parsed.reserve(total_entries);
        std::set<std::string> names;
        struct LocalEntryRange {
            size_t begin = 0;
            size_t end = 0;
        };
        std::vector<LocalEntryRange> local_ranges;
        local_ranges.reserve(total_entries);
        size_t cursor = directory_offset;
        size_t total_uncompressed = 0;
        for (uint16_t index = 0; index < total_entries; ++index) {
            uint32_t signature = 0;
            uint16_t flags = 0;
            uint16_t method = 0;
            uint32_t crc = 0;
            uint32_t compressed_size = 0;
            uint32_t uncompressed_size = 0;
            uint16_t name_size = 0;
            uint16_t extra_size = 0;
            uint16_t entry_comment_size = 0;
            uint16_t entry_disk = 0;
            uint32_t local_offset = 0;
            if (cursor > eocd || eocd - cursor < 46 ||
                !ReadU32(bytes, cursor, signature) ||
                signature != kCentralDirectorySignature ||
                !ReadU16(bytes, cursor + 8, flags) ||
                !ReadU16(bytes, cursor + 10, method) ||
                !ReadU32(bytes, cursor + 16, crc) ||
                !ReadU32(bytes, cursor + 20, compressed_size) ||
                !ReadU32(bytes, cursor + 24, uncompressed_size) ||
                !ReadU16(bytes, cursor + 28, name_size) ||
                !ReadU16(bytes, cursor + 30, extra_size) ||
                !ReadU16(bytes, cursor + 32, entry_comment_size) ||
                !ReadU16(bytes, cursor + 34, entry_disk) ||
                !ReadU32(bytes, cursor + 42, local_offset)) {
                return RejectZip("central-entry");
            }
            const size_t name_offset = cursor + 46;
            const size_t next = name_offset +
                gsl::narrow_cast<size_t>(name_size) +
                gsl::narrow_cast<size_t>(extra_size) +
                gsl::narrow_cast<size_t>(entry_comment_size);
            if (next < name_offset || next > eocd ||
                (flags != 0 && flags != kUtf8FileNames) ||
                method != kStoredMethod ||
                compressed_size != uncompressed_size ||
                compressed_size > kMaxZipEntryBytes ||
                entry_disk != 0 ||
                compressed_size > kMaxZipArchiveBytes - total_uncompressed) {
                return RejectZip(
                    compressed_size > kMaxZipEntryBytes ||
                            compressed_size > kMaxZipArchiveBytes - total_uncompressed
                        ? "entry-too-large"
                        : "entry-attributes",
                    compressed_size > kMaxZipEntryBytes
                        ? ERROR_FILE_TOO_LARGE
                        : ERROR_INVALID_DATA);
            }
            total_uncompressed += compressed_size;
            const std::span<const uint8_t> name_bytes =
                bytes.subspan(name_offset, name_size);
            const std::string name(
                name_bytes.begin(), name_bytes.end());
            if (!IsPortableZipName(name) || !names.emplace(name).second) {
                return RejectZip("entry-name", ERROR_DUP_NAME);
            }

            if (local_offset > directory_offset ||
                directory_offset - local_offset < 30) {
                return RejectZip("local-range");
            }
            uint32_t local_signature = 0;
            uint16_t local_flags = 0;
            uint16_t local_method = 0;
            uint32_t local_crc = 0;
            uint32_t local_compressed_size = 0;
            uint32_t local_uncompressed_size = 0;
            uint16_t local_name_size = 0;
            uint16_t local_extra_size = 0;
            if (!ReadU32(bytes, local_offset, local_signature) ||
                local_signature != kLocalFileHeaderSignature ||
                !ReadU16(bytes, local_offset + 6, local_flags) ||
                !ReadU16(bytes, local_offset + 8, local_method) ||
                !ReadU32(bytes, local_offset + 14, local_crc) ||
                !ReadU32(bytes, local_offset + 18, local_compressed_size) ||
                !ReadU32(bytes, local_offset + 22, local_uncompressed_size) ||
                !ReadU16(bytes, local_offset + 26, local_name_size) ||
                !ReadU16(bytes, local_offset + 28, local_extra_size) ||
                local_flags != flags || local_method != method ||
                local_crc != crc ||
                local_compressed_size != compressed_size ||
                local_uncompressed_size != uncompressed_size ||
                local_name_size != name_size) {
                return RejectZip("local-header");
            }
            const size_t local_name_offset = local_offset + 30;
            const size_t data_offset = local_name_offset +
                gsl::narrow_cast<size_t>(local_name_size) +
                gsl::narrow_cast<size_t>(local_extra_size);
            if (data_offset < local_name_offset ||
                data_offset > directory_offset ||
                compressed_size > directory_offset - data_offset) {
                return RejectZip("local-data-range");
            }
            const auto local_name =
                bytes.subspan(local_name_offset, local_name_size);
            if (!std::ranges::equal(
                    local_name, name,
                    [](uint8_t byte, char ch) noexcept {
                        return byte == gsl::narrow_cast<uint8_t>(ch);
                    })) {
                return RejectZip("local-name");
            }
            const std::span<const uint8_t> entry_data =
                bytes.subspan(data_offset, compressed_size);
            if (Crc32(entry_data) != crc) {
                return RejectZip("crc", ERROR_CRC);
            }
            local_ranges.push_back({
                gsl::narrow_cast<size_t>(local_offset),
                data_offset + gsl::narrow_cast<size_t>(compressed_size)});
            parsed.push_back({
                name, std::vector<uint8_t>(entry_data.begin(), entry_data.end())});
            cursor = next;
        }
        if (cursor != eocd) return RejectZip("directory-length");
        std::ranges::sort(
            local_ranges, {}, &LocalEntryRange::begin);
        size_t local_cursor = 0;
        for (const auto& range : local_ranges) {
            if (range.begin != local_cursor || range.end <= range.begin) {
                return RejectZip("local-entry-layout");
            }
            local_cursor = range.end;
        }
        if (local_cursor != directory_offset) {
            return RejectZip("local-entry-coverage");
        }
        entries = std::move(parsed);
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("ZIP read failed with an allocation error");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("ZIP read failed with an unexpected exception");
        return E_UNEXPECTED;
    }
}

HRESULT WriteZipArchiveFileResult(
    const std::filesystem::path& path, std::span<const ZipArchiveEntry> entries) {
    std::vector<uint8_t> bytes;
    const HRESULT build_result = BuildZipArchive(entries, bytes);
    if (FAILED(build_result)) return build_result;
    return WriteFileAtomicallyResult(path, bytes);
}

HRESULT ReadZipArchiveFileResult(
    const std::filesystem::path& path, std::vector<ZipArchiveEntry>& entries) {
    try {
        std::string bytes;
        const HRESULT read_result =
            ReadFileToUtf8StringResult(path, bytes, kMaxZipArchiveBytes);
        if (FAILED(read_result)) return read_result;
        const std::vector<uint8_t> binary(bytes.begin(), bytes.end());
        return ReadZipArchive(binary, entries);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("ZIP file read failed with an allocation error");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("ZIP file read failed with an unexpected exception");
        return E_UNEXPECTED;
    }
}

} // namespace qiqi
