#pragma once
#include <windows.h>
#include <inputscope.h>
#include <msctf.h>
#include <oleauto.h>
#include <wrl/client.h>
#include <gsl/gsl>
#include <cwchar>
#include <span>
#include <string_view>

#include "common/com_object.h"
#include "common/logger.h"
#include "ime/comp/state.h"

namespace qiqi {

// Standard GUID_PROP_INPUTSCOPE: {1713DD5A-68E7-4A5B-9AF6-592A595C778D}
inline constexpr GUID kGuidPropInputScope = {
    0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}
};

enum class AccessFactStage : uint8_t {
    None = 0,
    ContextMissing,
    CompartmentMgrQuery,
    DisabledCompartmentGet,
    DisabledCompartmentValue,
    EmptyCompartmentGet,
    EmptyCompartmentValue,
    ContextStatus,
    ActiveViewQuery,
    ViewWindowQuery,
    WindowStyleQuery,
    AppPropertyGet,
    RangeQuery,
    PropertyValueGet,
    InputScopeQuery,
    GetInputScopesCall
};

enum class InputScopeClassification : uint8_t {
    Unknown = 0,     // Optional scope absent, unsupported, or empty
    Pending,         // Scope requires an edit session that has not completed yet
    Allowed,         // Explicit non-sensitive scope (e.g. IS_DEFAULT, IS_NUMBER, IS_URL)
    Private,         // IS_PRIVATE: allow normal input/candidates, forbid learning/persistence
    PasswordOrPin,   // IS_PASSWORD, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN, IS_ALPHANUMERIC_PIN, IS_ALPHANUMERIC_PIN_SET
    QueryError       // Unexpected runtime failure while reading scope
};

struct ContextFacts {
    bool has_context = false;
    bool has_query_error = false;
    AccessFactStage stage = AccessFactStage::None;
    HRESULT error_hr = S_OK;
    VARTYPE error_vt = VT_EMPTY;

    bool is_keyboard_disabled = false;
    bool is_empty_context = false;
    bool is_readonly = false;
    bool is_native_password_edit = false;
    bool is_native_readonly_edit = false;
    InputScopeClassification scope_state = InputScopeClassification::Unknown;

    [[nodiscard]] constexpr bool IsBlocked() const noexcept {
        return !has_context || is_keyboard_disabled || is_empty_context || is_readonly ||
               is_native_password_edit || is_native_readonly_edit ||
               scope_state == InputScopeClassification::PasswordOrPin;
    }

    [[nodiscard]] constexpr bool HasError() const noexcept {
        return has_query_error || scope_state == InputScopeClassification::QueryError;
    }
};

struct AccessPolicy {
    bool can_input = false;
    bool can_learn = false;
    bool should_reset_active = false;

    constexpr bool operator==(const AccessPolicy&) const noexcept = default;
};

[[nodiscard]] constexpr AccessPolicy EvaluatePolicy(const ContextFacts& facts) noexcept {
    if (facts.HasError()) {
        return {.can_input = false, .can_learn = false, .should_reset_active = true};
    }
    if (facts.IsBlocked()) {
        return {.can_input = false, .can_learn = false, .should_reset_active = true};
    }
    if (facts.scope_state == InputScopeClassification::Pending) {
        return {.can_input = false, .can_learn = false, .should_reset_active = false};
    }
    const bool allow_learn = (facts.scope_state != InputScopeClassification::Private);
    return {.can_input = true, .can_learn = allow_learn, .should_reset_active = false};
}

[[nodiscard]] constexpr std::string_view AccessFactStageToString(AccessFactStage stage) noexcept {
    switch (stage) {
        case AccessFactStage::None:                    return "none";
        case AccessFactStage::ContextMissing:          return "context-missing";
        case AccessFactStage::CompartmentMgrQuery:     return "compartment-mgr-qi";
        case AccessFactStage::DisabledCompartmentGet:  return "disabled-comp-get";
        case AccessFactStage::DisabledCompartmentValue:return "disabled-comp-val";
        case AccessFactStage::EmptyCompartmentGet:     return "empty-comp-get";
        case AccessFactStage::EmptyCompartmentValue:   return "empty-comp-val";
        case AccessFactStage::ContextStatus:           return "context-status";
        case AccessFactStage::ActiveViewQuery:         return "active-view";
        case AccessFactStage::ViewWindowQuery:         return "view-hwnd";
        case AccessFactStage::WindowStyleQuery:        return "window-style";
        case AccessFactStage::AppPropertyGet:          return "app-property-get";
        case AccessFactStage::RangeQuery:              return "range-query";
        case AccessFactStage::PropertyValueGet:        return "property-val-get";
        case AccessFactStage::InputScopeQuery:         return "input-scope-qi";
        case AccessFactStage::GetInputScopesCall:      return "get-input-scopes";
        default:                                       return "unknown";
    }
}

[[nodiscard]] constexpr std::string_view InputScopeClassificationToString(InputScopeClassification cls) noexcept {
    switch (cls) {
        case InputScopeClassification::Unknown:       return "unknown";
        case InputScopeClassification::Pending:       return "pending";
        case InputScopeClassification::Allowed:       return "allowed";
        case InputScopeClassification::Private:       return "private";
        case InputScopeClassification::PasswordOrPin: return "password-or-pin";
        case InputScopeClassification::QueryError:    return "query-error";
        default:                                      return "unknown";
    }
}

[[nodiscard]] constexpr bool IsPasswordOrPinInputScope(InputScope scope) noexcept {
    switch (scope) {
        case IS_PASSWORD:
        case IS_NUMERIC_PASSWORD:
        case IS_NUMERIC_PIN:
        case IS_ALPHANUMERIC_PIN:
        case IS_ALPHANUMERIC_PIN_SET:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr InputScopeClassification ClassifyInputScopeSpan(std::span<const InputScope> scopes) noexcept {
    if (scopes.empty()) {
        return InputScopeClassification::Unknown;
    }
    bool saw_private = false;
    for (const InputScope scope : scopes) {
        if (IsPasswordOrPinInputScope(scope)) {
            return InputScopeClassification::PasswordOrPin;
        }
        if (scope == IS_PRIVATE) {
            saw_private = true;
        }
    }
    return saw_private ? InputScopeClassification::Private : InputScopeClassification::Allowed;
}

struct CompartmentFlagReadResult {
    AccessFactStage stage = AccessFactStage::None;
    HRESULT hr = S_OK;
    VARTYPE vt = VT_EMPTY;
    bool is_nonzero = false;
    bool has_error = false;
};

#pragma warning(push)
#pragma warning(disable : 26447 26476)
[[nodiscard]] inline CompartmentFlagReadResult ReadContextCompartmentFlag(
    ITfCompartmentMgr* mgr, REFGUID guid,
    AccessFactStage get_stage, AccessFactStage val_stage) noexcept {
    CompartmentFlagReadResult out{};
    if (!mgr) {
        out.stage = get_stage;
        out.hr = E_INVALIDARG;
        out.has_error = true;
        return out;
    }
    Microsoft::WRL::ComPtr<ITfCompartment> comp;
    out.hr = mgr->GetCompartment(guid, &comp);
    if (FAILED(out.hr) || !comp) {
        out.stage = get_stage;
        out.hr = FAILED(out.hr) ? out.hr : E_UNEXPECTED;
        out.has_error = true;
        return out;
    }
    out.stage = val_stage;
    VARIANT var{};
    ::VariantInit(&var);
    const auto clear_var = gsl::finally([&var]() noexcept {
        std::ignore = ::VariantClear(&var);
    });
    out.hr = comp->GetValue(&var);
    out.vt = var.vt;
    if (FAILED(out.hr)) {
        out.has_error = true;
        return out;
    }
    if (out.hr == S_FALSE || var.vt == VT_EMPTY) {
        out.is_nonzero = false;
        return out;
    }
    if (var.vt != VT_I4) {
        out.hr = DISP_E_TYPEMISMATCH;
        out.has_error = true;
        return out;
    }
    out.is_nonzero = (var.lVal != 0);
    return out;
}
#pragma warning(pop)

struct NativeEditWindowStyleFacts {
    bool is_native_edit = false;
    bool is_password = false;
    bool is_readonly = false;
};

[[nodiscard]] inline NativeEditWindowStyleFacts InspectConfirmedCurrentThreadEditWindow(HWND hwnd) noexcept {
    NativeEditWindowStyleFacts result{};
    if (!hwnd || !::IsWindow(hwnd)) {
        return result;
    }
    DWORD pid = 0;
    const DWORD tid = ::GetWindowThreadProcessId(hwnd, &pid);
    if (tid != ::GetCurrentThreadId() || pid != ::GetCurrentProcessId()) {
        return result;
    }
    std::array<wchar_t, 32> class_name{};
    const int len = ::GetClassNameW(hwnd, class_name.data(), gsl::narrow_cast<int>(class_name.size()));
    if (len <= 0 || ::_wcsicmp(class_name.data(), L"Edit") != 0) {
        return result;
    }
    ::SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (style == 0 && ::GetLastError() != ERROR_SUCCESS) {
        return result;
    }
    result.is_native_edit = true;
    result.is_password = (style & ES_PASSWORD) != 0;
    result.is_readonly = (style & ES_READONLY) != 0;
    return result;
}

template <typename CheckCurrent>
[[nodiscard]] ContextFacts ReadFastContextFacts(
    ITfContext* context, InputScopeClassification cached_scope, CheckCurrent is_current,
    bool query_view_window = true) noexcept {
    ContextFacts facts{};
    facts.scope_state = cached_scope;
    if (!context) {
        facts.stage = AccessFactStage::ContextMissing;
        return facts;
    }
    facts.has_context = true;

    Microsoft::WRL::ComPtr<ITfCompartmentMgr> comp_mgr;
#pragma warning(suppress : 26447)
    const HRESULT qi_hr = context->QueryInterface(IID_PPV_ARGS(&comp_mgr));
    if (!is_current()) {
        facts.has_context = false;
        facts.stage = AccessFactStage::ContextMissing;
        return facts;
    }
    if (FAILED(qi_hr) || !comp_mgr) {
        facts.has_query_error = true;
        facts.stage = AccessFactStage::CompartmentMgrQuery;
        facts.error_hr = FAILED(qi_hr) ? qi_hr : E_UNEXPECTED;
        return facts;
    }

    const auto disabled_res = ReadContextCompartmentFlag(
        comp_mgr.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED,
        AccessFactStage::DisabledCompartmentGet, AccessFactStage::DisabledCompartmentValue);
    if (!is_current()) {
        facts.has_context = false;
        facts.stage = AccessFactStage::ContextMissing;
        return facts;
    }
    if (disabled_res.has_error) {
        facts.has_query_error = true;
        facts.stage = disabled_res.stage;
        facts.error_hr = disabled_res.hr;
        facts.error_vt = disabled_res.vt;
        return facts;
    }
    facts.is_keyboard_disabled = disabled_res.is_nonzero;

    const auto empty_res = ReadContextCompartmentFlag(
        comp_mgr.Get(), GUID_COMPARTMENT_EMPTYCONTEXT,
        AccessFactStage::EmptyCompartmentGet, AccessFactStage::EmptyCompartmentValue);
    if (!is_current()) {
        facts.has_context = false;
        facts.stage = AccessFactStage::ContextMissing;
        return facts;
    }
    if (empty_res.has_error) {
        facts.has_query_error = true;
        facts.stage = empty_res.stage;
        facts.error_hr = empty_res.hr;
        facts.error_vt = empty_res.vt;
        return facts;
    }
    facts.is_empty_context = empty_res.is_nonzero;

    TF_STATUS status{};
#pragma warning(suppress : 26447)
    const HRESULT status_hr = context->GetStatus(&status);
    if (!is_current()) {
        facts.has_context = false;
        facts.stage = AccessFactStage::ContextMissing;
        return facts;
    }
    if (status_hr == E_NOTIMPL || status_hr == E_NOINTERFACE ||
        status_hr == TF_E_DISCONNECTED || status_hr == E_FAIL) {
        facts.is_readonly = false;
    } else if (FAILED(status_hr)) {
        facts.has_query_error = true;
        facts.stage = AccessFactStage::ContextStatus;
        facts.error_hr = status_hr;
        return facts;
    } else {
        facts.is_readonly = (status.dwDynamicFlags & TF_SD_READONLY) != 0;
    }

    // Optional native EDIT style check via a current-thread focus/caret window confirmed by the context's active view.
    if (query_view_window && !facts.IsBlocked()) {
        HWND candidate_hwnd = ::GetFocus();
        if (!candidate_hwnd) {
            GUITHREADINFO gti{};
            gti.cbSize = sizeof(gti);
            if (::GetGUIThreadInfo(::GetCurrentThreadId(), &gti)) {
                candidate_hwnd = gti.hwndFocus ? gti.hwndFocus : gti.hwndCaret;
            }
        }
        const auto focus_edit = InspectConfirmedCurrentThreadEditWindow(candidate_hwnd);
        if (focus_edit.is_password || focus_edit.is_readonly) {
            Microsoft::WRL::ComPtr<ITfContextView> view;
#pragma warning(suppress : 26447)
            const HRESULT view_hr = context->GetActiveView(&view);
            if (!is_current()) {
                facts.has_context = false;
                facts.stage = AccessFactStage::ContextMissing;
                return facts;
            }
            if (SUCCEEDED(view_hr) && view) {
                HWND view_hwnd = nullptr;
#pragma warning(suppress : 26447)
                const HRESULT wnd_hr = view->GetWnd(&view_hwnd);
                if (!is_current()) {
                    facts.has_context = false;
                    facts.stage = AccessFactStage::ContextMissing;
                    return facts;
                }
                if (SUCCEEDED(wnd_hr) && view_hwnd &&
                    (view_hwnd == candidate_hwnd || ::IsChild(view_hwnd, candidate_hwnd))) {
                    facts.is_native_password_edit = focus_edit.is_password;
                    facts.is_native_readonly_edit = focus_edit.is_readonly;
                }
            }
        }
    }

    return facts;
}

struct InputScopeQueryResult {
    InputScopeClassification classification = InputScopeClassification::Unknown;
    AccessFactStage stage = AccessFactStage::None;
    HRESULT hr = S_OK;
    VARTYPE vt = VT_EMPTY;
};

#pragma warning(push)
#pragma warning(disable : 26447 26476)
template <typename CheckCurrent>
[[nodiscard]] InputScopeQueryResult QueryContextInputScopeInSession(
    TfEditCookie ec, gsl::not_null<ITfContext*> context, CheckCurrent is_current) noexcept {
    InputScopeQueryResult out{};
    Microsoft::WRL::ComPtr<ITfReadOnlyProperty> prop;
    const HRESULT prop_hr = context->GetAppProperty(kGuidPropInputScope, &prop);
    if (!is_current()) {
        return out;
    }
    if (prop_hr == S_FALSE || prop_hr == E_NOTIMPL || prop_hr == E_NOINTERFACE) {
        out.classification = InputScopeClassification::Unknown;
        out.stage = AccessFactStage::AppPropertyGet;
        out.hr = prop_hr;
        return out;
    }
    if (FAILED(prop_hr) || !prop) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::AppPropertyGet;
        out.hr = FAILED(prop_hr) ? prop_hr : E_UNEXPECTED;
        return out;
    }

    Microsoft::WRL::ComPtr<ITfRange> range;
    const HRESULT start_hr = context->GetStart(ec, &range);
    if (!is_current()) {
        return out;
    }
    if (start_hr == E_NOTIMPL || (SUCCEEDED(start_hr) && !range)) {
        out.classification = InputScopeClassification::Unknown;
        out.stage = AccessFactStage::RangeQuery;
        out.hr = start_hr;
        return out;
    }
    if (FAILED(start_hr) || !range) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::RangeQuery;
        out.hr = FAILED(start_hr) ? start_hr : E_UNEXPECTED;
        return out;
    }
    if (!is_current()) {
        return out;
    }

    VARIANT var{};
    ::VariantInit(&var);
    const auto clear_var = gsl::finally([&var]() noexcept {
        std::ignore = ::VariantClear(&var);
    });
    const HRESULT val_hr = prop->GetValue(ec, range.Get(), &var);
    range.Reset();
    prop.Reset();
    out.vt = var.vt;
    if (!is_current()) {
        return out;
    }
    if (val_hr == S_FALSE || val_hr == E_NOTIMPL || val_hr == E_NOINTERFACE ||
        val_hr == E_FAIL || val_hr == TF_E_NOLOCK || val_hr == TS_E_NOLOCK || var.vt == VT_EMPTY) {
        if (SUCCEEDED(val_hr) || val_hr == E_NOTIMPL || val_hr == E_NOINTERFACE ||
            val_hr == E_FAIL || val_hr == TF_E_NOLOCK || val_hr == TS_E_NOLOCK) {
            out.classification = InputScopeClassification::Unknown;
            out.stage = AccessFactStage::PropertyValueGet;
            out.hr = val_hr;
            return out;
        }
    }
    if (FAILED(val_hr)) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::PropertyValueGet;
        out.hr = val_hr;
        return out;
    }
    if (var.vt != VT_UNKNOWN || !var.punkVal) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::PropertyValueGet;
        out.hr = DISP_E_TYPEMISMATCH;
        return out;
    }

    Microsoft::WRL::ComPtr<ITfInputScope> input_scope;
    const HRESULT scope_qi_hr = var.punkVal->QueryInterface(IID_PPV_ARGS(&input_scope));
    if (!is_current()) {
        return out;
    }
    if (FAILED(scope_qi_hr) || !input_scope) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::InputScopeQuery;
        out.hr = FAILED(scope_qi_hr) ? scope_qi_hr : E_UNEXPECTED;
        return out;
    }

    InputScope* raw_scopes = nullptr;
    UINT count = 0;
    const auto free_scopes = gsl::finally([&raw_scopes]() noexcept {
        if (raw_scopes) {
            ::CoTaskMemFree(raw_scopes);
        }
    });
    const HRESULT get_scopes_hr = input_scope->GetInputScopes(&raw_scopes, &count);
    if (!is_current()) {
        return out;
    }
    if (FAILED(get_scopes_hr)) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::GetInputScopesCall;
        out.hr = get_scopes_hr;
        return out;
    }
    if (count > 0 && !raw_scopes) {
        out.classification = InputScopeClassification::QueryError;
        out.stage = AccessFactStage::GetInputScopesCall;
        out.hr = E_UNEXPECTED;
        return out;
    }
    if (count == 0 || !raw_scopes) {
        out.classification = InputScopeClassification::Unknown;
        out.stage = AccessFactStage::GetInputScopesCall;
        out.hr = get_scopes_hr;
        return out;
    }

    out.classification = ClassifyInputScopeSpan(std::span<const InputScope>(raw_scopes, count));
    out.stage = AccessFactStage::GetInputScopesCall;
    out.hr = get_scopes_hr;
    return out;
}
#pragma warning(pop)

// Per-service access state cache bound to EditSessionTicket + state revision.
class ContextAccessTracker {
public:
    ContextAccessTracker() noexcept = default;

    void Reset() noexcept {
        ++revision_;
        ticket_ = {};
        is_native_password_edit = false;
        is_native_readonly_edit = false;
        window_queried = false;
        scope_classification_ = InputScopeClassification::Unknown;
        scope_queried_ = false;
        has_logged_diagnostic_ = false;
    }

    void OnDynamicStateChanged() noexcept {
        ++revision_;
        is_native_password_edit = false;
        is_native_readonly_edit = false;
        window_queried = false;
        scope_classification_ = InputScopeClassification::Unknown;
        scope_queried_ = false;
    }

    [[nodiscard]] uint64_t Revision() const noexcept { return revision_; }

    [[nodiscard]] bool NeedsWindowQuery(EditSessionTicket current_ticket) const noexcept {
        return !window_queried || !ticket_.Matches(current_ticket.epoch, current_ticket.target);
    }

    void UpdateWindowFacts(EditSessionTicket current_ticket, uint64_t expected_revision,
                           bool is_pwd, bool is_ro) noexcept {
        if (revision_ != expected_revision) {
            return;
        }
        if (!ticket_.Matches(current_ticket.epoch, current_ticket.target)) {
            ticket_ = current_ticket;
            scope_classification_ = InputScopeClassification::Unknown;
            scope_queried_ = false;
        }
        is_native_password_edit = is_pwd;
        is_native_readonly_edit = is_ro;
        window_queried = true;
    }

    void ApplyCachedWindowFacts(EditSessionTicket current_ticket, ContextFacts& facts) const noexcept {
        if (window_queried && ticket_.Matches(current_ticket.epoch, current_ticket.target)) {
            facts.is_native_password_edit = is_native_password_edit;
            facts.is_native_readonly_edit = is_native_readonly_edit;
        }
    }

    [[nodiscard]] InputScopeClassification CachedScopeFor(EditSessionTicket current_ticket) const noexcept {
        if (scope_queried_ && ticket_.Matches(current_ticket.epoch, current_ticket.target)) {
            return scope_classification_;
        }
        return InputScopeClassification::Unknown;
    }

    [[nodiscard]] bool NeedsScopeQuery(EditSessionTicket current_ticket) const noexcept {
        return !scope_queried_ || !ticket_.Matches(current_ticket.epoch, current_ticket.target);
    }

    void UpdateScopeResult(EditSessionTicket current_ticket, uint64_t expected_revision,
                           const InputScopeQueryResult& res) noexcept {
        if (revision_ != expected_revision) {
            return;
        }
        ticket_ = current_ticket;
        scope_classification_ = res.classification;
        scope_queried_ = (res.classification != InputScopeClassification::QueryError);
    }

    void LogIfChanged(uint64_t service_id, const GUID& activity, const ContextFacts& facts,
                      const AccessPolicy& policy) noexcept {
        if (has_logged_diagnostic_ &&
            last_policy_ == policy &&
            last_stage_ == facts.stage &&
            last_hr_ == facts.error_hr &&
            last_scope_ == facts.scope_state &&
            last_disabled_ == facts.is_keyboard_disabled &&
            last_empty_ == facts.is_empty_context &&
            last_readonly_ == (facts.is_readonly || facts.is_native_readonly_edit) &&
            last_password_ == facts.is_native_password_edit) {
            return;
        }
        const bool was_restricted_or_error =
            has_logged_diagnostic_ && (!last_policy_.can_input || !last_policy_.can_learn);
        has_logged_diagnostic_ = true;
        last_policy_ = policy;
        last_stage_ = facts.stage;
        last_hr_ = facts.error_hr;
        last_scope_ = facts.scope_state;
        last_disabled_ = facts.is_keyboard_disabled;
        last_empty_ = facts.is_empty_context;
        last_readonly_ = (facts.is_readonly || facts.is_native_readonly_edit);
        last_password_ = facts.is_native_password_edit;

        if (facts.HasError()) {
            QIQI_LOG_ACT_INFO(
                log::Level::warn,
                log::Keyword::Input,
                &activity,
                "ContextAccess(service={}, rev={}, can_input={}, can_learn={}, reset={}, stage={}, hr=0x{:08x}, vt={}, scope={})",
                service_id, revision_, policy.can_input, policy.can_learn, policy.should_reset_active,
                AccessFactStageToString(facts.stage), gsl::narrow_cast<unsigned int>(facts.error_hr),
                gsl::narrow_cast<unsigned int>(facts.error_vt),
                InputScopeClassificationToString(facts.scope_state));
        } else if (!policy.can_input || !policy.can_learn || was_restricted_or_error) {
            QIQI_LOG_ACT_INFO(
                log::Level::debug,
                log::Keyword::Input,
                &activity,
                "ContextAccess(service={}, rev={}, can_input={}, can_learn={}, reset={}, disabled={}, empty={}, readonly={}, native_pwd={}, scope={})",
                service_id, revision_, policy.can_input, policy.can_learn, policy.should_reset_active,
                facts.is_keyboard_disabled, facts.is_empty_context,
                facts.is_readonly || facts.is_native_readonly_edit,
                facts.is_native_password_edit,
                InputScopeClassificationToString(facts.scope_state));
        }
    }

private:
    uint64_t revision_ = 0;
    EditSessionTicket ticket_{};
    bool is_native_password_edit = false;
    bool is_native_readonly_edit = false;
    bool window_queried = false;
    InputScopeClassification scope_classification_ = InputScopeClassification::Unknown;
    bool scope_queried_ = false;

    bool has_logged_diagnostic_ = false;
    AccessPolicy last_policy_{};
    AccessFactStage last_stage_ = AccessFactStage::None;
    HRESULT last_hr_ = S_OK;
    InputScopeClassification last_scope_ = InputScopeClassification::Unknown;
    bool last_disabled_ = false;
    bool last_empty_ = false;
    bool last_readonly_ = false;
    bool last_password_ = false;
};

} // namespace qiqi
