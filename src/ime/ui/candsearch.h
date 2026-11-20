// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <ctffunc.h>
#include <memory>
#include <string>
#include <vector>

namespace qiqi {

[[nodiscard]] HRESULT CreateSearchCandidateList(
    std::vector<std::wstring> candidates,
    ITfCandidateList** output) noexcept;

} // namespace qiqi
