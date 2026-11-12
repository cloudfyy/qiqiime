// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "common/binary_codec.h"
#include "common/com_object.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/etw_registration.h"
#include "common/etw_write.h"
#include "common/scoped_handle.h"
#include "common/sound.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <tuple>
#include <thread>

namespace qiqi::test::common_cases {

void TestEtwWriteDiagnostics();
void TestEtwRegistrationLifetime();
void TestEtwLogger();
void TestFileUtilityBoundaries();
void TestVersionMechanism();
void TestConfigFieldProjections();
void TestDictionaryLoadPlans();
void TestIniConfiguration();
void TestScopedHandleTemplate();
void TestStaticSoundWavs();
void TestI18nLocalization();

} // namespace qiqi::test::common_cases
