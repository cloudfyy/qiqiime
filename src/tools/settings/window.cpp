#include "tools/settings/internal.h"
#include "common/dialog_util.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/engine/binary_dict.h"
#include "tools/dictionary_validation.h"

#include <commctrl.h>
#include <shellapi.h>
#include <gsl/gsl>
#include <memory>
#include <new>
#include <utility>

namespace qiqi {

using namespace settings_detail;

DWORD settings_detail::OpenDictionaryManager(
    HWND owner, const std::filesystem::path& executable, UiLanguage language,
    gsl::not_null<decltype(&::ShellExecuteExW)> execute) {
#pragma warning(suppress : 26476) // Win32 requires this union-backed descriptor.
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = L"open";
    info.lpFile = executable.c_str();
    info.nShow = SW_SHOWNORMAL;
    const BOOL launched = execute(&info);
    const DWORD error = launched ? ERROR_SUCCESS : ::GetLastError();
    const ScopedKernelHandle process(info.hProcess);
    if (!launched) {
        QIQI_LOG_CAT_WARN(log::Keyword::UI,
            "Settings failed to launch QiQiIMEDictManager.exe (error={})", error);
        ShowSettingsMessage(owner, language, SettingsMessageKind::DictManagerLaunchFailure);
    }
    return error;
}

namespace {

void ShowSettingsSaveFailure(HWND hwnd, UiLanguage ui_language) noexcept {
    ShowSettingsMessage(hwnd, ui_language, SettingsMessageKind::SaveFailure);
}

void CheckPrimaryDictRadio(HWND hwnd, PrimaryDict dict) noexcept {
    int target_id = kIdRadioJidian;
    if (dict == PrimaryDict::Haifeng) {
        target_id = kIdRadioHaifeng;
    } else if (dict == PrimaryDict::BuiltinOnly) {
        target_id = kIdRadioBuiltin;
    } else if (dict == PrimaryDict::Custom) {
        target_id = kIdRadioCustom;
    }
    for (const int id : {kIdRadioJidian, kIdRadioHaifeng, kIdRadioBuiltin, kIdRadioCustom}) {
        ::CheckDlgButton(hwnd, id, id == target_id ? BST_CHECKED : BST_UNCHECKED);
    }
}

std::filesystem::path GetSettingsModuleDirectory(const DialogContext& ctx) {
    const std::filesystem::path mod_path =
        GetModuleFilePath(ctx.instance ? ctx.instance : g_dll_instance);
    return mod_path.empty() ? std::filesystem::path{} : mod_path.parent_path();
}

bool ValidateSettingsPrimaryDictionary(HWND hwnd, const DialogContext& ctx, const QiQiConfig& edited) {
    if (edited.primary_dict != PrimaryDict::Custom) {
        return true;
    }
    const auto resolved = ResolveConfiguredDictionaryPath(
        GetSettingsModuleDirectory(ctx), edited.primary_dict_path);
    const auto validation = WubiMappedDict::ValidateAndLoadPrimaryDictionary(resolved);
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, ctx);
    if (!validation) {
        ShowSettingsMessage(hwnd, language, SettingsMessageKind::InvalidPrimaryDict);
        return false;
    }
    return dictionary_ui::ConfirmPartialDictionary(hwnd, language, validation);
}

} // namespace

bool SelectCustomPrimaryDictionaryInSettings(
    HWND hwnd, const std::filesystem::path& file_path) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) {
        return false;
    }
    auto* ctx = reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ctx) {
        return false;
    }
    const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    try {
        const std::filesystem::path resolved =
            ResolveConfiguredDictionaryPath(GetSettingsModuleDirectory(*ctx), file_path.wstring());
        const PrimaryDictValidationResult validation =
            WubiMappedDict::ValidateAndLoadPrimaryDictionary(resolved);
        if (!validation) {
            QIQI_LOG_CAT_WARN(
                log::Keyword::Dictionary,
                "Settings rejected invalid custom primary dictionary '{}' (status={}, error={})",
                WideToUtf8(file_path.wstring()),
                static_cast<unsigned int>(validation.status),
                validation.win32_error);
            CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            ShowSettingsMessage(hwnd, lang, SettingsMessageKind::InvalidPrimaryDict);
            return false;
        }
        if (!dictionary_ui::ConfirmPartialDictionary(hwnd, lang, validation)) {
            CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return false;
        }
        ctx->custom_primary_dict_path = file_path.wstring();
        ctx->last_valid_primary_dict = PrimaryDict::Custom;
        CheckPrimaryDictRadio(hwnd, PrimaryDict::Custom);
        const auto& str = GetUiStrings(lang);
        ::SetDlgItemTextW(
            hwnd,
            kIdLblCustomPrimaryPath,
            ResolveDictionaryPathDisplayText(ctx->custom_primary_dict_path, str.dlg_lbl_custom_dict_none));
        UpdateExtraDictCheckboxState(hwnd);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    } catch (...) {
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Dictionary,
            "SelectCustomPrimaryDictionaryInSettings failed with an exception");
        CheckPrimaryDictRadio(hwnd, ctx->last_valid_primary_dict);
        UpdateExtraDictCheckboxState(hwnd);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        ShowSettingsMessage(hwnd, lang, SettingsMessageKind::InvalidPrimaryDict);
        return false;
    }
}

namespace {

bool BrowseCustomPrimaryDictionary(HWND hwnd, const DialogContext& ctx) noexcept {
    try {
        const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
        std::filesystem::path selected_path;
        if (!dialog_detail::PromptForPrimaryDictionaryFile(
                hwnd, lang, ctx.custom_primary_dict_path, selected_path)) {
            return false;
        }
        return SelectCustomPrimaryDictionaryInSettings(hwnd, selected_path);
    } catch (...) {
        QIQI_LOG_CAT_ERROR(log::Keyword::UI, "BrowseCustomPrimaryDictionary failed with an exception");
        return false;
    }
}

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

void HandleSettingsCommand(HWND hwnd, WPARAM wparam, DialogContext& ctx) {
    switch (LOWORD(wparam)) {
        case kIdRadioJidian:
            ctx.last_valid_primary_dict = PrimaryDict::Jidian;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioHaifeng:
            ctx.last_valid_primary_dict = PrimaryDict::Haifeng;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioBuiltin:
            ctx.last_valid_primary_dict = PrimaryDict::BuiltinOnly;
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdRadioCustom:
            if (ctx.custom_primary_dict_path.empty()) {
                if (!BrowseCustomPrimaryDictionary(hwnd, ctx)) {
                    CheckPrimaryDictRadio(hwnd, ctx.last_valid_primary_dict);
                }
            } else {
                ctx.last_valid_primary_dict = PrimaryDict::Custom;
            }
            UpdateExtraDictCheckboxState(hwnd);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnBrowsePrimaryDict:
            std::ignore = BrowseCustomPrimaryDictionary(hwnd, ctx);
            return;
        case kIdComboUiLang:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                ApplyLiveSettingsLanguage(hwnd, ctx);
            }
            return;
        case kIdComboUnderlineStyle:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                UpdateExtraDictCheckboxState(hwnd);
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            return;
        case kIdChkBoldUnderline:
            if (const HWND combo = ::GetDlgItem(hwnd, kIdComboUnderlineStyle)) {
                ::InvalidateRect(combo, nullptr, FALSE);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
        case kIdBtnRestoreDefaults: {
            // Restored values are unsaved edits, not a new external-configuration baseline.
            QiQiConfig baseline = std::move(ctx.baseline_config);
            WriteSettingsToWindowControls(hwnd, QiQiConfig{});
            ctx.baseline_config = std::move(baseline);
            ApplyLiveSettingsLanguage(hwnd, ctx, false);
            return;
        }
        case kIdBtnOk: {
            if (ctx.config) {
                if (ctx.save_on_accept) {
                    QiQiConfig edited;
                    try {
                        edited = *ctx.config;
                    } catch (const std::bad_alloc&) {
                        QIQI_LOG_ERROR("Settings save could not copy configuration");
                        ShowSettingsSaveFailure(hwnd, GetSelectedUiLanguageFromCombo(hwnd, ctx));
                        return;
                    }
                    ReadSettingsFromWindowControls(hwnd, edited);
                    if (!ValidateSettingsPrimaryDictionary(hwnd, ctx, edited)) {
                        return;
                    }
                    if (!SaveConfig(edited)) {
                        QIQI_LOG_ERROR("Settings save failed; retaining edits for retry");
                        ShowSettingsSaveFailure(hwnd, edited.ui_language);
                        return;
                    }
                    ctx.config_registry_sequence = QueryRegistryConfigSequence();
                    ctx.baseline_config = edited;
                    *ctx.config = std::move(edited);
                } else {
                    QiQiConfig edited = *ctx.config;
                    ReadSettingsFromWindowControls(hwnd, edited);
                    if (!ValidateSettingsPrimaryDictionary(hwnd, ctx, edited)) {
                        return;
                    }
                    ctx.baseline_config = edited;
                    *ctx.config = std::move(edited);
                }
            }
            ctx.accepted = true;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        }
        case kIdBtnCancel:
            ctx.accepted = false;
            ctx.done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return;
        case kIdBtnAbout: {
            const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
            ShowAboutDialog(g_dll_instance, hwnd, lang);
            return;
        }
        case kIdBtnHelp: {
            const UiLanguage lang = GetSelectedUiLanguageFromCombo(hwnd, ctx);
            ShowHelpDialog(g_dll_instance, hwnd, lang);
            return;
        }
        case kIdBtnDictManager: {
            const std::filesystem::path mod_path = GetModuleFilePath(ctx.instance ? ctx.instance : g_dll_instance);
            const std::filesystem::path exe_path =
                mod_path.empty() ? std::filesystem::path(L"QiQiIMEDictManager.exe")
                                 : (mod_path.parent_path() / L"QiQiIMEDictManager.exe");
            std::ignore = OpenDictionaryManager(
                hwnd, exe_path, GetSelectedUiLanguageFromCombo(hwnd, ctx));
            return;
        }
        default:
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return;
    }
}

void DestroySettingsControls(HWND hwnd) noexcept {
    ::EnumChildWindows(
        hwnd,
        [](HWND child, LPARAM) noexcept -> BOOL {
            ::DestroyWindow(child);
            return TRUE;
        },
        0);
}

void UpdateSettingsWindowForMonitor(HWND hwnd, DialogContext& ctx, UINT dpi, const RECT* suggested_rect) noexcept {
    const UINT effective_dpi = dpi > 0 ? dpi : 96u;
    RECT current_rect{};
    if (!::GetWindowRect(hwnd, &current_rect)) {
        QIQI_LOG_ERROR("Settings DPI update could not query window bounds (error={})", ::GetLastError());
        return;
    }

    RECT target_rect = current_rect;
    if (suggested_rect && suggested_rect->right > suggested_rect->left &&
        suggested_rect->bottom > suggested_rect->top) {
        target_rect = *suggested_rect;
    } else if (ctx.d2d.dpi != effective_dpi) {
        RECT desired_rect{0, 0, ScalePxByDpi(settings_layout::kClientSize.cx, effective_dpi),
                         ScalePxByDpi(settings_layout::kClientSize.cy, effective_dpi)};
        const DWORD style = gsl::narrow_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
        const DWORD ex_style = gsl::narrow_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (!::AdjustWindowRectEx(&desired_rect, style, FALSE, ex_style)) {
            QIQI_LOG_ERROR("Settings DPI update could not calculate window frame (error={})", ::GetLastError());
            return;
        }
        target_rect.right = target_rect.left + desired_rect.right - desired_rect.left;
        target_rect.bottom = target_rect.top + desired_rect.bottom - desired_rect.top;
    }

    const POINT center{
        target_rect.left + (target_rect.right - target_rect.left) / 2,
        target_rect.top + (target_rect.bottom - target_rect.top) / 2
    };
    const MonitorMetrics monitor = QueryMonitorForPoint(center, hwnd);
    const int width = target_rect.right - target_rect.left;
    const int height = target_rect.bottom - target_rect.top;
    const POINT position = ClampPointToWorkArea(
        POINT{target_rect.left, target_rect.top}, width, height, monitor.work_area);
    target_rect.left = position.x;
    target_rect.top = position.y;
    target_rect.right = position.x + width;
    target_rect.bottom = position.y + height;

    const bool dpi_changed = ctx.d2d.dpi != effective_dpi;
    QiQiConfig current_config = ctx.config ? *ctx.config : ctx.baseline_config;
    const QiQiConfig baseline_config = ctx.baseline_config;
    int focused_control_id = 0;
    if (dpi_changed) {
        if (ctx.controls_initialized) {
            const HWND focused = ::GetFocus();
            if (focused && ::IsChild(hwnd, focused)) {
                focused_control_id = ::GetDlgCtrlID(focused);
            }
            ReadSettingsFromWindowControls(hwnd, current_config);
            DestroySettingsControls(hwnd);
        }

        ctx.d2d.Init(effective_dpi);
        ctx.font = CreateUiFont(ctx.d2d.ScalePx(settings_layout::kFontSize), FW_NORMAL, CLEARTYPE_QUALITY);
        ctx.bold_font = CreateUiFont(ctx.d2d.ScalePx(settings_layout::kBoldFontSize), FW_SEMIBOLD, CLEARTYPE_QUALITY);
        ctx.icon_small = dialog_detail::LoadOrCreateBrandIcon(ctx.d2d.ScalePx(kSmallBrandIconSize), ctx.instance);
        ctx.icon_big = dialog_detail::LoadOrCreateBrandIcon(ctx.d2d.ScalePx(kLargeBrandIconSize), ctx.instance);
        if (!ctx.font || !ctx.bold_font || !ctx.icon_small || !ctx.icon_big) {
            QIQI_LOG_ERROR("Settings DPI update could not recreate scaled window resources");
        }
        dialog_detail::ApplyDialogIcons(hwnd, ctx.icon_small.Get(), ctx.icon_big.Get());
    }

    if (!::SetWindowPos(
            hwnd,
            nullptr,
            target_rect.left,
            target_rect.top,
            width,
            height,
            SWP_NOZORDER | SWP_NOACTIVATE)) {
        QIQI_LOG_ERROR("Settings DPI update could not move window (error={})", ::GetLastError());
    }

    if (dpi_changed && ctx.controls_initialized) {
        CreateSettingsControls(hwnd, ctx.instance, ctx);
        WriteSettingsToWindowControls(hwnd, current_config);
        ctx.baseline_config = baseline_config;
        ApplyLiveSettingsLanguage(hwnd, ctx, false);
        if (focused_control_id != 0) {
            if (const HWND focused_control = ::GetDlgItem(hwnd, focused_control_id)) {
                ::SetFocus(focused_control);
            }
        }
    }

    ::InvalidateRect(hwnd, nullptr, TRUE);
    ::UpdateWindow(hwnd);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* ctx = BindOrGetWindowUserData<DialogContext>(hwnd, msg, lparam);
    if (!ctx || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
        case WM_DPICHANGED:
            UpdateSettingsWindowForMonitor(
                hwnd, *ctx, HIWORD(wparam), reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE: {
            RECT rect{};
            if (::GetWindowRect(hwnd, &rect)) {
                const POINT center{
                    rect.left + (rect.right - rect.left) / 2,
                    rect.top + (rect.bottom - rect.top) / 2
                };
                const MonitorMetrics monitor = QueryMonitorForPoint(center, hwnd);
                UpdateSettingsWindowForMonitor(hwnd, *ctx, monitor.dpi, nullptr);
            }
            return 0;
        }
        case WM_PAINT:
            return PaintWindowClient(hwnd, [hwnd, ctx](HDC hdc, const RECT& rc) {
                PaintFluentSettingsWindow(hwnd, hdc, rc, *ctx);
            });
        case WM_PRINTCLIENT: {
            RECT rect{};
            ::GetClientRect(hwnd, &rect);
            PaintFluentSettingsWindow(hwnd, reinterpret_cast<HDC>(wparam), rect, *ctx);
            return 0;
        }
        case WM_MEASUREITEM: {
            if (lparam != 0) {
                auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (mis->CtlType == ODT_COMBOBOX) {
                    mis->itemHeight = gsl::narrow_cast<UINT>(ctx->d2d.ScalePx(kFluentComboItemHeight));
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_DRAWITEM: {
            if (lparam != 0) {
                const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (dis->CtlType == ODT_BUTTON) {
                    if (dis->CtlID == kIdBtnAbout || dis->CtlID == kIdBtnDictManager ||
                        dis->CtlID == kIdBtnHelp || dis->CtlID == kIdBtnRestoreDefaults ||
                        dis->CtlID == kIdBtnBrowsePrimaryDict ||
                        dis->CtlID == kIdBtnOk || dis->CtlID == kIdBtnCancel) {
                        const bool inside_card = (dis->CtlID == kIdBtnBrowsePrimaryDict);
                        const D2D1_COLOR_F outer_bg = inside_card
                            ? (IsSystemDarkTheme() ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255))
                            : ColorRefToD2DColor(IsSystemDarkTheme() ? kDarkWindowBg : kLightCaptionBg);
                        DrawFluentPushButton(*dis, ctx->d2d, dis->CtlID == kIdBtnOk, outer_bg);
                    } else {
                        const bool is_radio =
                            (dis->CtlID >= kIdRadioJidian && dis->CtlID <= kIdRadioBuiltin) ||
                            dis->CtlID == kIdRadioCustom;
                        DrawFluentToggleOrRadioRow(*dis, ctx->d2d, is_radio);
                    }
                    return TRUE;
                }
                if (dis->CtlType == ODT_COMBOBOX) {
                    DrawFluentComboBoxItem(*dis, ctx->d2d);
                    return TRUE;
                }
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            return reinterpret_cast<LRESULT>(ApplyFluentControlColors(hdc, ctx->d2d, true));
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const HDC hdc = reinterpret_cast<HDC>(wparam);
            return reinterpret_cast<LRESULT>(ApplyFluentControlColors(hdc, ctx->d2d, false));
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            const bool is_dark = IsSystemDarkTheme();
            ctx->d2d.RefreshThemeBrushes();
            ApplySystemThemeToMenuAndWindow(hwnd, is_dark);
            ApplyLiveSettingsLanguage(hwnd, *ctx);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wparam) != WA_INACTIVE) {
                SyncSettingsWindowFromExternalConfig(hwnd, *ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case kSettingsExternalConfigChangedMessage:
            SyncSettingsWindowFromExternalConfig(hwnd, *ctx);
            return 0;
        case WM_COMMAND:
            HandleSettingsCommand(hwnd, wparam, *ctx);
            return 0;
        case WM_CLOSE:
            ctx->accepted = false;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            {
                const std::unique_ptr<DialogContext> owned_ctx(ctx);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace

bool ActivateExistingSettingsWindow() noexcept {
    const HWND existing = ::FindWindowW(kSettingsWndClass, nullptr);
    if (!existing || !::IsWindow(existing)) {
        return false;
    }
    if (::IsIconic(existing)) {
        ::ShowWindow(existing, SW_RESTORE);
    }
    const HWND popup = ::GetLastActivePopup(existing);
    const HWND target = (popup && ::IsWindow(popup)) ? popup : existing;
    if (::IsIconic(target)) {
        ::ShowWindow(target, SW_RESTORE);
    }
    DWORD target_pid = 0;
    ::GetWindowThreadProcessId(target, &target_pid);
    if (target_pid != 0 && target_pid != ::GetCurrentProcessId()) {
        ::AllowSetForegroundWindow(target_pid);
    }
    ::SetForegroundWindow(target);
    ::BringWindowToTop(target);
    if (target_pid == ::GetCurrentProcessId()) {
        ::SetActiveWindow(target);
    }
    return true;
}

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

    std::unique_ptr<DialogContext> ctx;
    try {
        ctx = std::make_unique<DialogContext>();
        ctx->instance = instance;
        ctx->config = bound_config;
        if (bound_config) {
            ctx->baseline_config = *bound_config;
            ctx->custom_primary_dict_path = bound_config->primary_dict_path;
            ctx->last_valid_primary_dict = bound_config->primary_dict;
        }
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Settings dialog context allocation failed");
        return {};
    }
    ctx->config_registry_sequence = QueryRegistryConfigSequence();
    ctx->save_on_accept = save_on_accept;

    const MonitorMetrics mon = QueryActiveMonitor(parent);
    const auto S = [dpi = mon.dpi](int px) noexcept { return ScalePxByDpi(px, dpi); };

    ctx->d2d.Init(mon.dpi);
    const bool is_dark = IsSystemDarkTheme();
    ctx->font = CreateUiFont(S(settings_layout::kFontSize), FW_NORMAL, CLEARTYPE_QUALITY);
    ctx->bold_font = CreateUiFont(S(settings_layout::kBoldFontSize), FW_SEMIBOLD, CLEARTYPE_QUALITY);
    ctx->icon_small = dialog_detail::LoadOrCreateBrandIcon(S(kSmallBrandIconSize), instance);
    ctx->icon_big = dialog_detail::LoadOrCreateBrandIcon(S(kLargeBrandIconSize), instance);

    const auto& str = GetUiStrings(bound_config ? bound_config->ui_language : UiLanguage::Auto);

    constexpr DWORD wnd_style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    constexpr DWORD wnd_ex_style = WS_EX_APPWINDOW;
    const RECT bounds = ComputeCenteredWindowBounds(
        S(settings_layout::kClientSize.cx), S(settings_layout::kClientSize.cy),
        wnd_style, wnd_ex_style, mon.work_area);
    const int dlg_w = bounds.right - bounds.left;
    const int dlg_h = bounds.bottom - bounds.top;

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
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), is_dark);
    QIQI_LOG_DEBUG("SettingsWindow created (pos=({}, {}), size={}x{}, dpi={})",
                   bounds.left, bounds.top, dlg_w, dlg_h, mon.dpi);
    const gsl::not_null<DialogContext*> owned_ctx(ctx.release());
    dialog_detail::ApplyDialogIcons(hwnd.Get(), owned_ctx->icon_small.Get(), owned_ctx->icon_big.Get());

    CreateSettingsControls(hwnd.Get(), instance, *owned_ctx);
    owned_ctx->controls_initialized = true;
    if (bound_config) {
        WriteSettingsToWindowControls(hwnd.Get(), *bound_config);
    }
    InitSettingsCompartmentSubscription(hwnd.Get(), *owned_ctx);
    ApplySystemThemeToMenuAndWindow(hwnd.Get(), is_dark);
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
