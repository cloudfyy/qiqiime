#pragma once

#include "common/scoped_handle.h"
#include <memory>
#include <string_view>

namespace qiqi::monitor {
inline constexpr std::wstring_view kWindowClass = L"QiQiIMELogMonitorWindow";
enum class ControlId { Start = 100, Pause, Clear, Export, Source, About, Process, Pid, Level, Search,
                 List, Details, Status, ProcessLabel, PidLabel, LevelLabel, SearchLabel,
                 DetailsSizeLabel, DetailsSize, LanguageLabel, Language, Copy };

class MonitorWindow final {
public:
    MonitorWindow();
    ~MonitorWindow() noexcept;
    MonitorWindow(const MonitorWindow&) = delete;
    MonitorWindow& operator=(const MonitorWindow&) = delete;
    MonitorWindow(MonitorWindow&&) = delete;
    MonitorWindow& operator=(MonitorWindow&&) = delete;
    bool Create(HINSTANCE instance);
    HWND Handle() const noexcept { return window_.Get(); }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ScopedWindow window_;
};
int RunMonitor(HINSTANCE instance, int show);
} // namespace qiqi::monitor
