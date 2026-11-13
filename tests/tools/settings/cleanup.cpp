// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"
#include "tools/settings/cleanup.h"

#include <winioctl.h>
#include <algorithm>
#include <array>
#include <filesystem>

namespace qiqi::test {
namespace {

void CreateJunction(const std::filesystem::path& link, const std::filesystem::path& target) {
    QIQI_TEST_CHECK(std::filesystem::create_directory(link));
    const ScopedFileHandle directory(::CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    QIQI_TEST_CHECK(directory);
    struct MountPoint {
        DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        WORD data_length = 0;
        WORD reserved = 0;
        WORD substitute_offset = 0;
        WORD substitute_length = 0;
        WORD print_offset = 0;
        WORD print_length = 0;
        std::array<wchar_t, 2048> names{};
    } buffer;
    const auto substitute = L"\\??\\" + target.wstring();
    QIQI_TEST_CHECK(substitute.size() + 1 < buffer.names.size());
    std::copy(substitute.begin(), substitute.end(), buffer.names.begin());
    buffer.substitute_length = gsl::narrow<WORD>(substitute.size() * sizeof(wchar_t));
    buffer.print_offset = gsl::narrow<WORD>(buffer.substitute_length + sizeof(wchar_t));
    buffer.data_length = gsl::narrow<WORD>(gsl::narrow_cast<size_t>(buffer.print_offset) + 8 + sizeof(wchar_t));
    DWORD returned = 0;
    QIQI_TEST_CHECK(::DeviceIoControl(directory.Get(), FSCTL_SET_REPARSE_POINT, &buffer,
        DWORD{buffer.data_length} + 8, nullptr, 0, &returned, nullptr));
}

} // namespace

void RunSettingsCleanupTests() {
    const auto base = std::filesystem::temp_directory_path() /
        (L"QiQiCleanupTest_" + std::to_wstring(::GetCurrentProcessId()) + L"_" +
         std::to_wstring(::GetTickCount64()));
    const auto directory = base / L"QiQiIME";
    const auto outside = base / L"custom";
    const auto subkey = L"Software\\QiQiIME_CleanupTest_" + std::to_wstring(::GetCurrentProcessId());
    QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
    QIQI_TEST_CHECK(std::filesystem::create_directory(outside));
    const auto cleanup = gsl::finally([&]() noexcept {
        const auto status = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
        std::error_code error;
        std::filesystem::remove_all(base, error);
        QIQI_TEST_CHECK(!error);
    });
    const auto write = [](const std::filesystem::path& path) {
        constexpr std::array<uint8_t, 4> bytes{'d', 'a', 't', 'a'};
        QIQI_TEST_CHECK(WriteFileAtomicallyResult(path, bytes) == S_OK);
    };
    for (const auto* name : {L"user_dict.txt", L"user_dict.qdict", L"phrase_freq.qfreq",
                            L"custom.txt", L"export.txt", L"backup.qbak"}) {
        write(directory / name);
    }
    write(outside / L"user_dict.txt");
    for (const auto* name : {L"user_dict.txt.sync", L"phrase_freq.qfreq.sync"}) {
        const auto sync = directory / name;
        QIQI_TEST_CHECK(std::filesystem::create_directory(sync));
        for (const auto* file : {L"snapshot.txt", L"snapshot.qdict", L"snapshot.qfreq",
                                L"source.meta", L"journal.bin", L"writer.lock", L"backup.qbak"}) {
            write(sync / file);
        }
    }
    ScopedRegKey key;
    QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
        KEY_WRITE, nullptr, key.Put(), nullptr) == ERROR_SUCCESS);
    key.Reset();
    {
        const ScopedFileHandle locked(::CreateFileW((directory / L"user_dict.txt").c_str(),
            GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(directory, subkey.c_str()) ==
            HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(std::filesystem::exists(directory / L"user_dict.txt"));
    }
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(directory, subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(directory, subkey.c_str()) == S_OK);
    for (const auto* name : {L"user_dict.txt", L"user_dict.qdict", L"phrase_freq.qfreq",
                            L"user_dict.txt.sync\\snapshot.txt", L"user_dict.txt.sync\\snapshot.qdict",
                            L"phrase_freq.qfreq.sync\\snapshot.qfreq",
                            L"phrase_freq.qfreq.sync\\journal.bin", L"user_dict.txt.sync\\source.meta",
                            L"user_dict.txt.sync\\writer.lock"}) {
        QIQI_TEST_CHECK(!std::filesystem::exists(directory / name));
    }
    QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, key.Put()) == ERROR_FILE_NOT_FOUND);
    for (const auto* name : {L"custom.txt", L"export.txt", L"backup.qbak",
                            L"user_dict.txt.sync\\backup.qbak", L"phrase_freq.qfreq.sync\\backup.qbak"}) {
        std::string contents;
        QIQI_TEST_CHECK(ReadFileToUtf8String(directory / name, contents) && contents == "data");
    }
    const auto junction = directory / L"user_dict.txt.sync";
    std::error_code error;
    std::filesystem::remove_all(junction, error);
    QIQI_TEST_CHECK(!error);
    CreateJunction(junction, outside);
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(directory, subkey.c_str()) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(::RemoveDirectoryW(junction.c_str()));
    const auto root_link = base / L"linked" / L"QiQiIME";
    QIQI_TEST_CHECK(std::filesystem::create_directory(root_link.parent_path()));
    CreateJunction(root_link, outside);
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(root_link, subkey.c_str()) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(::RemoveDirectoryW(root_link.c_str()));
    std::string contents;
    QIQI_TEST_CHECK(ReadFileToUtf8String(outside / L"user_dict.txt", contents) && contents == "data");
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(base / L"missing" / L"QiQiIME", subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(settings::detail::RemoveUserDataAt(base, subkey.c_str()) == E_INVALIDARG);
}

} // namespace qiqi::test
