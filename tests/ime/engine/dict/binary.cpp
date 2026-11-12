// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dict/test.h"

namespace qiqi::test::dict_cases {

void RefreshQdictChecksum(std::vector<uint8_t>& buffer) {
    DictBinaryHeader header{};
    ReadBinaryValue(buffer, 0, header);
    header.payload_checksum = codec::ComputeFnv1a32(
        std::span<const uint8_t>(buffer).subspan(sizeof(DictBinaryHeader)));
    WriteBinaryValue(buffer, 0, header);
}

std::vector<uint8_t> MakeSingleEntryQdictBuffer() {
    DictBinaryHeader header{};
    header.code_count = 1;
    header.word_count = 1;
    header.char_count = 1;
    const DictCodeIndex code_index{PackWubiCode(L"aaaa"), 0, 1};
    const DictWordSlice word_slice{0, 1};
    constexpr wchar_t word = L'词';

    std::vector<uint8_t> buffer(sizeof(header) + sizeof(code_index) + sizeof(word_slice) + sizeof(word));
    size_t offset = 0;
    WriteBinaryValue(buffer, offset, header);
    offset += sizeof(header);
    WriteBinaryValue(buffer, offset, code_index);
    offset += sizeof(code_index);
    WriteBinaryValue(buffer, offset, word_slice);
    offset += sizeof(word_slice);
    WriteBinaryValue(buffer, offset, word);
    RefreshQdictChecksum(buffer);
    return buffer;
}

bool WriteInvalidIndexQdictCache(
    const std::filesystem::path& source_path,
    const std::filesystem::path& cache_path) {
    std::error_code ec;
    const uint64_t source_size = std::filesystem::file_size(source_path, ec);
    if (ec) {
        return false;
    }
    const uint64_t source_mtime = gsl::narrow_cast<uint64_t>(
        std::filesystem::last_write_time(source_path, ec).time_since_epoch().count());
    if (ec) {
        return false;
    }

    auto buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader header{};
    std::memcpy(&header, buffer.data(), sizeof(header));
    header.source_size = source_size;
    header.source_mtime = source_mtime;
    std::memcpy(buffer.data(), &header, sizeof(header));

    DictCodeIndex code_index{};
    ReadBinaryValue(buffer, sizeof(DictBinaryHeader), code_index);
    code_index.first_word_idx = UINT32_MAX;
    WriteBinaryValue(buffer, sizeof(DictBinaryHeader), code_index);
    RefreshQdictChecksum(buffer);

    std::ofstream cache_file(cache_path, std::ios::binary | std::ios::trunc);
    if (!cache_file) {
        return false;
    }
    cache_file.write(
        reinterpret_cast<const char*>(buffer.data()),
        gsl::narrow_cast<std::streamsize>(buffer.size()));
    return cache_file.good();
}

void TestBinaryDictBufferValidation() {
    auto valid_buffer = MakeSingleEntryQdictBuffer();
    const auto valid_dict = WubiMappedDict::LoadFromBinaryBuffer(std::move(valid_buffer));
    QIQI_TEST_CHECK(valid_dict != nullptr && valid_dict->IsValid());
    std::vector<std::wstring> words;
    valid_dict->LookupExact(PackWubiCode(L"aaaa"), words);
    QIQI_TEST_CHECK(words.size() == 1 && words.front() == L"词");

    // Outdated .qdict version (v1) and corrupted payload checksum must be rejected
    auto v1_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader v1_header{};
    std::memcpy(&v1_header, v1_buffer.data(), sizeof(v1_header));
    v1_header.version = 1u;
    std::memcpy(v1_buffer.data(), &v1_header, sizeof(v1_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(v1_buffer)) == nullptr);

    auto bad_checksum_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader bad_checksum_header{};
    std::memcpy(&bad_checksum_header, bad_checksum_buffer.data(), sizeof(bad_checksum_header));
    bad_checksum_header.payload_checksum ^= 1u;
    std::memcpy(bad_checksum_buffer.data(), &bad_checksum_header, sizeof(bad_checksum_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(bad_checksum_buffer)) == nullptr);

    auto trailing_bytes_buffer = MakeSingleEntryQdictBuffer();
    trailing_bytes_buffer.push_back(0xAA);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(trailing_bytes_buffer)) == nullptr);
    WubiMappedDict::InvalidateProcessCacheForPath({});

    auto invalid_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_count_header{};
    std::memcpy(&invalid_count_header, invalid_count_buffer.data(), sizeof(invalid_count_header));
    invalid_count_header.code_count = UINT32_MAX;
    std::memcpy(invalid_count_buffer.data(), &invalid_count_header, sizeof(invalid_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_count_buffer)) == nullptr);

    auto invalid_word_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_word_count_header{};
    std::memcpy(&invalid_word_count_header, invalid_word_count_buffer.data(), sizeof(invalid_word_count_header));
    invalid_word_count_header.word_count = UINT32_MAX;
    std::memcpy(invalid_word_count_buffer.data(), &invalid_word_count_header, sizeof(invalid_word_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_word_count_buffer)) == nullptr);

    auto invalid_char_count_buffer = MakeSingleEntryQdictBuffer();
    DictBinaryHeader invalid_char_count_header{};
    std::memcpy(&invalid_char_count_header, invalid_char_count_buffer.data(), sizeof(invalid_char_count_header));
    invalid_char_count_header.char_count = UINT32_MAX;
    std::memcpy(invalid_char_count_buffer.data(), &invalid_char_count_header, sizeof(invalid_char_count_header));
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_char_count_buffer)) == nullptr);

    auto invalid_index_buffer = MakeSingleEntryQdictBuffer();
    DictCodeIndex invalid_index{};
    ReadBinaryValue(invalid_index_buffer, sizeof(DictBinaryHeader), invalid_index);
    invalid_index.first_word_idx = UINT32_MAX;
    WriteBinaryValue(invalid_index_buffer, sizeof(DictBinaryHeader), invalid_index);
    RefreshQdictChecksum(invalid_index_buffer);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_index_buffer)) == nullptr);

    auto invalid_code_buffer = MakeSingleEntryQdictBuffer();
    DictCodeIndex invalid_code{};
    ReadBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    invalid_code.packed_code = 0xFF000000u;
    WriteBinaryValue(invalid_code_buffer, sizeof(DictBinaryHeader), invalid_code);
    RefreshQdictChecksum(invalid_code_buffer);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_code_buffer)) == nullptr);

    auto invalid_slice_offset_buffer = MakeSingleEntryQdictBuffer();
    DictWordSlice invalid_slice_offset{};
    ReadBinaryValue(
        invalid_slice_offset_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_offset);
    invalid_slice_offset.char_offset = UINT32_MAX;
    WriteBinaryValue(
        invalid_slice_offset_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_offset);
    RefreshQdictChecksum(invalid_slice_offset_buffer);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_slice_offset_buffer)) == nullptr);

    auto invalid_slice_length_buffer = MakeSingleEntryQdictBuffer();
    DictWordSlice invalid_slice_length{};
    ReadBinaryValue(
        invalid_slice_length_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_length);
    invalid_slice_length.char_length = UINT32_MAX;
    WriteBinaryValue(
        invalid_slice_length_buffer,
        sizeof(DictBinaryHeader) + sizeof(DictCodeIndex),
        invalid_slice_length);
    RefreshQdictChecksum(invalid_slice_length_buffer);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromBinaryBuffer(std::move(invalid_slice_length_buffer)) == nullptr);
}

} // namespace qiqi::test::dict_cases
