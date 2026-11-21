// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/test.h"

namespace qiqi::test::common_cases {
void TestImeSkins();
}
namespace qiqi::test {
void RunSettingsSkinTests();
void RunCandidateSkinTests();
void RunMenuSkinTests();

void RunSkinTests() {
    g_dll_instance = ::GetModuleHandleW(nullptr);
    QIQI_TEST_CHECK(g_dll_instance);
    RunTsfTestStage("skin.catalog", common_cases::TestImeSkins);
    RunTsfTestStage("skin.settings", RunSettingsSkinTests);
    RunTsfTestStage("skin.candidate", RunCandidateSkinTests);
    RunTsfTestStage("skin.menu", RunMenuSkinTests);
    RunTsfTestStage("skin.input-lifecycle", [] {
        service_cases::TestQiQiTextServiceLifecycleAndKeyFlow(false, true);
    });
}

} // namespace qiqi::test
