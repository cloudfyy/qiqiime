#pragma once
#include <windows.h>
#include <msctf.h>
#include <gsl/gsl>

namespace qiqi {
class QiQiTextService;
}

namespace qiqi::test {

class MockTfThreadMgr;

void RunTsfServiceLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfDeferredSessionTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink, gsl::not_null<ITfCompositionSink*> comp_sink);
void RunTsfWriteRequestTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfEditActionTests();
void RunTsfPresentationSyncTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfDocumentLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfTargetPreparationTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfCandidatePresentationTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfLayoutQueryTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void RunTsfLayoutSessionTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);
void TestDeactivationWindowLifecycle();
void TestReentrantCompositionEdits();
void RunTsfServiceCleanupTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink);
void RunTsfNativeLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm,
    gsl::not_null<ITfKeyEventSink*> key_sink);

} // namespace qiqi::test
