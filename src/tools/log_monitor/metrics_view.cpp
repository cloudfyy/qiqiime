#include "tools/log_monitor/internal.h"
#include "common/file_util.h"
#include "common/win32_ui_util.h"

#include <algorithm>
#include <format>
#include <limits>
#include <system_error>

namespace qiqi::monitor {
using enum ControlId;
namespace {
constexpr std::array kMetricKinds{metrics::Metric::FocusTotal, metrics::Metric::FocusReload,
    metrics::Metric::FocusPosition, metrics::Metric::KeyCallback, metrics::Metric::CandidateQuery,
    metrics::Metric::CandidateUpdate, metrics::Metric::Commit, metrics::Metric::Cleanup};
constexpr std::array kMetricOutcomes{metrics::Outcome::Unspecified, metrics::Outcome::Success,
    metrics::Outcome::Failure, metrics::Outcome::Cancelled, metrics::Outcome::Stale,
    metrics::Outcome::Incomplete, metrics::Outcome::Exception, metrics::Outcome::Unhandled,
    metrics::Outcome::Rejected};
constexpr std::array kMetricModes{metrics::Mode::Unspecified, metrics::Mode::Popup,
    metrics::Mode::Uiless, metrics::Mode::FocusGained, metrics::Mode::FocusLost};

void CheckMetricResult(ULONG error, const char* operation) {
    if (error != ERROR_SUCCESS) throw std::system_error(gsl::narrow_cast<int>(error), std::system_category(), operation);
}
void ReplaceMetricItems(HWND control, std::span<const wchar_t* const> items) {
    const HRESULT error = ReplaceComboBoxItems(control, items);
    if (FAILED(error)) throw std::system_error(gsl::narrow_cast<int>(error), std::system_category(), "Metric filter items");
}
template<class T, size_t N>
std::optional<T> ReadMetricFilter(HWND control, const std::array<T, N>& choices) {
    const LRESULT index = ::SendMessageW(control, CB_GETCURSEL, 0, 0);
    CheckMetricResult(index < 0 || gsl::narrow_cast<size_t>(index) > choices.size() ?
        ERROR_INVALID_DATA : ERROR_SUCCESS, "Metric filter selection");
    return index == 0 ? std::nullopt : std::optional<T>(gsl::at(choices, gsl::narrow_cast<size_t>(index - 1)));
}
std::wstring MetadataText(std::string_view text) {
    std::wstring result;
    const HRESULT error = Utf8ToWideResult(text, result);
    if (FAILED(error)) throw std::system_error(gsl::narrow_cast<int>(error), std::system_category(), "Metric instance UTF-8");
    return result;
}
}

const wchar_t* MonitorWindow::Impl::MetricName(metrics::Metric metric) const noexcept {
    switch (metric) {
    case metrics::Metric::FocusTotal: return Text(L"焦点总耗时", L"Focus total");
    case metrics::Metric::FocusReload: return Text(L"焦点重载", L"Focus reload");
    case metrics::Metric::FocusPosition: return Text(L"焦点定位", L"Focus position");
    case metrics::Metric::KeyCallback: return Text(L"按键回调", L"Key callback");
    case metrics::Metric::CandidateQuery: return Text(L"候选查询", L"Candidate query");
    case metrics::Metric::CandidateUpdate: return Text(L"候选更新", L"Candidate update");
    case metrics::Metric::Commit: return Text(L"提交", L"Commit");
    case metrics::Metric::Cleanup: return Text(L"清理", L"Cleanup");
    }
    return Text(L"未知指标", L"Unknown metric");
}
const wchar_t* MonitorWindow::Impl::OutcomeName(metrics::Outcome outcome) const noexcept {
    switch (outcome) {
    case metrics::Outcome::Unspecified: return Text(L"未指定", L"Unspecified");
    case metrics::Outcome::Success: return Text(L"成功", L"Success");
    case metrics::Outcome::Failure: return Text(L"失败", L"Failure");
    case metrics::Outcome::Cancelled: return Text(L"取消", L"Cancelled");
    case metrics::Outcome::Stale: return Text(L"过期", L"Stale");
    case metrics::Outcome::Incomplete: return Text(L"不完整", L"Incomplete");
    case metrics::Outcome::Exception: return Text(L"异常", L"Exception");
    case metrics::Outcome::Unhandled: return Text(L"未处理", L"Unhandled");
    case metrics::Outcome::Rejected: return Text(L"拒绝", L"Rejected");
    }
    return Text(L"未知结果", L"Unknown outcome");
}
const wchar_t* MonitorWindow::Impl::ModeName(metrics::Mode mode) const noexcept {
    switch (mode) {
    case metrics::Mode::Unspecified: return Text(L"未指定", L"Unspecified");
    case metrics::Mode::Popup: return Text(L"弹出窗口", L"Popup");
    case metrics::Mode::Uiless: return Text(L"无界面", L"UI-less");
    case metrics::Mode::FocusGained: return Text(L"获得焦点", L"Focus gained");
    case metrics::Mode::FocusLost: return Text(L"失去焦点", L"Focus lost");
    }
    return Text(L"未知模式", L"Unknown mode");
}
std::array<const wchar_t*, kMetricTableWidths.size()> MonitorWindow::Impl::MetricColumns() const noexcept {
    return {Text(L"进程实例", L"Process instance"), Text(L"指标", L"Metric"), Text(L"服务 ID", L"Service ID"),
        Text(L"模式", L"Mode"), Text(L"来源", L"Source"), Text(L"意图", L"Intents"),
        OutcomeName(metrics::Outcome::Success), OutcomeName(metrics::Outcome::Failure),
        Text(L"待完成(当前)", L"Pending (current)"), OutcomeName(metrics::Outcome::Cancelled),
        OutcomeName(metrics::Outcome::Stale), OutcomeName(metrics::Outcome::Incomplete),
        OutcomeName(metrics::Outcome::Exception), OutcomeName(metrics::Outcome::Unhandled),
        OutcomeName(metrics::Outcome::Rejected), Text(L"覆盖率", L"Coverage"),
        Text(L"失败率", L"Failure rate"), Text(L"耗时样本", L"Timing samples"),
        L"P50 (ms)", L"P95 (ms)", L"P99 (ms)", Text(L"最大值 (ms)", L"Max (ms)"),
        Text(L"超阈值", L"Over threshold"), OutcomeName(metrics::Outcome::Unspecified)};
}
bool MonitorWindow::Impl::CreateMetricControls() {
    for (const auto id : {MetricInstanceLabel, MetricKindLabel, MetricOutcomeLabel, MetricModeLabel, MetricWindowLabel})
        if (!Add(L"STATIC", L"", SS_LEFT, id)) return false;
    for (const auto id : {MetricInstanceFilter, MetricKindFilter, MetricOutcomeFilter, MetricModeFilter, MetricWindowFilter})
        if (!Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, id))
            return false;
    if (!Add(L"STATIC", L"", SS_LEFT, MetricSummaryStatus) ||
        !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | LVS_SINGLESEL |
            WS_BORDER | WS_TABSTOP, MetricList)) return false;
    ListView_SetExtendedListViewStyle(Child(MetricList), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    if (!::SetWindowSubclass(Child(MetricList), ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(metric list header)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    const HRESULT error = InsertListViewColumns(Child(MetricList), MetricColumns(), kMetricTableWidths, theme.dpi);
    if (FAILED(error)) {
        Failure(L"Insert metric list columns", gsl::narrow_cast<ULONG>(error));
        return false;
    }
    return true;
}
void MonitorWindow::Impl::ApplyMetricLanguage() {
    for (const auto& [id, text] : std::array{
        std::pair{MetricInstanceLabel, Text(L"实例", L"Instance")}, std::pair{MetricKindLabel, Text(L"指标", L"Metric")},
        std::pair{MetricOutcomeLabel, Text(L"结果", L"Outcome")}, std::pair{MetricModeLabel, Text(L"模式", L"Mode")},
        std::pair{MetricWindowLabel, Text(L"时间窗口", L"Window")}}) ::SetWindowTextW(Child(id), text);
    const auto replace = [&](ControlId id, const auto& choices, auto name) {
        std::vector<const wchar_t*> items{Text(L"全部", L"All")};
        for (const auto choice : choices) items.push_back((this->*name)(choice));
        ReplaceMetricItems(Child(id), items);
    };
    replace(MetricKindFilter, kMetricKinds, &Impl::MetricName);
    replace(MetricOutcomeFilter, kMetricOutcomes, &Impl::OutcomeName);
    replace(MetricModeFilter, kMetricModes, &Impl::ModeName);
    const std::array windows{Text(L"累计", L"Cumulative"), Text(L"最近 60 秒", L"Recent 60 seconds")};
    ReplaceMetricItems(Child(MetricWindowFilter), windows);
    RefreshMetricInstances(true);
    const auto columns = MetricColumns();
    for (size_t i = 0; i < columns.size(); ++i) {
        std::wstring text(gsl::at(columns, i));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = text.data();
        if (!ListView_SetColumn(Child(MetricList), gsl::narrow_cast<int>(i), &column))
            CheckMetricResult(ERROR_INVALID_DATA, "Rename metric column");
    }
}
void MonitorWindow::Impl::RefreshMetricInstances(bool force) {
    if (!force && ::SendMessageW(Child(MetricInstanceFilter), CB_GETDROPPEDSTATE, 0, 0)) return;
    auto next = metrics.Instances();
    const auto same_identity = [](const MetricProcessInstance& a, const MetricProcessInstance& b) noexcept {
        return a.pid == b.pid && a.metadata.instance_id == b.metadata.instance_id;
    };
    if (!force && std::equal(next.begin(), next.end(), metric_instances.begin(), metric_instances.end(), same_identity)) return;
    std::vector<std::wstring> strings{Text(L"全部", L"All")};
    int selected_index = 0;
    for (const auto& instance_info : next) {
        strings.push_back(std::format(L"{} [{}]", instance_info.pid, FormatGuid(instance_info.metadata.instance_id)));
        if (metric_instance_filter && same_identity(instance_info, *metric_instance_filter))
            selected_index = gsl::narrow_cast<int>(strings.size() - 1);
    }
    std::vector<const wchar_t*> items;
    items.reserve(strings.size());
    for (const auto& text : strings) items.push_back(text.c_str());
    ReplaceMetricItems(Child(MetricInstanceFilter), items);
    ::SendMessageW(Child(MetricInstanceFilter), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(selected_index), 0);
    if (selected_index == 0) metric_instance_filter.reset();
    metric_instances.swap(next);
}
void MonitorWindow::Impl::RefilterMetrics() {
    const LRESULT index = ::SendMessageW(Child(MetricInstanceFilter), CB_GETCURSEL, 0, 0);
    CheckMetricResult(index < 0 || gsl::narrow_cast<size_t>(index) > metric_instances.size() ?
        ERROR_INVALID_DATA : ERROR_SUCCESS, "Metric instance selection");
    metric_instance_filter = index == 0 ? std::nullopt :
        std::optional<MetricProcessInstance>(gsl::at(metric_instances, gsl::narrow_cast<size_t>(index - 1)));
    metric_kind_filter = ReadMetricFilter(Child(MetricKindFilter), kMetricKinds);
    metric_outcome_filter = ReadMetricFilter(Child(MetricOutcomeFilter), kMetricOutcomes);
    metric_mode_filter = ReadMetricFilter(Child(MetricModeFilter), kMetricModes);
    const LRESULT window = ::SendMessageW(Child(MetricWindowFilter), CB_GETCURSEL, 0, 0);
    CheckMetricResult(window < 0 || window > 1 ? ERROR_INVALID_DATA : ERROR_SUCCESS, "Metric window selection");
    metric_window = window == 0 ? MetricView::Cumulative : MetricView::Recent;
    RebuildMetricRows();
    UpdateMetricStatus();
}
void MonitorWindow::Impl::RebuildMetricRows() {
    const auto generation = metrics.Status().capture_generation;
    if (generation != metric_display_generation) {
        metric_selected.reset();
        metric_display_generation = generation;
    }
    RefreshMetricInstances();
    const auto selection = MetricSelectionForView();
    std::vector<MetricTableRow> next;
    for (const auto& key : metrics.Dimensions()) {
        if (!selection.MatchesDimension(key)) continue;
        MetricSummary summary;
        CheckMetricResult(metrics.Snapshot(key, metric_window, summary), "Metric summary snapshot");
        if (!selection.MatchesSummary(summary)) continue;
        MetricHistogram timing;
        CheckMetricResult(SelectMetricTiming(summary, selection.outcome, timing), "Metric latency distribution merge");
        MetricTableRow row;
        row.dimension = key;
        row.intents = summary.intents;
        row.pending = summary.pending;
        row.outcomes = summary.outcomes;
        row.coverage = summary.Coverage();
        row.failure_rate = summary.FailureRate();
        row.samples = timing.Samples();
        row.over_threshold = timing.OverThreshold();
        row.maximum = timing.Maximum();
        for (size_t i = 0; i < row.quantiles.size(); ++i)
            gsl::at(row.quantiles, i) = timing.Quantile(gsl::at(kMetricDisplayPercentiles, i));
        next.push_back(row);
    }
    updating_metric_rows = true;
    const auto restore = gsl::finally([this]() noexcept { updating_metric_rows = false; });
    metric_rows.swap(next);
    const HWND list = Child(MetricList);
    ListView_SetItemCountEx(list, gsl::narrow_cast<int>(metric_rows.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    int selected_index = -1;
    for (size_t i = 0; i < metric_rows.size(); ++i) {
        if (metric_selected && gsl::at(metric_rows, i).dimension == *metric_selected) selected_index = gsl::narrow_cast<int>(i);
    }
    if (selected_index >= 0) {
        ListView_SetItemState(list, selected_index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    } else {
        metric_selected.reset();
    }
    if (!paused && selected_index >= 0) ListView_EnsureVisible(list, selected_index, FALSE);
    ::InvalidateRect(list, nullptr, FALSE);
    metric_display_tick_ms = ::GetTickCount64();
    metric_display_active = metrics.Status().active;
    if (metrics_view) UpdateDetails();
    RefreshCommandState();
}
void MonitorWindow::Impl::ShowMetrics(bool show) {
    metrics_view = show;
    Layout();
    if (show) RebuildMetricRows();
    UpdateDetails();
    UpdateStatus();
    if (::IsWindowVisible(hwnd)) ::SetFocus(Child(show ? MetricList : List));
}
void MonitorWindow::Impl::SyncMetricSelection() {
    const int index = ListView_GetNextItem(Child(MetricList), -1, LVNI_SELECTED);
    metric_selected = index >= 0 && gsl::narrow_cast<size_t>(index) < metric_rows.size() ?
        std::optional<MetricDimension>(gsl::at(metric_rows, gsl::narrow_cast<size_t>(index)).dimension) : std::nullopt;
    UpdateDetails();
    RefreshCommandState();
}
GUID MonitorWindow::Impl::SelectedMetricActivity() const noexcept {
    return metric_selected ? metrics.LatestActivity(*metric_selected) : GUID_NULL;
}
std::wstring MonitorWindow::Impl::MetricCell(const MetricTableRow& row, int column) const {
    const auto percent = [](std::optional<double> value) { return value ? std::format(L"{:.2f}%", *value * 100) : L"N/A"; };
    const auto duration = [](std::optional<uint64_t> value) {
        return value ? std::format(L"{:.6f}", gsl::narrow_cast<double>(*value) / kNanosecondsPerMillisecond) : L"N/A";
    };
    const auto outcome = [&](metrics::Outcome value) { return std::to_wstring(gsl::at(row.outcomes, std::to_underlying(value))); };
    switch (column) {
    case 0: return std::format(L"{} [{}]", row.dimension.pid, FormatShortGuid(row.dimension.instance_id));
    case 1: return MetricName(row.dimension.metric);
    case 2: return std::to_wstring(row.dimension.service_id);
    case 3: return ModeName(row.dimension.mode);
    case 4:
        switch (row.dimension.source) {
        case metrics::Source::ThreadManager: return Text(L"线程管理器", L"Thread manager");
        case metrics::Source::KeyEvent: return Text(L"按键事件", L"Key event");
        default: return Text(L"未指定", L"Unspecified");
        }
    case 5: return std::to_wstring(row.intents);
    case 6: return outcome(metrics::Outcome::Success);
    case 7: return outcome(metrics::Outcome::Failure);
    case 8: return std::to_wstring(row.pending);
    case 9: return outcome(metrics::Outcome::Cancelled);
    case 10: return outcome(metrics::Outcome::Stale);
    case 11: return outcome(metrics::Outcome::Incomplete);
    case 12: return outcome(metrics::Outcome::Exception);
    case 13: return outcome(metrics::Outcome::Unhandled);
    case 14: return outcome(metrics::Outcome::Rejected);
    case 15: return percent(row.coverage);
    case 16: return percent(row.failure_rate);
    case 17: return std::to_wstring(row.samples);
    case 18: case 19: case 20: {
        const auto& value = gsl::at(row.quantiles, gsl::narrow_cast<size_t>(column - 18));
        switch (value.state) {
        case QuantileState::Value: return L"\x2264 " + duration(value.upper_ns);
        case QuantileState::Overflow: return std::format(L"> {}", kMetricHistogramLimitNs / kNanosecondsPerMillisecond);
        case QuantileState::NoSamples: return Text(L"无耗时样本", L"No timing samples");
        case QuantileState::InsufficientSamples: return Text(L"样本不足", L"Insufficient samples");
        case QuantileState::InvalidPercentile: return Text(L"无效百分位", L"Invalid percentile");
        }
        break;
    }
    case 21: return duration(row.maximum);
    case 22: {
        return MetricThresholdNs(row.dimension.metric) != 0 && row.samples != 0 ? std::to_wstring(row.over_threshold) : L"N/A";
    }
    case 23: return outcome(metrics::Outcome::Unspecified);
    default: break;
    }
    return L"";
}
void MonitorWindow::Impl::NotifyMetrics(NMHDR& header) {
    if (header.code == LVN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMLVDISPINFOW*>(&header);
        if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 &&
            gsl::narrow_cast<size_t>(info.item.iItem) < metric_rows.size() && info.item.pszText && info.item.cchTextMax > 0) {
            const auto text = MetricCell(gsl::at(metric_rows, gsl::narrow_cast<size_t>(info.item.iItem)), info.item.iSubItem);
            wcsncpy_s(info.item.pszText, gsl::narrow_cast<size_t>(info.item.cchTextMax), text.c_str(), _TRUNCATE);
        }
    } else if (!updating_metric_rows && (header.code == LVN_ITEMCHANGED || header.code == LVN_ODSTATECHANGED)) {
        SyncMetricSelection();
    }
}
std::wstring MonitorWindow::Impl::MetricIntegrity() const {
    const auto status = metrics.Status();
    const auto& queue = status.queue;
    const auto& provider = status.provider_deltas;
    return std::format(
        L"{}: {} | {}: {} | {}: {}\r\n"
        L"{}: {}/{} | {}: {} | {}: {}/{}\r\n"
        L"{}: {} | {}: {} | {}: {} | {}: {} | {}: {}\r\n"
        L"{}: {} | {}: {} | {}: {} | {}: {} | {}: {}\r\n"
        L"{}: {} | {}: {} | {}: {} | {}: {}\r\n"
        L"{}: {} / {} KiB | {}: {} | {}: {}\r\n"
        L"{}: {} | {}: {} | {}: {} | {}: {} | {}: {}",
        Text(L"采集代次", L"Capture generation"), status.capture_generation, Text(L"协议版本", L"Schema"), metrics::kSchemaVersion,
        Text(L"统计起点(接收时钟 ms)", L"Statistics start (receive clock ms)"),
            status.statistics_start_tick_ms ? std::to_wstring(*status.statistics_start_tick_ms) : L"N/A",
        Text(L"ETW 丢失(事件/缓冲)", L"ETW lost (events/buffers)"), queue.events_lost, queue.buffers_lost,
        Text(L"指标队列丢弃", L"Metric queue dropped"), queue.queue_dropped,
        Text(L"队列(条目/字节)", L"Queue (records/bytes)"), queue.queued_records, queue.queued_bytes,
        Text(L"队列解码/协议错误", L"Queue decode/protocol errors"), queue.protocol_errors,
        Text(L"队列代次错误", L"Queue generation errors"), queue.generation_errors,
        Text(L"实例上限", L"Instance limit errors"), queue.source_limit_errors,
        Text(L"队列分配错误", L"Queue allocation errors"), queue.allocation_errors,
        Text(L"采集错误", L"Capture errors"), queue.capture_errors,
        Text(L"聚合丢弃", L"Aggregation dropped"), status.aggregation_dropped,
        Text(L"待完成溢出", L"Pending dropped"), status.pending_dropped,
        Text(L"聚合协议错误", L"Aggregation protocol errors"), status.protocol_errors,
        Text(L"聚合代次错误", L"Aggregation generation errors"), status.generation_errors,
        Text(L"聚合分配错误", L"Aggregation allocation errors"), status.allocation_errors,
        Text(L"重复事件", L"Duplicate events"), status.duplicate_events,
        Text(L"未匹配结束", L"Unmatched ends"), status.unmatched_ends,
        Text(L"错误码溢出", L"Error codes dropped"), status.error_codes_dropped,
        Text(L"数据可能不完整", L"Data may be incomplete"), status.incomplete ? Text(L"是", L"Yes") : Text(L"否", L"No"),
        Text(L"聚合内存", L"Aggregation memory"), status.memory_bytes / 1024, status.memory_limit / 1024,
        Text(L"去重记录", L"Remembered operations"), status.remembered_operations, Text(L"待完成", L"Pending"), status.pending,
        Text(L"提供方协议错误", L"Provider protocol"), provider.protocol,
        Text(L"提供方时钟错误", L"Provider clock"), provider.clock, Text(L"提供方身份错误", L"Provider identity"), provider.identity,
        Text(L"提供方写入错误", L"Provider write"), provider.write, Text(L"提供方中断", L"Provider interrupted"), provider.interrupted);
}
std::wstring MonitorWindow::Impl::MetricDetails() const {
    std::wstring text = MetricIntegrity() + L"\r\n\r\n";
    text += Text(L"筛选仅选择维度；计数和比率仍是该维度的完整统计。结果筛选选择耗时分布：全部=成功+失败；其它结果无耗时。\r\n"
        L"P50/P95/P99 是直方图上界；P95 至少 100 样本，P99 至少 1000。最近窗口按接收时间计算，覆盖率不适用；待完成始终是当前值。\r\n",
        L"Filters select dimensions; counts and rates retain each dimension's full statistics. Outcome selects latency: All=Success+Failure; other outcomes have no timing.\r\n"
        L"P50/P95/P99 are histogram upper bounds; P95 needs 100 samples, P99 needs 1000. Recent uses receive time; coverage is unavailable and pending is always current.\r\n");
    if (!metric_selected) return text + Text(L"\r\n请选择指标查看实例和错误码。", L"\r\nSelect a metric for instance and error details.");
    text += std::format(L"\r\n{}: {} | PID: {} | {}: {} | {}: {}\r\n",
        Text(L"实例", L"Instance"), FormatGuid(metric_selected->instance_id), metric_selected->pid,
        Text(L"服务 ID", L"Service ID"), metric_selected->service_id, Text(L"指标", L"Metric"), MetricName(metric_selected->metric));
    for (const auto& instance_info : metrics.Instances()) {
        if (instance_info.pid == metric_selected->pid && instance_info.metadata.instance_id == metric_selected->instance_id) {
            text += std::format(L"{}: {} | {}: {} | {}: {} | {}: {}\r\n",
                Text(L"版本", L"Build"), MetadataText(instance_info.metadata.build_version.data()),
                Text(L"架构", L"Architecture"), MetadataText(instance_info.metadata.architecture.data()),
                Text(L"协议", L"Schema"), instance_info.metadata.schema_version,
                Text(L"提供方代次", L"Provider generation"), instance_info.metadata.capture_generation);
            break;
        }
    }
    const GUID activity = SelectedMetricActivity();
    text += std::format(L"{}: {}\r\n", Text(L"最近活动(可按活动关联筛选日志)", L"Latest activity (activity filter opens matching logs)"),
        activity == GUID_NULL ? L"N/A" : FormatGuid(activity));
    const auto row = std::find_if(metric_rows.begin(), metric_rows.end(),
        [&](const MetricTableRow& value) noexcept { return value.dimension == *metric_selected; });
    if (row != metric_rows.end()) {
        const auto columns = MetricColumns();
        for (size_t i = 0; i < columns.size(); ++i)
            text += std::format(L"{}: {}\r\n", gsl::at(columns, i), MetricCell(*row, gsl::narrow_cast<int>(i)));
    }
    MetricSummary summary;
    CheckMetricResult(metrics.Snapshot(*metric_selected, metric_window, summary), "Metric error details snapshot");
    text += std::format(L"\r\n{}: {}\r\n", Text(L"其它错误(容量溢出)", L"Other errors (capacity overflow)"), summary.other_errors);
    for (const auto& error : std::span(summary.errors).first(summary.error_count)) {
        text += std::format(L"{} 0x{:08X} ({}) : {}\r\n",
            error.domain == metrics::ErrorDomain::HResult ? L"HRESULT" : L"Win32", error.code, error.code, error.count);
    }
    return text;
}
void MonitorWindow::Impl::UpdateMetricStatus() {
    const auto status = metrics.Status();
    uint64_t samples = 0;
    for (const auto& row : metric_rows) {
        CheckMetricResult(row.samples > (std::numeric_limits<uint64_t>::max)() - samples ?
            ERROR_ARITHMETIC_OVERFLOW : ERROR_SUCCESS, "Metric display sample count");
        samples += row.samples;
    }
    const auto text = std::format(L"{} | {}: {} | {} | {}: {} | {}: {}/{} | {}: {}\r\n"
        L"{} | {}: {} | {}: {}/{} | {}: {} | {}: {}/{}\r\n{}",
        status.active ? Text(L"指标采集中", L"Metrics capturing") :
            status.capture_generation == 0 ? Text(L"尚未采集指标", L"No metrics capture yet") : Text(L"指标已冻结", L"Metrics frozen"),
        Text(L"代次", L"Generation"), status.capture_generation,
        metric_window == MetricView::Cumulative ? Text(L"累计", L"Cumulative") : Text(L"最近 60 秒", L"Recent 60 seconds"),
        Text(L"耗时结果", L"Timing outcome"), metric_outcome_filter ? OutcomeName(*metric_outcome_filter) : Text(L"成功+失败", L"Success+Failure"),
        Text(L"维度", L"Dimensions"), metric_rows.size(), status.dimensions, Text(L"耗时样本", L"Timing samples"), samples,
        status.incomplete ? Text(L"数据可能不完整", L"Data may be incomplete") :
            Text(L"未检测到丢失(非完整性保证)", L"No detected loss (not a completeness guarantee)"),
        Text(L"协议", L"Schema"), metrics::kSchemaVersion,
        Text(L"ETW 丢失", L"ETW lost"), status.queue.events_lost, status.queue.buffers_lost,
        Text(L"队列丢弃", L"Queue dropped"), status.queue.queue_dropped,
        Text(L"聚合/待完成丢弃", L"Aggregation/pending dropped"), status.aggregation_dropped, status.pending_dropped,
        collector.Running() ? collector.CollectingMetrics() ?
            Text(L"当前采集 Logs + Metrics；切换视图不改变采集模式。",
                L"Capturing Logs + Metrics; changing views does not change capture mode.") :
            Text(L"当前只采集 Logs；请停止后在指标视图重新开始以采集 Metrics。",
                L"Capturing Logs only; stop and restart in Metrics view to capture Metrics.") :
            Text(L"在指标视图点击“开始采集”将采集 Logs + Metrics；其它视图启动只采集 Logs。",
                L"Start in Metrics view to capture Logs + Metrics; starting in other views captures Logs only."));
    ::SetWindowTextW(Child(MetricSummaryStatus), text.c_str());
    if (metrics_view) {
        auto footer = std::wstring(Text(L"导出保存当前指标筛选的 JSON 快照；不包含原始日志或进程身份。",
            L"Export saves a JSON snapshot of current metric filters; no raw logs or process identities."));
        if (!last_error.empty()) footer = std::wstring(Text(L"错误: ", L"ERROR: ")) + last_error;
        ::SetWindowTextW(Child(Status), footer.c_str());
    }
}
MetricExportSelection MonitorWindow::Impl::MetricSelectionForView() const noexcept {
    MetricExportSelection selection;
    selection.window = metric_window;
    if (metric_instance_filter)
        selection.instance = MetricInstanceRef{metric_instance_filter->pid, metric_instance_filter->metadata.instance_id};
    selection.metric = metric_kind_filter;
    selection.outcome = metric_outcome_filter;
    selection.mode = metric_mode_filter;
    return selection;
}
void MonitorWindow::Impl::ExportMetricsSnapshot(const dialog_detail::FileDialogApi& api) {
    Tick();
    RefreshMetricInstances();
    const ULONG snapshot_error = ExportMetrics(metrics, MetricSelectionForView(), metric_export_snapshot);
    if (snapshot_error != ERROR_SUCCESS) {
        Failure(Text(L"生成指标 JSON 快照失败；指标统计仍保留", L"Cannot create Metrics JSON snapshot; statistics retained"), snapshot_error);
        return;
    }
    std::array<wchar_t, kWin32PathBufferChars> path{};
    wcscpy_s(path.data(), path.size(), L"qiqi-metrics.json");
    const auto selection = dialog_detail::PromptForFile(hwnd,
        Text(L"导出指标 JSON 快照", L"Export Metrics JSON snapshot"),
        Text(L"UTF-8 指标 JSON (*.json)\0*.json\0所有文件\0*.*\0",
            L"UTF-8 Metrics JSON (*.json)\0*.json\0All files\0*.*\0"),
        path, dialog_detail::FileDialogMode::Save, L"json", api);
    if (!selection) {
        if (selection.status == dialog_detail::FileDialogStatus::Failed)
            Failure(Text(L"选择指标导出文件失败；内存快照仍保留", L"Cannot select Metrics export file; snapshot retained"), selection.error);
        return;
    }
    const ULONG error = WriteExport(path.data(), metric_export_snapshot);
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"指标导出失败，目标文件可能不完整；内存快照仍保留",
            L"Metrics export failed; target may be partial. Snapshot retained"), error);
        return;
    }
    diagnostics.Record(4, L"Metrics JSON snapshot exported");
    std::string{}.swap(metric_export_snapshot);
}
} // namespace qiqi::monitor
