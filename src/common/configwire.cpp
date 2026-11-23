// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configwire.h"
#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/logger.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <utility>

namespace qiqi::config {
namespace {

constexpr uint32_t kConfigTransportMagic = 0x47464351u;
constexpr uint32_t kConfigTransportVersion = 1;
constexpr uint32_t kSnapshotKind = 1;
constexpr uint32_t kSaveRequestKind = 2;
constexpr uint32_t kSyncDirectoryRequestKind = 3;
constexpr size_t kHeaderBytes = 4 * 5 + 8;
constexpr size_t kChecksumBytes = 8;
constexpr uint32_t kFieldCount =
    gsl::narrow_cast<uint32_t>(std::tuple_size_v<std::remove_cvref_t<decltype(kConfigFields)>>);

HRESULT Checked(std::string_view operation, auto action) noexcept {
    try {
        const HRESULT result = action();
        if (FAILED(result)) {
            QIQI_LOG_WARN(
                "Config transport {} rejected (hr=0x{:08X})",
                operation,
                gsl::narrow_cast<unsigned long>(result));
        }
        return result;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
}

void AppendHeader(
    std::vector<uint8_t>& output,
    uint32_t kind,
    uint64_t revision,
    uint32_t status = 0) {
    codec::AppendLe32(output, kConfigTransportMagic);
    codec::AppendLe32(output, kConfigTransportVersion);
    codec::AppendLe32(output, kind);
    codec::AppendLe32(output, status);
    codec::AppendLe64(output, revision);
    codec::AppendLe32(output, kFieldCount);
}

HRESULT AppendChecksum(std::vector<uint8_t>& output) noexcept {
    try {
        if (output.size() > kMaxConfigTransportBytes - kChecksumBytes) {
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        }
        codec::AppendLe64(output, codec::ComputeFnv1a64(output));
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT ReadHeader(
    codec::BinarySpanReader& reader,
    uint32_t expected_kind,
    uint64_t& revision,
    uint32_t& status) noexcept {
    if (reader.ReadLe32() != kConfigTransportMagic) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (reader.ReadLe32() != kConfigTransportVersion) {
        return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    }
    if (reader.ReadLe32() != expected_kind) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    status = reader.ReadLe32();
    revision = reader.ReadLe64();
    if (reader.ReadLe32() != kFieldCount) {
        return HRESULT_FROM_WIN32(ERROR_UNKNOWN_REVISION);
    }
    return reader.Valid() ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

HRESULT ValidateChecksum(
    std::span<const uint8_t> bytes,
    std::span<const uint8_t>& payload) noexcept {
    if (bytes.size() < kHeaderBytes + kChecksumBytes ||
        bytes.size() > kMaxConfigTransportBytes) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    payload = bytes.first(bytes.size() - kChecksumBytes);
    codec::BinarySpanReader checksum(bytes.last(kChecksumBytes));
    return codec::ComputeFnv1a64(payload) == checksum.ReadLe64()
        ? S_OK
        : HRESULT_FROM_WIN32(ERROR_CRC);
}

HRESULT ValidateConfig(const QiQiConfig& value) noexcept {
    bool valid = true;
    ForEachConfigField([&](const auto& field) noexcept {
        const uint64_t raw = field.Value(value);
        valid = valid && raw >= field.minimum && raw <= field.maximum;
    });
    if (!valid ||
        !IsValidCandidateFontFamily(value.candidate_font_family) ||
        value.primary_dict_path.size() > kMaxConfigPathChars ||
        value.user_dict_path.size() > kMaxConfigPathChars ||
        value.primary_dict == PrimaryDict::Custom &&
            value.primary_dict_path.empty() ||
        value.easy_reading &&
            value.ui_text_scale < UiTextScale::Percent150) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    return S_OK;
}

void AppendConfig(
    std::vector<uint8_t>& output,
    const QiQiConfig& value) {
    ForEachConfigField([&](const auto& field) {
        codec::AppendLe64(output, field.Value(value));
    });
    codec::AppendWString(output, value.candidate_font_family);
    codec::AppendWString(output, value.primary_dict_path);
    codec::AppendWString(output, value.user_dict_path);
}

HRESULT ReadConfig(
    codec::BinarySpanReader& reader,
    QiQiConfig& output) noexcept {
    try {
        QiQiConfig value;
        bool valid = true;
        ForEachConfigField([&](const auto& field) noexcept {
            valid = field.Assign(value, reader.ReadLe64()) && valid;
        });
        value.candidate_font_family =
            reader.ReadWString(LF_FACESIZE);
        value.primary_dict_path =
            reader.ReadWString(kMaxConfigPathChars);
        value.user_dict_path =
            reader.ReadWString(kMaxConfigPathChars);
        if (!valid || !reader.Valid()) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const HRESULT validation = ValidateConfig(value);
        if (FAILED(validation)) {
            return validation;
        }
        output = std::move(value);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT ReadWString(
    codec::BinarySpanReader& reader,
    size_t maximum,
    std::wstring& output) noexcept {
    try {
        auto value = reader.ReadWString(maximum);
        if (!reader.Valid()) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        output = std::move(value);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

HRESULT AppendCatalog(
    std::vector<uint8_t>& output,
    const ImeSkinCatalog& catalog) noexcept {
    try {
        const HRESULT validation = ValidateImeSkinCatalog(catalog);
        if (FAILED(validation)) {
            return validation;
        }
        codec::AppendWString(output, catalog.id);
        codec::AppendLe32(
            output,
            gsl::narrow_cast<uint32_t>(catalog.skins.size()));
        for (const auto& skin : catalog.skins) {
            codec::AppendLe32(
                output,
                std::to_underlying(skin.id));
            codec::AppendLe32(output, skin.display_order);
            codec::AppendWString(output, skin.name_zh);
            codec::AppendWString(output, skin.name_en);
            for (const DWORD color : skin.palettes.light.colors) {
                codec::AppendLe32(output, color);
            }
            for (const DWORD color : skin.palettes.dark.colors) {
                codec::AppendLe32(output, color);
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
}

HRESULT ReadCatalog(
    codec::BinarySpanReader& reader,
    ImeSkinCatalog& output) noexcept {
    try {
        ImeSkinCatalog catalog;
        HRESULT result =
            ReadWString(reader, 64, catalog.id);
        if (FAILED(result)) {
            return result;
        }
        const uint32_t count = reader.ReadLe32();
        if (count != GetBuiltinImeSkins().size()) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        catalog.skins.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            ImeSkin skin;
            skin.id = gsl::narrow_cast<ImeSkinId>(
                reader.ReadLe32());
            skin.display_order = reader.ReadLe32();
            result = ReadWString(
                reader, 64, skin.name_zh);
            if (FAILED(result)) {
                return result;
            }
            result = ReadWString(
                reader, 64, skin.name_en);
            if (FAILED(result)) {
                return result;
            }
            for (DWORD& color :
                 skin.palettes.light.colors) {
                color = reader.ReadLe32();
            }
            for (DWORD& color :
                 skin.palettes.dark.colors) {
                color = reader.ReadLe32();
            }
            catalog.skins.push_back(std::move(skin));
        }
        if (!reader.Valid() ||
            FAILED(ValidateImeSkinCatalog(catalog))) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        output = std::move(catalog);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }
}

} // namespace

HRESULT EncodeSnapshot(
    const Snapshot& snapshot,
    std::vector<uint8_t>& output) noexcept {
    return Checked("snapshot encode", [&]() -> HRESULT {
        HRESULT result =
            ValidateConfig(snapshot.value);
        if (FAILED(result)) {
            return result;
        }
        std::vector<uint8_t> bytes;
        AppendHeader(
            bytes,
            kSnapshotKind,
            snapshot.revision,
            gsl::narrow_cast<uint32_t>(
                snapshot.notification_result));
        AppendConfig(bytes, snapshot.value);
        codec::AppendWString(
            bytes,
            snapshot.user_dict_sync_directory
                .native());
        result =
            AppendCatalog(
                bytes,
                snapshot.skin_catalog);
        if (FAILED(result)) {
            return result;
        }
        result = AppendChecksum(bytes);
        if (FAILED(result)) {
            return result;
        }
        output = std::move(bytes);
        return S_OK;
    });
}

HRESULT DecodeSnapshot(
    std::span<const uint8_t> bytes,
    Snapshot& output) noexcept {
    return Checked("snapshot decode", [&]() -> HRESULT {
        std::span<const uint8_t> payload;
        HRESULT result =
            ValidateChecksum(bytes, payload);
        if (FAILED(result)) {
            return result;
        }
        codec::BinarySpanReader reader(payload);
        Snapshot snapshot;
        uint32_t status = 0;
        result = ReadHeader(
            reader,
            kSnapshotKind,
            snapshot.revision,
            status);
        if (FAILED(result)) {
            return result;
        }
        result = ReadConfig(reader, snapshot.value);
        if (FAILED(result)) {
            return result;
        }
        std::wstring sync_directory;
        result = ReadWString(
            reader,
            kMaxConfigPathChars,
            sync_directory);
        snapshot.user_dict_sync_directory =
            std::move(sync_directory);
        if (FAILED(result) ||
            !snapshot.user_dict_sync_directory
                 .empty() &&
                !snapshot.user_dict_sync_directory
                     .is_absolute()) {
            return FAILED(result)
                ? result
                : HRESULT_FROM_WIN32(
                      ERROR_INVALID_DATA);
        }
        result = ReadCatalog(
            reader,
            snapshot.skin_catalog);
        if (FAILED(result) ||
            !reader.Valid() ||
            !reader.Empty()) {
            return FAILED(result)
                ? result
                : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        if (snapshot.revision == 0) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        snapshot.notification_result =
            gsl::narrow_cast<HRESULT>(status);
        output = std::move(snapshot);
        return S_OK;
    });
}

HRESULT EncodeSaveRequest(
    const SaveRequest& request,
    std::vector<uint8_t>& output) noexcept {
    return Checked("save-request encode", [&]() -> HRESULT {
        if (request.expected_revision == 0) {
            return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        }
        HRESULT result =
            ValidateConfig(request.value);
        if (FAILED(result)) {
            return result;
        }
        std::vector<uint8_t> bytes;
        AppendHeader(
            bytes,
            kSaveRequestKind,
            request.expected_revision);
        AppendConfig(bytes, request.value);
        result = AppendChecksum(bytes);
        if (FAILED(result)) {
            return result;
        }
        output = std::move(bytes);
        return S_OK;
    });
}

HRESULT DecodeSaveRequest(
    std::span<const uint8_t> bytes,
    SaveRequest& output) noexcept {
    return Checked("save-request decode", [&]() -> HRESULT {
        std::span<const uint8_t> payload;
        HRESULT result =
            ValidateChecksum(bytes, payload);
        if (FAILED(result)) {
            return result;
        }
        codec::BinarySpanReader reader(payload);
        SaveRequest request;
        uint32_t status = 0;
        result = ReadHeader(
            reader,
            kSaveRequestKind,
            request.expected_revision,
            status);
        if (FAILED(result)) {
            return result;
        }
        result = ReadConfig(reader, request.value);
        if (FAILED(result) ||
            status != 0 ||
            !reader.Valid() ||
            !reader.Empty() ||
            request.expected_revision == 0) {
            return FAILED(result)
                ? result
                : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        output = std::move(request);
        return S_OK;
    });
}

HRESULT EncodeSyncDirectoryRequest(
    const SyncDirectoryRequest& request,
    std::vector<uint8_t>& output) noexcept {
    return Checked("sync-directory encode", [&]() -> HRESULT {
        if (request.expected_revision == 0 ||
            request.directory.wstring().size() >
                kMaxConfigPathChars ||
            !request.directory.empty() &&
                !request.directory.is_absolute()) {
            return E_INVALIDARG;
        }
        std::vector<uint8_t> bytes;
        AppendHeader(
            bytes,
            kSyncDirectoryRequestKind,
            request.expected_revision);
        codec::AppendWString(
            bytes,
            request.directory.native());
        const HRESULT result =
            AppendChecksum(bytes);
        if (FAILED(result)) {
            return result;
        }
        output = std::move(bytes);
        return S_OK;
    });
}

HRESULT DecodeSyncDirectoryRequest(
    std::span<const uint8_t> bytes,
    SyncDirectoryRequest& output) noexcept {
    return Checked("sync-directory decode", [&]() -> HRESULT {
        std::span<const uint8_t> payload;
        HRESULT result =
            ValidateChecksum(bytes, payload);
        if (FAILED(result)) {
            return result;
        }
        codec::BinarySpanReader reader(payload);
        SyncDirectoryRequest request;
        uint32_t status = 0;
        result = ReadHeader(
            reader,
            kSyncDirectoryRequestKind,
            request.expected_revision,
            status);
        if (FAILED(result)) {
            return result;
        }
        std::wstring path;
        result = ReadWString(
            reader,
            kMaxConfigPathChars,
            path);
        request.directory = std::move(path);
        if (FAILED(result) ||
            request.expected_revision == 0 ||
            status != 0 ||
            !request.directory.empty() &&
                !request.directory.is_absolute() ||
            !reader.Valid() ||
            !reader.Empty()) {
            return FAILED(result)
                ? result
                : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        output = std::move(request);
        return S_OK;
    });
}

HRESULT AllocateRpcBuffer(
    std::span<const uint8_t> bytes,
    ULONG& output_size,
    byte*& output) noexcept {
    output_size = 0;
    output = nullptr;
    if (bytes.size() >
        kMaxConfigTransportBytes) {
        return HRESULT_FROM_WIN32(
            ERROR_BUFFER_OVERFLOW);
    }
    if (bytes.empty()) {
        return S_OK;
    }
    byte* allocated =
        static_cast<byte*>(
            ::CoTaskMemAlloc(
                bytes.size()));
    if (!allocated) {
        return E_OUTOFMEMORY;
    }
    std::copy(
        bytes.begin(),
        bytes.end(),
        allocated);
    output_size =
        gsl::narrow_cast<ULONG>(
            bytes.size());
    output = allocated;
    return S_OK;
}

HRESULT ReadRpcBuffer(
    ULONG size,
    const byte* bytes,
    std::vector<uint8_t>& output) noexcept {
    if (size >
        kMaxConfigTransportBytes) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    if (size == 0) {
        if (bytes) {
            return HRESULT_FROM_WIN32(
                ERROR_INVALID_DATA);
        }
        output.clear();
        return S_OK;
    }
    if (!bytes) {
        return E_POINTER;
    }
    try {
        output.assign(
            bytes,
            bytes + size);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}

} // namespace qiqi::config
