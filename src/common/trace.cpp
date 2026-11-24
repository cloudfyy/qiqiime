// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/trace.h"

#include "common/logger.h"

#include <objbase.h>
#include <gsl/gsl>
#include <utility>

namespace qiqi::trace {
namespace {

GUID CreateGuid() noexcept {
    GUID value = GUID_NULL;
    return SUCCEEDED(::CoCreateGuid(&value))
        ? value
        : GUID_NULL;
}

const GUID* OperationActivity(
    Endpoint endpoint,
    const Context& context,
    const Result& result) noexcept {
    return endpoint ==
            Endpoint::Broker
        ? &result.broker_activity_id
        : &context.caller_activity_id;
}

const GUID* OperationParent(
    Endpoint endpoint,
    const Context& context) noexcept {
    if (endpoint ==
            Endpoint::Broker) {
        return &context
                    .caller_activity_id;
    }
    return context.parent_activity_id !=
            GUID_NULL
        ? &context.parent_activity_id
        : nullptr;
}

} // namespace

OperationSpan::OperationSpan(
    std::string_view component,
    std::string_view operation,
    Endpoint endpoint,
    uint64_t input_revision,
    const Context& input,
    Result& output) noexcept
    : component_(component),
      operation_(operation),
      endpoint_(endpoint),
      input_revision_(input_revision),
      context_(NormalizeContext(input)),
      output_(output) {
    output_ = {};
    if (endpoint_ ==
        Endpoint::Broker) {
        output_.broker_activity_id =
            log::CreateActivityId();
    }
    valid_ =
        IsValid(context_) &&
        (endpoint_ != Endpoint::Broker ||
         output_
                 .broker_activity_id !=
             GUID_NULL);
    QIQI_LOG_CTX(
        log::Level::info,
        (log::LogContext{
            log::Keyword::Config,
            log::Opcode::Start,
            OperationActivity(
                endpoint_,
                context_,
                output_),
            OperationParent(
                endpoint_,
                context_),
            &context_.trace_id}),
        "{} operation={} started revision={}",
        component_,
        operation_,
        input_revision_);
}

OperationSpan::~OperationSpan() noexcept {
    if (!finished_) {
        std::ignore = Finish(E_ABORT);
    }
}

bool OperationSpan::Valid() const noexcept {
    return valid_;
}

Context&
OperationSpan::GetContext() noexcept {
    return context_;
}

const Context&
OperationSpan::GetContext() const noexcept {
    return context_;
}

Result&
OperationSpan::GetResult() noexcept {
    return output_;
}

const Result&
OperationSpan::GetResult() const noexcept {
    return output_;
}

HRESULT OperationSpan::Finish(
    HRESULT result) noexcept {
    if (finished_) {
        return result;
    }
    finished_ = true;
    const log::Level level =
        FAILED(result) &&
                result != S_FALSE
            ? log::Level::err
            : log::Level::info;
    QIQI_LOG_CTX(
        level,
        (log::LogContext{
            log::Keyword::Config,
            log::Opcode::Stop,
            OperationActivity(
                endpoint_,
                context_,
                output_),
            nullptr,
            &context_.trace_id}),
        "{} operation={} completed "
        "hr=0x{:08X} revision={} "
        "commit_revision={}",
        component_,
        operation_,
        gsl::narrow_cast<unsigned long>(
            result),
        output_.revision,
        output_.commit_revision);
    return result;
}

Context CreateContext(
    const GUID* parent_activity_id) noexcept {
    Context result;
    result.trace_id = CreateGuid();
    result.caller_activity_id = CreateGuid();
    if (parent_activity_id) {
        result.parent_activity_id =
            *parent_activity_id;
    }
    return result;
}

Context CreateChildContext(
    const Context& parent) noexcept {
    Context result = parent;
    result.caller_activity_id =
        CreateGuid();
    result.parent_activity_id =
        parent.caller_activity_id;
    return result;
}

bool IsValid(const Context& context) noexcept {
    return context.trace_id != GUID_NULL &&
        context.caller_activity_id !=
            GUID_NULL &&
        context.flags == 0;
}

Context NormalizeContext(
    const Context& context) noexcept {
    Context result = context;
    if (result.trace_id == GUID_NULL) {
        result.trace_id = CreateGuid();
    }
    if (result.caller_activity_id ==
        GUID_NULL) {
        result.caller_activity_id =
            CreateGuid();
    }
    return result;
}

} // namespace qiqi::trace
