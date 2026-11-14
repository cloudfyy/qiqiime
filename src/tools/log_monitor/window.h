// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/scoped_handle.h"
#include "common/config.h"
#include "tools/log_monitor/collector.h"
#include <memory>
#include <optional>
#include <string_view>

namespace qiqi::monitor {
inline constexpr std::wstring_view kWindowClass = L"QiQiIMELogMonitorWindow";
enum class ControlId { Start = 100, Pause, Clear, Export, Source, About, Process, Pid, Level, Search,
                 List, Details, Status, ProcessLabel, PidLabel, LevelLabel, SearchLabel,
                 DetailsSplitter, Copy = 121, CategoryLabel = 124, Category = 125,
                 Toolbar, LanguageAuto, LanguageZh, LanguageEn, Exit, SelectAll, FilterActivity, ClearActivity,
                 MetricsView = 161, MetricInstanceFilter, MetricKindFilter,
                 MetricOutcomeFilter, MetricModeFilter, MetricWindowFilter, MetricList, MetricSummaryStatus,
                 MetricInstanceLabel, MetricKindLabel, MetricOutcomeLabel, MetricModeLabel, MetricWindowLabel,
                 DiagnosticsView = 180 };

class MonitorWindow final {
public:
    explicit MonitorWindow(CaptureStartApi api = {});
    ~MonitorWindow() noexcept;
    MonitorWindow(const MonitorWindow&) = delete;
    MonitorWindow& operator=(const MonitorWindow&) = delete;
    MonitorWindow(MonitorWindow&&) = delete;
    MonitorWindow& operator=(MonitorWindow&&) = delete;
    bool Create(HINSTANCE instance, std::optional<POINT> startup_point = std::nullopt,
        UiLanguage language = UiLanguage::Auto);
    HWND Handle() const noexcept { return window_.Get(); }
private:
    friend struct MonitorWindowTestAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ScopedWindow window_;
};
int RunMonitor(HINSTANCE instance, int show, std::wstring_view command_line = {});
} // namespace qiqi::monitor
