// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/fileutil.h"

#include <vector>

namespace qiqi {

class PinyinIndex final {
public:
    static constexpr size_t kMaxSyllableLength = 6;

    HRESULT Open(const std::filesystem::path& path) noexcept;
    HRESULT OpenBundled() noexcept;
    std::vector<std::wstring> Lookup(std::wstring_view syllable) const;

private:
    ReadOnlyFileMapping mapping_;
};

} // namespace qiqi
