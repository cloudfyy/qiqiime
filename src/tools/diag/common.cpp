// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"
#include "common/fileutil.h"
#include <gsl/gsl>
#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <system_error>

namespace qiqi::diag::detail {

std::string HResultText(HRESULT result) {
    return std::format("0x{:08X}", std::bit_cast<uint32_t>(result));
}

std::string Win32Message(HRESULT result) {
    DWORD code = std::bit_cast<uint32_t>(result);
    if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
        code = HRESULT_CODE(result);
    }
    std::array<wchar_t, 512> buffer{};
    const DWORD length = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                                          code, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), buffer.data(),
                                          gsl::narrow_cast<DWORD>(buffer.size()), nullptr);
    if (length == 0)
        return "Unknown Windows error";
    std::wstring_view message(buffer.data(), length);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.remove_suffix(1);
    }
    return WideToUtf8(message);
}

Json CheckResult(std::string_view name, HRESULT result, std::string_view status) {
    const bool success = SUCCEEDED(result);
    Json output{
        {"name", name},
        {"status", status.empty() ? (success ? "ok" : "error") : status},
        {"hresult", HResultText(result)},
    };
    if (FAILED(result)) {
        output["message"] = Win32Message(result);
        if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
            output["win32_error"] = HRESULT_CODE(result);
        }
    }
    return output;
}

void AddCheck(Json& stage, const Json& check) {
    stage["checks"].push_back(check);
}

void FinalizeStage(Json& stage) {
    if (stage["status"] == "skipped") {
        return;
    }
    bool has_error = false;
    bool has_warning = false;
    for (const auto& check : stage["checks"]) {
        const auto& status = check.value("status", "ok");
        if (status == "error") {
            has_error = true;
        } else if (status == "warning") {
            has_warning = true;
        }
    }
    stage["status"] = has_error ? "error" : (has_warning ? "warning" : "ok");
}

Json SkippedStage(std::string_view name, std::string_view label) {
    return Json{
        {"id", name},
        {"label", label},
        {"status", "skipped"},
        {"reason", "not approved by the user"},
        {"checks", Json::array()},
    };
}

Json NewStage(std::string_view name, std::string_view label) {
    return Json{
        {"id", name},
        {"label", label},
        {"status", "collecting"},
        {"checks", Json::array()},
    };
}

std::wstring GuidString(REFGUID value) {
    std::array<wchar_t, 64> buffer{};
    const int length = ::StringFromGUID2(value, buffer.data(), gsl::narrow_cast<int>(buffer.size()));
    if (length == 0) {
        throw std::system_error(E_UNEXPECTED, std::system_category(), "StringFromGUID2");
    }
    return std::wstring(buffer.data(), gsl::narrow_cast<size_t>(length - 1));
}

std::string UtcTimestamp() {
    SYSTEMTIME time{};
    ::GetSystemTime(&time);
    return std::format("{:04d}-{:02d}-{:02d}T"
                       "{:02d}:{:02d}:{:02d}Z",
                       time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD required = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0)
        return {};
    std::wstring output(required, L'\0');
    const DWORD length = ::GetEnvironmentVariableW(name, output.data(), required);
    if (length == 0 || length >= required) {
        return {};
    }
    output.resize(length);
    return output;
}

bool StartsWithPathRoot(std::wstring_view value, std::wstring_view root) noexcept {
    if (value.size() < root.size()) {
        return false;
    }
    if (::_wcsnicmp(value.data(), root.data(), root.size()) != 0) {
        return false;
    }
    return value.size() == root.size() || value[root.size()] == L'\\' || value[root.size()] == L'/';
}

std::wstring RedactOnePathRoot(std::wstring path, const wchar_t* variable, std::wstring_view token) {
    const std::wstring root = EnvironmentValue(variable);
    if (root.empty() || !StartsWithPathRoot(path, root)) {
        return path;
    }
    return std::wstring(token) +
           std::wstring(path.begin() + gsl::narrow_cast<ptrdiff_t>(root.size()), path.end());
}

HRESULT OpenRegistryKeyRead(HKEY root, std::wstring_view path, REGSAM access, ScopedRegKey& output) {
    const std::wstring native(path);
    HKEY raw = nullptr;
    const LSTATUS status = ::RegOpenKeyExW(root, native.c_str(), 0, access, &raw);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    output.Reset(raw);
    return S_OK;
}

HRESULT ReadRegistryValue(HKEY key, const wchar_t* name, DWORD& type, std::vector<uint8_t>& output) {
    DWORD size = 0;
    LSTATUS status = ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    std::vector<uint8_t> bytes(size);
    status = ::RegQueryValueExW(key, name, nullptr, &type, bytes.data(), &size);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    bytes.resize(size);
    output = std::move(bytes);
    return S_OK;
}

HRESULT ReadRegistryString(HKEY key, const wchar_t* name, std::wstring& output) {
    DWORD type = 0;
    std::vector<uint8_t> bytes;
    const HRESULT result = ReadRegistryValue(key, name, type, bytes);
    if (FAILED(result))
        return result;
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    if (bytes.size() % sizeof(wchar_t) != 0) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    std::wstring value(bytes.size() / sizeof(wchar_t), L'\0');
    if (!bytes.empty()) {
        std::memcpy(value.data(), bytes.data(), bytes.size());
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    output = std::move(value);
    return S_OK;
}

HRESULT ReadRegistryDword(HKEY key, const wchar_t* name, DWORD& output) noexcept {
    DWORD type = 0;
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = ::RegQueryValueExW(key, name, nullptr, &type, std::bit_cast<BYTE*>(&value), &size);
    if (status != ERROR_SUCCESS) {
        return HRESULT_FROM_WIN32(status);
    }
    if (type != REG_DWORD || size != sizeof(value)) {
        return HRESULT_FROM_WIN32(ERROR_UNSUPPORTED_TYPE);
    }
    output = value;
    return S_OK;
}

std::string ArchitectureName(USHORT architecture) noexcept {
    switch (architecture) {
    case IMAGE_FILE_MACHINE_AMD64:
        return "x64";
    case IMAGE_FILE_MACHINE_I386:
        return "x86";
    case IMAGE_FILE_MACHINE_ARM64:
        return "arm64";
    case IMAGE_FILE_MACHINE_ARM:
        return "arm";
    case IMAGE_FILE_MACHINE_UNKNOWN:
        return "unknown";
    default:
        return std::format("machine-0x{:04X}", architecture);
    }
}

std::string ProcessorArchitectureName(WORD architecture) noexcept {
    switch (architecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        return "x64";
    case PROCESSOR_ARCHITECTURE_INTEL:
        return "x86";
    case PROCESSOR_ARCHITECTURE_ARM64:
        return "arm64";
    case PROCESSOR_ARCHITECTURE_ARM:
        return "arm";
    case PROCESSOR_ARCHITECTURE_UNKNOWN:
        return "unknown";
    default:
        return std::format("processor-0x{:04X}", architecture);
    }
}

bool IsNative64BitProcessor(WORD architecture) noexcept {
    return architecture == PROCESSOR_ARCHITECTURE_AMD64 || architecture == PROCESSOR_ARCHITECTURE_ARM64;
}

std::string IntegrityName(DWORD rid) noexcept {
    switch (rid) {
    case SECURITY_MANDATORY_UNTRUSTED_RID:
        return "untrusted";
    case SECURITY_MANDATORY_LOW_RID:
        return "low";
    case SECURITY_MANDATORY_MEDIUM_RID:
        return "medium";
    case SECURITY_MANDATORY_MEDIUM_PLUS_RID:
        return "medium-plus";
    case SECURITY_MANDATORY_HIGH_RID:
        return "high";
    case SECURITY_MANDATORY_SYSTEM_RID:
        return "system";
    case SECURITY_MANDATORY_PROTECTED_PROCESS_RID:
        return "protected-process";
    default:
        return std::format("rid-{}", rid);
    }
}

} // namespace qiqi::diag::detail
