// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/test.h"

namespace qiqi::test::common_cases {

static_assert(kWin32PathBufferChars == 32'768);

void TestFileUtilityBoundaries() {
    QIQI_TEST_CHECK(WideToUtf8(std::wstring_view(L"A\0B", 3)) == std::string("A\0B", 3));
    QIQI_TEST_CHECK(Utf8ToWide(std::string_view("A\0B", 3)) == std::wstring(L"A\0B", 3));

    const auto path = std::filesystem::temp_directory_path() /
        (L"qiqi_file_boundaries_" + std::to_wstring(::GetCurrentProcessId()) + L".bin");
    const auto cleanup = gsl::finally([&path]() noexcept {
        std::error_code error;
        std::filesystem::remove(path, error);
        QIQI_TEST_CHECK(!error);
    });
    constexpr LONGLONG limit = 64LL * 1024LL * 1024LL;
    {
        const ScopedFileHandle file(::CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(file.IsValid());
        LARGE_INTEGER size{};
        size.QuadPart = limit;
        QIQI_TEST_CHECK(::SetFilePointerEx(file.Get(), size, nullptr, FILE_BEGIN));
        QIQI_TEST_CHECK(::SetEndOfFile(file.Get()));
    }
    std::string content;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content));
    QIQI_TEST_CHECK(content.size() == gsl::narrow_cast<size_t>(limit));
    {
        const ScopedFileHandle file(::CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(file.IsValid());
        LARGE_INTEGER size{};
        size.QuadPart = limit + 1;
        QIQI_TEST_CHECK(::SetFilePointerEx(file.Get(), size, nullptr, FILE_BEGIN));
        QIQI_TEST_CHECK(::SetEndOfFile(file.Get()));
    }
    QIQI_TEST_CHECK(!ReadFileToUtf8String(path, content));
    QIQI_TEST_CHECK(!ReadFileToUtf8String(path / L"missing", content));

    constexpr std::array<uint8_t, 3> original{'o', 'l', 'd'};
    constexpr std::array<uint8_t, 3> replacement{'n', 'e', 'w'};
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(path, original) == S_OK);
    {
        const ScopedFileHandle locked(::CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked.IsValid());
        const HRESULT failed_write = WriteFileAtomicallyResult(path, replacement);
        QIQI_TEST_CHECK(failed_write == E_ACCESSDENIED ||
                        failed_write == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == "old");
        QIQI_TEST_CHECK(!std::filesystem::exists(
            path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId())));
    }
    QIQI_TEST_CHECK(WriteFileAtomicallyResult(path, replacement) == S_OK);
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == "new");

    const auto with_environment = [](const wchar_t* name, const wchar_t* value, const auto& action) {
        std::wstring previous(32768, L'\0');
        const DWORD length = ::GetEnvironmentVariableW(
            name, previous.data(), gsl::narrow_cast<DWORD>(previous.size()));
        QIQI_TEST_CHECK(length < previous.size());
        previous.resize(length);
        const auto restore_environment = gsl::finally([&previous, name]() noexcept {
            QIQI_TEST_CHECK(::SetEnvironmentVariableW(name, previous.empty() ? nullptr : previous.c_str()));
        });
        QIQI_TEST_CHECK(::SetEnvironmentVariableW(name, value));
        action();
    };
    const std::wstring long_directory =
        L"C:\\" + std::wstring(100, L'a') + L"\\" + std::wstring(100, L'b') +
        L"\\" + std::wstring(100, L'c');
    with_environment(L"LOCALAPPDATA", long_directory.c_str(), [&]() {
        QIQI_TEST_CHECK(GetAppDataDirectory() == std::filesystem::path(long_directory) / L"QiQiIME");
        const auto freq_path = std::filesystem::path(long_directory) / L"QiQiIME" / L"phrase_freq.qfreq";
        QIQI_TEST_CHECK(GetDefaultPhraseFreqPath() == freq_path);
        QIQI_TEST_CHECK(GetSyncChannelDirectoryForFile(freq_path) ==
            std::filesystem::path(freq_path.wstring() + L".sync"));
    });
    with_environment(L"LOCALAPPDATA", nullptr, [&]() {
        std::wstring temporary(32768, L'\0');
        const DWORD length = ::GetTempPathW(gsl::narrow_cast<DWORD>(temporary.size()), temporary.data());
        QIQI_TEST_CHECK(length > 0 && length < temporary.size());
        QIQI_TEST_CHECK(GetAppDataDirectory() == std::filesystem::path(temporary.data()) / L"QiQiIME");
        QIQI_TEST_CHECK(GetDefaultPhraseFreqPath() ==
            std::filesystem::path(temporary.data()) / L"QiQiIME" / L"phrase_freq.qfreq");
    });
}

void TestScopedHandleTemplate() {
    ScopedKernelHandle h1(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    QIQI_TEST_CHECK(h1.IsValid());
    QIQI_TEST_CHECK(static_cast<bool>(h1));

    // Move construction
    ScopedKernelHandle h2(std::move(h1));
    QIQI_TEST_CHECK(!h1.IsValid());
    QIQI_TEST_CHECK(h2.IsValid());

    // Move assignment
    ScopedKernelHandle h3;
    h3 = std::move(h2);
    QIQI_TEST_CHECK(!h2.IsValid());
    QIQI_TEST_CHECK(h3.IsValid());

    h3.Reset();
    QIQI_TEST_CHECK(!h3.IsValid());

    // Test ScopedKernelTransaction RAII (rollback-on-destruction vs explicit Commit)
    {
        constexpr const wchar_t* kTxnTestSubKey = L"Software\\QiQiIME_TxnUnitTest";
        ::RegDeleteTreeW(HKEY_CURRENT_USER, kTxnTestSubKey);
        const auto cleanup_txn_key = gsl::finally([]() noexcept {
            ::RegDeleteTreeW(HKEY_CURRENT_USER, kTxnTestSubKey);
        });

        const ScopedKernelTransaction invalid_txn(INVALID_HANDLE_VALUE);
        QIQI_TEST_CHECK(!invalid_txn && !invalid_txn.IsValid() && !invalid_txn.IsCommitted());
        QIQI_TEST_CHECK(invalid_txn.Error() == ERROR_INVALID_HANDLE);

        // 1) Uncommitted ScopedKernelTransaction automatically rolls back on scope exit and preserves GetLastError()
        ::SetLastError(ERROR_FILE_NOT_FOUND);
        {
            ScopedKernelTransaction txn;
            QIQI_TEST_CHECK(txn.IsValid() && !txn.IsCommitted() && txn.Error() == ERROR_SUCCESS);
            ScopedKernelTransaction moved_txn(std::move(txn));
            QIQI_TEST_CHECK(!txn.IsValid() && txn.Error() == ERROR_INVALID_HANDLE &&
                            moved_txn.IsValid() && moved_txn.Error() == ERROR_SUCCESS);
            ScopedKernelTransaction assigned_txn(INVALID_HANDLE_VALUE);
            assigned_txn = std::move(moved_txn);
            QIQI_TEST_CHECK(!moved_txn.IsValid() && moved_txn.Error() == ERROR_INVALID_HANDLE &&
                            assigned_txn.IsValid() && assigned_txn.Error() == ERROR_SUCCESS);

            ScopedRegKey key;
            DWORD disp = 0;
            QIQI_TEST_CHECK(::RegCreateKeyTransactedW(
                                HKEY_CURRENT_USER,
                                kTxnTestSubKey,
                                0,
                                nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | KEY_WRITE,
                                nullptr,
                                key.Put(),
                                &disp,
                                assigned_txn.Get(),
                                nullptr) == ERROR_SUCCESS);
            constexpr DWORD val = 123;
            QIQI_TEST_CHECK(::RegSetValueExW(
                                key.Get(),
                                L"TestValue",
                                0,
                                REG_DWORD,
                                reinterpret_cast<const BYTE*>(&val),
                                sizeof(val)) == ERROR_SUCCESS);
            ::SetLastError(ERROR_SHARING_VIOLATION);
        }
        QIQI_TEST_CHECK(::GetLastError() == ERROR_SHARING_VIOLATION);
        ScopedRegKey verify_rolled_back;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
                            HKEY_CURRENT_USER, kTxnTestSubKey, 0, KEY_READ, verify_rolled_back.Put()) !=
                        ERROR_SUCCESS);

        // 2) Committed ScopedKernelTransaction persists changes
        {
            ScopedKernelTransaction txn;
            QIQI_TEST_CHECK(txn.IsValid());
            ScopedRegKey key;
            DWORD disp = 0;
            QIQI_TEST_CHECK(::RegCreateKeyTransactedW(
                                HKEY_CURRENT_USER,
                                kTxnTestSubKey,
                                0,
                                nullptr,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ | KEY_WRITE,
                                nullptr,
                                key.Put(),
                                &disp,
                                txn.Get(),
                                nullptr) == ERROR_SUCCESS);
            constexpr DWORD val = 456;
            QIQI_TEST_CHECK(::RegSetValueExW(
                                key.Get(),
                                L"TestValue",
                                0,
                                REG_DWORD,
                                reinterpret_cast<const BYTE*>(&val),
                                sizeof(val)) == ERROR_SUCCESS);
            key.Reset();
            QIQI_TEST_CHECK(txn.Commit() && txn.IsCommitted() && txn.Error() == ERROR_SUCCESS);
            QIQI_TEST_CHECK(!txn.Commit() && txn.Error() == ERROR_INVALID_STATE);
        }
        ScopedRegKey verify_committed;
        QIQI_TEST_CHECK(::RegOpenKeyExW(
                            HKEY_CURRENT_USER, kTxnTestSubKey, 0, KEY_READ, verify_committed.Put()) ==
                        ERROR_SUCCESS);
    }

    // Test ScopedGlobalLock<wchar_t>, ScopedClipboard, ScopedLastErrorRestore, MakeInt4Variant, and CopyToBstr
    ::SetLastError(ERROR_SUCCESS);
    const ScopedGlobalLock<wchar_t> null_lock(nullptr);
    QIQI_TEST_CHECK(!null_lock && null_lock.Get() == nullptr && null_lock.Error() == ERROR_INVALID_HANDLE);

    const HGLOBAL hmem = ::GlobalAlloc(GMEM_MOVEABLE, 16 * sizeof(wchar_t));
    QIQI_TEST_CHECK(hmem != nullptr);
    if (auto* raw_ptr = static_cast<wchar_t*>(::GlobalLock(hmem))) {
        wcscpy_s(raw_ptr, 16, L"奇奇五笔");
        ::GlobalUnlock(hmem);
    }
    {
        const ScopedGlobalLock<wchar_t> lock(hmem);
        QIQI_TEST_CHECK(lock && lock.Error() == ERROR_SUCCESS && std::wstring(lock.Get()) == L"奇奇五笔");
        ::SetLastError(ERROR_ALREADY_EXISTS);
    }
    QIQI_TEST_CHECK(::GetLastError() == ERROR_ALREADY_EXISTS);
    ::GlobalFree(hmem);

    {
        const ScopedClipboard clip{};
        if (clip.IsOpen()) {
            QIQI_TEST_CHECK(clip.Error() == ERROR_SUCCESS);
        } else {
            QIQI_TEST_CHECK(clip.Error() != ERROR_SUCCESS);
        }
        std::ignore = static_cast<bool>(clip);
        ::SetLastError(ERROR_PATH_NOT_FOUND);
    }
    QIQI_TEST_CHECK(::GetLastError() == ERROR_PATH_NOT_FOUND);

    const VARIANT v42 = MakeInt4Variant(42);
    QIQI_TEST_CHECK(v42.vt == VT_I4 && v42.lVal == 42);

    QIQI_TEST_CHECK(CopyToBstr(L"test", nullptr) == E_INVALIDARG);
    ScopedBstr bstr_out;
    QIQI_TEST_CHECK(SUCCEEDED(CopyToBstr(L"奇奇", bstr_out.Put())) &&
                    bstr_out.IsValid() &&
                    std::wstring(bstr_out.Get()) == L"奇奇");

    // Test CreateUiFont and WriteFileAtomically (including directory error branch)
    {
        const ScopedGdiHandle<HFONT> ui_font = CreateUiFont(16, FW_MEDIUM);
        QIQI_TEST_CHECK(ui_font.IsValid());
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        QIQI_TEST_CHECK(::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0));
        LOGFONTW description{};
        QIQI_TEST_CHECK(::GetObjectW(ui_font.Get(), sizeof(description), &description) == sizeof(description));
        QIQI_TEST_CHECK(std::wcscmp(
            std::data(description.lfFaceName), std::data(metrics.lfMessageFont.lfFaceName)) == 0);
        QIQI_TEST_CHECK(description.lfHeight == -16 && description.lfWeight == FW_MEDIUM);
        QIQI_TEST_CHECK(description.lfQuality == CLEARTYPE_QUALITY);

        const auto atomic_path = std::filesystem::temp_directory_path() / L"qiqiime_atomic_test.bin";
        const std::array<uint8_t, 4> payload{1, 2, 3, 4};
        QIQI_TEST_CHECK(WriteFileAtomically(atomic_path, payload));
        std::string read_back;
        QIQI_TEST_CHECK(ReadFileToUtf8String(atomic_path, read_back) && read_back.size() == 4);
        std::error_code rm_ec;
        std::filesystem::remove(atomic_path, rm_ec);
        QIQI_TEST_CHECK(!WriteFileAtomically(std::filesystem::temp_directory_path(), payload));
    }

    // Test ScopedSaveDC (both null HDC and valid memory HDC state restoration)
    {
        const ScopedSaveDC null_save(nullptr);
        QIQI_TEST_CHECK(!null_save);
    }
    {
        const ScopedMemoryDC mem_dc(::CreateCompatibleDC(nullptr));
        QIQI_TEST_CHECK(mem_dc.IsValid());
        ::SetTextColor(mem_dc.Get(), RGB(12, 34, 56));
        ::SetBkMode(mem_dc.Get(), OPAQUE);
        {
            const ScopedSaveDC save_dc(mem_dc.Get());
            QIQI_TEST_CHECK(static_cast<bool>(save_dc));
            ::SetTextColor(mem_dc.Get(), RGB(200, 100, 50));
            ::SetBkMode(mem_dc.Get(), TRANSPARENT);
            QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(200, 100, 50));
            QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == TRANSPARENT);
        }
        QIQI_TEST_CHECK(::GetTextColor(mem_dc.Get()) == RGB(12, 34, 56));
        QIQI_TEST_CHECK(::GetBkMode(mem_dc.Get()) == OPAQUE);
    }

        // Test BindOrGetWindowUserData, ClientPointFromLParam, and PaintWindowClient on a temporary window
    {
        const ScopedWindow test_wnd(::CreateWindowExW(
            0, L"STATIC", L"", WS_POPUP, 100, 200, 50, 40, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        QIQI_TEST_CHECK(test_wnd.IsValid());
        int payload = 12345;
        CREATESTRUCTW cs{};
        cs.lpCreateParams = &payload;
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_NCCREATE, 0) == nullptr);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(
                            test_wnd.Get(), WM_NCCREATE, reinterpret_cast<LPARAM>(&cs)) == &payload);
        QIQI_TEST_CHECK(BindOrGetWindowUserData<int>(test_wnd.Get(), WM_PAINT, 0) == &payload);

        const POINT client_pt = ClientPointFromLParam(test_wnd.Get(), MAKELPARAM(15, 25), false);
        QIQI_TEST_CHECK(client_pt.x == 15 && client_pt.y == 25);
        const POINT nc_null_pt = ClientPointFromLParam(nullptr, MAKELPARAM(115, 225), true);
        QIQI_TEST_CHECK(nc_null_pt.x == 115 && nc_null_pt.y == 225);
        POINT screen_pt{10, 20};
        ::ClientToScreen(test_wnd.Get(), &screen_pt);
        const POINT nc_converted = ClientPointFromLParam(
            test_wnd.Get(), MAKELPARAM(screen_pt.x, screen_pt.y), true);
        QIQI_TEST_CHECK(nc_converted.x == 10 && nc_converted.y == 20);

        bool painted = false;
        QIQI_TEST_CHECK(PaintWindowClient(test_wnd.Get(), [&painted](HDC hdc, const RECT& rc) {
            painted = (hdc != nullptr && rc.right == 50 && rc.bottom == 40);
        }) == 0);
        QIQI_TEST_CHECK(painted);
    }
}

} // namespace qiqi::test::common_cases
