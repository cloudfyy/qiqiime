#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/tip_service.h"
#include "ime/tsf/service_lifecycle.h"

namespace qiqi::test {

void RunTsfServiceLifecycleTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))) && key_sink);
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))) && comp_sink);

    RunTsfDeferredSessionTests(service, mock_tm, key_sink.Get(), comp_sink.Get());
    RunTsfDocumentLifecycleTests(service, mock_tm);
    RunTsfTargetPreparationTests(service, mock_tm);
    TestDeactivationWindowLifecycle();
    TestReentrantCompositionEdits();
    RunTsfServiceCleanupTests(service, mock_tm, key_sink.Get());
    RunTsfNativeLifecycleTests(service, mock_tm, key_sink.Get());
}

} // namespace qiqi::test
