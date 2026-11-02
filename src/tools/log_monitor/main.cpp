#include "tools/log_monitor/window.h"
#include <windows.h>
#include <exception>
#include "common/i18n.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        return qiqi::monitor::RunMonitor(instance, show);
    } catch (const std::exception&) {
        ::MessageBoxW(nullptr, qiqi::ResolveUiLanguage(qiqi::UiLanguage::Auto) == qiqi::UiLanguage::EnUs ?
            L"Cannot initialize QiQiIME Log Monitor (allocation or system error)." :
            L"无法初始化奇奇日志监控（内存分配或系统错误）。",
            L"QiQiIME Log Monitor", MB_OK | MB_ICONERROR);
        return 1;
    }
}
