// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

using namespace qiqi::dict_manager::tests;

int main(int argc, char** argv) try {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 2 && std::string_view(argv[1]) == "--backup-restore-test") {
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
