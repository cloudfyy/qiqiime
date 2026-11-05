#pragma once
#include <windows.h>
#include <msctf.h>
#include <cstdint>
#include <functional>

#include "common/com_object.h"

namespace qiqi {

struct EditSessionEpoch {
    uint64_t activation = 0;
    uint64_t context = 0;
};

// Non-owning identity: callbacks must retain their existing strong COM references.
// Detached composition cleanup deliberately does not use this cancellation ticket.
struct EditSessionTicket {
    EditSessionEpoch epoch;
    const ITfContext* target = nullptr;

    [[nodiscard]] bool Matches(EditSessionEpoch current, const ITfContext* context) const noexcept {
        return epoch.activation == current.activation && epoch.context == current.context && target == context;
    }
};

// Generic modern C++ wrapper for ITfEditSession
// TSF requires all document reads/writes and GetTextExt calls to happen inside DoEditSession.
class LambdaEditSession final : public ComObjectBase<LambdaEditSession, ITfEditSession> {
public:
    using Callback = std::function<HRESULT(TfEditCookie)>;

    explicit LambdaEditSession(Callback cb) noexcept : callback_(std::move(cb)) {}
    ~LambdaEditSession() noexcept = default;

    // ITfEditSession
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        return callback_ ? callback_(ec) : E_FAIL;
    }

private:
    Callback callback_;
};

} // namespace qiqi
