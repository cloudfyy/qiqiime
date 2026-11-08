#pragma once

#include "tools/log_monitor/window.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/aggregate.h"
#include "common/d2d_controls.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/command_ui.h"

#include <commctrl.h>
#include <array>

namespace qiqi::monitor {

inline constexpr UINT_PTR kMonitorRefreshTimerId = 1;
inline constexpr UINT kMonitorRefreshIntervalMs = 100;
inline constexpr UINT kMonitorLossPollIntervalMs = 1000;
inline constexpr unsigned int kMonitorLossPollTicks = kMonitorLossPollIntervalMs / kMonitorRefreshIntervalMs;
inline constexpr int kMonitorDefaultDetailHeight = 130;
inline constexpr int kMonitorDefaultDetailLimit = 400;
inline constexpr int kMonitorMinimumDetailHeight = 60;
inline constexpr int kMonitorDetailsFooterSpace = 50;
inline constexpr int kMonitorLayoutReferenceDpi = 96;
inline constexpr size_t kMonitorPreviewChars = 1024;
inline constexpr std::array kMetricTableWidths{150, 130, 100, 100, 100, 80, 80, 80, 80, 90, 90,
    90, 90, 90, 90, 90, 90, 110, 110, 110, 110, 110, 110, 110};
inline constexpr std::array kMetricDisplayPercentiles{50u, 95u, 99u};
inline constexpr UINT kMetricDisplayRefreshIntervalMs = 500;
inline constexpr uint64_t kNanosecondsPerMillisecond = 1'000'000;
inline constexpr int kMetricSummaryHeight = 126;

struct MetricTableRow {
    MetricDimension dimension;
    uint64_t intents = 0;
    uint64_t pending = 0;
    std::array<uint64_t, 9> outcomes{};
    std::optional<double> coverage;
    std::optional<double> failure_rate;
    std::array<MetricQuantile, kMetricDisplayPercentiles.size()> quantiles;
    std::optional<uint64_t> maximum;
    uint64_t samples = 0;
    uint64_t over_threshold = 0;
};

struct MonitorWindow::Impl {
#pragma warning(suppress : 26455) // Bounded queues allocate; failures reach the executable's startup diagnostics.
    explicit Impl(CaptureStartApi api = {}) : collector(pending, diagnostics, api) {}
    Diagnostics diagnostics;
    EventQueue pending;
    Collector collector{pending, diagnostics};
    MetricAggregator metrics;
    EventBuffer events;
    EventBuffer tools{kDiagnosticMaxRecords, kDiagnosticMaxBytes};
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    ScopedIcon small_icon;
    ScopedIcon big_icon;
    ToolCommandUi command_ui{ReportCommandError, this};
    std::optional<std::array<bool, 8>> command_state;
    UiLanguage ui_language = UiLanguage::Auto;
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    MonitorWindow* owner = nullptr;
    std::vector<uint64_t> rows;
    std::vector<uint64_t> selected_ids;
    Filter filter;
    ProcessChoices process_choices;
    std::vector<MetricProcessInstance> metric_instances;
    std::vector<MetricTableRow> metric_rows;
    std::optional<MetricDimension> metric_selected;
    std::optional<MetricProcessInstance> metric_instance_filter;
    std::optional<metrics::Metric> metric_kind_filter;
    std::optional<metrics::Outcome> metric_outcome_filter;
    std::optional<metrics::Mode> metric_mode_filter;
    MetricView metric_window = MetricView::Cumulative;
    uint64_t metric_display_tick_ms = 0;
    uint64_t metric_display_generation = 0;
    bool metrics_view = false;
    bool updating_metric_rows = false;
    bool metric_display_active = false;
    std::wstring last_error;
    uint64_t selected = 0;
    unsigned int ticks = 0;
    bool paused = false;
    bool diagnostic_view = false;
    bool ready = false;
    bool updating_rows = false;
    bool updating_display = false;
    bool updating_filters = false;
    bool automatic_pid = false;
    bool process_choices_pending = false;
    int detail_height = kMonitorDefaultDetailHeight; // Logical pixels.
    int detail_limit = kMonitorDefaultDetailLimit;
    int splitter_drag_offset = 0;

    EventBuffer& Current() noexcept { return diagnostic_view ? tools : events; }
    const EventBuffer& Current() const noexcept { return diagnostic_view ? tools : events; }
    HWND Child(ControlId id) const noexcept { return ::GetDlgItem(hwnd, std::to_underlying(id)); }
    const wchar_t* Text(const wchar_t* zh, const wchar_t* en) const noexcept {
        return ResolveUiLanguage(ui_language) == UiLanguage::EnUs ? en : zh;
    }

    const wchar_t* LevelText(UCHAR level) const noexcept {
        switch (level) {
        case 1: return Text(L"严重", L"Critical");
        case 2: return Text(L"错误", L"Error");
        case 3: return Text(L"警告", L"Warning");
        case 4: return Text(L"信息", L"Info");
        case 5: return Text(L"详细", L"Verbose");
        default: return Text(L"全部", L"All");
        }
    }

    const wchar_t* CategoryText(uint64_t keyword) const noexcept {
        switch (keyword) {
        case std::to_underlying(log::Keyword::General):     return Text(L"通用", L"General");
        case std::to_underlying(log::Keyword::Lifecycle):   return Text(L"生命周期", L"Lifecycle");
        case std::to_underlying(log::Keyword::Input):       return Text(L"输入/FSM", L"Input");
        case std::to_underlying(log::Keyword::Composition): return Text(L"组合/会话", L"Composition");
        case std::to_underlying(log::Keyword::Dictionary):  return Text(L"词库", L"Dictionary");
        case std::to_underlying(log::Keyword::Sync):        return Text(L"跨进程同步", L"Sync");
        case std::to_underlying(log::Keyword::UI):          return Text(L"界面", L"UI");
        case std::to_underlying(log::Keyword::Config):      return Text(L"配置", L"Config");
        case std::to_underlying(log::Keyword::Metrics):     return Text(L"性能指标", L"Metrics");
        default:                                            return Text(L"全部", L"All");
        }
    }

    static constexpr uint64_t CategoryMaskFromIndex(LRESULT index) noexcept {
        switch (index) {
        case 1: return std::to_underlying(log::Keyword::General);
        case 2: return std::to_underlying(log::Keyword::Lifecycle);
        case 3: return std::to_underlying(log::Keyword::Input);
        case 4: return std::to_underlying(log::Keyword::Composition);
        case 5: return std::to_underlying(log::Keyword::Dictionary);
        case 6: return std::to_underlying(log::Keyword::Sync);
        case 7: return std::to_underlying(log::Keyword::UI);
        case 8: return std::to_underlying(log::Keyword::Config);
        case 9: return std::to_underlying(log::Keyword::Metrics);
        default: return 0;
        }
    }

    void UpdateIcons() noexcept;
    void Failure(std::wstring_view operation, ULONG error);
    bool Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id);
    void ResizeDetails(int height) noexcept;
    static LRESULT CALLBACK SplitterProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR context) noexcept;
    static LRESULT CALLBACK ListProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR context) noexcept;
    bool CreateControls();
    static void ReportCommandError(void* context, const char* operation, ULONG error) noexcept;
    const wchar_t* CommandText(ControlId id) const noexcept;
    ToolCommand UiCommand(ControlId id) const noexcept;
    const Event* FirstSelectedEvent() const noexcept;
    void UpdateCommands();
    void RefreshCommandState();
    void Layout() noexcept;
    void UpdateTheme() noexcept;
    void ApplyLanguage();
    void UpdateDpi(UINT dpi, const RECT& proposed) noexcept;
    void RecoverDisplay() noexcept;
    void RefreshProcessChoices(bool force = false);
    void Refilter();
    void SyncSelectionFromList();
    void SelectAllRows();
    void RebuildRows();
    void Tick();
    void UpdateStatus();
    std::wstring Cell(const Event& event, int column) const;
    void Notify(NMHDR& header);
    std::vector<Event> CollectSelectedEvents() const;
    void UpdateDetails();
    bool CreateMetricControls();
    void ApplyMetricLanguage();
    void RefreshMetricInstances(bool force = false);
    void RefilterMetrics();
    void RebuildMetricRows();
    void ShowMetrics(bool show);
    void UpdateMetricStatus();
    void NotifyMetrics(NMHDR& header);
    void SyncMetricSelection();
    std::array<const wchar_t*, kMetricTableWidths.size()> MetricColumns() const noexcept;
    const wchar_t* MetricName(metrics::Metric metric) const noexcept;
    const wchar_t* OutcomeName(metrics::Outcome outcome) const noexcept;
    const wchar_t* ModeName(metrics::Mode mode) const noexcept;
    std::wstring MetricCell(const MetricTableRow& row, int column) const;
    std::wstring MetricDetails() const;
    std::wstring MetricIntegrity() const;
    GUID SelectedMetricActivity() const noexcept;
    void CopySelection();
    void ShowListContextMenu(POINT screen_pt);
    void ExportSnapshot();
    bool RecoverCapturePermission(const CaptureStartFailure& failure);
    void Command(int id, int notification);
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
};

} // namespace qiqi::monitor
