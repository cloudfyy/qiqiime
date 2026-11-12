// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "common/test_support.h"
#include <filesystem>
#include <format>
#include <fstream>
#include <exception>
#include <cstdio>
#include <stdexcept>

namespace qiqi::test {

class TsfTestTrace {
public:
    explicit TsfTestTrace(std::string_view mode) {
        Expects(retained_failure_log == nullptr);
        SYSTEMTIME time{};
        ::GetSystemTime(&time);
        const auto directory = std::filesystem::current_path() / L"test-logs";
        std::filesystem::create_directories(directory);
        const auto filename = std::format(
            "{}-{:04}{:02}{:02}T{:02}{:02}{:02}{:03}Z-{}-{}.log", mode,
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
            ::GetCurrentProcessId(), ::GetTickCount64());
        const auto path = directory / filename;
        file_.open(path, std::ios::app);
        if (!file_) {
            std::cerr << "[FAIL] Opening retained test log failed: test-logs\\" << filename << std::endl;
            throw std::runtime_error("Opening retained TSF test log failed");
        }
        std::cerr << "[TRACE] Retained log: test-logs\\" << filename << std::endl;
        file_ << "[RUN] mode=" << mode << ", pid=" << ::GetCurrentProcessId()
              << ", pointer_bits=" << sizeof(void*) * 8 << std::endl;
        if (!file_) throw std::runtime_error("Writing retained TSF test log failed");
        retained_failure_log = &file_;
    }
    ~TsfTestTrace() noexcept { retained_failure_log = nullptr; }
    TsfTestTrace(const TsfTestTrace&) = delete;
    TsfTestTrace& operator=(const TsfTestTrace&) = delete;
    TsfTestTrace(TsfTestTrace&&) = delete;
    TsfTestTrace& operator=(TsfTestTrace&&) = delete;

private:
    std::ofstream file_;
};

inline void WriteTsfTestCaseLine(std::string_view event, std::string_view name, ULONGLONG start) noexcept {
    const auto now = ::GetTickCount64();
    const auto write = [&](std::ostream& output) {
        output << "[" << event << "] " << name << ", tick_ms=" << now << ", elapsed_ms=" << now - start
               << ", thread=" << ::GetCurrentThreadId()
               << ", shift=" << ::GetKeyState(VK_SHIFT) << ", ctrl=" << ::GetKeyState(VK_CONTROL)
               << ", alt=" << ::GetKeyState(VK_MENU) << ", caps=" << ::GetKeyState(VK_CAPITAL)
               << ", unwinding=" << (std::uncaught_exceptions() != 0) << std::endl;
    };
    try {
        write(std::cerr);
        write(*retained_failure_log);
        if (!*retained_failure_log) {
            std::cerr << "[FAIL] Writing retained test case log failed." << std::endl;
            std::abort();
        }
    } catch (const std::ios_base::failure&) {
        std::fputs("[FAIL] Writing retained test case log threw an I/O exception.\n", stderr);
        std::fflush(stderr);
        std::abort();
    }
}

class TsfTestCaseTrace {
public:
    explicit TsfTestCaseTrace(std::string_view name) noexcept
        : name_(name), start_(::GetTickCount64()), enabled_(retained_failure_log != nullptr) {
        if (enabled_) WriteTsfTestCaseLine("CASE-BEGIN", name_, start_);
    }
    ~TsfTestCaseTrace() noexcept {
        if (enabled_) WriteTsfTestCaseLine("CASE-END", name_, start_);
    }
    TsfTestCaseTrace(const TsfTestCaseTrace&) = delete;
    TsfTestCaseTrace& operator=(const TsfTestCaseTrace&) = delete;
    TsfTestCaseTrace(TsfTestCaseTrace&&) = delete;
    TsfTestCaseTrace& operator=(TsfTestCaseTrace&&) = delete;

private:
    const std::string_view name_;
    const ULONGLONG start_;
    const bool enabled_;
};

template <typename Test>
void RunTsfTestStage(std::string_view name, Test test) {
    Expects(retained_failure_log != nullptr);
    const auto start = ::GetTickCount64();
    const auto write = [&](std::string_view event) {
        const auto message = std::format(
            "[{}] {}, tick_ms={}, elapsed_ms={}, thread={}, shift={}, ctrl={}, alt={}, caps={}",
            event, name, ::GetTickCount64(), ::GetTickCount64() - start, ::GetCurrentThreadId(),
            ::GetKeyState(VK_SHIFT), ::GetKeyState(VK_CONTROL), ::GetKeyState(VK_MENU), ::GetKeyState(VK_CAPITAL));
        std::cerr << message << std::endl;
        *retained_failure_log << message << std::endl;
        if (!*retained_failure_log) throw std::runtime_error("Writing retained TSF stage log failed");
    };
    write("BEGIN");
    test();
    write("END");
}

} // namespace qiqi::test
