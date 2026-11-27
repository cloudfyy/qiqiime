// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "tools/settings/transfer.h"
#include "common/dlgutil.h"
#include "common/configclient.h"
#include "common/dictui.h"
#include "common/fileutil.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/engine/dict.h"

#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <gsl/gsl>
#include <memory>
#include <new>
#include <utility>

namespace qiqi {

using namespace settings_detail;

namespace {

void InitSettingsCompartmentSubscription(HWND hwnd, DialogContext& ctx) noexcept {
    Microsoft::WRL::ComPtr<ITfThreadMgr> thread_mgr;
    if (const HMODULE msctf = ::GetModuleHandleW(L"msctf.dll")) {
        using TfGetThreadMgrFn = HRESULT(WINAPI*)(ITfThreadMgr**);
        const auto get_fn = reinterpret_cast<TfGetThreadMgrFn>(
            ::GetProcAddress(msctf, "TF_GetThreadMgr"));
        if (get_fn) {
            std::ignore = log::CheckHResult(get_fn(&thread_mgr), "Settings::TF_GetThreadMgr", log::Level::debug);
        }
        if (!thread_mgr) {
            using TfCreateThreadMgrFn = HRESULT(WINAPI*)(ITfThreadMgr**);
            const auto create_fn = reinterpret_cast<TfCreateThreadMgrFn>(
                ::GetProcAddress(msctf, "TF_CreateThreadMgr"));
            if (create_fn) {
                std::ignore = log::CheckHResult(create_fn(&thread_mgr), "Settings::TF_CreateThreadMgr", log::Level::debug);
            }
        }
    }
    if (!thread_mgr) {
        std::ignore = log::CheckHResult(::CoCreateInstance(
            CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&thread_mgr)),
            "Settings::CoCreateInstance(ITfThreadMgr)");
    }
    if (!thread_mgr) {
        return;
    }
    Microsoft::WRL::ComPtr<ITfCompartmentMgr> global_mgr;
#pragma warning(suppress : 26447) // TSF COM methods report failure through HRESULT.
    if (!log::CheckHResult(thread_mgr->GetGlobalCompartment(&global_mgr),
                          "Settings::GetGlobalCompartment") || !global_mgr) {
        return;
    }
    Microsoft::WRL::ComPtr<SettingsCompartmentListener> listener;
    try {
        listener = MakeComObject<SettingsCompartmentListener>(hwnd);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_WARN("Settings compartment listener allocation failed");
        return;
    }
    if (!listener) {
        return;
    }
    if (ctx.global_config_comp.Bind(global_mgr.Get(), kGuidCompartmentConfigVersion, listener.Get())) {
        ctx.tsf_thread_mgr = std::move(thread_mgr);
        ctx.compartment_listener = std::move(listener);
    }
}

} // namespace

ScopedWindow CreateSettingsWindow(
    HINSTANCE instance, HWND parent, QiQiConfig* bound_config, bool save_on_accept) noexcept {
    if (save_on_accept && !bound_config) {
        QIQI_LOG_ERROR("Settings persistence requires a bound configuration");
        return {};
    }
    if (!dialog_detail::RegisterDialogWindowClass(
            instance, kSettingsWndClass, &SettingsWndProc, 0,
            ::LoadIconW(instance, MAKEINTRESOURCEW(1)),
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1))) {
        QIQI_LOG_ERROR("Settings dialog class registration failed (error={})", ::GetLastError());
        return {};
    }

    const bool registry_override = HasConfigRegistrySubKeyOverride();
    config::Snapshot config_snapshot;
    const HRESULT snapshot_result = registry_override ? S_FALSE : config::CopyCachedSnapshot(config_snapshot);
    const bool use_broker_snapshot = save_on_accept && snapshot_result == S_OK;
    const QiQiConfig defaults;
    const QiQiConfig& initial = use_broker_snapshot ? config_snapshot.value : (bound_config ? *bound_config : defaults);
    const uint64_t initial_revision = registry_override ? QueryRegistryConfigSequence() : config_snapshot.revision;

    std::unique_ptr<DialogContext> ctx;
    try {
        ctx = std::make_unique<DialogContext>();
        ctx->instance = instance;
        ctx->config = bound_config;
        ctx->baseline_config = initial;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings dialog context allocation failed");
        return {};
    }
    ctx->save_on_accept = save_on_accept;
    if (save_on_accept) {
        if (registry_override) {
            ctx->skin_catalog_result =
                EnsureBuiltinImeSkinCatalog();
            if (SUCCEEDED(
                    ctx->skin_catalog_result)) {
                ctx->skin_catalog_result =
                    LoadImeSkinCatalog(
                        ctx->skin_catalog);
            }
        } else if (!use_broker_snapshot) {
            ctx->skin_catalog_result = snapshot_result == S_FALSE ? E_PENDING : snapshot_result;
        } else {
            ctx->skin_catalog = std::move(config_snapshot.skin_catalog);
        }
    } else {
        ctx->skin_catalog_result =
            MakeBuiltinImeSkinCatalog(
                ctx->skin_catalog);
    }

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    const UiLanguage initial_language = initial.ui_language;
    const auto preferences = GetDisplayPreferences(initial);
    ctx->d2d.clip_controls_to_dc = true;
    if (!RefreshSettingsUiAppearance(nullptr, *ctx, initial_language, preferences, mon.dpi)) return {};
    ctx->icon_small = dialog_detail::LoadOrCreateBrandIcon(S(kSmallBrandIconSize), instance);
    ctx->icon_big = dialog_detail::LoadOrCreateBrandIcon(S(kLargeBrandIconSize), instance);

    const auto& str = GetUiStrings(initial_language);

    constexpr DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
        WS_THICKFRAME | WS_CLIPCHILDREN;
    constexpr DWORD wnd_ex_style = WS_EX_APPWINDOW;
    const RECT bounds = ComputeCenteredWindowBounds(
        S(settings_layout::kClientSize.cx), S(settings_layout::kClientSize.cy),
        wnd_style, wnd_ex_style, mon.work_area);
    const int dlg_w = std::min(bounds.right - bounds.left, mon.work_area.right - mon.work_area.left);
    const int dlg_h = std::min(bounds.bottom - bounds.top, mon.work_area.bottom - mon.work_area.top);

    ScopedWindow hwnd(::CreateWindowExW(
        wnd_ex_style,
        kSettingsWndClass,
        str.dlg_title,
        wnd_style,
        bounds.left, bounds.top, dlg_w, dlg_h,
        parent, nullptr, instance, ctx.get()));
    if (!hwnd) {
        QIQI_LOG_ERROR("CreateSettingsWindow CreateWindowExW failed (error={})", ::GetLastError());
        return {};
    }
    QIQI_LOG_DEBUG("SettingsWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   bounds.left, bounds.top, dlg_w, dlg_h, mon.dpi);
    const gsl::not_null<DialogContext*> owned_ctx(ctx.release());
    dialog_detail::ApplyDialogIcons(hwnd.Get(), owned_ctx->icon_small.Get(), owned_ctx->icon_big.Get());

    CreateSettingsControls(hwnd.Get(), instance, *owned_ctx);
    owned_ctx->controls_initialized = true;
    if (!log::CheckHResult(WriteSettingsToWindowControlsResult(hwnd.Get(), initial), "Settings::ApplyInitialConfig"))
        return {};
    if (bound_config && use_broker_snapshot) *bound_config = std::move(config_snapshot.value);
    owned_ctx->baseline_revision = initial_revision;
    InitSettingsCompartmentSubscription(hwnd.Get(), *owned_ctx);
    LayoutSettingsControls(hwnd.Get(), *owned_ctx);
    return hwnd;
}

bool ShowSettingsDialog(
    HINSTANCE instance, HWND parent, QiQiConfig& in_out_config, bool save_on_accept) noexcept {
    if (ActivateExistingSettingsWindow()) {
        QIQI_LOG_INFO("Settings dialog is already open; activated existing window");
        return false;
    }
    const ScopedKernelHandle single_instance_mutex(
        ::CreateMutexW(nullptr, TRUE, kSettingsSingleInstanceMutexName));
    const DWORD mutex_error = ::GetLastError();
    if (!single_instance_mutex) {
        QIQI_LOG_ERROR("Settings::CreateMutexW failed (error={})", mutex_error);
        return false;
    }
    if (mutex_error == ERROR_ALREADY_EXISTS) {
        std::ignore = ActivateExistingSettingsWindow();
        QIQI_LOG_INFO("Settings single-instance mutex already held; skipping duplicate dialog");
        return false;
    }
    const ScopedWindow hwnd(CreateSettingsWindow(instance, parent, &in_out_config, save_on_accept));
    const auto* ctx = reinterpret_cast<const DialogContext*>(::GetWindowLongPtrW(hwnd.Get(), GWLP_USERDATA));
    if (!hwnd || !ctx) {
        return false;
    }
    return dialog_detail::RunModalDialogLoop(hwnd.Get(), ctx->done) && ctx->accepted;
}

void ReportSettingsNotificationFailure(HWND parent, UiLanguage ui_language) noexcept {
    QIQI_LOG_WARN("Settings saved, but the running IME could not be notified");
    ShowSettingsMessage(parent, ui_language, SettingsMessageKind::NotificationFailure);
}

} // namespace qiqi
