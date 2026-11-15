// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestVersionResource() {
    auto path = GetModuleFilePath(nullptr);
    QIQI_TEST_CHECK(!path.empty());
    path.replace_filename(L"QiQiIMEDictManager.exe");

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
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionMS) == version::kMajor);
    QIQI_TEST_CHECK(LOWORD(info->dwFileVersionMS) == version::kMinor);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionLS) == version::kPatch);

    void* text = nullptr;
    UINT count = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(data.data(), L"\\StringFileInfo\\080404b0\\OriginalFilename", &text, &count));
    QIQI_TEST_CHECK(text != nullptr && std::wstring_view(static_cast<const wchar_t*>(text)) == L"QiQiIMEDictManager.exe");

    const ScopedModule resource(::LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    QIQI_TEST_CHECK(resource.IsValid());
    const HRSRC group = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    QIQI_TEST_CHECK(group != nullptr);
    const HGLOBAL loaded = ::LoadResource(resource.Get(), group);
    QIQI_TEST_CHECK(loaded != nullptr);
    const std::span<const BYTE> bytes(
        static_cast<const BYTE*>(::LockResource(loaded)), ::SizeofResource(resource.Get(), group));
    QIQI_TEST_CHECK(bytes.size() >= 6);
    WORD icon_count = 0;
    std::memcpy(&icon_count, bytes.subspan(4, 2).data(), 2);
    QIQI_TEST_CHECK(icon_count == 10 && bytes.size() >= 6 + size_t{icon_count} * 14);
    bool has_256_frame = false;
    for (size_t i = 0; i < icon_count; ++i) {
        const auto entry = bytes.subspan(6 + i * 14, 14);
        if (entry.front() == 0 && gsl::at(entry, 1) == 0) has_256_frame = true;
    }
    QIQI_TEST_CHECK(has_256_frame);
}

} // namespace qiqi::dict_manager::tests
