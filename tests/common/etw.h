#pragma once
#include <windows.h>
#include <evntcons.h>

namespace qiqi::test {
using MetricEventObserver = ULONG (*)(EVENT_RECORD&);
int RunMetricsEtwTest(MetricEventObserver observer = nullptr);
}
