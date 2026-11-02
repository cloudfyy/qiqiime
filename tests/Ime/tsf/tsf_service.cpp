#include "common/test_support.h"
#include "tsf_mocks.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/sound.h"
#include "common/file_util.h"
#include "Ime/tsf/display_attribute.h"
#include "Ime/tsf/edit_session.h"
#include "Ime/tsf/tip_service.h"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <tuple>

namespace qiqi::test {

namespace {

struct NativeTsfState {
    std::wstring text;
    std::wstring composition_text;
    unsigned int composition_count = 0;
    bool composing = false;
    bool display_attribute = false;
};

class NativeCleanupLayoutSink final : public ComObjectBase<NativeCleanupLayoutSink, ITfTextLayoutSink> {
public:
    STDMETHODIMP OnLayoutChange(ITfContext*, TfLayoutCode code, ITfContextView*) override {
        if (code != TF_LC_CHANGE || called || !callback) return S_OK;
        called = true;
        return callback();
    }
    bool called = false;
    std::function<HRESULT()> callback;
};

class NativeDocumentEventSink final : public ComObjectBase<NativeDocumentEventSink, ITfThreadMgrEventSink> {
public:
    explicit NativeDocumentEventSink(gsl::not_null<QiQiTextService*> service) noexcept : service_(service) {}
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr* document) override { return service_->OnInitDocumentMgr(document); }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr* document) override {
        ++uninits;
        return service_->OnUninitDocumentMgr(document);
    }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr*, ITfDocumentMgr*) noexcept override { return S_OK; }
    STDMETHODIMP OnPushContext(ITfContext* context) override {
        ++pushes;
        return service_->OnPushContext(context);
    }
    STDMETHODIMP OnPopContext(ITfContext* context) override {
        ++pops;
        return service_->OnPopContext(context);
    }
    unsigned int pushes = 0;
    unsigned int pops = 0;
    unsigned int uninits = 0;
private:
    const gsl::not_null<QiQiTextService*> service_;
};

NativeTsfState ReadNativeTsfStateInSession(gsl::not_null<ITfContext*> context, TfEditCookie ec) {
    NativeTsfState state;
    ComPtr<ITfRange> range;
    ComPtr<ITfRange> end;
    QIQI_TEST_CHECK(context->GetStart(ec, &range) == S_OK);
    QIQI_TEST_CHECK(context->GetEnd(ec, &end) == S_OK);
    QIQI_TEST_CHECK(range->ShiftEndToRange(ec, end.Get(), TF_ANCHOR_END) == S_OK);
    const auto read_text = [ec](gsl::not_null<ITfRange*> target) {
        std::array<wchar_t, 16> text{};
        ULONG count = 0;
        QIQI_TEST_CHECK(target->GetText(ec, 0, text.data(), gsl::narrow_cast<ULONG>(text.size()), &count) == S_OK);
        return std::wstring(text.data(), count);
    };
    state.text = read_text(range.Get());
    ComPtr<ITfContextComposition> compositions;
    QIQI_TEST_CHECK(context->QueryInterface(IID_PPV_ARGS(&compositions)) == S_OK);
    ComPtr<IEnumITfCompositionView> enumerator;
    QIQI_TEST_CHECK(compositions->EnumCompositions(&enumerator) == S_OK);
    ComPtr<ITfRange> composition_range;
    for (;;) {
        ComPtr<ITfCompositionView> view;
        ULONG fetched = 0;
        const HRESULT hr = enumerator->Next(1, &view, &fetched);
        if (hr == S_FALSE) {
            QIQI_TEST_CHECK(fetched == 0);
            break;
        }
        QIQI_TEST_CHECK(hr == S_OK && fetched == 1 && view);
        ++state.composition_count;
        QIQI_TEST_CHECK(view->GetRange(&composition_range) == S_OK);
        state.composition_text = read_text(composition_range.Get());
    }
    const auto has_property = [&](REFGUID guid) {
        ComPtr<ITfProperty> property;
        QIQI_TEST_CHECK(context->GetProperty(guid, &property) == S_OK);
        ComPtr<IEnumTfRanges> ranges;
        QIQI_TEST_CHECK(property->EnumRanges(ec, &ranges,
            composition_range ? composition_range.Get() : range.Get()) == S_OK);
        bool present = false;
        for (;;) {
            ComPtr<ITfRange> property_range;
            ULONG fetched = 0;
            const HRESULT hr = ranges->Next(1, &property_range, &fetched);
            if (hr == S_FALSE) {
                QIQI_TEST_CHECK(fetched == 0);
                break;
            }
            QIQI_TEST_CHECK(hr == S_OK && fetched == 1 && property_range);
            VARIANT value = MakeInt4Variant(0);
            const auto clear = gsl::finally([&]() noexcept { ::VariantClear(&value); });
            QIQI_TEST_CHECK(property->GetValue(ec, property_range.Get(), &value) == S_OK);
            QIQI_TEST_CHECK(value.vt == VT_EMPTY || value.vt == VT_I4);
            present = present || (value.vt == VT_I4 && value.lVal != 0);
        }
        return present;
    };
    state.composing = has_property(GUID_PROP_COMPOSING);
    state.display_attribute = has_property(GUID_PROP_ATTRIBUTE);
    return state;
}

NativeTsfState ReadNativeTsfState(gsl::not_null<ITfContext*> context, TfClientId client) {
    NativeTsfState state;
    const auto session = MakeComObject<LambdaEditSession>([&](TfEditCookie ec) -> HRESULT {
        state = ReadNativeTsfStateInSession(context, ec);
        return S_OK;
    });
    HRESULT result = E_FAIL;
    QIQI_TEST_CHECK(context->RequestEditSession(client, session.Get(), TF_ES_SYNC | TF_ES_READ, &result) == S_OK);
    QIQI_TEST_CHECK(result == S_OK);
    return state;
}

NativeTsfState WaitForNativeTsfState(gsl::not_null<ITfContext*> context, TfClientId client,
                                    std::wstring_view text, unsigned int composition_count) {
    const ULONGLONG started = ::GetTickCount64();
    for (;;) {
        const auto state = ReadNativeTsfState(context, client);
        if (state.text == text && state.composition_count == composition_count) {
            return state;
        }
        if (::GetTickCount64() - started >= 5000) {
            std::cerr << "[FAIL] Native TSF state: text_length=" << state.text.size()
                      << " expected_length=" << text.size() << " compositions=" << state.composition_count
                      << " expected_compositions=" << composition_count << '\n';
            QIQI_TEST_CHECK(false);
        }
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        QIQI_TEST_CHECK(::MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE) != WAIT_FAILED);
    }
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
            if (message.contains(L"FocusPerformance(")) focus_records.push_back(message);
            if (message.contains(L"ReloadConfigIfModified(focus_id=")) reload_records.push_back(message);
            if (message.contains(L"TsfCleanup(")) cleanup_records.push_back(message);
            if (message.contains(L"TsfLifecycle(")) lifecycle_records.push_back(message);
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
        const auto reload_ns = std::stoull(match[1].str());
        const auto position_ns = std::stoull(match[2].str());
        const auto total_ns = std::stoull(match[3].str());
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

    // Deferred sessions must not cross activation or input-context epochs.
    {
        const auto ui_calls = [&] {
            return std::tuple{mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
        };
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto old_context = MakeComObject<MockTfContext>(queue);
        BOOL deferred_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(old_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && old_context->range_->text_.empty());
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        auto new_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2 && new_context->range_->text_.empty());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).context.Get() == old_context.Get());
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 1).context.Get() == new_context.Get());
        const auto before_old_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(old_context->range_->text_.empty() && old_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(old_context->start_composition_calls_ == 0 && ui_calls() == before_old_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->range_->text_ == L"b" && new_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(new_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 0).flags == (TF_ES_ASYNCDONTCARE | TF_ES_READ));
        QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK && queue->PendingCount() == 1);
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        const auto before_old_layout = ui_calls();
        const auto old_view_calls = new_context->get_active_view_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(new_context->get_active_view_calls_ == old_view_calls && ui_calls() == before_old_layout);
        QIQI_TEST_CHECK(current_context->range_->text_ == L"d");
        QIQI_TEST_CHECK(service->OnLayoutChange(new_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 0);
        const auto current_view_calls = current_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(current_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(current_context->get_active_view_calls_ == current_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // No callback may write or create UI while the service is stopped.
        auto stopped_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(stopped_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 2);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        const auto while_stopped = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && stopped_context->range_->text_.empty());
        QIQI_TEST_CHECK(stopped_context->start_composition_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = true;
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == E_FAIL);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(stopped_context->range_->set_text_calls_ == 0 && ui_calls() == while_stopped);
        mock_tm->fail_key_sink_ = false;
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);

        // An old commit/cancel must not affect a fresh composition on the same COM context.
        for (const UINT action_key : {VK_SPACE, VK_ESCAPE}) {
            auto same_context = MakeComObject<MockTfContext>(queue);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), action_key, 0, &deferred_eaten) == S_OK && deferred_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(same_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
            const auto before_stale_action = ui_calls();
            const auto writes_before_stale_action = same_context->range_->set_text_calls_;
            const auto clears_before_stale_action = same_context->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"a");
            QIQI_TEST_CHECK(same_context->range_->set_text_calls_ == writes_before_stale_action);
            QIQI_TEST_CHECK(same_context->prop_->clear_calls_ == clears_before_stale_action &&
                            ui_calls() == before_stale_action);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(same_context->range_->text_ == L"b" && same_context->start_composition_calls_ == 2);
            QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        }

        // A delayed cancel must not terminate or hide another context's fresh composition.
        auto cancelled_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(cancelled_context.Get(), VK_ESCAPE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        auto fresh_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fresh_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->start_composition_calls_ == 1);
        const auto before_old_cancel = ui_calls();
        const auto fresh_writes = fresh_context->range_->set_text_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fresh_context->range_->text_ == L"b" && fresh_context->range_->set_text_calls_ == fresh_writes);
        QIQI_TEST_CHECK(ui_calls() == before_old_cancel);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Resetting focus invalidates old work even when activation and context pointers are unchanged.
        auto focused_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(TRUE) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(focused_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_focus_reset = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_focus_reset);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && focused_context->range_->text_ == L"b");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Multiple current snapshots remain FIFO, including commit and the next composition.
        auto fifo_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), VK_SPACE, 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->PendingCount() == 3);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"ab");
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(fifo_context->range_->set_text_calls_ == 3 && fifo_context->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(fifo_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fifo_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(fifo_context->start_composition_calls_ == 2);
        const auto fifo_view_calls = fifo_context->get_active_view_calls_;
        QIQI_TEST_CHECK(service->OnLayoutChange(fifo_context.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(fifo_context->get_active_view_calls_ == fifo_view_calls + 1);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // A -> B -> A requires an epoch, not merely pointer equality.
        auto context_a = MakeComObject<MockTfContext>(queue);
        auto context_b = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_b.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_a.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_context_switch = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_a->start_composition_calls_ == 0 && context_b->start_composition_calls_ == 0);
        QIQI_TEST_CHECK(context_a->range_->set_text_calls_ == 0 && context_b->range_->set_text_calls_ == 0);
        QIQI_TEST_CHECK(ui_calls() == before_context_switch);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_a->start_composition_calls_ == 1);
        QIQI_TEST_CHECK(context_a->range_->text_ == L"c" && context_b->range_->text_.empty());

        // Never reuse A's composition range when B becomes the current input target.
        QIQI_TEST_CHECK(service->OnLayoutChange(context_a.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1);
        const auto text_in_a = context_a->range_->text_;
        const auto reads_in_a = context_a->get_active_view_calls_;
        auto context_c = MakeComObject<MockTfContext>();
        const auto old_composition = context_a->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), VK_BACK, 0, &deferred_eaten) == S_OK && !deferred_eaten);
        QIQI_TEST_CHECK(context_c->start_composition_calls_ == 0 && context_c->range_->text_.empty());
        QIQI_TEST_CHECK(old_composition->ended_ && context_a->prop_->clear_calls_ > 0);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context_c.Get(), 'D', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(context_c->range_->text_ == L"d");
        const auto before_context_layout = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->PendingCount() == 0);
        QIQI_TEST_CHECK(context_a->range_->text_ == text_in_a && context_a->get_active_view_calls_ == reads_in_a);
        QIQI_TEST_CHECK(ui_calls() == before_context_layout);
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);

        // Host termination is another context-epoch reset, independent of activation.
        auto terminated_context = MakeComObject<MockTfContext>(queue);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'A', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'B', 0, &deferred_eaten) == S_OK && deferred_eaten);
        QIQI_TEST_CHECK(comp_sink->OnCompositionTerminated(1, nullptr) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(terminated_context.Get(), 'C', 0, &deferred_eaten) == S_OK && deferred_eaten);
        const auto before_terminated_write = ui_calls();
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(ui_calls() == before_terminated_write);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && terminated_context->range_->text_ == L"c");
        QIQI_TEST_CHECK(key_sink->OnSetFocus(FALSE) == S_OK);
    }

    const auto document_objects_before = g_com_object_count.load();
    // Document/stack notifications share the epoch barrier and old-host cleanup.
    {
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto document_a = MakeComObject<MockTfDocumentMgr>();
        auto document_b = MakeComObject<MockTfDocumentMgr>();
        document_a->events = service.Get();
        document_b->events = service.Get();
        const auto disconnect = gsl::finally([&]() noexcept {
            document_a->events = nullptr;
            document_b->events = nullptr;
        });
        auto context_a = document_a->CreateTestContext(queue);
        auto context_b = document_b->CreateTestContext(queue);
        auto modal = document_b->CreateTestContext();
        QIQI_TEST_CHECK(document_a->Push(context_a.Get()) == S_OK);
        QIQI_TEST_CHECK(document_b->Push(context_b.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'A', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        const auto composition_a = context_a->last_composition_;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'B', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(service->OnLayoutChange(context_a.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(composition_a->ended_ && context_a->range_->text_ == L"a");
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'C', 0, &handled) == S_OK && handled);
        const auto ui_before_stale = std::tuple{
            mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
        const auto old_views = context_a->get_active_view_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_a->range_->text_ == L"a" && context_a->get_active_view_calls_ == old_views);
        QIQI_TEST_CHECK((std::tuple{
            mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before_stale);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"c");
        const auto composition_b = context_b->last_composition_;
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'D', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(document_b->Push(modal.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_b.Get()) == S_OK);
        QIQI_TEST_CHECK(!composition_b->ended_);
        QIQI_TEST_CHECK(document_a->Pop(TF_POPF_ALL) == S_OK && !composition_b->ended_);
        QIQI_TEST_CHECK(document_b->Pop(0) == S_OK && !composition_b->ended_);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"cd");
        QIQI_TEST_CHECK(context_b->last_composition_ == composition_b && queue->PendingCount() == 0);

        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'E', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(service->OnLayoutChange(context_b.Get(), TF_LC_CHANGE, nullptr) == S_OK);
        const auto requests_before_pop = context_b->request_edit_session_calls_;
        context_b->edit_session_request_result_ = TF_E_DISCONNECTED;
        context_b->document_link_.reset();
        QIQI_TEST_CHECK(document_b->Pop(TF_POPF_ALL) == S_OK);
        QIQI_TEST_CHECK(context_b->request_edit_session_calls_ == requests_before_pop);
        auto fresh = document_a->CreateTestContext(queue);
        QIQI_TEST_CHECK(document_a->Push(fresh.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(fresh.Get(), 'F', 0, &handled) == S_OK && handled);
        const auto views_before_stale = context_b->get_active_view_calls_;
        const auto ends_before_stale = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && queue->RunNext() == S_OK);
        QIQI_TEST_CHECK(context_b->range_->text_ == L"cd" && context_b->get_active_view_calls_ == views_before_stale);
        QIQI_TEST_CHECK(mock_tm->end_ui_calls_ == ends_before_stale);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK && fresh->range_->text_ == L"f");
        QIQI_TEST_CHECK(composition_b->NotifyHostTermination(23) == S_OK);
        QIQI_TEST_CHECK(fresh->last_composition_ && !fresh->last_composition_->ended_);

        const auto requests_before_uninit = fresh->request_edit_session_calls_;
        fresh->edit_session_request_result_ = TF_E_DISCONNECTED;
        fresh->document_link_.reset();
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(fresh->request_edit_session_calls_ == requests_before_uninit);
        QIQI_TEST_CHECK(service->OnUninitDocumentMgr(document_a.Get()) == S_OK);
        auto fallback = document_b->CreateTestContext();
        QIQI_TEST_CHECK(document_b->Push(fallback.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'G', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(fallback->range_->text_ == L"g");
        document_a->get_top_result = E_ACCESSDENIED;
        QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
        QIQI_TEST_CHECK(fallback->last_composition_->ended_);
        document_a->get_top_result = S_OK;
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'H', 0, &handled) == S_OK && handled);
        document_a->get_top_callback = [&] { QIQI_TEST_CHECK(service->Deactivate() == S_OK); };
        QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
        document_a->get_top_callback = {};
        QIQI_TEST_CHECK(service->OnKeyDown(fallback.Get(), 'I', 0, &handled) == S_OK && !handled);
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    }

    // Focus A -> B -> A must preserve the asynchronous old-cleanup barrier.
    for (const bool remove_old_context : {false, true}) {
        auto queue = std::make_shared<MockEditSessionQueue>();
        auto document_a = MakeComObject<MockTfDocumentMgr>();
        auto document_b = MakeComObject<MockTfDocumentMgr>();
        auto context_a = document_a->CreateTestContext(queue);
        auto context_b = document_b->CreateTestContext(queue);
        QIQI_TEST_CHECK(document_a->Push(context_a.Get()) == S_OK && document_b->Push(context_b.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'A', 0, &handled) == S_OK && handled);
        QIQI_TEST_CHECK(queue->RunNext() == S_OK);
        const auto old_composition = context_a->last_composition_;
        context_a->allow_sync_edit_sessions_ = false;
        QIQI_TEST_CHECK(service->OnSetFocus(document_b.Get(), document_a.Get()) == S_OK);
        QIQI_TEST_CHECK(queue->PendingCount() == 1 && !old_composition->ended_);
        QIQI_TEST_CHECK(service->OnKeyDown(context_b.Get(), 'B', 0, &handled) == S_OK && handled);
        if (remove_old_context) {
            context_a->edit_session_request_result_ = TF_E_DISCONNECTED;
            QIQI_TEST_CHECK(document_a->Pop(TF_POPF_ALL) == S_OK);
            QIQI_TEST_CHECK(service->OnPopContext(context_a.Get()) == S_OK);
            const auto clears = context_a->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            QIQI_TEST_CHECK(!old_composition->ended_ && context_a->prop_->clear_calls_ == clears);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->range_->text_ == L"b");
            QIQI_TEST_CHECK(old_composition->NotifyHostTermination(23) == S_OK);
            QIQI_TEST_CHECK(!context_b->last_composition_->ended_);
        } else {
            QIQI_TEST_CHECK(service->OnSetFocus(document_a.Get(), document_b.Get()) == S_OK);
            QIQI_TEST_CHECK(service->OnKeyDown(context_a.Get(), 'C', 0, &handled) == S_OK && handled);
            QIQI_TEST_CHECK(gsl::at(queue->PendingRequests(), 2).flags == (TF_ES_ASYNC | TF_ES_READWRITE));
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && old_composition->ended_);
            const auto cleared_old_attribute = context_a->prop_->clear_calls_;
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_b->start_composition_calls_ == 0);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context_a->range_->text_ == L"c");
            QIQI_TEST_CHECK(context_a->prop_->clear_calls_ == cleared_old_attribute);
            QIQI_TEST_CHECK(context_a->last_composition_ != old_composition);
            context_a->allow_sync_edit_sessions_ = true;
        }
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK && queue->PendingCount() == 0);
    }

    {
        auto document = MakeComObject<MockTfDocumentMgr>();
        document->events = service.Get();
        const auto disconnect = gsl::finally([&]() noexcept { document->events = nullptr; });
        auto context = document->CreateTestContext();
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        BOOL handled = FALSE;
        QIQI_TEST_CHECK(service->OnKeyDown(context.Get(), 'A', 0, &handled) == S_OK && handled);
        const auto composition = context->last_composition_;
        const auto requests_before_removal = context->request_edit_session_calls_;
        composition->get_range_callback_ = [&] {
            context->edit_session_request_result_ = TF_E_DISCONNECTED;
            QIQI_TEST_CHECK(document->Pop(TF_POPF_ALL) == S_OK);
        };
        QIQI_TEST_CHECK(service->OnSetFocus(FALSE) == S_OK);
        composition->get_range_callback_ = {};
        QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before_removal);
        QIQI_TEST_CHECK(composition->end_composition_calls_ == 0 && context->prop_->clear_calls_ == 0);
    }
    QIQI_TEST_CHECK(g_com_object_count.load() == document_objects_before);

    // Deactivation preserves preedit text and ends only the captured host composition.
    {
        enum class Failure { None, Synchronous, ReadOnly, Request, End, Range, Property, PropertyQuery };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const auto failure : {Failure::None, Failure::Synchronous, Failure::ReadOnly, Failure::Request,
                                   Failure::End, Failure::Range, Failure::Property, Failure::PropertyQuery}) {
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL cleanup_eaten = FALSE;
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"a");
            const auto composition = context->last_composition_;
            QIQI_TEST_CHECK(composition && !composition->ended_);
            composition->end_callback_ = [&](TfEditCookie ec) {
                QIQI_TEST_CHECK(ec == 1);
                if (failure != Failure::Synchronous) QIQI_TEST_CHECK(context->in_immediate_write_session_);
            };
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
            QIQI_TEST_CHECK(queue->PendingCount() == 1);
            context->allow_sync_edit_sessions_ = failure != Failure::Synchronous;
            context->read_only_ = failure == Failure::ReadOnly;
            if (failure == Failure::Request) context->edit_session_request_result_ = E_ACCESSDENIED;
            if (failure == Failure::End) composition->end_result_ = E_FAIL;
            if (failure == Failure::Range) composition->fail_get_range_ = true;
            if (failure == Failure::Property) context->prop_->clear_result_ = E_ACCESSDENIED;
            if (failure == Failure::PropertyQuery) context->get_property_result_ = E_ACCESSDENIED;
            const auto requests_before = context->request_edit_session_calls_;
            const auto writes_before = context->range_->set_text_calls_;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            const auto cleanup_requests = failure == Failure::Synchronous ? 2u : 1u;
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(context->last_edit_session_flags_ ==
                            gsl::narrow_cast<DWORD>(
                                (failure == Failure::Synchronous ? TF_ES_ASYNC : TF_ES_SYNC) | TF_ES_READWRITE));
            QIQI_TEST_CHECK(queue->PendingCount() == (failure == Failure::Synchronous ? 2u : 1u) && !context->layout_sink_);
            const bool can_edit = failure != Failure::Synchronous && failure != Failure::ReadOnly &&
                                  failure != Failure::Request;
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(composition->ended_ == (can_edit && failure != Failure::End));
            const bool can_clear = can_edit && failure != Failure::Range && failure != Failure::PropertyQuery;
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == (can_clear ? 1u : 0u));
            if (can_clear) QIQI_TEST_CHECK(context->prop_->last_clear_cookie_ == 1);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
            BOOL shown = TRUE;
            QIQI_TEST_CHECK(mock_tm->last_ui_element_->IsShown(&shown) == S_OK && shown == FALSE);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(context->request_edit_session_calls_ == requests_before + cleanup_requests);
            QIQI_TEST_CHECK(composition->end_composition_calls_ == (can_edit ? 1u : 0u));
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            if (failure == Failure::Synchronous) {
                const auto ui_before_cleanup = std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
                QIQI_TEST_CHECK(queue->PendingCount() == 1 && queue->RunNext() == S_OK);
                QIQI_TEST_CHECK(composition->ended_ && composition->end_composition_calls_ == 1);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
                QIQI_TEST_CHECK((std::tuple{
                    mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before_cleanup);
            }
            QIQI_TEST_CHECK(queue->PendingCount() == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && context->range_->set_text_calls_ == writes_before);
        }

        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
        auto host_context = MakeComObject<MockTfContext>();
        BOOL cleanup_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(host_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto host_composition = host_context->last_composition_;
        const auto host_requests = host_context->request_edit_session_calls_;
        QIQI_TEST_CHECK(host_composition->NotifyHostTermination(17) == S_OK);
        QIQI_TEST_CHECK(host_composition->ended_ && host_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(host_context->range_->text_ == L"a");
        QIQI_TEST_CHECK(host_context->prop_->clear_calls_ == 1 && host_context->prop_->last_clear_cookie_ == 17);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(host_context->request_edit_session_calls_ == host_requests);
        QIQI_TEST_CHECK(host_composition->end_composition_calls_ == 0);

        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
        auto reentrant_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(reentrant_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto reentrant_composition = reentrant_context->last_composition_;
        reentrant_composition->end_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(reentrant_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == E_UNEXPECTED);
            QIQI_TEST_CHECK(reentrant_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(reentrant_composition->ended_ && reentrant_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(reentrant_context->prop_->clear_calls_ == 1 && reentrant_context->range_->text_ == L"a");
        reentrant_composition->end_callback_ = {};

        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
        auto interrupted_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(interrupted_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto interrupted_composition = interrupted_context->last_composition_;
        interrupted_context->prop_->clear_callback_ = [&](TfEditCookie ec) {
            QIQI_TEST_CHECK(interrupted_context->in_immediate_write_session_ && ec == 1);
            QIQI_TEST_CHECK(interrupted_composition->NotifyHostTermination(ec) == S_OK);
        };
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(interrupted_composition->ended_ && interrupted_composition->end_composition_calls_ == 0);
        QIQI_TEST_CHECK(interrupted_context->prop_->clear_calls_ == 1 && interrupted_context->range_->text_ == L"a");
        interrupted_context->prop_->clear_callback_ = {};

        // Late termination of A must not reset B's composition or candidate UI.
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
        auto previous_context = MakeComObject<MockTfContext>();
        auto current_context = MakeComObject<MockTfContext>();
        QIQI_TEST_CHECK(key_sink->OnKeyDown(previous_context.Get(), 'A', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto previous_composition = previous_context->last_composition_;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(current_context.Get(), 'B', 0, &cleanup_eaten) == S_OK && cleanup_eaten);
        const auto current_composition = current_context->last_composition_;
        const auto current_text = current_context->range_->text_;
        const auto ui_ends = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(previous_composition->NotifyHostTermination(19) == S_OK);
        QIQI_TEST_CHECK(current_context->range_->text_ == current_text && mock_tm->end_ui_calls_ == ui_ends);
        QIQI_TEST_CHECK(!current_composition->ended_);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        QIQI_TEST_CHECK(current_composition->ended_ && current_composition->end_composition_calls_ == 1);
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    }

    // Deferred cleanup survives deactivation, but cannot clear a new composition's attributes.
    {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        for (const bool host_terminates : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto old_composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(!old_composition->ended_);
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 1);
            if (host_terminates) {
                QIQI_TEST_CHECK(old_composition->NotifyHostTermination(23) == S_OK);
                QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1 && context->prop_->last_clear_cookie_ == 23);
            }
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->PendingCount() == 2 && context->range_->text_ == L"a");
            if (!host_terminates) {
                QIQI_TEST_CHECK(context->last_edit_session_flags_ == (TF_ES_ASYNC | TF_ES_READWRITE));
            }
            const auto ui_before = std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
            QIQI_TEST_CHECK(queue->RunNext(29) == S_OK && queue->PendingCount() == 1);
            QIQI_TEST_CHECK(old_composition->end_composition_calls_ == (host_terminates ? 0u : 1u));
            QIQI_TEST_CHECK(old_composition->ended_ && context->range_->text_ == L"a");
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            QIQI_TEST_CHECK((std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->range_->text_ == L"b");
            const auto new_composition = context->last_composition_;
            QIQI_TEST_CHECK(new_composition.Get() != old_composition.Get() && !new_composition->ended_);
            QIQI_TEST_CHECK(context->prop_->clear_calls_ == 1);
            context->allow_sync_edit_sessions_ = true;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && new_composition->ended_);
        }

        for (const HRESULT failure : {E_ACCESSDENIED, TF_E_DISCONNECTED}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            const auto composition = context->last_composition_;
            context->allow_sync_edit_sessions_ = false;
            context->async_edit_session_result_ = failure;
            QIQI_TEST_CHECK(service->Deactivate() == S_OK && queue->PendingCount() == 0);
            QIQI_TEST_CHECK(!composition->ended_ && context->prop_->clear_calls_ == 0);
            QIQI_TEST_CHECK(context->range_->text_ == L"a" && !context->layout_sink_);
        }
        const auto initial_objects = g_com_object_count.load();
        for (const bool execute_cleanup : {false, true}) {
            auto queue = std::make_shared<MockEditSessionQueue>();
            auto context = MakeComObject<MockTfContext>(queue);
            auto peer = MakeComObject<QiQiTextService>(g_dll_instance);
            auto peer_manager = MakeComObject<MockTfThreadMgr>();
            BOOL eaten_deferred = FALSE;
            QIQI_TEST_CHECK(peer->Activate(peer_manager.Get(), 2) == S_OK);
            QIQI_TEST_CHECK(peer->OnKeyDown(context.Get(), 'A', 0, &eaten_deferred) == S_OK && eaten_deferred);
            QIQI_TEST_CHECK(queue->RunNext() == S_OK);
            context->allow_sync_edit_sessions_ = false;
            QIQI_TEST_CHECK(peer->Deactivate() == S_OK && queue->PendingCount() == 1);
            peer.Reset();
            peer_manager.Reset();
            if (execute_cleanup) {
                QIQI_TEST_CHECK(queue->RunNext() == S_OK && context->last_composition_->ended_);
            } else {
                QIQI_TEST_CHECK(queue->ClearForCleanup() == S_OK && queue->PendingCount() == 0);
                QIQI_TEST_CHECK(!context->last_composition_->ended_);
            }
        }
        QIQI_TEST_CHECK(g_com_object_count.load() == initial_objects);
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
    }

    // Real TSF, private document, no system TIP activation: cleanup must remove host composition state.
    {
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        ComPtr<ITfThreadMgr2> native_manager;
        QIQI_TEST_CHECK(::CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&native_manager)) == S_OK);
        TfClientId native_client = TF_CLIENTID_NULL;
        QIQI_TEST_CHECK(native_manager->ActivateEx(&native_client, TF_TMAE_NOACTIVATETIP) == S_OK);
        const auto deactivate_native = gsl::finally([&]() noexcept {
            std::ignore = log::CheckHResult(native_manager->Deactivate(), "NativeCleanupTest::Deactivate");
        });
        ComPtr<ITfDocumentMgr> document;
        QIQI_TEST_CHECK(native_manager->CreateDocumentMgr(&document) == S_OK);
        ComPtr<ITfContext> context;
        TfEditCookie owner_cookie = 0;
        QIQI_TEST_CHECK(document->CreateContext(native_client, 0, nullptr, &context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(document->Push(context.Get()) == S_OK);
        const auto pop_native = gsl::finally([&]() noexcept {
            std::ignore = log::CheckHResult(document->Pop(TF_POPF_ALL), "NativeCleanupTest::Pop");
        });
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), native_client) == S_OK);
        BOOL native_eaten = FALSE;
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &native_eaten) == S_OK && native_eaten);
        const auto before = WaitForNativeTsfState(context.Get(), native_client, L"a", 1);
        QIQI_TEST_CHECK(before.composing && before.display_attribute);
        ComPtr<ITfContextOwnerServices> owner_services;
        QIQI_TEST_CHECK(context.As(&owner_services) == S_OK);
        const auto cleanup_layout_sink = MakeComObject<NativeCleanupLayoutSink>();
        ScopedTfSinkConnection layout_connection;
        QIQI_TEST_CHECK(layout_connection.Advise(context.Get(), IID_ITfTextLayoutSink, cleanup_layout_sink.Get()));
        cleanup_layout_sink->callback = [&]() -> HRESULT {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            const auto pending = ReadNativeTsfState(context.Get(), native_client);
            QIQI_TEST_CHECK(pending.text == L"a" && pending.composition_count == 1);
            return S_OK;
        };
        QIQI_TEST_CHECK(owner_services->OnLayoutChange() == S_OK && cleanup_layout_sink->called);
        const auto ended = WaitForNativeTsfState(context.Get(), native_client, L"a", 0);
        QIQI_TEST_CHECK(!ended.composing && !ended.display_attribute);

        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), native_client) == S_OK);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'A', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aa", 1).composing);
        cleanup_layout_sink->called = false;
        cleanup_layout_sink->callback = [&]() -> HRESULT {
            QIQI_TEST_CHECK(service->Deactivate() == S_OK);
            QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), native_client) == S_OK);
            const auto ui_before = std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_};
            QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'B', 0, &native_eaten) == S_OK && native_eaten);
            QIQI_TEST_CHECK((std::tuple{
                mock_tm->begin_ui_calls_, mock_tm->update_ui_calls_, mock_tm->end_ui_calls_}) == ui_before);
            QIQI_TEST_CHECK(ReadNativeTsfState(context.Get(), native_client).text == L"aa");
            return S_OK;
        };
        QIQI_TEST_CHECK(owner_services->OnLayoutChange() == S_OK && cleanup_layout_sink->called);
        const auto fresh = WaitForNativeTsfState(context.Get(), native_client, L"aab", 1);
        QIQI_TEST_CHECK(fresh.composition_text == L"b" && fresh.composing && fresh.display_attribute);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        const auto final_state = WaitForNativeTsfState(context.Get(), native_client, L"aab", 0);
        QIQI_TEST_CHECK(!final_state.composing && !final_state.display_attribute);
        layout_connection.Reset();
        cleanup_layout_sink->callback = {};

        // Forward actual stack notifications without changing the user's native document focus.
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), native_client) == S_OK);
        const auto document_events = MakeComObject<NativeDocumentEventSink>(
            gsl::not_null<QiQiTextService*>(service.Get()));
        ScopedTfSinkConnection document_connection;
        QIQI_TEST_CHECK(document_connection.Advise(native_manager.Get(), IID_ITfThreadMgrEventSink, document_events.Get()));
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), 'C', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aabc", 1).composing);
        ComPtr<ITfContext> modal_context;
        QIQI_TEST_CHECK(document->CreateContext(native_client, 0, nullptr, &modal_context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(document->Push(modal_context.Get()) == S_OK);
        QIQI_TEST_CHECK(service->OnSetFocus(document.Get(), document.Get()) == S_OK);
        QIQI_TEST_CHECK(document->Pop(0) == S_OK);
        QIQI_TEST_CHECK(WaitForNativeTsfState(context.Get(), native_client, L"aabc", 1).display_attribute);
        ComPtr<ITfDocumentMgr> second_document;
        ComPtr<ITfContext> second_context;
        QIQI_TEST_CHECK(native_manager->CreateDocumentMgr(&second_document) == S_OK);
        QIQI_TEST_CHECK(second_document->CreateContext(
            native_client, 0, nullptr, &second_context, &owner_cookie) == S_OK);
        QIQI_TEST_CHECK(second_document->Push(second_context.Get()) == S_OK);
        bool second_on_stack = true;
        const auto pop_second = gsl::finally([&]() noexcept {
            if (second_on_stack) {
                std::ignore = log::CheckHResult(second_document->Pop(TF_POPF_ALL), "NativeDocumentTest::Pop");
            }
        });
        QIQI_TEST_CHECK(service->OnSetFocus(second_document.Get(), document.Get()) == S_OK);
        const auto switched = WaitForNativeTsfState(context.Get(), native_client, L"aabc", 0);
        QIQI_TEST_CHECK(!switched.composing && !switched.display_attribute);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(second_context.Get(), 'D', 0, &native_eaten) == S_OK && native_eaten);
        QIQI_TEST_CHECK(WaitForNativeTsfState(second_context.Get(), native_client, L"d", 1).composing);
        const auto ui_ends_before_pop = mock_tm->end_ui_calls_;
        QIQI_TEST_CHECK(second_document->Pop(TF_POPF_ALL) == S_OK);
        second_on_stack = false;
        QIQI_TEST_CHECK(document_events->pushes == 2 && document_events->pops == 2 && document_events->uninits == 1);
        QIQI_TEST_CHECK(mock_tm->end_ui_calls_ > ui_ends_before_pop);
        QIQI_TEST_CHECK(key_sink->OnKeyDown(context.Get(), VK_BACK, 0, &native_eaten) == S_OK && !native_eaten);
        QIQI_TEST_CHECK(service->Deactivate() == S_OK);
        document_connection.Reset();
        QIQI_TEST_CHECK(service->Activate(mock_tm.Get(), 1) == S_OK);
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

    // Test Shift tap mode toggle while composing (cancels composition and switches to English)
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == FALSE);
    // Auto-repeat VK_SHIFT while still within 350ms preserves shift_tap_candidate_
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyDown(mock_ctx.Get(), VK_SHIFT, 1LL << 30, &eaten)) && eaten == FALSE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnTestKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);
    QIQI_TEST_CHECK(SUCCEEDED(key_sink->OnKeyUp(mock_ctx.Get(), VK_SHIFT, 0, &eaten)) && eaten == TRUE);

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
                const HWND foreground_before = ::GetForegroundWindow();
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
}

void RunTsfServiceTests() {
    TestQiQiTextServiceLifecycleAndKeyFlow();
    TestFocusPerformanceLogs();
}

} // namespace qiqi::test
