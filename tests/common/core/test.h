// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/support.h"
#include "common/dictcodec.h"
#include "common/comobj.h"
#include "common/config.h"
#include "common/fileutil.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/etwreg.h"
#include "common/etwwrite.h"
#include "common/RAII.h"
#include "common/sound.h"
#include "common/version.h"
#include "common/uiutil.h"

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
void TestZipArchives();
void TestVersionMechanism();
void TestConfigFieldProjections();
void TestConfigJson();
void TestDictionaryLoadPlans();
void TestIniConfiguration();
void TestScopedHandleTemplate();
void TestStaticSoundWavs();
void TestI18nLocalization();

} // namespace qiqi::test::common_cases
