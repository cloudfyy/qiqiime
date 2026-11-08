#include "common/test_support.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/version.h"
#include "common/d2d_controls.h"
#include "common/theme.h"
#include "common/i18n.h"
#include "common/win32_ui_util.h"
#include "common/command_ui.h"
#include "tools/log_monitor/collector.h"
#include "tools/log_monitor/permissions.h"
#include "tools/log_monitor/window.h"

#include <commctrl.h>
#include <winver.h>
#include <chrono>
#include <filesystem>
#include <thread>
#include <span>
#include <system_error>
#include <cstring>

namespace qiqi::monitor::permission_tests {
void RunPermissionRecoveryTests();
void RunCaptureStartupRollbackTests();
}

namespace {
using namespace qiqi;
using namespace qiqi::monitor;
using enum qiqi::monitor::ControlId;

HWND Child(const MonitorWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

void Click(const MonitorWindow& window, ControlId id) {
    const HWND toolbar = Child(window, Toolbar);
    if (::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) >= 0) {
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
        RECT rect{};
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETRECT, std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
        const LPARAM point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
        ::SendMessageW(toolbar, WM_LBUTTONDOWN, MK_LBUTTON, point);
        ::SendMessageW(toolbar, WM_LBUTTONUP, 0, point);
    } else {
        ::SendMessageW(window.Handle(), WM_COMMAND, std::to_underlying(id), 0);
    }
}

void TestScrollIcons() {
    const ToolCommandUi commands(
        +[](void*, const char*, ULONG) noexcept { QIQI_TEST_CHECK(false); }, nullptr);
    const ScopedWindowDC screen(nullptr);
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), 64, 64));
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(RGB(255, 255, 255)));
    QIQI_TEST_CHECK(screen && dc && bitmap && background);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(selected);
    constexpr RECT rect{0, 0, 64, 64};
    constexpr std::array icons{ToolIcon::Stop, ToolIcon::Pause, ToolIcon::Resume};
    for (const int size : {16, 20, 24, 32}) {
        std::array<std::vector<uint8_t>, icons.size()> masks;
        for (size_t index = 0; index < icons.size(); ++index) {
            QIQI_TEST_CHECK(::FillRect(dc.Get(), &rect, background.Get()));
            commands.DrawIcon(dc.Get(), rect, gsl::at(icons, index), size,
                ::MulDiv(2, size, 20), RGB(0, 0, 0));
            auto& mask = gsl::at(masks, index);
            for (int y = 0; y < 64; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel == RGB(0, 0, 0) || pixel == RGB(255, 255, 255));
                    mask.push_back(pixel == RGB(0, 0, 0));
                }
            }
        }
        for (size_t first = 0; first < masks.size(); ++first) {
            for (size_t second = first + 1; second < masks.size(); ++second) {
                size_t difference = 0;
                for (size_t pixel = 0; pixel < gsl::at(masks, first).size(); ++pixel) {
                    if (gsl::at(gsl::at(masks, first), pixel) != gsl::at(gsl::at(masks, second), pixel)) {
                        ++difference;
                    }
                }
                QIQI_TEST_CHECK(difference >= 10);
            }
        }
    }
}

std::wstring CommandName(const MonitorWindow& window, ControlId id) {
    std::array<wchar_t, 256> text{};
    QIQI_TEST_CHECK(::GetMenuStringW(::GetMenu(window.Handle()), std::to_underlying(id), text.data(),
                                     gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND) > 0);
    return text.data();
}

void SetProcessFilter(const MonitorWindow& window, const wchar_t* name) {
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, Process), name));
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_EDITCHANGE), 0);
}

void SelectPid(const MonitorWindow& window, DWORD pid) {
    const auto text = std::to_wstring(pid);
    const LRESULT index = ::SendMessageW(Child(window, Pid), CB_FINDSTRINGEXACT, gsl::narrow_cast<WPARAM>(-1),
                                         reinterpret_cast<LPARAM>(text.c_str()));
    QIQI_TEST_CHECK(index != CB_ERR);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(index), 0) != CB_ERR);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
}

LRESULT CALLBACK SimulateOpenDropdown(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                      DWORD_PTR) noexcept {
    if (message == CB_GETDROPPEDSTATE)
        return TRUE;
    return ::DefSubclassProc(control, message, wparam, lparam);
}

std::wstring ControlText(HWND hwnd) {
    std::wstring text(gsl::narrow_cast<size_t>(::GetWindowTextLengthW(hwnd)) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, text.data(), gsl::narrow_cast<int>(text.size()));
    text.resize(gsl::narrow_cast<size_t>(copied));
    return text;
}

void SelectLanguage(const MonitorWindow& window, UiLanguage language) noexcept {
    const auto id = std::to_underlying(LanguageAuto) + std::to_underlying(language);
    ::SendMessageW(window.Handle(), WM_COMMAND, id, 0);
}

void CheckComboEditFont(HWND combo, UINT dpi) {
    COMBOBOXINFO info{};
    info.cbSize = sizeof(info);
    QIQI_TEST_CHECK(::GetComboBoxInfo(combo, &info) && info.hwndItem);
    const auto font = reinterpret_cast<HFONT>(::SendMessageW(info.hwndItem, WM_GETFONT, 0, 0));
    LOGFONTW logical{};
    QIQI_TEST_CHECK(::GetObjectW(font, sizeof(logical), &logical) == sizeof(logical));
    QIQI_TEST_CHECK(-logical.lfHeight == ScalePxByDpi(14, dpi));
    const ScopedWindowDC dc(info.hwndItem);
    const ScopedSelectObject selected(dc.Get(), font);
    QIQI_TEST_CHECK(dc && selected);
    TEXTMETRICW metrics{};
    RECT rect{};
    QIQI_TEST_CHECK(::GetTextMetricsW(dc.Get(), &metrics) && ::GetClientRect(info.hwndItem, &rect));
    QIQI_TEST_CHECK(metrics.tmHeight <= rect.bottom - rect.top);
}

void TestMenusAndToolbar(const RECT& work_area) {
    SetMonitorMetricsOverrideForTest(MonitorMetrics{work_area, 96});
    const auto restore = gsl::finally([]() noexcept {
        SetMonitorMetricsOverrideForTest(std::nullopt);
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        MSG message{};
        while (::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
        }
    });
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND toolbar = Child(window, Toolbar);
    constexpr std::array commands{Start, Export, Clear, Copy, Pause, Source};
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        for (const int width : {760, 920, 1160}) {
            RECT proposed{0, 0, ScalePxByDpi(width, dpi), ScalePxByDpi(780, dpi)};
            ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&proposed));
            for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn, UiLanguage::Auto}) {
                SelectLanguage(window, language);
                const HMENU menu = ::GetMenu(window.Handle());
                QIQI_TEST_CHECK(menu && ::GetMenuItemCount(menu) == 5);
                constexpr std::array keys{L'f', L'e', L'v', L'l', L'h'};
                for (size_t i = 0; i < keys.size(); ++i) {
                    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MENUCHAR, gsl::at(keys, i),
                                                   reinterpret_cast<LPARAM>(menu)) == MAKELRESULT(i, MNC_EXECUTE));
                    MEASUREITEMSTRUCT measure{};
                    measure.CtlType = ODT_MENU;
                    measure.itemData = i;
                    QIQI_TEST_CHECK(
                        ::SendMessageW(window.Handle(), WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)));
                    QIQI_TEST_CHECK(measure.itemWidth > 0 &&
                                    measure.itemHeight >= gsl::narrow_cast<UINT>(ScalePxByDpi(14, dpi)));
                }
                for (const auto id : {LanguageAuto, LanguageZh, LanguageEn}) {
                    QIQI_TEST_CHECK(
                        (::GetMenuState(menu, std::to_underlying(id), MF_BYCOMMAND) & MF_CHECKED) ==
                        (std::to_underlying(id) - std::to_underlying(LanguageAuto) == std::to_underlying(language)
                             ? UINT{MF_CHECKED}
                             : 0u));
                }
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) ==
                                gsl::narrow_cast<LRESULT>(commands.size()));
                QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETTEXTROWS, 0, 0) == 0);
                for (size_t i = 0; i < commands.size(); ++i) {
                    const auto id = gsl::at(commands, i);
                    QIQI_TEST_CHECK(Child(window, id) == nullptr);
                    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) ==
                                    gsl::narrow_cast<LRESULT>(i));
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
                    RECT rect{};
                    QIQI_TEST_CHECK(
                        ::SendMessageW(toolbar, TB_GETRECT, std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
                    QIQI_TEST_CHECK(rect.right - rect.left == ScalePxByDpi(36, dpi));
                    QIQI_TEST_CHECK(rect.bottom - rect.top == ScalePxByDpi(36, dpi));
                    std::array<wchar_t, 256> tooltip{};
                    NMTBGETINFOTIPW tip{};
                    tip.hdr = {toolbar, std::to_underlying(Toolbar), TBN_GETINFOTIPW};
                    tip.iItem = std::to_underlying(id);
                    tip.pszText = tooltip.data();
                    tip.cchTextMax = gsl::narrow_cast<int>(tooltip.size());
                    ::SendMessageW(window.Handle(), WM_NOTIFY, tip.hdr.idFrom, reinterpret_cast<LPARAM>(&tip));
                    QIQI_TEST_CHECK(std::wstring_view(tooltip.data()) == CommandName(window, id));

                    const auto tooltip_hwnd = reinterpret_cast<HWND>(::SendMessageW(toolbar, TB_GETTOOLTIPS, 0, 0));
                    QIQI_TEST_CHECK(tooltip_hwnd != nullptr);
                    NMTTDISPINFOW disp{};
                    disp.hdr = {tooltip_hwnd, gsl::narrow_cast<UINT_PTR>(std::to_underlying(id)), TTN_GETDISPINFOW};
                    ::SendMessageW(window.Handle(), WM_NOTIFY, disp.hdr.idFrom, reinterpret_cast<LPARAM>(&disp));
                    QIQI_TEST_CHECK(disp.lpszText && std::wstring_view(disp.lpszText) == CommandName(window, id));
                    TBBUTTONINFO button{};
                    button.cbSize = sizeof(button);
                    button.dwMask = TBIF_STYLE;
                    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTONINFOW, std::to_underlying(id),
                                                   reinterpret_cast<LPARAM>(&button)) >= 0);
                    QIQI_TEST_CHECK((button.fsStyle & (BTNS_SHOWTEXT | BTNS_AUTOSIZE)) == 0);
                }
                RECT client{};
                ::GetClientRect(window.Handle(), &client);
                ::MapWindowPoints(window.Handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
                std::vector<RECT> controls;
                for (HWND child = ::GetWindow(window.Handle(), GW_CHILD); child;
                     child = ::GetWindow(child, GW_HWNDNEXT)) {
                    std::array<wchar_t, 64> name{};
                    ::GetClassNameW(child, name.data(), gsl::narrow_cast<int>(name.size()));
                    QIQI_TEST_CHECK(_wcsicmp(name.data(), L"BUTTON") != 0);
                    RECT bounds{}, overlap{};
                    ::GetWindowRect(child, &bounds);
                    QIQI_TEST_CHECK(bounds.left >= client.left && bounds.top >= client.top &&
                                    bounds.right <= client.right && bounds.bottom <= client.bottom);
                    for (const auto& other : controls)
                        QIQI_TEST_CHECK(!::IntersectRect(&overlap, &bounds, &other));
                    controls.push_back(bounds);
                }
                for (const auto id : {ProcessLabel, PidLabel, LevelLabel, CategoryLabel, SearchLabel}) {
                    const HWND label = Child(window, id);
                    const ScopedWindowDC dc(label);
                    const ScopedSelectObject font(dc.Get(),
                                                  reinterpret_cast<HFONT>(::SendMessageW(label, WM_GETFONT, 0, 0)));
                    const auto text = ControlText(label);
                    SIZE size{};
                    RECT bounds{};
                    ::GetClientRect(label, &bounds);
                    QIQI_TEST_CHECK(
                        ::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(text.size()), &size));
                    QIQI_TEST_CHECK(size.cx <= bounds.right && size.cy <= bounds.bottom);
                }
            }
        }
    }
    SelectLanguage(window, UiLanguage::EnUs);
    const auto icon = [&](ControlId id) {
        TBBUTTONINFO button{};
        button.cbSize = sizeof(button);
        button.dwMask = TBIF_LPARAM;
        QIQI_TEST_CHECK(
            ::SendMessageW(toolbar, TB_GETBUTTONINFOW, std::to_underlying(id), reinterpret_cast<LPARAM>(&button)) >= 0);
        return gsl::narrow_cast<ToolIcon>(button.lParam);
    };
    QIQI_TEST_CHECK(icon(Start) == ToolIcon::Start && icon(Export) == ToolIcon::Save &&
                    icon(Pause) == ToolIcon::Pause && icon(Source) == ToolIcon::Diagnostics);

    // Verify elevated / non-elevated title bar indicator across languages.
    SetElevatedOverrideForTest(false);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(!ControlText(window.Handle()).contains(L"[Administrator]"));
    SetElevatedOverrideForTest(true);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(ControlText(window.Handle()).ends_with(L" [Administrator]"));
    SelectLanguage(window, UiLanguage::ZhCn);
    QIQI_TEST_CHECK(ControlText(window.Handle()).ends_with(L" [管理员]"));
    SetElevatedOverrideForTest(std::nullopt);
    SelectLanguage(window, UiLanguage::EnUs);
    Click(window, Pause);
    QIQI_TEST_CHECK(icon(Pause) == ToolIcon::Resume);
    QIQI_TEST_CHECK(CommandName(window, Pause) == L"Resume auto-scroll");
    QIQI_TEST_CHECK((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(Pause), 0) & TBSTATE_CHECKED) != 0);
    Click(window, Source);
    QIQI_TEST_CHECK(icon(Source) == ToolIcon::Events);
    QIQI_TEST_CHECK(CommandName(window, Source) == L"IME events");
    QIQI_TEST_CHECK((::SendMessageW(toolbar, TB_GETSTATE, std::to_underlying(Source), 0) & TBSTATE_CHECKED) != 0);
    for (const bool dark : {false, true}) {
        for (const bool contrast : {false, true}) {
            SetThemeDarkOverrideForTest(dark);
            SetHighContrastOverrideForTest(contrast);
            ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
            const ScopedWindowDC window_dc(window.Handle());
            const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
            const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(window_dc.Get(), 80, 80));
            const ScopedSelectObject selected(dc.Get(), bitmap.Get());
            std::vector<uint64_t> shapes;
            for (const auto id : commands) {
                NMCUSTOMDRAW draw{};
                draw.hdr = {toolbar, std::to_underlying(Toolbar), NM_CUSTOMDRAW};
                draw.hdc = dc.Get();
                draw.rc = {0, 0, 80, 80};
                draw.dwDrawStage = CDDS_ITEMPREPAINT;
                draw.dwItemSpec = std::to_underlying(id);
                const COLORREF brush_color = ::GetDCBrushColor(dc.Get());
                QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, draw.hdr.idFrom,
                                               reinterpret_cast<LPARAM>(&draw)) == CDRF_SKIPDEFAULT);
                QIQI_TEST_CHECK(::GetDCBrushColor(dc.Get()) == brush_color);
                const auto& palette = GetThemePalette(dark);
                const COLORREF background = contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg;
                const COLORREF foreground = contrast ? ::GetSysColor(COLOR_BTNTEXT) : palette.text_primary;
                size_t ink = 0;
                uint64_t shape = 14695981039346656037ull;
                for (int y = 16; y < 64; ++y) {
                    for (int x = 16; x < 64; ++x) {
                        const auto pixel = ::GetPixel(dc.Get(), x, y);
                        QIQI_TEST_CHECK(pixel == background || pixel == foreground);
                        if (pixel == foreground)
                            ++ink;
                        shape = (shape ^ gsl::narrow_cast<uint64_t>(pixel == foreground)) * 1099511628211ull;
                    }
                }
                QIQI_TEST_CHECK(ink > 10 && std::ranges::find(shapes, shape) == shapes.end());
                shapes.push_back(shape);
            }
        }
    }
    std::array<wchar_t, 16> text{};
    NMTBGETINFOTIPW invalid_tip{};
    invalid_tip.hdr = {toolbar, std::to_underlying(Toolbar), TBN_GETINFOTIPW};
    invalid_tip.iItem = 0x7fff;
    invalid_tip.pszText = text.data();
    invalid_tip.cchTextMax = gsl::narrow_cast<int>(text.size());
    ::SendMessageW(window.Handle(), WM_NOTIFY, invalid_tip.hdr.idFrom, reinterpret_cast<LPARAM>(&invalid_tip));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"TB_GETBUTTONTEXTW");
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    Click(window, Exit);
}

struct AboutExpectation {
    HWND owner = nullptr;
    UiLanguage language = UiLanguage::Auto;
    RECT work_area{};
    bool seen = false;
};

thread_local AboutExpectation* current_about = nullptr;

std::vector<DWORD> IconPixels(HICON icon) {
    ICONINFO info{};
    QIQI_TEST_CHECK(icon && ::GetIconInfo(icon, &info));
    const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
    const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
    BITMAP bitmap{};
    QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
    BITMAPINFO dib{};
    dib.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dib.bmiHeader.biWidth = bitmap.bmWidth;
    dib.bmiHeader.biHeight = -bitmap.bmHeight;
    dib.bmiHeader.biPlanes = 1;
    dib.bmiHeader.biBitCount = 32;
    dib.bmiHeader.biCompression = BI_RGB;
    std::vector<DWORD> pixels(gsl::narrow_cast<size_t>(bitmap.bmWidth) * gsl::narrow_cast<size_t>(bitmap.bmHeight));
    const ScopedWindowDC dc;
    QIQI_TEST_CHECK(dc);
    QIQI_TEST_CHECK(::GetDIBits(dc.Get(), color.Get(), 0, gsl::narrow_cast<UINT>(bitmap.bmHeight), pixels.data(), &dib,
                                DIB_RGB_COLORS) == bitmap.bmHeight);
    return pixels;
}

void TestAboutDialogs() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::vector<RECT> work_areas;
    QIQI_TEST_CHECK(::EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            QIQI_TEST_CHECK(::GetMonitorInfoW(monitor, &info));
            reinterpret_cast<std::vector<RECT>*>(context)->push_back(info.rcWork);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&work_areas)));
    QIQI_TEST_CHECK(!work_areas.empty());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    for (const RECT& work : work_areas) {
        const int width = std::min<LONG>(640, work.right - work.left);
        const int height = std::min<LONG>(480, work.bottom - work.top);
        QIQI_TEST_CHECK(::SetWindowPos(window.Handle(), nullptr, work.left + (work.right - work.left - width) * 3 / 4,
                                       work.top + (work.bottom - work.top - height) / 3, width, height,
                                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW));
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            SelectLanguage(window, language);
            AboutExpectation expected{window.Handle(), language, work};
            auto* previous = std::exchange(current_about, &expected);
            const auto restore = gsl::finally([previous]() noexcept { current_about = previous; });
            const UINT_PTR timer =
                ::SetTimer(window.Handle(), 2, 15, [](HWND timer_owner, UINT, UINT_PTR id, DWORD) noexcept {
                    const HWND dialog = test::FindThreadWindowByClass(kAboutWndClass);
                    if (!dialog)
                        return;
                    QIQI_TEST_CHECK(current_about != nullptr);
                    QIQI_TEST_CHECK(::GetWindow(dialog, GW_OWNER) == current_about->owner);
                    const auto& strings = GetUiStrings(current_about->language);
                    QIQI_TEST_CHECK(ControlText(dialog) == strings.about_title);
                    QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, 201)) == strings.about_btn_github);
                    QIQI_TEST_CHECK(ControlText(::GetDlgItem(dialog, IDOK)) == strings.dlg_btn_ok);
                    const UINT dpi = ::GetDpiForWindow(dialog);
                    const ScopedIcon expected_icon(static_cast<HICON>(
                        ::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     ScalePxByDpi(56, dpi), ScalePxByDpi(56, dpi), LR_DEFAULTCOLOR)));
                    const auto actual_icon = reinterpret_cast<HICON>(::SendMessageW(dialog, WM_GETICON, ICON_BIG, 0));
                    QIQI_TEST_CHECK(IconPixels(actual_icon) == IconPixels(expected_icon.Get()));
                    RECT owner_rect{}, dialog_rect{};
                    QIQI_TEST_CHECK(::GetWindowRect(current_about->owner, &owner_rect));
                    QIQI_TEST_CHECK(::GetWindowRect(dialog, &dialog_rect));
                    const int dialog_width = dialog_rect.right - dialog_rect.left;
                    const int dialog_height = dialog_rect.bottom - dialog_rect.top;
                    const POINT centered = ClampPointToWorkArea(
                        {owner_rect.left + (owner_rect.right - owner_rect.left - dialog_width) / 2,
                         owner_rect.top + (owner_rect.bottom - owner_rect.top - dialog_height) / 2},
                        dialog_width, dialog_height, current_about->work_area);
                    QIQI_TEST_CHECK(std::abs(dialog_rect.left - centered.x) <= 2);
                    QIQI_TEST_CHECK(std::abs(dialog_rect.top - centered.y) <= 2);
                    QIQI_TEST_CHECK(::MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST) ==
                                    ::MonitorFromWindow(current_about->owner, MONITOR_DEFAULTTONEAREST));
                    current_about->seen = true;
                    ::KillTimer(timer_owner, id);
                    ::SendMessageW(dialog, WM_COMMAND, IDOK, 0);
                });
            QIQI_TEST_CHECK(timer != 0);
            const auto cancel_timer = gsl::finally([&window, timer]() noexcept { ::KillTimer(window.Handle(), timer); });
            Click(window, About);
            QIQI_TEST_CHECK(expected.seen);
        }
    }
    ::DestroyWindow(window.Handle());
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

std::wstring ReadClipboardText(HWND owner = nullptr) {
    for (int attempt = 0; attempt < 25; ++attempt) {
        const ScopedClipboard clip(owner);
        if (!clip.IsOpen()) {
            const HWND clip_owner = ::GetOpenClipboardWindow();
            const DWORD holder_tid = clip_owner ? ::GetWindowThreadProcessId(clip_owner, nullptr) : 0;
            if (clip.Error() == ERROR_ACCESS_DENIED && holder_tid != ::GetCurrentThreadId()) {
                ::Sleep(2);
                continue;
            }
        }
        QIQI_TEST_CHECK(clip.IsOpen());
        const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        QIQI_TEST_CHECK(data != nullptr);
        const ScopedGlobalLock<wchar_t> lock(data);
        QIQI_TEST_CHECK(lock.IsValid());
        return std::wstring(lock.Get());
    }
    QIQI_TEST_CHECK(false);
    return {};
}

void TestDarkHeader(HWND list) {
    const HWND header = ListView_GetHeader(list);
    RECT rect{};
    QIQI_TEST_CHECK(::GetClientRect(header, &rect));
    const ScopedWindowDC screen;
    const ScopedMemoryDC dc(::CreateCompatibleDC(screen.Get()));
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(screen.Get(), rect.right, rect.bottom));
    QIQI_TEST_CHECK(screen && dc && bitmap);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    ::SendMessageW(header, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    const int width = ListView_GetColumnWidth(list, 0);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), width / 2, 2) == RGB(54, 58, 66));
    bool readable_text = false;
    for (int y = 3; y < rect.bottom - 3 && !readable_text; ++y) {
        for (int x = 6; x < width - 6; ++x) {
            const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
            if (pixel != CLR_INVALID && GetRValue(pixel) > 180 && GetGValue(pixel) > 180 && GetBValue(pixel) > 180) {
                readable_text = true;
                break;
            }
        }
    }
    QIQI_TEST_CHECK(readable_text);
}

int IconWidth(HICON icon) {
    QIQI_TEST_CHECK(icon != nullptr);
    ICONINFO info{};
    QIQI_TEST_CHECK(::GetIconInfo(icon, &info));
    const ScopedGdiHandle<HBITMAP> color(info.hbmColor);
    const ScopedGdiHandle<HBITMAP> mask(info.hbmMask);
    BITMAP bitmap{};
    QIQI_TEST_CHECK(::GetObjectW(color.Get(), sizeof(bitmap), &bitmap) == sizeof(bitmap));
    return bitmap.bmWidth;
}

Event MakeEvent(std::wstring text = L"message") {
    FILETIME time{};
    ::GetSystemTimeAsFileTime(&time);
    Event event;
    event.time = (gsl::narrow_cast<uint64_t>(time.dwHighDateTime) << 32) + gsl::narrow_cast<uint64_t>(time.dwLowDateTime);
    event.pid = 42;
    event.tid = 84;
    event.level = 4;
    event.process = L"Code.exe";
    event.name = L"QiQiLogInfo";
    event.message = std::move(text);
    return event;
}

void TestBuffersAndFilters() {
    EventBuffer buffer(2, 4096);
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"first")));
    const uint64_t first = buffer.Events().front().id;
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"second")));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent(L"third")));
    QIQI_TEST_CHECK(buffer.Events().size() == 2 && buffer.Evicted() == 1 && !buffer.Find(first));
    QIQI_TEST_CHECK(buffer.Find(first + 1)->message == L"second");
    const uint64_t last = buffer.Events().back().id;
    buffer.Clear();
    QIQI_TEST_CHECK(buffer.Bytes() == 0 && !buffer.Find(last));
    QIQI_TEST_CHECK(buffer.Push(MakeEvent()));
    QIQI_TEST_CHECK(buffer.Events().front().id > last);
    QIQI_TEST_CHECK(!buffer.Push(MakeEvent(std::wstring(5000, L'x'))));
    QIQI_TEST_CHECK(buffer.Events().size() == 1 && buffer.Evicted() == 2);

    EventBuffer bytes(100, 1024);
    for (int i = 0; i < 100; ++i) bytes.Push(MakeEvent(std::wstring(150, L'x')));
    QIQI_TEST_CHECK(bytes.Bytes() <= 1024 && bytes.Events().size() < 100 && bytes.Evicted() > 0);
    auto exact_event = MakeEvent(std::wstring(150, L'x'));
    const size_t exact_bytes = exact_event.Bytes();
    EventBuffer exact(100, exact_bytes);
    QIQI_TEST_CHECK(exact.Push(std::move(exact_event)) && exact.Bytes() == exact_bytes);
    QIQI_TEST_CHECK(exact.Push(MakeEvent(std::wstring(150, L'x'))));
    QIQI_TEST_CHECK(exact.Events().size() == 1 && exact.Evicted() == 1 && exact.Bytes() == exact_bytes);
    EventBuffer too_small(100, exact_bytes - 1);
    QIQI_TEST_CHECK(!too_small.Push(MakeEvent(std::wstring(150, L'x'))) && too_small.Events().empty());
    EventBuffer capacities(10, 1024);
    auto reserved = MakeEvent();
    reserved.message.reserve(4096);
    QIQI_TEST_CHECK(!capacities.Push(std::move(reserved)));
    QIQI_TEST_CHECK(capacities.Events().empty());

    EventBuffer processes(3, 4096);
    auto first_process = MakeEvent();
    first_process.pid = 10;
    QIQI_TEST_CHECK(processes.Push(first_process));
    QIQI_TEST_CHECK(processes.Push(first_process));
    auto second_process = first_process;
    second_process.pid = 20;
    second_process.process = L"code.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    auto choices = CollectProcessChoices(processes, L"CODE");
    QIQI_TEST_CHECK(choices.names.size() == 1 && choices.pids == std::vector<DWORD>({10, 20}));
    second_process.pid = 30;
    second_process.process = L"notepad.exe";
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").pids == std::vector<DWORD>({10, 20, 30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"notepad").pids == std::vector<DWORD>({30}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"missing").pids.empty());
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"code").pids == std::vector<DWORD>({20}));
    QIQI_TEST_CHECK(processes.Push(second_process));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names == std::vector<std::wstring>({L"notepad.exe"}));
    processes.Clear();
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"").names.empty() &&
                    CollectProcessChoices(processes, L"").pids.empty());
    auto unnamed = MakeEvent();
    unnamed.process.clear();
    unnamed.pid = 0;
    QIQI_TEST_CHECK(processes.Push(unnamed));
    auto maximum_pid = MakeEvent();
    maximum_pid.pid = MAXDWORD;
    QIQI_TEST_CHECK(processes.Push(maximum_pid));
    choices = CollectProcessChoices(processes, L"");
    QIQI_TEST_CHECK(choices.names == std::vector<std::wstring>({L"Code.exe"}));
    QIQI_TEST_CHECK(choices.pids == std::vector<DWORD>({0, MAXDWORD}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L"code.exe").pids == std::vector<DWORD>({MAXDWORD}));
    QIQI_TEST_CHECK(CollectProcessChoices(processes, L" ").pids.empty());

    Filter filter;
    auto event = MakeEvent(L"Case sensitive? no.\tline\nnext");
    const GUID parent_act{0x12345678, 0x9abc, 0xdef0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    const GUID child_act{0x87654321, 0x9abc, 0xdef0, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
    event.keyword = std::to_underlying(log::Keyword::Composition);
    event.opcode = std::to_underlying(log::Opcode::Start);
    event.activity_id = child_act;
    event.related_activity_id = parent_act;
    QIQI_TEST_CHECK(FormatShortGuid(GUID_NULL).empty() && FormatGuid(GUID_NULL).empty());
    QIQI_TEST_CHECK(FormatShortGuid(parent_act) == L"12345678");
    QIQI_TEST_CHECK(FormatGuid(parent_act) == L"{12345678-9abc-def0-1122-334455667788}");
    QIQI_TEST_CHECK(LevelName(1) == L"Critical" && LevelName(3) == L"Warning" &&
                    LevelName(5) == L"Verbose" && LevelName(99) == L"Level 99");
    bool bad_limits = false;
    try { EventBuffer invalid_buf(0, 1024); } catch (const std::invalid_argument&) { bad_limits = true; }
    QIQI_TEST_CHECK(bad_limits);
    bool bad_time = false;
    try { std::ignore = FormatTime(std::numeric_limits<uint64_t>::max()); } catch (const std::runtime_error&) { bad_time = true; }
    QIQI_TEST_CHECK(bad_time);
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.process = L"CODE";
    filter.pid = 42;
    filter.level = 4;
    filter.keyword = std::to_underlying(log::Keyword::Composition);
    filter.activity_id = parent_act; // Matches via related_activity_id!
    filter.text = L"NEXT";
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.activity_id = child_act; // Matches via activity_id!
    QIQI_TEST_CHECK(filter.Matches(event));
    filter.activity_id = GUID_NULL;
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.activity_id.reset();
    filter.keyword = std::to_underlying(log::Keyword::Sync);
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.keyword = 0;
    filter.pid = 43;
    QIQI_TEST_CHECK(!filter.Matches(event));
    filter.pid.reset();
    filter.level = 2;
    QIQI_TEST_CHECK(!filter.Matches(event));
    QIQI_TEST_CHECK(!ParsePid(L"") && !ParsePid(L"-1") && !ParsePid(L"4294967296") && !ParsePid(L"1x"));
    QIQI_TEST_CHECK(ParsePid(L"0") == 0 && ParsePid(L"4294967295") == MAXDWORD);
    const auto exported = ExportEvents({event}, L"test");
    QIQI_TEST_CHECK(exported.contains(version::kFullVersion) && exported.contains(version::kArch));
    QIQI_TEST_CHECK(exported.contains("\\tline\\nnext") &&
                    exported.contains("PID\tTID\tLevel\tCategory\tOpcode\tActivityId\tRelatedActivityId") &&
                    exported.contains("Composition\tStart\t{87654321-9abc-def0-1122-334455667788}\t{12345678-9abc-def0-1122-334455667788}"));
    const auto entry_details = FormatEventEntry(event, L"");
    QIQI_TEST_CHECK(entry_details.contains(L"Composition:Start") &&
                    entry_details.contains(L"Activity={87654321-9abc-def0-1122-334455667788}") &&
                    entry_details.contains(L"Related={12345678-9abc-def0-1122-334455667788}"));
    QIQI_TEST_CHECK(FormatTime(event.time).ends_with(L"Z"));
    auto second_event = MakeEvent(std::wstring(L"multi\0nul", 9));
    second_event.level = 2;
    second_event.pid = 99;
    const auto formatted_clip = FormatEventsForClipboard(std::array{event, second_event});
    QIQI_TEST_CHECK(formatted_clip.contains(
        L"PID=42\tTID=84\tInfo\tComposition\tStart\t{87654321-9abc-def0-1122-334455667788}\tQiQiLogInfo\tCase sensitive? no.\tline\nnext\r\n"));
    QIQI_TEST_CHECK(formatted_clip.contains(
        L"PID=99\tTID=84\tError\tGeneral\tInfo\t\tQiQiLogInfo\tmulti\x2400nul"));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, std::wstring_view(L"clip\0test", 9)) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == L"clip\x2400test");
    std::vector<Event> full_buffer;
    full_buffer.reserve(10'000);
    for (int i = 0; i < 10'000; ++i) {
        full_buffer.push_back(MakeEvent(std::format(L"bulk-record-{} {}", i, std::wstring(256, L'x'))));
    }
    const auto full_copy = FormatEventsForClipboard(full_buffer);
    QIQI_TEST_CHECK(full_copy.size() > 1'000'000);
    QIQI_TEST_CHECK(std::count(full_copy.begin(), full_copy.end(), L'\n') == 9'999);
    QIQI_TEST_CHECK(full_copy.contains(L"bulk-record-0 ") && full_copy.contains(L"bulk-record-9999 "));
    QIQI_TEST_CHECK(CopyTextToClipboard(nullptr, full_copy) == ERROR_SUCCESS);
    QIQI_TEST_CHECK(ReadClipboardText() == full_copy);
    event.message = std::wstring(1, L'\xD800');
    bool invalid = false;
    try { ExportEvents({event}, L"test"); } catch (const std::system_error&) { invalid = true; }
    QIQI_TEST_CHECK(invalid);
    EventQueue queue(5, 4096);
    std::thread producer([&] { for (int i = 0; i < 100; ++i) queue.Push(MakeEvent()); });
    producer.join();
    QIQI_TEST_CHECK(queue.Evicted() == 95 && queue.Take().size() == 5 && queue.Take().empty());
}

void TestExport() {
    const auto path = std::filesystem::temp_directory_path() /
        (L"qiqi-monitor-export-" + std::to_wstring(::GetCurrentProcessId()) + L".log");
    const auto cleanup = gsl::finally([&]() noexcept {
        std::error_code error;
        std::filesystem::remove(path, error);
    });
    const auto bytes = ExportEvents({MakeEvent(std::wstring(L"中文\0\r\tline\nend\\", 14))}, L"test");
    QIQI_TEST_CHECK(bytes.contains("\\0\\r\\tline\\nend\\\\"));
    QIQI_TEST_CHECK(WriteExport(path.c_str(), bytes) == ERROR_SUCCESS);
    std::string content;
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    {
        const ScopedFileHandle lock(::CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        QIQI_TEST_CHECK(lock && WriteExport(path.c_str(), "replacement") == ERROR_SHARING_VIOLATION);
    }
    QIQI_TEST_CHECK(ReadFileToUtf8String(path, content) && content == bytes);
    const auto missing = path / L"invalid.log";
    QIQI_TEST_CHECK(WriteExport(missing.c_str(), bytes) != ERROR_SUCCESS);
}

void TestWindowAndVersion() {
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(::GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA);
    std::array<wchar_t, 256> title{};
    ::GetWindowTextW(window.Handle(), title.data(), gsl::narrow_cast<int>(title.size()));
    QIQI_TEST_CHECK(std::wstring_view(title.data()).contains(version::kVersionW));
    Click(window, Pause);
    Click(window, Source);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    QIQI_TEST_CHECK(::GetWindowTextLengthW(Child(window, Details)) > 0);
    for (int col = 0; col < 7; ++col) {
        std::array<wchar_t, 256> cell_buf{};
        NMLVDISPINFOW disp{};
        disp.hdr.hwndFrom = list;
        disp.hdr.idFrom = gsl::narrow_cast<UINT_PTR>(std::to_underlying(List));
        disp.hdr.code = LVN_GETDISPINFOW;
        disp.item.mask = LVIF_TEXT;
        disp.item.iItem = 0;
        disp.item.iSubItem = col;
        disp.item.pszText = cell_buf.data();
        disp.item.cchTextMax = gsl::narrow_cast<int>(cell_buf.size());
        ::SendMessageW(window.Handle(), WM_NOTIFY, disp.hdr.idFrom, reinterpret_cast<LPARAM>(&disp));
        QIQI_TEST_CHECK(gsl::at(cell_buf, 0) != L'\0');
    }
    ::SendMessageW(Child(window, Process), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_SELCHANGE), 0);
    ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_CLOSEUP), 0);
    for (int col = 1; col <= 8; ++col) {
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, col, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
    }
    ::SendMessageW(Child(window, Category), CB_SETCURSEL, 0, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    {
        const UINT_PTR cancel_menu_timer = ::SetTimer(window.Handle(), 3, 15, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(owner, id);
            ::SendMessageW(owner, WM_CANCELMODE, 0, 0);
        });
        QIQI_TEST_CHECK(cancel_menu_timer != 0);
        const auto kill_timer = gsl::finally([&window, cancel_menu_timer]() noexcept {
            ::KillTimer(window.Handle(), cancel_menu_timer);
        });
        ::SendMessageW(window.Handle(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list), MAKELPARAM(-1, -1));
    }
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    SetProcessFilter(window, L"missing-process");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
    SetProcessFilter(window, L"QiQiIMELogMonitor.exe");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
    Click(window, Clear);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Process), CB_GETCOUNT, 0, 0) == 0);
    SetProcessFilter(window, L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    const int column_width = ListView_GetColumnWidth(list, 0);
    RECT rect{};
    ::GetWindowRect(window.Handle(), &rect);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    CheckComboEditFont(Child(window, Process), dpi);
    const WORD doubled_dpi = gsl::narrow<WORD>(gsl::narrow_cast<uint64_t>(dpi) * 2);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(doubled_dpi, doubled_dpi), reinterpret_cast<LPARAM>(&rect));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == column_width * 2);
    for (const auto id : {Process, Pid, Level, Category}) {
        QIQI_TEST_CHECK(::SendMessageW(Child(window, id), CB_GETITEMHEIGHT, 0, 0) ==
                        ScalePxByDpi(22, doubled_dpi));
        const LRESULT edit_height = ::SendMessageW(Child(window, id), CB_GETITEMHEIGHT,
                                                   gsl::narrow_cast<WPARAM>(-1), 0);
        if (id == Process) {
            CheckComboEditFont(Child(window, Process), doubled_dpi);
        } else {
            QIQI_TEST_CHECK(edit_height == ScalePxByDpi(22, doubled_dpi));
        }
    }
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
    const auto executable = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMELogMonitor.exe";
    DWORD unused = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(executable.c_str(), &unused);
    QIQI_TEST_CHECK(size > 0);
    std::vector<BYTE> resource(size);
    QIQI_TEST_CHECK(::GetFileVersionInfoW(executable.c_str(), 0, size, resource.data()));
    VS_FIXEDFILEINFO* info = nullptr;
    UINT bytes = 0;
    QIQI_TEST_CHECK(::VerQueryValueW(resource.data(), L"\\", reinterpret_cast<void**>(&info), &bytes));
    QIQI_TEST_CHECK(info && info->dwFileType == VFT_APP);
    QIQI_TEST_CHECK(HIWORD(info->dwFileVersionMS) == version::kMajor &&
                   LOWORD(info->dwFileVersionMS) == version::kMinor &&
                   HIWORD(info->dwFileVersionLS) == version::kPatch);
    QIQI_TEST_CHECK(::DestroyWindow(window.Handle()));
    QIQI_TEST_CHECK(window.Handle() == nullptr);
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

void TestDetailsSplitter(const MonitorWindow& window) {
    const HWND splitter = Child(window, DetailsSplitter);
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(splitter);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 118) == nullptr);
    const auto bounds = [](HWND control) {
        RECT rect{};
        QIQI_TEST_CHECK(::GetWindowRect(control, &rect));
        return rect;
    };
    const auto height = [&](HWND control) {
        const RECT rect = bounds(control);
        return rect.bottom - rect.top;
    };
    const int count = ListView_GetItemCount(list);
    const int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    const auto details = ControlText(Child(window, Details));
    const auto search = ControlText(Child(window, Search));
    const auto pid = ControlText(Child(window, Pid));
    const auto level = ::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0);
    const UINT dpi = ::GetDpiForWindow(window.Handle());
    const int initial_height = height(Child(window, Details));
    const int initial_list_height = height(list);
    const auto drag = [&](int delta) {
        const int center = height(splitter) / 2;
        ::SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, center));
        QIQI_TEST_CHECK(::GetCapture() == splitter);
        ::SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, gsl::narrow_cast<int64_t>(center) + delta));
        ::SendMessageW(splitter, WM_LBUTTONUP, 0, MAKELPARAM(10, center));
        QIQI_TEST_CHECK(::GetCapture() != splitter);
    };
    ::ShowWindow(window.Handle(), SW_SHOWNOACTIVATE);
    QIQI_TEST_CHECK(::SendMessageW(splitter, WM_GETDLGCODE, 0, 0) == DLGC_WANTARROWS);
    ::SendMessageW(splitter, WM_SETCURSOR, reinterpret_cast<WPARAM>(splitter), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    QIQI_TEST_CHECK(::GetCursor() == ::LoadCursorW(nullptr, IDC_SIZENS));
    drag(-ScalePxByDpi(50, dpi));
    QIQI_TEST_CHECK(height(Child(window, Details)) > initial_height);
    QIQI_TEST_CHECK(height(list) < initial_list_height);
    const RECT list_rect = bounds(list);
    const RECT splitter_rect = bounds(splitter);
    QIQI_TEST_CHECK(list_rect.bottom == splitter_rect.top);
    QIQI_TEST_CHECK(splitter_rect.bottom == bounds(Child(window, Details)).top);
    drag(ScalePxByDpi(50, dpi));
    QIQI_TEST_CHECK(std::abs(height(Child(window, Details)) - initial_height) <= 1);
    drag(-10000);
    QIQI_TEST_CHECK(height(list) >= ScalePxByDpi(60, dpi));
    const int largest = height(Child(window, Details));
    drag(10000);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    ::SendMessageW(splitter, WM_KEYDOWN, VK_END, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == largest);
    ::SendMessageW(splitter, WM_KEYDOWN, VK_HOME, 0);
    ::SendMessageW(splitter, WM_KEYDOWN, VK_UP, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(70, dpi));
    ::SendMessageW(splitter, WM_KEYDOWN, VK_DOWN, 0);
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    ::SendMessageW(splitter, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 2));
    QIQI_TEST_CHECK(::GetCapture() == splitter && ::ReleaseCapture());
    ::SendMessageW(splitter, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, -100));
    QIQI_TEST_CHECK(height(Child(window, Details)) == ScalePxByDpi(60, dpi));
    for (int i = 0; i < 7; ++i) ::SendMessageW(splitter, WM_KEYDOWN, VK_UP, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == count);
    QIQI_TEST_CHECK(ListView_GetNextItem(list, -1, LVNI_SELECTED) == selected);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)) == details);
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == search);
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == pid);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0) == level);
}

void TestDesktopIntegration() {
    SetThemeDarkOverrideForTest(false);
    SetHighContrastOverrideForTest(false);
    const auto restore = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
        SetMonitorMetricsOverrideForTest(std::nullopt);
    });
    const RECT negative = ClampWindowRectToWorkArea({-2200, -1200, 100, 100}, {-1600, -900, 0, 0});
    QIQI_TEST_CHECK(negative.left == -1600 && negative.top == -900 && negative.right == 0 && negative.bottom == 0);
    const RECT inside = ClampWindowRectToWorkArea({-1400, -800, -500, -100}, {-1600, -900, 0, 0});
    QIQI_TEST_CHECK(inside.left == -1400 && inside.top == -800 && inside.right == -500);
    MonitorWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
    const HWND list = Child(window, List);
    const LONG_PTR list_style = ::GetWindowLongPtrW(list, GWL_STYLE);
    QIQI_TEST_CHECK((list_style & LVS_OWNERDATA) != 0 && (list_style & LVS_SINGLESEL) == 0);
    const UINT initial_dpi = ::GetDpiForWindow(window.Handle());
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_BIG, 0))) == ScalePxByDpi(48, initial_dpi));
    QIQI_TEST_CHECK(Child(window, Copy) == nullptr && Child(window, Toolbar) != nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 122) == nullptr);
    QIQI_TEST_CHECK(::GetDlgItem(window.Handle(), 123) == nullptr);
    SelectLanguage(window, UiLanguage::EnUs);
    QIQI_TEST_CHECK(CommandName(window, Clear) == L"Clear");
    QIQI_TEST_CHECK(CommandName(window, Copy) == L"Copy");
    QIQI_TEST_CHECK(CommandName(window, LanguageEn) == GetUiStrings(UiLanguage::EnUs).dlg_lang_en);
    Click(window, Source);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    SelectPid(window, ::GetCurrentProcessId());
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    Click(window, Copy);
    const auto single_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(single_copied.contains(L"Monitor initialized") && single_copied.contains(L"PID="));
    // Populate diagnostics without repeatedly accessing the system clipboard.
    for (int i = 0; i < 450; ++i) {
        ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    }
    SelectLanguage(window, UiLanguage::EnUs);
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 0, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SetWindowTextW(Child(window, Search), L"");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) >= 405);
    ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 405);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"Selected entries: "));
    Click(window, Copy);
    const auto multi_copied = ReadClipboardText(window.Handle());
    QIQI_TEST_CHECK(multi_copied.contains(L"Monitor initialized") && multi_copied.contains(L"Invalid PID selection") && multi_copied.contains(L"\r\n"));
    ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == 0);
    {
        ::SendMessageW(Child(window, Level), CB_SETCURSEL, 0, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
        std::array<BYTE, 256> keys{};
        QIQI_TEST_CHECK(::GetKeyboardState(keys.data()));
        const auto restore_keys = gsl::finally([&keys]() noexcept {
            ::SetKeyboardState(keys.data());
        });
        std::array<BYTE, 256> ctrl_down = keys;
        gsl::at(ctrl_down, VK_CONTROL) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(ctrl_down.data()));
#pragma warning(suppress : 26496) // Mutated by the Win32 subclass callback through dwRefData.
        unsigned int detail_updates = 0;
        const HWND details = Child(window, Details);
        const SUBCLASSPROC count_updates =
            [](HWND control, UINT message, WPARAM wparam, LPARAM lparam,
               UINT_PTR, DWORD_PTR context) noexcept -> LRESULT {
                if (message == WM_SETTEXT) ++*reinterpret_cast<unsigned int*>(context);
                return ::DefSubclassProc(control, message, wparam, lparam);
            };
        QIQI_TEST_CHECK(::SetWindowSubclass(details, count_updates, 91,
                                           reinterpret_cast<DWORD_PTR>(&detail_updates)));
        const auto remove_counter = gsl::finally([=]() noexcept {
            ::RemoveWindowSubclass(details, count_updates, 91);
        });
        const auto select_begin = std::chrono::steady_clock::now();
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        QIQI_TEST_CHECK(std::chrono::steady_clock::now() - select_begin < std::chrono::seconds(1));
        QIQI_TEST_CHECK(detail_updates == 1);
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) >= 405);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        const auto bulk_copied = ReadClipboardText(window.Handle());
        QIQI_TEST_CHECK(bulk_copied.contains(L"Monitor initialized"));
        const auto copied_count = std::count(bulk_copied.begin(), bulk_copied.end(), L'\n');
        QIQI_TEST_CHECK(gsl::narrow_cast<UINT>(copied_count + 1) == ListView_GetSelectedCount(list));
        ::SendMessageW(list, WM_KEYDOWN, VK_INSERT, 0);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        Click(window, Copy);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        QIQI_TEST_CHECK(ReadClipboardText(window.Handle()) == bulk_copied);
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
        QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
        const auto refreshed_copy = ReadClipboardText(window.Handle());
        size_t record_count = 0;
        const std::wstring record_marker = std::format(L"\tPID={}\tTID=", ::GetCurrentProcessId());
        for (size_t position = 0; (position = refreshed_copy.find(record_marker, position)) != std::wstring::npos;
             position += record_marker.size()) {
            ++record_count;
        }
        QIQI_TEST_CHECK(record_count == gsl::narrow_cast<size_t>(ListView_GetItemCount(list)));
        QIQI_TEST_CHECK(refreshed_copy.contains(L"Monitor initialized"));
        QIQI_TEST_CHECK(refreshed_copy.contains(std::format(L"Copied {} log record(s) to clipboard",
                                                          copied_count + 1)));
        QIQI_TEST_CHECK(refreshed_copy.size() > bulk_copied.size());
        ::SendMessageW(Child(window, Level), CB_SETCURSEL, 4, 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Level), CBN_SELCHANGE), 0);
    }
    ::SendMessageW(Child(window, Pid), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(-1), 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Pid), CBN_SELCHANGE), 0);
    SelectLanguage(window, UiLanguage::EnUs);
    ::SetWindowTextW(Child(window, Search), L"initialized");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetSelectedCount(list) == 1);
    Click(window, Pause);
    SelectLanguage(window, UiLanguage::ZhCn);
    QIQI_TEST_CHECK(CommandName(window, Clear) == L"清空");
    QIQI_TEST_CHECK(CommandName(window, Copy) == L"复制");
    QIQI_TEST_CHECK(CommandName(window, LanguageZh) == GetUiStrings(UiLanguage::ZhCn).dlg_lang_zh);
    QIQI_TEST_CHECK(CommandName(window, Pause) == L"恢复自动滚动");
    QIQI_TEST_CHECK(CommandName(window, Source) == L"输入法日志");
    QIQI_TEST_CHECK(ControlText(window.Handle()).contains(L"奇奇日志监控"));
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == L"initialized");
    QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    QIQI_TEST_CHECK(::SendMessageW(Child(window, Level), CB_GETCURSEL, 0, 0) == 4);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    QIQI_TEST_CHECK(ControlText(Child(window, Details)).contains(L"信息") &&
        ControlText(Child(window, Details)).contains(L"Monitor initialized"));
    QIQI_TEST_CHECK(ControlText(Child(window, Status)).contains(L"解码失败"));
    QIQI_TEST_CHECK(ControlText(Child(window, DetailsSplitter)) == L"调整详情区高度");
    TestDetailsSplitter(window);
    SetThemeDarkOverrideForTest(true);
    for (const UINT message : {WM_THEMECHANGED, WM_SETTINGCHANGE, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED}) {
        ::SendMessageW(window.Handle(), message, 0, 0);
        QIQI_TEST_CHECK(ListView_GetBkColor(list) == RGB(43, 46, 52));
        QIQI_TEST_CHECK(ListView_GetTextColor(list) == RGB(238, 240, 245));
        QIQI_TEST_CHECK(CommandName(window, Clear) == L"清空");
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    }
    TestDarkHeader(list);
    SetHighContrastOverrideForTest(true);
    ::SendMessageW(window.Handle(), WM_SYSCOLORCHANGE, 0, 0);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == ::GetSysColor(COLOR_WINDOW));
    QIQI_TEST_CHECK(ListView_GetTextColor(list) == ::GetSysColor(COLOR_WINDOWTEXT));
    SetHighContrastOverrideForTest(false);
    SetThemeDarkOverrideForTest(false);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    QIQI_TEST_CHECK(ListView_GetBkColor(list) == RGB(255, 255, 255));
    SelectLanguage(window, UiLanguage::Auto);
    QIQI_TEST_CHECK(CommandName(window, LanguageAuto) == GetUiStrings(UiLanguage::Auto).dlg_lang_auto);
    QIQI_TEST_CHECK(CommandName(window, Clear) ==
        (ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs ? L"Clear" : L"清空"));
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, 0, 0);
    QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(LanguageAuto), MF_BYCOMMAND) & MF_CHECKED) != 0);
    const int original_column = ListView_GetColumnWidth(list, 0);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{-2560, 0, 0, 1440}, 192});
    RECT proposed{-2700, -100, 100, 1600};
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&proposed));
    RECT actual{};
    ::GetWindowRect(window.Handle(), &actual);
    // The real desktop may impose a smaller maximum track size than the simulated monitor.
    QIQI_TEST_CHECK(actual.left >= -2560 && actual.top >= 0 && actual.right <= 0 && actual.bottom <= 1440);
    QIQI_TEST_CHECK(actual.right > actual.left && actual.bottom > actual.top);
    const int scaled_column = ::MulDiv(original_column, 192, gsl::narrow_cast<int>(initial_dpi));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == scaled_column);
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_BIG, 0))) == 96);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(192, 192), reinterpret_cast<LPARAM>(&proposed));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(list, 0) == scaled_column);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{80, 40, 1200, 820}, 96});
    ::SendMessageW(window.Handle(), WM_DISPLAYCHANGE, 0, 0);
    ::GetWindowRect(window.Handle(), &actual);
    QIQI_TEST_CHECK(actual.left >= 80 && actual.top >= 40 && actual.right <= 1200 && actual.bottom <= 820);
    QIQI_TEST_CHECK(IconWidth(reinterpret_cast<HICON>(::SendMessageW(window.Handle(), WM_GETICON, ICON_SMALL, 0))) == 16);
    SetMonitorMetricsOverrideForTest(MonitorMetrics{{100, 70, 900, 570}, 96});
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    ::GetWindowRect(window.Handle(), &actual);
    QIQI_TEST_CHECK(actual.left >= 100 && actual.top >= 70 && actual.right <= 900 && actual.bottom <= 570);
    QIQI_TEST_CHECK(ControlText(Child(window, Search)) == L"initialized");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && ListView_GetNextItem(list, -1, LVNI_SELECTED) == 0);
    const auto executable = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMELogMonitor.exe";
    const ScopedModule resource(::LoadLibraryExW(executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
    QIQI_TEST_CHECK(resource);
    const HRSRC group = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    QIQI_TEST_CHECK(group);
    const HGLOBAL loaded = ::LoadResource(resource.Get(), group);
    QIQI_TEST_CHECK(loaded != nullptr);
    const std::span<const BYTE> bytes(static_cast<const BYTE*>(::LockResource(loaded)), ::SizeofResource(resource.Get(), group));
    QIQI_TEST_CHECK(bytes.size() >= 6);
    WORD count = 0;
    std::memcpy(&count, bytes.subspan(4, 2).data(), 2);
    QIQI_TEST_CHECK(bytes.size() >= 6 + size_t{count} * 14);
    bool high_resolution = false;
    WORD monitor_icon_id = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto entry = bytes.subspan(6 + i * 14, 14);
        if (entry.front() == 0 && gsl::at(entry, 1) == 0) {
            high_resolution = true;
            std::memcpy(&monitor_icon_id, entry.subspan(12, 2).data(), 2);
        }
    }
    QIQI_TEST_CHECK(high_resolution && monitor_icon_id != 0);
    const auto settings_exe = GetModuleFilePath(nullptr).parent_path() / L"QiQiIMESettings.exe";
    if (std::filesystem::exists(settings_exe)) {
        const ScopedModule settings_res(::LoadLibraryExW(settings_exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE));
        QIQI_TEST_CHECK(settings_res.IsValid());
        const HRSRC mon_frame = ::FindResourceW(resource.Get(), MAKEINTRESOURCEW(monitor_icon_id), RT_ICON);
        const HRSRC set_frame = ::FindResourceW(settings_res.Get(), MAKEINTRESOURCEW(monitor_icon_id), RT_ICON);
        QIQI_TEST_CHECK(mon_frame != nullptr && set_frame != nullptr);
        const DWORD mon_size = ::SizeofResource(resource.Get(), mon_frame);
        const DWORD set_size = ::SizeofResource(settings_res.Get(), set_frame);
        const auto* mon_ptr = static_cast<const BYTE*>(::LockResource(::LoadResource(resource.Get(), mon_frame)));
        const auto* set_ptr = static_cast<const BYTE*>(::LockResource(::LoadResource(settings_res.Get(), set_frame)));
        QIQI_TEST_CHECK(mon_ptr != nullptr && set_ptr != nullptr);
        const bool distinct_icons = (mon_size != set_size) || (std::memcmp(mon_ptr, set_ptr, mon_size) != 0);
        QIQI_TEST_CHECK(distinct_icons);
    }
    QIQI_TEST_CHECK(!log::IsEtwProviderRegistered());
}

int Integration(const wchar_t* peer) {
    Diagnostics diagnostics;
    EventQueue queue{32, 128 * 1024};
    Collector collector(queue, diagnostics);
    // Initialize with default Level::info to verify that Collector's TRACE_LEVEL_VERBOSE session
    // receives Debug/Trace events without requiring SetLevel(Level::trace/debug).
    log::InitLogger(log::Level::info);
    const auto shutdown = gsl::finally([]() noexcept { log::Shutdown(); });
    if (!collector.Start()) {
        const auto errors = diagnostics.queue.Take();
        for (const auto& error : errors) std::wcerr << error.message << L"\n";
        const auto& failure = collector.LastStartFailure();
        if (failure && failure->IsPermissionDenied()) return 77;
        QIQI_TEST_CHECK(false);
    }
    const std::wstring session_name(collector.SessionName());
    const std::string marker = std::string("QiQi monitor exact UTF-8: \xe4\xbd\xa0\xe5\xa5\xbd\tline\nend") + std::string("\0tail\0", 6);
    const GUID parent_act = log::CreateActivityId();
    const GUID child_act = log::CreateActivityId();
    log::WriteLogMessage(
        log::Level::info,
        marker,
        log::LogContext{log::Keyword::Sync, log::Opcode::Start, &child_act, &parent_act});
    QIQI_LOG_ACT_STOP(log::Level::debug, log::Keyword::Sync, &child_act, "monitor-verbose-with-default-info-level");
    diagnostics.Record(2, L"MONITOR_ONLY_SENTINEL", ERROR_INVALID_DATA);
    log::Shutdown();
    QIQI_TEST_CHECK(log::EnsureInitialized());
    log::WriteLogMessage(log::Level::warn, "after provider re-registration");

    const auto executable = peer ? std::filesystem::path(peer) : GetModuleFilePath(nullptr);
    std::wstring command = L"\"" + executable.wstring() + L"\" --emit";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const ScopedKernelHandle child(process.hProcess);
    const ScopedKernelHandle thread(process.hThread);
    QIQI_TEST_CHECK(::WaitForSingleObject(child.Get(), 10'000) == WAIT_OBJECT_0);
    DWORD exit = 1;
    QIQI_TEST_CHECK(::GetExitCodeProcess(child.Get(), &exit) && exit == 0);
    std::vector<Event> received;
    for (int i = 0; i < 60; ++i) {
        auto batch = queue.Take();
        for (auto& event : batch) received.push_back(std::move(event));
        const bool own = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.message == Utf8ToWide(marker);
        });
        const bool own_verbose = std::any_of(received.begin(), received.end(), [](const Event& event) {
            return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5;
        });
        const bool remote = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-emitter";
        });
        const bool remote_verbose = std::any_of(received.begin(), received.end(), [&](const Event& event) {
            return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" && event.level == 5;
        });
        if (own && own_verbose && remote && remote_verbose) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    collector.PollLoss();
    QIQI_TEST_CHECK(collector.Stop());
    if (collector.decode_errors != 0 || collector.events_lost != 0) {
        std::cerr << "ETW counters: decode=" << collector.decode_errors.load()
                  << ", events_lost=" << collector.events_lost.load()
                  << ", buffers_lost=" << collector.buffers_lost.load() << "\n";
    }
    QIQI_TEST_CHECK(!collector.Running() && collector.decode_errors == 0 && collector.events_lost == 0);
    const auto own = std::find_if(received.begin(), received.end(), [&](const Event& event) {
        return event.message == Utf8ToWide(marker);
    });
    QIQI_TEST_CHECK(own != received.end());
    std::wcout << L"Decoded event: " << own->name << L", UTC=" << FormatTime(own->time)
               << L", now=" << FormatTime(MakeEvent().time) << std::endl;
    QIQI_TEST_CHECK(own->pid == ::GetCurrentProcessId() && own->tid == ::GetCurrentThreadId());
    QIQI_TEST_CHECK(own->level == 4 && !own->process.empty());
    QIQI_TEST_CHECK(own->keyword == std::to_underlying(log::Keyword::Sync));
    QIQI_TEST_CHECK(own->opcode == std::to_underlying(log::Opcode::Start));
    QIQI_TEST_CHECK(own->activity_id == child_act && own->related_activity_id == parent_act);
    QIQI_TEST_CHECK(own->name == L"QiQiLogInfo");
    QIQI_TEST_CHECK(own->time <= MakeEvent().time && MakeEvent().time - own->time < 60ULL * 10'000'000);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.message == L"monitor-verbose-with-default-info-level" && event.level == 5 &&
               event.keyword == std::to_underlying(log::Keyword::Sync) &&
               event.opcode == std::to_underlying(log::Opcode::Stop) &&
               event.activity_id == child_act && event.related_activity_id == GUID_NULL &&
               event.name == L"QiQiLogVerbose";
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-emitter" &&
               event.keyword == std::to_underlying(log::Keyword::General) &&
               event.opcode == std::to_underlying(log::Opcode::Info) &&
               event.activity_id == GUID_NULL;
    }));
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [&](const Event& event) {
        return event.pid == process.dwProcessId && event.message == L"child-debug-default-info" &&
               event.level == 5 && event.keyword == std::to_underlying(log::Keyword::Lifecycle) &&
               event.name == L"QiQiLogVerbose";
    }));
    QIQI_TEST_CHECK(std::none_of(received.begin(), received.end(), [](const Event& event) {
        return event.message.contains(L"MONITOR_ONLY_SENTINEL");
    }));
    struct Properties {
        EVENT_TRACE_PROPERTIES trace{};
        std::array<wchar_t, 256> name{};
    } query;
    query.trace.Wnode.BufferSize = sizeof(query);
    query.trace.LoggerNameOffset = offsetof(Properties, name);
    QIQI_TEST_CHECK(::ControlTraceW(0, session_name.c_str(), &query.trace, EVENT_TRACE_CONTROL_QUERY) == ERROR_WMI_INSTANCE_NOT_FOUND);
    QIQI_TEST_CHECK(std::any_of(received.begin(), received.end(), [](const Event& event) {
        return event.message == L"after provider re-registration" && event.level == 3;
    }));

    {
        MonitorWindow window;
        QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr)));
        SelectLanguage(window, UiLanguage::EnUs);
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Stop capture");
        GUID gui_activity{};
        QIQI_TEST_CHECK(SUCCEEDED(::CoCreateGuid(&gui_activity)));
        log::WriteLogMessage(log::Level::info, "filter-parent",
            log::LogContext{log::Keyword::Sync, log::Opcode::Start, &gui_activity});
        SetProcessFilter(window, own->process.c_str());
        const auto parent_pid = std::to_wstring(::GetCurrentProcessId());
        for (int i = 0; i < 50 && ControlText(Child(window, Pid)) != parent_pid; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
        const LRESULT name_index = ::SendMessageW(Child(window, Process), CB_FINDSTRINGEXACT,
            gsl::narrow_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(own->process.c_str()));
        QIQI_TEST_CHECK(name_index != CB_ERR);
        ::SendMessageW(Child(window, Process), CB_SETCURSEL, gsl::narrow_cast<WPARAM>(name_index), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)) == own->process);
        ::SetWindowTextW(Child(window, Search), L"filter-parent");
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(FilterActivity), MF_BYCOMMAND) & MF_GRAYED) == 0);
        Click(window, FilterActivity);
        QIQI_TEST_CHECK(ControlText(Child(window, Status)).contains(L"Activity filter"));
        QIQI_TEST_CHECK((::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(ClearActivity), MF_BYCOMMAND) & MF_GRAYED) == 0);
        Click(window, ClearActivity);
        QIQI_TEST_CHECK(!ControlText(Child(window, Status)).contains(L"Activity filter"));
        ::SetWindowTextW(Child(window, Search), L"");

        const HWND process_combo = Child(window, Process);
        QIQI_TEST_CHECK(::SetWindowSubclass(process_combo, SimulateOpenDropdown, 92, 0));
        const auto restore_dropdown = gsl::finally([=]() noexcept {
            ::RemoveWindowSubclass(process_combo, SimulateOpenDropdown, 92);
        });
        PROCESS_INFORMATION second_child{};
        QIQI_TEST_CHECK(::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &second_child));
        const ScopedKernelHandle second_process(second_child.hProcess);
        const ScopedKernelHandle second_thread(second_child.hThread);
        QIQI_TEST_CHECK(::WaitForSingleObject(second_process.Get(), 10'000) == WAIT_OBJECT_0);
        DWORD second_exit = 1;
        QIQI_TEST_CHECK(::GetExitCodeProcess(second_process.Get(), &second_exit) && second_exit == 0);
        // Drain captured child events while refresh is deferred; no new events are needed on close.
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Process), CBN_CLOSEUP), 0);
        QIQI_TEST_CHECK(::RemoveWindowSubclass(process_combo, SimulateOpenDropdown, 92));
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        for (int i = 0; i < 50 && ::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) != 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        }
        QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, 2, 0); // Lifecycle
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        ::SendMessageW(Child(window, Category), CB_SETCURSEL, 0, 0); // All
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(Category), CBN_SELCHANGE), 0);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        ::SetWindowTextW(Child(window, Search), L"child-debug");
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 3);
        SelectLanguage(window, UiLanguage::ZhCn);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"停止采集");
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(second_child.dwProcessId));
        SelectLanguage(window, UiLanguage::EnUs);
        ::SetWindowTextW(Child(window, Search), L"");
        SetProcessFilter(window, L"");
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SelectPid(window, second_child.dwProcessId);
        QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 2);
        Click(window, Start);
        QIQI_TEST_CHECK(CommandName(window, Start) == L"Start capture");
        Click(window, Clear);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Process), CB_GETCOUNT, 0, 0) == 0);
        QIQI_TEST_CHECK(::SendMessageW(Child(window, Pid), CB_GETCOUNT, 0, 0) == 1);
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        Click(window, Source);
        QIQI_TEST_CHECK(ControlText(Child(window, Process)).empty());
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == L"All");
        SetProcessFilter(window, L"QiQiIMELogMonitor.exe");
        QIQI_TEST_CHECK(!::IsWindowEnabled(Child(window, Pid)));
        QIQI_TEST_CHECK(ControlText(Child(window, Pid)) == std::to_wstring(::GetCurrentProcessId()));
    }

    QIQI_TEST_CHECK(collector.Start());
    for (int i = 0; i < 1024; ++i) log::WriteLogMessage(log::Level::info, "burst-monitor");
    for (int i = 0; i < 30 && queue.Evicted() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    QIQI_TEST_CHECK(queue.Evicted() > 0 && queue.Take().size() == 32);
    QIQI_TEST_CHECK(collector.Stop());
    QIQI_TEST_CHECK(collector.decode_errors == 0);
    QIQI_TEST_CHECK(collector.Stop());
    qiqi::monitor::permission_tests::RunCaptureStartupRollbackTests();
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    QIQI_TEST_CHECK(argv != nullptr);
    const std::span<wchar_t*> args(argv, gsl::narrow_cast<size_t>(argc));
    if (args.size() == 2 && (std::wstring_view(gsl::at(args, 1)) == L"--preview-dark" ||
        std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast")) {
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        SetThemeDarkOverrideForTest(true);
        SetHighContrastOverrideForTest(std::wstring_view(gsl::at(args, 1)) == L"--preview-contrast");
        return RunMonitor(::GetModuleHandleW(nullptr), SW_SHOWNORMAL);
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--emit") {
        qiqi::log::InitLogger(qiqi::log::Level::info);
        QIQI_LOG_CAT_DEBUG(qiqi::log::Keyword::Lifecycle, "child-debug-default-info");
        qiqi::log::WriteLogMessage(qiqi::log::Level::info, "child-emitter");
        qiqi::log::Shutdown();
        return 0;
    }
    if (args.size() >= 2 && std::wstring_view(gsl::at(args, 1)) == L"--integration") {
        return Integration(args.size() == 3 ? gsl::at(args, 2) : nullptr);
    }
    TestBuffersAndFilters();
    qiqi::monitor::permission_tests::RunPermissionRecoveryTests();
    TestExport();
    TestScrollIcons();
    TestAboutDialogs();
    TestWindowAndVersion();
    TestMenusAndToolbar({0, 0, 3840, 2160});
    TestMenusAndToolbar({0, 0, 1024, 728});
    TestDesktopIntegration();
    std::cout << "[PASS] Log monitor model, GUI and version tests\n";
    return 0;
}
