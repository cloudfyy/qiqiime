// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/zip.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <miniz/miniz.h>
#include <memory>
#include <new>
#include <set>
#include <utility>

namespace qiqi {
namespace {

bool IsPortableZipName(std::string_view name) noexcept {
    if (name.empty() || name.size() >= MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE) {
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

HRESULT RejectZip(
    std::string_view reason,
    HRESULT error = HRESULT_FROM_WIN32(ERROR_INVALID_DATA)) noexcept {
    QIQI_LOG_WARN(
        "ZIP archive rejected (reason={}, hr=0x{:08X})",
        reason, gsl::narrow_cast<uint32_t>(error));
    return error;
}

HRESULT MinizFailure(
    mz_zip_archive& archive, std::string_view operation) noexcept {
    const mz_zip_error error = mz_zip_peek_last_error(&archive);
    HRESULT result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    switch (error) {
    case MZ_ZIP_ALLOC_FAILED:
        result = E_OUTOFMEMORY;
        break;
    case MZ_ZIP_CRC_CHECK_FAILED:
        result = HRESULT_FROM_WIN32(ERROR_CRC);
        break;
    case MZ_ZIP_FILE_TOO_LARGE:
    case MZ_ZIP_ARCHIVE_TOO_LARGE:
        result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        break;
    case MZ_ZIP_TOO_MANY_FILES:
        result = HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES);
        break;
    case MZ_ZIP_UNSUPPORTED_METHOD:
    case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
    case MZ_ZIP_UNSUPPORTED_FEATURE:
    case MZ_ZIP_UNSUPPORTED_MULTIDISK:
    case MZ_ZIP_UNSUPPORTED_CDIR_SIZE:
        result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        break;
    case MZ_ZIP_INVALID_PARAMETER:
        result = E_INVALIDARG;
        break;
    case MZ_ZIP_NO_ERROR:
    case MZ_ZIP_UNDEFINED_ERROR:
    case MZ_ZIP_INTERNAL_ERROR:
    case MZ_ZIP_COMPRESSION_FAILED:
        result = E_FAIL;
        break;
    default:
        break;
    }
    QIQI_LOG_WARN(
        "ZIP {} failed (miniz_error={}, reason={}, hr=0x{:08X})",
        operation, gsl::narrow_cast<unsigned int>(error),
        mz_zip_get_error_string(error), gsl::narrow_cast<uint32_t>(result));
    return result;
}

HRESULT CloseArchive(mz_zip_archive& archive) noexcept {
    if (archive.m_zip_mode == MZ_ZIP_MODE_INVALID) return S_OK;
    return mz_zip_end(&archive)
        ? S_OK
        : MinizFailure(archive, "cleanup");
}

} // namespace

HRESULT BuildZipArchive(
    std::span<const ZipArchiveEntry> entries, std::vector<uint8_t>& output) {
    try {
        if (entries.size() > kMaxZipArchiveEntries) {
            return RejectZip(
                "too-many-entries",
                HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES));
        }
        size_t total_data = 0;
        std::set<std::string_view> names;
        for (const auto& entry : entries) {
            if (!IsPortableZipName(entry.name) ||
                entry.data.size() > kMaxZipEntryBytes) {
                return RejectZip("entry-name-or-size", E_INVALIDARG);
            }
            if (!names.emplace(entry.name).second) {
                return RejectZip(
                    "duplicate-entry", HRESULT_FROM_WIN32(ERROR_DUP_NAME));
            }
            if (entry.data.size() > kMaxZipArchiveBytes - total_data) {
                return RejectZip(
                    "total-entry-size", HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            }
            total_data += entry.data.size();
        }

        mz_zip_archive archive{};
        const auto cleanup = gsl::finally([&]() noexcept {
            std::ignore = CloseArchive(archive);
        });
        if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
            return MinizFailure(archive, "writer initialization");
        }
        for (const auto& entry : entries) {
            if (!mz_zip_writer_add_mem(
                    &archive, entry.name.c_str(), entry.data.data(),
                    entry.data.size(), MZ_DEFAULT_LEVEL)) {
                return MinizFailure(archive, "entry compression");
            }
        }
        void* buffer = nullptr;
        size_t size = 0;
        if (!mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size)) {
            return MinizFailure(archive, "archive finalization");
        }
        const std::unique_ptr<void, decltype(&mz_free)> owned_buffer(buffer, &mz_free);
        if (size > kMaxZipArchiveBytes) {
            return RejectZip(
                "archive-size", HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        }
        const HRESULT close_result = CloseArchive(archive);
        if (FAILED(close_result)) return close_result;
        const std::span bytes(static_cast<const uint8_t*>(buffer), size);
        std::vector<uint8_t> built(bytes.begin(), bytes.end());
        output = std::move(built);
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
            return RejectZip(
                "invalid-size", HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        }
        mz_zip_archive archive{};
        const auto cleanup = gsl::finally([&]() noexcept {
            std::ignore = CloseArchive(archive);
        });
        if (!mz_zip_reader_init_mem(
                &archive, bytes.data(), bytes.size(),
                MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
            return MinizFailure(archive, "reader initialization");
        }
        const mz_uint count = mz_zip_reader_get_num_files(&archive);
        if (count > kMaxZipArchiveEntries) {
            return RejectZip(
                "too-many-entries",
                HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES));
        }

        std::vector<ZipArchiveEntry> parsed;
        parsed.reserve(count);
        std::set<std::string> names;
        size_t total_uncompressed = 0;
        for (mz_uint index = 0; index < count; ++index) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&archive, index, &stat)) {
                return MinizFailure(archive, "entry metadata");
            }
            if (stat.m_is_directory || stat.m_is_encrypted || !stat.m_is_supported) {
                return RejectZip("entry-attributes");
            }
            if (stat.m_uncomp_size > kMaxZipEntryBytes ||
                stat.m_uncomp_size > kMaxZipArchiveBytes - total_uncompressed) {
                return RejectZip(
                    "uncompressed-entry-size",
                    HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
            }
            if (stat.m_comp_size > bytes.size() ||
                (stat.m_comp_size == 0 &&
                 (stat.m_uncomp_size != 0 || stat.m_method != 0)) ||
                (stat.m_method == 0 && stat.m_comp_size != stat.m_uncomp_size)) {
                return RejectZip("entry-data-size");
            }
            const size_t size = gsl::narrow_cast<size_t>(stat.m_uncomp_size);
            total_uncompressed += size;
            const mz_uint name_size =
                mz_zip_reader_get_filename(&archive, index, nullptr, 0);
            if (name_size <= 1) return RejectZip("entry-name");
            std::string name(name_size, '\0');
            if (mz_zip_reader_get_filename(
                    &archive, index, name.data(), name_size) != name_size) {
                return MinizFailure(archive, "entry name");
            }
            name.pop_back();
            if (!IsPortableZipName(name) || !names.emplace(name).second) {
                return RejectZip("entry-name", HRESULT_FROM_WIN32(ERROR_DUP_NAME));
            }
            if (!mz_zip_validate_file(
                    &archive, index, MZ_ZIP_FLAG_VALIDATE_HEADERS_ONLY)) {
                return MinizFailure(archive, "entry headers");
            }
            std::vector<uint8_t> data(size);
            if (!mz_zip_reader_extract_to_mem(
                    &archive, index, data.data(), data.size(), 0)) {
                return MinizFailure(archive, "entry decompression");
            }
            if (data.empty()) {
                if (stat.m_crc32 != 0) {
                    return RejectZip("crc", HRESULT_FROM_WIN32(ERROR_CRC));
                }
                // miniz's normal empty-entry paths skip the local header and CRC.
                std::unique_ptr<
                    mz_zip_reader_extract_iter_state,
                    decltype(&mz_zip_reader_extract_iter_free)> iterator(
                        mz_zip_reader_extract_iter_new(&archive, index, 0),
                        &mz_zip_reader_extract_iter_free);
                if (!iterator) {
                    return MinizFailure(archive, "empty-entry headers");
                }
                if (!mz_zip_reader_extract_iter_free(iterator.release())) {
                    return MinizFailure(archive, "empty-entry validation");
                }
            }
            parsed.push_back({std::move(name), std::move(data)});
        }
        const HRESULT close_result = CloseArchive(archive);
        if (FAILED(close_result)) return close_result;
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
