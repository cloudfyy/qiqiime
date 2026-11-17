// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/RAII.h"
#include <cstdio>

namespace qiqi::test {

struct TestClipboardTraits : ClipboardGuardTraits {
    static bool Acquire(HWND owner, DWORD& error) noexcept {
        constexpr int attempts = 25;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            if (ClipboardGuardTraits::Acquire(owner, error)) return true;
            if (error != ERROR_ACCESS_DENIED) return false;
            const HWND holder = ::GetOpenClipboardWindow();
            if (holder && ::GetWindowThreadProcessId(holder, nullptr) == ::GetCurrentThreadId()) return false;
            if (attempt + 1 < attempts) ::Sleep(2);
        }
        return false;
    }
};

// Only acquire fixture resources this way; expected-denial probes use ScopedClipboard directly.
using ScopedTestClipboard = ScopedContextGuard<TestClipboardTraits>;

inline void ReportClipboardSetupFailure(DWORD error) noexcept {
    const HWND holder = ::GetOpenClipboardWindow();
    DWORD process = 0;
    const DWORD thread = holder ? ::GetWindowThreadProcessId(holder, &process) : 0;
    std::fprintf(stderr,
        "[CLIPBOARD-SETUP] error=%lu, holder_pid=%lu, holder_thread=%lu, caller_thread=%lu\n",
        error, process, thread, ::GetCurrentThreadId());
    std::fflush(stderr);
}

} // namespace qiqi::test
