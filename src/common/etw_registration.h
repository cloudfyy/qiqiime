#pragma once

#include <windows.h>
#include <atomic>
#include <gsl/gsl>

namespace qiqi::log::detail {

class EtwRegistration final {
public:
    using RegisterFunction = ULONG (*)() noexcept;
    using UnregisterFunction = void (*)() noexcept;

    constexpr EtwRegistration(RegisterFunction register_provider, UnregisterFunction unregister_provider) noexcept
        : register_provider_(register_provider), unregister_provider_(unregister_provider) {}
    ~EtwRegistration() noexcept { Shutdown(); }

    EtwRegistration(const EtwRegistration&) = delete;
    EtwRegistration& operator=(const EtwRegistration&) = delete;
    EtwRegistration(EtwRegistration&&) = delete;
    EtwRegistration& operator=(EtwRegistration&&) = delete;

    ULONG EnsureRegistered() noexcept {
        if (IsRegistered()) {
            return ERROR_SUCCESS;
        }
        ::AcquireSRWLockExclusive(&registration_lock_);
        const auto unlock = gsl::finally([this]() noexcept { ::ReleaseSRWLockExclusive(&registration_lock_); });
        if (IsRegistered()) {
            return ERROR_SUCCESS;
        }
        const ULONG error = register_provider_();
        if (error == ERROR_SUCCESS) {
            registered_.store(true, std::memory_order_release);
        }
        return error;
    }

    bool IsRegistered() const noexcept { return registered_.load(std::memory_order_acquire); }

    // Module teardown requires quiescent callers; do not acquire the initialization lock while unloading.
    void Shutdown() noexcept {
        if (registered_.exchange(false, std::memory_order_acq_rel)) {
            unregister_provider_();
        }
    }

private:
    gsl::not_null<RegisterFunction> register_provider_;
    gsl::not_null<UnregisterFunction> unregister_provider_;
    SRWLOCK registration_lock_ = SRWLOCK_INIT;
    std::atomic<bool> registered_{false};
};

} // namespace qiqi::log::detail
