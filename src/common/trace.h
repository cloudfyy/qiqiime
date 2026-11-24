// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <cguid.h>

#include <cstdint>
#include <string_view>

namespace qiqi::trace {

struct Context {
    GUID trace_id = GUID_NULL;
    GUID caller_activity_id = GUID_NULL;
    GUID parent_activity_id = GUID_NULL;
    uint32_t flags = 0;
};

struct Result {
    GUID broker_activity_id = GUID_NULL;
    GUID commit_trace_id = GUID_NULL;
    GUID commit_activity_id = GUID_NULL;
    uint64_t commit_revision = 0;
    uint64_t revision = 0;
};

enum class Endpoint {
    Client,
    Broker,
};

class OperationSpan final {
public:
    OperationSpan(
        std::string_view component,
        std::string_view operation,
        Endpoint endpoint,
        uint64_t input_revision,
        const Context& input,
        Result& output) noexcept;
    ~OperationSpan() noexcept;

    OperationSpan(
        const OperationSpan&) =
        delete;
    OperationSpan& operator=(
        const OperationSpan&) =
        delete;
    OperationSpan(
        OperationSpan&&) =
        delete;
    OperationSpan& operator=(
        OperationSpan&&) =
        delete;

    bool Valid() const noexcept;
    Context& GetContext() noexcept;
    const Context&
    GetContext() const noexcept;
    Result& GetResult() noexcept;
    const Result&
    GetResult() const noexcept;
    HRESULT Finish(
        HRESULT result) noexcept;

private:
    const std::string_view
        component_;
    const std::string_view
        operation_;
    const Endpoint endpoint_;
    const uint64_t input_revision_;
    Context context_;
    Result& output_;
    bool valid_ = false;
    bool finished_ = false;
};

Context CreateContext(const GUID* parent_activity_id = nullptr) noexcept;
Context CreateChildContext(
    const Context& parent) noexcept;
bool IsValid(const Context& context) noexcept;
Context NormalizeContext(const Context& context) noexcept;

} // namespace qiqi::trace
