// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/fileutil.h"
#include <gsl/gsl>
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace qiqi::test {

template <typename Pause>
std::error_code RemoveDirectoryWithSharingRetry(
    const std::filesystem::path& directory, DWORD budget_ms, Pause pause) {
    constexpr DWORD kDelay = 50;
    const auto started = ::GetTickCount64();
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    const auto first_error = error;
    size_t attempts = 1;
    while (error == std::error_code(ERROR_SHARING_VIOLATION, std::system_category())) {
        const auto elapsed = ::GetTickCount64() - started;
        if (elapsed >= budget_ms) break;
        std::fprintf(stderr,
            "[CLEANUP-RETRY] pid=%lu, directory=\"%s\", attempts=%zu, error=%d, elapsed_ms=%llu, budget_ms=%lu\n",
            ::GetCurrentProcessId(), WideToUtf8(directory.native()).c_str(), attempts, error.value(),
            elapsed, budget_ms);
        std::fflush(stderr);
        pause(gsl::narrow_cast<DWORD>(std::min<ULONGLONG>(kDelay, budget_ms - elapsed)));
        if (::GetTickCount64() - started >= budget_ms) break;
        ++attempts;
        std::filesystem::remove_all(directory, error);
    }
    if (first_error) {
        std::fprintf(stderr,
            "[%s] pid=%lu, directory=\"%s\", attempts=%zu, first_error=%d, category=%s, last_error=%d, "
            "elapsed_ms=%llu, budget_ms=%lu\n",
            error ? "CLEANUP-RETRY-STOP" : "CLEANUP-RECOVERED", ::GetCurrentProcessId(),
            WideToUtf8(directory.native()).c_str(), attempts, first_error.value(), error.category().name(),
            error.value(), ::GetTickCount64() - started, budget_ms);
        std::fflush(stderr);
    }
    return error;
}

} // namespace qiqi::test
