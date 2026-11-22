// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backupfixture.h"
#include <miniz/miniz.h>

namespace qiqi::dict_manager::tests::backup {

std::string DeflatedZipFixture(bool corrupt_crc) {
    using namespace std::string_view_literals;
    // Python zipfile ZIP_DEFLATED output containing 4096 copies of 'a'.
    constexpr std::string_view bytes =
        "\x50\x4b\x03\x04\x14\x00\x00\x00\x08\x00\x00\x00\x21\x5c\x73\xdc"
        "\x99\x9c\x16\x00\x00\x00\x00\x10\x00\x00\x0b\x00\x00\x00\x70\x61"
        "\x79\x6c\x6f\x61\x64\x2e\x74\x78\x74\xed\xc1\x01\x0d\x00\x00\x00"
        "\xc2\xa0\xac\xef\x5f\xc2\x1e\x0e\x28\x00\x00\x00\xe0\xdd\x00\x50"
        "\x4b\x01\x02\x14\x00\x14\x00\x00\x00\x08\x00\x00\x00\x21\x5c\x73"
        "\xdc\x99\x9c\x16\x00\x00\x00\x00\x10\x00\x00\x0b\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x00\x70\x61\x79"
        "\x6c\x6f\x61\x64\x2e\x74\x78\x74\x50\x4b\x05\x06\x00\x00\x00\x00"
        "\x01\x00\x01\x00\x39\x00\x00\x00\x3f\x00\x00\x00\x00\x00"sv;
    std::string archive(bytes);
    if (corrupt_crc) {
        const auto central = archive.find(std::string("\x50\x4b\x01\x02", 4));
        QIQI_TEST_CHECK(central != std::string::npos);
        gsl::at(archive, 14) ^= 1;
        gsl::at(archive, central + 16) ^= 1;
    }
    return archive;
}

void TestZipCompression() {
    std::vector<ZipArchiveEntry> decoded;
    const auto fixture = DeflatedZipFixture();
    QIQI_TEST_CHECK(ReadZipArchive(codec::AsByteSpan(fixture), decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == 1 && decoded.front().name == "payload.txt" &&
        decoded.front().data == std::vector<uint8_t>(4096, 'a'));
    QIQI_TEST_CHECK(
        ReadZipArchive(codec::AsByteSpan(DeflatedZipFixture(true)), decoded) ==
        HRESULT_FROM_WIN32(ERROR_CRC));
    QIQI_TEST_CHECK(decoded.size() == 1 &&
        decoded.front().data == std::vector<uint8_t>(4096, 'a'));

    std::string oversized = fixture;
    const auto central = oversized.find(std::string("\x50\x4b\x01\x02", 4));
    QIQI_TEST_CHECK(central != std::string::npos);
    const auto excessive_size = gsl::narrow_cast<uint32_t>(kMaxZipEntryBytes + 1);
    for (size_t index = 0; index < 4; ++index) {
        const auto byte = gsl::narrow_cast<char>((excessive_size >> (index * 8)) & 0xffu);
        gsl::at(oversized, 22 + index) = byte;
        gsl::at(oversized, central + 24 + index) = byte;
    }
    QIQI_TEST_CHECK(ReadZipArchive(codec::AsByteSpan(oversized), decoded) ==
        HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    QIQI_TEST_CHECK(decoded.size() == 1 &&
        decoded.front().data == std::vector<uint8_t>(4096, 'a'));

    const std::array entries{
        ZipArchiveEntry{"payload.txt", std::vector<uint8_t>(16 * 1024, 'a')},
        ZipArchiveEntry{"empty.txt", {}}};
    std::vector<uint8_t> compressed;
    QIQI_TEST_CHECK(BuildZipArchive(entries, compressed) == S_OK);
    QIQI_TEST_CHECK(compressed.size() < entries.front().data.size() / 4);
    {
        mz_zip_archive archive{};
        QIQI_TEST_CHECK(mz_zip_reader_init_mem(
            &archive, compressed.data(), compressed.size(), 0));
        const auto cleanup = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK_CLEANUP(mz_zip_reader_end(&archive));
        });
        mz_zip_archive_file_stat stat{};
        QIQI_TEST_CHECK(mz_zip_reader_file_stat(&archive, 0, &stat));
        QIQI_TEST_CHECK(stat.m_method == MZ_DEFLATED &&
            stat.m_uncomp_size == entries.front().data.size());
    }
    QIQI_TEST_CHECK(ReadZipArchive(compressed, decoded) == S_OK);
    QIQI_TEST_CHECK(decoded.size() == entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        QIQI_TEST_CHECK(gsl::at(decoded, index).name == gsl::at(entries, index).name &&
            gsl::at(decoded, index).data == gsl::at(entries, index).data);
    }

    std::vector<uint8_t> empty_archive;
    QIQI_TEST_CHECK(BuildZipArchive(std::span(entries).last(1), empty_archive) == S_OK);
    std::string invalid_empty(empty_archive.begin(), empty_archive.end());
    const auto empty_central = invalid_empty.find(std::string("\x50\x4b\x01\x02", 4));
    QIQI_TEST_CHECK(empty_central != std::string::npos);
    gsl::at(invalid_empty, 14) = 1;
    gsl::at(invalid_empty, empty_central + 16) = 1;
    QIQI_TEST_CHECK(ReadZipArchive(codec::AsByteSpan(invalid_empty), decoded) ==
        HRESULT_FROM_WIN32(ERROR_CRC));
    QIQI_TEST_CHECK(decoded.size() == entries.size() && decoded.back().data.empty());
}

} // namespace qiqi::dict_manager::tests::backup
