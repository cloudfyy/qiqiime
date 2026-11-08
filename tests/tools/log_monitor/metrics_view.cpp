#include "common/test_support.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"
#include "common/window.h"
#include "support.h"
#include "tools/log_monitor/internal.h"
#include <filesystem>
#include <format>

namespace qiqi::monitor {
using enum ControlId;
namespace {
thread_local uint64_t enabled_keywords = 0;
thread_local std::wstring export_path;
thread_local bool cancel_export = false;
thread_local DWORD export_dialog_error = ERROR_SUCCESS;
thread_local size_t export_save_calls = 0;
thread_local MetricAggregator* clear_on_save = nullptr;
BOOL WINAPI SaveMetricsFile(OPENFILENAMEW* dialog) {
    ++export_save_calls;
    QIQI_TEST_CHECK(dialog != nullptr);
    QIQI_TEST_CHECK(std::wstring_view(dialog->lpstrDefExt) == L"json");
    QIQI_TEST_CHECK(std::wstring_view(dialog->lpstrFile) == L"qiqi-metrics.json");
    QIQI_TEST_CHECK(std::wstring_view(dialog->lpstrTitle) == L"Export Metrics JSON snapshot");
    if (cancel_export) return FALSE;
    QIQI_TEST_CHECK(!export_path.empty() && export_path.size() < dialog->nMaxFile);
    wcscpy_s(dialog->lpstrFile, dialog->nMaxFile, export_path.c_str());
    if (clear_on_save) clear_on_save->Clear();
    return TRUE;
}
DWORD WINAPI FileDialogError() noexcept { return export_dialog_error; }
ULONG WINAPI FakeStart(PTRACEHANDLE session, LPCWSTR, PEVENT_TRACE_PROPERTIES) {
    QIQI_TEST_CHECK(session != nullptr);
    *session = 0;
    return ERROR_SUCCESS;
}
ULONG WINAPI ObserveEnable(TRACEHANDLE, LPCGUID, ULONG, UCHAR, ULONGLONG any, ULONGLONG, ULONG,
    PENABLE_TRACE_PARAMETERS) noexcept {
    enabled_keywords = any;
    return ERROR_NOT_SUPPORTED;
}
void SendCommand(HWND window, ControlId id) noexcept {
    ::SendMessageW(window, WM_COMMAND, gsl::narrow_cast<WPARAM>(std::to_underlying(id)), 0);
}
void SelectFilter(HWND window, ControlId id, int index) noexcept {
    ::SendMessageW(::GetDlgItem(window, std::to_underlying(id)), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(index), 0);
    ::SendMessageW(window, WM_COMMAND, MAKEWPARAM(std::to_underlying(id), CBN_SELCHANGE), 0);
}
}

struct MonitorWindowTestAccess {
    static void Exercise(const MonitorWindow& window) {
        auto& ui = *window.impl_;
        const HWND handle = window.Handle();
        const HWND list = ui.Child(MetricList);
        SendCommand(handle, LanguageEn);
        QIQI_TEST_CHECK(!ui.collector.CollectingMetrics() && !ui.metrics_view);
        QIQI_TEST_CHECK(ui.UiCommand(Start).text == std::wstring_view(L"Start capture (Logs)"));
        const auto check_view = [&](ControlId selected_view) {
            QIQI_TEST_CHECK(ui.UiCommand(Source).checked == (selected_view == Source));
            QIQI_TEST_CHECK(ui.UiCommand(DiagnosticsView).checked == (selected_view == DiagnosticsView));
            QIQI_TEST_CHECK(ui.UiCommand(MetricsView).checked == (selected_view == MetricsView));
        };
        check_view(Source);
        ::SetWindowTextW(ui.Child(Search), L"retain-log-filter");
        SendCommand(handle, Source);
        QIQI_TEST_CHECK(ui.filter.text == L"retain-log-filter");
        SendCommand(handle, DiagnosticsView);
        check_view(DiagnosticsView);
        QIQI_TEST_CHECK(ui.filter.text.empty());
        ::SetWindowTextW(ui.Child(Search), L"retain-diagnostic-filter");
        SendCommand(handle, DiagnosticsView);
        QIQI_TEST_CHECK(ui.filter.text == L"retain-diagnostic-filter");
        SendCommand(handle, MetricsView);
        check_view(MetricsView);
        SendCommand(handle, DiagnosticsView);
        check_view(DiagnosticsView);
        QIQI_TEST_CHECK(ui.filter.text == L"retain-diagnostic-filter");
        SendCommand(handle, Source);
        check_view(Source);
        QIQI_TEST_CHECK(ui.filter.text.empty());
        SendCommand(handle, Start);
        QIQI_TEST_CHECK(enabled_keywords == std::to_underlying(log::Keyword::Logs));
        SendCommand(handle, DiagnosticsView);
        SendCommand(handle, Start);
        QIQI_TEST_CHECK(enabled_keywords == std::to_underlying(log::Keyword::Logs));
        SendCommand(handle, MetricsView);
        QIQI_TEST_CHECK(ui.UiCommand(Start).text == std::wstring_view(L"Start capture (Logs + Metrics)"));
        SendCommand(handle, Start);
        QIQI_TEST_CHECK(enabled_keywords == std::to_underlying(log::Keyword::All));
        QIQI_TEST_CHECK(!ui.collector.Running() && !ui.collector.CollectingMetrics() && !ui.UiCommand(Start).checked);
        QIQI_TEST_CHECK(ui.UiCommand(Start).text == std::wstring_view(L"Start capture (Logs + Metrics)"));
        QIQI_TEST_CHECK(ui.collector.LastStartFailure()->error == ERROR_NOT_SUPPORTED);
        SendCommand(handle, Source);
        QIQI_TEST_CHECK(ui.UiCommand(Start).text == std::wstring_view(L"Start capture (Logs)"));

        QIQI_TEST_CHECK(ui.collector.metric_queue.BeginCapture(1) == ERROR_SUCCESS);
        auto metadata = metric_tests::Record(true);
        metadata.received_tick_ms = ::GetTickCount64();
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(metadata) == ERROR_SUCCESS);
        auto sample = metric_tests::Record();
        sample.received_tick_ms = metadata.received_tick_ms;
        auto& event = std::get<metrics::Event>(sample.payload);
        event.activity_id = metric_tests::kInstance;
        event.duration_ns = 1'000'000;
        for (uint64_t i = 1; i <= 1000; ++i) {
            event.operation_id = i;
            QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        }
        event.operation_id = 1001;
        event.outcome = metrics::Outcome::Failure;
        event.duration_ns = 7'000'000;
        event.error_domain = metrics::ErrorDomain::HResult;
        event.error_code = gsl::narrow_cast<uint32_t>(E_ACCESSDENIED);
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        event.operation_id = 1002;
        event.outcome = metrics::Outcome::Cancelled;
        event.duration_ns = 0;
        event.error_domain = metrics::ErrorDomain::None;
        event.error_code = 0;
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        event.operation_id = 1003;
        event.phase = metrics::Phase::Begin;
        event.outcome = metrics::Outcome::Unspecified;
        event.duration_ns.reset();
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        event.operation_id = 2000;
        event.phase = metrics::Phase::Complete;
        event.outcome = metrics::Outcome::Success;
        event.duration_ns = 2'000'000;
        event.metric = metrics::Metric::FocusReload;
        event.mode = metrics::Mode::FocusGained;
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);

        constexpr GUID second_instance{2, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
        metadata.pid = 2;
        std::get<MetricInstance>(metadata.payload).instance_id = second_instance;
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(metadata) == ERROR_SUCCESS);
        sample.pid = 2;
        event.instance_id = second_instance;
        event.metric = metrics::Metric::FocusTotal;
        event.mode = metrics::Mode::Unspecified;
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        Event log_event;
        log_event.pid = 1;
        log_event.activity_id = metric_tests::kInstance;
        log_event.message = L"ordinary log";
        ui.events.Push(log_event);
        ui.Tick();
        QIQI_TEST_CHECK(ui.metrics.Status().instances == 2 && ui.metrics.Status().dimensions == 3);
        QIQI_TEST_CHECK(ui.metric_rows.empty());

        SendCommand(handle, MetricsView);
        QIQI_TEST_CHECK(ui.metrics_view);
        check_view(MetricsView);
        SendCommand(handle, MetricsView);
        QIQI_TEST_CHECK(ui.metrics_view);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(list, GWL_STYLE) & WS_VISIBLE) != 0);
        QIQI_TEST_CHECK((::GetWindowLongPtrW(ui.Child(List), GWL_STYLE) & WS_VISIBLE) == 0);
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 3);
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(handle), std::to_underlying(Export), MF_BYCOMMAND) & MF_GRAYED) == 0);
        QIQI_TEST_CHECK(ui.UiCommand(Export).text == std::wstring_view(L"Export Metrics JSON..."));
        QIQI_TEST_CHECK(!ui.metric_rows.front().coverage || *ui.metric_rows.front().coverage < 1);
        QIQI_TEST_CHECK(ui.metric_rows.front().samples == 1001 && ui.metric_rows.front().pending == 1);
        QIQI_TEST_CHECK(ui.metric_rows.front().quantiles.back().state == QuantileState::Value);
        QIQI_TEST_CHECK(ui.MetricCell(ui.metric_rows.front(), 18).starts_with(L"\x2264 "));
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ui.SyncMetricSelection();
        const auto selected = ui.metric_selected;
        ui.RebuildMetricRows();
        QIQI_TEST_CHECK(ui.metric_selected == selected);
        QIQI_TEST_CHECK(ReadWindowTextString(ui.Child(Details)).contains(L"HRESULT 0x80070005"));
        QIQI_TEST_CHECK(ReadWindowTextString(ui.Child(Details)).contains(L"Build: test"));
        QIQI_TEST_CHECK(ReadWindowTextString(ui.Child(Details)).contains(L"Latest activity"));
        QIQI_TEST_CHECK(!ReadWindowTextString(ui.Child(Details)).contains(L"ordinary log"));
        QIQI_TEST_CHECK(ui.UiCommand(Copy).enabled);
        SendCommand(handle, FilterActivity);
        QIQI_TEST_CHECK(!ui.metrics_view && ui.filter.activity_id == metric_tests::kInstance);
        QIQI_TEST_CHECK(ui.rows.size() == 1);
        SendCommand(handle, MetricsView);
        QIQI_TEST_CHECK(ui.metric_rows.size() == 3);
        const auto log_filter = ui.filter.activity_id;
        SelectFilter(handle, MetricInstanceFilter, 1);
        QIQI_TEST_CHECK(ui.metric_rows.size() == 2);
        SelectFilter(handle, MetricKindFilter, 1);
        QIQI_TEST_CHECK(ui.metric_rows.size() == 1);
        SelectFilter(handle, MetricOutcomeFilter, 3);
        QIQI_TEST_CHECK(ui.metric_rows.size() == 1 && ui.metric_rows.front().samples == 1);
        QIQI_TEST_CHECK(ui.metric_rows.front().failure_rate && *ui.metric_rows.front().failure_rate < 0.01);
        QIQI_TEST_CHECK(ui.MetricCell(ui.metric_rows.front(), 19) == L"Insufficient samples");
        SelectFilter(handle, MetricOutcomeFilter, 4);
        QIQI_TEST_CHECK(ui.metric_rows.front().samples == 0 && !ui.metric_rows.front().maximum);
        QIQI_TEST_CHECK(ui.MetricCell(ui.metric_rows.front(), 18) == L"No timing samples");
        SelectFilter(handle, MetricOutcomeFilter, 0);
        SelectFilter(handle, MetricModeFilter, 5);
        QIQI_TEST_CHECK(ui.metric_rows.empty());
        SelectFilter(handle, MetricModeFilter, 0);
        SelectFilter(handle, MetricWindowFilter, 1);
        QIQI_TEST_CHECK(ui.metric_rows.size() == 1 && !ui.metric_rows.front().coverage);
        QIQI_TEST_CHECK(ui.MetricCell(ui.metric_rows.front(), 15) == L"N/A");
        QIQI_TEST_CHECK(ui.filter.activity_id == log_filter);
        SendCommand(handle, LanguageZh);
        QIQI_TEST_CHECK(ui.metric_window == MetricView::Recent && ui.metric_outcome_filter == std::nullopt);
        QIQI_TEST_CHECK(ReadWindowTextString(ui.Child(MetricKindLabel)) == L"指标");
        SendCommand(handle, LanguageEn);
        for (const UINT dpi : {96u, 144u, 192u}) {
            RECT rect{0, 0, ScalePxByDpi(760, dpi), ScalePxByDpi(560, dpi)};
            ::SendMessageW(handle, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
            QIQI_TEST_CHECK(::SendMessageW(ui.Child(MetricModeFilter), CB_GETITEMHEIGHT, 0, 0) ==
                ScalePxByDpi(kFluentComboItemHeight, dpi));
            QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == ScalePxByDpi(kMetricTableWidths.front(), dpi));
            QIQI_TEST_CHECK(::SendMessageW(list, WM_GETFONT, 0, 0) == ::SendMessageW(ui.Child(Details), WM_GETFONT, 0, 0));
            RECT list_rect{};
            ::GetWindowRect(list, &list_rect);
            QIQI_TEST_CHECK(list_rect.bottom - list_rect.top >= ScalePxByDpi(60, dpi));
            RECT client{};
            ::GetClientRect(handle, &client);
            ::MapWindowPoints(handle, nullptr, reinterpret_cast<POINT*>(&client), 2);
            std::vector<RECT> bounds;
            for (HWND child = ::GetWindow(handle, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
                if ((::GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0) continue;
                RECT rect_child{};
                ::GetWindowRect(child, &rect_child);
                QIQI_TEST_CHECK(rect_child.left >= client.left && rect_child.top >= client.top &&
                    rect_child.right <= client.right && rect_child.bottom <= client.bottom);
                for (const auto& other : bounds) {
                    RECT overlap{};
                    QIQI_TEST_CHECK(!::IntersectRect(&overlap, &rect_child, &other));
                }
                bounds.push_back(rect_child);
            }
            const HWND summary_status = ui.Child(MetricSummaryStatus);
            const ScopedWindowDC dc(summary_status);
            const ScopedSelectObject font(dc.Get(), reinterpret_cast<HFONT>(::SendMessageW(summary_status, WM_GETFONT, 0, 0)));
            RECT summary_rect{};
            ::GetClientRect(summary_status, &summary_rect);
            RECT measure{0, 0, summary_rect.right, 0};
            const auto summary_text = ReadWindowTextString(summary_status);
            ::DrawTextW(dc.Get(), summary_text.c_str(), gsl::narrow_cast<int>(summary_text.size()), &measure,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            QIQI_TEST_CHECK(measure.bottom <= summary_rect.bottom);
        }
        for (const bool contrast : {false, true}) {
            SetThemeDarkOverrideForTest(true);
            SetHighContrastOverrideForTest(contrast);
            ::SendMessageW(handle, WM_THEMECHANGED, 0, 0);
            QIQI_TEST_CHECK(ListView_GetBkColor(list) == ListView_GetBkColor(ui.Child(List)));
            if (contrast) QIQI_TEST_CHECK(ListView_GetBkColor(list) == ::GetSysColor(COLOR_WINDOW));
        }
        SendCommand(handle, Pause);
        QIQI_TEST_CHECK(ui.paused);
        sample.pid = 1;
        event.instance_id = metric_tests::kInstance;
        event.operation_id = 3000;
        event.duration_ns = 3'000'000;
        sample.received_tick_ms = ::GetTickCount64();
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(sample) == ERROR_SUCCESS);
        ui.Tick();
        ui.RebuildMetricRows();
        QIQI_TEST_CHECK(ui.paused && ui.metric_rows.front().samples == 1002);
        ui.collector.metric_queue.RecordLoss(3, 2);
        ui.collector.metric_queue.RecordDecodeError();
        ui.collector.metric_queue.RecordAllocationError();
        ui.Tick();
        QIQI_TEST_CHECK(ui.metrics.Status().incomplete);
        QIQI_TEST_CHECK(ui.MetricIntegrity().contains(L"ETW lost (events/buffers): 3/2"));
        QIQI_TEST_CHECK(ui.MetricIntegrity().contains(L"Queue allocation errors: 1"));
        ui.collector.metric_queue.EndCapture();
        ui.Tick();
        QIQI_TEST_CHECK(!ui.metrics.Status().active && ui.metrics.Status().pending == 0);
        QIQI_TEST_CHECK(gsl::at(ui.metric_rows.front().outcomes, std::to_underlying(metrics::Outcome::Incomplete)) == 1);
        const auto frozen_samples = ui.metric_rows.front().samples;
        QIQI_TEST_CHECK(ui.metrics.Advance(::GetTickCount64() + 120'000) == ERROR_INVALID_STATE);
        ui.RebuildMetricRows();
        QIQI_TEST_CHECK(ui.metric_rows.front().samples == frozen_samples);
        const auto export_file = std::filesystem::temp_directory_path() /
            std::format(L"qiqi-metrics-gui-{}-{}.json", ::GetCurrentProcessId(), ::GetTickCount64());
        QIQI_TEST_CHECK(!std::filesystem::exists(export_file));
        export_path = export_file.wstring();
        const dialog_detail::FileDialogApi file_api{::GetOpenFileNameW, SaveMetricsFile, FileDialogError};
        cancel_export = true;
        ui.ExportMetricsSnapshot(file_api);
        QIQI_TEST_CHECK(!ui.metric_export_snapshot.empty() && !std::filesystem::exists(export_file));
        QIQI_TEST_CHECK(ui.metric_rows.front().samples == frozen_samples && ui.paused);
        const auto expected_snapshot = ui.metric_export_snapshot;
        bool write_failure_seen = false;
        {
            const auto original_kind = ui.metric_kind_filter;
            const auto save_calls = export_save_calls;
            ui.metric_kind_filter = gsl::narrow_cast<metrics::Metric>(0);
            test::WithDismissedNativeMessage(
                L"Cannot create Metrics JSON snapshot; statistics retained\r\nError=87 (0x00000057)",
                [&] { ui.ExportMetricsSnapshot(file_api); });
            ui.metric_kind_filter = original_kind;
            QIQI_TEST_CHECK(export_save_calls == save_calls && ui.metric_export_snapshot == expected_snapshot);
            QIQI_TEST_CHECK(ui.metrics.Status().dimensions != 0);
            export_dialog_error = CDERR_DIALOGFAILURE;
            test::WithDismissedNativeMessage(
                L"Cannot select Metrics export file; snapshot retained\r\nError=65535 (0x0000FFFF)",
                [&] { ui.ExportMetricsSnapshot(file_api); });
            export_dialog_error = ERROR_SUCCESS;
            QIQI_TEST_CHECK(ui.metric_export_snapshot == expected_snapshot && !std::filesystem::exists(export_file));
            cancel_export = false;
            QIQI_TEST_CHECK(WriteExport(export_file.c_str(), "original") == ERROR_SUCCESS);
            const ScopedFileHandle lock(::CreateFileW(export_file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            QIQI_TEST_CHECK(lock);
            test::WithDismissedNativeMessage(
                L"Metrics export failed; target may be partial. Snapshot retained\r\nError=32 (0x00000020)",
                [&] { ui.ExportMetricsSnapshot(file_api); });
            write_failure_seen = ui.metric_export_snapshot == expected_snapshot;
        }
        QIQI_TEST_CHECK(write_failure_seen && ui.metrics.Status().dimensions != 0);
        std::string saved_json;
        QIQI_TEST_CHECK(ReadFileToUtf8String(export_file, saved_json) && saved_json == "original");
        clear_on_save = &ui.metrics;
        ui.ExportMetricsSnapshot(file_api);
        clear_on_save = nullptr;
        QIQI_TEST_CHECK(ReadFileToUtf8String(export_file, saved_json) && saved_json == expected_snapshot);
        QIQI_TEST_CHECK(ui.metric_export_snapshot.empty());
        QIQI_TEST_CHECK(!saved_json.contains("ordinary log") && !saved_json.contains("\"pid\"") && !saved_json.contains("\"activity_id\""));
        QIQI_TEST_CHECK(std::filesystem::remove(export_file));
        export_path.clear();
        SendCommand(handle, Clear);
        QIQI_TEST_CHECK(ui.metric_rows.empty() && ui.events.Events().size() == 1);
        QIQI_TEST_CHECK(ui.metrics.Status().queue.events_lost == 3 && ui.metrics.Status().incomplete);
        QIQI_TEST_CHECK(ui.metrics.Status().statistics_start_tick_ms.has_value());
        QIQI_TEST_CHECK(ui.collector.metric_queue.BeginCapture(2) == ERROR_SUCCESS);
        metadata.capture_generation = 2;
        metadata.received_tick_ms = ::GetTickCount64();
        QIQI_TEST_CHECK(ui.collector.metric_queue.Push(metadata) == ERROR_SUCCESS);
        ui.Tick();
        QIQI_TEST_CHECK(ui.metrics.Status().capture_generation == 2 && !ui.metrics.Status().incomplete);
        QIQI_TEST_CHECK(ui.metrics.Status().queue.events_lost == 0 && !ui.metric_selected);
        ui.collector.metric_queue.EndCapture();
        SendCommand(handle, Source);
        QIQI_TEST_CHECK(!ui.metrics_view && !ui.diagnostic_view);
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(handle), std::to_underlying(Export), MF_BYCOMMAND) & MF_GRAYED) == 0);
        QIQI_TEST_CHECK(ui.UiCommand(Export).text == std::wstring_view(L"Export..."));
    }
};
void RunMetricsViewTests() {
    const auto restore = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        SetMonitorMetricsOverrideForTest(std::nullopt);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
    });
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 3840, 2160}, 96});
    MonitorWindow window(CaptureStartApi{FakeStart, ObserveEnable, ::OpenTraceW});
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    test::WithDismissedNativeMessage(L"Outer automatic dismissal", [&] {
        test::WithDismissedNativeMessage(L"Inner automatic dismissal", [&] {
            QIQI_TEST_CHECK(::MessageBoxW(window.Handle(), L"Inner automatic dismissal", L"Test", MB_OK) == IDOK);
        });
        QIQI_TEST_CHECK(::MessageBoxW(window.Handle(), L"Outer automatic dismissal", L"Test", MB_OK) == IDOK);
    });
    MonitorWindowTestAccess::Exercise(window);
}
} // namespace qiqi::monitor
