// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "broker/runtime.h"

#include <wrl/client.h>

namespace qiqi::broker {

Microsoft::WRL::ComPtr<IClassFactory>
CreateConfigBrokerFactory(
    Runtime& runtime) noexcept;

} // namespace qiqi::broker
