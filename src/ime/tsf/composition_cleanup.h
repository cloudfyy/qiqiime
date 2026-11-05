#pragma once
#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "common/logger.h"

namespace qiqi {

struct CompositionCleanup {
    CompositionCleanup() = default;
    ~CompositionCleanup() noexcept;
    CompositionCleanup(const CompositionCleanup&) = delete;
    CompositionCleanup& operator=(const CompositionCleanup&) = delete;
    CompositionCleanup(CompositionCleanup&&) = delete;
    CompositionCleanup& operator=(CompositionCleanup&&) = delete;
    void LogResult(std::string_view stage, HRESULT hr, log::Opcode opcode = log::Opcode::Info) const noexcept;
    uint64_t service_id = 0;
    uint64_t id = 0; // Zero tracks an unpublished StartComposition result, not a scheduled cleanup.
    GUID activity_id = GUID_NULL;
    GUID parent_activity_id = GUID_NULL;
    Microsoft::WRL::ComPtr<ITfContext> context;
    Microsoft::WRL::ComPtr<ITfComposition> composition;
    Microsoft::WRL::ComPtr<ITfRange> range;
    bool started = false;
    bool attribute_clear_started = false;
    bool finished = false;
    bool terminated = false;
    bool async_requested = false;
    bool context_removed = false;
};

// Best-effort end: attribute failures are logged; when attempted, return EndComposition's HRESULT.
HRESULT ExecuteCompositionCleanup(std::shared_ptr<CompositionCleanup> cleanup, TfEditCookie ec);

// STA bookkeeping only; edit-session callbacks, not this registry, own cleanup records.
class CompositionCleanupRegistry {
public:
    CompositionCleanupRegistry() = default;
    ~CompositionCleanupRegistry() = default;
    CompositionCleanupRegistry(const CompositionCleanupRegistry&) = delete;
    CompositionCleanupRegistry& operator=(const CompositionCleanupRegistry&) = delete;
    CompositionCleanupRegistry(CompositionCleanupRegistry&&) = delete;
    CompositionCleanupRegistry& operator=(CompositionCleanupRegistry&&) = delete;

    void Track(const std::shared_ptr<CompositionCleanup>& cleanup);
    [[nodiscard]] std::shared_ptr<CompositionCleanup> FindComposition(const ITfComposition* composition) const noexcept;
    [[nodiscard]] std::shared_ptr<CompositionCleanup> FindPending(const ITfContext* context) noexcept;
    void MarkContextRemoved(const ITfContext* context, uint64_t service_id, const GUID& lifecycle_activity) const noexcept;

    // Retain the diagnostic slot count; only FindPending prunes expired/finished records.
    [[nodiscard]] size_t TrackedCount() const noexcept { return records_.size(); }

private:
    std::vector<std::weak_ptr<CompositionCleanup>> records_;
};

} // namespace qiqi
