// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/sound.h"
#include "common/file_util.h"
#include "ime/tsf/display_attribute.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/svc/lifecycle.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/test_trace.h"

#include <cstdlib>
#include <chrono>
#include <commctrl.h>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <thread>
#include <tuple>
#include <utility>

namespace qiqi::test::service_cases {

void TestModeSwitchCompositionPolicy(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> manager, bool commit);
void TestShiftTapTracking();
void TestFocusPerformanceLogs();
void TestProcessDictCacheLifetime();
void TestDictionarySharingAndLegacyRegistryIgnored();
void TestPreservedKeyRegistrationLifetime(gsl::not_null<QiQiTextService*> service);
void TestQiQiTextServiceLifecycleAndKeyFlow();

} // namespace qiqi::test::service_cases
