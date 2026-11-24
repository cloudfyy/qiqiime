// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/configclient.h"
#include "common/configrpc.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/support.h"

#include <wrl/client.h>
#include <gsl/gsl>
#include <cstdio>
#include <condition_variable>
#include <chrono>
#include <mutex>

void WriteProbeLog(
    qiqi::log::Level,
    std::string_view message,
    const qiqi::log::LogContext&) noexcept {
    std::fwrite(
        message.data(),
        1,
        message.size(),
        stderr);
    std::fputc('\n', stderr);
}

int main() {
    qiqi::log::SetExternalLogWriter(
        &WriteProbeLog);
    const HRESULT com =
        ::CoInitializeEx(
            nullptr,
            COINIT_APARTMENTTHREADED);
    QIQI_TEST_CHECK(
        SUCCEEDED(com));
    const auto uninitialize =
        gsl::finally(
            [com]() noexcept {
                if (SUCCEEDED(com)) {
                    ::CoUninitialize();
                }
            });
    Microsoft::WRL::ComPtr<
        IQiQiConfigBroker> direct;
    const HRESULT activation =
        ::CoCreateInstance(
            qiqi::kClsidQiQiConfigBroker,
            nullptr,
            CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(&direct));
    std::cerr
        << "CoCreateInstance hr=0x"
        << std::hex
        << gsl::narrow_cast<unsigned long>(
               activation)
        << std::dec
        << '\n';
    QIQI_TEST_CHECK(
        SUCCEEDED(activation) &&
        direct);
    const HRESULT blanket =
        ::CoSetProxyBlanket(
            direct.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_DEFAULT,
            nullptr,
            RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE);
    std::cerr
        << "CoSetProxyBlanket hr=0x"
        << std::hex
        << gsl::narrow_cast<unsigned long>(
               blanket)
        << std::dec
        << '\n';
    QIQI_TEST_CHECK(
        SUCCEEDED(blanket));

    qiqi::config::Snapshot initial;
    const HRESULT read_result =
        qiqi::config::ReadSnapshot(
            initial);
    if (read_result != S_OK) {
        std::cerr
            << "ReadSnapshot failed "
               "hr=0x"
            << std::hex
            << gsl::narrow_cast<unsigned long>(
                   read_result)
            << std::dec
            << '\n';
    }
    QIQI_TEST_CHECK(
        read_result == S_OK);

    qiqi::QiQiConfig changed =
        initial.value;
    changed.page_size =
        initial.value.page_size == 8
            ? 7
            : 8;
    qiqi::config::Snapshot saved;
    QIQI_TEST_CHECK(
        qiqi::config::SaveSnapshot(
            {
                initial.revision,
                std::move(changed),
            },
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        initial.revision);

    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    HRESULT async_result =
        E_PENDING;
    qiqi::config::Snapshot async_read;
    const bool submitted =
        qiqi::config::ReadSnapshotAsync(
            [&](HRESULT result,
                const qiqi::config::
                    Snapshot& snapshot) {
                {
                    const std::scoped_lock lock(
                        mutex);
                    async_result = result;
                    async_read = snapshot;
                    completed = true;
                }
                condition.notify_one();
            });
    QIQI_TEST_CHECK(submitted);
    {
        std::unique_lock lock(mutex);
        QIQI_TEST_CHECK(
            condition.wait_for(
                lock,
                std::chrono::seconds(10),
                [&]() {
                    return completed;
                }));
    }
    if (async_result != S_OK) {
        std::cerr
            << "Async Read failed "
               "hr=0x"
            << std::hex
            << gsl::narrow_cast<unsigned long>(
                   async_result)
            << std::dec
            << '\n';
    }
    QIQI_TEST_CHECK(
        async_result == S_OK);
    QIQI_TEST_CHECK(
        async_read.revision ==
        saved.revision);

    changed = saved.value;
    changed.page_size =
        saved.value.page_size == 6
            ? 5
            : 6;
    completed = false;
    async_result = E_PENDING;
    const bool async_save =
        qiqi::config::SaveSnapshotAsync(
            {
                saved.revision,
                std::move(changed),
            },
            [&](HRESULT result,
                const qiqi::config::
                    Snapshot& snapshot) {
                {
                    const std::scoped_lock lock(
                        mutex);
                    async_result = result;
                    async_read = snapshot;
                    completed = true;
                }
                condition.notify_one();
            });
    QIQI_TEST_CHECK(async_save);
    {
        std::unique_lock lock(mutex);
        QIQI_TEST_CHECK(
            condition.wait_for(
                lock,
                std::chrono::seconds(10),
                [&]() {
                    return completed;
                }));
    }
    QIQI_TEST_CHECK(
        async_result == S_OK);
    QIQI_TEST_CHECK(
        async_read.revision >
        saved.revision);

    qiqi::config::Snapshot rejected;
    QIQI_TEST_CHECK(
        qiqi::config::SaveSnapshot(
            {
                initial.revision,
                initial.value,
            },
            rejected) ==
        HRESULT_FROM_WIN32(
            ERROR_REVISION_MISMATCH));
    std::cout
        << "[PASS] Synchronous and "
           "native-async DCOM broker "
           "calls succeeded.\n";
    direct.Reset();
    qiqi::log::SetExternalLogWriter(
        nullptr);
    return 0;
}
