// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/diag/diag.h"

#include "common/RAII.h"

#include <aclapi.h>
#include <tlhelp32.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace qiqi::diag::detail {

using Json = nlohmann::ordered_json;

std::string HResultText(HRESULT result);
Json CheckResult(std::string_view name, HRESULT result, std::string_view status = {});
void AddCheck(Json& stage, const Json& check);
void FinalizeStage(Json& stage);
Json SkippedStage(std::string_view name, std::string_view label);
Json NewStage(std::string_view name, std::string_view label);
std::wstring GuidString(REFGUID value);
std::string UtcTimestamp();
std::wstring EnvironmentValue(const wchar_t* name);
bool StartsWithPathRoot(std::wstring_view value, std::wstring_view root) noexcept;
std::wstring RedactOnePathRoot(std::wstring path, const wchar_t* variable, std::wstring_view token);
HRESULT OpenRegistryKeyRead(HKEY root, std::wstring_view path, REGSAM access, ScopedRegKey& output);
HRESULT ReadRegistryValue(HKEY key, const wchar_t* name, DWORD& type, std::vector<uint8_t>& output);
HRESULT ReadRegistryString(HKEY key, const wchar_t* name, std::wstring& output);
HRESULT ReadRegistryDword(HKEY key, const wchar_t* name, DWORD& output) noexcept;
std::string ArchitectureName(USHORT architecture) noexcept;
std::string ProcessorArchitectureName(WORD architecture) noexcept;
bool IsNative64BitProcessor(WORD architecture) noexcept;
std::string IntegrityName(DWORD rid) noexcept;

Json ProcessDetails(const PROCESSENTRY32W& process);
HRESULT InspectSecurityDescriptor(PSECURITY_DESCRIPTOR descriptor, Json& output);
bool AclGrants(const Json& security, std::string_view sid, ACCESS_MASK required);
HRESULT InspectRegistryHandleAcl(HKEY key, Json& output);
Json InspectPath(const std::filesystem::path& path, bool include_acl, ACCESS_MASK required_mask);

Json CollectMachine();
Json CollectProcesses();
void CollectDisplayInformation(Json& stage);
void CollectFontInformation(Json& stage);
void CollectCpuTopology(Json& cpu, Json& stage);
void CollectAccessibilityInformation(Json& stage);
void CollectThemeInformation(Json& stage);
Json CollectIme();
Json CollectBroker();
Json CollectDcomPolicy();

} // namespace qiqi::diag::detail
