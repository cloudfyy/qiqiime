// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/configstore.h"
#include "broker/runtime.h"
#include "broker/security.h"
#include "common/appcontainer.h"
#include "common/comobj.h"
#include "common/config.h"
#include "common/brokerctrl.h"
#include "common/configclient.h"
#include "common/RAII.h"
#include "common/support.h"

#include <gsl/gsl>
#include <aclapi.h>
#include <sddl.h>
#include <atomic>
#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <span>
#include <thread>

namespace trace = qiqi::trace;

namespace qiqi {
std::atomic<long> g_com_object_count{0};
} // namespace qiqi

namespace {

using qiqi::QiQiConfig;
using qiqi::ImeSkinId;
using qiqi::DiscoverWindowsSearchPackageSids;
using qiqi::ScopedLocalAlloc;
using qiqi::ScopedRegKey;

void CheckEqual(
    const QiQiConfig& expected,
    const QiQiConfig& actual) {
    qiqi::ForEachConfigField(
        [&](const auto& field) {
            QIQI_TEST_CHECK(
                field.Value(expected) ==
                field.Value(actual));
        });
    QIQI_TEST_CHECK(
        expected.candidate_font_family ==
        actual.candidate_font_family);
    QIQI_TEST_CHECK(
        expected.primary_dict_path ==
        actual.primary_dict_path);
    QIQI_TEST_CHECK(
        expected.user_dict_path ==
        actual.user_dict_path);
}

class TestServerSecurity final : public qiqi::ComObjectBase<TestServerSecurity, IServerSecurity> {
public:
    explicit TestServerSecurity(HRESULT revert_result) noexcept : revert_result_(revert_result) {}

    STDMETHODIMP QueryBlanket(DWORD*, DWORD*, OLECHAR**, DWORD*, DWORD*, RPC_AUTHZ_HANDLE*, DWORD*) noexcept override {
        return E_NOTIMPL;
    }

    STDMETHODIMP ImpersonateClient() noexcept override {
        if (!::ImpersonateSelf(SecurityImpersonation)) return HRESULT_FROM_WIN32(::GetLastError());
        impersonating_ = true;
        return S_OK;
    }

    STDMETHODIMP RevertToSelf() noexcept override {
        if (!::RevertToSelf()) return HRESULT_FROM_WIN32(::GetLastError());
        impersonating_ = false;
        return revert_result_;
    }

    STDMETHODIMP_(BOOL) IsImpersonating() noexcept override { return impersonating_ ? TRUE : FALSE; }

private:
    HRESULT revert_result_;
    bool impersonating_ = false;
};

void CheckCallerReversion(const std::wstring& subkey) {
    QIQI_TEST_CHECK(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const auto uninitialize = gsl::finally([]() noexcept { ::CoUninitialize(); });
    const qiqi::broker::Runtime runtime(subkey);
    for (const HRESULT status : {S_OK, E_UNEXPECTED}) {
        const auto security = qiqi::MakeComObject<TestServerSecurity>(status);
        IUnknown* previous = nullptr;
        QIQI_TEST_CHECK(::CoSwitchCallContext(security.Get(), &previous) == S_OK);
        const auto restore = gsl::finally([&]() noexcept {
            IUnknown* current = nullptr;
            QIQI_TEST_CHECK(::CoSwitchCallContext(previous, &current) == S_OK);
            QIQI_TEST_CHECK(current == static_cast<IServerSecurity*>(security.Get()));
        });
        QIQI_TEST_CHECK(runtime.ValidateCaller(qiqi::broker::CallerAccess::Mutation) == status);
        QIQI_TEST_CHECK(!security->IsImpersonating());
    }
}

void CheckCallerSecurityPolicy() {
    using qiqi::broker::CallerAccess;
    using qiqi::broker::CallerIdentity;
    using qiqi::broker::IsCallerIdentityAllowed;

    QIQI_TEST_CHECK(
        IsCallerIdentityAllowed(
            CallerAccess::Read,
            CallerIdentity{
                .user_match = true,
                .session_match = true,
                .app_container = false,
            }));
    QIQI_TEST_CHECK(
        IsCallerIdentityAllowed(
            CallerAccess::Mutation,
            CallerIdentity{
                .user_match = true,
                .session_match = true,
                .app_container = false,
            }));
    QIQI_TEST_CHECK(
        IsCallerIdentityAllowed(
            CallerAccess::Read,
            CallerIdentity{
                .user_match = true,
                .session_match = false,
                .app_container = true,
            }));
    QIQI_TEST_CHECK(
        IsCallerIdentityAllowed(
            CallerAccess::Mutation,
            CallerIdentity{
                .user_match = true,
                .session_match = true,
                .app_container = true,
            }));
    QIQI_TEST_CHECK(
        IsCallerIdentityAllowed(
            CallerAccess::Mutation,
            CallerIdentity{
                .user_match = true,
                .session_match = false,
                .app_container = true,
            }));
    QIQI_TEST_CHECK(
        !IsCallerIdentityAllowed(
            CallerAccess::Mutation,
            CallerIdentity{
                .user_match = true,
                .session_match = false,
                .app_container = false,
            }));
    QIQI_TEST_CHECK(
        !IsCallerIdentityAllowed(
            CallerAccess::Read,
            CallerIdentity{
                .user_match = false,
                .session_match = true,
                .app_container = true,
            }));
}

void CheckBrokerHandoffPolicy() {
    using qiqi::config::BrokerHandoffReason;
    using qiqi::config::
        kBrokerMutationHandoffRequired;
    using qiqi::config::
        SelectBrokerHandoff;

    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            true,
            kBrokerMutationHandoffRequired,
            false,
            false) ==
        BrokerHandoffReason::
            MutationRequired);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            false,
            kBrokerMutationHandoffRequired,
            false,
            false) ==
        BrokerHandoffReason::None);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            false,
            REGDB_E_CLASSNOTREG,
            false,
            false) ==
        BrokerHandoffReason::
            ActivationUnavailable);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            true,
            REGDB_E_CLASSNOTREG,
            false,
            false) ==
        BrokerHandoffReason::
            ActivationUnavailable);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            true,
            kBrokerMutationHandoffRequired,
            true,
            false) ==
        BrokerHandoffReason::None);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            true,
            kBrokerMutationHandoffRequired,
            false,
            true) ==
        BrokerHandoffReason::None);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            false,
            REGDB_E_CLASSNOTREG,
            true,
            false) ==
        BrokerHandoffReason::None);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            false,
            REGDB_E_CLASSNOTREG,
            false,
            true) ==
        BrokerHandoffReason::None);
    QIQI_TEST_CHECK(
        SelectBrokerHandoff(
            false,
            E_ACCESSDENIED,
            false,
            false) ==
        BrokerHandoffReason::None);
}

void CheckWindowsSearchPackageAccess(
    const std::wstring& subkey) {
    std::vector<std::wstring> sid_texts;
    QIQI_TEST_CHECK(
        SUCCEEDED(
            DiscoverWindowsSearchPackageSids(
                sid_texts)));
    if (sid_texts.empty()) {
        return;
    }

    std::vector<std::wstring> key_paths{
        subkey};
    for (size_t key_index = 0;
         key_index < key_paths.size();
         ++key_index) {
        const std::wstring key_path =
            key_paths.at(key_index);
        ScopedRegKey key;
        const LSTATUS open_error =
            ::RegOpenKeyExW(
                HKEY_CURRENT_USER,
                key_path.c_str(),
                0,
                KEY_READ |
                    READ_CONTROL,
                key.Put());
        if (open_error !=
            ERROR_SUCCESS) {
            std::wcerr
                << L"Opening Search package "
                   "registry key failed: key="
                << key_path
                << L" error="
                << open_error
                << L"\n";
        }
        QIQI_TEST_CHECK(
            open_error ==
            ERROR_SUCCESS);
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR raw = nullptr;
        QIQI_TEST_CHECK(
            ::GetSecurityInfo(
                key.Get(),
                SE_REGISTRY_KEY,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                &dacl,
                nullptr,
                &raw) ==
            ERROR_SUCCESS);
        const ScopedLocalAlloc
            descriptor_owner(raw);
        PEXPLICIT_ACCESS_W entries =
            nullptr;
        ULONG entry_count = 0;
        QIQI_TEST_CHECK(
            ::GetExplicitEntriesFromAclW(
                dacl,
                &entry_count,
                &entries) ==
            ERROR_SUCCESS);
        QIQI_TEST_CHECK(
            entries != nullptr);
        const ScopedLocalAlloc
            entries_owner(entries);
        const std::span<
            const EXPLICIT_ACCESS_W>
            access_entries(
                entries,
                gsl::narrow_cast<
                    size_t>(
                    entry_count));
        for (const auto& sid_text :
             sid_texts) {
            PSID expected = nullptr;
            QIQI_TEST_CHECK(
                ::ConvertStringSidToSidW(
                    sid_text.c_str(),
                    &expected));
            const ScopedLocalAlloc
                expected_owner(expected);
            bool found = false;
            for (const auto& entry :
                 access_entries) {
                if (entry.grfAccessMode ==
                        GRANT_ACCESS &&
                    entry.Trustee
                            .TrusteeForm ==
                        TRUSTEE_IS_SID &&
                    ::EqualSid(
                        expected,
                        entry.Trustee
                            .ptstrName) &&
                    (entry
                         .grfAccessPermissions &
                     KEY_READ) ==
                        KEY_READ) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::wcerr
                    << L"Missing Search package "
                       "registry read access: sid="
                    << sid_text
                    << L" key="
                    << key_path
                    << L"\n";
            }
            QIQI_TEST_CHECK(found);
        }

        for (DWORD child_index = 0;;) {
            std::array<wchar_t, 256>
                child_name{};
            DWORD child_length =
                gsl::narrow_cast<DWORD>(
                    child_name.size());
            const LSTATUS enumerate =
                ::RegEnumKeyExW(
                    key.Get(),
                    child_index,
                    child_name.data(),
                    &child_length,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr);
            if (enumerate ==
                ERROR_NO_MORE_ITEMS) {
                break;
            }
            QIQI_TEST_CHECK(
                enumerate ==
                ERROR_SUCCESS);
            key_paths.push_back(
                key_path + L"\\" +
                child_name.data());
            ++child_index;
        }
    }
}

} // namespace

int main() {
    CheckCallerSecurityPolicy();
    CheckBrokerHandoffPolicy();
    const std::wstring subkey =
        L"Software\\QiQiIME_BrokerStore_" +
        std::to_wstring(
            ::GetCurrentProcessId());
    const auto cleanup =
        gsl::finally(
            [&]() noexcept {
                const LSTATUS status =
                    ::RegDeleteTreeW(
                        HKEY_CURRENT_USER,
                        subkey.c_str());
                QIQI_TEST_CHECK(
                    status == ERROR_SUCCESS ||
                    status ==
                        ERROR_FILE_NOT_FOUND);
            });

    CheckCallerReversion(subkey);
    qiqi::broker::ConfigStore store(
        subkey);
    qiqi::config::Snapshot first;
    const HRESULT first_read =
        store.Read(first);
    if (FAILED(first_read)) {
        std::cerr
            << "Initial Broker store read "
               "failed hr=0x"
            << std::hex
            << gsl::narrow_cast<
                unsigned long>(
                first_read)
            << std::dec
            << '\n';
    }
    QIQI_TEST_CHECK(
        first_read == S_OK);
    CheckWindowsSearchPackageAccess(
        subkey);
    QIQI_TEST_CHECK(
        first.revision != 0);
    QIQI_TEST_CHECK(
        qiqi::ValidateImeSkinCatalog(
            first.skin_catalog) == S_OK);

    QiQiConfig changed =
        first.value;
    changed.page_size = 9;
    changed.ime_skin_id =
        ImeSkinId::PeachBlossom;
    changed.candidate_font_family =
        L"Microsoft YaHei UI";
    const trace::Context save_trace =
        trace::CreateContext();
    const GUID save_activity =
        trace::CreateContext()
            .caller_activity_id;
    QIQI_TEST_CHECK(
        trace::IsValid(save_trace));
    QIQI_TEST_CHECK(
        save_activity != GUID_NULL);
    qiqi::config::Snapshot saved;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            changed,
            save_trace,
            save_activity,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        first.revision);
    CheckEqual(saved.value, changed);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        save_trace.trace_id);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        save_activity);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);

    const auto unchanged = saved;
    const trace::Context conflict_trace =
        trace::CreateContext();
    const GUID conflict_activity =
        trace::CreateContext()
            .caller_activity_id;
    QIQI_TEST_CHECK(
        store.Save(
            first.revision,
            QiQiConfig{},
            conflict_trace,
            conflict_activity,
            saved) ==
        HRESULT_FROM_WIN32(
            ERROR_REVISION_MISMATCH));
    QIQI_TEST_CHECK(
        saved.revision ==
        unchanged.revision);
    CheckEqual(saved.value, unchanged.value);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        save_trace.trace_id);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        save_activity);

    std::atomic<int> successes{0};
    std::atomic<int> conflicts{0};
    const uint64_t concurrent_revision =
        saved.revision;
    QiQiConfig left = saved.value;
    QiQiConfig right = saved.value;
    left.page_size = 6;
    right.page_size = 7;
    const auto save_once =
        [&](QiQiConfig value,
            const trace::Context trace,
            const GUID activity) {
            qiqi::config::Snapshot output;
            const HRESULT result =
                store.Save(
                    concurrent_revision,
                    value,
                    trace,
                    activity,
                    output);
            if (result == S_OK) {
                ++successes;
            } else if (
                result ==
                HRESULT_FROM_WIN32(
                    ERROR_REVISION_MISMATCH)) {
                ++conflicts;
            } else {
                QIQI_TEST_CHECK(
                    result == S_OK);
            }
        };
    {
        std::jthread left_thread(
            save_once,
                left,
                trace::CreateContext(),
                trace::CreateContext()
                    .caller_activity_id);
        std::jthread right_thread(
            save_once,
                right,
                trace::CreateContext(),
                trace::CreateContext()
                    .caller_activity_id);
    }
    QIQI_TEST_CHECK(successes == 1);
    QIQI_TEST_CHECK(conflicts == 1);

    QIQI_TEST_CHECK(
        store.Read(saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.commit_trace_id !=
        GUID_NULL);
    QIQI_TEST_CHECK(
        saved.commit_activity_id !=
        GUID_NULL);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);
    auto commit_trace =
        saved.commit_trace_id;
    auto commit_activity =
        saved.commit_activity_id;
    const auto directory =
        std::filesystem::temp_directory_path() /
        L"QiQiIME_BrokerStore";
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.commit_revision ==
        saved.revision);
    QIQI_TEST_CHECK(
        saved.commit_trace_id !=
        commit_trace);
    commit_trace =
        saved.commit_trace_id;
    commit_activity =
        saved.commit_activity_id;
    const uint64_t before_noop_publish =
        saved.revision;
    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            directory,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision ==
        before_noop_publish);
    QIQI_TEST_CHECK(
        saved.commit_trace_id ==
        commit_trace);
    QIQI_TEST_CHECK(
        saved.commit_activity_id ==
        commit_activity);
    std::filesystem::path published;
    QIQI_TEST_CHECK(
        qiqi::LoadUserDictSyncDirectory(
            published,
            subkey.c_str()) == S_OK);
    QIQI_TEST_CHECK(
        published == directory);

    const uint64_t before_reset =
        saved.revision;
    QIQI_TEST_CHECK(
        store.Reset(
            before_reset,
            trace::CreateContext(),
            trace::CreateContext()
                .caller_activity_id,
            saved) == S_OK);
    QIQI_TEST_CHECK(
        saved.revision >
        before_reset);
    CheckEqual(saved.value, QiQiConfig{});

    qiqi::broker::ConfigStore restarted(
        subkey);
    qiqi::config::Snapshot after_restart;
    QIQI_TEST_CHECK(
        restarted.Read(after_restart) ==
        S_OK);
    QIQI_TEST_CHECK(
        after_restart.commit_trace_id ==
        saved.commit_trace_id);
    QIQI_TEST_CHECK(
        after_restart.commit_activity_id ==
        saved.commit_activity_id);
    QIQI_TEST_CHECK(
        after_restart.commit_revision ==
        saved.commit_revision);

    const wchar_t* previous_override =
        qiqi::HasConfigRegistrySubKeyOverride()
            ? qiqi::
                GetActiveConfigRegistrySubKey()
            : nullptr;
    qiqi::SetConfigRegistrySubKeyOverrideForTest(
        subkey.c_str());
    const auto restore_override =
        gsl::finally([previous_override]() noexcept {
            qiqi::SetConfigRegistrySubKeyOverrideForTest(
                previous_override);
        });
    qiqi::config::Snapshot registry_snapshot;
    QIQI_TEST_CHECK(
        qiqi::config::
            ReadSnapshotFromRegistry(
                registry_snapshot) ==
        S_OK);
    QIQI_TEST_CHECK(
        registry_snapshot.revision ==
        after_restart.revision);
    QIQI_TEST_CHECK(
        qiqi::config::CachedRevision() ==
        registry_snapshot.revision);
    CheckEqual(
        registry_snapshot.value,
        after_restart.value);
    QIQI_TEST_CHECK(
        registry_snapshot.skin_catalog.id ==
        after_restart.skin_catalog.id);

    const auto restore_route =
        gsl::finally([]() noexcept {
            qiqi::config::
                SetConfigReadRouteOverrideForTest(
                    std::nullopt);
        });
    const auto check_async_mutation_denied =
        [](const auto& submit) {
            std::promise<HRESULT>
                async_rejection;
            auto rejection_result =
                async_rejection
                    .get_future();
            QIQI_TEST_CHECK(
                submit(
                    [&async_rejection](
                        HRESULT result,
                        const qiqi::
                            config::
                                Snapshot&) {
                        async_rejection
                            .set_value(
                                result);
                    }) == S_OK);
            QIQI_TEST_CHECK(
                rejection_result.wait_for(
                    std::chrono::seconds(
                        5)) ==
                std::future_status::ready);
            QIQI_TEST_CHECK(
                rejection_result.get() ==
                E_ACCESSDENIED);
        };
    const auto check_local_mutations_denied =
        [&](const qiqi::config::
                    SaveRequest& save_request) {
            qiqi::config::Snapshot rejected;
            const qiqi::config::
                SyncDirectoryRequest
                    sync_request{
                        .expected_revision =
                            save_request
                                .expected_revision,
                        .directory =
                            std::filesystem::
                                temp_directory_path() /
                            L"qiqi-config-"
                            L"route-denied",
                    };
            QIQI_TEST_CHECK(
                qiqi::config::
                    SaveSnapshot(
                        save_request,
                        rejected) ==
                E_ACCESSDENIED);
            QIQI_TEST_CHECK(
                qiqi::config::
                    ResetSnapshot(
                        save_request
                            .expected_revision,
                        rejected) ==
                E_ACCESSDENIED);
            QIQI_TEST_CHECK(
                qiqi::config::
                    PublishSyncDirectory(
                        sync_request,
                        rejected) ==
                E_ACCESSDENIED);
            check_async_mutation_denied(
                [&](auto completion) {
                    return qiqi::config::
                        SaveSnapshotAsync(
                            save_request,
                            std::move(
                                completion));
                });
            check_async_mutation_denied(
                [&](auto completion) {
                    return qiqi::config::
                        ResetSnapshotAsync(
                            save_request
                                .expected_revision,
                            std::move(
                                completion));
                });
            check_async_mutation_denied(
                [&](auto completion) {
                    return qiqi::config::
                        PublishSyncDirectoryAsync(
                            sync_request,
                            std::move(
                                completion));
                });
        };
    qiqi::config::
        SetConfigReadRouteOverrideForTest(
            qiqi::config::ConfigReadRoute::
                AppContainerRegistry);
    qiqi::config::Snapshot registry_sync;
    QIQI_TEST_CHECK(
        qiqi::config::ReadSnapshot(
            registry_sync) == S_OK);
    QIQI_TEST_CHECK(
        registry_sync.revision ==
        after_restart.revision);
    std::promise<std::pair<
        HRESULT,
        qiqi::config::Snapshot>>
        async_route;
    auto async_result =
        async_route.get_future();
    QIQI_TEST_CHECK(
        qiqi::config::ReadSnapshotAsync(
            [&async_route](
                HRESULT result,
                const qiqi::config::
                    Snapshot& snapshot) {
                async_route.set_value(
                    {result, snapshot});
            }) == S_OK);
    QIQI_TEST_CHECK(
        async_result.wait_for(
            std::chrono::seconds(5)) ==
        std::future_status::ready);
    const auto [async_hr,
                async_snapshot] =
        async_result.get();
    QIQI_TEST_CHECK(
        async_hr == S_OK &&
        async_snapshot.revision ==
            after_restart.revision);
    check_local_mutations_denied(
        qiqi::config::SaveRequest{
                .expected_revision =
                    after_restart.revision,
                .value =
                    after_restart.value,
            });

    qiqi::config::
        SetConfigReadRouteOverrideForTest(
            qiqi::config::ConfigReadRoute::
                BuiltinDefaults);
    qiqi::config::Snapshot builtin_sync;
    QIQI_TEST_CHECK(
        qiqi::config::ReadSnapshot(
            builtin_sync) == S_OK);
    CheckEqual(
        builtin_sync.value,
        QiQiConfig{});
    QIQI_TEST_CHECK(
        builtin_sync.revision == 0 &&
        builtin_sync.skin_catalog.id ==
            L"builtin-v1");
    std::promise<std::pair<
        HRESULT,
        qiqi::config::Snapshot>>
        builtin_async_route;
    auto builtin_async_result =
        builtin_async_route.get_future();
    QIQI_TEST_CHECK(
        qiqi::config::ReadSnapshotAsync(
            [&builtin_async_route](
                HRESULT result,
                const qiqi::config::
                    Snapshot& snapshot) {
                builtin_async_route.set_value(
                    {result, snapshot});
            }) == S_OK);
    QIQI_TEST_CHECK(
        builtin_async_result.wait_for(
            std::chrono::seconds(5)) ==
        std::future_status::ready);
    const auto [builtin_async_hr,
                builtin_async_snapshot] =
        builtin_async_result.get();
    QIQI_TEST_CHECK(
        builtin_async_hr == S_OK &&
        builtin_async_snapshot.revision ==
            0);
    CheckEqual(
        builtin_async_snapshot.value,
        QiQiConfig{});
    QIQI_TEST_CHECK(
        builtin_async_snapshot
            .user_dict_sync_directory
            .empty() &&
        builtin_async_snapshot
            .commit_trace_id ==
            GUID_NULL &&
        builtin_async_snapshot
            .commit_activity_id ==
            GUID_NULL &&
        builtin_async_snapshot
                .commit_revision ==
            0);
    check_local_mutations_denied(
        qiqi::config::SaveRequest{
                .expected_revision =
                    after_restart.revision,
                .value = QiQiConfig{},
            });

    QIQI_TEST_CHECK(
        store.PublishSyncDirectory(
            saved.revision,
            L"relative",
            saved) == E_INVALIDARG);
    std::cout
        << "[PASS] Config broker store "
           "tests succeeded.\n";
    return 0;
}
