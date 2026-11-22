// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/support.h"

namespace qiqi::test {

inline thread_local int dictionary_confirmation_button = IDCANCEL;
inline thread_local bool dictionary_confirmation_seen = false;

template <typename Action>
void WithDictionaryConfirmation(int button, Action action) {
    dictionary_confirmation_button = button;
    dictionary_confirmation_seen = false;
    const UINT_PTR timer = ::SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND dialog = FindThreadWindowByClass(L"#32770");
        if (!dialog) {
            return;
        }
        ::KillTimer(nullptr, id);
        dictionary_confirmation_seen = true;
        const HWND button = ::GetDlgItem(dialog, dictionary_confirmation_button);
        QIQI_TEST_CHECK(button);
        ::SendMessageW(button, BM_CLICK, 0, 0);
    });
    QIQI_TEST_CHECK(timer != 0);
    const auto cleanup = gsl::finally([timer]() noexcept { ::KillTimer(nullptr, timer); });
    action();
    QIQI_TEST_CHECK(dictionary_confirmation_seen);
}

void RunSettingsDictionaryTests();
void RunSettingsInteractionTests();
void RunSettingsLayoutTests();
void RunSettingsDisplayTests();

} // namespace qiqi::test
