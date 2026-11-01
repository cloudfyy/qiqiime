#include "common/test_support.h"
#include "common/file_util.h"
#include "common/version.h"

#include <winver.h>
#include <cstddef>
#include <string>
#include <span>
#include <vector>

namespace qiqi::test {

void RunSettingsVersionTests() {
    auto path = GetModuleFilePath(nullptr);
    QIQI_TEST_CHECK(!path.empty());
    path.replace_filename(L"QiQiIMESettings.exe");

    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), nullptr);
    QIQI_TEST_CHECK(size > 0);
    std::vector<std::byte> data(size);
    QIQI_TEST_CHECK(::GetFileVersionInfoW(path.c_str(), 0, size, data.data()));

    void* value = nullptr;
    UINT length = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(data.data(), L"\\", &value, &length));
    QIQI_TEST_CHECK(value != nullptr && length >= sizeof(VS_FIXEDFILEINFO));
    const auto* info = static_cast<const VS_FIXEDFILEINFO*>(value);
    QIQI_TEST_CHECK(info->dwSignature == VS_FFI_SIGNATURE);
    QIQI_TEST_CHECK(info->dwFileType == VFT_APP);
    QIQI_TEST_CHECK(info->dwFileSubtype == 0);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionMS) == version::kMajor);
    QIQI_TEST_CHECK(LOWORD(info->dwFileVersionMS) == version::kMinor);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionLS) == version::kPatch);
    QIQI_TEST_CHECK(LOWORD(info->dwFileVersionLS) == version::kTweak);
    QIQI_TEST_CHECK(info->dwProductVersionMS == info->dwFileVersionMS);
    QIQI_TEST_CHECK(info->dwProductVersionLS == info->dwFileVersionLS);

    const auto query_string = [&data](const std::wstring& key) {
        void* text = nullptr;
        UINT count = 0;
        if (!::VerQueryValueW(data.data(), key.c_str(), &text, &count) || !text || count == 0) {
            QIQI_TEST_CHECK(false);
            return std::wstring_view{};
        }
        const auto* chars = static_cast<const wchar_t*>(text);
        const std::span<const wchar_t> chars_view(chars, count);
        QIQI_TEST_CHECK(gsl::at(chars_view, chars_view.size() - 1) == L'\0');
        return std::wstring_view(chars_view.data(), chars_view.size() - 1);
    };
    const std::wstring description =
        L"奇奇五笔设置 (QiQi Wubi Settings - " + std::wstring(version::kArchW) + L")";
    for (const auto* prefix : {L"\\StringFileInfo\\080404b0\\",
                               L"\\StringFileInfo\\040904b0\\"}) {
        const std::wstring block(prefix);
        QIQI_TEST_CHECK(query_string(block + L"InternalName") == L"QiQiIMESettings.exe");
        QIQI_TEST_CHECK(query_string(block + L"OriginalFilename") == L"QiQiIMESettings.exe");
        QIQI_TEST_CHECK(query_string(block + L"FileDescription") == description);
        QIQI_TEST_CHECK(query_string(block + L"FileVersion") == version::kFullVersionW);
        QIQI_TEST_CHECK(query_string(block + L"ProductVersion") == version::kFullVersionW);
        QIQI_TEST_CHECK(query_string(block + L"CompanyName") ==
                        L"cloudfyy (https://github.com/cloudfyy/qiqiime)");
        QIQI_TEST_CHECK(query_string(block + L"ProductName") == L"奇奇五笔 (QiQi Wubi IME)");
    }
}

} // namespace qiqi::test
