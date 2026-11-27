// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

namespace qiqi::dict_manager::tests {

namespace {
void TestSavedDisplayPreferences() {
    ConfigRegistryFixture registry;
    const auto& subkey = registry.Subkey();
    SetThemeDarkOverrideForTest(true);
    SetHighContrastOverrideForTest(false);
    const auto directory = std::filesystem::current_path() /
        (L"dictmgr-display-test-" + std::to_wstring(::GetCurrentProcessId()));
    const auto cleanup = gsl::finally([&]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        SetMonitorMetricsOverrideForTest(std::nullopt);
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        DrainQuitMessages();
    });
    std::filesystem::create_directories(directory);
    QiQiConfig saved;
    saved.ui_theme = UiTheme::Light;
    saved.ui_language = UiLanguage::ZhCn;
    saved.ui_text_scale = UiTextScale::Percent125;
    saved.easy_reading = true;
    QIQI_TEST_CHECK(SaveFixtureConfig(saved, subkey));
    DictManagerWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr),
        directory / L"user_dict.txt", directory / L"phrase_freq.qfreq"));
    const HWND list = Child(window, List);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    const auto check_font = [&](UiTextScale scale) {
        const int expected = ScaleDisplayPx(kFluentBodyFontSize, dpi, scale);
        LOGFONTW face{};
        QIQI_TEST_CHECK(TryGetLocalizedUiFont(saved.ui_language, face));
        for (const HWND control : {Child(window, Toolbar), Child(window, SourceCombo),
                 Child(window, SearchEdit), list, ListView_GetHeader(list),
                 Child(window, EditWordInput), Child(window, EditCodeInput), Child(window, Status)}) {
            LOGFONTW font{};
            const auto handle = reinterpret_cast<HFONT>(::SendMessageW(control, WM_GETFONT, 0, 0));
            QIQI_TEST_CHECK(::GetObjectW(handle, sizeof(font), &font) == sizeof(font));
            QIQI_TEST_CHECK(-font.lfHeight == expected && LogFontFaceNameView(font) == LogFontFaceNameView(face));
        }
    };
    check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
    QIQI_TEST_CHECK(ControlText(Child(window, SearchLabel)) == L"搜索");
    QIQI_TEST_CHECK(GetDisplayPreferences(LoadConfig()) == GetDisplayPreferences(saved));
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == GetThemePalette(false).input_bg);
    const int target = ScalePxByDpi(kComfortMinimumTargetDip, dpi);
    RECT search{}, combo{};
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, SearchEdit), &search));
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, SourceCombo), &combo));
    QIQI_TEST_CHECK(search.bottom - search.top >= target);
    QIQI_TEST_CHECK(combo.bottom - combo.top >= target);
    const auto reload = ToolbarItemRect(window, Reload);
    QIQI_TEST_CHECK(reload.right - reload.left >= target && reload.bottom - reload.top >= target);

    InvokeCommand(window, NewWord);
    ::SetWindowTextW(Child(window, EditWordInput), L"未保存词条");
    ::SetWindowTextW(Child(window, EditCodeInput), L"abcd");
    ::SendMessageW(Child(window, EditCodeInput), EM_SETSEL, 1, 3);
    const int count = ListView_GetItemCount(list);
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), ScalePxByDpi(250, dpi));
    const int custom_width = ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word));
    const auto poll = [&]() {
        ::SendMessageW(window.Handle(), WM_TIMER, kRefreshTimerId, 0);
    };
    saved.ui_language = UiLanguage::EnUs;
    QIQI_TEST_CHECK(SaveFixtureConfig(saved, subkey));
    poll();
    check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
    QIQI_TEST_CHECK(ControlText(Child(window, SearchLabel)) == L"Search");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"未保存词条");
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word)) == custom_width);
    auto unsaved_settings = saved;
    unsaved_settings.ui_theme = UiTheme::Dark;
    unsaved_settings.ui_text_scale = UiTextScale::Percent200;
    // A cancelled settings draft never reaches the saved configuration.
    QIQI_TEST_CHECK(GetDisplayPreferences(unsaved_settings) != GetDisplayPreferences(LoadConfig()));
    poll();
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == GetThemePalette(false).input_bg);

    SetThemeDarkOverrideForTest(false);
    for (const auto scale : {UiTextScale::Percent150, UiTextScale::Percent200, UiTextScale::Percent100}) {
        constexpr int kColumnScaleReferenceDip = 100;
        const int previous_scale = ScaleDisplayPx(kColumnScaleReferenceDip, dpi,
            NormalizeDisplayPreferences(GetDisplayPreferences(LoadConfig())).text_scale);
        saved.ui_text_scale = scale;
        saved.ui_theme = UiTheme::Dark;
        QIQI_TEST_CHECK(SaveFixtureConfig(saved, subkey));
        const int old_width = ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word));
        poll();
        check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
        const int next_scale = ScaleDisplayPx(kColumnScaleReferenceDip, dpi,
            NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
        QIQI_TEST_CHECK(ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word)) ==
            ::MulDiv(old_width, next_scale, previous_scale));
        QIQI_TEST_CHECK(ListView_GetBkColor(list) == GetThemePalette(true).input_bg);
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == count);
        QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == selected);
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"未保存词条");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
        DWORD start{}, end{};
        ::SendMessageW(Child(window, EditCodeInput), EM_GETSEL,
            reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        QIQI_TEST_CHECK(start == 1 && end == 3);
        RECT row{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetItemRect(list, selected, &row, LVIR_BOUNDS));
        QIQI_TEST_CHECK(row.bottom - row.top >= target);
    }
    QIQI_TEST_CHECK(custom_width > target);
    QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr, 0, 0,
        ScalePxByDpi(1400, dpi), ScalePxByDpi(680, dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    for (const auto control : {EditWordInput, EditCodeInput, InlineSave}) {
        RECT bounds{};
        QIQI_TEST_CHECK((::GetWindowLongPtrW(Child(window, control), GWL_STYLE) & WS_VISIBLE) != 0);
        QIQI_TEST_CHECK(::GetWindowRect(Child(window, control), &bounds));
        QIQI_TEST_CHECK(bounds.bottom - bounds.top >= target);
        if (control == InlineSave) {
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
            RECT button{};
            QIQI_TEST_CHECK(::SendMessageW(Child(window, InlineSave), TB_GETRECT,
                std::to_underlying(SaveWord), reinterpret_cast<LPARAM>(&button)));
            QIQI_TEST_CHECK(button.right - button.left >= target && button.bottom - button.top >= target);
        }
    }
    SetHighContrastOverrideForTest(true);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == ::GetSysColor(COLOR_WINDOW));
    SetHighContrastOverrideForTest(false);
    saved.ui_theme = UiTheme::HighContrast;
    QIQI_TEST_CHECK(SaveFixtureConfig(saved, subkey));
    poll();
    check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == ResolveDisplayAppearance(GetDisplayPreferences(saved)).palette.input_bg);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{
        {0, 0, ScalePxByDpi(440, dpi), ScalePxByDpi(680, dpi)}, dpi});
    QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr, 0, 0,
        ScalePxByDpi(440, dpi), ScalePxByDpi(680, dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    RECT source{}, search_label{}, list_rect{}, client{};
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, SourceCombo), &source));
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, SearchLabel), &search_label));
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, SearchEdit), &search));
    QIQI_TEST_CHECK(::GetWindowRect(list, &list_rect));
    QIQI_TEST_CHECK(search_label.top >= source.bottom);
    QIQI_TEST_CHECK(list_rect.top >= search.bottom);
    QIQI_TEST_CHECK(::GetClientRect(window.Handle(), &client));
    POINT origin{};
    QIQI_TEST_CHECK(::ClientToScreen(window.Handle(), &origin));
    QIQI_TEST_CHECK(search.right <= origin.x + client.right);
    check_font(NormalizeDisplayPreferences(GetDisplayPreferences(saved)).text_scale);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{
        {0, 0, ScalePxByDpi(440, dpi), ScalePxByDpi(240, dpi)}, dpi});
    QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr, 0, 0,
        ScalePxByDpi(440, dpi), ScalePxByDpi(240, dpi), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE;
    QIQI_TEST_CHECK(::GetScrollInfo(window.Handle(), SB_VERT, &scroll));
    QIQI_TEST_CHECK(scroll.nMax >= gsl::narrow_cast<int>(scroll.nPage));
    ::SendMessageW(window.Handle(), WM_VSCROLL, SB_BOTTOM, 0);
    RECT status{};
    QIQI_TEST_CHECK(::GetWindowRect(Child(window, Status), &status));
    QIQI_TEST_CHECK(::GetClientRect(window.Handle(), &client));
    QIQI_TEST_CHECK(::ClientToScreen(window.Handle(), &origin));
    QIQI_TEST_CHECK(status.bottom <= origin.y + client.bottom);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"未保存词条");
    saved.easy_reading = false;
    QIQI_TEST_CHECK(SaveFixtureConfig(saved, subkey));
    poll();
    QIQI_TEST_CHECK(!ListView_GetImageList(list, LVSIL_SMALL));
    check_font(saved.ui_text_scale);
}
} // namespace

void TestGuiWindow() {
    TestConfigRegistryIsolation();
    ConfigRegistryFixture registry;
    const auto& display_subkey = registry.Subkey();
    QIQI_TEST_CHECK(SaveFixtureConfig(QiQiConfig{}, display_subkey));
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
    TestSavedDisplayPreferences();
    QIQI_TEST_CHECK(std::wstring_view(GetActiveConfigRegistrySubKey()) == display_subkey);
}

} // namespace qiqi::dict_manager::tests
