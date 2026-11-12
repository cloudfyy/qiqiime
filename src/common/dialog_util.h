// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/win32_ui_util.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"

#include <commdlg.h>
#include <cderr.h>
#include <filesystem>

namespace qiqi::dialog_detail {

enum class FileDialogMode { Open, Save };
enum class FileDialogStatus { Selected, Cancelled, Failed };

struct FileDialogResult {
    FileDialogStatus status = FileDialogStatus::Cancelled;
    DWORD error = ERROR_SUCCESS;
    constexpr explicit operator bool() const noexcept { return status == FileDialogStatus::Selected; }
};

struct FileDialogApi {
    gsl::not_null<decltype(&::GetOpenFileNameW)> open = &::GetOpenFileNameW;
    gsl::not_null<decltype(&::GetSaveFileNameW)> save = &::GetSaveFileNameW;
    gsl::not_null<decltype(&::CommDlgExtendedError)> error = &::CommDlgExtendedError;
};

inline FileDialogResult PromptForFile(
    HWND owner, const wchar_t* title, const wchar_t* filter, std::span<wchar_t> path,
    FileDialogMode mode, const wchar_t* default_extension = nullptr, const FileDialogApi& api = {}) noexcept {
    if (!title || !filter || path.empty() || path.size() > (std::numeric_limits<DWORD>::max)() ||
        std::ranges::find(path, L'\0') == path.end() ||
        (mode != FileDialogMode::Open && mode != FileDialogMode::Save)) {
        QIQI_LOG_CAT_WARN(log::Keyword::UI, "PromptForFile received invalid dialog parameters");
        return {FileDialogStatus::Failed, CDERR_INITIALIZATION};
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = title;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.lpstrDefExt = default_extension;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (mode == FileDialogMode::Open ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    const BOOL selected = mode == FileDialogMode::Open ? api.open(&dialog) : api.save(&dialog);
    if (!selected) {
        const DWORD error = api.error();
        if (error != ERROR_SUCCESS) {
            QIQI_LOG_CAT_WARN(log::Keyword::UI,
                "PromptForFile failed (mode={}, commdlg_error=0x{:08X})", std::to_underlying(mode), error);
            return {FileDialogStatus::Failed, error};
        }
        return {};
    }
    if (path.front() == L'\0') {
        QIQI_LOG_CAT_WARN(log::Keyword::UI, "PromptForFile returned an empty selected path");
        return {FileDialogStatus::Failed, FNERR_INVALIDFILENAME};
    }
    return {FileDialogStatus::Selected, ERROR_SUCCESS};
}

inline ScopedIcon LoadOrCreateBrandIcon(int size_px, HINSTANCE instance = nullptr) noexcept {
    // Only load embedded icon resource 1 when running inside standalone QiQiIMESettings.exe
    // (which embeds blue qiqiime-settings.ico); qiqiime.dll embeds a boxed black/white IME icon.
    bool use_resource = false;
    if (instance) {
        std::array<wchar_t, MAX_PATH> module_path{};
        const DWORD len = ::GetModuleFileNameW(
            instance, module_path.data(), gsl::narrow_cast<DWORD>(module_path.size()));
        if (len > 0 && len < module_path.size()) {
            const std::wstring_view full_view(module_path.data(), len);
            const size_t sep = full_view.find_last_of(L"\\/");
            const std::wstring_view filename =
                (sep == std::wstring_view::npos) ? full_view : full_view.substr(sep + 1);
            use_resource = (filename.size() == 19 &&
                            _wcsnicmp(filename.data(), L"QiQiIMESettings.exe", 19) == 0);
        }
    }
    return LoadAppIconOrBrandFallback(instance, size_px, 1, use_resource);
}

inline void ApplyDialogIcons(HWND hwnd, HICON small_icon, HICON big_icon) noexcept {
    if (!hwnd) return;
    if (small_icon) {
        ::SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    }
    if (big_icon) {
        ::SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
    }
}

inline bool RegisterDialogWindowClass(
    HINSTANCE instance,
    const wchar_t* class_name,
    WNDPROC wnd_proc,
    UINT style = 0,
    HICON icon = nullptr,
    HBRUSH background = nullptr) noexcept {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = style;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.hIcon = icon;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = background;
    wc.lpszClassName = class_name;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

inline int MeasureDialogTextHeight(HFONT font, const wchar_t* text, int width) noexcept {
    if (!font || !text || !*text || width <= 0) {
        QIQI_LOG_ERROR("Dialog text measurement requires a font, nonempty text and positive width");
        return 0;
    }
    const ScopedWindowDC dc;
    if (!dc) {
        QIQI_LOG_ERROR("Dialog text measurement DC unavailable");
        return 0;
    }
    const ScopedSelectObject select(dc.Get(), font);
    if (!select) {
        QIQI_LOG_ERROR("Dialog text measurement font selection failed");
        return 0;
    }
    RECT bounds{0, 0, width, 0};
    if (::DrawTextW(dc.Get(), text, -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX) <= 0) {
        QIQI_LOG_ERROR("Dialog text measurement failed");
        return 0;
    }
    return gsl::narrow_cast<int>(bounds.bottom);
}

inline bool RunModalDialogLoop(HWND hwnd, const bool& done_flag, bool route_dialog_keys = false) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) {
        return false;
    }
    const HWND owner = ::GetWindow(hwnd, GW_OWNER);
    const bool enable_owner_on_exit = owner && ::IsWindowEnabled(owner);
    const HWND previous_focus = ::GetFocus();
    if (enable_owner_on_exit) {
        ::EnableWindow(owner, FALSE);
    }
    const auto restore_owner = gsl::finally([owner, enable_owner_on_exit, previous_focus]() noexcept {
        if (enable_owner_on_exit && ::IsWindow(owner)) {
            ::EnableWindow(owner, TRUE);
            ::SetActiveWindow(owner);
            if (::IsWindow(previous_focus) &&
                (previous_focus == owner || ::IsChild(owner, previous_focus))) {
                ::SetFocus(previous_focus);
            }
        }
    });
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd);

    MSG msg{};
    while (::IsWindow(hwnd) && !done_flag) {
        const BOOL result = ::GetMessageW(&msg, nullptr, 0, 0);
        if (result == -1) {
            QIQI_LOG_ERROR("Modal dialog GetMessageW failed (error={})", ::GetLastError());
            return false;
        }
        if (result == 0) {
            ::PostQuitMessage(gsl::narrow_cast<int>(msg.wParam));
            return false;
        }
        if (route_dialog_keys && msg.message == WM_KEYDOWN &&
            (msg.hwnd == hwnd || ::IsChild(hwnd, msg.hwnd)) &&
            (msg.wParam == VK_TAB || msg.wParam == VK_RETURN || msg.wParam == VK_ESCAPE)) {
            ::SendMessageW(hwnd, WM_KEYDOWN, msg.wParam, msg.lParam);
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return ::IsWindow(hwnd) != FALSE;
}

inline bool PromptForPrimaryDictionaryFile(
    HWND owner,
    UiLanguage language,
    std::wstring_view initial_path,
    std::filesystem::path& out_selected_path) {
    const auto& str = GetUiStrings(language);
    std::array<wchar_t, kWin32PathBufferChars> path{};
    if (!initial_path.empty() && initial_path.size() < path.size()) {
        std::wmemcpy(path.data(), initial_path.data(), initial_path.size());
    }
    if (!PromptForFile(owner, str.dlg_open_primary_dict_title, str.dlg_open_primary_dict_filter,
            path, FileDialogMode::Open)) {
        return false;
    }
    out_selected_path = std::filesystem::path(path.data());
    return !out_selected_path.empty();
}

} // namespace qiqi::dialog_detail
