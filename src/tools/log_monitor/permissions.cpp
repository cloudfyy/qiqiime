#include "tools/log_monitor/permissions.h"
#include "common/scoped_handle.h"

#include <commctrl.h>
#include <objbase.h>
#include <array>

namespace qiqi::monitor {
namespace {
HRESULT CALLBACK MarkElevatedRestartButton(HWND window, UINT notification, WPARAM, LPARAM, LONG_PTR) noexcept {
    if (notification == TDN_CREATED) {
        ::SendMessageW(window, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDYES, TRUE);
    }
    return S_OK;
}
} // namespace

ULONG QueryCapturePermissions(CapturePermissions& permissions) noexcept {
    ScopedKernelHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put())) return ::GetLastError();
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!::GetTokenInformation(token.Get(), TokenElevation, &elevation, sizeof(elevation), &size)) {
        return ::GetLastError();
    }
    std::array<BYTE, SECURITY_MAX_SID_SIZE> sid{};
    size = gsl::narrow_cast<DWORD>(sid.size());
    if (!::CreateWellKnownSid(WinBuiltinPerfLoggingUsersSid, nullptr, sid.data(), &size)) return ::GetLastError();
    BOOL member = FALSE;
    if (!::CheckTokenMembership(nullptr, sid.data(), &member)) return ::GetLastError();
    permissions = {elevation.TokenIsElevated != 0, member != FALSE};
    return ERROR_SUCCESS;
}

std::wstring CapturePermissionMessage(UiLanguage language, bool has_captured_logs) {
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    std::wstring message = english ?
        L"Your current permissions do not allow log capture. Restart the monitor as administrator, then click Start." :
        L"当前权限不足，无法开始采集。请以管理员身份重新启动日志监控，重启后再次点击“开始采集”。";
    if (has_captured_logs) {
        message += english ?
            L"\n\nRestarting clears captured logs. To keep them, cancel and export first." :
            L"\n\n重启会清空已采集的日志。如需保留，请先取消并导出。";
    }
    return message;
}

HRESULT ConfirmElevatedRestart(
    HWND owner, UiLanguage language, const CaptureStartFailure& failure, bool& accepted, bool has_captured_logs) {
    accepted = false;
    if (!failure.IsPermissionDenied()) return E_INVALIDARG;
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    const auto content = CapturePermissionMessage(language, has_captured_logs);
    const TASKDIALOG_BUTTON restart{IDYES, english ? L"Restart as administrator" : L"以管理员身份重新启动"};
#pragma warning(suppress : 26476) // Win32 requires this union-backed descriptor.
    TASKDIALOGCONFIG dialog{};
    dialog.cbSize = sizeof(dialog);
    dialog.hwndParent = owner;
    dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_SIZE_TO_CONTENT;
    dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    dialog.pszWindowTitle = english ? L"QiQiIME Log Monitor" : L"奇奇日志监控";
    dialog.pszMainIcon = TD_WARNING_ICON;
    dialog.pszMainInstruction = english ? L"Administrator permission needed" : L"需要管理员权限";
    dialog.pszContent = content.c_str();
    dialog.cButtons = 1;
    dialog.pButtons = &restart;
    dialog.nDefaultButton = IDCANCEL;
    dialog.pfCallback = MarkElevatedRestartButton;
    int button = IDCANCEL;
    const HRESULT result = ::TaskDialogIndirect(&dialog, &button, nullptr, nullptr);
    if (SUCCEEDED(result)) accepted = button == IDYES;
    return result;
}

ULONG LaunchElevatedMonitor(HWND owner, gsl::not_null<decltype(&::ShellExecuteExW)> execute) {
    const HRESULT co_result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(co_result)) return gsl::narrow_cast<ULONG>(co_result);
    const auto uninitialize = gsl::finally([]() noexcept { ::CoUninitialize(); });
    std::wstring executable(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, executable.data(), gsl::narrow_cast<DWORD>(executable.size()));
    if (!length) return ::GetLastError();
    if (length >= executable.size()) return ERROR_INSUFFICIENT_BUFFER;
    executable.resize(length);
#pragma warning(suppress : 26476) // Win32 requires this union-backed descriptor.
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = owner;
    info.lpVerb = L"runas";
    info.lpFile = executable.c_str();
    info.nShow = SW_SHOWNORMAL;
    const BOOL launched = execute(&info);
    const DWORD error = launched ? ERROR_SUCCESS : ::GetLastError();
    const ScopedKernelHandle process(info.hProcess);
    return error;
}

} // namespace qiqi::monitor
