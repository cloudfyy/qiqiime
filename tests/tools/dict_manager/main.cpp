// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

using namespace qiqi::dict_manager::tests;

int main() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
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
}
