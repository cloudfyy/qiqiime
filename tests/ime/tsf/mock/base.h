// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "common/com_object.h"
#include "common/guids.h"
#include "common/logger.h"

namespace qiqi::test {

using Microsoft::WRL::ComPtr;

} // namespace qiqi::test
