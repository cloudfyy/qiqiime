// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/fileutil.h"

namespace qiqi {

class FullCodeIndex final {
public:
    HRESULT Open(const std::filesystem::path& path) noexcept;
    HRESULT OpenBundled() noexcept;
    uint32_t CharacterCode(std::wstring_view character) const noexcept;

private:
    ReadOnlyFileMapping mapping_;
};

} // namespace qiqi
