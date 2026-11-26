// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/dictcodec.h"
#include "common/fileutil.h"
#include "common/version.h"
#include <gsl/gsl>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <format>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>

namespace qiqi::diag {
namespace detail {

Json StageOrSkipped(bool selected, gsl::not_null<Json (*)()> collector, std::string_view id,
                    std::string_view label) {
    return selected ? collector.get()() : SkippedStage(id, label);
}

std::string OverallStatus(const Json& stages) {
    bool has_error = false;
    bool has_warning = false;
    for (const auto& stage : stages) {
        const auto status = stage.value("status", "skipped");
        if (status == "error") {
            has_error = true;
        } else if (status == "warning") {
            has_warning = true;
        }
    }
    return has_error ? "error" : (has_warning ? "warning" : "ok");
}

} // namespace detail

using namespace detail;

constexpr std::string_view kReportSchema = "qiqiime-diag";
constexpr uint32_t kReportSchemaVersion = 1;

std::filesystem::path MakeDefaultReportDirectory() {
    SYSTEMTIME time{};
    ::GetSystemTime(&time);
    const std::filesystem::path directory = GetAppDataDirectory() / L"diagnostics";
    return directory / std::format(L"QiQiIME-diag-"
                                   L"{:04d}{:02d}{:02d}-"
                                   L"{:02d}{:02d}{:02d}-{}",
                                   time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                                   ::GetCurrentProcessId());
}

std::wstring RedactPathForReport(std::wstring_view path) {
    std::wstring output(path);
    output = RedactOnePathRoot(std::move(output), L"LOCALAPPDATA", L"<LOCALAPPDATA>");
    return RedactOnePathRoot(std::move(output), L"USERPROFILE", L"<USERPROFILE>");
}

std::wstring ExtractLocalServerExecutable(std::wstring_view command) {
    std::wstring_view value = command;
    while (!value.empty() && std::iswspace(value.front()) != 0) {
        value.remove_prefix(1);
    }
    if (value.empty())
        return {};
    if (value.front() == L'"') {
        value.remove_prefix(1);
        const size_t end = value.find(L'"');
        return std::wstring(value.substr(0, end));
    }
    const size_t end = value.find_first_of(L" \t\r\n");
    return std::wstring(value.substr(0, end));
}

HRESULT BuildReport(const Selection& selection, bool compact, std::string& output) {
    try {
        Json stages = Json::array();
        stages.push_back(StageOrSkipped(selection.machine, CollectMachine, "machine", "Machine information"));
        stages.push_back(StageOrSkipped(selection.processes, CollectProcesses, "processes",
                                        "Related process information"));
        stages.push_back(StageOrSkipped(selection.ime, CollectIme, "ime", "IME configuration and files"));
        stages.push_back(
            StageOrSkipped(selection.broker, CollectBroker, "broker", "Broker and DCOM information"));

        Json report{
            {"schema", kReportSchema},
            {"schema_version", kReportSchemaVersion},
            {"generated_at_utc", UtcTimestamp()},
            {"tool",
             Json{
                 {"name", "QiQiIMEDiag"},
                 {"version", version::kFullVersion},
                 {"architecture", version::kArch},
                 {"pid", ::GetCurrentProcessId()},
             }},
            {"privacy",
             Json{
                 {"report_location", "local-only"},
                 {"path_redaction", "user-profile and local-appdata roots"},
                 {"excluded", Json::array({
                                  "host name",
                                  "account names",
                                  "command lines",
                                  "typed text",
                                  "dictionary contents",
                                  "process memory",
                                  "secrets",
                              })},
                 {"review_before_sharing", true},
             }},
            {"overall_status", OverallStatus(stages)},
            {"stages", std::move(stages)},
        };
        std::string generated = report.dump(compact ? -1 : 2) + "\n";
        output = std::move(generated);
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    } catch (const Json::exception&) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    } catch (const std::system_error& error) {
        const int code = error.code().value();
        return code != 0 ? HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(code)) : E_FAIL;
    }
}

HRESULT WriteReportSet(const std::filesystem::path& directory, std::string_view report) {
    try {
        const Json document = Json::parse(report);
        if (!directory.filename().empty() && std::filesystem::exists(directory)) {
            const auto first = std::filesystem::directory_iterator(directory);
            if (first != std::filesystem::directory_iterator{}) {
                return HRESULT_FROM_WIN32(ERROR_DIR_NOT_EMPTY);
            }
        } else {
            std::filesystem::create_directories(directory);
        }
        const bool compact = report.find('\n') == report.size() - 1;
        const auto& stages = document.at("stages");
        Json summary{
            {"schema", document.at("schema")},
            {"schema_version", document.at("schema_version")},
            {"generated_at_utc", document.at("generated_at_utc")},
            {"tool", document.at("tool")},
            {"privacy", document.at("privacy")},
            {"overall_status", document.at("overall_status")},
            {"stages", Json::array()},
        };
        for (const auto& stage : stages) {
            const std::string id = stage.at("id").get<std::string>();
            const std::string status = stage.at("status").get<std::string>();
            Json summary_stage{
                {"id", id},
                {"label", stage.at("label")},
                {"status", status},
            };
            if (stage.contains("reason")) {
                summary_stage["reason"] = stage.at("reason");
            }
            if (status != "skipped") {
                const std::string file = id + ".json";
                summary_stage["file"] = file;
                Json stage_value = stage;
                const bool split_dcom = id == "broker" && stage_value.contains("dcom");
                Json dcom_value;
                if (split_dcom) {
                    dcom_value = stage_value["dcom"];
                    stage_value.erase("dcom");
                    summary_stage["additional_files"] = Json::array({"dcom.json"});
                }
                const auto write_part = [&](const std::string& file_name, std::string_view part,
                                            const Json& value) {
                    Json part_document{
                        {"schema", document.at("schema")},
                        {"schema_version", document.at("schema_version")},
                        {"generated_at_utc", document.at("generated_at_utc")},
                        {"tool", document.at("tool")},
                    };
                    part_document[std::string(part)] = value;
                    std::string content = part_document.dump(compact ? -1 : 2) + "\n";
                    return WriteFileAtomicallyResult(directory / Utf8ToWide(file_name),
                                                     codec::AsByteSpan(content));
                };
                const HRESULT stage_result = write_part(file, "stage", stage_value);
                if (FAILED(stage_result)) {
                    return stage_result;
                }
                if (split_dcom) {
                    const HRESULT dcom_result = write_part("dcom.json", "dcom", dcom_value);
                    if (FAILED(dcom_result)) {
                        return dcom_result;
                    }
                }
            }
            summary["stages"].push_back(std::move(summary_stage));
        }
        std::string summary_text = summary.dump(compact ? -1 : 2) + "\n";
        return WriteFileAtomicallyResult(directory / L"summary.json", codec::AsByteSpan(summary_text));
    } catch (const Json::exception&) {
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::length_error&) {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    } catch (const std::filesystem::filesystem_error& error) {
        return HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(error.code().value()));
    } catch (const std::system_error& error) {
        return HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(error.code().value()));
    }
}

} // namespace qiqi::diag
