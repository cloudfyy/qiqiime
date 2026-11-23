// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <winrt/Windows.Storage.h>

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::host {

enum class DiagnosticArea : uint8_t {
    General,
    Input,
};

class Diagnostics final : public std::enable_shared_from_this<Diagnostics> {
public:
    Diagnostics() noexcept;
    ~Diagnostics() noexcept;

    Diagnostics(const Diagnostics&) = delete;
    Diagnostics& operator=(const Diagnostics&) = delete;

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

    HRESULT PersistentError() const noexcept {
        return persistent_error_.load(std::memory_order_relaxed);
    }

private:
    void Write(
        uint8_t level,
        DiagnosticArea area,
        std::wstring_view stage,
        std::wstring_view detail) noexcept;
    void QueuePersistent(std::wstring message) noexcept;
    winrt::fire_and_forget FlushPersistentAsync();
    void SetPersistentError(HRESULT error, std::wstring_view operation) noexcept;

    static std::wstring Timestamp(std::wstring_view message);

    ULONG registration_error_ = ERROR_SUCCESS;
    std::atomic<HRESULT> persistent_error_{S_OK};
    std::vector<std::wstring> pending_;
    bool flush_started_ = false;
};

} // namespace qiqi::host
