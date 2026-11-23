// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string_view>

namespace qiqi::host::fulltrust {

enum class DiagnosticArea : uint8_t {
    General,
    Input,
};

class Diagnostics final {
public:
    Diagnostics() noexcept;
    ~Diagnostics() noexcept;

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;
    Diagnostics(Diagnostics&&) = delete;
    Diagnostics& operator=(Diagnostics&&) = delete;

    void Info(
        std::wstring_view stage,
        std::wstring_view detail,
        DiagnosticArea area = DiagnosticArea::General) noexcept;
    void Warning(
        std::wstring_view stage,
        std::wstring_view detail,
        DiagnosticArea area = DiagnosticArea::General) noexcept;
    void Error(
        std::wstring_view stage,
        std::wstring_view detail,
        DiagnosticArea area = DiagnosticArea::General) noexcept;

    ULONG RegistrationError() const noexcept {
        return registration_error_;
    }

    DWORD PersistentError() const noexcept {
        return persistent_error_;
    }

    std::wstring_view PersistentLogFileName() const noexcept;

private:
    void Write(
        uint8_t level,
        DiagnosticArea area,
        std::wstring_view stage,
        std::wstring_view detail) noexcept;
    bool EnsureLogFile() noexcept;
    void AppendPersistent(std::wstring_view message) noexcept;
    void ReportPersistentFailure(
        DWORD error,
        std::wstring_view operation) noexcept;

    ULONG registration_error_ = ERROR_SUCCESS;
    DWORD persistent_error_ = ERROR_SUCCESS;
    HANDLE log_file_ = INVALID_HANDLE_VALUE;
};

} // namespace qiqi::host::fulltrust

