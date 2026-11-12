// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

namespace qiqi::test {

void RunCommonTests() {
    using namespace common_cases;
    TestEtwWriteDiagnostics();
    TestEtwRegistrationLifetime();
    TestEtwLogger();
    TestFileUtilityBoundaries();
    TestVersionMechanism();
    TestConfigFieldProjections();
    TestDictionaryLoadPlans();
    TestIniConfiguration();
    TestScopedHandleTemplate();
    TestStaticSoundWavs();
    TestI18nLocalization();
}

} // namespace qiqi::test
