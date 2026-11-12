// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"
#include "ime/tsf/test_trace.h"

namespace qiqi::test {

void RunTsfServiceLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))) && key_sink);
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))) && comp_sink);

    RunTsfTestStage("lifecycle.deferred", [&] { RunTsfDeferredSessionTests(service, mock_tm, key_sink.Get(), comp_sink.Get()); });
    RunTsfTestStage("lifecycle.write", [&] { RunTsfWriteRequestTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.edit-action", RunTsfEditActionTests);
    RunTsfTestStage("lifecycle.presentation-sync", [&] { RunTsfPresentationSyncTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.document", [&] { RunTsfDocumentLifecycleTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.preparation", [&] { RunTsfTargetPreparationTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.candidates", [&] { RunTsfCandidatePresentationTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.layout-query", [&] { RunTsfLayoutQueryTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.layout-session", [&] { RunTsfLayoutSessionTests(service, mock_tm); });
    RunTsfTestStage("lifecycle.window", TestDeactivationWindowLifecycle);
    RunTsfTestStage("lifecycle.edits", TestReentrantCompositionEdits);
    RunTsfTestStage("lifecycle.cleanup", [&] { RunTsfServiceCleanupTests(service, mock_tm, key_sink.Get()); });
    RunTsfTestStage("lifecycle.native", [&] { RunTsfNativeLifecycleTests(service, mock_tm, key_sink.Get()); });
}

} // namespace qiqi::test
