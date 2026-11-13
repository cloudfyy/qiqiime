// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <filesystem>

namespace qiqi::settings {

HRESULT RemoveCurrentUserData();

namespace detail {
HRESULT RemoveUserDataAt(const std::filesystem::path& directory, const wchar_t* registry_subkey);
}

} // namespace qiqi::settings
