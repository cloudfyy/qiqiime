// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/transfer.h"
#include "tools/settings/internal.h"
#include "common/dictcodec.h"

#include <format>
#include <memory>
#include <new>

namespace qiqi::settings_detail {
namespace {

const wchar_t* Text(UiLanguage language, const wchar_t* zh, const wchar_t* en) noexcept {
    return ResolveUiLanguage(language) == UiLanguage::EnUs ? en : zh;
}

DialogContext* Context(HWND hwnd) noexcept {
    if (!::IsWindow(hwnd)) return nullptr;
    return reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

class TransferScope final {
public:
    explicit TransferScope(HWND hwnd) noexcept : hwnd_(hwnd), context_(Context(hwnd)) {
        if (!context_ || !context_->config || context_->done || context_->transfer_in_progress) {
            QIQI_LOG_WARN("Settings configuration transfer requires an idle bound settings window");
            context_ = nullptr;
        } else {
            context_->transfer_in_progress = true;
        }
    }
    TransferScope(const TransferScope&) = delete;
    TransferScope& operator=(const TransferScope&) = delete;
    TransferScope(TransferScope&&) = delete;
    TransferScope& operator=(TransferScope&&) = delete;
    ~TransferScope() {
        if (context_ && Context(hwnd_) == context_) {
            context_->transfer_in_progress = false;
            if (!::PostMessageW(hwnd_, kSettingsExternalConfigChangedMessage, 0, 0))
                QIQI_LOG_WARN("Settings post-transfer sync failed (error={})", ::GetLastError());
        }
    }
    DialogContext* Get() const noexcept { return context_; }
private:
    HWND hwnd_;
    DialogContext* context_;
};

void ShowTransferError(HWND hwnd, UiLanguage language, const ConfigJsonError& error, HRESULT result) {
    const auto message = FormatConfigJsonError(error, result, language);
    if (!::MessageBoxW(hwnd, message.c_str(),
        Text(language, L"配置导入／导出失败", L"Configuration transfer failed"), MB_OK | MB_ICONERROR)) {
        QIQI_LOG_ERROR("Configuration error dialog failed (error={})", ::GetLastError());
    }
}

struct PreviewContext {
    DisplayPreferences display;
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    bool done = false;
    bool accepted = false;
};

void LayoutPreview(HWND hwnd, const PreviewContext& ctx) noexcept {
    RECT bounds{};
    if (!::GetClientRect(hwnd, &bounds)) {
        QIQI_LOG_WARN("Configuration preview bounds unavailable (error={})", ::GetLastError());
        return;
    }
    const int inset = ctx.theme.ScaleDipPx(16);
    const int button_width = std::max(1, std::min(ctx.theme.ScalePx(112),
        (gsl::narrow_cast<int>(bounds.right) - 3 * inset) / 2));
    const int button_height = ctx.theme.ScalePx(36);
    const int footer = bounds.bottom - inset - button_height;
    const auto move = [&](int id, int x, int y, int width, int height) noexcept {
        if (!::SetWindowPos(::GetDlgItem(hwnd, id), nullptr, x, y,
            std::max(1, width), std::max(1, height), SWP_NOZORDER | SWP_NOACTIVATE)) {
            QIQI_LOG_WARN("Configuration preview control placement failed (error={})", ::GetLastError());
        }
    };
    move(kIdConfigChanges, inset, inset, bounds.right - 2 * inset, footer - 2 * inset);
    move(IDOK, bounds.right - 2 * inset - 2 * button_width, footer, button_width, button_height);
    move(IDCANCEL, bounds.right - inset - button_width, footer, button_width, button_height);
}

void RefreshPreview(HWND hwnd, PreviewContext& ctx, UINT dpi) noexcept {
    ctx.theme.Init(dpi);
    ctx.theme.SetDisplayPreferences(ctx.display);
    auto font = CreateUiFont(ctx.theme.ScalePx(15), FW_NORMAL, CLEARTYPE_QUALITY);
    if (!font) {
        QIQI_LOG_ERROR("Configuration preview font creation failed (error={})", ::GetLastError());
        return;
    }
    for (const int id : {kIdConfigChanges, IDOK, IDCANCEL})
        ::SendDlgItemMessageW(hwnd, id, WM_SETFONT, reinterpret_cast<WPARAM>(font.Get()), TRUE);
    ctx.font = std::move(font);
    ApplyDisplayThemeToWindow(hwnd, ctx.theme.appearance);
    LayoutPreview(hwnd, ctx);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* ctx = BindOrGetWindowUserData<PreviewContext>(hwnd, message, lparam);
    if (!ctx || message == WM_NCCREATE) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
        case WM_COMMAND:
            if (LOWORD(wparam) != IDOK && LOWORD(wparam) != IDCANCEL) return 0;
            ctx->accepted = LOWORD(wparam) == IDOK;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_CLOSE:
            ctx->accepted = false;
            ctx->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE || wparam == VK_RETURN) {
                const bool cancel = wparam == VK_ESCAPE || ::GetDlgCtrlID(::GetFocus()) == IDCANCEL;
                return ::SendMessageW(hwnd, WM_COMMAND, cancel ? IDCANCEL : IDOK, 0);
            }
            if (wparam == VK_TAB) {
                const HWND next = ::GetNextDlgTabItem(hwnd, ::GetFocus(), (::GetKeyState(VK_SHIFT) & 0x8000) != 0);
                if (next) ::SetFocus(next);
                return 0;
            }
            break;
        case WM_SETFOCUS:
            ::SetFocus(::GetDlgItem(hwnd, IDCANCEL));
            return 0;
        case WM_SIZE:
            LayoutPreview(hwnd, *ctx);
            return 0;
        case WM_DPICHANGED:
            if (lparam) {
                const auto& rect = *reinterpret_cast<const RECT*>(lparam);
                if (!::SetWindowPos(hwnd, nullptr, rect.left, rect.top,
                    rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE)) {
                    QIQI_LOG_WARN("Configuration preview DPI placement failed (error={})", ::GetLastError());
                }
            }
            RefreshPreview(hwnd, *ctx, HIWORD(wparam));
            return 0;
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            ctx->theme.SetDisplayPreferences(ctx->display);
            ApplyDisplayThemeToWindow(hwnd, ctx->theme.appearance);
            ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
            return 0;
        case WM_DRAWITEM:
            if (lparam) {
                const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                if (item.CtlType == ODT_BUTTON) {
                    DrawFluentPushButton(item, ctx->theme, item.CtlID == IDOK,
                        ColorRefToD2DColor(ctx->theme.appearance.palette.window_bg));
                    return TRUE;
                }
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            return reinterpret_cast<LRESULT>(
                ApplyFluentControlColors(reinterpret_cast<HDC>(wparam), ctx->theme, true));
        case WM_ERASEBKGND: {
            const ScopedGdiHandle<HBRUSH> brush(::CreateSolidBrush(ctx->theme.appearance.palette.window_bg));
            RECT rect{};
            if (brush && ::GetClientRect(hwnd, &rect)) {
                ::FillRect(reinterpret_cast<HDC>(wparam), &rect, brush.Get());
                return TRUE;
            }
            break;
        }
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default: break;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

HRESULT ConfirmImport(HWND parent, const QiQiConfig& before, const QiQiConfig& after, UiLanguage language) {
    const auto body = FormatSettingsConfigChanges(before, after, language);
    const HINSTANCE instance = g_dll_instance ? g_dll_instance : ::GetModuleHandleW(nullptr);
    if (!dialog_detail::RegisterDialogWindowClass(instance, kSettingsImportPreviewWndClass, PreviewWndProc))
        return HRESULT_FROM_WIN32(::GetLastError());
    const auto monitor = QueryActiveMonitor(parent);
    PreviewContext context;
    context.display = GetDisplayPreferences(before);
    context.theme.Init(monitor.dpi);
    context.theme.SetDisplayPreferences(context.display);
    context.font = CreateUiFont(context.theme.ScalePx(15), FW_NORMAL, CLEARTYPE_QUALITY);
    if (!context.font) return E_OUTOFMEMORY;
    constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    const int margin = context.theme.ScaleDipPx(32);
    const RECT bounds = ComputeCenteredWindowBounds(
        std::min(context.theme.ScalePx(720), gsl::narrow_cast<int>(
            monitor.work_area.right - monitor.work_area.left) - margin),
        std::min(context.theme.ScalePx(480), gsl::narrow_cast<int>(
            monitor.work_area.bottom - monitor.work_area.top) - margin),
        style, 0, monitor.work_area);
    const ScopedWindow window(::CreateWindowExW(0, kSettingsImportPreviewWndClass,
        Text(language, L"导入配置：变更预览", L"Import configuration: changes"), style,
        bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, instance, &context));
    if (!window) return HRESULT_FROM_WIN32(::GetLastError());
    const auto create = [&](const wchar_t* class_name, const wchar_t* text, DWORD child_style, int id) noexcept {
        return CreateFluentChildControl(window.Get(), instance, context.font.Get(), &context.theme,
            class_name, text, child_style, 0, 0, 1, 1, id);
    };
    if (!create(L"EDIT", body.c_str(), ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
            kIdConfigChanges) ||
        !create(L"BUTTON", Text(language, L"加载配置", L"Load config"), BS_OWNERDRAW | WS_TABSTOP, IDOK) ||
        !create(L"BUTTON", GetUiStrings(language).dlg_btn_cancel, BS_OWNERDRAW | WS_TABSTOP, IDCANCEL))
        return HRESULT_FROM_WIN32(::GetLastError());
    ApplyDisplayThemeToWindow(window.Get(), context.theme.appearance);
    LayoutPreview(window.Get(), context);
    if (!dialog_detail::RunModalDialogLoop(window.Get(), context.done, true)) return E_ABORT;
    return context.accepted ? S_OK : S_FALSE;
}

} // namespace

std::wstring FormatConfigJsonError(const ConfigJsonError& error, HRESULT result, UiLanguage language) {
    const wchar_t* reason = Text(language, L"读取或写入失败。", L"Could not read or write the configuration.");
    switch (error.issue) {
        case ConfigJsonIssue::Syntax:
            reason = Text(language, L"JSON 或 UTF-8 无效；请检查语法。", L"Invalid JSON or UTF-8; check the syntax."); break;
        case ConfigJsonIssue::TooLarge:
            reason = Text(language, L"配置文件不得超过 64 KiB。", L"The configuration must not exceed 64 KiB."); break;
        case ConfigJsonIssue::ObjectRequired:
            reason = Text(language, L"必须使用 JSON 对象。", L"A JSON object is required."); break;
        case ConfigJsonIssue::UnknownField:
            reason = Text(language, L"不支持此字段；不可导入路径或任意注册表项。",
                L"Unknown field; paths and arbitrary registry values are not importable."); break;
        case ConfigJsonIssue::DuplicateField:
            reason = Text(language, L"字段重复；每个字段只能出现一次。", L"Duplicate field; each field may appear only once."); break;
        case ConfigJsonIssue::MissingField:
            reason = Text(language, L"缺少必需字段。", L"A required field is missing."); break;
        case ConfigJsonIssue::InvalidType:
            reason = Text(language, L"字段类型不正确：开关使用布尔值，候选数使用整数，枚举使用字符串。",
                L"Wrong type: use booleans for toggles, an integer for page size, and strings for enums."); break;
        case ConfigJsonIssue::OutOfRange:
            reason = Text(language, L"数值超出范围；每页候选数必须为 1–9。",
                L"Value out of range; page size must be 1-9."); break;
        case ConfigJsonIssue::InvalidEnum:
            reason = Text(language, L"枚举名称无效；请使用导出文件中的稳定名称。",
                L"Invalid enum name; use the stable names shown in exported files."); break;
        case ConfigJsonIssue::UnsupportedFormat:
            reason = Text(language, L"format 必须为 \"qiqiime-config\"。", L"format must be \"qiqiime-config\"."); break;
        case ConfigJsonIssue::UnsupportedVersion:
            reason = Text(language, L"只支持整数 version: 1。", L"Only integer version: 1 is supported."); break;
        case ConfigJsonIssue::CustomDictionaryRequired:
            reason = Text(language, L"请先在本机选择自定义主词库；配置文件不携带词库路径。",
                L"Select a custom primary dictionary locally first; configuration files contain no dictionary paths."); break;
        case ConfigJsonIssue::ComfortScaleRequired:
            reason = Text(language, L"关怀模式需要至少 150% 字号；请同时设置 ui_text_scale。",
                L"Comfort mode requires at least 150% text size; also set ui_text_scale."); break;
        case ConfigJsonIssue::None: break;
    }
    std::wstring field;
    const HRESULT conversion = Utf8ToWideResult(error.field, field);
    if (FAILED(conversion)) return std::format(L"{} HRESULT: 0x{:08X}", reason, static_cast<unsigned long>(result));
    if (field.size() > 256) field.resize(256);
    for (auto& ch : field) if (ch < L' ') ch = L'?';
    return std::format(L"{}\r\n{}{}\r\n{}{}\r\nHRESULT: 0x{:08X}",
        reason, Text(language, L"字段：", L"Field: "), field,
        Text(language, L"字节位置：", L"Byte position: "), error.byte, static_cast<unsigned long>(result));
}

std::wstring FormatSettingsConfigChanges(
    const QiQiConfig& before, const QiQiConfig& after, UiLanguage language) {
    auto message = std::wstring(Text(language,
        L"确认后加载到设置窗口；点击“确定”才保存，取消设置则放弃。\r\n"
        L"未列出的设置、词库路径和用户词库不会更改。\r\n\r\n",
        L"Load these changes into Settings. Click OK there to save, or Cancel to discard.\r\n"
        L"Unlisted settings, dictionary paths, and user dictionaries are unchanged.\r\n\r\n"));
    const auto changes = GetConfigJsonChanges(before, after);
    if (changes.empty()) {
        message += Text(language, L"没有配置变化。", L"No configuration changes.");
    }
    for (const auto& change : changes) {
        const auto* label = SettingsConfigFieldLabel(change.key, language);
        message += std::format(L"{} ({})\r\n    {} -> {}\r\n\r\n",
            label ? label : L"", std::wstring(change.key.begin(), change.key.end()),
            std::wstring(change.before.begin(), change.before.end()),
            std::wstring(change.after.begin(), change.after.end()));
    }
    return message;
}

HRESULT ExportSettingsConfiguration(HWND hwnd, const std::filesystem::path& path) {
    const TransferScope transfer(hwnd);
    const auto* ctx = transfer.Get();
    if (!ctx) return E_ABORT;
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    ConfigJsonError error;
    HRESULT result = S_OK;
    try {
        QiQiConfig current = *ctx->config;
        ReadSettingsFromWindowControls(hwnd, current);
        std::string json;
        result = SerializeConfigJson(current, json, error);
        if (SUCCEEDED(result)) result = WriteFileAtomicallyResult(path, codec::AsByteSpan(json));
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
    } catch (const std::filesystem::filesystem_error& failure) {
        result = HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(failure.code().value()));
    }
    if (FAILED(result)) {
        std::ignore = log::CheckHResult(result, "ExportSettingsConfiguration");
        ShowTransferError(hwnd, language, error, result);
    }
    return result;
}

HRESULT ImportSettingsConfiguration(HWND hwnd, const std::filesystem::path& path) {
    const TransferScope transfer(hwnd);
    auto* ctx = transfer.Get();
    if (!ctx) return E_ABORT;
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    ConfigJsonError error;
    HRESULT result = S_OK;
    try {
        std::string json;
        result = ReadFileToUtf8StringResult(path, json, kMaxConfigJsonBytes);
        if (result == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE)) error.issue = ConfigJsonIssue::TooLarge;
        QiQiConfig current = *ctx->config;
        ReadSettingsFromWindowControls(hwnd, current);
        QiQiConfig candidate;
        if (SUCCEEDED(result)) result = ParseConfigJson(json, current, candidate, error);
        if (SUCCEEDED(result)) {
            result = ConfirmImport(hwnd, current, candidate, language);
            if (result == S_OK && Context(hwnd) == ctx && !ctx->done) {
                std::optional<QiQiConfig> imported(std::move(candidate));
                result = RenderSettingsDraft(hwnd, *ctx, imported.value());
                if (SUCCEEDED(result)) {
                    ctx->imported_config = std::move(imported);
                    UpdateSettingsWindowTitle(hwnd, *ctx);
                }
            } else if (result == S_OK) {
                result = E_ABORT;
            }
        }
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
    } catch (const std::filesystem::filesystem_error& failure) {
        result = HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(failure.code().value()));
    }
    if (FAILED(result)) {
        std::ignore = log::CheckHResult(result, "ImportSettingsConfiguration");
        ShowTransferError(::IsWindow(hwnd) ? hwnd : nullptr, language, error, result);
    }
    return result;
}

void PromptSettingsConfigTransfer(HWND hwnd, bool importing, const dialog_detail::FileDialogApi& api) {
    const auto* ctx = Context(hwnd);
    if (!ctx || !ctx->config) {
        QIQI_LOG_WARN("Configuration file dialog requires a bound settings window");
        return;
    }
    const UiLanguage language = GetSelectedUiLanguageFromCombo(hwnd, *ctx);
    std::array<wchar_t, kWin32PathBufferChars> path{};
    if (!importing) wcscpy_s(path.data(), path.size(), L"qiqiime-config.json");
    dialog_detail::FileDialogResult selected;
    {
        const TransferScope transfer(hwnd);
        if (!transfer.Get()) return;
        selected = dialog_detail::PromptForFile(hwnd,
            Text(language, importing ? L"导入配置 JSON" : L"导出当前配置 JSON",
                importing ? L"Import configuration JSON" : L"Export current configuration JSON"),
            Text(language, L"配置 JSON (*.json)\0*.json\0所有文件 (*.*)\0*.*\0",
                L"Configuration JSON (*.json)\0*.json\0All files (*.*)\0*.*\0"),
            path, importing ? dialog_detail::FileDialogMode::Open : dialog_detail::FileDialogMode::Save,
            L"json", api);
    }
    if (selected.status == dialog_detail::FileDialogStatus::Failed) {
        const auto message = std::format(L"{}\r\nCommDlgExtendedError: 0x{:08X}",
            Text(language, L"无法打开配置文件选择窗口。", L"Could not open the configuration file dialog."),
            selected.error);
        if (!::MessageBoxW(hwnd, message.c_str(), GetUiStrings(language).dlg_title, MB_OK | MB_ICONERROR))
            QIQI_LOG_ERROR("Configuration file-dialog error message failed (error={})", ::GetLastError());
    } else if (selected) {
        if (importing) std::ignore = ImportSettingsConfiguration(hwnd, path.data());
        else std::ignore = ExportSettingsConfiguration(hwnd, path.data());
    }
}

} // namespace qiqi::settings_detail
