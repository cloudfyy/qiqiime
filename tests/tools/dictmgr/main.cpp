// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi {
HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};
} // namespace qiqi

using namespace qiqi::dict_manager::tests;

int main(int argc, char** argv) try {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::span args(argv, gsl::narrow_cast<size_t>(argc));
    if (argc == 2 && std::string_view(gsl::at(args, 1)) == "--backup-restore-test") {
        TestBackupRestoreResults();
        std::cout << "[PASS] QiQi backup restore tests\n";
        return 0;
    }
    TestVersionResource();
    TestSmartSearch();
    TestDictionarySourceSelection();
    TestModelAndCrossProcessSync();
    TestBackupRestoreResults();
    TestEditorState();
    const auto original_dpi_context = ::GetThreadDpiAwarenessContext();
    TestGuiWindow();
    QIQI_TEST_CHECK(::AreDpiAwarenessContextsEqual(::GetThreadDpiAwarenessContext(), original_dpi_context));
    TestInlineEditing();
    std::cout << "[PASS] QiQiDictManagerTest\n";
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "[FAIL] %s\n", error.what());
    return 1;
}
