// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wizard.h"

#include <gsl/gsl>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace qiqi::installer {
namespace {

struct DialogState {
    const WizardText& text;
    bool configuration;
    bool dictionaries;
    HRESULT error = S_OK;
};

HRESULT LastFailure() noexcept {
    const DWORD error = ::GetLastError();
    return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
}

INT_PTR CALLBACK DialogProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_INITDIALOG) {
        auto* state = reinterpret_cast<DialogState*>(lparam);
        ::SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = ::SetWindowLongPtrW(window, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        const DWORD error = ::GetLastError();
        if (previous == 0 && error != ERROR_SUCCESS) state->error = HRESULT_FROM_WIN32(error);
        if (SUCCEEDED(state->error) && !::SetWindowTextW(window, state->text.title.c_str())) {
            state->error = LastFailure();
        }
        for (const auto& control : {
                std::pair{1201, state->text.description.c_str()},
                std::pair{kConfigurationControl, state->text.configuration.c_str()},
                std::pair{kDictionariesControl, state->text.dictionaries.c_str()},
                std::pair{1202, state->text.warning.c_str()},
                std::pair{IDOK, state->text.remove.c_str()},
                std::pair{IDCANCEL, state->text.cancel.c_str()}}) {
            if (SUCCEEDED(state->error) && !::SetDlgItemTextW(window, control.first, control.second)) {
                state->error = LastFailure();
            }
        }
        if (SUCCEEDED(state->error) &&
            (!::CheckDlgButton(window, kConfigurationControl, state->configuration ? BST_CHECKED : BST_UNCHECKED) ||
             !::CheckDlgButton(window, kDictionariesControl, state->dictionaries ? BST_CHECKED : BST_UNCHECKED))) {
            state->error = E_FAIL;
        }
        if (FAILED(state->error)) ::EndDialog(window, IDCANCEL);
        if (SUCCEEDED(state->error)) {
            ::SetForegroundWindow(window);
            ::SetActiveWindow(window);
            ::BringWindowToTop(window);
        }
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const WORD command = LOWORD(wparam);
        if (command == IDOK || command == IDCANCEL) {
            auto* state = reinterpret_cast<DialogState*>(::GetWindowLongPtrW(window, DWLP_USER));
            if (command == IDOK && state) {
                state->configuration = ::IsDlgButtonChecked(window, kConfigurationControl) == BST_CHECKED;
                state->dictionaries = ::IsDlgButtonChecked(window, kDictionariesControl) == BST_CHECKED;
            }
            ::EndDialog(window, command);
            return TRUE;
        }
    }
    if (message == WM_CLOSE) {
        ::EndDialog(window, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

class DialogTemplate {
public:
    DialogTemplate(short width, short height) {
        const DLGTEMPLATE header{
            WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER |
                DS_SETFOREGROUND,
            0, 6, 0, 0, width, height
        };
        Append(header);
        Word(0);
        Word(0);
        Text(L"QiQi");
        Word(9);
        Text(L"Segoe UI");
        Control(1201, 0x0082, SS_LEFT, 16, 14, 328, 24);
        Control(kConfigurationControl, 0x0080, BS_AUTOCHECKBOX | WS_TABSTOP | BS_MULTILINE,
            16, 44, 328, 24);
        Control(kDictionariesControl, 0x0080, BS_AUTOCHECKBOX | WS_TABSTOP | BS_MULTILINE,
            16, 74, 328, 34);
        Control(1202, 0x0082, SS_LEFT, 16, 114, 328, 42);
        Control(IDOK, 0x0080, BS_DEFPUSHBUTTON | WS_TABSTOP, 192, 176, 72, 22);
        Control(IDCANCEL, 0x0080, BS_PUSHBUTTON | WS_TABSTOP, 272, 176, 72, 22);
    }

    const DLGTEMPLATE* Get() const noexcept {
        return reinterpret_cast<const DLGTEMPLATE*>(bytes_.data());
    }

private:
    template<typename T>
    void Append(const T& value) {
        const auto bytes = std::as_bytes(std::span{&value, size_t{1}});
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    }
    void Word(WORD value) { Append(value); }
    void Text(std::wstring_view value) {
        for (const wchar_t character : value) Word(gsl::narrow_cast<WORD>(character));
        Word(0);
    }
    void Control(WORD id, WORD type, DWORD style, short x, short y, short width, short height) {
        while (bytes_.size() % alignof(DWORD) != 0) bytes_.push_back(std::byte{0});
        const DLGITEMTEMPLATE item{WS_CHILD | WS_VISIBLE | style, 0, x, y, width, height, id};
        Append(item);
        Word(0xFFFF);
        Word(type);
        Word(0);
        Word(0);
    }
    std::vector<std::byte> bytes_;
};

} // namespace

HWND FindInstallerOwner(const wchar_t* product_name) noexcept {
    if (!product_name || !*product_name) return nullptr;
    const std::wstring_view product(product_name);
    HWND previous = nullptr;
    while (const HWND candidate =
               ::FindWindowExW(nullptr, previous, L"MsiDialogCloseClass", nullptr)) {
        DWORD process = 0;
        ::GetWindowThreadProcessId(candidate, &process);
        if (::IsWindowVisible(candidate) &&
            process == ::GetCurrentProcessId()) {
            std::array<wchar_t, 512> title{};
            const int length = ::GetWindowTextW(
                candidate, title.data(),
                gsl::narrow_cast<int>(title.size()));
            if (length > 0) {
                const std::wstring_view caption(title.data(), gsl::narrow_cast<size_t>(length));
                for (const auto indexed :
                     std::views::enumerate(
                         caption)) {
                    const auto offset =
                        gsl::narrow_cast<size_t>(
                            std::get<0>(
                                indexed));
                    if (caption.size() -
                            offset <
                        product.size()) {
                        break;
                    }
                    const auto slice = caption.substr(offset, product.size());
                    if (::CompareStringOrdinal(
                            slice.data(),
                            gsl::narrow_cast<int>(slice.size()),
                            product.data(),
                            gsl::narrow_cast<int>(product.size()),
                            TRUE) == CSTR_EQUAL) {
                        return candidate;
                    }
                }
            }
        }
        previous = candidate;
    }
    return nullptr;
}

HRESULT ShowDataChoices(const WizardText& text, bool& configuration, bool& dictionaries,
                        bool& accepted, HWND owner) {
    const DPI_AWARENESS_CONTEXT previous_dpi =
        ::SetThreadDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!previous_dpi) {
        return LastFailure();
    }
    const auto restore_dpi =
        gsl::finally([previous_dpi]() noexcept {
            std::ignore =
                ::SetThreadDpiAwarenessContext(
                    previous_dpi);
        });
    DialogState state{text, configuration, dictionaries};
    // Dialog templates use DLU, converted by Windows with the active DPI and font.
    const DialogTemplate dialog(360, 208);
    ::SetLastError(ERROR_SUCCESS);
    const INT_PTR result = ::DialogBoxIndirectParamW(nullptr, dialog.Get(), owner,
        DialogProcedure, reinterpret_cast<LPARAM>(&state));
    accepted = result == IDOK;
    if (FAILED(state.error)) return state.error;
    if (result == -1 || result == 0) {
        const DWORD error = ::GetLastError();
        return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
    }
    if (accepted) {
        configuration = state.configuration;
        dictionaries = state.dictionaries;
    }
    return S_OK;
}

} // namespace qiqi::installer
