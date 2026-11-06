#include "common/test_support.h"
#include "ime/comp/layout.h"
#include "ime/comp/tests.h"
#include "ime/tsf/mocks.h"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace qiqi::test {
namespace {

bool SameRect(const RECT& left, const RECT& right) noexcept {
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

void TestLayoutPositionBranches() {
    enum class Fallback { Failure, Success, FailureWithOutput };
    enum class Event { Fallback, Move };
    constexpr std::array rectangles{RECT{}, RECT{100, 110, 140, 134}, RECT{300, 310, 280, 290}};
    constexpr RECT fallback_rect{400, 410, 402, 430};
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool current : {false, true}) {
        for (const bool got_position : {false, true}) {
            for (const bool had_position : {false, true}) {
                for (const auto fallback : {Fallback::Failure, Fallback::Success, Fallback::FailureWithOutput}) {
                    for (const auto& rect : rectangles) {
                        const auto range = MakeComObject<MockTfRange>();
                        const CompositionLayoutQuery query{
                            .range = range, .rect = rect, .clipped = TRUE,
                            .got_position = got_position, .current = current,
                        };
                        bool has_position = had_position;
                        const EditSessionEpoch epoch{7, 11};
                        std::vector<Event> events;
                        unsigned int fallback_calls = 0;
                        unsigned int moves = 0;
                        ApplyCompositionLayoutPosition(
                            query, has_position, epoch, 467, GUID_NULL,
                            [&, calls = std::make_unique<unsigned int>(0)](RECT& output) {
                                fallback_calls = ++*calls;
                                events.push_back(Event::Fallback);
                                QIQI_TEST_CHECK(!has_position && SameRect(output, rect) && &output != &query.rect &&
                                                range->AddRef() == 3 && range->Release() == 2);
                                if (fallback != Fallback::Failure) output = fallback_rect;
                                return fallback == Fallback::Success;
                            },
                            [&, calls = std::make_unique<unsigned int>(0)](const RECT& output) {
                                moves = ++*calls;
                                events.push_back(Event::Move);
                                QIQI_TEST_CHECK(has_position == (got_position || had_position) &&
                                                SameRect(output, got_position ? rect : fallback_rect) &&
                                                &output != &query.rect && range->AddRef() == 3 && range->Release() == 2);
                            });
                        const bool fallback_called = current && !got_position && !had_position;
                        const bool moved = current && (got_position || (fallback_called && fallback == Fallback::Success));
                        std::vector<Event> expected;
                        if (fallback_called) expected.push_back(Event::Fallback);
                        if (moved) expected.push_back(Event::Move);
                        QIQI_TEST_CHECK(events == expected && fallback_calls == (fallback_called ? 1u : 0u) &&
                                        moves == (moved ? 1u : 0u) && has_position == (had_position || (current && got_position)) &&
                                        SameRect(query.rect, rect) && query.clipped == TRUE &&
                                        query.current == current && query.got_position == got_position &&
                                        query.range.Get() == range.Get() && range->AddRef() == 3 && range->Release() == 2);
                        ++cases;
                    }
                }
            }
        }
    }
    QIQI_TEST_CHECK(cases == 72 && g_com_object_count.load() == objects_before);
}

void TestLayoutPositionCallbackBoundaries() {
    enum class Effect { MoveMutation, MoveException, NestedMove, FailedFallbackMutation, SuccessfulFallbackMutation, FallbackException };
    constexpr std::array effects{
        Effect::MoveMutation, Effect::MoveException, Effect::NestedMove,
        Effect::FailedFallbackMutation, Effect::SuccessfulFallbackMutation, Effect::FallbackException,
    };
    const auto objects_before = g_com_object_count.load();
    size_t cases = 0;
    for (const bool tsf_position : {false, true}) {
        for (const auto effect : effects) {
            if (tsf_position && (effect == Effect::FailedFallbackMutation ||
                                effect == Effect::SuccessfulFallbackMutation || effect == Effect::FallbackException)) continue;
            bool released = false;
            auto range = MakeComObject<MockTfRange>();
            range->release_observer_ = [&]() noexcept { released = true; };
            CompositionLayoutQuery query{
                .range = range, .rect = RECT{100, 110, 140, 134}, .got_position = tsf_position, .current = true,
            };
            range.Reset();
            bool has_position = false;
            unsigned int fallback_calls = 0;
            unsigned int moves = 0;
            unsigned int nested_calls = 0;
            bool caught = false;
            try {
                ApplyCompositionLayoutPosition(
                    query, has_position, EditSessionEpoch{13, 17}, 479, GUID_NULL,
                    [&](RECT& output) {
                        ++fallback_calls;
                        QIQI_TEST_CHECK(!has_position && !released &&
                                        query.range->AddRef() == 2 && query.range->Release() == 1);
                        output = RECT{500, 510, 502, 530};
                        if (effect == Effect::FailedFallbackMutation || effect == Effect::SuccessfulFallbackMutation ||
                            effect == Effect::FallbackException) {
                            has_position = true;
                        }
                        if (effect == Effect::FallbackException) throw std::runtime_error("injected fallback failure");
                        return effect != Effect::FailedFallbackMutation;
                    },
                    [&](const RECT& output) {
                        ++moves;
                        QIQI_TEST_CHECK(!released && SameRect(output, tsf_position ? query.rect : RECT{500, 510, 502, 530}) &&
                                        has_position == (tsf_position || effect == Effect::SuccessfulFallbackMutation) &&
                                        query.range->AddRef() == 2 && query.range->Release() == 1);
                        if (effect == Effect::SuccessfulFallbackMutation) return;
                        has_position = false;
                        if (effect == Effect::MoveException) throw std::runtime_error("injected movement failure");
                        if (effect == Effect::NestedMove) {
                            const CompositionLayoutQuery nested_query{.range = query.range, .current = true};
                            ApplyCompositionLayoutPosition(
                                nested_query, has_position, EditSessionEpoch{19, 23}, 479, GUID_NULL,
                                [&](RECT&) {
                                    ++nested_calls;
                                    QIQI_TEST_CHECK(!has_position && !released &&
                                                    query.range->AddRef() == 3 && query.range->Release() == 2);
                                    return false;
                                },
                                [](const RECT&) { QIQI_TEST_CHECK(false); });
                        }
                    });
            } catch (const std::runtime_error&) {
                caught = true;
            }
            const bool exceptional = effect == Effect::MoveException || effect == Effect::FallbackException;
            const bool expected_flag = effect == Effect::FailedFallbackMutation ||
                effect == Effect::SuccessfulFallbackMutation || effect == Effect::FallbackException;
            const bool moved = effect != Effect::FailedFallbackMutation && effect != Effect::FallbackException;
            QIQI_TEST_CHECK(caught == exceptional && has_position == expected_flag &&
                            fallback_calls == (tsf_position ? 0u : 1u) && moves == (moved ? 1u : 0u) &&
                            nested_calls == (effect == Effect::NestedMove ? 1u : 0u) && !released &&
                            SameRect(query.rect, RECT{100, 110, 140, 134}) &&
                            query.range->AddRef() == 2 && query.range->Release() == 1);
            query.range.Reset();
            QIQI_TEST_CHECK(released);
            ++cases;
        }
    }
    QIQI_TEST_CHECK(cases == 9 && g_com_object_count.load() == objects_before);
}

} // namespace

void RunTsfCompositionLayoutPositionTests() {
    TestLayoutPositionBranches();
    TestLayoutPositionCallbackBoundaries();
}

} // namespace qiqi::test
