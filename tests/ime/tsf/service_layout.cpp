#include "common/test_support.h"
#include "ime/tsf/mocks.h"
#include "ime/tsf/service_lifecycle.h"
#include "ime/tsf/tip_service.h"
#include <array>

namespace qiqi::test {

void RunTsfLayoutQueryTests(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm) {
    enum class Boundary { View, Range, Extent };
    const auto objects_before = g_com_object_count.load();
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));
    size_t cases = 0;
    for (const bool notified : {false, true}) {
        for (const auto boundary : {Boundary::View, Boundary::Range, Boundary::Extent}) {
            if (notified && boundary == Boundary::View) continue;
            for (const bool reactivate : {false, true}) {
                const auto context = MakeComObject<MockTfContext>();
                const auto replacement = MakeComObject<MockTfContext>();
                replacement->view_->text_ext_ = {300, 200, 340, 224};
                BOOL eaten = FALSE;
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
                auto* const ui = dynamic_cast<CandidateUI*>(mock_tm->last_ui_element_.Get());
                QIQI_TEST_CHECK(ui);
                const auto composition = context->last_composition_;
                const auto queried_view = notified ? MakeComObject<MockTfContextView>() : context->view_;
                queried_view->text_ext_ = {700, 500, 740, 524};
                const auto view_calls_before = context->get_active_view_calls_;
                const auto extent_calls_before = queried_view->get_text_ext_calls_;
                unsigned int callbacks = 0;
                RECT replacement_rect{};
                const auto callback = [&] {
                    if (callbacks != 0) return;
                    ++callbacks;
                    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                    if (reactivate) {
                        QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
                        QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && eaten);
                        QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &replacement_rect));
                    }
                };
                const auto clear_callbacks = gsl::finally([&]() noexcept {
                    context->get_active_view_observer_ = {};
                    composition->get_range_callback_ = {};
                    queried_view->get_text_ext_callback_ = {};
                });
                if (boundary == Boundary::View) {
                    context->get_active_view_observer_ = [&](ITfContextView**) { callback(); };
                } else if (boundary == Boundary::Range) {
                    composition->get_range_callback_ = callback;
                } else {
                    queried_view->get_text_ext_callback_ = callback;
                }
                QIQI_TEST_CHECK(service->OnLayoutChange(
                    context.Get(), TF_LC_CHANGE, notified ? queried_view.Get() : nullptr) == S_OK);
                context->get_active_view_observer_ = {};
                composition->get_range_callback_ = {};
                queried_view->get_text_ext_callback_ = {};
                QIQI_TEST_CHECK(callbacks == 1 && composition->ended_ && context->range_->text_ == L"a" &&
                                context->get_active_view_calls_ == view_calls_before + (notified ? 0u : 1u) &&
                                queried_view->get_text_ext_calls_ ==
                                    extent_calls_before + (boundary == Boundary::Extent ? 1u : 0u));
                if (reactivate) {
                    RECT after{};
                    QIQI_TEST_CHECK(::GetWindowRect(ui->WindowHandle(), &after) &&
                                    ::EqualRect(&after, &replacement_rect) && ::IsWindowVisible(ui->WindowHandle()));
                    QIQI_TEST_CHECK(replacement->range_->text_ == L"c" && !replacement->last_composition_->ended_);
                    QIQI_TEST_CHECK(service->OnKeyDown(replacement.Get(), 'D', 0, &eaten) == S_OK && eaten &&
                                    replacement->range_->text_ == L"cd");
                } else {
                    QIQI_TEST_CHECK(!ui->WindowHandle() && ui->UiElementId() == TF_INVALID_UIELEMENTID &&
                                    service->OnKeyDown(replacement.Get(), 'C', 0, &eaten) == S_OK && !eaten &&
                                    replacement->range_->text_.empty());
                }
                QIQI_TEST_CHECK(service->Deactivate() == S_OK);
                QIQI_TEST_CHECK(service->Activate(mock_tm.get(), 1) == S_OK);
                ++cases;
            }
        }
    }
    QIQI_TEST_CHECK(cases == 10 && g_com_object_count.load() == objects_before);
}

} // namespace qiqi::test
