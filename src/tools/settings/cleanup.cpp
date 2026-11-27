// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/cleanup.h"
#include "common/fileutil.h"
#include "common/logger.h"
#include "common/RAII.h"
#include "common/dictfiles.h"

#include <shlobj.h>
#include <gsl/gsl>

namespace qiqi::settings {
namespace {

constexpr bool Missing(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

void RecordFailure(HRESULT hr, const char* operation, HRESULT& result) noexcept {
    if (FAILED(hr)) {
        std::ignore = log::CheckHResult(hr, operation);
        if (SUCCEEDED(result)) result = hr;
    }
}

HRESULT RemoveSyncData(const std::filesystem::path& directory) {
    ScopedFileHandle folder;
    const HRESULT opened = OpenDataItemNoReparse(directory, true, 0, folder);
    if (opened != S_OK) return opened;
    HRESULT result = S_OK;
    for (const auto* name : dictionary_files::kSyncFiles) {
        RecordFailure(RemoveFileNoReparse(directory / name), "Uninstall::RemoveSyncFile", result);
    }
    // Keep the directory itself and unrecognized files (including backups); do not recurse.
    return result;
}

} // namespace

HRESULT detail::RemoveUserDataAt(const std::filesystem::path& directory, const wchar_t* registry_subkey) {
    if (!directory.is_absolute() || directory.filename() != L"QiQiIME" || !registry_subkey || !*registry_subkey) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "Uninstall::DataLocation");
        return E_INVALIDARG;
    }
    HRESULT result = S_OK;
    ScopedFileHandle folder;
    const HRESULT opened = OpenDataItemNoReparse(directory, true, 0, folder);
    RecordFailure(opened, "Uninstall::OpenDataDirectory", result);
    if (opened == S_OK) {
        for (const auto* name : dictionary_files::kUserFiles) {
            RecordFailure(RemoveFileNoReparse(directory / name), "Uninstall::RemoveDataFile", result);
        }
        for (const auto* name : dictionary_files::kSyncDirectories) {
            RecordFailure(RemoveSyncData(directory / name), "Uninstall::RemoveSyncData", result);
        }
    }
    const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, registry_subkey);
    if (status != ERROR_SUCCESS && !Missing(gsl::narrow_cast<DWORD>(status))) {
        RecordFailure(HRESULT_FROM_WIN32(status), "Uninstall::RemoveConfiguration", result);
    }
    return result;
}

HRESULT RemoveCurrentUserData() {
    PWSTR local_app_data = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local_app_data);
    const ScopedCoTaskMem release(local_app_data);
    if (FAILED(hr)) {
        std::ignore = log::CheckHResult(hr, "Uninstall::LocalAppData");
        return hr;
    }
    return detail::RemoveUserDataAt(std::filesystem::path(local_app_data) / L"QiQiIME", L"Software\\QiQiIME");
}

} // namespace qiqi::settings
