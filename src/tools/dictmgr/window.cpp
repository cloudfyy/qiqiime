// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/dictmgr/internal.h"
#include "common/dialog.h"
#include "common/dlgutil.h"
#include "common/fileutil.h"
#include "common/uiutil.h"
#include "common/version.h"

#include <gsl/gsl>
#include <format>
#include <system_error>

namespace qiqi::dict_manager {
namespace layout = dict_manager_layout;

std::wstring FormatWindowTitle(UiLanguage language) {
    return std::format(L"{} v{} ({})", GetUiStrings(language).dict_manager_name,
                       version::kVersionW, version::kArchW);
}

[[noreturn]] void ThrowUiError(const char* operation, DWORD error) {
    QIQI_LOG_ERROR("DictManager {} failed (error={})", operation, error);
    throw std::system_error(error, std::system_category(), operation);
}

void DictManagerWindow::Impl::ReportCommandError(void*, const char* operation, ULONG error) noexcept {
    QIQI_LOG_ERROR("DictManager UI operation '{}' failed (error=0x{:08X})", operation, error);
}

void DictManagerWindow::Impl::Failure(std::wstring_view operation, ULONG error) {
    QIQI_LOG_CAT_ERROR(
        log::Keyword::Dictionary,
        "DictManager failure: op='{}', error=0x{:08X}",
        WideToUtf8(operation),
        error);
    const auto message = FormatToolWindowFailureMessage(
        operation, error, ui_language);
    ::MessageBoxW(hwnd, message.c_str(), GetUiStrings(ui_language).dict_manager_name, MB_OK | MB_ICONERROR);
}

#pragma warning(suppress : 26455)
DictManagerWindow::DictManagerWindow() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}
DictManagerWindow::~DictManagerWindow() noexcept = default;

bool DictManagerWindow::Create(
    HINSTANCE instance,
    const std::filesystem::path& user_dict_path,
    const std::filesystem::path& phrase_freq_path) {
    if (window_) return true;
    std::ignore = log::EnsureInitialized();
    impl_->instance = instance;
    const auto config = LoadConfig();
    impl_->language_override.reset();
    impl_->ui_language = ResolveUiLanguage(config.ui_language);
    const std::filesystem::path module_path = GetModuleFilePath(instance);
    impl_->model.InitializePaths(
        module_path.empty() ? std::filesystem::path{} : module_path.parent_path(),
        user_dict_path,
        phrase_freq_path);

    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!::InitCommonControlsEx(&controls)) {
        impl_->Failure(L"InitCommonControlsEx", ERROR_DLL_INIT_FAILED);
        return false;
    }
    if (!dialog_detail::RegisterDialogWindowClass(instance, kWindowClass.data(), &Impl::Procedure,
            0, ::LoadIconW(instance, MAKEINTRESOURCEW(1)))) {
        impl_->Failure(L"RegisterClassEx", ::GetLastError());
        return false;
    }
    const auto title = FormatWindowTitle(impl_->ui_language);
    const auto monitor = QueryActiveMonitor(nullptr);
    const auto rect = ComputeCenteredToolWindowRect(
        monitor, layout::kInitialWindowSize.cx, layout::kInitialWindowSize.cy, monitor.dpi);
    window_.Reset(::CreateWindowExW(
        0,
        kWindowClass.data(),
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        impl_.get()));
    if (!window_) {
        impl_->Failure(L"CreateWindowEx", ::GetLastError());
        return false;
    }
    impl_->hwnd = window_.Get();
    std::ignore = log::CheckHResult(impl_->config_subscription.Subscribe(impl_->hwnd),
        "DictManager::SubscribeToConfigChanges", log::Level::warn);
    if (!impl_->CreateControls(GetDisplayPreferences(config))) return false;
    impl_->UpdateDpi(impl_->theme.dpi, ComputeCenteredToolWindowRect(
        monitor, layout::kInitialWindowSize.cx, layout::kInitialWindowSize.cy, impl_->theme.dpi));
    impl_->Layout();
    return true;
}

int RunDictManager(HINSTANCE instance, int show) {
    DictManagerWindow window;
    if (!window.Create(instance)) return 1;
    return RunToolWindowMessageLoop(window.Handle(), show);
}

} // namespace qiqi::dict_manager
