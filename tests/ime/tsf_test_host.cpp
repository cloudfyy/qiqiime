#include "common/com_object.h"
#include "common/file_util.h"
#include "common/scoped_handle.h"
#include "common/version.h"
#include "common/window.h"
#include "common/win32_ui_util.h"
#include "ime/comp/access_policy.h"
#include "ime/tsf/edit_session.h"

#include <windows.h>
#include <commctrl.h>
#include <inputscope.h>
#include <msctf.h>
#include <objbase.h>
#include <wrl/client.h>
#include <gsl/gsl>

#include <array>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

namespace qiqi {
HINSTANCE g_dll_instance = nullptr;
std::atomic<long> g_com_object_count{0};
} // namespace qiqi

namespace {

using Microsoft::WRL::ComPtr;
using namespace qiqi;

constexpr const wchar_t* kHostWindowClass = L"QiQiTsfRestrictedTestHostWindow";

enum class HostControlId : int {
    EditNormal = 1001,
    EditEsPassword = 1002,
    EditEsReadOnly = 1003,
    EditEsNumber = 1004,
    EditScopePassword = 1005,
    EditScopePin = 1006,
    EditScopePrivate = 1007,
    EditScopeUrlEmail = 1008,
    EditCompDisabled = 1009,
    EditCompEmpty = 1010,
    EditDynamic = 1011,

    BtnDynNormal = 1101,
    BtnDynPassword = 1102,
    BtnDynReadOnly = 1103,
    BtnDynDisabled = 1104,
    BtnDynPrivate = 1105,
    BtnDynDelayedDisable = 1106,
    BtnDynDelayedReadOnly = 1107,
    BtnClearAll = 1108,

    InspectorBox = 1201
};

enum class DynamicMode : int {
    Normal = 0,
    EsPassword,
    EsReadOnly,
    CompartmentDisabled,
    ScopePrivate
};

constexpr UINT_PTR kTimerInspector = 1;
constexpr UINT_PTR kTimerDelayedAction = 2;

using SetInputScopesFn = HRESULT(WINAPI*)(
    HWND hwnd, const InputScope* pInputScopes, UINT cInputScopes,
    PWSTR* ppszPhraseList, UINT cPhrases, PWSTR pszRegExp, PWSTR pszSRGS);

void ApplyWindowInputScopes(HWND hwnd, std::span<const InputScope> scopes) noexcept {
    static const HMODULE msctf = ::LoadLibraryW(L"msctf.dll");
    if (!msctf) return;
#pragma warning(suppress : 26490)
    static const auto set_scopes = reinterpret_cast<SetInputScopesFn>(
        ::GetProcAddress(msctf, "SetInputScopes"));
    if (!set_scopes) return;
    if (scopes.empty()) {
        std::ignore = set_scopes(hwnd, nullptr, 0, nullptr, 0, nullptr, nullptr);
    } else {
        std::ignore = set_scopes(
            hwnd, scopes.data(), gsl::narrow_cast<UINT>(scopes.size()),
            nullptr, 0, nullptr, nullptr);
    }
}

struct FieldSpec {
    HostControlId id;
    const wchar_t* label;
    const wchar_t* expectation;
    DWORD extra_style;
    const wchar_t* initial_text;
};

constexpr std::array<FieldSpec, 11> kFields{{
    {HostControlId::EditNormal,
     L"1. 普通输入框 (Standard EDIT / IS_DEFAULT)",
     L"预期：Allowed（正常五笔输入 + 正常动态调频）",
     0, L""},
    {HostControlId::EditEsPassword,
     L"2. Win32 原生密码框 (GWL_STYLE: ES_PASSWORD)",
     L"预期：Blocked（按键直接上屏英文密码，Shift/Ctrl+Space 不切模式）",
     ES_PASSWORD, L""},
    {HostControlId::EditEsReadOnly,
     L"3. Win32 原生只读框 (GWL_STYLE: ES_READONLY)",
     L"预期：Blocked（禁止输入与组合，允许选中复制）",
     ES_READONLY, L"只读示例文本 Read-only sample"},
    {HostControlId::EditEsNumber,
     L"4. Win32 数字框 (GWL_STYLE: ES_NUMBER + IS_NUMBER)",
     L"预期：Allowed（不强制禁用中文输入法）",
     ES_NUMBER, L""},
    {HostControlId::EditScopePassword,
     L"5. InputScope 密码框 (IS_PASSWORD，无 ES_PASSWORD 样式，模拟网页/WPF/自绘密码框)",
     L"预期：Blocked（通过 ITfInputScope 识别并拦截组合与快捷键）",
     0, L""},
    {HostControlId::EditScopePin,
     L"6. InputScope PIN 码框 (IS_NUMERIC_PIN + IS_ALPHANUMERIC_PIN)",
     L"预期：Blocked（通过多 InputScope 数组识别 PIN 并拦截）",
     0, L""},
    {HostControlId::EditScopePrivate,
     L"7. InputScope 无痕/私密框 (IS_PRIVATE，模拟浏览器 InPrivate 搜索框)",
     L"预期：Private（可正常中文打字上屏，但禁止动态调频、Ctrl+= 造词、置顶/删词）",
     0, L""},
    {HostControlId::EditScopeUrlEmail,
     L"8. InputScope 网址/邮箱框 (IS_URL + IS_EMAIL_SMTPEMAILADDRESS)",
     L"预期：Allowed（普通非敏感作用域，允许正常中文输入）",
     0, L""},
    {HostControlId::EditCompDisabled,
     L"9. TSF 禁用上下文框 (GUID_COMPARTMENT_KEYBOARD_DISABLED = 1)",
     L"预期：Blocked（聚焦时自动置位当前 Context 的 KEYBOARD_DISABLED=1）",
     0, L""},
    {HostControlId::EditCompEmpty,
     L"10. TSF 空上下文框 (GUID_COMPARTMENT_EMPTYCONTEXT = 1，模拟游戏非聊天态)",
     L"预期：Blocked（聚焦时自动置位当前 Context 的 EMPTYCONTEXT=1）",
     0, L""},
    {HostControlId::EditDynamic,
     L"11. 动态切换实验框 (支持输入组合中途原地切换状态 / 2 秒延时切换)",
     L"预期：组合中输入 rcn 等编码不按空格，点击下方或延时切换为禁用/只读/密码时立即安全清理组合",
     0, L""}
}};

struct HostAppState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    ScopedGdiHandle<HFONT> font;
    ScopedGdiHandle<HFONT> mono_font;
    ComPtr<ITfThreadMgr> thread_mgr;
    TfClientId client_id = TF_CLIENTID_NULL;
    DynamicMode dynamic_mode = DynamicMode::Normal;
    DynamicMode pending_delayed_mode = DynamicMode::Normal;
    int countdown_ticks = 0;
    std::wstring last_inspector_text;

    HWND Child(HostControlId id) const noexcept {
        return ::GetDlgItem(hwnd, std::to_underlying(id));
    }

    void EnsureTsfClient() {
        if (!thread_mgr) {
            std::ignore = ::CoCreateInstance(
                CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr));
        }
        if (thread_mgr && client_id == TF_CLIENTID_NULL) {
            std::ignore = thread_mgr->Activate(&client_id);
        }
    }

    void ShutdownTsfClient() {
        if (thread_mgr && client_id != TF_CLIENTID_NULL) {
            std::ignore = thread_mgr->Deactivate();
            client_id = TF_CLIENTID_NULL;
        }
        thread_mgr.Reset();
    }

    ComPtr<ITfContext> GetFocusedContext() const {
        if (!thread_mgr) return {};
        ComPtr<ITfDocumentMgr> doc_mgr;
        if (FAILED(thread_mgr->GetFocus(&doc_mgr)) || !doc_mgr) return {};
        ComPtr<ITfContext> ctx;
        if (FAILED(doc_mgr->GetTop(&ctx)) || !ctx) return {};
        return ctx;
    }

    void SetFocusedContextCompartment(REFGUID guid, LONG val) {
        EnsureTsfClient();
        const ComPtr<ITfContext> ctx = GetFocusedContext();
        if (!ctx || client_id == TF_CLIENTID_NULL) return;
        ComPtr<ITfCompartmentMgr> comp_mgr;
        if (FAILED(ctx.As(&comp_mgr)) || !comp_mgr) return;
        ComPtr<ITfCompartment> comp;
        if (FAILED(comp_mgr->GetCompartment(guid, &comp)) || !comp) return;
        std::ignore = WriteCompartmentInt4(comp.Get(), client_id, val);
    }

    void ApplyDynamicMode(DynamicMode mode) {
        dynamic_mode = mode;
        const HWND edit = Child(HostControlId::EditDynamic);
        if (!edit) return;

        // Reset all dynamic overrides first
        ::SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
        ::SendMessageW(edit, EM_SETPASSWORDCHAR, 0, 0);
        constexpr std::array<InputScope, 1> default_scope{{IS_DEFAULT}};
        ApplyWindowInputScopes(edit, default_scope);
        if (::GetFocus() == edit) {
            SetFocusedContextCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
            SetFocusedContextCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, 0);
        }

        switch (mode) {
        case DynamicMode::Normal:
            break;
        case DynamicMode::EsPassword:
            ::SendMessageW(edit, EM_SETPASSWORDCHAR, L'*', 0);
            break;
        case DynamicMode::EsReadOnly:
            ::SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
            break;
        case DynamicMode::CompartmentDisabled:
            if (::GetFocus() == edit) {
                SetFocusedContextCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, 1);
            }
            break;
        case DynamicMode::ScopePrivate: {
            constexpr std::array<InputScope, 1> priv_scope{{IS_PRIVATE}};
            ApplyWindowInputScopes(edit, priv_scope);
            break;
        }
        }
        ::InvalidateRect(edit, nullptr, TRUE);
        UpdateInspector();
    }

    void OnEditFocusGained(HostControlId id) {
        EnsureTsfClient();
        if (id == HostControlId::EditCompDisabled) {
            SetFocusedContextCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, 1);
            SetFocusedContextCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, 0);
        } else if (id == HostControlId::EditCompEmpty) {
            SetFocusedContextCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
            SetFocusedContextCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, 1);
        } else if (id == HostControlId::EditDynamic) {
            SetFocusedContextCompartment(
                GUID_COMPARTMENT_KEYBOARD_DISABLED,
                dynamic_mode == DynamicMode::CompartmentDisabled ? 1 : 0);
            SetFocusedContextCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, 0);
        } else {
            SetFocusedContextCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, 0);
            SetFocusedContextCompartment(GUID_COMPARTMENT_EMPTYCONTEXT, 0);
        }
        UpdateInspector();
    }

    void UpdateInspector() {
        EnsureTsfClient();
        const HWND focus_hwnd = ::GetFocus();
        const int focus_id = focus_hwnd ? ::GetDlgCtrlID(focus_hwnd) : 0;
        const ComPtr<ITfContext> ctx = GetFocusedContext();

        InputScopeClassification scope_cls = InputScopeClassification::Unknown;
        std::wstring scope_detail = L"未查询";
        if (ctx && client_id != TF_CLIENTID_NULL) {
            const auto session = MakeComObject<LambdaEditSession>([&](TfEditCookie ec) -> HRESULT {
                const auto res = QueryContextInputScopeInSession(
                    ec, gsl::not_null<ITfContext*>(ctx.Get()), [] { return true; });
                scope_cls = res.classification;
                scope_detail = std::format(
                    L"{} (stage={}, hr=0x{:08X})",
                    Utf8ToWide(InputScopeClassificationToString(res.classification)),
                    Utf8ToWide(AccessFactStageToString(res.stage)),
                    gsl::narrow_cast<unsigned int>(res.hr));
                return S_OK;
            });
            HRESULT session_hr = S_OK;
            std::ignore = ctx->RequestEditSession(
                client_id, session.Get(), TF_ES_SYNC | TF_ES_READ, &session_hr);
        }

        const ContextFacts facts = ReadFastContextFacts(
            ctx.Get(), scope_cls, [] { return true; }, true);
        const AccessPolicy policy = EvaluatePolicy(facts);

        const auto edit_style = InspectConfirmedCurrentThreadEditWindow(focus_hwnd);
        std::wstring countdown_msg;
        if (countdown_ticks > 0) {
            countdown_msg = std::format(
                L"  [倒计时 {:.1f}s 后自动切换动态框状态... 请立即在 11 号框内输入编码！]",
                static_cast<double>(countdown_ticks) * 0.2);
        }

        const std::wstring report = std::format(
            L"当前焦点控件 ID: {} (HWND=0x{:X}) | 动态框模式: {}{}\r\n"
            L"底层信号 (ContextFacts): has_context={} | KEYBOARD_DISABLED={} | EMPTYCONTEXT={} | TF_SD_READONLY={} | "
            L"NativeEdit(is_edit={}, ES_PASSWORD={}, ES_READONLY={}) | InputScope={}\r\n"
            L"归约决策 (AccessPolicy): can_input={} ({}) | can_learn={} ({}) | should_reset_active={}",
            focus_id,
            reinterpret_cast<uintptr_t>(focus_hwnd),
            static_cast<int>(dynamic_mode),
            countdown_msg,
            facts.has_context,
            facts.is_keyboard_disabled,
            facts.is_empty_context,
            facts.is_readonly,
            edit_style.is_native_edit,
            edit_style.is_password,
            edit_style.is_readonly,
            scope_detail,
            policy.can_input,
            policy.can_input ? L"允许中文输入" : L"拦截/放行英文",
            policy.can_learn,
            policy.can_learn ? L"允许调频/造词" : L"禁止调频/造词",
            policy.should_reset_active);

        if (report != last_inspector_text) {
            last_inspector_text = report;
            ::SetWindowTextW(Child(HostControlId::InspectorBox), report.c_str());
        }
    }
};

bool CreateHostControls(HostAppState& app) noexcept {
    const UINT dpi = ::GetDpiForWindow(app.hwnd);
    const auto s = [dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };
    app.font = CreateUiFont(s(13));
    app.mono_font = CreateUiFont(s(13), FW_BOLD);
    if (!app.font || !app.mono_font) return false;

    int y = s(10);
    const int left = s(14);
    const int label_w = s(460);
    const int edit_x = left + label_w + s(8);
    const int edit_w = s(480);
    const int row_h = s(24);

    for (const auto& field : kFields) {
        HWND lbl = ::CreateWindowExW(
            0, L"STATIC", field.label, WS_CHILD | WS_VISIBLE | SS_LEFT,
            left, y + s(3), label_w, row_h, app.hwnd, nullptr, app.instance, nullptr);
        if (!lbl) return false;
        ::SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(app.font.Get()), TRUE);

        HWND ed = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", field.initial_text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | field.extra_style,
            edit_x, y, edit_w, row_h, app.hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(std::to_underlying(field.id))),
            app.instance, nullptr);
        if (!ed) return false;
        ::SendMessageW(ed, WM_SETFONT, reinterpret_cast<WPARAM>(app.font.Get()), TRUE);
        ::SendMessageW(ed, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(field.expectation));

        y += s(30);
    }

    // Configure InputScopes on specific controls
    {
        constexpr std::array<InputScope, 1> s_num{{IS_NUMBER}};
        ApplyWindowInputScopes(app.Child(HostControlId::EditEsNumber), s_num);

        constexpr std::array<InputScope, 1> s_pwd{{IS_PASSWORD}};
        ApplyWindowInputScopes(app.Child(HostControlId::EditScopePassword), s_pwd);

        constexpr std::array<InputScope, 2> s_pin{{IS_NUMERIC_PIN, IS_ALPHANUMERIC_PIN}};
        ApplyWindowInputScopes(app.Child(HostControlId::EditScopePin), s_pin);

        constexpr std::array<InputScope, 1> s_priv{{IS_PRIVATE}};
        ApplyWindowInputScopes(app.Child(HostControlId::EditScopePrivate), s_priv);

        constexpr std::array<InputScope, 2> s_url{{IS_URL, IS_EMAIL_SMTPEMAILADDRESS}};
        ApplyWindowInputScopes(app.Child(HostControlId::EditScopeUrlEmail), s_url);
    }

    y += s(4);
    struct BtnSpec {
        HostControlId id;
        const wchar_t* text;
        int width;
    };
    constexpr std::array<BtnSpec, 8> kButtons{{
        {HostControlId::BtnDynNormal, L"11号框->普通", 105},
        {HostControlId::BtnDynPassword, L"11号框->ES_PASSWORD", 145},
        {HostControlId::BtnDynReadOnly, L"11号框->ES_READONLY", 145},
        {HostControlId::BtnDynDisabled, L"11号框->DISABLED", 130},
        {HostControlId::BtnDynPrivate, L"11号框->IS_PRIVATE", 135},
        {HostControlId::BtnDynDelayedDisable, L"3秒后自动变DISABLED(测组合中打断)", 225},
        {HostControlId::BtnDynDelayedReadOnly, L"3秒后自动变READONLY", 150},
        {HostControlId::BtnClearAll, L"清空全部输入", 105}
    }};

    int bx = left;
    for (const auto& btn : kButtons) {
        const int bw = s(btn.width);
        if (bx + bw > left + label_w + s(8) + edit_w) {
            bx = left;
            y += s(32);
        }
        HWND b = ::CreateWindowExW(
            0, L"BUTTON", btn.text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            bx, y, bw, s(26), app.hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(std::to_underlying(btn.id))),
            app.instance, nullptr);
        if (!b) return false;
        ::SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(app.font.Get()), TRUE);
        bx += bw + s(6);
    }

    y += s(36);
    HWND inspector = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
        left, y, label_w + s(8) + edit_w, s(82), app.hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(std::to_underlying(HostControlId::InspectorBox))),
        app.instance, nullptr);
    if (!inspector) return false;
    ::SendMessageW(inspector, WM_SETFONT, reinterpret_cast<WPARAM>(app.mono_font.Get()), TRUE);

    return true;
}

LRESULT CALLBACK HostWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) noexcept {
    auto* app = BindOrGetWindowUserData<HostAppState>(hwnd, msg, lparam);
    if (!app || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    try {
        switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (code == EN_SETFOCUS && id >= std::to_underlying(HostControlId::EditNormal) &&
                id <= std::to_underlying(HostControlId::EditDynamic)) {
                app->OnEditFocusGained(gsl::narrow_cast<HostControlId>(id));
                return 0;
            }
            if (code == BN_CLICKED) {
                switch (gsl::narrow_cast<HostControlId>(id)) {
                case HostControlId::BtnDynNormal:
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->ApplyDynamicMode(DynamicMode::Normal);
                    return 0;
                case HostControlId::BtnDynPassword:
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->ApplyDynamicMode(DynamicMode::EsPassword);
                    return 0;
                case HostControlId::BtnDynReadOnly:
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->ApplyDynamicMode(DynamicMode::EsReadOnly);
                    return 0;
                case HostControlId::BtnDynDisabled:
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->ApplyDynamicMode(DynamicMode::CompartmentDisabled);
                    return 0;
                case HostControlId::BtnDynPrivate:
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->ApplyDynamicMode(DynamicMode::ScopePrivate);
                    return 0;
                case HostControlId::BtnDynDelayedDisable:
                    app->ApplyDynamicMode(DynamicMode::Normal);
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->pending_delayed_mode = DynamicMode::CompartmentDisabled;
                    app->countdown_ticks = 15; // 15 * 200ms = 3.0s
                    ::SetTimer(hwnd, kTimerDelayedAction, 200, nullptr);
                    app->UpdateInspector();
                    return 0;
                case HostControlId::BtnDynDelayedReadOnly:
                    app->ApplyDynamicMode(DynamicMode::Normal);
                    ::SetFocus(app->Child(HostControlId::EditDynamic));
                    app->pending_delayed_mode = DynamicMode::EsReadOnly;
                    app->countdown_ticks = 15;
                    ::SetTimer(hwnd, kTimerDelayedAction, 200, nullptr);
                    app->UpdateInspector();
                    return 0;
                case HostControlId::BtnClearAll:
                    for (const auto& f : kFields) {
                        if (f.id != HostControlId::EditEsReadOnly) {
                            ::SetWindowTextW(app->Child(f.id), L"");
                        }
                    }
                    ::SetFocus(app->Child(HostControlId::EditNormal));
                    return 0;
                default:
                    break;
                }
            }
            break;
        }
        case WM_TIMER:
            if (wparam == kTimerInspector) {
                app->UpdateInspector();
                return 0;
            }
            if (wparam == kTimerDelayedAction) {
                if (app->countdown_ticks > 0) {
                    --app->countdown_ticks;
                    if (app->countdown_ticks == 0) {
                        ::KillTimer(hwnd, kTimerDelayedAction);
                        app->ApplyDynamicMode(app->pending_delayed_mode);
                    } else {
                        app->UpdateInspector();
                    }
                }
                return 0;
            }
            break;
        case WM_DESTROY:
            ::KillTimer(hwnd, kTimerInspector);
            ::KillTimer(hwnd, kTimerDelayedAction);
            app->ShutdownTsfClient();
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
        }
    } catch (const std::exception&) {
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(
    _In_ HINSTANCE instance,
    _In_opt_ HINSTANCE /*prev_instance*/,
    _In_ PWSTR /*cmd_line*/,
    _In_ int show_cmd) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto co_cleanup = gsl::finally([co_hr]() noexcept {
        if (SUCCEEDED(co_hr)) ::CoUninitialize();
    });

    const INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    ::InitCommonControlsEx(&icc);

    HostAppState app{};
    app.instance = instance;
    g_dll_instance = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = app.instance;
    wc.lpfnWndProc = &HostWndProc;
    wc.lpszClassName = kHostWindowClass;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return 1;
    }

    const auto monitor = QueryActiveMonitor(nullptr);
    const RECT rect = ComputeCenteredToolWindowRect(monitor, 1000, 540, monitor.dpi);
    const std::wstring title = std::format(
        L"奇奇五笔 任务36 禁输入与特殊上下文真实测试宿主 v{} ({})",
        version::kVersionW, version::kArchW);

    ScopedWindow window(::CreateWindowExW(
        0, kHostWindowClass, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, app.instance, &app));
    if (!window) {
        return 1;
    }
    app.hwnd = window.Get();
    if (!CreateHostControls(app)) {
        return 1;
    }

    ::ShowWindow(window.Get(), show_cmd);
    ::UpdateWindow(window.Get());
    ::SetFocus(app.Child(HostControlId::EditNormal));
    ::SetTimer(window.Get(), kTimerInspector, 250, nullptr);
    app.UpdateInspector();

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(window.Get(), &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    std::ignore = window.Detach();
    return 0;
}
