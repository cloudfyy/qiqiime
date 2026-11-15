// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "actions.h"
#include "common/RAII.h"

#include <msiquery.h>
#include <gsl/gsl>
#include <tuple>

namespace {

struct MsiRecordTraits {
    using HandleType = MSIHANDLE;
    static constexpr MSIHANDLE Invalid() noexcept { return 0; }
    static constexpr bool IsValid(MSIHANDLE record) noexcept { return record != 0; }
    static void Close(MSIHANDLE record) noexcept { std::ignore = ::MsiCloseHandle(record); }
};

using ScopedMsiRecord = qiqi::ScopedHandle<MsiRecordTraits>;

UINT ReportFailure(MSIHANDLE install, const wchar_t* operation, int code) noexcept {
    const ScopedMsiRecord record(::MsiCreateRecord(2));
    if (!record ||
        ::MsiRecordSetStringW(record.Get(), 0, L"QiQiIME uninstall prompt: [1] failed (code=[2]).") != ERROR_SUCCESS ||
        ::MsiRecordSetStringW(record.Get(), 1, operation) != ERROR_SUCCESS ||
        ::MsiRecordSetInteger(record.Get(), 2, code) != ERROR_SUCCESS) {
        ::OutputDebugStringW(L"QiQiIME uninstall prompt failed; MSI diagnostic record creation also failed.\n");
        return ERROR_INSTALL_FAILURE;
    }
    if (::MsiProcessMessage(install, INSTALLMESSAGE_INFO, record.Get()) == -1) {
        ::OutputDebugStringW(L"QiQiIME uninstall prompt failed; MSI diagnostic delivery also failed.\n");
    }
    return ERROR_INSTALL_FAILURE;
}

} // namespace

extern "C" UINT __stdcall PromptRemoveUserData(MSIHANDLE install) noexcept {
    // Basic/reduced UI skips authored modal dialogs. Full UI keeps its checkbox.
    const MSICONDITION condition = ::MsiEvaluateConditionW(install,
        L"Installed AND REMOVE=\"ALL\" AND NOT UPGRADINGPRODUCTCODE "
        L"AND UILevel >= 3 AND UILevel < 5 AND NOT QIQI_REMOVE_USER_DATA");
    if (condition == MSICONDITION_FALSE) return ERROR_SUCCESS;
    if (condition != MSICONDITION_TRUE) {
        return ReportFailure(install, L"MsiEvaluateCondition", condition);
    }
    const MSICONDITION has_prompt = ::MsiEvaluateConditionW(install, L"QIQI_REMOVE_USER_DATA_PROMPT");
    if (has_prompt != MSICONDITION_TRUE) {
        return ReportFailure(install, L"Missing prompt text", has_prompt);
    }

    const ScopedMsiRecord record(::MsiCreateRecord(0));
    if (!record) return ReportFailure(install, L"MsiCreateRecord", ERROR_NOT_ENOUGH_MEMORY);
    UINT result = ::MsiRecordSetStringW(record.Get(), 0, L"[QIQI_REMOVE_USER_DATA_PROMPT]");
    if (result != ERROR_SUCCESS) {
        return ReportFailure(install, L"MsiRecordSetString", gsl::narrow_cast<int>(result));
    }
    const int choice = ::MsiProcessMessage(install,
        static_cast<INSTALLMESSAGE>(INSTALLMESSAGE_USER | MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON2),
        record.Get());
    switch (choice) {
    case IDYES:
        result = ::MsiSetPropertyW(install, L"QIQI_REMOVE_USER_DATA", L"1");
        break;
    case IDNO:
        result = ::MsiSetPropertyW(install, L"QIQI_REMOVE_USER_DATA", nullptr);
        break;
    case IDCANCEL:
        return ERROR_INSTALL_USEREXIT;
    default:
        return ReportFailure(install, L"MsiProcessMessage", choice);
    }
    return result == ERROR_SUCCESS ? ERROR_SUCCESS :
        ReportFailure(install, L"MsiSetProperty", gsl::narrow_cast<int>(result));
}
