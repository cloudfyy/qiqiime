// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/fileutil.h"

#include <tlhelp32.h>
#include <array>

namespace qiqi::diag::detail {

void CollectAccessibilityInformation(Json& stage) {
    Json accessibility = Json::object();

    BOOL screen_reader = FALSE;
    HRESULT screen_reader_result = ::SystemParametersInfoW(SPI_GETSCREENREADER, 0, &screen_reader, 0) != FALSE
                                       ? S_OK
                                       : HRESULT_FROM_WIN32(::GetLastError());
    accessibility["screen_reader"] = screen_reader != FALSE;
    AddCheck(stage, CheckResult("screen_reader", screen_reader_result,
                                FAILED(screen_reader_result) ? "warning" : ""));

    BOOL keyboard_preference = FALSE;
    const HRESULT keyboard_result =
        ::SystemParametersInfoW(SPI_GETKEYBOARDPREF, 0, &keyboard_preference, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    accessibility["keyboard_preference"] = keyboard_preference != FALSE;
    AddCheck(stage,
             CheckResult("keyboard_preference", keyboard_result, FAILED(keyboard_result) ? "warning" : ""));

    std::array<wchar_t, MAX_PATH> high_contrast_scheme{};
    HIGHCONTRASTW high_contrast{
        .cbSize = sizeof(HIGHCONTRASTW),
        .dwFlags = 0,
        .lpszDefaultScheme = high_contrast_scheme.data(),
    };
    const HRESULT high_contrast_result =
        ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, high_contrast.cbSize, &high_contrast, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    if (SUCCEEDED(high_contrast_result)) {
        accessibility["high_contrast"] = (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        accessibility["high_contrast_available"] = (high_contrast.dwFlags & HCF_AVAILABLE) != 0;
        accessibility["high_contrast_hotkey_active"] = (high_contrast.dwFlags & HCF_HOTKEYACTIVE) != 0;
        if (high_contrast_scheme.front() != L'\0') {
            accessibility["high_contrast_scheme"] =
                WideToUtf8(RedactPathForReport(high_contrast_scheme.data()));
        }
    }
    AddCheck(stage, CheckResult("high_contrast", high_contrast_result,
                                FAILED(high_contrast_result) ? "warning" : ""));

    ANIMATIONINFO animation{.cbSize = sizeof(ANIMATIONINFO)};
    const HRESULT animation_result =
        ::SystemParametersInfoW(SPI_GETANIMATION, animation.cbSize, &animation, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    if (SUCCEEDED(animation_result)) {
        accessibility["animations"] = animation.iMinAnimate != 0;
    }
    AddCheck(stage, CheckResult("animations", animation_result, FAILED(animation_result) ? "warning" : ""));

    BOOL client_animation = FALSE;
    const HRESULT client_animation_result =
        ::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &client_animation, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    accessibility["client_area_animation"] = client_animation != FALSE;
    AddCheck(stage, CheckResult("client_area_animation", client_animation_result,
                                FAILED(client_animation_result) ? "warning" : ""));

    STICKYKEYS sticky{.cbSize = sizeof(STICKYKEYS)};
    const HRESULT sticky_result =
        ::SystemParametersInfoW(SPI_GETSTICKYKEYS, sticky.cbSize, &sticky, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    AddCheck(stage, CheckResult("sticky_keys", sticky_result, FAILED(sticky_result) ? "warning" : ""));
    if (SUCCEEDED(sticky_result)) {
        accessibility["sticky_keys"] = (sticky.dwFlags & SKF_STICKYKEYSON) != 0;
    }
    FILTERKEYS filter{.cbSize = sizeof(FILTERKEYS)};
    const HRESULT filter_result =
        ::SystemParametersInfoW(SPI_GETFILTERKEYS, filter.cbSize, &filter, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    AddCheck(stage, CheckResult("filter_keys", filter_result, FAILED(filter_result) ? "warning" : ""));
    if (SUCCEEDED(filter_result)) {
        accessibility["filter_keys"] = (filter.dwFlags & FKF_FILTERKEYSON) != 0;
    }
    TOGGLEKEYS toggle{.cbSize = sizeof(TOGGLEKEYS)};
    const HRESULT toggle_result =
        ::SystemParametersInfoW(SPI_GETTOGGLEKEYS, toggle.cbSize, &toggle, 0) != FALSE
            ? S_OK
            : HRESULT_FROM_WIN32(::GetLastError());
    AddCheck(stage, CheckResult("toggle_keys", toggle_result, FAILED(toggle_result) ? "warning" : ""));
    if (SUCCEEDED(toggle_result)) {
        accessibility["toggle_keys"] = (toggle.dwFlags & TKF_TOGGLEKEYSON) != 0;
    }
    MOUSEKEYS mouse{.cbSize = sizeof(MOUSEKEYS)};
    const HRESULT mouse_result = ::SystemParametersInfoW(SPI_GETMOUSEKEYS, mouse.cbSize, &mouse, 0) != FALSE
                                     ? S_OK
                                     : HRESULT_FROM_WIN32(::GetLastError());
    AddCheck(stage, CheckResult("mouse_keys", mouse_result, FAILED(mouse_result) ? "warning" : ""));
    if (SUCCEEDED(mouse_result)) {
        accessibility["mouse_keys"] = (mouse.dwFlags & MKF_MOUSEKEYSON) != 0;
    }

    Json narrator = Json::array();
    ScopedKernelHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot) {
        PROCESSENTRY32W process{};
        process.dwSize = sizeof(process);
        if (::Process32FirstW(snapshot.Get(), &process) != FALSE) {
            do {
                if (_wcsicmp(process.szExeFile, L"Narrator.exe") == 0) {
                    narrator.push_back(ProcessDetails(process));
                }
            } while (::Process32NextW(snapshot.Get(), &process) != FALSE);
        } else {
            AddCheck(stage,
                     CheckResult("narrator_processes", HRESULT_FROM_WIN32(::GetLastError()), "warning"));
        }
    } else {
        AddCheck(stage, CheckResult("narrator_processes", HRESULT_FROM_WIN32(::GetLastError()), "warning"));
    }
    accessibility["narrator_processes"] = narrator;
    accessibility["narrator_running"] = !narrator.empty();
    accessibility["narrator_process_count"] = narrator.size();
    stage["accessibility"] = std::move(accessibility);
}

} // namespace qiqi::diag::detail
