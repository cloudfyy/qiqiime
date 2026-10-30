#pragma once
#include <windows.h>
#include <msctf.h>
#include <functional>

#include "common/com_object.h"

namespace qiqi {

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
