// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/diag.h"

#include "common/fileutil.h"
#include "common/i18n.h"
#include "common/version.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <bit>
#include <cwctype>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <gsl/gsl>

namespace {

struct Options {
    qiqi::diag::Selection selection;
    std::optional<qiqi::UiLanguage> language;
    bool accept_all = false;
    bool compact = false;
    bool show_help = false;
    std::filesystem::path output_directory;
    std::filesystem::path log_directory;
};

std::string UiText(qiqi::UiLanguage language, const wchar_t* chinese, const wchar_t* english) noexcept {
    return qiqi::WideToUtf8(qiqi::LocalizedUiText(language, chinese, english));
}

void PrintUsage(qiqi::UiLanguage language) {
    std::cout
        << "QiQiIMEDiag " << qiqi::version::kFullVersion
        << "\nQiQiIMEDiag [--yes] [--compact] [--output <directory> | --log-directory <directory>]\n"
        << UiText(language,
                  L"  --yes, -y        跳过四个确认步骤并收集全部阶段。\n"
                  L"  --compact        写入压缩后的 JSON。\n"
                  L"  --output, -o     使用指定目录作为本次运行目录。\n"
                  L"  --log-directory  在指定目录内新建带时间戳的运行目录。\n"
                  L"  --language       覆盖输入法界面语言：auto、zh-CN、en-US（可缩写 --lang）。\n"
                  L"  --help, -h       显示帮助。\n",
                  L"  --yes, -y        Approve all four stages without interactive consent.\n"
                  L"  --compact        Write minified JSON.\n"
                  L"  --output, -o     Use the specified directory for this run.\n"
                  L"  --log-directory  Create a timestamped run directory inside the specified directory.\n"
                  L"  --language       Override IME language: auto, zh-CN, or en-US (alias: --lang).\n"
                  L"  --help, -h       Show this help.\n");
}

void PrintPrivacyNotice(qiqi::UiLanguage language) {
    std::cout
        << "\n"
        << UiText(language, L"奇奇输入法诊断 (", L"QiQi IME diagnostics (") << qiqi::version::kFullVersion
        << ")\n"
        << UiText(
               language,
               L"这是只读诊断工具，不会修复设置或修改 ACL。\n"
               L"报告仅保存在此计算机。用户路径已脱敏；不收集命令行、词典内容、键盘输入、账号名、\n"
               L"主机名、网络数据或秘密。经你确认后，报告可能包含 SID、PID、配置值、文件存在状态\n"
               L"和安全掩码。分享前请先检查文件。\n",
               L"This is a read-only diagnostic tool. It will not repair settings or change ACLs.\n"
               L"Reports stay on this computer. Profile paths are redacted; command lines, dictionary\n"
               L"contents, typed text, account names, secrets, host names, and network data are not "
               L"collected.\n"
               L"Approved stages may include SIDs, PIDs, configuration values, file presence, and security\n"
               L"masks. Review the files before sharing.\n");
}

std::wstring Trim(std::wstring text) {
    const auto is_space = [](wchar_t value) { return std::iswspace(value) != 0; };
    const auto first = std::ranges::find_if_not(text, is_space);
    if (first == text.end())
        return {};
    const auto last = std::find_if_not(text.rbegin(), text.rend(), is_space).base();
    return std::wstring(first, last);
}

constexpr wchar_t FoldAscii(wchar_t value) noexcept {
    return value >= L'A' && value <= L'Z' ? value + (L'a' - L'A') : value;
}

constexpr bool EqualsIgnoreAsciiCase(std::wstring_view left, std::wstring_view right) noexcept {
    return std::ranges::equal(left, right,
                              [](wchar_t lhs, wchar_t rhs) { return FoldAscii(lhs) == FoldAscii(rhs); });
}

constexpr bool IsConfirmed(std::wstring_view answer) noexcept {
    return EqualsIgnoreAsciiCase(answer, L"y") || EqualsIgnoreAsciiCase(answer, L"yes") || answer == L"是";
}

bool Confirm(qiqi::UiLanguage language, std::wstring_view heading, std::wstring_view fields) {
    std::cout << "\n"
              << qiqi::WideToUtf8(heading) << "\n"
              << qiqi::WideToUtf8(fields)
              << UiText(language, L"\n是否收集此阶段？[y/N]: ", L"\nCollect this stage? [y/N]: ")
              << std::flush;
    std::wstring answer;
    if (!std::getline(std::wcin, answer)) {
        std::cerr << UiText(language, L"\n输入已关闭，此阶段已跳过。\n",
                            L"\nInput closed; this stage was skipped.\n");
        return false;
    }
    return IsConfirmed(Trim(std::move(answer)));
}

bool HasInteractiveConsole() noexcept {
    return ::GetFileType(::GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_CHAR &&
           ::GetFileType(::GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR;
}

qiqi::diag::Selection ConfirmWithFtxui(qiqi::UiLanguage language) {
    using namespace ftxui;

    qiqi::diag::Selection selection;
    bool proceed = false;
    auto screen = ScreenInteractive::FitComponent();
    const auto exit = screen.ExitLoopClosure();
    auto stages = Container::Vertical({
        Checkbox(UiText(language, L"1. 机器信息  - Windows、显示/DPI、中文字体、主题、辅助功能",
                        L"1. Machine  - Windows, displays/DPI, Chinese fonts, theme, accessibility"),
                 &selection.machine),
        Checkbox(UiText(language, L"2. 进程信息  - 相关 PID、会话、架构、完整性级别",
                        L"2. Processes - related PIDs, session, architecture, integrity"),
                 &selection.processes),
        Checkbox(UiText(language, L"3. 输入法    - 设置、修订号、皮肤、文件、配置 ACL",
                        L"3. IME       - settings, revision, skins, files, config ACL"),
                 &selection.ime),
        Checkbox(UiText(language, L"4. Broker    - 注册、运行时完整性、DCOM、ACL",
                        L"4. Broker    - registration, runtime integrity, DCOM, ACLs"),
                 &selection.broker),
    });
    auto actions = Container::Horizontal({
        Button(UiText(language, L"收集选中阶段", L"Collect selected stages"),
               [&] {
                   proceed = true;
                   exit();
               }),
        Button(UiText(language, L"取消", L"Cancel"), exit),
    });
    auto root = Container::Vertical({
        stages,
        actions,
    });
    auto renderer = Renderer(root, [&] {
        return vbox({
                   text(UiText(language, L"奇奇输入法诊断", L"QiQi IME Diag")) | bold | color(Color::Cyan) |
                       center,
                   text(qiqi::WideToUtf8(qiqi::version::kFullVersionW)) | dim | center,
                   separator(),
                   vbox({
                       text(UiText(language, L"隐私说明：只读诊断，报告仅保存在此计算机。",
                                   L"Privacy: this is read-only; reports stay on this computer.")) |
                           bold,
                       paragraph(
                           UiText(language,
                                  L"用户路径已脱敏；不收集命令行、词典内容、键盘输入、账号名、秘密、"
                                  L"主机名、进程内存和网络数据。分享前请检查 JSON 文件。",
                                  L"Profile paths are redacted. Command lines, dictionary contents, "
                                  L"typed text, account names, secrets, host names, process memory, "
                                  L"and network data are not collected. Review the JSON before sharing.")),
                   }) | border |
                       color(Color::CyanLight),
                   separator(),
                   text(UiText(language, L"选择一个或多个采集阶段：",
                               L"Choose one or more collection stages:")) |
                       bold,
                   stages->Render(),
                   separator(),
                   actions->Render() | center,
                   text(UiText(
                       language, L"空格：切换 | Tab/方向键：导航 | Enter：确认 | 取消：不生成报告",
                       L"Space: toggle | Tab/arrow keys: navigate | Enter: activate | Cancel: no report")) |
                       dim | center,
               }) |
               border | size(WIDTH, LESS_THAN, 112);
    });
    screen.Loop(renderer);
    return proceed ? selection : qiqi::diag::Selection{};
}

HRESULT ParseArguments(std::span<wchar_t*> arguments, Options& output) noexcept {
    try {
        for (size_t index = 1; index < arguments.size(); ++index) {
            const std::wstring_view argument(gsl::at(arguments, index));
            if (argument == L"--yes" || argument == L"-y") {
                output.accept_all = true;
                continue;
            }
            if (argument == L"--compact") {
                output.compact = true;
                continue;
            }
            if (argument == L"--output" || argument == L"-o") {
                if (++index >= arguments.size())
                    return E_INVALIDARG;
                output.output_directory = gsl::at(arguments, index);
                continue;
            }
            if (argument == L"--log-directory") {
                if (++index >= arguments.size())
                    return E_INVALIDARG;
                output.log_directory = gsl::at(arguments, index);
                continue;
            }
            if (argument == L"--language" || argument == L"--lang") {
                if (++index >= arguments.size())
                    return E_INVALIDARG;
                const std::wstring_view value(gsl::at(arguments, index));
                if (_wcsicmp(value.data(), L"auto") == 0) {
                    output.language = qiqi::UiLanguage::Auto;
                } else if (_wcsicmp(value.data(), L"zh-CN") == 0 || _wcsicmp(value.data(), L"zh") == 0) {
                    output.language = qiqi::UiLanguage::ZhCn;
                } else if (_wcsicmp(value.data(), L"en-US") == 0 || _wcsicmp(value.data(), L"en") == 0) {
                    output.language = qiqi::UiLanguage::EnUs;
                } else {
                    return E_INVALIDARG;
                }
                continue;
            }
            if (argument == L"--help" || argument == L"-h") {
                output.show_help = true;
                continue;
            }
            return E_INVALIDARG;
        }
        if (!output.output_directory.empty() && !output.log_directory.empty()) {
            return E_INVALIDARG;
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::filesystem::filesystem_error&) {
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    }
}

void PrintFailure(HRESULT result, qiqi::UiLanguage language) {
    std::cerr << UiText(language, L"奇奇输入法诊断失败 (HRESULT ", L"QiQi IME diagnostic failed (HRESULT ")
              << std::format("0x{:08X}", std::bit_cast<uint32_t>(result))
              << UiText(language, L")。\n", L").\n");
}

int Run(int argc, wchar_t** argv, qiqi::UiLanguage& language) {
    Options options;
    const std::span<wchar_t*> arguments(argv, gsl::narrow_cast<size_t>(argc));
    const HRESULT parse_result = ParseArguments(arguments, options);
    if (options.language) {
        language = qiqi::ResolveUiLanguage(*options.language);
    } else {
        qiqi::UiLanguage configured = qiqi::UiLanguage::Auto;
        const HRESULT result = qiqi::diag::ReadConfiguredUiLanguage(configured);
        language = qiqi::ResolveUiLanguage(configured);
        if (FAILED(result)) {
            std::cerr << UiText(language, L"无法读取界面语言设置，使用 Windows 界面语言 (HRESULT ",
                L"Cannot read the IME language setting; using the Windows UI language (HRESULT ")
                << std::format("0x{:08X}", std::bit_cast<uint32_t>(result)) << ").\n";
        }
    }
    if (FAILED(parse_result)) {
        PrintFailure(parse_result, language);
        PrintUsage(language);
        return 2;
    }
    if (options.show_help) {
        PrintUsage(language);
        return 0;
    }

    if (options.accept_all) {
        PrintPrivacyNotice(language);
        options.selection = {true, true, true, true};
    } else if (HasInteractiveConsole()) {
        options.selection = ConfirmWithFtxui(language);
    } else {
        PrintPrivacyNotice(language);
        options.selection.machine = Confirm(
            language, qiqi::LocalizedUiText(language, L"[1/4] 机器信息", L"[1/4] Machine information"),
            qiqi::LocalizedUiText(
                language, L"Windows 版本/架构、CPU/内存、显示器/DPI、中文字体、当前主题与辅助功能/Narrator。",
                L"Windows version/architecture, CPU/memory, displays/DPI, Chinese fonts, theme, "
                L"accessibility, and Narrator."));
        options.selection.processes = Confirm(
            language,
            qiqi::LocalizedUiText(language, L"[2/4] 相关进程信息", L"[2/4] Related process information"),
            qiqi::LocalizedUiText(
                language,
                L"奇奇输入法、TSF、Search、Feedback Hub 和 Broker 相关进程的名称、PID、父进程/会话、\n"
                L"架构与完整性级别；不收集命令行和进程内存。",
                L"Names, PIDs, parent/session IDs, architecture, and integrity level for QiQi, TSF, Search,\n"
                L"Feedback Hub, and broker-related processes. Command lines and process memory are "
                L"excluded."));
        options.selection.ime = Confirm(
            language, qiqi::LocalizedUiText(language, L"[3/4] 输入法信息", L"[3/4] IME information"),
            qiqi::LocalizedUiText(
                language,
                L"有效设置、配置修订号、皮肤目录、词典/配置文件存在状态、COM 注册和配置键 ACL；\n"
                L"用户路径已脱敏，不收集词典内容。",
                L"Effective settings, config revision, skin catalog, dictionary/config file presence, COM\n"
                L"registration, and config-key ACL. Profile paths are redacted; dictionary contents are "
                L"excluded."));
        options.selection.broker = Confirm(
            language,
            qiqi::LocalizedUiText(language, L"[4/4] Broker 与 DCOM 信息",
                                  L"[4/4] Broker and DCOM information"),
            qiqi::LocalizedUiText(
                language,
                L"Broker CLSID/AppID 注册、LocalServer/Proxy 路径、ACL 摘要、Broker 运行状态、\n"
                L"完整性级别及 DCOM 策略；此阶段不激活 COM，也不会启动 Broker。",
                L"Broker CLSID/AppID registration, LocalServer/Proxy paths, ACL summaries, Broker process\n"
                L"state and integrity, and DCOM policy. This stage does not activate COM or start the "
                L"Broker."));
    }

    if (!options.selection.Any()) {
        std::cout << UiText(language, L"\n未选择任何阶段，没有生成报告。\n",
                            L"\nNo stage selected. No report was created.\n");
        return 0;
    }

    std::string report;
    const HRESULT collect_result = qiqi::diag::BuildReport(options.selection, options.compact, report);
    if (FAILED(collect_result)) {
        PrintFailure(collect_result, language);
        return 4;
    }

    try {
        if (options.output_directory.empty()) {
            const auto default_directory = qiqi::diag::MakeDefaultReportDirectory();
            options.output_directory = options.log_directory.empty()
                                           ? default_directory
                                           : options.log_directory / default_directory.filename();
        }
        const HRESULT write_result = qiqi::diag::WriteReportSet(options.output_directory, report);
        if (FAILED(write_result)) {
            PrintFailure(write_result, language);
            return 5;
        }
        std::cout << UiText(language, L"\n诊断报告文件已写入：\n", L"\nDiagnostic report files written to:\n")
                  << qiqi::WideToUtf8(options.output_directory.wstring()) << "\n";
        return 0;
    } catch (const std::bad_alloc&) {
        PrintFailure(E_OUTOFMEMORY, language);
        return 4;
    } catch (const std::filesystem::filesystem_error& error) {
        PrintFailure(HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(error.code().value())), language);
        return 5;
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    auto language = qiqi::ResolveUiLanguage(qiqi::UiLanguage::Auto);
    try {
        return Run(argc, argv, language);
    } catch (const std::bad_alloc&) {
        PrintFailure(E_OUTOFMEMORY, language);
        return 4;
    } catch (const std::filesystem::filesystem_error& error) {
        PrintFailure(HRESULT_FROM_WIN32(gsl::narrow_cast<DWORD>(error.code().value())), language);
        return 5;
    }
}
