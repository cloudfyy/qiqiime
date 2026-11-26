// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include <gsl/gsl>

#include <bit>
#include <format>
#include <limits>
#include <span>
#include <string>

namespace qiqi::diag::detail {
namespace {

template <typename Visitor>
HRESULT VisitProcessorTopology(LOGICAL_PROCESSOR_RELATIONSHIP relation, Visitor&& visitor) {
    DWORD length = 0;
    if (::GetLogicalProcessorInformationEx(relation, nullptr, &length) != FALSE) {
        return S_OK;
    }
    const DWORD initial_error = ::GetLastError();
    if (initial_error != ERROR_INSUFFICIENT_BUFFER) {
        return HRESULT_FROM_WIN32(initial_error);
    }
    HLOCAL raw = ::LocalAlloc(LMEM_FIXED, length);
    if (!raw) {
        return E_OUTOFMEMORY;
    }
    const ScopedLocalAlloc buffer(raw);
    if (::GetLogicalProcessorInformationEx(
            relation, std::bit_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(raw), &length) == FALSE) {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    const std::span<const uint8_t> bytes(std::bit_cast<const uint8_t*>(raw),
                                         gsl::narrow_cast<size_t>(length));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.subspan(offset);
        if (remaining.size() < sizeof(DWORD) * 2) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const gsl::not_null<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*> entry(
            std::bit_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(remaining.data()));
        if (entry->Size < sizeof(DWORD) * 2 || entry->Size > remaining.size()) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const HRESULT result = visitor(*entry);
        if (FAILED(result)) {
            return result;
        }
        offset += entry->Size;
    }
    return S_OK;
}

std::string CacheTypeName(PROCESSOR_CACHE_TYPE type) noexcept {
    switch (type) {
    case CacheUnified:
        return "unified";
    case CacheInstruction:
        return "instruction";
    case CacheData:
        return "data";
    case CacheTrace:
        return "trace";
    default:
        return "unknown";
    }
}

} // namespace

void CollectCpuTopology(Json& cpu, Json& stage) {
    const WORD processor_groups = ::GetActiveProcessorGroupCount();
    cpu["processor_group_count"] = processor_groups;
    Json groups = Json::array();
    DWORD active_logical = 0;
    for (WORD group = 0; group < processor_groups; ++group) {
        const DWORD active = ::GetActiveProcessorCount(group);
        const DWORD maximum = ::GetMaximumProcessorCount(group);
        active_logical += active;
        groups.push_back(Json{
            {"index", group},
            {"active_logical_processors", active},
            {"maximum_logical_processors", maximum},
        });
    }
    cpu["logical_processor_count"] = active_logical;
    cpu["processor_groups"] = std::move(groups);

    size_t physical_cores = 0;
    Json efficiency_classes = Json::object();
    const HRESULT core_result = VisitProcessorTopology(RelationProcessorCore, [&](const auto& entry) {
        if (entry.Relationship != RelationProcessorCore) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ++physical_cores;
        const std::string key = std::to_string(entry.Processor.EfficiencyClass);
        efficiency_classes[key] = efficiency_classes.value(key, uint64_t{0}) + 1;
        return S_OK;
    });
    cpu["physical_core_count"] = physical_cores;
    cpu["core_count_by_efficiency_class"] = std::move(efficiency_classes);
    AddCheck(stage, CheckResult("cpu_physical_topology", core_result, FAILED(core_result) ? "warning" : ""));

    size_t packages = 0;
    const HRESULT package_result = VisitProcessorTopology(RelationProcessorPackage, [&](const auto& entry) {
        if (entry.Relationship != RelationProcessorPackage) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ++packages;
        return S_OK;
    });
    cpu["socket_count"] = packages;
    AddCheck(stage,
             CheckResult("cpu_package_topology", package_result, FAILED(package_result) ? "warning" : ""));

    size_t numa_nodes = 0;
    const HRESULT numa_result = VisitProcessorTopology(RelationNumaNode, [&](const auto& entry) {
        if (entry.Relationship != RelationNumaNode) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        ++numa_nodes;
        return S_OK;
    });
    cpu["numa_node_count"] = numa_nodes;
    AddCheck(stage, CheckResult("cpu_numa_topology", numa_result, FAILED(numa_result) ? "warning" : ""));

    std::array<uint64_t, 4> cache_bytes{};
    Json caches = Json::array();
    const HRESULT cache_result = VisitProcessorTopology(RelationCache, [&](const auto& entry) {
        if (entry.Relationship != RelationCache || entry.Cache.Level < 1 ||
            entry.Cache.Level > cache_bytes.size()) {
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }
        const size_t index = gsl::narrow_cast<size_t>(entry.Cache.Level - 1);
        gsl::at(cache_bytes, index) += entry.Cache.CacheSize;
        caches.push_back(Json{
            {"level", entry.Cache.Level},
            {"size_bytes", entry.Cache.CacheSize},
            {"type", CacheTypeName(entry.Cache.Type)},
            {"associativity", entry.Cache.Associativity},
            {"line_size_bytes", entry.Cache.LineSize},
        });
        return S_OK;
    });
    Json cache_totals = Json::object();
    for (size_t index = 0; index < cache_bytes.size(); ++index) {
        cache_totals[std::format("l{}_bytes", index + 1)] = gsl::at(cache_bytes, index);
    }
    cpu["cache_total_bytes"] = std::move(cache_totals);
    cpu["caches"] = std::move(caches);
    AddCheck(stage, CheckResult("cpu_cache_topology", cache_result, FAILED(cache_result) ? "warning" : ""));
}

} // namespace qiqi::diag::detail
