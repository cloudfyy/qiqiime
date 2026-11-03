#pragma once
#include "common/scoped_handle.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace qiqi::sync {

enum class DataType : uint32_t { Dictionary = 1, PhraseFrequency = 2 };
enum class Operation : uint32_t { Add = 1, Modify = 2, Remove = 3, Reorder = 4 };

struct Source {
    DataType type = DataType::Dictionary;
    GUID id{};
    bool operator==(const Source&) const noexcept = default;
};

struct Version {
    GUID baseline{};
    uint64_t sequence = 0;
    bool operator==(const Version&) const noexcept = default;
};

struct Change {
    uint64_t sequence = 0;
    Operation operation = Operation::Add;
    std::vector<uint8_t> payload;
};

enum class SnapshotReason { None, Startup, BaselineMismatch, VersionGap, InterruptedWrite };
enum class InputBoundary { Composing, NewInput };

struct Batch {
    Version version;
    SnapshotReason snapshot = SnapshotReason::None;
    std::vector<Change> changes;
};

// Payloads and full snapshots belong to the data-type adapter, not this transport.
class Channel final {
public:
    static constexpr size_t kMaxRecords = 256;
    static constexpr size_t kMaxPayloadBytes = 64 * 1024;
    static constexpr size_t kMaxJournalBytes = 2 * 1024 * 1024;

    // Provisions the absolute, dedicated per-user directory for owner writes and AppContainer reads.
    // Do not pass an installation directory or a directory containing unrelated private files.
    static HRESULT Create(const std::filesystem::path& directory, Source source) noexcept;

    Channel(std::filesystem::path directory, Source source) noexcept
        : directory_(std::move(directory)), source_(source) {}
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) noexcept = default;
    Channel& operator=(Channel&&) noexcept = default;

    HRESULT Watch() noexcept;
    HANDLE NotificationHandle() const noexcept { return notification_.Get(); }
    // Rearm before checking versions. A wakeup is not proof of a committed change.
    HRESULT Rearm() noexcept;
    HRESULT Read(Version applied, Batch& batch) const noexcept;

    // The callback performs the source read/modify/write under the same exclusive lock; S_OK commits.
    // It must use atomic replacement for its own file; process death cannot undo partial source writes.
    HRESULT Publish(Version expected, Operation operation, std::span<const uint8_t> payload,
                    const std::function<HRESULT()>& write_source, Version& committed) const noexcept;

    // Call on focus/new-input boundaries. Callbacks must publish local state atomically on success
    // and must not publish back to this channel while the shared read lock is held. Only S_OK advances.
    HRESULT Synchronize(Version& applied, InputBoundary boundary,
                        const std::function<HRESULT()>& load_snapshot,
                        const std::function<HRESULT(std::span<const Change>)>& apply_changes) const noexcept;

private:
    struct NotificationTraits : FileHandleTraits {
        static void Close(HANDLE handle) noexcept { ::FindCloseChangeNotification(handle); }
    };
    std::filesystem::path directory_;
    Source source_;
    ScopedHandle<NotificationTraits> notification_;
};

} // namespace qiqi::sync
