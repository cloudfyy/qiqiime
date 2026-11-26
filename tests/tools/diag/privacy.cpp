// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

#include "tools/diag/diag.h"

#include "common/support.h"

#include <gsl/gsl>

#include <array>
#include <filesystem>
#include <string>

void TestPathPrivacy() {
    std::array<wchar_t, 32'768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(L"USERPROFILE", buffer.data(), gsl::narrow_cast<DWORD>(buffer.size()));
    QIQI_TEST_CHECK(length > 0);
    QIQI_TEST_CHECK(length < buffer.size());
    const std::wstring profile(buffer.data(), length);
    const std::filesystem::path path = std::filesystem::path(profile) / L"private\\settings.json";
    const std::wstring redacted = qiqi::diag::RedactPathForReport(path.wstring());
    QIQI_TEST_CHECK(redacted.starts_with(L"<USERPROFILE>"));
    QIQI_TEST_CHECK(!std::wstring_view(redacted).contains(profile));
    QIQI_TEST_CHECK(qiqi::diag::ExtractLocalServerExecutable(L"\"C:\\Program Files\\QiQiIME\\x64\\"
                                                             L"QiQiIMEConfigBroker.exe\" -Embedding") ==
                    L"C:\\Program Files\\QiQiIME\\x64\\"
                    L"QiQiIMEConfigBroker.exe");
}
