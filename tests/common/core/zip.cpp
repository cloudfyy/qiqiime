// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"
#include "common/zip.h"

#include <miniz/miniz.h>
#include <optional>
#include <source_location>

namespace qiqi::test::common_cases {
namespace {

std::string UncheckedZipFixture(std::span<const ZipArchiveEntry> entries) {
    mz_zip_archive archive{};
    QIQI_TEST_CHECK(mz_zip_writer_init_heap(&archive, 0, 0));
    const auto cleanup = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(mz_zip_writer_end(&archive));
    });
    for (const auto& entry : entries) {
        QIQI_TEST_CHECK(mz_zip_writer_add_mem(
            &archive, entry.name.c_str(), entry.data.data(),
            entry.data.size(), MZ_DEFAULT_LEVEL));
    }
    void* buffer = nullptr;
    size_t size = 0;
    QIQI_TEST_CHECK(mz_zip_writer_finalize_heap_archive(&archive, &buffer, &size));
    const std::unique_ptr<void, decltype(&mz_free)> owned_buffer(buffer, &mz_free);
    return {static_cast<const char*>(buffer), size};
}

struct ZipEntryOffsets {
    size_t local;
    size_t central;
    size_t data;
};

ZipEntryOffsets FixtureOffsets(std::string_view bytes, mz_uint index) {
    mz_zip_archive archive{};
    QIQI_TEST_CHECK(mz_zip_reader_init_mem(&archive, bytes.data(), bytes.size(), 0));
    const auto cleanup = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(mz_zip_reader_end(&archive));
    });
    mz_zip_archive_file_stat stat{};
    QIQI_TEST_CHECK(mz_zip_reader_file_stat(&archive, index, &stat));
    const mz_uint name_size = mz_zip_reader_get_filename(&archive, index, nullptr, 0);
    QIQI_TEST_CHECK(name_size > 1);
    const auto local = gsl::narrow_cast<size_t>(stat.m_local_header_ofs);
    // The fixture writer supplies no local extra fields.
    return {
        local,
        gsl::narrow_cast<size_t>(
            archive.m_central_directory_file_ofs + stat.m_central_dir_ofs),
        local + 30 + name_size - 1};
}

void ExpectReadFailure(
    std::span<const uint8_t> bytes,
    std::optional<HRESULT> expected = std::nullopt,
    const std::source_location location = std::source_location::current()) {
    const ZipArchiveEntry original{"unchanged.txt", {'o', 'l', 'd'}};
    std::vector<ZipArchiveEntry> output{original};
    const HRESULT actual = ReadZipArchive(bytes, output);
    const bool correct_error = FAILED(actual) && (!expected || actual == *expected);
    if (!correct_error) {
        std::fprintf(stderr, "[FAIL] %s:%u: ZIP read returned 0x%08X\n",
            location.file_name(), gsl::narrow_cast<unsigned int>(location.line()),
            gsl::narrow_cast<unsigned int>(actual));
        if (expected) {
            std::fprintf(stderr, "[FAIL] Expected 0x%08X\n",
                gsl::narrow_cast<unsigned int>(*expected));
        }
    }
    QIQI_TEST_CHECK(correct_error);
    QIQI_TEST_CHECK(output.size() == 1 &&
        output.front().name == original.name && output.front().data == original.data);
}

void ExpectBuildFailure(
    std::span<const ZipArchiveEntry> entries, HRESULT expected,
    const std::source_location location = std::source_location::current()) {
    const std::vector<uint8_t> original{'o', 'l', 'd'};
    auto output = original;
    const HRESULT actual = BuildZipArchive(entries, output);
    if (actual != expected) {
        std::fprintf(stderr, "[FAIL] %s:%u: ZIP build returned 0x%08X (expected 0x%08X)\n",
            location.file_name(), gsl::narrow_cast<unsigned int>(location.line()),
            gsl::narrow_cast<unsigned int>(actual),
            gsl::narrow_cast<unsigned int>(expected));
    }
    QIQI_TEST_CHECK(actual == expected);
    QIQI_TEST_CHECK(output == original);
}

void TestEmptyAndMalformedZipEntries() {
    const std::array empty{ZipArchiveEntry{"empty.txt", {}}};
    const auto valid_empty = UncheckedZipFixture(empty);
    const auto offsets = FixtureOffsets(valid_empty, 0);
    std::vector<ZipArchiveEntry> decoded;
    QIQI_TEST_CHECK(ReadZipArchive(codec::AsByteSpan(valid_empty), decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == 1 && decoded.front().data.empty());

    auto invalid_header = valid_empty;
    gsl::at(invalid_header, offsets.local) ^= 1;
    ExpectReadFailure(
        codec::AsByteSpan(invalid_header), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    auto missing_deflate_stream = valid_empty;
    gsl::at(missing_deflate_stream, offsets.local + 8) = MZ_DEFLATED;
    gsl::at(missing_deflate_stream, offsets.central + 10) = MZ_DEFLATED;
    ExpectReadFailure(codec::AsByteSpan(missing_deflate_stream),
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA));

    const std::array valid{
        ZipArchiveEntry{"first.txt", {'g', 'o', 'o', 'd'}},
        ZipArchiveEntry{"later.txt", std::vector<uint8_t>(4096, 'a')}};
    auto damaged_stream = UncheckedZipFixture(valid);
    const auto later = FixtureOffsets(damaged_stream, 1);
    // Deflate block type 3 is reserved, so the second entry cannot be decoded.
    auto& block = gsl::at(damaged_stream, later.data);
    block = gsl::narrow_cast<char>((gsl::narrow_cast<uint8_t>(block) & 0xf9u) | 0x06u);
    ExpectReadFailure(codec::AsByteSpan(damaged_stream));

    const auto single_entry = UncheckedZipFixture(std::span(valid).first(1));
    const auto first = FixtureOffsets(single_entry, 0);
    auto embedded_nul = single_entry;
    gsl::at(embedded_nul, first.local + 30) = '\0';
    gsl::at(embedded_nul, first.central + 46) = '\0';
    ExpectReadFailure(
        codec::AsByteSpan(embedded_nul), HRESULT_FROM_WIN32(ERROR_DUP_NAME));

    auto encrypted = single_entry;
    gsl::at(encrypted, first.local + 6) |= 1;
    gsl::at(encrypted, first.central + 8) |= 1;
    ExpectReadFailure(
        codec::AsByteSpan(encrypted), HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
}

void TestZipNamesAndCounts() {
    const std::array longest_name{
        ZipArchiveEntry{
            std::string(MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1, 'a'),
            {'d', 'a', 't', 'a'}}};
    std::vector<uint8_t> encoded;
    std::vector<ZipArchiveEntry> decoded;
    QIQI_TEST_CHECK(BuildZipArchive(longest_name, encoded) == S_OK);
    QIQI_TEST_CHECK(ReadZipArchive(encoded, decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == 1 &&
        decoded.front().name == longest_name.front().name &&
        decoded.front().data == longest_name.front().data);

    auto truncated_name = longest_name;
    truncated_name.front().name.push_back('a');
    ExpectBuildFailure(truncated_name, E_INVALIDARG);
    ExpectReadFailure(codec::AsByteSpan(UncheckedZipFixture(truncated_name)),
        HRESULT_FROM_WIN32(ERROR_DUP_NAME));

    const std::array duplicates{
        ZipArchiveEntry{"same.txt", {'f', 'i', 'r', 's', 't'}},
        ZipArchiveEntry{"same.txt", {'l', 'a', 's', 't'}}};
    ExpectBuildFailure(duplicates, HRESULT_FROM_WIN32(ERROR_DUP_NAME));
    ExpectReadFailure(codec::AsByteSpan(UncheckedZipFixture(duplicates)),
        HRESULT_FROM_WIN32(ERROR_DUP_NAME));

    const std::array embedded_nul{
        ZipArchiveEntry{std::string("safe\0.txt", 9), {'x'}}};
    ExpectBuildFailure(embedded_nul, E_INVALIDARG);

    std::vector<ZipArchiveEntry> entries;
    for (size_t index = 0; index < kMaxZipArchiveEntries; ++index) {
        entries.push_back({
            "entry-" + std::to_string(index) + ".txt",
            {'d', 'a', 't', 'a'}});
    }
    QIQI_TEST_CHECK(BuildZipArchive(entries, encoded) == S_OK);
    QIQI_TEST_CHECK(ReadZipArchive(encoded, decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == entries.size() &&
        decoded.back().name == entries.back().name &&
        decoded.back().data == entries.back().data);
    entries.push_back({"one-too-many.txt", {'x'}});
    ExpectBuildFailure(entries, HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES));
    ExpectReadFailure(codec::AsByteSpan(UncheckedZipFixture(entries)),
        HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES));
}

void TestZipExpandedSizeLimits() {
    std::vector<ZipArchiveEntry> entries{
        {"first.bin", std::vector<uint8_t>(kMaxZipEntryBytes + 1, 'a')}};
    ExpectBuildFailure(entries, E_INVALIDARG);
    entries.front().data.pop_back();
    std::vector<uint8_t> encoded;
    std::vector<ZipArchiveEntry> decoded;
    QIQI_TEST_CHECK(BuildZipArchive(entries, encoded) == S_OK);
    QIQI_TEST_CHECK(ReadZipArchive(encoded, decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == 1 &&
        decoded.front().data == entries.front().data);
    decoded.clear();
    entries.push_back({"second.bin", std::vector<uint8_t>(kMaxZipEntryBytes, 'b')});
    QIQI_TEST_CHECK(BuildZipArchive(entries, encoded) == S_OK);
    QIQI_TEST_CHECK(ReadZipArchive(encoded, decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == 2 &&
        decoded.front().data == entries.front().data &&
        decoded.back().data == entries.back().data);
    decoded.clear();
    entries.push_back({"one-too-many.bin", {'x'}});
    ExpectBuildFailure(entries, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    const auto oversized = UncheckedZipFixture(entries);
    QIQI_TEST_CHECK(oversized.size() < kMaxZipArchiveBytes);
    ExpectReadFailure(codec::AsByteSpan(oversized),
        HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    entries.clear();

    const std::vector<uint8_t> encoded_too_large(kMaxZipArchiveBytes + 1);
    ExpectReadFailure(
        encoded_too_large, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
}

} // namespace

void TestZipArchives() {
    TestEmptyAndMalformedZipEntries();
    TestZipNamesAndCounts();
    TestZipExpandedSizeLimits();
}

} // namespace qiqi::test::common_cases
