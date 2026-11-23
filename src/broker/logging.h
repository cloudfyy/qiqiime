// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include "common/guids.h"

namespace qiqi::broker::diag {

inline constexpr GUID kProviderId =
    kGuidConfigBrokerEtwProvider;

HRESULT Initialize() noexcept;
void Shutdown() noexcept;

} // namespace qiqi::broker::diag
