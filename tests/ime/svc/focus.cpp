// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/test.h"

namespace qiqi::test {

namespace {
class DiagnosticLangBarSink final : public ComObjectBase<DiagnosticLangBarSink, ITfLangBarItemSink> {
public:
    STDMETHODIMP OnUpdate(DWORD) noexcept override { return result; }
    HRESULT result = S_OK;
};

} // namespace

namespace service_cases {

void TestFocusPerformanceLogs() {
    const qiqi::test::TsfTestCaseTrace test_case_trace(__func__);
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

} // namespace service_cases

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

} // namespace qiqi::test
