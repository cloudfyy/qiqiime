// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

void TestGuiWindow() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_mgr_gui_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        DrainQuitMessages();
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.qfreq";

    {
        DictManagerWindow native_window;
        QIQI_TEST_CHECK(native_window.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
        QIQI_TEST_CHECK(::AreDpiAwarenessContextsEqual(
            ::GetWindowDpiAwarenessContext(native_window.Handle()), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        CheckLayout(native_window, ::GetDpiForWindow(native_window.Handle()));
        for (const auto language : {LanguageEn, LanguageZh, LanguageAuto}) {
            InvokeCommand(native_window, language);
            for (const UINT message : {WM_THEMECHANGED, WM_DISPLAYCHANGE}) {
                ::SendMessageW(native_window.Handle(), message, 0, 0);
                CheckLayout(native_window, ::GetDpiForWindow(native_window.Handle()));
            }
        }
    }
    DrainQuitMessages();

    // Keep native per-monitor corrections out of the synthetic DPI matrix.
    const auto previous_dpi_context = ::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    QIQI_TEST_CHECK(previous_dpi_context);
    const auto restore_dpi_context = gsl::finally([previous_dpi_context]() noexcept {
        QIQI_TEST_CHECK(::SetThreadDpiAwarenessContext(previous_dpi_context));
    });

    DictManagerWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
    QIQI_TEST_CHECK(::IsWindow(window.Handle()));
    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 0, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    const int all_count = ListView_GetItemCount(Child(window, List));
    QIQI_TEST_CHECK(all_count > 0);
    InvokeCommand(window, Reload);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == all_count);
    CheckLayout(window, ::GetDpiForWindow(window.Handle()));

    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        for (const int width : {920, 1020, 1160}) {
            RECT proposed{0, 0, ScalePxByDpi(width, dpi), ScalePxByDpi(680, dpi)};
            ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&proposed));
            for (const auto language : {LanguageEn, LanguageZh, LanguageAuto}) {
                InvokeCommand(window, language);
                QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 0);
                QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == all_count);
                const bool english = language == LanguageEn ||
                    (language == LanguageAuto && ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs);
                const auto sources = english
                    ? std::array{L"All Sources", L"User Dict", L"Pinned", L"Blocked", L"Phrase Ordering",
                          L"Primary Dict", L"Jidian Extra Dict"}
                    : std::array{L"全部来源", L"用户词库", L"手动置顶", L"已屏蔽词", L"词组排序", L"主词库", L"极点扩展词库"};
                QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCOUNT, 0, 0) ==
                    gsl::narrow_cast<LRESULT>(sources.size()));
                for (size_t i = 0; i < sources.size(); ++i) {
                    std::array<wchar_t, 64> text{};
                    const LRESULT length = ::SendMessageW(Child(window, SourceCombo), CB_GETLBTEXTLEN, i, 0);
                    QIQI_TEST_CHECK(length > 0 && length < gsl::narrow_cast<LRESULT>(text.size()));
                    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETLBTEXT, i,
                        reinterpret_cast<LPARAM>(text.data())) == length);
                    QIQI_TEST_CHECK(std::wstring_view(text.data()) == gsl::at(sources, i));
                }
                const HMENU menu = ::GetMenu(window.Handle());
                QIQI_TEST_CHECK(MenuText(menu, ClearFreq) == (english ? L"Reset Phrase Ordering" : L"重置词组排序"));
                QIQI_TEST_CHECK(MenuText(menu, Promote) == (english ? L"Prioritize Phrase" : L"词组置前"));
                QIQI_TEST_CHECK(ControlText(Child(window, Status)).find(
                    english ? L"Phrase ordering" : L"词组排序") != std::wstring::npos);
                std::array<wchar_t, 64> edit_tip{};
                NMLVGETINFOTIPW tip{};
                tip.hdr = {Child(window, List), std::to_underlying(List), LVN_GETINFOTIPW};
                tip.iItem = 0;
                tip.iSubItem = std::to_underlying(ColumnId::Edit);
                tip.pszText = edit_tip.data();
                tip.cchTextMax = gsl::narrow_cast<int>(edit_tip.size());
                ::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(List), reinterpret_cast<LPARAM>(&tip));
                QIQI_TEST_CHECK(std::wstring_view(edit_tip.data()) ==
                    (english ? L"Edit entry (code)" : L"编辑词条（编码）"));
                for (const auto id : {LanguageAuto, LanguageZh, LanguageEn}) {
                    QIQI_TEST_CHECK((::GetMenuState(menu, std::to_underlying(id), MF_BYCOMMAND) & MF_CHECKED) ==
                        (id == language ? UINT{MF_CHECKED} : 0u));
                }
                CheckLayout(window, dpi);
                if (width == 1020 && language == LanguageAuto) {
                    CheckCommandAppearance(window, dpi, false, false);
                    CheckCommandAppearance(window, dpi, true, false);
                    CheckCommandAppearance(window, dpi, false, true);
                }
            }
        }
    }

    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    InvokeCommand(window, Reload);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);
    ExerciseGuiCommands(window);
}

} // namespace qiqi::dict_manager::tests
