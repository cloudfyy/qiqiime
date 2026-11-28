// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/config.h"
#include "common/fileutil.h"

namespace qiqi {

class ConversionIndex final {
public:
    HRESULT Open(const std::filesystem::path& path) noexcept;
    HRESULT OpenBundled() noexcept;
    std::wstring Convert(std::wstring_view text, OutputConversion mode) const;

private:
    ReadOnlyFileMapping mapping_;
    size_t s2t_count_ = 0;
    size_t t2s_count_ = 0;
};

} // namespace qiqi
