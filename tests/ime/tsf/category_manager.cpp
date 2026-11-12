// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/tsf/test_trace.h"
#include "common/test_support.h"
#include "mocks.h"
#include "ime/tsf/category_manager.h"

#include <thread>

namespace qiqi::test {
namespace {

struct CreationScenario {
    bool module_available = true;
    bool module_alive = false;
    bool release_order_correct = true;
    bool registration_correct = true;
    bool null_export = false;
    bool null_com = false;
    bool object_on_export_failure = false;
    HRESULT export_result = S_OK;
    HRESULT com_result = S_OK;
    HRESULT register_result = S_OK;
    unsigned int export_calls = 0;
    unsigned int com_calls = 0;
    unsigned int live_managers = 0;
    unsigned int destroyed_managers = 0;
    unsigned int module_releases = 0;
    unsigned int register_calls = 0;
};

class TestModuleReference final {
public:
    explicit TestModuleReference(CreationScenario& scenario) noexcept : scenario_(scenario) {
        scenario_.module_alive = scenario_.module_available;
    }
    ~TestModuleReference() noexcept {
        scenario_.release_order_correct = scenario_.release_order_correct && scenario_.live_managers == 0;
        if (scenario_.module_available) {
            scenario_.release_order_correct = scenario_.release_order_correct && scenario_.module_alive;
            scenario_.module_alive = false;
            ++scenario_.module_releases;
        }
    }
    TestModuleReference(const TestModuleReference&) = delete;
    TestModuleReference& operator=(const TestModuleReference&) = delete;
    TestModuleReference(TestModuleReference&&) = delete;
    TestModuleReference& operator=(TestModuleReference&&) = delete;
    explicit operator bool() const noexcept { return scenario_.module_available; }
    const CreationScenario* Get() const noexcept { return &scenario_; }
private:
    CreationScenario& scenario_;
};

struct TestCategoryManagerApi {
    CreationScenario& scenario;

    TestModuleReference AcquireModule() const noexcept { return TestModuleReference(scenario); }

    void CreateManager(gsl::not_null<ITfCategoryMgr**> output) const {
        auto manager = MakeComObject<MockTfCategoryMgr>();
        manager->register_guid_result_ = scenario.register_result;
        manager->release_context_ = &scenario;
        manager->on_final_release_ = [](MockTfCategoryMgr* object, void* context) noexcept {
            auto* state = static_cast<CreationScenario*>(context);
            state->release_order_correct =
                state->release_order_correct && state->module_alive == state->module_available;
            state->registration_correct = state->registration_correct && object->register_guid_calls_ <= 1;
            if (object->register_guid_calls_ != 0) {
                state->registration_correct = state->registration_correct &&
                    IsEqualGUID(object->registered_guid_, kGuidDisplayAttributeInput);
                ++state->register_calls;
            }
            --state->live_managers;
            ++state->destroyed_managers;
        };
        ++scenario.live_managers;
        *output = manager.Detach();
    }

    HRESULT CreateFromExport(const CreationScenario* module, ITfCategoryMgr** output) const {
        QIQI_TEST_CHECK(module == &scenario && scenario.module_alive);
        ++scenario.export_calls;
        if ((!scenario.null_export && SUCCEEDED(scenario.export_result)) || scenario.object_on_export_failure) {
            CreateManager(output);
        }
        return scenario.export_result;
    }

    HRESULT CreateFromCom(ITfCategoryMgr** output) const {
        QIQI_TEST_CHECK(scenario.live_managers == 0);
        ++scenario.com_calls;
        if (!scenario.null_com && SUCCEEDED(scenario.com_result)) {
            CreateManager(output);
        }
        return scenario.com_result;
    }
};

void TestCreationPaths() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    const auto run = [](CreationScenario& scenario, TfGuidAtom expected) {
        const auto result = qiqi::detail::RegisterCompositionDisplayAttributeAtom(TestCategoryManagerApi{scenario});
        QIQI_TEST_CHECK(result == expected);
        QIQI_TEST_CHECK(!scenario.module_alive && scenario.live_managers == 0);
        QIQI_TEST_CHECK(scenario.release_order_correct && scenario.registration_correct);
        QIQI_TEST_CHECK(scenario.module_releases == (scenario.module_available ? 1u : 0u));
    };
    CreationScenario exported;
    run(exported, 73);
    QIQI_TEST_CHECK(exported.export_calls == 1 && exported.com_calls == 0 && exported.register_calls == 1);

    CreationScenario no_module;
    no_module.module_available = false;
    run(no_module, 73);
    QIQI_TEST_CHECK(no_module.export_calls == 0 && no_module.com_calls == 1 && no_module.register_calls == 1);

    for (const HRESULT failure : {HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND), E_FAIL, E_OUTOFMEMORY}) {
        CreationScenario fallback;
        fallback.export_result = failure;
        run(fallback, 73);
        QIQI_TEST_CHECK(fallback.export_calls == 1 && fallback.com_calls == 1 && fallback.register_calls == 1);

        CreationScenario failed;
        failed.export_result = failure;
        failed.com_result = REGDB_E_CLASSNOTREG;
        run(failed, TF_INVALID_GUIDATOM);
        QIQI_TEST_CHECK(failed.export_calls == 1 && failed.com_calls == 1 && failed.register_calls == 0);
    }

    CreationScenario null_export;
    null_export.null_export = true;
    run(null_export, 73);
    QIQI_TEST_CHECK(null_export.com_calls == 1);

    CreationScenario null_com;
    null_com.module_available = false;
    null_com.null_com = true;
    run(null_com, TF_INVALID_GUIDATOM);
    QIQI_TEST_CHECK(null_com.com_calls == 1 && null_com.register_calls == 0);

    CreationScenario failed_output;
    failed_output.export_result = E_FAIL;
    failed_output.object_on_export_failure = true;
    run(failed_output, 73);
    QIQI_TEST_CHECK(failed_output.destroyed_managers == 2 && failed_output.register_calls == 1);

    for (const bool from_com : {false, true}) {
        CreationScenario registration_failure;
        registration_failure.module_available = !from_com;
        registration_failure.register_result = E_ACCESSDENIED;
        run(registration_failure, TF_INVALID_GUIDATOM);
        QIQI_TEST_CHECK(registration_failure.register_calls == 1 && registration_failure.destroyed_managers == 1);
    }
}

struct ComFallbackApi : qiqi::detail::CategoryManagerApi {
    static HRESULT CreateFromExport(HMODULE, ITfCategoryMgr**) noexcept { return E_FAIL; }
};

void TestNativeCreation() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    // A new thread has no COM apartment; keep an initial module reference to exercise the export path.
    std::thread worker([] {
        const ScopedModule module(::LoadLibraryExW(L"msctf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
        QIQI_TEST_CHECK(module);
        ULONG_PTR context_token = 0;
        QIQI_TEST_CHECK(::CoGetContextToken(&context_token) == CO_E_NOTINITIALIZED);
        const auto atom = qiqi::detail::RegisterCompositionDisplayAttributeAtom();
        QIQI_TEST_CHECK(atom != TF_INVALID_GUIDATOM);
        QIQI_TEST_CHECK(::CoGetContextToken(&context_token) == CO_E_NOTINITIALIZED);
        ComPtr<ITfCategoryMgr> manager;
        QIQI_TEST_CHECK(qiqi::detail::CategoryManagerApi::CreateFromExport(module.Get(), &manager) == S_OK);
        GUID guid{};
        QIQI_TEST_CHECK(manager->GetGUID(atom, &guid) == S_OK);
        QIQI_TEST_CHECK(IsEqualGUID(guid, kGuidDisplayAttributeInput));
    });
    worker.join();

    // The test runner initialized STA; force export failure and use the real COM fallback.
    const auto atom = qiqi::detail::RegisterCompositionDisplayAttributeAtom(ComFallbackApi{});
    QIQI_TEST_CHECK(atom != TF_INVALID_GUIDATOM);
    ComPtr<ITfCategoryMgr> manager;
    QIQI_TEST_CHECK(qiqi::detail::CategoryManagerApi::CreateFromCom(&manager) == S_OK);
    GUID guid{};
    QIQI_TEST_CHECK(manager->GetGUID(atom, &guid) == S_OK);
    QIQI_TEST_CHECK(IsEqualGUID(guid, kGuidDisplayAttributeInput));
}

} // namespace

void RunCategoryManagerTests() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
    TestCreationPaths();
    TestNativeCreation();
}

} // namespace qiqi::test
