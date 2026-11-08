#pragma once
#include <windows.h>
#include <evntcons.h>

namespace qiqi::test {
using MetricEventObserver = ULONG (*)(EVENT_RECORD&);
using MetricCaptureAction = void (*)();
int RunMetricsEtwTest(MetricEventObserver observer = nullptr, MetricCaptureAction action = nullptr);
}
