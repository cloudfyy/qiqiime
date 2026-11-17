// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cleanup.h"
#include "internal.h"
#include "common/RAII.h"
#include "common/test_support.h"

#include <aclapi.h>
#include <objbase.h>
#include <sddl.h>
#include <winioctl.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace qiqi::test {
namespace {

namespace backend = installer::detail;

std::wstring Guid() {
    GUID value{};
    QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&value)));
    std::array<wchar_t, 39> text{};
    QIQI_TEST_CHECK(::StringFromGUID2(value, text.data(), gsl::narrow_cast<int>(text.size())) != 0);
    return text.data();
}

std::wstring UserSid() {
    ScopedKernelHandle token;
    QIQI_TEST_CHECK(::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put()));
    DWORD size = 0;
    QIQI_TEST_CHECK(!::GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &size));
    QIQI_TEST_CHECK(::GetLastError() == ERROR_INSUFFICIENT_BUFFER);
    std::vector<BYTE> buffer(size);
    QIQI_TEST_CHECK(::GetTokenInformation(token.Get(), TokenUser, buffer.data(), size, &size));
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR text = nullptr;
    QIQI_TEST_CHECK(::ConvertSidToStringSidW(user->User.Sid, &text));
    const ScopedLocalAlloc release(text);
    return text;
}

void Text(HKEY key, const wchar_t* name, const std::wstring& value) {
    QIQI_TEST_CHECK(::RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        gsl::narrow_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
}

void Write(const std::filesystem::path& path) {
    QIQI_TEST_CHECK(std::filesystem::create_directories(path.parent_path()) ||
        std::filesystem::is_directory(path.parent_path()));
    const ScopedFileHandle file(::CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    QIQI_TEST_CHECK(file);
    constexpr std::string_view data = "synthetic";
    DWORD written = 0;
    QIQI_TEST_CHECK(::WriteFile(file.Get(), data.data(), gsl::narrow_cast<DWORD>(data.size()), &written, nullptr));
    QIQI_TEST_CHECK(written == data.size());
}

void Junction(const std::filesystem::path& link, const std::filesystem::path& destination) {
    QIQI_TEST_CHECK(std::filesystem::create_directory(link));
    const ScopedFileHandle handle(::CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    QIQI_TEST_CHECK(handle);
    struct MountPoint {
        DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
        WORD data_length = 0;
        WORD reserved = 0;
        WORD substitute_offset = 0;
        WORD substitute_length = 0;
        WORD print_offset = 0;
        WORD print_length = 0;
        std::array<wchar_t, 2048> names{};
    } buffer;
    const std::wstring substitute = L"\\??\\" + destination.wstring();
    QIQI_TEST_CHECK(substitute.size() + 1 < buffer.names.size());
    std::copy(substitute.begin(), substitute.end(), buffer.names.begin());
    buffer.substitute_length = gsl::narrow<WORD>(substitute.size() * sizeof(wchar_t));
    buffer.print_offset = gsl::narrow<WORD>(buffer.substitute_length + sizeof(wchar_t));
    buffer.data_length = gsl::narrow<WORD>(gsl::narrow_cast<size_t>(buffer.print_offset) + 8 + sizeof(wchar_t));
    DWORD returned = 0;
    QIQI_TEST_CHECK(::DeviceIoControl(handle.Get(), FSCTL_SET_REPARSE_POINT, &buffer,
        gsl::narrow_cast<DWORD>(buffer.data_length) + 8, nullptr, 0, &returned, nullptr));
}

struct Fixture {
    std::wstring id;
    std::wstring sid = UserSid();
    std::wstring subkey = L"Software\\QiQiIMEInstallerTests\\" + id;
    // Keep every disposable file under the test runner's working directory.
    std::filesystem::path base = std::filesystem::current_path() / (L"qiqi-installer-cleanup-" + id);
    std::filesystem::path profile = base / L"Profile";
    std::filesystem::path data = profile / L"AppData" / L"Local" / L"QiQiIME";
    ScopedRegKey machine;
    ScopedRegKey users;
    backend::CleanupSandbox sandbox;
    std::vector<std::filesystem::path> scheduled;
    HRESULT scheduler_error = S_OK;
    HRESULT persistence_error = S_OK;

    explicit Fixture(std::wstring identifier) : id(std::move(identifier)) {
        QIQI_TEST_CHECK(std::filesystem::create_directories(data / L"cache"));
        QIQI_TEST_CHECK(::RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, machine.Put(), nullptr) == ERROR_SUCCESS);
        QIQI_TEST_CHECK(::RegCreateKeyExW(machine.Get(), L"Users", 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, users.Put(), nullptr) == ERROR_SUCCESS);
        SetProfile(profile.wstring());
        ScopedRegKey settings;
        QIQI_TEST_CHECK(::RegCreateKeyExW(users.Get(), (sid + L"\\Software\\QiQiIME\\Nested").c_str(),
            0, nullptr, 0, KEY_ALL_ACCESS, nullptr, settings.Put(), nullptr) == ERROR_SUCCESS);
        Text(settings.Get(), L"Synthetic", L"keep unless configuration selected");
        sandbox.machine = machine.Get();
        sandbox.users = users.Get();
        sandbox.quarantine_root = base / L"Quarantine";
        sandbox.boot_session = Guid();
        sandbox.schedule = [this](const std::filesystem::path& path) {
            scheduled.push_back(path);
            return scheduler_error;
        };
        sandbox.before_pending_persist = [this]() { return persistence_error; };
    }

    ~Fixture() noexcept {
        users.Reset();
        machine.Reset();
        const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        QIQI_TEST_CHECK_CLEANUP(status == ERROR_SUCCESS);
        try {
            std::error_code error;
            std::filesystem::remove_all(base, error);
            QIQI_TEST_CHECK_CLEANUP(!error);
        } catch (...) {
            QIQI_TEST_CHECK_CLEANUP(false && "Removing the private cleanup fixture threw");
        }
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    Fixture(Fixture&&) = delete;
    Fixture& operator=(Fixture&&) = delete;

    void SetProfile(const std::wstring& path) {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegCreateKeyExW(machine.Get(), (L"ProfileList\\" + sid).c_str(),
            0, nullptr, 0, KEY_ALL_ACCESS, nullptr, key.Put(), nullptr) == ERROR_SUCCESS);
        Text(key.Get(), L"ProfileImagePath", path);
    }

    installer::CleanupRequest Request(bool configuration, bool dictionaries) const {
        return {sid, Guid(), configuration, dictionaries};
    }

    bool HasConfiguration() const {
        ScopedRegKey key;
        const auto status = ::RegOpenKeyExW(users.Get(), (sid + L"\\Software\\QiQiIME").c_str(),
            0, KEY_READ, key.Put());
        QIQI_TEST_CHECK(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND);
        return status == ERROR_SUCCESS;
    }

    void Pending(installer::PendingState expected) const {
        installer::PendingState state = installer::PendingState::incomplete;
        QIQI_TEST_CHECK(backend::CheckPendingCleanup(state, sandbox) == S_OK);
        QIQI_TEST_CHECK(state == expected);
    }

    void Stored(const installer::CleanupRequest& request, const installer::CleanupResult& expected) const {
        installer::CleanupResult result;
        QIQI_TEST_CHECK(backend::ReadCleanupResult(request, result, sandbox) == S_OK);
        QIQI_TEST_CHECK(result.error == expected.error && result.deleted == expected.deleted &&
            result.scheduled == expected.scheduled && result.restart_required == expected.restart_required &&
            result.operation == expected.operation);
    }
};

struct Mapping {
    ScopedKernelHandle section;
    ScopedMappedView view;

    explicit Mapping(const std::filesystem::path& path) {
        Write(path);
        const ScopedFileHandle file(::CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(file);
        section.Reset(::CreateFileMappingW(file.Get(), nullptr, PAGE_READONLY, 0, 0, nullptr));
        QIQI_TEST_CHECK(section);
        view.Reset(::MapViewOfFile(section.Get(), FILE_MAP_READ, 0, 0, 0));
        QIQI_TEST_CHECK(view);
    }

    void Close() noexcept {
        view.Reset();
        section.Reset();
    }
};

void OptionCombinations() {
    constexpr std::array removable{
        L"user_dict.txt", L"user_dict.qdict", L"phrase_freq.qfreq",
        L"user_dict.txt.sync\\snapshot.txt", L"user_dict.txt.sync\\source.meta",
        L"phrase_freq.qfreq.sync\\snapshot.qfreq", L"phrase_freq.qfreq.sync\\journal.bin",
        L"cache\\wubi86_jidian.qdict", L"cache\\my_dictionary-0123456789ABCDEF.qdict"};
    constexpr std::array retained{
        L"custom.txt", L"custom.qdict", L"export.txt", L"backup.qbak",
        L"user_dict.txt.sync\\backup.qbak", L"phrase_freq.qfreq.sync\\custom.txt",
        L"cache\\custom.qdict", L"cache\\wubi86_jidian.txt",
        // A distinct stem avoids aliasing the uppercase-hash fixture on NTFS.
        L"cache\\lowercase_dictionary-0123456789abcdef.qdict",
        L"cache\\my_dictionary-0123456789ABCDEF.qdict.bak",
        L"cache\\nested\\wubi86_jidian.qdict"};
    for (const bool configuration : {false, true}) {
        for (const bool dictionaries : {false, true}) {
            Fixture fixture{Guid()};
            for (const auto* name : removable) Write(fixture.data / name);
            for (const auto* name : retained) Write(fixture.data / name);
            const auto outside = fixture.base / L"custom-source" / L"user_dict.txt";
            Write(outside);
            const auto request = fixture.Request(configuration, dictionaries);
            const auto result = backend::PerformCleanup(request, fixture.sandbox);
            QIQI_TEST_CHECK(result.error == S_OK && result.deleted == (dictionaries ? removable.size() : 0));
            QIQI_TEST_CHECK(result.scheduled == 0 && !result.restart_required && fixture.scheduled.empty());
            QIQI_TEST_CHECK(fixture.HasConfiguration() == !configuration);
            for (const auto* name : removable) {
                QIQI_TEST_CHECK(std::filesystem::exists(fixture.data / name) == !dictionaries);
            }
            for (const auto* name : retained) QIQI_TEST_CHECK(std::filesystem::exists(fixture.data / name));
            QIQI_TEST_CHECK(std::filesystem::exists(outside));
            fixture.Stored(request, result);
            fixture.Pending(installer::PendingState::clear);
            const auto repeated = backend::PerformCleanup(request, fixture.sandbox);
            QIQI_TEST_CHECK(repeated.error == result.error && repeated.deleted == result.deleted);
        }
    }
}

void MappedCacheAndBoots() {
    Fixture fixture{Guid()};
    const auto original = fixture.data / L"cache" / L"wubi86_jidian-0123456789ABCDEF.qdict";
    Mapping mapping(original);
    const auto request = fixture.Request(false, true);
    const auto result = backend::PerformCleanup(request, fixture.sandbox);
    QIQI_TEST_CHECK(result.error == S_OK && result.deleted == 0 && result.scheduled == 1 && result.restart_required);
    QIQI_TEST_CHECK(fixture.scheduled.size() == 1 && !std::filesystem::exists(original));
    const auto isolated = fixture.scheduled.front();
    QIQI_TEST_CHECK(isolated.parent_path() == fixture.sandbox.quarantine_root / request.transaction &&
        isolated.extension() == L".pending" && isolated.filename().wstring().size() == 46 &&
        isolated.filename().wstring().find(L"wubi86") == std::wstring::npos && isolated != original);
    QIQI_TEST_CHECK(std::filesystem::exists(isolated));
    fixture.Stored(request, result);
    fixture.Pending(installer::PendingState::restart_required);
    Write(original);
    mapping.Close();
    fixture.sandbox.boot_session = Guid();
    fixture.Pending(installer::PendingState::incomplete);
    QIQI_TEST_CHECK(std::filesystem::remove(isolated));
    fixture.Pending(installer::PendingState::clear);
    QIQI_TEST_CHECK(std::filesystem::exists(original));
    fixture.Stored(request, result);
}

void BootOnlyRestart() {
    Fixture fixture{Guid()};
    const auto request = fixture.Request(false, false);
    const auto result = backend::PerformCleanup(request, fixture.sandbox, true);
    QIQI_TEST_CHECK(result.error == S_OK && result.deleted == 0 && result.scheduled == 0 && result.restart_required);
    QIQI_TEST_CHECK(fixture.scheduled.empty() && fixture.HasConfiguration());
    fixture.Stored(request, result);
    fixture.Pending(installer::PendingState::restart_required);
    fixture.sandbox.boot_session = Guid();
    fixture.Pending(installer::PendingState::clear);
    fixture.Stored(request, result);
}

void SchedulerAndPersistenceFailures() {
    for (const bool fail_persistence : {false, true}) {
        Fixture fixture{Guid()};
        const auto original = fixture.data / L"cache" / L"wubi86_haifeng.qdict";
        Mapping mapping(original);
        const HRESULT failure = HRESULT_FROM_WIN32(fail_persistence ? ERROR_DISK_FULL : ERROR_PRIVILEGE_NOT_HELD);
        if (fail_persistence) fixture.persistence_error = failure;
        else fixture.scheduler_error = failure;
        const auto request = fixture.Request(false, true);
        const auto result = backend::PerformCleanup(request, fixture.sandbox);
        QIQI_TEST_CHECK(result.error == failure && result.scheduled == 0 && !result.restart_required);
        QIQI_TEST_CHECK(result.operation == (fail_persistence
            ? installer::CleanupOperation::persist_pending_target
            : installer::CleanupOperation::schedule_reboot_delete));
        fixture.Stored(request, result);
        if (fail_persistence) {
            QIQI_TEST_CHECK(fixture.scheduled.empty() && std::filesystem::exists(original));
            fixture.Pending(installer::PendingState::clear);
        } else {
            QIQI_TEST_CHECK(fixture.scheduled.size() == 1 && !std::filesystem::exists(original));
            QIQI_TEST_CHECK(std::filesystem::exists(fixture.scheduled.front()));
            fixture.Pending(installer::PendingState::incomplete);
            mapping.Close();
            QIQI_TEST_CHECK(std::filesystem::remove(fixture.scheduled.front()));
            fixture.Pending(installer::PendingState::clear);
        }
    }
}

void OperationDiagnostics() {
    Fixture fixture{Guid()};
    const auto outside = fixture.base / L"outside.txt";
    Write(outside);
    const auto linked = fixture.data / L"user_dict.txt";
    QIQI_TEST_CHECK(::CreateHardLinkW(linked.c_str(), outside.c_str(), nullptr));
    const auto cache = fixture.data / L"cache" / L"wubi86_jidian.qdict";
    Mapping mapping(cache);
    fixture.scheduler_error = HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD);
    const auto request = fixture.Request(false, true);
    const auto result = backend::PerformCleanup(request, fixture.sandbox);
    // A later failure must not replace the first HRESULT or its operation.
    QIQI_TEST_CHECK(result.error == E_ACCESSDENIED &&
        result.operation == installer::CleanupOperation::verify_source_file);
    QIQI_TEST_CHECK(result.scheduled == 0 && fixture.scheduled.size() == 1 &&
        std::filesystem::exists(linked) && !std::filesystem::exists(cache));
    fixture.Stored(request, result);
    const auto repeated = backend::PerformCleanup(request, fixture.sandbox);
    QIQI_TEST_CHECK(repeated.error == result.error && repeated.operation == result.operation &&
        fixture.scheduled.size() == 1);

    ScopedRegKey job;
    QIQI_TEST_CHECK(::RegOpenKeyExW(fixture.machine.Get(),
        (L"State\\Jobs\\" + request.transaction).c_str(), 0, KEY_ALL_ACCESS, job.Put()) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(::RegDeleteValueW(job.Get(), L"Operation") == ERROR_SUCCESS);
    installer::CleanupResult legacy;
    QIQI_TEST_CHECK(backend::ReadCleanupResult(request, legacy, fixture.sandbox) == S_OK);
    QIQI_TEST_CHECK(legacy.error == result.error && legacy.operation == installer::CleanupOperation::unknown);
    Text(job.Get(), L"Operation", L"not a number");
    QIQI_TEST_CHECK(backend::ReadCleanupResult(request, legacy, fixture.sandbox) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    constexpr DWORD invalid = MAXDWORD;
    QIQI_TEST_CHECK(::RegSetValueExW(job.Get(), L"Operation", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&invalid), sizeof(invalid)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(backend::ReadCleanupResult(request, legacy, fixture.sandbox) ==
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
}

void QuarantineCreationDenied() {
    Fixture fixture{Guid()};
    const auto original = fixture.data / L"cache" / L"wubi86_jidian.qdict";
    Mapping mapping(original);
    auto directory = fixture.base.wstring();
    PACL previous = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    QIQI_TEST_CHECK(::GetNamedSecurityInfoW(directory.data(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &previous, nullptr, &descriptor) == ERROR_SUCCESS);
    const ScopedLocalAlloc release_descriptor(descriptor);
    PSID sid = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSidToSidW(fixture.sid.c_str(), &sid));
    const ScopedLocalAlloc release_sid(sid);
    EXPLICIT_ACCESSW denied{};
    denied.grfAccessPermissions = FILE_ADD_SUBDIRECTORY;
    denied.grfAccessMode = DENY_ACCESS;
    denied.grfInheritance = NO_INHERITANCE;
    denied.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    denied.Trustee.TrusteeType = TRUSTEE_IS_USER;
    denied.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    PACL restricted = nullptr;
    QIQI_TEST_CHECK(::SetEntriesInAclW(1, &denied, previous, &restricted) == ERROR_SUCCESS);
    const ScopedLocalAlloc release_acl(restricted);
    QIQI_TEST_CHECK(::SetNamedSecurityInfoW(directory.data(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, restricted, nullptr) == ERROR_SUCCESS);
    const auto restore = gsl::finally([&]() noexcept {
        QIQI_TEST_CHECK_CLEANUP(::SetNamedSecurityInfoW(directory.data(), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, previous, nullptr) == ERROR_SUCCESS);
    });

    const auto request = fixture.Request(false, true);
    const auto result = backend::PerformCleanup(request, fixture.sandbox);
    QIQI_TEST_CHECK(result.error == E_ACCESSDENIED &&
        result.operation == installer::CleanupOperation::create_quarantine_directory);
    QIQI_TEST_CHECK(result.scheduled == 0 && !result.restart_required && fixture.scheduled.empty() &&
        std::filesystem::exists(original) && !std::filesystem::exists(fixture.sandbox.quarantine_root));
    fixture.Stored(request, result);
    fixture.Pending(installer::PendingState::clear);
}

void ProductionQuarantineSecurity() {
    constexpr std::wstring_view machine =
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)";
    const auto validate = [](const std::wstring& sddl, bool volume_root, HRESULT expected) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
        const ScopedLocalAlloc release(descriptor);
        QIQI_TEST_CHECK(backend::ValidateQuarantineSecurityDescriptor(descriptor, volume_root) == expected);
    };
    validate(std::wstring(machine) + L"(A;CI;0x001200A0;;;BU)", false, S_OK);
    validate(std::wstring(machine) + L"(A;CI;0x001200A1;;;BU)", false, E_ACCESSDENIED);
    validate(L"O:BAG:BAD:(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", false, E_ACCESSDENIED);
    validate(std::wstring(machine) + L"(A;;0x4;;;AU)(A;OICIIO;FA;;;AU)", true, S_OK);
    validate(std::wstring(machine) + L"(A;;0x40;;;AU)", true, E_ACCESSDENIED);
    validate(L"O:S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464"
        L"D:PAI(A;;LC;;;AU)(A;OICIIO;SDGXGWGR;;;AU)(A;OICI;FA;;;SY)"
        L"(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)"
        L"(A;;0x1000a1;;;S-1-15-3-1)", true, S_OK);

    Fixture fixture{Guid()};
    const auto path = fixture.base / L"UntrustedQuarantine";
    const std::wstring security = L"O:" + fixture.sid + L"D:P(A;;FA;;;" + fixture.sid + L")";
    PSECURITY_DESCRIPTOR supplied = nullptr;
    QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
        security.c_str(), SDDL_REVISION_1, &supplied, nullptr));
    const ScopedLocalAlloc release_supplied(supplied);
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), supplied, FALSE};
    QIQI_TEST_CHECK(::CreateDirectoryW(path.c_str(), &attributes));
    const ScopedFileHandle directory(::CreateFileW(path.c_str(), READ_CONTROL, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    QIQI_TEST_CHECK(directory);
    PSECURITY_DESCRIPTOR actual = nullptr;
    QIQI_TEST_CHECK(::GetSecurityInfo(directory.Get(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &actual) == ERROR_SUCCESS);
    const ScopedLocalAlloc release_actual(actual);
    QIQI_TEST_CHECK(backend::ValidateQuarantineSecurityDescriptor(actual, false) == E_ACCESSDENIED);
}

void UnsafeFilesAndPaths() {
    {
        Fixture fixture{Guid()};
        const auto file = fixture.data / L"user_dict.txt";
        Write(file);
        QIQI_TEST_CHECK(::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_READONLY));
        const auto request = fixture.Request(false, true);
        const auto result = backend::PerformCleanup(request, fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && result.scheduled == 0 && fixture.scheduled.empty());
        QIQI_TEST_CHECK(std::filesystem::exists(file));
        fixture.Stored(request, result);
        fixture.Pending(installer::PendingState::clear);
        QIQI_TEST_CHECK(::SetFileAttributesW(file.c_str(), FILE_ATTRIBUTE_NORMAL));
    }
    {
        Fixture fixture{Guid()};
        const auto file = fixture.data / L"user_dict.txt";
        Write(file);
        const ScopedFileHandle locked(::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        const auto result = backend::PerformCleanup(fixture.Request(false, true), fixture.sandbox);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(fixture.scheduled.empty() && std::filesystem::exists(file));
    }
    {
        Fixture fixture{Guid()};
        const auto outside = fixture.base / L"outside";
        Write(outside / L"snapshot.txt");
        const auto link = fixture.data / L"user_dict.txt.sync";
        Junction(link, outside);
        const auto result = backend::PerformCleanup(fixture.Request(false, true), fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && fixture.scheduled.empty());
        QIQI_TEST_CHECK(std::filesystem::exists(outside / L"snapshot.txt"));
        QIQI_TEST_CHECK(::RemoveDirectoryW(link.c_str()));
    }
    {
        Fixture fixture{Guid()};
        const auto link = fixture.base / L"linked-profile";
        Junction(link, fixture.profile);
        Write(fixture.data / L"user_dict.txt");
        fixture.SetProfile(link.wstring());
        const auto result = backend::PerformCleanup(fixture.Request(false, true), fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && fixture.scheduled.empty());
        QIQI_TEST_CHECK(std::filesystem::exists(fixture.data / L"user_dict.txt"));
        QIQI_TEST_CHECK(::RemoveDirectoryW(link.c_str()));
    }
    {
        Fixture fixture{Guid()};
        const auto outside = fixture.base / L"outside.txt";
        Write(outside);
        QIQI_TEST_CHECK(::CreateHardLinkW((fixture.data / L"user_dict.txt").c_str(), outside.c_str(), nullptr));
        const auto result = backend::PerformCleanup(fixture.Request(false, true), fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && std::filesystem::exists(outside));
    }
    for (const auto* profile : {L"relative\\profile", L"\\\\server\\share\\profile",
                                L"C:\\profile\\..\\elsewhere", L"C:\\profile:stream",
                                L"C:\\profile.\\child", L"%USERPROFILE%"}) {
        Fixture fixture{Guid()};
        fixture.SetProfile(profile);
        const auto result = backend::PerformCleanup(fixture.Request(false, true), fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_INVALIDARG && fixture.scheduled.empty());
    }
}

void TamperedStateAndReadErrors() {
    Fixture fixture{Guid()};
    const auto request = fixture.Request(false, true);
    Mapping mapping(fixture.data / L"cache" / L"wubi86_jidian.qdict");
    const auto result = backend::PerformCleanup(request, fixture.sandbox);
    QIQI_TEST_CHECK(result.error == S_OK && result.scheduled == 1);
    ScopedRegKey targets;
    QIQI_TEST_CHECK(::RegOpenKeyExW(fixture.machine.Get(),
        (L"State\\Jobs\\" + request.transaction + L"\\Targets").c_str(), 0,
        KEY_ALL_ACCESS, targets.Put()) == ERROR_SUCCESS);
    std::array<wchar_t, 256> name{};
    DWORD size = gsl::narrow_cast<DWORD>(name.size());
    QIQI_TEST_CHECK(::RegEnumKeyExW(targets.Get(), 0, name.data(), &size, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
    ScopedRegKey target;
    QIQI_TEST_CHECK(::RegOpenKeyExW(targets.Get(), name.data(), 0, KEY_ALL_ACCESS, target.Put()) == ERROR_SUCCESS);
    Text(target.Get(), L"Path", (fixture.base / L"outside" / (std::wstring(name.data()) + L".pending")).wstring());
    installer::PendingState state = installer::PendingState::clear;
    QIQI_TEST_CHECK(backend::CheckPendingCleanup(state, fixture.sandbox) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    QIQI_TEST_CHECK(state == installer::PendingState::incomplete);
    Text(target.Get(), L"Path", fixture.scheduled.front().wstring());
    Text(target.Get(), L"VolumeName", L"\\\\?\\Volume" + Guid() + L"\\");
    QIQI_TEST_CHECK(backend::CheckPendingCleanup(state, fixture.sandbox) == E_ACCESSDENIED);
    QIQI_TEST_CHECK(state == installer::PendingState::incomplete);
    ScopedRegKey job;
    QIQI_TEST_CHECK(::RegOpenKeyExW(fixture.machine.Get(),
        (L"State\\Jobs\\" + request.transaction).c_str(), 0, KEY_ALL_ACCESS, job.Put()) == ERROR_SUCCESS);
    Text(job.Get(), L"Deleted", L"not a number");
    installer::CleanupResult stored;
    QIQI_TEST_CHECK(backend::ReadCleanupResult(request, stored, fixture.sandbox) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    auto other = request;
    other.sid = L"S-1-5-21-1-2-3-1001";
    QIQI_TEST_CHECK(backend::ReadCleanupResult(other, stored, fixture.sandbox) == E_ACCESSDENIED);
}

void UnsafeQuarantineParents() {
    for (const bool replace_transaction : {false, true}) {
        Fixture fixture{Guid()};
        const auto request = fixture.Request(false, true);
        const auto original = fixture.data / L"cache" / L"wubi86_jidian.qdict";
        Mapping mapping(original);
        const auto outside = fixture.base / L"foreign";
        Write(outside / L"keep.txt");
        auto link = fixture.sandbox.quarantine_root;
        if (replace_transaction) {
            QIQI_TEST_CHECK(std::filesystem::create_directory(link));
            link /= request.transaction;
        }
        Junction(link, outside);
        const auto result = backend::PerformCleanup(request, fixture.sandbox);
        QIQI_TEST_CHECK(result.error == E_ACCESSDENIED && result.scheduled == 0 && !result.restart_required);
        QIQI_TEST_CHECK(result.operation == installer::CleanupOperation::verify_quarantine_directory);
        QIQI_TEST_CHECK(fixture.scheduled.empty() && std::filesystem::exists(original));
        QIQI_TEST_CHECK(std::filesystem::exists(outside / L"keep.txt"));
        fixture.Stored(request, result);
        fixture.Pending(installer::PendingState::clear);
        QIQI_TEST_CHECK(::RemoveDirectoryW(link.c_str()));
    }
    {
        Fixture fixture{Guid()};
        const auto request = fixture.Request(false, true);
        Mapping mapping(fixture.data / L"cache" / L"wubi86_jidian.qdict");
        const auto result = backend::PerformCleanup(request, fixture.sandbox);
        QIQI_TEST_CHECK(result.error == S_OK && fixture.scheduled.size() == 1);
        const auto isolated = fixture.scheduled.front();
        mapping.Close();
        QIQI_TEST_CHECK(std::filesystem::remove(isolated));
        QIQI_TEST_CHECK(::RemoveDirectoryW(isolated.parent_path().c_str()));
        const auto outside = fixture.base / L"foreign";
        Write(outside / isolated.filename());
        Junction(isolated.parent_path(), outside);
        installer::PendingState state = installer::PendingState::clear;
        QIQI_TEST_CHECK(backend::CheckPendingCleanup(state, fixture.sandbox) == E_ACCESSDENIED);
        QIQI_TEST_CHECK(state == installer::PendingState::incomplete);
        QIQI_TEST_CHECK(std::filesystem::exists(outside / isolated.filename()));
        QIQI_TEST_CHECK(::RemoveDirectoryW(isolated.parent_path().c_str()));
    }
}

} // namespace

void RunInstallerCleanupTestsImpl() {
    Fixture validation{Guid()};
    auto request = validation.Request(false, false);
    QIQI_TEST_CHECK(installer::ValidateRequest(request) == S_OK);
    for (const auto* sid : {L"", L"not-a-sid", L"S-1-5-18", L"S-1-5-19", L"S-1-5-20"}) {
        request.sid = sid;
        QIQI_TEST_CHECK(FAILED(installer::ValidateRequest(request)));
    }
    request.sid = validation.sid;
    for (const auto* transaction : {L"", L"not-a-guid", L"{00000000-0000-0000-0000-000000000000}"}) {
        request.transaction = transaction;
        QIQI_TEST_CHECK(FAILED(installer::ValidateRequest(request)));
    }
    validation.Pending(installer::PendingState::clear);
    installer::CleanupResult absent;
    QIQI_TEST_CHECK(backend::ReadCleanupResult(validation.Request(false, false), absent,
        validation.sandbox) == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    OptionCombinations();
    MappedCacheAndBoots();
    BootOnlyRestart();
    SchedulerAndPersistenceFailures();
    OperationDiagnostics();
    QuarantineCreationDenied();
    ProductionQuarantineSecurity();
    UnsafeFilesAndPaths();
    UnsafeQuarantineParents();
    TamperedStateAndReadErrors();
}

} // namespace qiqi::test

void RunInstallerCleanupTests() {
    qiqi::test::RunInstallerCleanupTestsImpl();
}
