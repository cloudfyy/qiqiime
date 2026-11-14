// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <evntcons.h>
#include <evntrace.h>
#include <functional>
#include <string>
#include <vector>
#include "common/logger.h"

namespace qiqi::test {
struct LogEvent {
    DWORD pid;
    UCHAR level;
    uint64_t keyword;
    std::string message;
};
void SetLogCaptureFilter(TRACEHANDLE session, UCHAR level, log::Keyword keywords = log::Keyword::Logs);
void DisableLogCapture(TRACEHANDLE session);
std::vector<LogEvent> CaptureLogEvents(const std::function<void(TRACEHANDLE)>& action);

using MetricEventObserver = ULONG (*)(EVENT_RECORD&);
using MetricCaptureAction = void (*)();
int RunMetricsEtwTest(MetricEventObserver observer = nullptr, MetricCaptureAction action = nullptr);
}
