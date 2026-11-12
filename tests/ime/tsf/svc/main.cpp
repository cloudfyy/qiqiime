// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/svc/test.h"

namespace qiqi::test {

void RunTsfServiceTests() {
    using namespace service_cases;
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    RunTsfTestStage("service.shift", TestShiftTapTracking);
    RunTsfTestStage("service.key-flow", TestQiQiTextServiceLifecycleAndKeyFlow);
    RunTsfTestStage("service.focus-performance", TestFocusPerformanceLogs);
}

} // namespace qiqi::test
