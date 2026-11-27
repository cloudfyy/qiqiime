// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/threadpool.h"

extern "C" __declspec(dllexport) HRESULT QueueTestSignal(HANDLE gate, HANDLE done) noexcept {
    return qiqi::SubmitModuleCallback([gate, done]() noexcept {
        if (::WaitForSingleObject(gate, 5'000) == WAIT_OBJECT_0) {
            ::SetEvent(done);
        }
    });
}
