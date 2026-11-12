// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <evntcons.h>

namespace qiqi::test {
using MetricEventObserver = ULONG (*)(EVENT_RECORD&);
using MetricCaptureAction = void (*)();
int RunMetricsEtwTest(MetricEventObserver observer = nullptr, MetricCaptureAction action = nullptr);
}
