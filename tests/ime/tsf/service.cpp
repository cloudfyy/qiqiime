#include "common/test_support.h"
#include "mocks.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/sound.h"
#include "common/file_util.h"
#include "ime/tsf/display_attribute.h"
#include "ime/tsf/edit_session.h"
#include "ime/tsf/tip_service.h"

#include <cstdlib>
#include <chrono>
#include <commctrl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <thread>
#include <tuple>

namespace qiqi::test {

void RunTsfServiceLifecycleTests(gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> mock_tm);

namespace {

class DiagnosticLangBarSink final : public ComObjectBase<DiagnosticLangBarSink, ITfLangBarItemSink> {
public:
    STDMETHODIMP OnUpdate(DWORD) noexcept override { return result; }
    HRESULT result = S_OK;
};

void TestModeSwitchCompositionPolicy(
    gsl::not_null<QiQiTextService*> service, gsl::not_null<MockTfThreadMgr*> manager, bool commit) {
    const QiQiConfig original_config = LoadConfig();
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(original_config));
        QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    });
    QiQiConfig config = original_config;
    config.commit_on_english_switch = commit;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnChange(kGuidCompartmentConfigVersion) == S_OK);
    std::array<BYTE, 256> original_keyboard{};
    QIQI_TEST_CHECK(::GetKeyboardState(original_keyboard.data()));
    const auto restore_keyboard = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(::SetKeyboardState(original_keyboard.data()));
    });
    std::array<BYTE, 256> clean_keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_keyboard.data()));

    enum class Switch { Shift, PreservedKey, LangBarClick, LangBarMenu, OpenClose, Conversion };
    for (const bool deferred : {false, true}) {
        for (const auto method : {Switch::Shift, Switch::PreservedKey, Switch::LangBarClick,
                                  Switch::LangBarMenu, Switch::OpenClose, Switch::Conversion}) {
            for (const std::wstring code : {L"a", L"ab", L"abc", L"zhello", L"z"}) {
                const std::wstring expected = commit ? code : L"";
                const auto queue = deferred ? std::make_shared<MockEditSessionQueue>() : nullptr;
                auto context = MakeComObject<MockTfContext>(queue);
                BOOL eaten = FALSE;
                for (const wchar_t ch : code) {
                    QIQI_TEST_CHECK(service->OnKeyDown(
                        context.Get(), gsl::narrow_cast<WPARAM>(ch - L'a' + L'A'), 0, &eaten) == S_OK && eaten);
                }
                if (queue) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                const auto composition = context->last_composition_;
                QIQI_TEST_CHECK(composition && !composition->ended_);
                composition->end_callback_ = [&](TfEditCookie) {
                    BOOL nested_eaten = TRUE;
                    QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'D', 0, &nested_eaten) == S_OK &&
                                    !nested_eaten);
                    const auto requests = context->request_edit_session_calls_;
                    const VARIANT value = MakeInt4Variant(0);
                    QIQI_TEST_CHECK(manager->openclose_compartment_->SetValue(1, &value) == S_OK);
                    QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests);
                };
                switch (method) {
                    case Switch::Shift:
                        QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
                        QIQI_TEST_CHECK(service->OnTestKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && eaten);
                        QIQI_TEST_CHECK(service->OnKeyUp(context.Get(), VK_SHIFT, 0, &eaten) == S_OK && eaten);
                        break;
                    case Switch::PreservedKey:
                        QIQI_TEST_CHECK(service->OnPreservedKey(
                            context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
                        break;
                    case Switch::LangBarClick:
                    case Switch::LangBarMenu: {
                        ComPtr<ITfLangBarItemButton> button;
                        QIQI_TEST_CHECK(manager->langbar_item_.As(&button) == S_OK && button);
                        if (method == Switch::LangBarClick) {
                            QIQI_TEST_CHECK(button->OnClick(TF_LBI_CLK_LEFT, POINT{}, nullptr) == S_OK);
                        } else {
                            QIQI_TEST_CHECK(button->OnMenuSelect(
                                static_cast<UINT>(LangBarMenuId::ToggleChinese)) == S_OK);
                        }
                        break;
                    }
                    case Switch::OpenClose: {
                        const VARIANT value = MakeInt4Variant(0);
                        QIQI_TEST_CHECK(manager->openclose_compartment_->SetValue(1, &value) == S_OK);
                        break;
                    }
                    case Switch::Conversion: {
                        const VARIANT value = MakeInt4Variant(
                            manager->convmode_compartment_->last_val_ & ~TF_CONVERSIONMODE_NATIVE);
                        QIQI_TEST_CHECK(manager->convmode_compartment_->SetValue(1, &value) == S_OK);
                        break;
                    }
                }
                QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 0);
                if (queue) {
                    while (queue->PendingCount() != 0) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                }
                composition->end_callback_ = {};
                QIQI_TEST_CHECK(context->range_->text_ == expected);
                QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                if (commit) QIQI_TEST_CHECK(context->prop_->clear_calls_ != 0);
                BOOL shown = TRUE;
                QIQI_TEST_CHECK(manager->last_ui_element_ &&
                                manager->last_ui_element_->IsShown(&shown) == S_OK && !shown);
                QIQI_TEST_CHECK(service->OnTestKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'D', 0, &eaten) == S_OK && !eaten);
                QIQI_TEST_CHECK(context->range_->text_ == expected);
                QIQI_TEST_CHECK(service->OnPreservedKey(
                    context.Get(), kGuidPreservedKeyToggleMode, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
                QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), VK_ESCAPE, 0, &eaten) == S_OK && eaten);
                if (queue) {
                    while (queue->PendingCount() != 0) QIQI_TEST_CHECK(queue->RunNext() == S_OK);
                }
                QIQI_TEST_CHECK(context->range_->text_.empty());
            }
        }
    }
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
}

void TestShiftTapTracking() {
    ShiftTapTracker tracker;
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.Sequence() == 0 && tracker.ElapsedMs(1000) == 0);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(1000, {})) == "no-candidate");

    for (const uint64_t elapsed : {uint64_t{0}, uint64_t{349}, uint64_t{350}, uint64_t{351}}) {
        tracker.Reset();
        const auto sequence = tracker.Sequence();
        QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1000, false, true, {})) == "armed");
        QIQI_TEST_CHECK(tracker.Sequence() == sequence + 1);
        const char* reason = tracker.ReleaseReason(1000 + elapsed, {});
        QIQI_TEST_CHECK((reason == nullptr) == (elapsed <= 350));
        if (reason) QIQI_TEST_CHECK(std::string_view(reason) == "timeout");
        QIQI_TEST_CHECK(tracker.IsCandidate() && tracker.ElapsedMs(1000 + elapsed) == elapsed);
    }

    tracker.Reset();
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1000, false, true, {})) == "armed");
    const auto sequence = tracker.Sequence();
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1220, false, true, {})) == "press-preserved");
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1349, true, true, {})) == "repeat-preserved");
    QIQI_TEST_CHECK(tracker.ReleaseReason(1350, {}) == nullptr && tracker.Sequence() == sequence);
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(1351, true, true, {})) == "repeat-timeout");
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(1351) == 351);
    tracker.OnKeyDown(1352, true, true, {});
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.Sequence() == sequence);
    tracker.Reset();
    QIQI_TEST_CHECK(tracker.ElapsedMs(1352) == 0 && tracker.Sequence() == sequence);

    for (uint32_t mask = 0; mask < 8; ++mask) {
        const ShiftTapModifiers modifiers{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
        for (const bool enabled : {false, true}) {
            tracker.Reset();
            const char* expected = !enabled ? "disabled" : modifiers.ctrl ? "ctrl" :
                                   modifiers.alt ? "alt" : modifiers.mouse ? "mouse-button" : "armed";
            QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(2000, false, enabled, modifiers)) == expected);
            QIQI_TEST_CHECK(tracker.IsCandidate() == (enabled && mask == 0));
        }

        tracker.Reset();
        tracker.OnKeyDown(2000, false, true, {});
        const char* reason = tracker.ReleaseReason(2100, modifiers);
        if (mask == 0) {
            QIQI_TEST_CHECK(reason == nullptr);
        } else {
            const char* expected = modifiers.mouse ? "mouse-button" : modifiers.ctrl ? "ctrl" : "alt";
            QIQI_TEST_CHECK(reason && std::string_view(reason) == expected);
        }
        QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(2351, modifiers)) == "timeout");
    }

    tracker.Reset();
    tracker.OnKeyDown(3000, false, true, {});
    QIQI_TEST_CHECK(std::string_view(tracker.OnKeyDown(3100, true, true, {false, false, true})) ==
                    "repeat-mouse-button");
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(3100) == 100);
    tracker.OnKeyDown(3200, false, true, {});
    tracker.Disarm();
    QIQI_TEST_CHECK(!tracker.IsCandidate() && tracker.ElapsedMs(3250) == 50);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(3250, {})) == "no-candidate");
    tracker.Reset();
    tracker.OnKeyDown(3300, true, true, {});
    QIQI_TEST_CHECK(!tracker.IsCandidate());
    tracker.OnKeyDown(0, false, true, {});
    QIQI_TEST_CHECK(tracker.ReleaseReason(350, {}) == nullptr);
    QIQI_TEST_CHECK(std::string_view(tracker.ReleaseReason(351, {})) == "timeout");
}

void TestFocusPerformanceLogs() {
    const auto executable = GetModuleFilePath(nullptr);
    QIQI_TEST_CHECK(!executable.empty());
    std::wstring command = L"\"" + executable.wstring() + L"\" --tsf-focus-diagnostics";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                                    DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &startup, &info));
    const ScopedKernelHandle process(info.hProcess);
    const ScopedKernelHandle thread(info.hThread);
    std::vector<std::wstring> focus_records;
    std::vector<std::wstring> reload_records;
    std::vector<std::wstring> cleanup_records;
    std::vector<std::wstring> lifecycle_records;
    std::vector<std::wstring> preserved_key_records;
    std::vector<std::wstring> shift_records;
    std::vector<std::wstring> indicator_records;
    std::vector<std::wstring> fsm_records;
    bool checking_shift_filter = false;
    bool checking_fsm = false;
    bool exited = false;
    while (!exited) {
        DEBUG_EVENT event{};
        QIQI_TEST_CHECK(::WaitForDebugEventEx(&event, 30'000));
        DWORD continuation = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            const ScopedFileHandle image(event.u.CreateProcessInfo.hFile);
        } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
            const ScopedFileHandle image(event.u.LoadDll.hFile);
        } else if (event.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT) {
            const auto& output = event.u.DebugString;
            SIZE_T read = 0;
            std::wstring message;
            if (output.fUnicode) {
                QIQI_TEST_CHECK(output.nDebugStringLength % sizeof(wchar_t) == 0);
                message.resize(output.nDebugStringLength / sizeof(wchar_t));
                QIQI_TEST_CHECK(::ReadProcessMemory(process.Get(), output.lpDebugStringData, message.data(),
                                                    message.size() * sizeof(wchar_t), &read));
                QIQI_TEST_CHECK(read == message.size() * sizeof(wchar_t));
            } else {
                std::string bytes(output.nDebugStringLength, '\0');
                QIQI_TEST_CHECK(::ReadProcessMemory(process.Get(), output.lpDebugStringData, bytes.data(),
                                                    bytes.size(), &read));
                QIQI_TEST_CHECK(read == bytes.size());
                message.assign(bytes.begin(), bytes.end());
            }
            QIQI_TEST_CHECK(!message.contains(L"SENSITIVE_DIAGNOSTIC_SENTINEL"));
            if (message.contains(L"FsmDiagnostics(begin)")) checking_fsm = true;
            if (message.contains(L"FsmDiagnostics(end)")) checking_fsm = false;
            if (checking_fsm && message.contains(L"FSM transition:")) {
                fsm_records.push_back(message);
            }
            if (message.contains(L"ShiftLogFilter(begin)")) checking_shift_filter = true;
            if (message.contains(L"ShiftLogFilter(end)")) checking_shift_filter = false;
            const bool shift_diagnostic = message.contains(L"ShiftTap(") || message.contains(L"ShiftTapContext(");
            const bool indicator_diagnostic = message.contains(L"SyncTsfCompartments(") ||
                                               message.contains(L"LangBarButton::");
            if (checking_shift_filter) QIQI_TEST_CHECK(!shift_diagnostic && !indicator_diagnostic);
            if (shift_diagnostic) shift_records.push_back(message);
            if (indicator_diagnostic) indicator_records.push_back(message);
            if (message.contains(L"FocusPerformance(")) focus_records.push_back(message);
            if (message.contains(L"ReloadConfigIfModified(focus_id=")) reload_records.push_back(message);
            if (message.contains(L"TsfCleanup(")) cleanup_records.push_back(message);
            if (message.contains(L"TsfLifecycle(")) lifecycle_records.push_back(message);
            if (message.contains(L"InitKeyEventSink: PreserveKey") ||
                message.contains(L"UninitKeyEventSink: UnpreserveKey") ||
                message.contains(L"UninitKeyEventSink::UnadviseKeyEventSink")) {
                preserved_key_records.push_back(message);
            }
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) {
                continuation = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            QIQI_TEST_CHECK(event.u.ExitProcess.dwExitCode == 0);
            exited = true;
        }
        QIQI_TEST_CHECK(::ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation));
    }

    // Observe actual logger output without adding a production test sink or changing ETW initialization.
    QIQI_TEST_CHECK(focus_records.size() == 12 && reload_records.size() == 10);
    const std::wregex durations(LR"(reload_ns=([0-9]+), position_ns=([0-9]+), total_ns=([0-9]+))");
    for (size_t i = 0; i < focus_records.size(); ++i) {
        const auto& record = gsl::at(focus_records, i);
        QIQI_TEST_CHECK(record.contains(std::format(L"FocusPerformance(id={}, ", i + 1)));
        QIQI_TEST_CHECK(record.contains(L"completed=true)"));
        std::wsmatch match;
        QIQI_TEST_CHECK(std::regex_search(record, match, durations));
        const auto reload_ns = std::stoull(match.str(1));
        const auto position_ns = std::stoull(match.str(2));
        const auto total_ns = std::stoull(match.str(3));
        QIQI_TEST_CHECK(total_ns > 0 && total_ns >= reload_ns + position_ns);
        if (i != 7 && i != 8) {
            QIQI_TEST_CHECK(reload_ns > 0 && position_ns > 0);
            QIQI_TEST_CHECK(record.contains(L"has_focus=true") && record.contains(L"reload_checked=true"));
        } else {
            QIQI_TEST_CHECK(reload_ns == 0 && position_ns == 0);
            QIQI_TEST_CHECK(record.contains(L"has_focus=false") && record.contains(L"reload_checked=false"));
        }
    }
    QIQI_TEST_CHECK(gsl::at(focus_records, 0).contains(L"source=thread-manager") &&
                    gsl::at(focus_records, 0).contains(L"same_state=false"));
    QIQI_TEST_CHECK(gsl::at(focus_records, 1).contains(L"source=key-event") &&
                    gsl::at(focus_records, 1).contains(L"same_state=true"));
    QIQI_TEST_CHECK(gsl::at(focus_records, 2).contains(L"position_changed=true"));
    QIQI_TEST_CHECK(gsl::at(focus_records, 3).contains(L"position_changed=false"));
    QIQI_TEST_CHECK(gsl::at(focus_records, 7).contains(L"same_state=false"));
    QIQI_TEST_CHECK(gsl::at(focus_records, 8).contains(L"same_state=true"));
    for (size_t i = 0; i < 7; ++i) {
        const auto& record = gsl::at(reload_records, i);
        const bool config_changed = i == 4 || i == 6;
        const bool dictionary_changed = i == 5 || i == 6;
        QIQI_TEST_CHECK(record.contains(std::format(L"focus_id={}, config_status={}", i + 1,
                                                   config_changed ? L"loaded" : L"unchanged")));
        QIQI_TEST_CHECK(record.contains(std::format(L"dictionary_modified={}, dictionary_reload_calls={}",
                                                   dictionary_changed, static_cast<unsigned int>(config_changed) +
                                                   static_cast<unsigned int>(dictionary_changed))));
        QIQI_TEST_CHECK(record.contains(L"mapped_dicts=1, phrase_frequency_status=not-requested, completed=true)"));
    }
    QIQI_TEST_CHECK(gsl::at(reload_records, 7).contains(L"focus_id=10, config_status=loaded"));
    QIQI_TEST_CHECK(gsl::at(reload_records, 7).contains(L"phrase_frequency_status=load-failed, completed=true"));
    QIQI_TEST_CHECK(gsl::at(reload_records, 8).contains(L"focus_id=11, config_status=loaded"));
    QIQI_TEST_CHECK(gsl::at(reload_records, 8).contains(L"phrase_frequency_status=loaded, completed=true"));
    QIQI_TEST_CHECK(gsl::at(reload_records, 9).contains(L"focus_id=12, config_status=sequence-unavailable"));
    QIQI_TEST_CHECK(gsl::at(reload_records, 9).contains(L"dictionary_reload_calls=0"));

    QIQI_TEST_CHECK(!cleanup_records.empty());
    const std::wregex cleanup_identity(LR"(TsfCleanup\(service=([0-9]+), cleanup=([0-9]+), stage=)");
    std::vector<std::wstring> identities;
    for (const auto& record : cleanup_records) {
        std::wsmatch identity;
        QIQI_TEST_CHECK(std::regex_search(record, identity, cleanup_identity));
        const auto key = identity.str();
        if (std::find(identities.begin(), identities.end(), key) == identities.end()) identities.push_back(key);
    }
    // Seven Debug scenarios; the following Info/Off scenarios must produce no cleanup diagnostics.
    QIQI_TEST_CHECK(identities.size() == 7);
    const auto find_stage = [&](size_t scenario, std::wstring_view stage) {
        for (size_t i = 0; i < cleanup_records.size(); ++i) {
            const auto& record = gsl::at(cleanup_records, i);
            if (record.contains(gsl::at(identities, scenario) + std::wstring(stage))) return i;
        }
        return cleanup_records.size();
    };
    const auto check_stage = [&](size_t scenario, std::wstring_view stage) {
        const size_t index = find_stage(scenario, stage);
        if (index == cleanup_records.size()) {
            std::wcerr << L"[FAIL] Cleanup diagnostic scenario " << scenario << L": missing " << stage << L'\n';
        }
        QIQI_TEST_CHECK(index < cleanup_records.size());
        return index;
    };
    const auto sync = check_stage(0, L"sync-request");
    const auto callback = check_stage(0, L"callback-enter");
    const auto ended = check_stage(0, L"end-composition, hr=0x00000000");
    const auto sync_result = check_stage(0, L"sync-result");
    QIQI_TEST_CHECK(sync < callback && callback < ended && ended < sync_result);
    const auto queued = check_stage(1, L"async-result");
    QIQI_TEST_CHECK(gsl::at(cleanup_records, queued).contains(L"queued=true"));
    QIQI_TEST_CHECK(queued < check_stage(1, L"write-forced-async"));
    QIQI_TEST_CHECK(queued < check_stage(1, L"callback-enter"));
    check_stage(1, L"attribute-clear, hr=0x00000000");
    check_stage(1, L"end-composition, hr=0x00000000");
    check_stage(2, L"host-terminated");
    check_stage(2, L"host-attribute-clear, hr=0x00000000");
    check_stage(2, L"skip-host-terminated");
    QIQI_TEST_CHECK(find_stage(2, L"end-composition") == cleanup_records.size());
    check_stage(3, L"async-rejected, hr=0x80070005");
    QIQI_TEST_CHECK(find_stage(3, L"callback-enter") == cleanup_records.size());
    const auto released = check_stage(4, L"released");
    QIQI_TEST_CHECK(gsl::at(cleanup_records, released).contains(L"async_requested=true, started=false, finished=false"));
    QIQI_TEST_CHECK(find_stage(4, L"callback-enter") == cleanup_records.size());
    check_stage(5, L"sync-failed-no-fallback");
    QIQI_TEST_CHECK(find_stage(5, L"async-request") == cleanup_records.size());
    QIQI_TEST_CHECK(find_stage(5, L"callback-enter") == cleanup_records.size());
    check_stage(6, L"attribute-clear, hr=0x80004005");
    check_stage(6, L"end-composition, hr=0x80070005");
    const auto failed_callback = check_stage(6, L"released");
    QIQI_TEST_CHECK(gsl::at(cleanup_records, failed_callback).contains(L"started=true, finished=true"));
    QIQI_TEST_CHECK(std::any_of(lifecycle_records.begin(), lifecycle_records.end(), [](const auto& record) {
        return record.contains(L"stage=stale-edit-skipped") &&
               record.contains(L"active=false, target_matches=false");
    }));
    QIQI_TEST_CHECK(std::any_of(lifecycle_records.begin(), lifecycle_records.end(), [](const auto& record) {
        return record.contains(L"stage=document-target, source=push, retained=true, covered_by_modal=true");
    }));
    QIQI_TEST_CHECK(std::any_of(lifecycle_records.begin(), lifecycle_records.end(), [](const auto& record) {
        return record.contains(L"stage=context-pop, current=false");
    }));
    QIQI_TEST_CHECK(std::any_of(preserved_key_records.begin(), preserved_key_records.end(), [](const auto& record) {
        return record.contains(
            std::format(L"PreserveKey(vkey=0x20, mod=0x{:X}) already registered; skipping ownership (hr=0x{:08x})",
                        gsl::narrow_cast<unsigned int>(TF_MOD_CONTROL),
                        gsl::narrow_cast<unsigned int>(TF_E_ALREADY_EXISTS)));
    }));
    QIQI_TEST_CHECK(std::any_of(preserved_key_records.begin(), preserved_key_records.end(), [](const auto& record) {
        return record.contains(
            std::format(L"PreserveKey(vkey=0xBE, mod=0x{:X}) failed (hr=0x{:08x})",
                        gsl::narrow_cast<unsigned int>(TF_MOD_CONTROL),
                        gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY)));
    }));
    QIQI_TEST_CHECK(std::any_of(preserved_key_records.begin(), preserved_key_records.end(), [](const auto& record) {
        return record.contains(
            std::format(L"UninitKeyEventSink: UnpreserveKey(vkey=0x20, mod=0x{:X}) failed (hr=0x{:08x})",
                        gsl::narrow_cast<unsigned int>(TF_MOD_SHIFT),
                        gsl::narrow_cast<unsigned int>(CONNECT_E_NOCONNECTION)));
    }));
    QIQI_TEST_CHECK(std::any_of(preserved_key_records.begin(), preserved_key_records.end(), [](const auto& record) {
        return record.contains(
            std::format(L"UninitKeyEventSink::UnadviseKeyEventSink failed (hr=0x{:08x})",
                        gsl::narrow_cast<unsigned int>(E_FAIL)));
    }));
    for (const auto* reason : {L"armed", L"accepted", L"toggle-complete", L"no-candidate", L"disabled",
                              L"ctrl", L"alt", L"mouse-button", L"timeout", L"repeat-preserved", L"press-preserved",
                              L"other-key-cancelled", L"context-reset-cancelled", L"inactive"}) {
        QIQI_TEST_CHECK(std::any_of(shift_records.begin(), shift_records.end(), [&](const auto& record) {
            return record.contains(std::format(L"reason={},", reason));
        }));
    }
    for (const auto* key : {L"left", L"right", L"generic"}) {
        QIQI_TEST_CHECK(std::any_of(shift_records.begin(), shift_records.end(), [&](const auto& record) {
            return record.contains(std::format(L"key={},", key));
        }));
    }
    QIQI_TEST_CHECK(std::any_of(shift_records.begin(), shift_records.end(), [](const auto& record) {
        return record.contains(L"stage=OnTestKeyDown, key=generic, reason=armed") &&
               record.contains(L"attempt=1,") && record.contains(L"eaten=false, candidate=true") &&
               record.contains(L"max_ms=350, enabled=true, active=true");
    }));
    QIQI_TEST_CHECK(std::any_of(shift_records.begin(), shift_records.end(), [](const auto& record) {
        return record.contains(L"stage=OnKeyDown, key=generic, reason=armed,") &&
               record.contains(L"eaten=false, candidate=true,");
    }));
    for (const auto* expected : {L"compartment=openclose, value=0, hr=0x00000000",
                                 L"compartment=conversion,", L"hr=0x80004005",
                                 L"LangBarButton::UpdateState(chinese_before=",
                                 L"LangBarButton::OnUpdate(chinese=false, hr=0x00000001)",
                                 L"LangBarButton::OnUpdate(chinese=true, hr=0x80070005)",
                                 L"LangBarButton::GetIcon(chinese=false, created=true)",
                                 L"LangBarButton::GetText(chinese=false, hr=0x00000000)"}) {
        QIQI_TEST_CHECK(std::any_of(indicator_records.begin(), indicator_records.end(), [&](const auto& record) {
            return record.contains(expected);
        }));
    }
    QIQI_TEST_CHECK(!checking_shift_filter);
    QIQI_TEST_CHECK(!checking_fsm);
    constexpr std::array<std::wstring_view, 16> expected_fsm = {
        L"state_before=1 -> op=1 state_after=2",
        L"state_before=2 -> op=1 state_after=3",
        L"state_before=3 -> op=1 state_after=4",
        L"state_before=4 -> op=2 state_after=1",
        L"state_before=1 -> op=1 state_after=2",
        L"state_before=2 -> op=1 state_after=3",
        L"state_before=3 -> op=1 state_after=4",
        L"state_before=4 -> op=2 state_after=5",
        L"state_before=5 -> op=3 state_after=5",
        L"state_before=5 -> op=8 state_after=5",
        L"state_before=1 -> op=1 state_after=2",
        L"state_before=2 -> op=1 state_after=3",
        L"state_before=3 -> op=1 state_after=4",
        L"state_before=4 -> op=2 state_after=1",
        L"state_before=1 -> op=16 state_after=6",
        L"state_before=6 -> op=18 state_after=1"
    };
    QIQI_TEST_CHECK(fsm_records.size() == expected_fsm.size());
    for (size_t i = 0; i < expected_fsm.size(); ++i) {
        QIQI_TEST_CHECK(gsl::at(fsm_records, i).contains(gsl::at(expected_fsm, i)));
    }
}

void TestProcessDictCacheLifetime() {
    const auto dict_path = std::filesystem::temp_directory_path() /
                           std::format(L"qiqi_process_cache_test_{}.txt", ::GetCurrentProcessId());
    std::filesystem::path qdict_path = dict_path;
    qdict_path.replace_extension(L".qdict");
    const auto cleanup_files = gsl::finally([&]() noexcept {
        WubiMappedDict::ClearProcessCache();
        std::error_code ec;
        std::filesystem::remove(dict_path, ec);
        std::filesystem::remove(qdict_path, ec);
    });
    {
        std::ofstream out(dict_path, std::ios::binary | std::ios::trunc);
        out << "xvwq 共享词一 共享词二\n";
        out.close();
        QIQI_TEST_CHECK(out.good());
    }

    const QiQiConfig saved_config = LoadConfig(g_dll_instance);
    auto config = saved_config;
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    config.user_dict_path = dict_path.wstring();
    config.default_chinese_mode = true;
    config.default_full_width = false;
    config.show_status_bar = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    const auto restore_config = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK(SaveConfigToRegistry(saved_config));
    });

    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0);

    auto service_a = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_a = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 1);
    auto cached_dict = WubiMappedDict::LoadFromTextOrCache(dict_path);
    QIQI_TEST_CHECK(cached_dict != nullptr);
    const std::weak_ptr<const WubiMappedDict> weak_dict = cached_dict;

    auto service_b = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_b = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service_a->Activate(manager_a.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(service_b->Activate(manager_b.Get(), 2) == S_OK);

    // Deactivating service_a releases its own engine references, but preserves the process-wide dictionary cache
    // so service_b and subsequent reactivations do not re-map the dictionary file from disk.
    QIQI_TEST_CHECK(service_a->Deactivate() == S_OK);
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 1 && !weak_dict.expired());
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(dict_path) == cached_dict);

    // service_b continues querying candidates from the shared dictionary after service_a deactivates.
    std::array<BYTE, 256> clean_kb{};
    QIQI_TEST_CHECK(::SetKeyboardState(clean_kb.data()));
    auto context_b = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_b->OnKeyDown(context_b.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_b->OnKeyDown(context_b.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_b->range_->text_ == L"共享词一");

    // Reactivating a service with a non-BuiltinOnly primary dictionary reuses the existing in-process cache entry.
    config.primary_dict = PrimaryDict::Jidian;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    auto service_c = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager_c = MakeComObject<MockTfThreadMgr>();
    QIQI_TEST_CHECK(service_c->Activate(manager_c.Get(), 3) == S_OK);
    QIQI_TEST_CHECK(service_c->Deactivate() == S_OK);
    QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(dict_path) == cached_dict);
    QIQI_TEST_CHECK(service_c->Activate(manager_c.Get(), 3) == S_OK);
    auto context_c = MakeComObject<MockTfContext>();
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_c->OnKeyDown(context_c.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_c->OnKeyDown(context_c.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_c->range_->text_ == L"共享词一");

    // BuiltinOnly service with user_dict_path also reloads external user dictionary upon reactivation.
    config.primary_dict = PrimaryDict::BuiltinOnly;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service_a->Activate(manager_a.Get(), 1) == S_OK);
    auto context_a = MakeComObject<MockTfContext>();
    for (const UINT key : std::array<UINT, 4>{'X', 'V', 'W', 'Q'}) {
        QIQI_TEST_CHECK(service_a->OnKeyDown(context_a.Get(), key, 0, &eaten) == S_OK && eaten);
    }
    QIQI_TEST_CHECK(service_a->OnKeyDown(context_a.Get(), VK_SPACE, 0, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(context_a->range_->text_ == L"共享词一");
    QIQI_TEST_CHECK(service_a->Deactivate() == S_OK);

    // Module-detach cleanup (or explicit ClearProcessCache) releases the process cache; once all services release
    // their external dictionaries, the underlying WubiMappedDict object is destroyed.
    cached_dict.reset();
    QIQI_TEST_CHECK(!weak_dict.expired());
    QIQI_TEST_CHECK(service_b->Deactivate() == S_OK);
    QIQI_TEST_CHECK(service_c->Deactivate() == S_OK);
    service_a.Reset();
    service_b.Reset();
    service_c.Reset();
    QIQI_TEST_CHECK(!weak_dict.expired() && WubiMappedDict::ProcessCacheSize() > 0);
    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(WubiMappedDict::ProcessCacheSize() == 0 && weak_dict.expired());
}

void TestPreservedKeyRegistrationLifetime(gsl::not_null<QiQiTextService*> service) {
    auto manager = MakeComObject<MockTfThreadMgr>();
    const auto reset_counters = [&]() {
        manager->advise_key_sink_calls_ = 0;
        manager->unadvise_key_sink_calls_ = 0;
        manager->preserve_key_calls_ = 0;
        manager->unpreserve_key_calls_ = 0;
        manager->preserved_keys_.clear();
        manager->unpreserved_keys_.clear();
        manager->preserve_key_callback_ = {};
        manager->unpreserve_key_callback_ = {};
        manager->unadvise_key_sink_result_ = S_OK;
        manager->unpreserve_key_result_ = S_OK;
    };

    // 1. All succeed: exact 4 keys registered with client ID, unregistered on Deactivate, no duplicate on second Deactivate.
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 11) == S_OK);
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 1 && manager->key_sink_client_id_ == 11 && manager->key_event_sink_);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 4);
    for (const auto& entry : manager->preserved_keys_) {
        QIQI_TEST_CHECK(entry.client_id == 11);
    }
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(manager->unadvise_key_sink_calls_ == 1 && manager->last_unadvised_key_sink_client_id_ == 11 &&
                    !manager->key_event_sink_);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);

    // 2. Rollback when AdviseKeyEventSink or QueryInterface(ITfKeystrokeMgr) fails: no PreserveKey, UnpreserveKey, or UnadviseKeyEventSink.
    reset_counters();
    manager->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 12) == E_FAIL);
    manager->fail_query_key_mgr_ = false;
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 0 && manager->unpreserve_key_calls_ == 0);

    manager->fail_key_sink_ = true;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 12) == E_FAIL);
    manager->fail_key_sink_ = false;
    QIQI_TEST_CHECK(manager->advise_key_sink_calls_ == 1 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 0 && manager->unpreserve_key_calls_ == 0);

    // 3. Partial success: TF_E_ALREADY_EXISTS and E_OUTOFMEMORY are NOT claimed as owned and NOT unpreserved on Deactivate.
    reset_counters();
    manager->preserve_key_callback_ = [&](TfClientId tid, REFGUID rguid, const TF_PRESERVEDKEY& pk) -> HRESULT {
        if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) {
            return TF_E_ALREADY_EXISTS;
        }
        if (IsEqualGUID(rguid, kGuidPreservedKeyTogglePunctuation)) {
            return E_OUTOFMEMORY;
        }
        manager->preserved_keys_.push_back({tid, rguid, pk});
        return S_OK;
    };
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 13) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 2);
    // Preserved key dispatch remains functional.
    auto context = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(service->OnPreservedKey(context.Get(), kGuidPreservedKeyToggleFullWidth, &eaten) == S_OK && eaten);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 2 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(manager->unpreserved_keys_.size() == 2);
    QIQI_TEST_CHECK(IsEqualGUID(gsl::at(manager->unpreserved_keys_, 0).guid, kGuidPreservedKeyToggleFullWidth));
    QIQI_TEST_CHECK(IsEqualGUID(gsl::at(manager->unpreserved_keys_, 1).guid, kGuidPreservedKeyAddUserWord));
    QIQI_TEST_CHECK(manager->unadvise_key_sink_calls_ == 1 && manager->last_unadvised_key_sink_client_id_ == 13);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 2 && manager->unadvise_key_sink_calls_ == 1);

    // 4. All PreserveKey fail (e.g. TF_E_ALREADY_EXISTS / E_FAIL): key sink is still unadvised on Deactivate, 0 UnpreserveKey calls.
    reset_counters();
    manager->preserve_key_callback_ = [](TfClientId, REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        return IsEqualGUID(rguid, kGuidPreservedKeyToggleMode) ? TF_E_ALREADY_EXISTS : E_FAIL;
    };
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 14) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.empty());
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 1);

    // 5. UnpreserveKey and UnadviseKeyEventSink failures on Deactivate still retire local ownership and allow clean reactivation.
    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 15) == S_OK);
    QIQI_TEST_CHECK(manager->preserved_keys_.size() == 4);
    manager->unpreserve_key_callback_ = [&](REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        return IsEqualGUID(rguid, kGuidPreservedKeyToggleFullWidth) ? CONNECT_E_NOCONNECTION : E_FAIL;
    };
    manager->unadvise_key_sink_result_ = E_FAIL;
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);

    // 6. QueryInterface(ITfKeystrokeMgr) failure during UninitKeyEventSink retires local ownership without retrying on second Deactivate,
    //    and subsequent reactivation succeeds cleanly.
    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 16) == S_OK);
    manager->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    manager->fail_query_key_mgr_ = false;
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 0 && manager->unadvise_key_sink_calls_ == 0);

    reset_counters();
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 17) == S_OK);
    QIQI_TEST_CHECK(manager->preserve_key_calls_ == 4 && manager->preserved_keys_.size() == 4);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(manager->unpreserve_key_calls_ == 4 && manager->unadvise_key_sink_calls_ == 1);
}

void TestQiQiTextServiceLifecycleAndKeyFlow() {
    constexpr const wchar_t* kTsfTestRegSubKey = L"Software\\QiQiIME_TsfUnitTest";
    ::RegDeleteTreeW(HKEY_CURRENT_USER, kTsfTestRegSubKey);
    SetConfigRegistrySubKeyOverrideForTest(kTsfTestRegSubKey);

    wchar_t* orig_localappdata = nullptr;
    size_t orig_len = 0;
    std::ignore = _wdupenv_s(&orig_localappdata, &orig_len, L"LOCALAPPDATA");
    const std::unique_ptr<wchar_t, decltype(&std::free)> orig_localappdata_owner(
        orig_localappdata, &std::free);
    std::error_code appdata_ec;
    const auto temp_appdata = std::filesystem::temp_directory_path() / L"QiQiIME_TsfTestAppData";
    std::filesystem::remove_all(temp_appdata, appdata_ec);
    _wputenv_s(L"LOCALAPPDATA", temp_appdata.wstring().c_str());
    const auto restore_appdata = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTsfTestRegSubKey);
        if (orig_localappdata_owner) {
            _wputenv_s(L"LOCALAPPDATA", orig_localappdata_owner.get());
        }
        std::error_code clean_ec;
        std::filesystem::remove_all(temp_appdata, clean_ec);
    });

    std::array<BYTE, 256> clean_kb_state{};
    ::SetKeyboardState(clean_kb_state.data());
    SetMuteAudioOutputForTest(true);
    ResetLastPlayedSoundForTest();

    auto service = MakeComObject<QiQiTextService>(g_dll_instance);

    // QueryInterface coverage for all supported interfaces
    QIQI_TEST_CHECK(service->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    void* out = nullptr;
    QIQI_TEST_CHECK(service->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE && out == nullptr);

    ComPtr<ITfTextInputProcessor> tip_base;
    ComPtr<ITfTextInputProcessorEx> tip_ex;
    ComPtr<ITfThreadMgrEventSink> tm_sink;
    ComPtr<ITfKeyEventSink> key_sink;
    ComPtr<ITfCompositionSink> comp_sink;
    ComPtr<ITfTextLayoutSink> layout_sink;
    ComPtr<ITfFunctionProvider> func_prov;
    ComPtr<ITfFunction> fn_base;
    ComPtr<ITfFnConfigure> fn_cfg;
    ComPtr<ITfDisplayAttributeProvider> disp_attr_prov;
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_base))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tip_ex))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&key_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&comp_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&layout_sink))));
    layout_sink.Reset();
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&func_prov))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&fn_base))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&fn_cfg))));
    QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&disp_attr_prov))));
    fn_base.Reset();

    // Test ITfFunctionProvider & ITfFnConfigure
    QIQI_TEST_CHECK(func_prov->GetType(nullptr) == E_INVALIDARG);
    GUID fp_guid{};
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetType(&fp_guid)) && IsEqualGUID(fp_guid, kClsidQiQiTextService));
    QIQI_TEST_CHECK(func_prov->GetDescription(nullptr) == E_INVALIDARG);
    ScopedBstr fp_desc;
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetDescription(fp_desc.Put())) && fp_desc.IsValid());
    QIQI_TEST_CHECK(func_prov->GetFunction(GUID_NULL, IID_ITfFnConfigure, nullptr) == E_INVALIDARG);
    ComPtr<IUnknown> fn_unk;
    QIQI_TEST_CHECK(func_prov->GetFunction(kGuidProfileWubi86, IID_ITfFnConfigure, &fn_unk) == E_NOINTERFACE);
    QIQI_TEST_CHECK(SUCCEEDED(func_prov->GetFunction(GUID_NULL, IID_ITfFnConfigure, &fn_unk)) && fn_unk);
    fn_unk.Reset();

    QIQI_TEST_CHECK(fn_cfg->GetDisplayName(nullptr) == E_INVALIDARG);
    ScopedBstr fn_name;
    QIQI_TEST_CHECK(SUCCEEDED(fn_cfg->GetDisplayName(fn_name.Put())) && fn_name.IsValid());
    func_prov.Reset();
    fn_cfg.Reset();

    // Test ITfDisplayAttributeProvider & IEnumTfDisplayAttributeInfo & ITfDisplayAttributeInfo
    QIQI_TEST_CHECK(disp_attr_prov->EnumDisplayAttributeInfo(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(disp_attr_prov->GetDisplayAttributeInfo(kGuidDisplayAttributeInput, nullptr) == E_INVALIDARG);
    ComPtr<ITfDisplayAttributeInfo> attr_info;
    QIQI_TEST_CHECK(disp_attr_prov->GetDisplayAttributeInfo(kGuidProfileWubi86, &attr_info) == E_INVALIDARG);
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->GetDisplayAttributeInfo(kGuidDisplayAttributeInput, &attr_info)) && attr_info);
    QIQI_TEST_CHECK(attr_info->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(attr_info->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);
    ComPtr<ITfDisplayAttributeInfo> attr_qi;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->QueryInterface(IID_PPV_ARGS(&attr_qi))) && attr_qi);
    attr_qi.Reset();
    QIQI_TEST_CHECK(attr_info->GetGUID(nullptr) == E_INVALIDARG);
    GUID attr_guid{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetGUID(&attr_guid)) && IsEqualGUID(attr_guid, kGuidDisplayAttributeInput));
    QIQI_TEST_CHECK(attr_info->GetDescription(nullptr) == E_INVALIDARG);
    ScopedBstr attr_desc;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetDescription(attr_desc.Put())) && attr_desc.IsValid());
    QIQI_TEST_CHECK(attr_info->GetAttributeInfo(nullptr) == E_INVALIDARG);
    TF_DISPLAYATTRIBUTE da{};
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->GetAttributeInfo(&da)) &&
                    da.lsStyle == TF_LS_DOT && da.crLine.type == TF_CT_NONE);
    QIQI_TEST_CHECK(QiQiDisplayAttributeInfo::BuildAttribute(CompositionUnderlineStyle::Solid, true).lsStyle ==
                    TF_LS_SOLID);
    QIQI_TEST_CHECK(QiQiDisplayAttributeInfo::BuildAttribute(CompositionUnderlineStyle::Dash, false).lsStyle ==
                    TF_LS_DASH);
    QIQI_TEST_CHECK(QiQiDisplayAttributeInfo::BuildAttribute(CompositionUnderlineStyle::Squiggle, true).lsStyle ==
                    TF_LS_SQUIGGLE);
    QIQI_TEST_CHECK(QiQiDisplayAttributeInfo::BuildAttribute(CompositionUnderlineStyle::None, false).lsStyle ==
                    TF_LS_NONE);
    QIQI_TEST_CHECK(attr_info->SetAttributeInfo(nullptr) == E_INVALIDARG);
    da.fBoldLine = TRUE;
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->SetAttributeInfo(&da)));
    QIQI_TEST_CHECK(SUCCEEDED(attr_info->Reset()));

    ComPtr<IEnumTfDisplayAttributeInfo> enum_attr;
    QIQI_TEST_CHECK(SUCCEEDED(disp_attr_prov->EnumDisplayAttributeInfo(&enum_attr)) && enum_attr);
    QIQI_TEST_CHECK(enum_attr->QueryInterface(IID_IUnknown, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(enum_attr->QueryInterface(IID_IClassFactory, &out) == E_NOINTERFACE);
    ComPtr<IEnumTfDisplayAttributeInfo> enum_qi;
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->QueryInterface(IID_PPV_ARGS(&enum_qi))) && enum_qi);
    enum_qi.Reset();
    QIQI_TEST_CHECK(enum_attr->Clone(nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(enum_attr->Next(1, nullptr, nullptr) == E_INVALIDARG);
    ComPtr<ITfDisplayAttributeInfo> fetched_info;
    ULONG fetched = 99;
    QIQI_TEST_CHECK(enum_attr->Next(0, &fetched_info, &fetched) == S_OK && fetched == 0);
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_OK && fetched == 1 && fetched_info);
    fetched_info.Reset();
    QIQI_TEST_CHECK(enum_attr->Next(1, &fetched_info, &fetched) == S_FALSE && fetched == 0);
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Reset()));
    QIQI_TEST_CHECK(enum_attr->Next(2, &fetched_info, &fetched) == S_FALSE && fetched == 1 && fetched_info);
    fetched_info.Reset();
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Reset()));
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Skip(1)));
    QIQI_TEST_CHECK(enum_attr->Skip(1) == S_FALSE);
    ComPtr<IEnumTfDisplayAttributeInfo> cloned_enum;
    QIQI_TEST_CHECK(SUCCEEDED(enum_attr->Clone(&cloned_enum)) && cloned_enum);
    cloned_enum.Reset();
    enum_attr.Reset();
    attr_info.Reset();
    disp_attr_prov.Reset();

    // Deactivate before Activate (exercises UninitKeyEventSink / UninitThreadMgrEventSink early-return)
    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    // Activate failure paths
    QIQI_TEST_CHECK(service->ActivateEx(nullptr, 1, 0) == E_INVALIDARG);
    auto mock_tm = MakeComObject<MockTfThreadMgr>();
    mock_tm->fail_query_source_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_query_source_ = false;
    mock_tm->fail_advise_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_advise_ = false;
    mock_tm->fail_query_key_mgr_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_query_key_mgr_ = false;
    mock_tm->fail_key_sink_ = true;
    QIQI_TEST_CHECK(service->ActivateEx(mock_tm.Get(), 1, 0) == E_FAIL);
    mock_tm->fail_key_sink_ = false;

    mock_tm->single_sink_result_ = E_ACCESSDENIED;
    mock_tm->add_item_result_ = E_OUTOFMEMORY;
    QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(!mock_tm->single_sink_ && !mock_tm->langbar_item_);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(mock_tm->unadvise_single_calls_ == 0 && mock_tm->remove_item_calls_ == 0);

    mock_tm->single_sink_result_ = S_OK;
    QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(mock_tm->single_sink_ && !mock_tm->langbar_item_);
    mock_tm->unadvise_single_result_ = E_FAIL;
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(mock_tm->unadvise_single_calls_ == 1 && mock_tm->remove_item_calls_ == 0);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(mock_tm->unadvise_single_calls_ == 1);

    mock_tm->single_sink_result_ = E_ACCESSDENIED;
    mock_tm->add_item_result_ = S_OK;
    mock_tm->remove_item_result_ = E_FAIL;
    QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(!mock_tm->single_sink_ && mock_tm->langbar_item_);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(mock_tm->unadvise_single_calls_ == 1 && mock_tm->remove_item_calls_ == 1);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(mock_tm->remove_item_calls_ == 1);
    mock_tm->single_sink_result_ = S_OK;
    mock_tm->unadvise_single_result_ = S_OK;
    mock_tm->remove_item_result_ = S_OK;

    TestPreservedKeyRegistrationLifetime(gsl::not_null<QiQiTextService*>(service.Get()));
    TestProcessDictCacheLifetime();

    // Activate success
    QIQI_TEST_CHECK(SUCCEEDED(service->Activate(mock_tm.Get(), 1)));
    QIQI_TEST_CHECK(log::IsEtwProviderRegistered());
    {
        auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
        auto peer_manager = MakeComObject<MockTfThreadMgr>();
        QIQI_TEST_CHECK(peer->Activate(peer_manager.Get(), 2) == S_OK);
        const auto previous_level = log::GetLevel();
        const auto restore_level = gsl::finally([previous_level]() noexcept { log::SetLevel(previous_level); });
        log::SetLevel(log::Level::off);
        QIQI_TEST_CHECK(peer->Deactivate() == S_OK);
        QIQI_TEST_CHECK(log::IsEtwProviderRegistered());
        QIQI_TEST_CHECK(peer->Activate(peer_manager.Get(), 2) == S_OK);
        QIQI_TEST_CHECK(log::GetLevel() == log::Level::off);
        QIQI_TEST_CHECK(peer->Deactivate() == S_OK);
        QIQI_TEST_CHECK(log::IsEtwProviderRegistered());
    }

    RunTsfServiceLifecycleTests(
        gsl::not_null<QiQiTextService*>(service.Get()), gsl::not_null<MockTfThreadMgr*>(mock_tm.Get()));

    for (const bool commit : {true, false}) {
        TestModeSwitchCompositionPolicy(
            gsl::not_null<QiQiTextService*>(service.Get()), gsl::not_null<MockTfThreadMgr*>(mock_tm.Get()), commit);
    }

    // ThreadMgrEventSink notifications (including non-null OnSetFocus to test config hot-reload check)
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnInitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnUninitDocumentMgr(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPushContext(nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnPopContext(nullptr)));
    auto empty_document = MakeComObject<MockTfDocumentMgr>();
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnSetFocus(empty_document.Get(), nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(tm_sink->OnSetFocus(nullptr, nullptr)));

    // KeyEventSink error checks
    QIQI_TEST_CHECK(key_sink->OnTestKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(nullptr, 'A', 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnTestKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(nullptr, VK_SHIFT, 0, nullptr) == E_INVALIDARG);
    QIQI_TEST_CHECK(key_sink->OnPreservedKey(nullptr, kGuidPreservedKeyToggleMode, nullptr) == E_INVALIDARG);

    auto mock_ctx = MakeComObject<MockTfContext>();
    BOOL eaten = FALSE;

    // Reproduce hosts that omit the test callbacks, including a not-yet-bound context.
    for (const UINT key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT}) {
        QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), key, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), key, 1LL << 30, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), key, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 0);
        QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), key, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(nullptr, key, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(key_sink->OnTestKeyUp(nullptr, key, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyUp(nullptr, key, 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);
    }
    for (const int modifier : {VK_CONTROL, VK_MENU, VK_LBUTTON}) {
        auto keyboard = clean_kb_state;
        gsl::at(keyboard, modifier) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
        QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(::SetKeyboardState(clean_kb_state.data()));
        QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
        QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);
    }
    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_CONTROL, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);

    QIQI_TEST_CHECK(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    ::Sleep(220);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    ::Sleep(160);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);

    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    ::Sleep(360);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 1LL << 30, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 1LL << 30, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);

    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
    QIQI_TEST_CHECK(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);

    QIQI_TEST_CHECK(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    auto modified_keyboard = clean_kb_state;
    gsl::at(modified_keyboard, VK_CONTROL) = 0x80;
    QIQI_TEST_CHECK(::SetKeyboardState(modified_keyboard.data()));
    QIQI_TEST_CHECK(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(::SetKeyboardState(clean_kb_state.data()));
    QIQI_TEST_CHECK(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(mock_tm->openclose_compartment_->last_val_ == 1);

    // Type 'A' then 'B' -> starts composition (BeginUIElement) then updates composition (UpdateUIElement)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");

    // Backspace back to 'a', then Space commits "工"
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_BACK, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SPACE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工");

    // Direct Chinese punctuation commit when not composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"。");

    // Start composing 'W', then cancel via Escape
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_.empty());

    // 5th key push auto-commit flow (triggers Commit with non-empty composition_text and EmptyCodeBeep on 4th code "yyyx")
    ResetLastPlayedSoundForTest();
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'Y', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
    QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");

    // Test Shift tap mode toggle while composing (commits raw code and switches to English)
    QIQI_TEST_CHECK(::SetKeyboardState(clean_kb_state.data()));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
    // Auto-repeat VK_SHIFT within 350ms preserves the tap candidate.
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 1LL << 30, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a" && mock_ctx->last_composition_->ended_);

    // Test Shift+Click anti-mistrigger: when VK_LBUTTON is pressed during Shift release, mode is NOT toggled
    {
        std::array<BYTE, 256> mouse_kb_state{};
        gsl::at(mouse_kb_state, VK_LBUTTON) = 0x80;
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
        ::SetKeyboardState(mouse_kb_state.data());
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
        ::SetKeyboardState(clean_kb_state.data());
    }

    // In English mode, 'A' is not eaten
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Test VK_LSHIFT / VK_RSHIFT tap toggle and Shift tap with nullptr / empty context
    {
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(nullptr, VK_LSHIFT, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(nullptr, VK_LSHIFT, 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(nullptr, VK_LSHIFT, 0, &eaten)) && eaten == TRUE);
        // Now back in Chinese mode: 'A' should be eaten
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);

        // Toggle back to English using VK_RSHIFT on an unprepared context
        auto fresh_ctx = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(fresh_ctx.Get(), VK_RSHIFT, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(fresh_ctx.Get(), VK_RSHIFT, 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(fresh_ctx.Get(), VK_RSHIFT, 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
    }

    // Toggle back to Chinese mode via PreservedKey (Ctrl+Space)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidProfileWubi86, &eaten)) && eaten == FALSE);

    // Test Caps Lock (VK_CAPITAL low-order bit = 1):
    // 1) In Idle Chinese mode, letter and punctuation keys pass through as English (not eaten by Wubi composition)
    // 2) Pressing VK_CAPITAL mid-composition commits the raw English code ("ab") while letting VK_CAPITAL pass through (*pfEaten = FALSE)
    // 3) While composing, pressing a letter with Caps Lock ON commits the current candidate + uppercase letter
    {
        std::array<BYTE, 256> caps_kb_state{};
        gsl::at(caps_kb_state, VK_CAPITAL) = 0x01;
        ::SetKeyboardState(caps_kb_state.data());
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);

        ::SetKeyboardState(clean_kb_state.data());
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"ab");

        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"a");

        ::SetKeyboardState(caps_kb_state.data());
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'B', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工B");
        ::SetKeyboardState(clean_kb_state.data());
    }

    // Test FullWidth toggle (Shift+Space) and Chinese Punctuation toggle (Ctrl+.) PreservedKeys
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), '1', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"１");
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleFullWidth, &eaten)) && eaten == TRUE);

    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyTogglePunctuation, &eaten)) && eaten == TRUE);

    // Populate clipboard with a 2-character phrase ("中国") and test Ctrl+= (kGuidPreservedKeyAddUserWord)
    if (const ScopedClipboard clip{}; clip.IsOpen()) {
        ::EmptyClipboard();
        if (const HGLOBAL hclip = ::GlobalAlloc(GMEM_MOVEABLE, 4 * sizeof(wchar_t))) {
            bool handed_to_clipboard = false;
            const auto free_hclip = gsl::finally([&]() noexcept {
                if (!handed_to_clipboard) {
                    ::GlobalFree(hclip);
                }
            });
            if (auto* p = static_cast<wchar_t*>(::GlobalLock(hclip))) {
                wcscpy_s(p, 4, L"中国");
                ::GlobalUnlock(hclip);
                handed_to_clipboard = (::SetClipboardData(CF_UNICODETEXT, hclip) != nullptr);
            }
        }
    }
    ResetLastPlayedSoundForTest();
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyAddUserWord, &eaten)) && eaten == TRUE);

    // Edge cases for kGuidPreservedKeyAddUserWord: 1-char text skipped, empty clipboard (no CF_UNICODETEXT), and locked clipboard
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, L"中") == ERROR_SUCCESS);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyAddUserWord, &eaten)) && eaten == TRUE);
    if (const ScopedClipboard clip{}; clip.IsOpen()) {
        ::EmptyClipboard();
    }
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyAddUserWord, &eaten)) && eaten == TRUE);
    {
        const ScopedWindow lock_owner(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        std::atomic<bool> locked{false};
        std::atomic<bool> release{false};
        std::jthread locker([&]() {
            if (::OpenClipboard(lock_owner.Get())) {
                locked.store(true);
                locked.notify_all();
                release.wait(false);
                ::CloseClipboard();
            }
        });
        locked.wait(false);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyAddUserWord, &eaten)) && eaten == TRUE);
        release.store(true);
        release.notify_all();
    }

    // Start composing again, then test mouse page-turn chevron click and candidate selection on CandidateUI
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    if (mock_tm->last_ui_element_) {
        auto* cand_ui = dynamic_cast<CandidateUI*>(mock_tm->last_ui_element_.Get());
        if (cand_ui && cand_ui->WindowHandle()) {
            UINT cur_page = 99;
            QIQI_TEST_CHECK(SUCCEEDED(cand_ui->GetCurrentPage(&cur_page)) && cur_page == 0);
            const RECT next_rc = cand_ui->NextPageButtonRect();
            if (next_rc.right > next_rc.left) {
                ::SendMessageW(
                    cand_ui->WindowHandle(),
                    WM_LBUTTONDOWN,
                    MK_LBUTTON,
                    MAKELPARAM((next_rc.left + next_rc.right) / 2, (next_rc.top + next_rc.bottom) / 2));
                QIQI_TEST_CHECK(SUCCEEDED(cand_ui->GetCurrentPage(&cur_page)) && cur_page == 1);
                const RECT page1_prev_rc = cand_ui->PrevPageButtonRect();
                ::SendMessageW(
                    cand_ui->WindowHandle(),
                    WM_LBUTTONDOWN,
                    MK_LBUTTON,
                    MAKELPARAM((page1_prev_rc.left + page1_prev_rc.right) / 2,
                               (page1_prev_rc.top + page1_prev_rc.bottom) / 2));
                QIQI_TEST_CHECK(SUCCEEDED(cand_ui->GetCurrentPage(&cur_page)) && cur_page == 0);
            }
            ::SendMessageW(cand_ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
            QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"工");
        }
    }

    for (const auto* code : {L"zhello1", L"z"}) {
        QIQI_TEST_CHECK(::SetKeyboardState(clean_kb_state.data()));
        auto click_context = MakeComObject<MockTfContext>();
        for (const wchar_t ch : std::wstring(code)) {
            const auto vk = gsl::narrow_cast<uint32_t>(ch >= L'a' && ch <= L'z' ? ch - L'a' + L'A' : ch);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(click_context.Get(), vk, 0, &eaten) == S_OK && eaten);
        }
        QIQI_TEST_CHECK(click_context->range_->text_ == code);
        const auto composition = click_context->last_composition_;
        QIQI_TEST_CHECK(composition && !composition->ended_);
        const auto element = mock_tm->last_ui_element_;
        auto* const cand_ui = dynamic_cast<CandidateUI*>(element.Get());
        QIQI_TEST_CHECK(cand_ui && cand_ui->WindowHandle());
        ::SendMessageW(cand_ui->WindowHandle(), WM_DPICHANGED, MAKEWPARAM(96, 96), 0);
        const std::wstring expected = std::wstring(code) == L"z" ? L"z" : L"hello1";
        ::SendMessageW(cand_ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
        QIQI_TEST_CHECK(click_context->range_->text_ == expected && composition->ended_);
        BOOL shown = TRUE;
        QIQI_TEST_CHECK(cand_ui->IsShown(&shown) == S_OK && !shown);
        const auto requests_after_commit = click_context->request_edit_session_calls_;
        ::SendMessageW(cand_ui->WindowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 42));
        QIQI_TEST_CHECK(click_context->range_->text_ == expected);
        QIQI_TEST_CHECK(click_context->request_edit_session_calls_ == requests_after_commit);
    }

    if (mock_tm->langbar_item_) {
        ComPtr<ITfLangBarItemButton> lb_btn;
        if (SUCCEEDED(mock_tm->langbar_item_.As(&lb_btn)) && lb_btn) {
            const POINT pt{0, 0};
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnClick(TF_LBI_CLK_LEFT, pt, nullptr)));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleChinese))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleFullWidth))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleFullWidth))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::TogglePunctuation))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::TogglePunctuation))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleStatusBar))));
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleStatusBar))));
            ComPtr<ITfFnConfigure> cfg_check;
            QIQI_TEST_CHECK(SUCCEEDED(service->QueryInterface(IID_PPV_ARGS(&cfg_check))) && cfg_check);
            ScopedBstr disp_name1;
            ScopedBstr disp_name2;
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage))));
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(disp_name1.Put())) && disp_name1.IsValid());
            QIQI_TEST_CHECK(SUCCEEDED(lb_btn->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage))));
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(disp_name2.Put())) && disp_name2.IsValid());
            QIQI_TEST_CHECK(std::wstring(disp_name1.Get()) != std::wstring(disp_name2.Get()));
        }
    }

    // Start composing again, then test PreservedKey while composing
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnPreservedKey(mock_ctx.Get(), kGuidPreservedKeyToggleMode, &eaten)) && eaten == TRUE);

    // Non-shift TestKeyUp and KeyUp
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);

    // Test ITfTextLayoutSink::OnLayoutChange while composing when initial GetTextExt returned TS_E_NOLAYOUT
    mock_ctx->view_->fail_get_text_ext_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(mock_ctx->layout_sink_ != nullptr);
    mock_ctx->view_->fail_get_text_ext_ = false;
    QIQI_TEST_CHECK(SUCCEEDED(mock_ctx->layout_sink_->OnLayoutChange(mock_ctx.Get(), TF_LC_CREATE, mock_ctx->view_.Get())));
    QIQI_TEST_CHECK(SUCCEEDED(mock_ctx->layout_sink_->OnLayoutChange(mock_ctx.Get(), TF_LC_CHANGE, mock_ctx->view_.Get())));

    // Host composition termination & focus loss
    QIQI_TEST_CHECK(SUCCEEDED(comp_sink->OnCompositionTerminated(1, nullptr)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnSetFocus(FALSE)));

    // Test GUID_COMPARTMENT_KEYBOARD_DISABLED and GUID_COMPARTMENT_EMPTYCONTEXT awareness (password / read-only fields)
    {
        ComPtr<ITfCompartment> disabled_comp;
        QIQI_TEST_CHECK(SUCCEEDED(mock_ctx->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled_comp)));
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(disabled_comp.Get(), 1, 1)));
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(disabled_comp.Get(), 1, 0)));

        ComPtr<ITfCompartment> empty_comp;
        QIQI_TEST_CHECK(SUCCEEDED(mock_ctx->GetCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, &empty_comp)));
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(empty_comp.Get(), 1, 1)));
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(empty_comp.Get(), 1, 0)));
    }

    // Test defensive failure branches in StartOrUpdateCompositionInSession, UpdateCandidateWindowPositionInSession, and BeginOrUpdateCandidateUI
    mock_ctx->fail_qi_insert_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_qi_insert_ = false;

    mock_ctx->fail_insert_at_sel_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_insert_at_sel_ = false;

    mock_ctx->fail_qi_comp_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_qi_comp_ = false;

    mock_ctx->fail_start_comp_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_start_comp_ = false;

    mock_ctx->fail_comp_get_range_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_comp_get_range_ = false;

    mock_ctx->fail_get_view_ = true;
    mock_tm->fail_query_ui_mgr_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_ctx->fail_get_view_ = false;
    mock_tm->fail_query_ui_mgr_ = false;

    mock_tm->fail_begin_ui_ = true;
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));
    mock_tm->fail_begin_ui_ = false;

    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    // Test UILess game mode activation (TF_TMAE_UIELEMENTENABLEDONLY with show_own_ui_ = FALSE)
    mock_tm->show_own_ui_ = FALSE;
    QIQI_TEST_CHECK(SUCCEEDED(service->ActivateEx(mock_tm.Get(), 1, TF_TMAE_UIELEMENTENABLEDONLY)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyDown(mock_ctx.Get(), VK_SPACE, 0, &eaten)));
    QIQI_TEST_CHECK(SUCCEEDED(service->Deactivate()));

    comp_sink.Reset();
    key_sink.Reset();
    tm_sink.Reset();
    tip_ex.Reset();
    tip_base.Reset();
    QIQI_TEST_CHECK(service.Detach()->Release() == 0);

    // Also test QiQiTextService with custom Haifeng + user_dict config and hot-reload on OnSetFocus
    std::error_code ec;
    std::filesystem::remove_all(temp_appdata, ec);

    QiQiConfig haifeng_cfg{};
    haifeng_cfg.primary_dict = PrimaryDict::Haifeng;
    haifeng_cfg.enable_jidian_extra = true;
    haifeng_cfg.up_down_page_turn = true;
    haifeng_cfg.user_dict_path = L"wubi86_jidian_extra.txt";
    QIQI_TEST_CHECK(SaveConfig(haifeng_cfg));

    auto service_haifeng = MakeComObject<QiQiTextService>(g_dll_instance);
    ComPtr<ITfThreadMgrEventSink> hf_tm_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&hf_tm_sink))));
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->Activate(mock_tm.Get(), 1)));
    if (mock_tm->langbar_item_) {
        ComPtr<ITfLangBarItemButton> hf_lb;
        if (SUCCEEDED(mock_tm->langbar_item_.As(&hf_lb)) && hf_lb) {
            ComPtr<ITfFnConfigure> cfg_check;
            QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&cfg_check))) && cfg_check);
            ScopedBstr display_name_before;
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(display_name_before.Put())) &&
                            display_name_before.IsValid());
            const uint64_t before_quick_save = QueryRegistryConfigSequence();
            const LONG before_quick_notify = ReadCompartmentInt4(mock_tm->global_config_compartment_.Get());
            {
                const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
                SetConfigRegistrySubKeyOverrideForTest(L"");
                const auto restore_registry = gsl::finally([previous_subkey]() noexcept {
                    SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
                });
                const auto* foreground_before = ::GetForegroundWindow();
                const long object_count_before = g_com_object_count.load();
                QIQI_TEST_CHECK(SUCCEEDED(
                    hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleUiLanguage))));
                const HWND toast = FindThreadWindowByClass(kSettingsToastWndClass);
                QIQI_TEST_CHECK(toast && ::IsWindowVisible(toast));
                std::array<wchar_t, 256> toast_text{};
                QIQI_TEST_CHECK(::GetWindowTextW(
                    toast, toast_text.data(), gsl::narrow_cast<int>(toast_text.size())) > 0);
                QIQI_TEST_CHECK(std::wstring_view(toast_text.data()) ==
                                GetUiStrings(haifeng_cfg.ui_language).quick_settings_save_failed);
                QIQI_TEST_CHECK((::GetWindowLongPtrW(toast, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
                QIQI_TEST_CHECK(::GetForegroundWindow() == foreground_before);
                QIQI_TEST_CHECK(g_com_object_count.load() == object_count_before + 1);
                ::SendMessageW(toast, WM_TIMER, 1, 0);
                QIQI_TEST_CHECK(!::IsWindow(toast));
                QIQI_TEST_CHECK(g_com_object_count.load() == object_count_before);
            }
            QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before_quick_save);
            QIQI_TEST_CHECK(ReadCompartmentInt4(mock_tm->global_config_compartment_.Get()) ==
                            before_quick_notify);
            QiQiConfig quick_saved;
            QIQI_TEST_CHECK(LoadConfigFromRegistry(quick_saved));
            QIQI_TEST_CHECK(quick_saved.ui_language == haifeng_cfg.ui_language);
            ScopedBstr display_name_after;
            QIQI_TEST_CHECK(SUCCEEDED(cfg_check->GetDisplayName(display_name_after.Put())) &&
                            display_name_after.IsValid());
            QIQI_TEST_CHECK(std::wstring(display_name_before.Get()) == std::wstring(display_name_after.Get()));

            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictJidian))));
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::DictHaifeng))));
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::ToggleExtraDict))));
            const uint64_t before_settings_save = QueryRegistryConfigSequence();
            const LONG before_settings_notify = ReadCompartmentInt4(mock_tm->global_config_compartment_.Get());
            ::SetTimer(nullptr, 2, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                ::KillTimer(nullptr, id);
                const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
                if (dlg) {
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                }
            });
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenSettings))));
            QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before_settings_save + 1);
            QIQI_TEST_CHECK(ReadCompartmentInt4(mock_tm->global_config_compartment_.Get()) ==
                            before_settings_notify + 1);

            {
                mock_tm->global_config_compartment_->fail_set_value_ = true;
                const auto restore_notification = gsl::finally([&]() noexcept {
                    mock_tm->global_config_compartment_->fail_set_value_ = false;
                });
                const uint64_t before = QueryRegistryConfigSequence();
                const LONG notified_before = ReadCompartmentInt4(mock_tm->global_config_compartment_.Get());
                const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                    const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
                    if (!dlg) return;
                    ::KillTimer(nullptr, id);
                    ::SendDlgItemMessageW(dlg, 109, CB_SETCURSEL, 7, 0);
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                });
                QIQI_TEST_CHECK(timer != 0);
                const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
                WithDismissedMessage(GetUiStrings(haifeng_cfg.ui_language).dlg_notify_failed, [&]() {
                    QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenSettings))));
                });
                QIQI_TEST_CHECK(QueryRegistryConfigSequence() == before + 1);
                QIQI_TEST_CHECK(ReadCompartmentInt4(mock_tm->global_config_compartment_.Get()) == notified_before);
                QiQiConfig saved;
                QIQI_TEST_CHECK(LoadConfigFromRegistry(saved) && saved.page_size == 8);
            }
            {
                const uint64_t before = QueryRegistryConfigSequence();
                const LONG notified_before = ReadCompartmentInt4(mock_tm->global_config_compartment_.Get());
                const wchar_t* previous_subkey = GetActiveConfigRegistrySubKey();
                SetConfigRegistrySubKeyOverrideForTest(L"");
                const auto restore_registry = gsl::finally([previous_subkey]() noexcept {
                    SetConfigRegistrySubKeyOverrideForTest(previous_subkey);
                });
                const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                    const HWND dlg = FindThreadWindowByClass(kSettingsWndClass);
                    if (!dlg) return;
                    ::KillTimer(nullptr, id);
                    ::SendDlgItemMessageW(dlg, 109, CB_SETCURSEL, 8, 0);
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
                });
                QIQI_TEST_CHECK(timer != 0);
                const auto cancel_timer = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
                WithDismissedMessage(GetUiStrings(haifeng_cfg.ui_language).dlg_save_failed, [&]() {
                    QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenSettings))));
                });
                QIQI_TEST_CHECK(QueryRegistryConfigSequence(previous_subkey) == before);
                QIQI_TEST_CHECK(ReadCompartmentInt4(mock_tm->global_config_compartment_.Get()) == notified_before);
                QiQiConfig saved;
                QIQI_TEST_CHECK(LoadConfigFromRegistry(saved, nullptr, previous_subkey) && saved.page_size == 8);
            }

            ::SetTimer(nullptr, 5, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
                ::KillTimer(nullptr, id);
                const HWND dlg = ::FindWindowW(kAboutWndClass, nullptr);
                if (dlg) {
                    ::SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
                }
            });
            QIQI_TEST_CHECK(SUCCEEDED(hf_lb->OnMenuSelect(static_cast<UINT>(LangBarMenuId::OpenAbout))));
        }
    }

    // Modify config in HKCU registry and trigger zero-latency hot-reload via ITfCompartmentEventSink::OnChange
    haifeng_cfg.page_size = 6;
    haifeng_cfg.show_status_bar = true;
    haifeng_cfg.enable_key_sound = true;
    haifeng_cfg.beep_on_dup_4code = true;
    haifeng_cfg.clear_on_empty_4code = true;
    QIQI_TEST_CHECK(SaveConfig(haifeng_cfg));
    ComPtr<ITfCompartmentEventSink> comp_event_sink;
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&comp_event_sink))) && comp_event_sink);
    QIQI_TEST_CHECK(SUCCEEDED(comp_event_sink->OnChange(GUID_NULL)));
    QIQI_TEST_CHECK(SUCCEEDED(comp_event_sink->OnChange(kGuidCompartmentConfigVersion)));
    comp_event_sink.Reset();

    // Also test ScopedTfSinkConnection and ReadCompartmentInt4 / WriteCompartmentInt4 helpers directly
    {
        QIQI_TEST_CHECK(ReadCompartmentInt4(nullptr, 77) == 77);
        QIQI_TEST_CHECK(WriteCompartmentInt4(nullptr, 1, 10) == E_INVALIDARG);
        QIQI_TEST_CHECK(mock_tm->global_config_compartment_ != nullptr);
        const LONG ver_before = ReadCompartmentInt4(mock_tm->global_config_compartment_.Get(), 0);
        QIQI_TEST_CHECK(ver_before > 0);
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(mock_tm->global_config_compartment_.Get(), 1, ver_before + 1)));
        QIQI_TEST_CHECK(ReadCompartmentInt4(mock_tm->global_config_compartment_.Get(), 0) == ver_before + 1);

        ScopedTfSinkConnection sink_conn;
        QIQI_TEST_CHECK(!sink_conn.IsConnected());
        QIQI_TEST_CHECK(!sink_conn.Advise(nullptr, IID_ITfCompartmentEventSink, nullptr));

        // Test cross-process StatusBarUI position sync via kGuidCompartmentStatusBarPos
        QIQI_TEST_CHECK(mock_tm->global_pos_compartment_ != nullptr);
        const LONG packed_pos = PackScreenPointToLong(POINT{280, 360});
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(mock_tm->global_pos_compartment_.Get(), 1, packed_pos)));
        if (const HWND sb_hwnd = ::FindWindowW(kStatusBarWndClass, nullptr)) {
            ::SendMessageW(sb_hwnd, WM_MOVE, 0, MAKELPARAM(240, 320));
            ::SendMessageW(sb_hwnd, WM_EXITSIZEMOVE, 0, 0);
            const POINT saved_pt = UnpackLongToScreenPoint(
                ReadCompartmentInt4(mock_tm->global_pos_compartment_.Get(), 0));
            QIQI_TEST_CHECK(saved_pt.x == 240 && saved_pt.y == 320);
        }

        // Test bidirectional system compartment sync (GUID_COMPARTMENT_KEYBOARD_OPENCLOSE & INPUTMODE_CONVERSION):
        // 1) Start composing 'W', then externally set OPENCLOSE = 0 -> cancels active composition and switches to English
        QIQI_TEST_CHECK(mock_tm->openclose_compartment_ != nullptr);
        QIQI_TEST_CHECK(mock_tm->convmode_compartment_ != nullptr);
        ComPtr<ITfKeyEventSink> check_key_sink;
        QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&check_key_sink))) && check_key_sink);
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(mock_tm->openclose_compartment_.Get(), 1, 0)));
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        // Restore Chinese mode via OPENCLOSE = 1
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(mock_tm->openclose_compartment_.Get(), 1, 1)));
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == TRUE);

        // 2) Start composing 'W', then externally set INPUTMODE_CONVERSION = 0 (English, HalfWidth, English punct)
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(mock_tm->convmode_compartment_.Get(), 1, 0)));
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnTestKeyDown(mock_ctx.Get(), 'A', 0, &eaten)) && eaten == FALSE);
        // Restore Chinese + FullWidth + ChinesePunct via INPUTMODE_CONVERSION, then back to Chinese + HalfWidth + ChinesePunct
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(
            mock_tm->convmode_compartment_.Get(),
            1,
            TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_FULLSHAPE | TF_CONVERSIONMODE_SYMBOL)));
        QIQI_TEST_CHECK(SUCCEEDED(check_key_sink->OnKeyDown(mock_ctx.Get(), '1', 0, &eaten)) && eaten == TRUE);
        QIQI_TEST_CHECK(mock_ctx->range_->text_ == L"１");
        QIQI_TEST_CHECK(SUCCEEDED(WriteCompartmentInt4(
            mock_tm->convmode_compartment_.Get(),
            1,
            TF_CONVERSIONMODE_NATIVE | TF_CONVERSIONMODE_SYMBOL)));
    }

    QIQI_TEST_CHECK(SUCCEEDED(hf_tm_sink->OnSetFocus(empty_document.Get(), nullptr)));
    ComPtr<ITfKeyEventSink> hf_key_sink;
    if (SUCCEEDED(service_haifeng->QueryInterface(IID_PPV_ARGS(&hf_key_sink))) && hf_key_sink) {
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::KeyClick);
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));

        // Type 4 duplicate-match keys ("aaaa" -> "工", "恭恭敬敬"): verifies beep_on_dup_4code plays DupCodeBeep
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'A', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::DupCodeBeep);
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_ESCAPE, 0, &eaten)));

        // Type 4 unmatched keys ("xvwq"): verifies clear_on_empty_4code cancels TSF composition & plays EmptyCodeBeep
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'X', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'V', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        ResetLastPlayedSoundForTest();
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'Q', 0, &eaten)));
        QIQI_TEST_CHECK(GetLastPlayedSoundForTest() == SoundEffectKind::EmptyCodeBeep);
        QIQI_TEST_CHECK(mock_ctx->range_->text_.empty());

        // Exercise FormatVirtualKey branches and VK_CAPITAL trace logging when log_keystrokes is enabled
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_MINUS, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PLUS, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_DOWN, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_BACK, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_1, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_7, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_RETURN, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_COMMA, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_OEM_PERIOD, 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), 'W', 0, &eaten)));
        QIQI_TEST_CHECK(SUCCEEDED(hf_key_sink->OnKeyDown(mock_ctx.Get(), VK_CAPITAL, 0, &eaten)) && eaten == FALSE);
        hf_key_sink.Reset();
    }
    QIQI_TEST_CHECK(SUCCEEDED(service_haifeng->Deactivate()));
    hf_tm_sink.Reset();
    QIQI_TEST_CHECK(service_haifeng.Detach()->Release() == 0);
    SetMuteAudioOutputForTest(false);

    // Also test QiQiTextService construction when:
    // 1) g_dll_instance points to kernel32.dll (where dictionaries do not exist next to DLL)
    // 2) g_dll_instance is invalid (GetModuleFileNameW failure branch)
    HINSTANCE saved_instance = g_dll_instance;
    g_dll_instance = ::GetModuleHandleW(L"kernel32.dll");
    auto service_no_dict = MakeComObject<QiQiTextService>(g_dll_instance);
    QIQI_TEST_CHECK(service_no_dict.Detach()->Release() == 0);

    g_dll_instance = reinterpret_cast<HINSTANCE>(0x12345678);
    auto service_bad_inst = MakeComObject<QiQiTextService>(g_dll_instance);
    QIQI_TEST_CHECK(service_bad_inst.Detach()->Release() == 0);
    g_dll_instance = saved_instance;
}

} // namespace

void RunTsfFocusDiagnosticsScenario() {
    QIQI_TEST_CHECK(::IsDebuggerPresent());
    const auto subkey = std::format(L"Software\\QiQiIME_FocusTest_{}", ::GetCurrentProcessId());
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto directory = std::filesystem::temp_directory_path() /
                           std::format(L"QiQiIME_FocusTest_{}", ::GetCurrentProcessId());
    QIQI_TEST_CHECK(std::filesystem::create_directory(directory));
    wchar_t* previous_appdata = nullptr;
    size_t previous_length = 0;
    QIQI_TEST_CHECK(_wdupenv_s(&previous_appdata, &previous_length, L"LOCALAPPDATA") == 0);
    const std::unique_ptr<wchar_t, decltype(&std::free)> previous_appdata_owner(previous_appdata, &std::free);
    QIQI_TEST_CHECK(_wputenv_s(L"LOCALAPPDATA", directory.c_str()) == 0);
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(_wputenv_s(L"LOCALAPPDATA", previous_appdata_owner ? previous_appdata_owner.get() : L"") == 0);
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        QIQI_TEST_CHECK(!ec);
        SetMuteAudioOutputForTest(false);
    });
    SetMuteAudioOutputForTest(true);
    const auto dictionary = directory / L"focus.txt";
    const auto write_dictionary = [&](int generation) {
        std::ofstream file(dictionary, std::ios::binary | std::ios::trunc);
        file << "zzzz first second" << generation << "\n";
        file.close();
        QIQI_TEST_CHECK(file.good());
        std::filesystem::last_write_time(dictionary, std::filesystem::file_time_type::clock::now() +
                                                     std::chrono::seconds(10 * generation));
    };
    write_dictionary(0);
    QiQiConfig config{};
    config.primary_dict = PrimaryDict::BuiltinOnly;
    config.enable_jidian_extra = false;
    config.user_dict_path = dictionary.wstring();
    config.log_level = log::Level::debug;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    auto service = MakeComObject<QiQiTextService>(g_dll_instance);
    auto manager = MakeComObject<MockTfThreadMgr>();
    ComPtr<ITfThreadMgr> document_manager_factory;
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(&document_manager_factory))));
    ComPtr<ITfDocumentMgr> document;
    QIQI_TEST_CHECK(document_manager_factory->CreateDocumentMgr(&document) == S_OK);
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    const auto deactivate = gsl::finally([&]() noexcept { QIQI_TEST_CHECK(service->Deactivate() == S_OK); });
    manager->global_pos_compartment_->last_val_ = 0;
    QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), nullptr) == S_OK);
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    manager->global_pos_compartment_->last_val_ = gsl::narrow_cast<LONG>(MAKELONG(137, 149));
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), nullptr) == S_OK);
    config.page_size = 4;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    write_dictionary(1);
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    write_dictionary(2);
    config.page_size = 6;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), nullptr) == S_OK);
    QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
    QIQI_TEST_CHECK(service->OnSetFocus(nullptr, document.Get()) == S_OK);
    config.dynamic_phrase_freq = true;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    {
        std::ofstream frequencies(GetDefaultPhraseFreqPath(), std::ios::binary | std::ios::trunc);
        frequencies << "zzzz first\n";
        frequencies.close();
        QIQI_TEST_CHECK(frequencies.good());
    }
    config.page_size = 5;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    {
        SetConfigRegistrySubKeyOverrideForTest(L"");
        const auto restore_registry = gsl::finally([&]() noexcept {
            SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
        });
        QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
    }
    for (const auto level : {log::Level::info, log::Level::off}) {
        log::SetLevel(level);
        QIQI_TEST_CHECK(service->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), nullptr) == S_OK);
    }

    std::array<BYTE, 256> keyboard{};
    QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
    log::SetLevel(log::Level::debug);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    for (int scenario = 0; scenario < 9; ++scenario) {
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto composition_document = MakeComObject<MockTfDocumentMgr>();
        auto context = composition_document->CreateTestContext(queue);
        QIQI_TEST_CHECK(composition_document->Push(context.Get()) == S_OK);
        if (scenario == 7) log::SetLevel(log::Level::info);
        if (scenario == 8) log::SetLevel(log::Level::off);
        QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &eaten) == S_OK && eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        if (scenario == 0) {
            const auto modal = composition_document->CreateTestContext();
            QIQI_TEST_CHECK(composition_document->Push(modal.Get()) == S_OK);
            QIQI_TEST_CHECK(service->OnPushContext(modal.Get()) == S_OK);
            QIQI_TEST_CHECK(composition_document->Pop(0) == S_OK);
            QIQI_TEST_CHECK(service->OnPopContext(modal.Get()) == S_OK);
        }
        context->range_->text_ = L"SENSITIVE_DIAGNOSTIC_SENTINEL";
        const auto composition = context->last_composition_;
        context->allow_sync_edit_sessions_ = scenario == 0 || scenario >= 5;
        if (scenario == 3) context->async_edit_session_result_ = E_ACCESSDENIED;
        if (scenario == 5) context->read_only_ = true;
        if (scenario == 6) {
            context->prop_->clear_result_ = E_FAIL;
            composition->end_result_ = E_ACCESSDENIED;
        }
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        if (scenario == 1) {
            QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
            QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'B', 0, &eaten) == S_OK && eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        } else if (scenario == 2) {
            QIQI_TEST_CHECK(composition->NotifyHostTermination(23) == S_OK);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        } else if (scenario == 4) {
            QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK);
        }
    }

    log::SetLevel(log::Level::debug);
    manager->preserve_key_callback_ = [](TfClientId, REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        if (IsEqualGUID(rguid, kGuidPreservedKeyToggleMode)) return TF_E_ALREADY_EXISTS;
        if (IsEqualGUID(rguid, kGuidPreservedKeyTogglePunctuation)) return E_OUTOFMEMORY;
        return S_OK;
    };
    manager->unpreserve_key_callback_ = [](REFGUID rguid, const TF_PRESERVEDKEY&) -> HRESULT {
        return IsEqualGUID(rguid, kGuidPreservedKeyToggleFullWidth) ? CONNECT_E_NOCONNECTION : S_OK;
    };
    manager->unadvise_key_sink_result_ = E_FAIL;
    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    manager->preserve_key_callback_ = {};
    manager->unpreserve_key_callback_ = {};
    manager->unadvise_key_sink_result_ = S_OK;

    QIQI_TEST_CHECK(service->Activate(manager.Get(), 1) == S_OK);
    auto shift_context = MakeComObject<MockTfContext>();
    shift_context->range_->text_ = L"SENSITIVE_DIAGNOSTIC_SENTINEL";
    ComPtr<ITfSource> button_source;
    ComPtr<ITfLangBarItemButton> button;
    QIQI_TEST_CHECK(manager->langbar_item_.As(&button_source) == S_OK);
    QIQI_TEST_CHECK(manager->langbar_item_.As(&button) == S_OK);
    auto indicator_sink = MakeComObject<DiagnosticLangBarSink>();
    indicator_sink->result = S_FALSE;
    DWORD cookie = 0;
    QIQI_TEST_CHECK(button_source->AdviseSink(IID_ITfLangBarItemSink, indicator_sink.Get(), &cookie) == S_OK);
    const auto release_shift = [&](UINT key, bool expected) {
        BOOL eaten = FALSE;
        QIQI_TEST_CHECK(service->OnTestKeyUp(shift_context.Get(), key, 0, &eaten) == S_OK &&
                       (eaten != FALSE) == expected);
        QIQI_TEST_CHECK(service->OnKeyUp(shift_context.Get(), key, 0, &eaten) == S_OK &&
                       (eaten != FALSE) == expected);
    };
    const auto press_shift = [&](UINT key, LPARAM flags = 0) {
        BOOL eaten = TRUE;
        QIQI_TEST_CHECK(service->OnTestKeyDown(shift_context.Get(), key, flags, &eaten) == S_OK && !eaten);
    };
    press_shift(VK_SHIFT);
    BOOL shift_down_eaten = TRUE;
    QIQI_TEST_CHECK(service->OnKeyDown(shift_context.Get(), VK_SHIFT, 0, &shift_down_eaten) == S_OK &&
                   !shift_down_eaten);
    press_shift(VK_SHIFT, 1LL << 30);
    release_shift(VK_SHIFT, true);
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 0);
    {
        ScopedIcon icon;
        ScopedBstr text;
        QIQI_TEST_CHECK(button->GetIcon(icon.Put()) == S_OK && icon.IsValid());
        QIQI_TEST_CHECK(button->GetText(text.Put()) == S_OK && std::wstring_view(text.Get()) == L"英");
    }
    indicator_sink->result = E_ACCESSDENIED;
    manager->convmode_compartment_->fail_set_value_ = true;
    press_shift(VK_LSHIFT);
    release_shift(VK_LSHIFT, true);
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 1);
    manager->convmode_compartment_->fail_set_value_ = false;
    indicator_sink->result = S_OK;
    release_shift(VK_RSHIFT, false);

    for (const int modifier : {VK_CONTROL, VK_MENU, VK_LBUTTON}) {
        press_shift(VK_RSHIFT);
        gsl::at(keyboard, modifier) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
        release_shift(VK_RSHIFT, false);
        press_shift(VK_RSHIFT);
        release_shift(VK_RSHIFT, false);
        gsl::at(keyboard, modifier) = 0;
        QIQI_TEST_CHECK(::SetKeyboardState(keyboard.data()));
        QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 1);
    }
    press_shift(VK_SHIFT);
    ::Sleep(360);
    release_shift(VK_SHIFT, false);
    press_shift(VK_SHIFT);
    BOOL eaten = FALSE;
    QIQI_TEST_CHECK(service->OnTestKeyDown(shift_context.Get(), 'Z', 0, &eaten) == S_OK);
    release_shift(VK_SHIFT, false);
    QIQI_TEST_CHECK(service->OnKeyDown(shift_context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 1);
    release_shift(VK_SHIFT, true);
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 0);
    QIQI_TEST_CHECK(service->OnKeyDown(shift_context.Get(), VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    release_shift(VK_SHIFT, true);
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 1);

    QIQI_LOG_INFO("ShiftLogFilter(begin)");
    for (const auto level : {log::Level::info, log::Level::off}) {
        log::SetLevel(level);
        press_shift(VK_SHIFT);
        release_shift(VK_SHIFT, true);
    }
    log::SetLevel(log::Level::debug);
    QIQI_LOG_INFO("ShiftLogFilter(end)");
    QIQI_TEST_CHECK(manager->openclose_compartment_->last_val_ == 1);
    QIQI_TEST_CHECK(button_source->UnadviseSink(cookie) == S_OK);
    press_shift(VK_SHIFT);
    QIQI_TEST_CHECK(service->Deactivate() == S_OK);
    press_shift(VK_SHIFT);
    release_shift(VK_SHIFT, false);

    config.shift_toggle_mode = false;
    QIQI_TEST_CHECK(SaveConfigToRegistry(config));
    auto disabled_service = MakeComObject<QiQiTextService>(g_dll_instance);
    QIQI_TEST_CHECK(disabled_service->Activate(manager.Get(), 1) == S_OK);
    QIQI_TEST_CHECK(disabled_service->OnTestKeyDown(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(disabled_service->OnTestKeyUp(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(disabled_service->OnKeyDown(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(disabled_service->OnKeyUp(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(disabled_service->Deactivate() == S_OK);
    QIQI_TEST_CHECK(disabled_service->OnKeyDown(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);
    QIQI_TEST_CHECK(disabled_service->OnKeyUp(nullptr, VK_SHIFT, 0, &eaten) == S_OK && !eaten);

    const auto previous_level = log::GetLevel();
    const auto restore_level = gsl::finally([previous_level]() noexcept { log::SetLevel(previous_level); });
    log::SetLevel(log::Level::trace);
    QIQI_LOG_INFO("FsmDiagnostics(begin)");
    WubiEngine engine;
    engine.AddEntry(L"abcd", L"SENSITIVE_DIAGNOSTIC_SENTINEL");
    for (const auto vk : {'A', 'B', 'C'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
    const auto automatic_commit = engine.ProcessKey('D');
    QIQI_TEST_CHECK(automatic_commit.action == KeyActionType::Commit && engine.State() == EngineState::Idle);
    QIQI_TEST_CHECK(automatic_commit.committed_text == L"SENSITIVE_DIAGNOSTIC_SENTINEL");

    engine.SetAutoCommit4Code(false);
    engine.SetPushCommit5th(false);
    for (const auto vk : {'A', 'B', 'C', 'D'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
    const auto fifth = engine.ProcessKey('E');
    QIQI_TEST_CHECK(fifth.action == KeyActionType::UpdateState && engine.State() == EngineState::Composing4);
    QIQI_TEST_CHECK(engine.RawCode() == L"abcd");
    const auto missing_candidate = engine.ProcessKey('9');
    QIQI_TEST_CHECK(missing_candidate.action == KeyActionType::UpdateState && engine.State() == EngineState::Composing4);
    QIQI_TEST_CHECK(engine.RawCode() == L"abcd");

    engine.Reset();
    engine.SetClearOnEmpty4Code(true);
    for (const auto vk : {'Y', 'Y', 'Y'}) engine.ProcessKey(gsl::narrow_cast<uint32_t>(vk));
    const auto empty_code = engine.ProcessKey('X');
    QIQI_TEST_CHECK(empty_code.action == KeyActionType::Cancel && engine.State() == EngineState::Idle);
    engine.ProcessKey('Z');
    const auto cancel_guide = engine.ProcessKey(VK_BACK);
    QIQI_TEST_CHECK(cancel_guide.action == KeyActionType::Cancel && engine.State() == EngineState::Idle);
    QIQI_TEST_CHECK(engine.ProcessKey(VK_F1).action == KeyActionType::PassThrough);

    for (const auto level : {log::Level::info, log::Level::off}) {
        log::SetLevel(level);
        QIQI_TEST_CHECK(engine.ProcessKey('A').action == KeyActionType::UpdateState);
        QIQI_TEST_CHECK(engine.ProcessKey(VK_ESCAPE).action == KeyActionType::Cancel);
    }
    log::SetLevel(log::Level::trace);
    QIQI_LOG_INFO("FsmDiagnostics(end)");
}

void RunTsfServiceTests() {
    TestShiftTapTracking();
    TestQiQiTextServiceLifecycleAndKeyFlow();
    TestFocusPerformanceLogs();
}

} // namespace qiqi::test
