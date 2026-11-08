#include "common/test_support.h"
#include "common/config.h"
#include "common/command_ui.h"
#include "common/d2d_controls.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/win32_ui_util.h"
#include "ime/engine/engine.h"
#include "tools/dict_manager/model.h"
#include "tools/dict_manager/editor.h"
#include "tools/dict_manager/window.h"
#include "tools/dict_manager/internal.h"

#include <commctrl.h>
#include <sddl.h>
#include <winver.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <span>

namespace {
using namespace qiqi;
using namespace qiqi::dict_manager;
using enum qiqi::dict_manager::ControlId;

namespace layout = dict_manager_layout;
static_assert(kRefreshTimerId == 1 && kRefreshIntervalMs == 500);
static_assert(layout::kInitialWindowSize.cx == 1020 && layout::kInitialWindowSize.cy == 680);
static_assert(layout::kSourceLabel.left == 12 && layout::kSourceLabel.top == 4 &&
              layout::kSourceLabel.right == 68 && layout::kSourceLabel.bottom == 28);
static_assert(layout::kSourceCombo.left == 72 && layout::kSourceCombo.top == 0 &&
              layout::kSourceCombo.right == 237 && layout::kSourceCombo.bottom == 220);
static_assert(layout::kSearchLabel.left == 250 && layout::kSearchLabel.top == 4 &&
              layout::kSearchLabel.right == 314 && layout::kSearchLabel.bottom == 28);
static_assert(layout::kSearchEditLeft == 318 && layout::kSearchEditRightReserve == 330);
static_assert(layout::kSearchEditHeight == 28 && layout::kWindowMargin == 12);
static_assert(layout::kFilterSectionHeight == 40 && layout::kListMinimumHeight == 80);
static_assert(layout::kListBottomInset == 40 && layout::kStatusBottomOffset == 32 && layout::kStatusHeight == 24);
static_assert(layout::kInlineSaveButtonSize == 26 && layout::kEditIconSize == 16);
static_assert(layout::kEditIconPadding == 4 && layout::kEditIconStrokeWidth == 1 && layout::kEditorCellInsetPx == 1);

HWND Child(const DictManagerWindow& window, ControlId id) noexcept {
    return ::GetDlgItem(window.Handle(), std::to_underlying(id));
}

RECT ToolbarItemRect(const DictManagerWindow& window, ControlId id) {
#pragma warning(suppress : 26496) // TB_GETRECT writes through LPARAM.
    RECT rect{};
    QIQI_TEST_CHECK(::SendMessageW(Child(window, id == SaveWord ? InlineSave : Toolbar), TB_GETRECT,
        std::to_underlying(id), reinterpret_cast<LPARAM>(&rect)));
    return rect;
}

void InvokeCommand(const DictManagerWindow& window, ControlId id, bool toolbar = false) {
    if (toolbar) {
        const HWND target = Child(window, id == SaveWord ? InlineSave : Toolbar);
        QIQI_TEST_CHECK(::SendMessageW(target, TB_COMMANDTOINDEX, std::to_underlying(id), 0) >= 0);
        const auto rect = ToolbarItemRect(window, id);
        const LPARAM point = MAKELPARAM((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
        ::SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, point);
        ::SendMessageW(target, WM_LBUTTONUP, 0, point);
        return;
    }
    ::SendMessageW(window.Handle(), WM_COMMAND, std::to_underlying(id), 0);
}

std::wstring MenuText(HMENU menu, ControlId id) {
    std::array<wchar_t, 256> text{};
    QIQI_TEST_CHECK(::GetMenuStringW(menu, std::to_underlying(id), text.data(),
        gsl::narrow_cast<int>(text.size()), MF_BYCOMMAND) > 0);
    return text.data();
}

std::wstring ControlText(HWND hwnd) {
    std::wstring text(gsl::narrow_cast<size_t>(::GetWindowTextLengthW(hwnd)) + 1, L'\0');
    const int copied = ::GetWindowTextW(hwnd, text.data(), gsl::narrow_cast<int>(text.size()));
    text.resize(gsl::narrow_cast<size_t>(copied));
    return text;
}

void DrainQuitMessages() noexcept {
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

void ClickRowEdit(const DictManagerWindow& window, int row) {
    const HWND list = Child(window, List);
    ::SetActiveWindow(window.Handle());
    ListView_EnsureVisible(list, row, FALSE);
    RECT rect{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
    QIQI_TEST_CHECK(ListView_GetItemRect(list, row, &rect, LVIR_BOUNDS));
    const POINT point{rect.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) / 2,
        (rect.top + rect.bottom) / 2};
    LVHITTESTINFO hit{};
    hit.pt = point;
    QIQI_TEST_CHECK(ListView_SubItemHitTest(list, &hit) == row);
    QIQI_TEST_CHECK(hit.iSubItem == std::to_underlying(ColumnId::Edit));
    const LPARAM position = MAKELPARAM(point.x, point.y);
    ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, position);
    QIQI_TEST_CHECK(::GetCapture() == list);
    ::SendMessageW(list, WM_LBUTTONUP, 0, position);
    QIQI_TEST_CHECK(::GetCapture() != list);
    QIQI_TEST_CHECK(::GetFocus() == Child(window, EditCodeInput));
}

void CheckLayout(const DictManagerWindow& window, UINT dpi) {
    QIQI_TEST_CHECK(Header_GetItemCount(ListView_GetHeader(Child(window, List))) ==
        std::to_underlying(ColumnId::Count));
    QIQI_TEST_CHECK(ListView_GetColumnWidth(Child(window, List), std::to_underlying(ColumnId::Edit)) ==
        ScalePxByDpi(40, dpi));
    constexpr std::array labels{SourceLabel, SearchLabel};
    for (const auto id : labels) {
        const HWND child = Child(window, id);
        const ScopedWindowDC dc(child);
        QIQI_TEST_CHECK(dc);
        const ScopedSelectObject font(dc.Get(), reinterpret_cast<HFONT>(::SendMessageW(child, WM_GETFONT, 0, 0)));
        QIQI_TEST_CHECK(font);
        const auto text = ControlText(child);
        SIZE size{};
        QIQI_TEST_CHECK(::GetTextExtentPoint32W(dc.Get(), text.data(), gsl::narrow_cast<int>(text.size()), &size));
        RECT rect{};
        QIQI_TEST_CHECK(::GetClientRect(child, &rect));
        QIQI_TEST_CHECK(size.cx <= rect.right && size.cy <= rect.bottom);
    }
    const HWND toolbar = Child(window, Toolbar);
    QIQI_TEST_CHECK(toolbar != nullptr);
    const ScopedWindowDC dc(toolbar);
    QIQI_TEST_CHECK(dc);
    const auto toolbar_font = reinterpret_cast<HFONT>(::SendMessageW(toolbar, WM_GETFONT, 0, 0));
    const ScopedSelectObject font(dc.Get(), toolbar_font);
    QIQI_TEST_CHECK(font);
    LOGFONTW logical_font{};
    QIQI_TEST_CHECK(::GetObjectW(toolbar_font, sizeof(logical_font), &logical_font) == sizeof(logical_font));
    QIQI_TEST_CHECK(-logical_font.lfHeight == ScalePxByDpi(14, dpi));
    const HMENU menu = ::GetMenu(window.Handle());
    QIQI_TEST_CHECK(menu && ::GetMenuItemCount(menu) == 4);
    for (UINT i = 0; i < 4; ++i) {
        std::array<wchar_t, 64> heading{};
        QIQI_TEST_CHECK(::GetMenuStringW(menu, i, heading.data(), gsl::narrow_cast<int>(heading.size()), MF_BYPOSITION));
        RECT bounds{};
        QIQI_TEST_CHECK(::DrawTextW(dc.Get(), heading.data(), -1, &bounds, DT_CALCRECT | DT_SINGLELINE));
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemData = i;
        QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)));
        QIQI_TEST_CHECK(measure.itemWidth >= gsl::narrow_cast<UINT>(bounds.right));
        QIQI_TEST_CHECK(measure.itemHeight >= gsl::narrow_cast<UINT>(bounds.bottom));
        constexpr std::array keys{L'f', L'e', L'l', L'h'};
        QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_MENUCHAR, gsl::at(keys, i),
            reinterpret_cast<LPARAM>(menu)) == MAKELRESULT(i, MNC_EXECUTE));
    }
    constexpr std::array commands{
        Reload, NewWord, Copy, Import, Export, Pin, Delete, Promote};
    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) == gsl::narrow_cast<LRESULT>(commands.size()));
    QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETTEXTROWS, 0, 0) == 0);
    std::vector<RECT> items;
    for (const auto id : commands) {
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_COMMANDTOINDEX, std::to_underlying(id), 0) ==
            gsl::narrow_cast<LRESULT>(items.size()));
        QIQI_TEST_CHECK(Child(window, id) == nullptr);
        std::array<wchar_t, 256> text{};
        const LRESULT length = ::SendMessageW(toolbar, TB_GETBUTTONTEXTW,
            std::to_underlying(id), reinterpret_cast<LPARAM>(text.data()));
        QIQI_TEST_CHECK(length > 0 && length < gsl::narrow_cast<LRESULT>(text.size()));
        QIQI_TEST_CHECK(MenuText(menu, id) == text.data());
        TBBUTTONINFO button{};
        button.cbSize = sizeof(button);
        button.dwMask = TBIF_STYLE | TBIF_LPARAM;
        QIQI_TEST_CHECK(::SendMessageW(toolbar, TB_GETBUTTONINFOW,
            std::to_underlying(id), reinterpret_cast<LPARAM>(&button)) >= 0);
        QIQI_TEST_CHECK((button.fsStyle & (BTNS_SHOWTEXT | BTNS_AUTOSIZE)) == 0);
        if (id == Export) {
            QIQI_TEST_CHECK(gsl::narrow_cast<ToolIcon>(button.lParam) == ToolIcon::Save);
        } else if (id == Import) {
            QIQI_TEST_CHECK(gsl::narrow_cast<ToolIcon>(button.lParam) == ToolIcon::Import);
        }
        std::array<wchar_t, 256> tooltip{};
        NMTBGETINFOTIPW tip{};
        tip.hdr.hwndFrom = toolbar;
        tip.hdr.idFrom = std::to_underlying(Toolbar);
        tip.hdr.code = TBN_GETINFOTIPW;
        tip.iItem = std::to_underlying(id);
        tip.pszText = tooltip.data();
        tip.cchTextMax = gsl::narrow_cast<int>(tooltip.size());
        ::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar), reinterpret_cast<LPARAM>(&tip));
        QIQI_TEST_CHECK(std::wstring_view(tooltip.data()) == text.data());
        const auto rect = ToolbarItemRect(window, id);
        QIQI_TEST_CHECK(rect.right - rect.left == ScalePxByDpi(36, dpi));
        QIQI_TEST_CHECK(rect.bottom - rect.top == ScalePxByDpi(36, dpi));
        RECT toolbar_client{};
        QIQI_TEST_CHECK(::GetClientRect(toolbar, &toolbar_client));
        QIQI_TEST_CHECK(rect.left >= 0 && rect.top >= 0 &&
            rect.right <= toolbar_client.right && rect.bottom <= toolbar_client.bottom);
        for (const auto& other : items) {
            RECT intersection{};
            QIQI_TEST_CHECK(!::IntersectRect(&intersection, &rect, &other));
        }
        items.push_back(rect);
    }
    for (const auto id : {Backup, Restore, ClearFreq, About, AutoCode, SelectAll, Exit,
             LanguageAuto, LanguageZh, LanguageEn}) {
        QIQI_TEST_CHECK(!MenuText(menu, id).empty());
    }

    RECT client{};
    QIQI_TEST_CHECK(::GetClientRect(window.Handle(), &client));
    POINT origin{};
    QIQI_TEST_CHECK(::ClientToScreen(window.Handle(), &origin));
    ::OffsetRect(&client, origin.x, origin.y);
    std::vector<RECT> controls;
    for (HWND child = ::GetWindow(window.Handle(), GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT)) {
        std::array<wchar_t, 64> class_name{};
        QIQI_TEST_CHECK(::GetClassNameW(child, class_name.data(), gsl::narrow_cast<int>(class_name.size())));
        QIQI_TEST_CHECK(_wcsicmp(class_name.data(), L"BUTTON") != 0);
        const int id = ::GetDlgCtrlID(child);
        if (id == std::to_underlying(EditCodeInput) || id == std::to_underlying(EditWordInput) ||
            id == std::to_underlying(InlineSave)) continue;
        RECT rect{};
        QIQI_TEST_CHECK(::GetWindowRect(child, &rect));
        QIQI_TEST_CHECK(rect.left >= client.left && rect.top >= client.top);
        QIQI_TEST_CHECK(rect.right <= client.right && rect.bottom <= client.bottom);
        for (const auto& other : controls) {
            RECT intersection{};
            QIQI_TEST_CHECK(!::IntersectRect(&intersection, &rect, &other));
        }
        controls.push_back(rect);
    }
}

void CheckCommandAppearance(const DictManagerWindow& window, UINT dpi, bool dark, bool contrast) {
    SetThemeDarkOverrideForTest(dark);
    SetHighContrastOverrideForTest(contrast);
    const auto restore = gsl::finally([]() noexcept {
        SetThemeDarkOverrideForTest(std::nullopt);
        SetHighContrastOverrideForTest(std::nullopt);
    });
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
    ::SendMessageW(window.Handle(), WM_SYSCOLORCHANGE, 0, 0);
    CheckLayout(window, dpi);
    const ScopedWindowDC window_dc(window.Handle());
    QIQI_TEST_CHECK(window_dc);
    const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
    QIQI_TEST_CHECK(dc);
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(window_dc.Get(), 512, 96));
    QIQI_TEST_CHECK(bitmap);
    const ScopedSelectObject selected(dc.Get(), bitmap.Get());
    QIQI_TEST_CHECK(selected);
    const HWND list = Child(window, List);
    RECT edit_cell{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
    QIQI_TEST_CHECK(ListView_GetItemRect(list, 0, &edit_cell, LVIR_BOUNDS));
    edit_cell.right = edit_cell.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit));
    ::SendMessageW(list, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
    size_t edit_ink = 0;
    for (int y = edit_cell.top + 2; y < edit_cell.bottom - 2; ++y) {
        for (int x = edit_cell.left + 2; x < edit_cell.right - 2; ++x) {
            if (::GetPixel(dc.Get(), x, y) == ListView_GetTextColor(list)) ++edit_ink;
        }
    }
    QIQI_TEST_CHECK(edit_ink > 10);
    const auto& palette = GetThemePalette(dark);
    NMTBCUSTOMDRAW toolbar{};
    toolbar.nmcd.hdr.hwndFrom = Child(window, Toolbar);
    toolbar.nmcd.hdr.idFrom = std::to_underlying(Toolbar);
    toolbar.nmcd.hdr.code = NM_CUSTOMDRAW;
    toolbar.nmcd.hdc = dc.Get();
    toolbar.nmcd.rc = {0, 0, 512, 96};
    toolbar.nmcd.dwDrawStage = CDDS_PREPAINT;
    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar),
        reinterpret_cast<LPARAM>(&toolbar)) == CDRF_NOTIFYITEMDRAW);
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) ==
        (contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg));
    toolbar.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
    constexpr std::array commands{
        Reload, Copy, Import, Export, NewWord, Pin, Delete, Promote, SaveWord};
    for (const UINT state : {0u, UINT{CDIS_HOT}, UINT{CDIS_SELECTED}, UINT{CDIS_DISABLED}, UINT{CDIS_FOCUS}}) {
        toolbar.nmcd.uItemState = state;
        const bool hot = (state & (CDIS_HOT | CDIS_SELECTED)) != 0;
        const COLORREF background = contrast
            ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE)
            : (hot ? palette.hover_bg : palette.window_bg);
        const COLORREF foreground = contrast
            ? ::GetSysColor((state & CDIS_DISABLED) ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT))
            : ((state & CDIS_DISABLED) ? palette.grip_dot : palette.text_primary);
        std::vector<uint64_t> shapes;
        for (const auto id : commands) {
            toolbar.nmcd.hdr.hwndFrom = Child(window, id == SaveWord ? InlineSave : Toolbar);
            toolbar.nmcd.dwItemSpec = std::to_underlying(id);
            QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(Toolbar),
                reinterpret_cast<LPARAM>(&toolbar)) == CDRF_SKIPDEFAULT);
            QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) == background);
            const int size = ScalePxByDpi(20, dpi);
            const int stroke = ScalePxByDpi(2, dpi);
            const int left = (512 - size) / 2 - stroke;
            const int top = (96 - size) / 2 - stroke;
            size_t ink = 0;
            uint64_t shape = 14695981039346656037ull;
            for (int y = top; y < top + size + 2 * stroke; ++y) {
                for (int x = left; x < left + size + 2 * stroke; ++x) {
                    const COLORREF pixel = ::GetPixel(dc.Get(), x, y);
                    QIQI_TEST_CHECK(pixel == background || pixel == foreground);
                    const bool filled = pixel == foreground;
                    if (filled) ++ink;
                    shape = (shape ^ gsl::narrow_cast<uint64_t>(filled)) * 1099511628211ull;
                }
            }
            QIQI_TEST_CHECK(ink > 10);
            QIQI_TEST_CHECK(std::ranges::find(shapes, shape) == shapes.end());
            shapes.push_back(shape);
        }
    }

    MENUINFO menu_info{};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_BACKGROUND;
    QIQI_TEST_CHECK(::GetMenuInfo(::GetMenu(window.Handle()), &menu_info));
    LOGBRUSH brush{};
    QIQI_TEST_CHECK(::GetObjectW(menu_info.hbrBack, sizeof(brush), &brush) == sizeof(brush));
    QIQI_TEST_CHECK(brush.lbColor == (contrast ? ::GetSysColor(COLOR_MENUBAR) : palette.window_bg));
    DRAWITEMSTRUCT menu{};
    menu.CtlType = ODT_MENU;
    menu.itemData = 2;
    menu.itemState = ODS_SELECTED;
    menu.hDC = dc.Get();
    menu.rcItem = {0, 0, 512, 96};
    QIQI_TEST_CHECK(::SendMessageW(window.Handle(), WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&menu)));
    QIQI_TEST_CHECK(::GetPixel(dc.Get(), 1, 1) ==
        (contrast ? ::GetSysColor(COLOR_HIGHLIGHT) : palette.hover_bg));
}

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

void TestDictionarySourceSelection() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_plan_" + std::to_wstring(::GetCurrentProcessId()));
    const std::wstring subkey = L"Software\\QiQiIME_DictPlan_" + std::to_wstring(::GetCurrentProcessId());
    const wchar_t* previous = GetActiveConfigRegistrySubKey();
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(previous);
        QIQI_TEST_CHECK(::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str()) == ERROR_SUCCESS);
        WubiMappedDict::ClearProcessCache();
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        QIQI_TEST_CHECK(!ec);
    });
    const auto module = directory / L"bin";
    const auto data = directory / L"data";
    QIQI_TEST_CHECK(std::filesystem::create_directories(module));
    QIQI_TEST_CHECK(std::filesystem::create_directories(data));
    const auto write = [](const std::filesystem::path& path, std::string_view text) {
        QIQI_TEST_CHECK(WriteFileAtomically(path, codec::AsByteSpan(text)));
    };
    write(data / L"wubi86_jidian.txt", "aaaa 计划极点\n");
    write(data / L"wubi86_jidian_extra.txt", "bbbb 计划扩展\n");
    write(data / L"wubi86_haifeng.txt", "cccc 计划海峰\n");
    write(module / L"relative-user.txt", "dddd 计划用户\n");
    for (const PrimaryDict primary : {PrimaryDict::BuiltinOnly, PrimaryDict::Jidian, PrimaryDict::Haifeng}) {
        for (const bool extra : {false, true}) {
            QiQiConfig config;
            config.primary_dict = primary;
            config.enable_jidian_extra = extra;
            config.user_dict_path = L"relative-user.txt";
            QIQI_TEST_CHECK(SaveConfig(config));
            DictManagerModel model;
            model.InitializePaths(module, {}, directory / L"missing-frequency.txt");
            QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK && model.UserEntryCount() == 1);
            std::vector<DictEntrySource> sources;
            for (const auto& record : model.Records()) {
                if (record.source == DictEntrySource::UserDict) {
                    QIQI_TEST_CHECK(record.word == L"计划用户");
                } else if (record.source == DictEntrySource::PrimaryDict) {
                    QIQI_TEST_CHECK(record.word == (primary == PrimaryDict::Jidian ? L"计划极点" : L"计划海峰"));
                    sources.push_back(record.source);
                } else if (record.source == DictEntrySource::ExtraDict) {
                    QIQI_TEST_CHECK(record.word == L"计划扩展");
                    sources.push_back(record.source);
                }
            }
            std::vector<DictEntrySource> expected;
            if (primary != PrimaryDict::BuiltinOnly) expected.push_back(DictEntrySource::PrimaryDict);
            if (primary == PrimaryDict::Jidian && extra) expected.push_back(DictEntrySource::ExtraDict);
            QIQI_TEST_CHECK(sources == expected);
            if (primary != PrimaryDict::BuiltinOnly) {
                const auto path = data / (primary == PrimaryDict::Jidian ? L"wubi86_jidian.txt" : L"wubi86_haifeng.txt");
                const auto cached = WubiMappedDict::LoadFromTextOrCache(path);
                QIQI_TEST_CHECK(cached && model.ReloadAllResult() == S_OK);
                QIQI_TEST_CHECK(WubiMappedDict::LoadFromTextOrCache(path) == cached);
            }
        }
    }
    WubiMappedDict::ClearProcessCache();
    QIQI_TEST_CHECK(std::filesystem::remove(data / L"wubi86_haifeng.txt"));
    QIQI_TEST_CHECK(std::filesystem::remove(data / L"wubi86_haifeng.qdict"));
    DictManagerModel missing;
    missing.InitializePaths(module, {}, directory / L"missing-frequency.txt");
    QIQI_TEST_CHECK(missing.ReloadAllResult() == S_OK && missing.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::none_of(missing.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict || record.source == DictEntrySource::ExtraDict;
    }));

    // Verify custom primary dictionary selection, validation failure retention, and default restoration
    const auto custom_valid = directory / L"custom_primary.txt";
    const auto custom_invalid = directory / L"custom_empty.txt";
    write(custom_valid, "gggg 自定义词管主词\n");
    write(custom_invalid, "# no valid entries\n");

    DictManagerModel custom_model;
    custom_model.InitializePaths(module, {}, directory / L"missing-frequency.txt");
    QIQI_TEST_CHECK(custom_model.SelectCustomPrimaryDictionary(custom_valid));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Custom);
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDictPath() == custom_valid);
    QIQI_TEST_CHECK(custom_model.UserEntryCount() == 1 && custom_model.SystemEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"自定义词管主词";
    }));

    const auto custom_partial = directory / L"custom_partial.txt";
    write(custom_partial, "hhhh 部分有效词\n12345 67890\n");
    const auto previous_sequence = QueryRegistryConfigSequence();
    QIQI_TEST_CHECK(!custom_model.SelectCustomPrimaryDictionary(custom_partial));
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == previous_sequence &&
        custom_model.ActivePrimaryDictPath() == custom_valid);
    const auto confirmed = WubiMappedDict::ValidateAndLoadPrimaryDictionary(custom_partial);
    QIQI_TEST_CHECK(confirmed && confirmed.diagnostics.invalid_lines == 1);
    QIQI_TEST_CHECK(SUCCEEDED(custom_model.SelectValidatedCustomPrimaryDictionary(custom_partial, confirmed)));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDictPath() == custom_partial &&
        custom_model.SystemEntryCount() == 1 && custom_model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"部分有效词";
    }));
    QIQI_TEST_CHECK(custom_model.SelectCustomPrimaryDictionary(custom_valid));

    // Selecting an invalid file fails and preserves the currently active custom primary dictionary
    QIQI_TEST_CHECK(!custom_model.SelectCustomPrimaryDictionary(custom_invalid));
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Custom);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"自定义词管主词";
    }));

    // Restoring default primary dictionary switches back to Jidian while keeping user words intact
    QIQI_TEST_CHECK(custom_model.RestoreDefaultPrimaryDictionary());
    QIQI_TEST_CHECK(custom_model.ActivePrimaryDict() == PrimaryDict::Jidian);
    QIQI_TEST_CHECK(custom_model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(std::ranges::any_of(custom_model.Records(), [](const DictRecord& record) {
        return record.source == DictEntrySource::PrimaryDict && record.word == L"计划极点";
    }));
}

void TestModelAndCrossProcessSync() {
    const auto temp_dir = std::filesystem::temp_directory_path() /
        (L"qiqi_dict_mgr_test_" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
    std::filesystem::create_directories(temp_dir, ec);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        std::error_code rm_ec;
        std::filesystem::remove_all(temp_dir, rm_ec);
    });

    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.txt";
    const auto import_file = temp_dir / L"import.txt";
    const auto export_file = temp_dir / L"export.txt";

    DictManagerModel model;
    const auto module_dir = GetModuleFilePath(nullptr).parent_path();
    model.InitializePaths(module_dir, user_dict, freq_file);
    QIQI_TEST_CHECK(model.UserEntryCount() == 0);
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);

    // Auto-compute Wubi 86 phrase and single-char codes
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中华人民共和国") == L"kwwl");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"中国") == L"khlg");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"工") == L"aaaa");
    QIQI_TEST_CHECK(model.ComputeAutoCode(L"   ").empty());

    // Add, modify, and remove user entries while an active WubiEngine instance syncs incrementally
    WubiEngine peer_engine;
    peer_engine.SetUserDictPath(user_dict);
    peer_engine.SetPhraseFreqPath(freq_file);
    peer_engine.SetDynamicPhraseFreq(true);

    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词一"));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 2);

    QIQI_TEST_CHECK(model.ModifyUserEntry(L"abcd", L"管理词一", L"管理词改"));
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    const auto after_mod = peer_engine.LookupExact(L"abcd");
    QIQI_TEST_CHECK(after_mod.size() == 2 && gsl::at(after_mod, 0) == L"管理词改");

    QIQI_TEST_CHECK(model.RemoveUserEntry(L"abcd", L"管理词二"));
    QIQI_TEST_CHECK(model.UserEntryCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"abcd").size() == 1);

    QIQI_TEST_CHECK(model.DeleteOrBlockEntry(L"abcd", L"管理词改")); // deletes user entry
    QIQI_TEST_CHECK(model.DeleteOrBlockEntry(L"aaaa", L"工"));     // blocks system entry
    QIQI_TEST_CHECK(model.UnblockSystemEntry(L"aaaa", L"工"));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"abcd", L"管理词改"));

    // Invalid input validation branches
    QIQI_TEST_CHECK(!model.AddOrUpdateUserEntry(L"", L"词") && !model.AddOrUpdateUserEntry(L"abcd", L""));
    QIQI_TEST_CHECK(!model.ModifyUserEntry(L"", L"旧", L"新") && !model.ModifyUserEntry(L"abcd", L"", L"新"));
    QIQI_TEST_CHECK(!model.PinUserEntry(L"", L"词") && !model.UnpinUserEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.BlockSystemEntry(L"", L"词") && !model.UnblockSystemEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.DeleteOrBlockEntry(L"", L"词"));
    QIQI_TEST_CHECK(!model.ExportRecordsToFile({}, model.Records()));
    QIQI_TEST_CHECK(!model.CreateBackupBundle({}));
    size_t bad_imported = 0;
    QIQI_TEST_CHECK(!model.ImportUserEntriesFromFile(temp_dir / L"missing.txt", bad_imported));

    // Phrase frequency promotion, removal, and clearing synced to peer WubiEngine
    QIQI_TEST_CHECK(!model.PromotePhraseFrequency(L"abcd", L"单"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"管理词改"));
    QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 2);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).front() == L"高频词组");

    QIQI_TEST_CHECK(model.RemovePhraseFrequency(L"abcd", L"高频词组"));
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 1);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().at(PackWubiCode(L"abcd")).size() == 1);

    QIQI_TEST_CHECK(model.ClearAllPhraseFrequencies());
    QIQI_TEST_CHECK(model.PhraseFreqCount() == 0);
    QIQI_TEST_CHECK(peer_engine.SynchronizePhraseFrequencies(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.PromotedPhrases().empty());

    // PollExternalChanges when peer_engine adds an entry and promotes a phrase
    QIQI_TEST_CHECK(!model.PollExternalChanges());
    QIQI_TEST_CHECK(peer_engine.AddUserEntry(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(peer_engine.PromoteUserPhraseFrequency(L"xyza", L"远端新词"));
    QIQI_TEST_CHECK(model.PollExternalChanges());
    QIQI_TEST_CHECK(model.UserEntryCount() == 2 && model.PhraseFreqCount() == 1);

    // Import and export
    {
        std::ofstream ofs(import_file, std::ios::binary | std::ios::trunc);
        ofs << "qwer 导入词一 导入词二\n";
    }
    size_t imported = 0;
    QIQI_TEST_CHECK(model.ImportUserEntriesFromFile(import_file, imported) && imported == 2);
    QIQI_TEST_CHECK(model.ExportRecordsToFile(export_file, model.Records()));
    std::string exported_utf8;
    QIQI_TEST_CHECK(ReadFileToUtf8String(export_file, exported_utf8) && !exported_utf8.empty());

    // Filter matching and clipboard formatting
    DictFilter filter{};
    filter.source = DictEntrySource::UserDict;
    for (const auto* query : {L" QW ", L"导入"}) {
        filter.SetQuery(query);
        size_t matched = 0;
        for (const auto& rec : model.Records()) {
            if (filter.Matches(rec)) ++matched;
        }
        QIQI_TEST_CHECK(matched == 2);
    }
    QIQI_TEST_CHECK(!FormatRecordsForClipboard(model.Records()).empty());

    // Backup & Restore (.qbak bundle with FNV-1a checksum validation, Merge vs Replace conflict modes)
    const std::wstring test_reg_subkey =
        L"Software\\QiQiIME_DictMgrBackupTest_" + std::to_wstring(::GetCurrentProcessId());
    SetConfigRegistrySubKeyOverrideForTest(test_reg_subkey.c_str());
    const auto restore_reg = gsl::finally([&test_reg_subkey]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, test_reg_subkey.c_str());
    });

    QiQiConfig custom_cfg = LoadConfig();
    custom_cfg.page_size = 7;
    custom_cfg.dynamic_phrase_freq = true;
    custom_cfg.commit_on_english_switch = false;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));

    QIQI_TEST_CHECK(model.PinUserEntry(L"abcd", L"备份置顶词"));
    QIQI_TEST_CHECK(model.BlockSystemEntry(L"aaaa", L"工"));
    const auto backup_file = temp_dir / L"test_backup.qbak";
    QIQI_TEST_CHECK(model.CreateBackupBundle(backup_file));

    // Corrupting the backup file payload must be rejected by checksum validation
    std::string raw_backup;
    QIQI_TEST_CHECK(ReadFileToUtf8String(backup_file, raw_backup));
    QIQI_TEST_CHECK(raw_backup.contains("commit_on_english_switch=0\n"));
    const auto corrupt_file = temp_dir / L"corrupt.qbak";
    {
        std::string corrupted = raw_backup + "\nabcd 被篡改词\n";
        QIQI_TEST_CHECK(WriteFileAtomically(corrupt_file, codec::AsByteSpan(corrupted)));
    }
    BackupRestoreSummary summary{};
    QIQI_TEST_CHECK(!model.RestoreBackupBundle(corrupt_file, BackupConflictMode::Replace, summary));

    // Mutate current state and test Replace mode (overwrites config, user_dict, and phrase_freq)
    custom_cfg.page_size = 3;
    custom_cfg.commit_on_english_switch = true;
    QIQI_TEST_CHECK(SaveConfig(custom_cfg));
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"临时未备份词"));
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Replace, summary));
    QIQI_TEST_CHECK(summary.config_restored && summary.user_entries_applied >= 5 && summary.phrase_freq_applied == 1);
    QIQI_TEST_CHECK(LoadConfig().page_size == 7);
    QIQI_TEST_CHECK(!LoadConfig().commit_on_english_switch);
    QIQI_TEST_CHECK(model.PinnedEntryCount() == 1 && model.BlockedEntryCount() == 1);

    // Test Merge mode (preserves newly added entry while re-applying backup entries)
    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"mnop", L"合并保留词"));
    const size_t before_merge_count = model.UserEntryCount();
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup_file, BackupConflictMode::Merge, summary));
    QIQI_TEST_CHECK(model.UserEntryCount() == before_merge_count);

    QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"lmno", L"待修改词"));
    const size_t before_replace = model.UserEntryCount();
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"待修改词", L"lmno", L"待修改词"));
    model.ReloadAll();
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"待修改词", L"lmno", L"同码新词"));
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"lmno").front() == L"同码新词");
    const auto snapshots = peer_engine.UserDictSnapshotSyncCount();
    QIQI_TEST_CHECK(model.ReplaceUserEntry(L"lmno", L"同码新词", L" PQRS ", L"  新码新词  "));
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(peer_engine.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer_engine.UserDictSnapshotSyncCount() > snapshots);
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"lmno").empty());
    QIQI_TEST_CHECK(peer_engine.LookupExact(L"pqrs").front() == L"新码新词");
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"新码新词", L"a1", L"无效"));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"新码新词", L"abcd", L" "));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"已删除原词", L"abcd", L"不能复活"));
    QIQI_TEST_CHECK(!model.ReplaceUserEntry(L"pqrs", L"已删除原词", L"pqrs", L"不能复活"));
    {
        const ScopedFileHandle locked(::CreateFileW(user_dict.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        QIQI_TEST_CHECK(model.ReplaceUserEntryResult(L"pqrs", L"新码新词", L"lmno", L"写入失败") ==
            HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
    }
    {
        const DWORD attributes = ::GetFileAttributesW(user_dict.c_str());
        QIQI_TEST_CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        const auto restore_attributes = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes));
        });
        QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes | FILE_ATTRIBUTE_READONLY));
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntryResult(L"abcd", L"无权新增") == E_ACCESSDENIED);
        QIQI_TEST_CHECK(model.ReplaceUserEntryResult(L"pqrs", L"新码新词", L"lmno", L"无权修改") == E_ACCESSDENIED);
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntryResult(L"a1", L"无效") == E_INVALIDARG);
    }
    model.ReloadAll();
    QIQI_TEST_CHECK(model.UserEntryCount() == before_replace);
    QIQI_TEST_CHECK(std::ranges::any_of(model.Records(), [](const DictRecord& rec) {
        return rec.source == DictEntrySource::UserDict && rec.code == L"pqrs" && rec.word == L"新码新词";
    }));
}

void WriteRestoreBundle(const std::filesystem::path& path, std::string_view body) {
    const auto bytes = std::format("# QIQIIME_BACKUP_V1\n# checksum_fnv1a64={:016X}\n{}",
        codec::ComputeFnv1a64(codec::AsByteSpan(body)), body);
    QIQI_TEST_CHECK(WriteFileAtomically(path, codec::AsByteSpan(bytes)));
}

void TestBackupRestoreResults() {
    const auto directory = std::filesystem::current_path() /
        (L"qiqi_restore_results_" + std::to_wstring(::GetCurrentProcessId()));
    const std::wstring subkey = L"Software\\QiQiIME_RestoreResults_" + std::to_wstring(::GetCurrentProcessId());
    const auto cleanup = gsl::finally([&]() noexcept {
        SetConfigRegistrySubKeyOverrideForTest(nullptr);
        ::RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
    });
    SetConfigRegistrySubKeyOverrideForTest(subkey.c_str());
    const auto user = directory / L"user.txt";
    const auto frequency = directory / L"frequency.txt";
    const auto backup = directory / L"restore.qbak";
    DictManagerModel model;
    const auto reset = [&]() {
        std::error_code ec;
        std::filesystem::remove_all(directory, ec);
        QIQI_TEST_CHECK(!ec);
        QIQI_TEST_CHECK(std::filesystem::create_directories(directory));
        QiQiConfig config;
        config.primary_dict = PrimaryDict::BuiltinOnly;
        config.page_size = 3;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths({}, user, frequency);
        QIQI_TEST_CHECK(model.AddOrUpdateUserEntry(L"zzzz", L"原有词条"));
        QIQI_TEST_CHECK(model.PromotePhraseFrequency(L"zzzz", L"原有词组"));
    };
    const auto has_word = [&](DictEntrySource source, std::wstring_view word) {
        return std::ranges::any_of(model.Records(), [=](const DictRecord& record) {
            return record.source == source && record.word == word;
        });
    };
    const auto lock_file = [](const std::filesystem::path& path) {
        return ScopedFileHandle(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    };
    const auto read_file = [](const std::filesystem::path& path) {
        std::string bytes;
        QIQI_TEST_CHECK(ReadFileToUtf8String(path, bytes));
        return bytes;
    };
    const std::string full_body =
        "[config]\npage_size=7\n[user_dict]\nabcd 新增词条\n[phrase_freq]\nabcd 新增词组\n";

    reset();
    for (const bool maximum : {false, true}) {
        QiQiConfig expected = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Assign(expected, maximum ? field.maximum : field.minimum));
        });
        expected.user_dict_path = L"not-in-backup.txt";
        QIQI_TEST_CHECK(SaveConfig(expected));
        QIQI_TEST_CHECK(model.CreateBackupBundle(backup));
        const auto bytes = read_file(backup);
        QIQI_TEST_CHECK(!bytes.contains("user_dict_path") && !bytes.contains("not-in-backup.txt"));
        size_t position = bytes.find("[config]\n");
        ForEachConfigField([&](const auto& field) {
            const auto line = std::format("{}={}\n", field.backup_key, field.Value(expected));
            const size_t found = bytes.find(line, position);
            QIQI_TEST_CHECK(found != std::string::npos && bytes.find(line, found + line.size()) == std::string::npos);
            position = found + line.size();
        });
        QIQI_TEST_CHECK(SaveConfig(QiQiConfig{}));
        QIQI_TEST_CHECK(model.RestoreBackupBundle(backup, BackupConflictMode::Merge).status == BackupRestoreStatus::Success);
        const auto restored = LoadConfig();
        ForEachConfigField([&](const auto& field) {
            QIQI_TEST_CHECK(field.Value(restored) == field.Value(expected));
        });
        QIQI_TEST_CHECK(restored.user_dict_path.empty());
    }
    ForEachConfigField([&](const auto& field) {
        for (uint64_t value = field.minimum; value <= field.maximum + 1; ++value) {
            const QiQiConfig before = LoadConfig();
            const uint64_t sequence = QueryRegistryConfigSequence();
            WriteRestoreBundle(backup, std::format("[config]\n{}={}\n", field.backup_key, value));
            QiQiConfig expected = before;
            const bool valid = field.Assign(expected, value);
            const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
            QIQI_TEST_CHECK(result.status == (valid ? BackupRestoreStatus::Success : BackupRestoreStatus::Failed));
            if (!valid) {
                QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && QueryRegistryConfigSequence() == sequence);
            }
            const auto restored = LoadConfig();
            ForEachConfigField([&](const auto& other) {
                QIQI_TEST_CHECK(other.Value(restored) == other.Value(expected));
            });
        }
    });
    WriteRestoreBundle(backup, "[config]\nfuture_numeric_key=123\n");
    QIQI_TEST_CHECK(model.RestoreBackupBundle(backup, BackupConflictMode::Merge).status == BackupRestoreStatus::Success);

    // Checksummed malformed later input must not apply earlier configuration or dictionary lines.
    reset();
    const auto old_user = read_file(user);
    const auto old_frequency = read_file(frequency);
    const auto old_sequence = QueryRegistryConfigSequence();
    const auto missing = model.RestoreBackupBundle(directory / L"missing.qbak", BackupConflictMode::Replace);
    QIQI_TEST_CHECK(missing.status == BackupRestoreStatus::Failed && missing.stage == BackupRestoreStage::Read);
    for (const auto& malformed : std::array<std::string, 6>{
             full_body + "not_a_code 损坏词组\n",
             full_body + "[config]\npage_size=0\n",
             full_body + "[unexpected]\n",
             full_body + "[user_dict]\nabcd ^\n",
             full_body + "abcd 单\n",
             full_body + "abcd \xFF\n"}) {
        WriteRestoreBundle(backup, malformed);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Validate && FAILED(result.error));
        QIQI_TEST_CHECK(result.refresh_error == S_FALSE && !result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3 && QueryRegistryConfigSequence() == old_sequence);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条"));
    }

    // Actual registry write denial, without changing the process user's real configuration.
    WriteRestoreBundle(backup, "[config]\npage_size=7\n");
    {
        ScopedRegKey key;
        QIQI_TEST_CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0,
            READ_CONTROL | WRITE_DAC, key.Put()) == ERROR_SUCCESS);
        DWORD size = 0;
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, nullptr, &size) ==
            ERROR_INSUFFICIENT_BUFFER);
        std::vector<uint8_t> original(size);
        QIQI_TEST_CHECK(::RegGetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION,
            original.data(), &size) == ERROR_SUCCESS);
        const auto restore_acl = gsl::finally([&]() noexcept {
            ::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, original.data());
        });
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        QIQI_TEST_CHECK(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;KW;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
        const ScopedLocalAlloc acl(descriptor);
        QIQI_TEST_CHECK(::RegSetKeySecurity(key.Get(), DACL_SECURITY_INFORMATION, descriptor) == ERROR_SUCCESS);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(result.error == E_FAIL && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
        WriteRestoreBundle(backup, full_body);
        const auto partial = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(partial.status == BackupRestoreStatus::Partial && partial.stage == BackupRestoreStage::Config);
        QIQI_TEST_CHECK(partial.operations_succeeded == 2 && partial.operations_failed == 1);
        QIQI_TEST_CHECK(partial.summary.user_entries_applied == 1 && partial.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(!partial.summary.config_restored && LoadConfig().page_size == 3);
    }
    QIQI_TEST_CHECK(QueryRegistryConfigSequence() == old_sequence);

    // Replace must stop when the first deletion fails, preserving the original files and config.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Failed && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 1);
        QIQI_TEST_CHECK(read_file(user) == old_user && read_file(frequency) == old_frequency);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    // Later preparation failures report already committed destructive work, never full success.
    {
        auto cache = user;
        cache.replace_extension(L".qdict");
        QIQI_TEST_CHECK(WriteFileAtomically(cache, codec::AsByteSpan(std::string_view("cache"))));
        const auto locked = lock_file(cache);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) == old_frequency && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto channel = GetSyncChannelDirectoryForFile(user);
        const auto journal = channel / L"journal.bin";
        // Make the refresh failure independent of channel-directory enumeration order:
        // the locked journal cannot be removed, and neither source nor snapshot can reload it.
        QIQI_TEST_CHECK(std::filesystem::remove(channel / L"snapshot.txt"));
        const auto locked = lock_file(journal);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION));
        QIQI_TEST_CHECK(result.operations_succeeded > 0 && result.operations_failed == 2);
        QIQI_TEST_CHECK(FAILED(result.refresh_error));
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Prepare);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.operations_failed == 1);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && !result.summary.config_restored);
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Best-effort application keeps honest per-item counts and skips config on partial failure.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::UserDictionary && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.user_entries_failed == 1);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 1 && result.summary.phrase_freq_failed == 0);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        BackupRestoreSummary summary;
        QIQI_TEST_CHECK(!model.RestoreBackupBundle(backup, BackupConflictMode::Merge, summary));
        QIQI_TEST_CHECK(summary.user_entries_failed == 1 && summary.phrase_freq_applied == 1);
    }
    reset();
    WriteRestoreBundle(backup, "[user_dict]\nabcd 新增词条\n");
    {
        const auto locked = lock_file(user);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.user_entries_failed == 1);
    }
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto locked = lock_file(frequency);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::PhraseFrequency);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 1 && result.summary.user_entries_failed == 0);
        QIQI_TEST_CHECK(result.summary.phrase_freq_applied == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && LoadConfig().page_size == 3);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"新增词条"));
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
    }

    // Missing optional sources remain a valid, empty view and need not be created by Merge.
    {
        DictManagerModel optional;
        const auto missing_user = directory / L"optional_user.txt";
        const auto missing_frequency = directory / L"optional_frequency.txt";
        optional.InitializePaths({}, missing_user, missing_frequency);
        QIQI_TEST_CHECK(optional.ReloadAllResult() == S_OK);
        WriteRestoreBundle(backup, "[user_dict]\n[phrase_freq]\n");
        const auto result = optional.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.operations_failed == 0);
        QIQI_TEST_CHECK(optional.UserEntryCount() == 0 && optional.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(!std::filesystem::exists(missing_user) && !std::filesystem::exists(missing_frequency));
    }

    // A final refresh failure cannot certify a successful restore or a complete displayed view.
    reset();
    WriteRestoreBundle(backup, "[config]\npage_size=7\n");
    {
        const ScopedFileHandle locked(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.operations_succeeded == 1 && result.operations_failed == 1);
        QIQI_TEST_CHECK(FormatBackupRestoreMessage(result, UiLanguage::EnUs).contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Replace must stop after an unsuccessful intermediate reload, before applying staged items.
    reset();
    {
        const auto module = directory / L"module";
        QIQI_TEST_CHECK(std::filesystem::create_directories(module));
        auto config = LoadConfig();
        config.primary_dict = PrimaryDict::Jidian;
        config.enable_jidian_extra = false;
        QIQI_TEST_CHECK(SaveConfig(config));
        model.InitializePaths(module, user, frequency); // The optional system file is not present yet.
        const auto dictionary = module / L"wubi86_jidian.txt";
        QIQI_TEST_CHECK(WriteFileAtomically(dictionary, codec::AsByteSpan(std::string_view("abcd 系统词组\n"))));
        const ScopedFileHandle locked(::CreateFileW(dictionary.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        WriteRestoreBundle(backup, full_body);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.stage == BackupRestoreStage::Refresh);
        QIQI_TEST_CHECK(result.error == E_FAIL && result.refresh_error == E_FAIL);
        QIQI_TEST_CHECK(result.operations_failed == 1 && !result.summary.config_restored);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 0 && result.summary.phrase_freq_applied == 0);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::UserDict, L"新增词条"));
    }

    // Preserve the apply failure independently of refresh failure; discard speculative promotions.
    reset();
    WriteRestoreBundle(backup, "[phrase_freq]\nabcd 新增词组\n");
    {
        const ScopedFileHandle unreadable_user(::CreateFileW(user.c_str(), GENERIC_READ, 0,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        const auto unwritable_frequency = lock_file(frequency);
        QIQI_TEST_CHECK(unreadable_user && unwritable_frequency);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Merge);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::PhraseFrequency && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.refresh_error == E_FAIL && result.operations_failed == 2);
        QIQI_TEST_CHECK(result.operations_succeeded == 0 && result.summary.phrase_freq_failed == 1);
        QIQI_TEST_CHECK(!has_word(DictEntrySource::PhraseFreq, L"新增词组"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组"));
        const auto message = FormatBackupRestoreMessage(result, UiLanguage::EnUs);
        QIQI_TEST_CHECK(message.contains(L"phrase ordering") && message.contains(L"view may be incomplete"));
    }
    QIQI_TEST_CHECK(model.ReloadAllResult() == S_OK);

    // Clear can fail after its source write when the required synchronization snapshot is locked.
    reset();
    WriteRestoreBundle(backup, full_body);
    {
        const auto snapshot = GetSyncChannelDirectoryForFile(frequency) / L"snapshot.qfreq";
        const auto old_snapshot = read_file(snapshot);
        const auto locked = lock_file(snapshot);
        QIQI_TEST_CHECK(locked);
        const auto result = model.RestoreBackupBundle(backup, BackupConflictMode::Replace);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Partial && result.effects_possible);
        QIQI_TEST_CHECK(result.stage == BackupRestoreStage::Prepare && result.error == E_FAIL);
        QIQI_TEST_CHECK(result.refresh_error == S_OK && result.operations_failed == 1);
        QIQI_TEST_CHECK(!result.summary.config_restored && result.summary.user_entries_applied == 0);
        QIQI_TEST_CHECK(read_file(frequency) != old_frequency && model.PhraseFreqCount() == 0);
        QIQI_TEST_CHECK(read_file(snapshot) == old_snapshot);
        QIQI_TEST_CHECK(LoadConfig().page_size == 3);
    }

    for (const auto mode : {BackupConflictMode::Merge, BackupConflictMode::Replace}) {
        reset();
        WriteRestoreBundle(backup, full_body +
            "[user_dict]\nabcd ^置顶词条 !屏蔽词条\n");
        const auto result = model.RestoreBackupBundle(backup, mode);
        QIQI_TEST_CHECK(result.status == BackupRestoreStatus::Success && result.stage == BackupRestoreStage::Complete);
        QIQI_TEST_CHECK(result.refresh_error == S_OK);
        QIQI_TEST_CHECK(result.error == S_OK && result.operations_failed == 0);
        QIQI_TEST_CHECK(result.summary.config_restored && LoadConfig().page_size == 7);
        QIQI_TEST_CHECK(result.summary.user_entries_applied == 3 && result.summary.phrase_freq_applied == 1);
        QIQI_TEST_CHECK(has_word(DictEntrySource::UserDict, L"原有词条") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::PhraseFreq, L"原有词组") == (mode == BackupConflictMode::Merge));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Pinned, L"置顶词条"));
        QIQI_TEST_CHECK(has_word(DictEntrySource::Blocked, L"屏蔽词条"));
    }

    // The UI's pure formatter cannot label a partial/failed result as successful.
    BackupRestoreResult message_result;
    message_result.operations_failed = 1;
    for (const auto stage : {BackupRestoreStage::Read, BackupRestoreStage::Validate,
             BackupRestoreStage::Prepare, BackupRestoreStage::UserDictionary,
             BackupRestoreStage::PhraseFrequency, BackupRestoreStage::Config, BackupRestoreStage::Refresh}) {
        message_result.stage = stage;
        for (const auto language : {UiLanguage::EnUs, UiLanguage::ZhCn}) {
            message_result.status = BackupRestoreStatus::Failed;
            const auto failed = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(failed.contains(language == UiLanguage::EnUs ? L"restore failed" : L"恢复备份失败"));
            message_result.status = BackupRestoreStatus::Partial;
            const auto partial = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(partial.contains(language == UiLanguage::EnUs ? L"restore incomplete" : L"恢复未完成"));
            QIQI_TEST_CHECK(!partial.contains(language == UiLanguage::EnUs ? L"Backup restored" : L"已恢复备份"));
            message_result.refresh_error = E_FAIL;
            const auto incomplete_view = FormatBackupRestoreMessage(message_result, language);
            QIQI_TEST_CHECK(incomplete_view.contains(language == UiLanguage::EnUs ?
                L"view may be incomplete" : L"当前显示可能不完整"));
            message_result.refresh_error = S_FALSE;
        }
    }
    message_result.status = BackupRestoreStatus::Success;
    message_result.refresh_error = E_FAIL;
    QIQI_TEST_CHECK(!FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    message_result.refresh_error = S_OK;
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::EnUs).contains(L"Backup restored"));
    QIQI_TEST_CHECK(FormatBackupRestoreMessage(message_result, UiLanguage::ZhCn).contains(L"已恢复备份"));
}

void TestSmartSearch() {
    const DictRecord record{1, PackWubiCode(L"khlg"), L"khlg", L"中国IME2026!", DictEntrySource::UserDict, 1};
    DictFilter filter;
    for (const auto& [query, expected] : std::array{
             std::pair{L"", true}, std::pair{L" \t\r\n ", true},
             std::pair{L" KH ", true}, std::pair{L"hL", true}, std::pair{L"khlg", true},
             std::pair{L"khlga", false}, std::pair{L"aaaa", false},
             std::pair{L"中国", true}, std::pair{L" 国 ", true},
             std::pair{L"不存在", false}, std::pair{L"IME", false},
             std::pair{L"国IME", true}, std::pair{L"2026", true}, std::pair{L"!", true},
             std::pair{L"k h", false}, std::pair{L"kh中国", false}}) {
        filter.SetQuery(query);
        QIQI_TEST_CHECK(filter.Matches(record) == expected);
    }
    filter.SetQuery(L"中国");
    filter.source = DictEntrySource::PrimaryDict;
    QIQI_TEST_CHECK(!filter.Matches(record));
    filter.source = DictEntrySource::UserDict;
    QIQI_TEST_CHECK(filter.Matches(record));
    filter.SetQuery(L"KH");
    QIQI_TEST_CHECK(filter.Matches(record));
    filter.SetQuery(L" \t ");
    filter.source = DictEntrySource::PrimaryDict;
    QIQI_TEST_CHECK(!filter.Matches(record));
}

void ExerciseGuiCommands(const DictManagerWindow& window);

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
    const auto freq_file = temp_dir / L"phrase_freq.txt";

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

void TestEditorState() {
    DictEditor editor;
    const auto check_commands = [&]() {
        QIQI_TEST_CHECK(editor.CanExecute(EditorCommand::NewWord) == !editor.Active());
        QIQI_TEST_CHECK(editor.CanExecute(EditorCommand::ChangeRecords) == !editor.Active());
        for (const auto command : {EditorCommand::Save, EditorCommand::Cancel, EditorCommand::AutoCode}) {
            QIQI_TEST_CHECK(editor.CanExecute(command) == editor.Active());
        }
    };
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::Browsing && !editor.Dirty());
    QIQI_TEST_CHECK(!editor.Original() && editor.Word().empty() && editor.Code().empty());
    QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::Inactive);
    QIQI_TEST_CHECK(editor.BeginNew());
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::NewDraft && !editor.Dirty());
    QIQI_TEST_CHECK(editor.WantsAutomaticCode() && editor.PrepareSave({}) == EditorSaveAction::AddUser);
    editor.ChangeWord(L"new");
    editor.ChangeCode(L"abcd", CodeChange::Automatic);
    QIQI_TEST_CHECK(editor.Dirty() && editor.WantsAutomaticCode());
    QIQI_TEST_CHECK(!editor.BeginNew() && editor.Word() == L"new");
    editor.ChangeCode(L"test", CodeChange::Manual);
    editor.ChangeWord(L"changed");
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode() && editor.Code() == L"test");
    editor.ChangeCode(L"wxyz", CodeChange::Automatic);
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode());
    editor.CompleteSave(false);
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::NewDraft && editor.Dirty());
    QIQI_TEST_CHECK(editor.Word() == L"changed" && editor.Code() == L"wxyz");
    editor.ChangeWord(L"");
    editor.ChangeCode(L"", CodeChange::Automatic);
    QIQI_TEST_CHECK(!editor.Dirty() && !editor.WantsAutomaticCode());
    editor.Cancel();
    check_commands();
    QIQI_TEST_CHECK(editor.BeginNew() && editor.WantsAutomaticCode());

    const DictRecord original{9, PackWubiCode(L"abcd"), L"abcd", L"original", DictEntrySource::UserDict, 1};
    editor.BeginExisting(original); // Selecting an existing row replaces the temporary new row.
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::ExistingDraft && !editor.Dirty());
    QIQI_TEST_CHECK(!editor.WantsAutomaticCode() && editor.Original()->word == original.word);
    QIQI_TEST_CHECK(!editor.BeginNew() && editor.Original()->id == original.id);
    QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::Unchanged);
    std::vector<DictRecord> records{original};
    records.front().id = 42; // Refresh renumbers rows; identity is source/code/word, not the old row ID.
    QIQI_TEST_CHECK(editor.FindOriginal(records) == &records.front());
    editor.ChangeWord(L"updated");
    QIQI_TEST_CHECK(editor.Dirty() && editor.PrepareSave(records) == EditorSaveAction::ReplaceUser);
    editor.CompleteSave(false);
    QIQI_TEST_CHECK(editor.Word() == L"updated" && editor.Original()->word == L"original");
    for (const bool change_source : {false, true}) {
        auto removed = records;
        if (change_source) removed.front().source = DictEntrySource::Pinned;
        else removed.front().word = L"externally changed";
        QIQI_TEST_CHECK(!editor.FindOriginal(removed));
        QIQI_TEST_CHECK(editor.PrepareSave(removed) == EditorSaveAction::MissingOriginal);
        editor.CompleteSave(false);
        QIQI_TEST_CHECK(editor.Dirty() && editor.Word() == L"updated");
    }
    editor.ChangeWord(original.word);
    editor.ChangeCode(L"other", CodeChange::Manual);
    QIQI_TEST_CHECK(editor.Dirty());
    editor.ChangeCode(original.code, CodeChange::Manual);
    QIQI_TEST_CHECK(!editor.Dirty() && editor.PrepareSave(records) == EditorSaveAction::Unchanged);
    for (const auto source : {DictEntrySource::Pinned, DictEntrySource::Blocked, DictEntrySource::PhraseFreq,
             DictEntrySource::PrimaryDict, DictEntrySource::ExtraDict}) {
        auto record = original;
        record.source = source;
        editor.BeginExisting(record);
        editor.ChangeWord(L"user copy");
        const std::array current{record};
        QIQI_TEST_CHECK(editor.PrepareSave(current) == EditorSaveAction::AddUser);
        QIQI_TEST_CHECK(editor.PrepareSave({}) == EditorSaveAction::MissingOriginal);
        QIQI_TEST_CHECK(editor.Original()->word == original.word);
    }
    editor.CompleteSave(true);
    check_commands();
    QIQI_TEST_CHECK(editor.Mode() == EditorMode::Browsing && !editor.Dirty() && !editor.Original());
    QIQI_TEST_CHECK(editor.Word().empty() && editor.Code().empty());
    editor.Cancel();
    QIQI_TEST_CHECK(editor.BeginNew() && editor.WantsAutomaticCode());
}

void TestInlineEditing() {
    const auto restore_monitor = gsl::finally([]() noexcept { SetMonitorMetricsOverrideForTest(std::nullopt); });
    const auto temp_dir =
        std::filesystem::temp_directory_path() / (L"qiqi_dict_inline_" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(temp_dir);
    const auto cleanup = gsl::finally([&temp_dir]() noexcept {
        DrainQuitMessages();
        std::error_code ec;
        std::filesystem::remove_all(temp_dir, ec);
    });
    const auto user_dict = temp_dir / L"user_dict.txt";
    const auto freq_file = temp_dir / L"phrase_freq.txt";
    std::string source = "abcd 原始词\n";
    for (int i = 0; i < 100; ++i)
        source += "zzzz 滚动测试词" + std::to_string(i) + "\n";
    QIQI_TEST_CHECK(WriteFileAtomically(user_dict, codec::AsByteSpan(source)));

    DictManagerWindow window;
    QIQI_TEST_CHECK(window.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
    const HWND list = Child(window, List);
    QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETCURSEL, 0, 0) == 1);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 101);
    const auto visible = [&](ControlId id) {
        return (::GetWindowLongPtrW(Child(window, id), GWL_STYLE) & WS_VISIBLE) != 0;
    };
    const auto search = [&](const wchar_t* text) {
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, SearchEdit), text));
    };
    const auto marked = [&](int row = 0) {
        std::array<wchar_t, 256> source_text{};
        ListView_GetItemText(list, row, std::to_underlying(ColumnId::Source),
            source_text.data(), gsl::narrow_cast<int>(source_text.size()));
        return std::wstring_view(source_text.data()).starts_with(L"* ");
    };
    const auto begin = [&](int row = 0, ColumnId column = ColumnId::Word) {
        ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, row, FALSE);
        RECT cell{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, row, std::to_underlying(column), LVIR_BOUNDS, &cell));
        NMITEMACTIVATE click{};
        click.hdr = {list, std::to_underlying(List), NM_DBLCLK};
        click.iItem = row;
        click.iSubItem = std::to_underlying(column);
        click.ptAction = {(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
        ::SendMessageW(window.Handle(), WM_NOTIFY, std::to_underlying(List), reinterpret_cast<LPARAM>(&click));
        QIQI_TEST_CHECK(visible(EditWordInput) && visible(EditCodeInput) && visible(InlineSave));
    };
    const auto check_position = [&](int row) {
        for (const auto& [id, column] :
             std::array{std::pair{EditCodeInput, ColumnId::Code}, std::pair{EditWordInput, ColumnId::Word},
                 std::pair{InlineSave, ColumnId::Action}}) {
            RECT cell{}, editor{}, intersection{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
            QIQI_TEST_CHECK(ListView_GetSubItemRect(list, row, std::to_underlying(column), LVIR_BOUNDS, &cell));
            ::MapWindowPoints(list, nullptr, reinterpret_cast<POINT*>(&cell), 2);
            QIQI_TEST_CHECK(::GetWindowRect(Child(window, id), &editor));
            QIQI_TEST_CHECK(::IntersectRect(&intersection, &cell, &editor) && ::EqualRect(&intersection, &editor));
        }
        const auto button = ToolbarItemRect(window, SaveWord);
        RECT toolbar{};
        ::GetClientRect(Child(window, InlineSave), &toolbar);
        QIQI_TEST_CHECK(button.left >= 0 && button.top >= 0 && button.right <= toolbar.right &&
                        button.bottom <= toolbar.bottom);
        const ScopedWindowDC window_dc(window.Handle());
        QIQI_TEST_CHECK(window_dc);
        const ScopedMemoryDC dc(::CreateCompatibleDC(window_dc.Get()));
        QIQI_TEST_CHECK(dc);
        const ScopedGdiHandle<HBITMAP> bitmap(
            ::CreateCompatibleBitmap(window_dc.Get(), toolbar.right, toolbar.bottom));
        QIQI_TEST_CHECK(bitmap);
        const ScopedSelectObject selected(dc.Get(), bitmap.Get());
        QIQI_TEST_CHECK(selected);
        ::SendMessageW(Child(window, InlineSave), WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc.Get()), PRF_CLIENT);
        const auto foreground = IsHighContrastEnabled() ? ::GetSysColor(COLOR_BTNTEXT)
                                                       : GetCurrentThemePalette().text_primary;
        size_t ink = 0;
        for (int y = button.top + 2; y < button.bottom - 2; ++y) {
            for (int x = button.left + 2; x < button.right - 2; ++x) {
                if (::GetPixel(dc.Get(), x, y) == foreground) ++ink;
            }
        }
        QIQI_TEST_CHECK(ink > 10);
    };
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    search(L"");
    ::ShowWindow(window.Handle(), SW_SHOWNOACTIVATE);
    {
        RECT row{};
#pragma warning(suppress : 26477) // ListView_GetItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetItemRect(list, 0, &row, LVIR_BOUNDS));
        const POINT icon_point{row.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) / 2,
            (row.top + row.bottom) / 2};
        const LPARAM icon = MAKELPARAM(icon_point.x, icon_point.y);
        ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, icon);
        QIQI_TEST_CHECK(::GetCapture() == list);
        ::SendMessageW(list, WM_CANCELMODE, 0, 0);
        QIQI_TEST_CHECK(::GetCapture() != list);
        ::SendMessageW(list, WM_LBUTTONUP, 0, icon);
        QIQI_TEST_CHECK(!visible(EditCodeInput));
        ::SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, icon);
        const POINT outside_point{row.left + ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Edit)) + 10,
            (row.top + row.bottom) / 2};
        const LPARAM outside = MAKELPARAM(outside_point.x, outside_point.y);
        ::SendMessageW(list, WM_LBUTTONUP, 0, outside);
        QIQI_TEST_CHECK(::GetCapture() != list && !visible(EditCodeInput));
    }
    for (const auto column : {ColumnId::Code, ColumnId::Word}) {
        RECT cell{};
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, 0, std::to_underlying(column), LVIR_BOUNDS, &cell));
        const LPARAM point = MAKELPARAM((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2);
        // Seed the first-click selection without entering native drag tracking.
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ::SendMessageW(list, WM_LBUTTONDBLCLK, MK_LBUTTON, point);
        ::SendMessageW(list, WM_LBUTTONUP, 0, point);
        QIQI_TEST_CHECK(visible(EditCodeInput) && visible(EditWordInput));
        QIQI_TEST_CHECK(::GetFocus() == Child(window,
            column == ColumnId::Code ? EditCodeInput : EditWordInput));
        InvokeCommand(window, CancelEdit);
    }
    ClickRowEdit(window, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    ClickRowEdit(window, 1);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"zzzz" && !marked(1));
    const auto second_word = ControlText(Child(window, EditWordInput));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"放弃的行内草稿"));
    QIQI_TEST_CHECK(marked(1));
    begin(1, ColumnId::Word);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"放弃的行内草稿" && marked(1));
    ClickRowEdit(window, 1);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"放弃的行内草稿" && marked(1));
    ClickRowEdit(window, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"原始词");
    QIQI_TEST_CHECK(!marked(0) && !marked(1));
    check_position(0);
    ListView_SetItemState(list, 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ::SendMessageW(list, WM_KEYDOWN, VK_F2, 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == second_word);
    InvokeCommand(window, CancelEdit);

    const int original_count = ListView_GetItemCount(list);
    InvokeCommand(window, NewWord);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == original_count + 1);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"放弃的新增草稿"));
    ClickRowEdit(window, 2);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == original_count);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"zzzz");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == second_word && !marked(1));
    check_position(1);
    InvokeCommand(window, CancelEdit);

    for (const auto target : {SourceCombo, SearchEdit}) {
        for (const bool new_word : {false, true}) {
            search(L"abcd");
            if (new_word) InvokeCommand(window, NewWord);
            else begin();
            QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, SearchEdit)) &&
                ::IsWindowEnabled(Child(window, SourceCombo)));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"drop"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"筛选时放弃的草稿"));
            QIQI_TEST_CHECK(marked());
            ::SetFocus(Child(window, target));
            QIQI_TEST_CHECK(::GetFocus() == Child(window, target));
            QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(EditCodeInput) && !visible(InlineSave));
            QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
            QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && !marked());
            if (target == SearchEdit) {
                search(L"zzzz");
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 100);
            } else {
                ::SendMessageW(Child(window, SourceCombo), CB_SHOWDROPDOWN, TRUE, 0);
                QIQI_TEST_CHECK(::SendMessageW(Child(window, SourceCombo), CB_GETDROPPEDSTATE, 0, 0));
                ::SendMessageW(Child(window, SourceCombo), CB_SHOWDROPDOWN, FALSE, 0);
                ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
                ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
                ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
                ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
                QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
            }
            QIQI_TEST_CHECK(::GetFocus() == Child(window, target));
        }
    }
    search(L"abcd");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && !visible(EditWordInput));
    begin(0, ColumnId::Code);
    check_position(0);
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
    ::RedrawWindow(window.Handle(), nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    ::RedrawWindow(list, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    {
        const ScopedWindowDC dc(Child(window, InlineSave));
        QIQI_TEST_CHECK(dc);
        const auto button = ToolbarItemRect(window, SaveWord);
        size_t ink = 0;
        for (int y = button.top + 2; y < button.bottom - 2; ++y) {
            for (int x = button.left + 2; x < button.right - 2; ++x) {
                if (::GetPixel(dc.Get(), x, y) == GetCurrentThemePalette().text_primary) ++ink;
            }
        }
        QIQI_TEST_CHECK(ink > 10);
    }
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"abcd");
    QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"原始词");
    QIQI_TEST_CHECK(::IsWindowEnabled(Child(window, SearchEdit)) && ::IsWindowEnabled(Child(window, SourceCombo)));
    QIQI_TEST_CHECK(
        (::GetMenuState(::GetMenu(window.Handle()), std::to_underlying(Delete), MF_BYCOMMAND) & MF_GRAYED) != 0);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"暂时修改"));
    QIQI_TEST_CHECK(marked() && ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"原始词"));
    QIQI_TEST_CHECK(!marked() && !ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"temp"));
    QIQI_TEST_CHECK(marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"abcd"));
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"wxyz"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"行内新词"));
    QIQI_TEST_CHECK(marked());
    InvokeCommand(window, Reload);
    InvokeCommand(window, Delete);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"行内新词");

    WubiEngine peer;
    peer.SetUserDictPath(user_dict);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"drop").empty());
    ::SendMessageW(Child(window, EditWordInput), WM_KILLFOCUS, reinterpret_cast<WPARAM>(list), 0);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(peer.LookupExact(L"abcd").front() == L"原始词" && peer.LookupExact(L"wxyz").empty());
    ::SendMessageW(Child(window, EditWordInput), WM_IME_STARTCOMPOSITION, 0, 0);
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_ESCAPE, 0);
    QIQI_TEST_CHECK(visible(EditWordInput));
    QIQI_TEST_CHECK(marked() && ControlText(Child(window, EditWordInput)) == L"行内新词");
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz");
    ::SendMessageW(Child(window, EditWordInput), WM_IME_ENDCOMPOSITION, 0, 0);
    QIQI_TEST_CHECK((::SendMessageW(Child(window, EditWordInput), WM_GETDLGCODE, VK_RETURN, 0) & DLGC_WANTMESSAGE) !=
                    0);
    QIQI_TEST_CHECK((::SendMessageW(Child(window, EditWordInput), WM_GETDLGCODE, VK_ESCAPE, 0) & DLGC_WANTMESSAGE) !=
                    0);

    SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 3840, 2160}, 96});
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        RECT rect{0, 0, ScalePxByDpi(1020, dpi), ScalePxByDpi(680, dpi)};
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        for (const auto language : {LanguageEn, LanguageZh}) {
            InvokeCommand(window, language);
            for (const bool dark : {false, true}) {
                SetThemeDarkOverrideForTest(dark);
                ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);
                ClickRowEdit(window, 0);
                QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"行内新词");
                QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz");
                QIQI_TEST_CHECK(marked() && ControlText(Child(window, Status)).starts_with(L"* "));
                check_position(0);
                QIQI_TEST_CHECK(ControlText(Child(window, SearchEdit)) == L"abcd");
            }
        }
    }
    for (const UINT dpi : {120u, 144u, 192u}) {
        SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 1024, 728}, 96});
        RECT rect{0, 0, ScalePxByDpi(1020, dpi), ScalePxByDpi(680, dpi)};
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        RECT client{}, action{};
        QIQI_TEST_CHECK(::GetClientRect(list, &client));
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        QIQI_TEST_CHECK(ListView_GetSubItemRect(list, 0, std::to_underlying(ColumnId::Action), LVIR_BOUNDS, &action));
        QIQI_TEST_CHECK(action.right > client.right);
        QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(EditCodeInput) && !visible(InlineSave));
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"行内新词");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz" && marked());
        QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(peer.LookupExact(L"abcd").front() == L"原始词" && peer.LookupExact(L"wxyz").empty());

        SetMonitorMetricsOverrideForTest(MonitorMetrics{{0, 0, 3840, 2160}, 96});
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&rect));
        QIQI_TEST_CHECK(visible(EditWordInput) && visible(EditCodeInput) && visible(InlineSave));
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"行内新词");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"wxyz" && marked());
        check_position(0);
    }
    SetThemeDarkOverrideForTest(std::nullopt);
    QIQI_TEST_CHECK(peer.AddUserEntry(L"aaaa", L"外部新增词"));
    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ::IsWindowEnabled(Child(window, SearchEdit)));
    QIQI_TEST_CHECK(!ControlText(Child(window, Status)).starts_with(L"* "));
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"abcd").empty() && peer.LookupExact(L"wxyz").front() == L"行内新词");
    QIQI_TEST_CHECK(peer.LookupExact(L"aaaa").front() == L"外部新增词");

    search(L"wxyz");
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ::SendMessageW(list, WM_KEYDOWN, VK_F2, 0);
    QIQI_TEST_CHECK(visible(EditWordInput));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"a1"));
    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditCodeInput)) == L"a1");
    ::SendMessageW(Child(window, EditCodeInput), WM_KEYDOWN, VK_ESCAPE, 0);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(peer.RemoveUserEntry(L"wxyz", L"行内新词"));
    QIQI_TEST_CHECK(peer.AddUserEntry(L"wxyz", L"远端修改词"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"不能覆盖远端"));
    InvokeCommand(window, SaveWord);
    QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"不能覆盖远端");
    QIQI_TEST_CHECK(peer.LookupExact(L"wxyz").front() == L"远端修改词");
    InvokeCommand(window, CancelEdit);

    search(L"");
    begin(0, ColumnId::Word);
    ListView_EnsureVisible(list, ListView_GetItemCount(list) - 1, FALSE);
    QIQI_TEST_CHECK(!visible(EditWordInput) && !visible(InlineSave));
    ListView_EnsureVisible(list, 0, FALSE);
    QIQI_TEST_CHECK(visible(EditWordInput));
    check_position(0);
    const int width = ListView_GetColumnWidth(list, std::to_underlying(ColumnId::Word));
    const int wider_column = width + ScalePxByDpi(20, 192);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), wider_column);
    check_position(0);
    RECT client{};
    ::GetClientRect(list, &client);
    const int oversized_column = width + gsl::narrow_cast<int>(client.right);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), oversized_column);
    QIQI_TEST_CHECK(!visible(EditWordInput));
    ListView_Scroll(list, ScalePxByDpi(60, 192), 0);
    QIQI_TEST_CHECK(!visible(EditWordInput));
    ListView_Scroll(list, -::GetScrollPos(list, SB_HORZ), 0);
    ListView_SetColumnWidth(list, std::to_underlying(ColumnId::Word), width);
    QIQI_TEST_CHECK(visible(EditWordInput));
    check_position(0);
    InvokeCommand(window, CancelEdit);

    search(L"不存在");
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 0);
    InvokeCommand(window, NewWord, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1 && visible(EditWordInput));
    QIQI_TEST_CHECK(!marked());
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    QIQI_TEST_CHECK(marked());
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"test"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中国"));
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"test");
    InvokeCommand(window, AutoCode);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"khlg");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"khlg");
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"test"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中国"));
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"test").front() == L"中国");

    search(L"test");
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"删除后的草稿"));
    QIQI_TEST_CHECK(peer.RemoveUserEntry(L"test", L"中国"));
    for (int attempt = 0; attempt < 2; ++attempt) {
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && marked());
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"删除后的草稿");
        QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
        QIQI_TEST_CHECK(peer.LookupExact(L"test").empty());
    }
    InvokeCommand(window, CancelEdit);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 0);
    QIQI_TEST_CHECK(peer.AddUserEntry(L"test", L"中国"));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);

    search(L"test");
    begin();
    InvokeCommand(window, SaveWord);
    QIQI_TEST_CHECK(!visible(EditWordInput) && ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"失败草稿"));
    {
        const ScopedFileHandle locked(::CreateFileW(user_dict.c_str(), GENERIC_READ, 0, nullptr,
                                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        QIQI_TEST_CHECK(locked);
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && ControlText(Child(window, EditWordInput)) == L"失败草稿");
        QIQI_TEST_CHECK(marked());
        const auto status = ControlText(Child(window, Status));
        QIQI_TEST_CHECK(status.find(L"0x80070020") != std::wstring::npos);
        QIQI_TEST_CHECK(status.find(L"访问被拒绝") == std::wstring::npos);
    }
    {
        const DWORD attributes = ::GetFileAttributesW(user_dict.c_str());
        QIQI_TEST_CHECK(attributes != INVALID_FILE_ATTRIBUTES);
        const auto restore_attributes = gsl::finally([&]() noexcept {
            QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes));
        });
        QIQI_TEST_CHECK(::SetFileAttributesW(user_dict.c_str(), attributes | FILE_ATTRIBUTE_READONLY));
        for (const auto language : {LanguageEn, LanguageZh}) {
            InvokeCommand(window, language);
            InvokeCommand(window, SaveWord);
            QIQI_TEST_CHECK(visible(EditWordInput) && marked());
            QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"失败草稿");
            const auto status = ControlText(Child(window, Status));
            QIQI_TEST_CHECK(status.find(L"0x80070005") != std::wstring::npos);
            QIQI_TEST_CHECK(status.find(language == LanguageEn ? L"access denied" : L"访问被拒绝") != std::wstring::npos);
        }
        InvokeCommand(window, CancelEdit);
        InvokeCommand(window, NewWord);
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"deny"));
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"无权新增草稿"));
        InvokeCommand(window, SaveWord);
        QIQI_TEST_CHECK(visible(EditWordInput) && marked());
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"无权新增草稿");
        QIQI_TEST_CHECK(ControlText(Child(window, Status)).find(L"访问被拒绝") != std::wstring::npos);
    }
    InvokeCommand(window, CancelEdit);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"test").front() == L"中国");

    QIQI_TEST_CHECK(peer.PinUserEntry(L"test", L"置顶原词"));
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
    begin();
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"用户副本"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"copy"));
    ::SendMessageW(Child(window, EditWordInput), WM_KEYDOWN, VK_RETURN, 0);
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"copy").front() == L"用户副本");
    QIQI_TEST_CHECK(peer.PinnedEntries().at(PackWubiCode(L"test")).front() == L"置顶原词");

    search(L"不存在");
    InvokeCommand(window, NewWord);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"退出确认草稿"));
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditCodeInput), L"exit"));
    const UINT_PTR timer = ::SetTimer(window.Handle(), 19, 20, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
        const HWND popup = ::GetLastActivePopup(owner);
        if (popup != owner) {
            ::KillTimer(owner, id);
            ::PostMessageW(popup, WM_COMMAND, IDNO, 0);
        }
    });
    QIQI_TEST_CHECK(timer != 0);
    const HWND closing = window.Handle();
    InvokeCommand(window, Exit);
    if (::IsWindow(closing)) ::KillTimer(closing, timer);
    QIQI_TEST_CHECK(!::IsWindow(closing));
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"exit").empty());
    DrainQuitMessages();

    for (const int mode : {0, 1, 2, 3, 4}) {
        DictManagerWindow unchanged;
        QIQI_TEST_CHECK(unchanged.Create(::GetModuleHandleW(nullptr), user_dict, freq_file));
        if (mode < 2 || mode == 4) {
            ::SendMessageW(Child(unchanged, SourceCombo), CB_SETCURSEL, 1, 0);
            ::SendMessageW(unchanged.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, SearchEdit), L"test"));
            const HWND unchanged_list = Child(unchanged, List);
            ListView_SetItemState(unchanged_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ::SendMessageW(unchanged_list, WM_KEYDOWN, VK_F2, 0);
            QIQI_TEST_CHECK(ControlText(Child(unchanged, EditWordInput)) == L"中国");
        } else {
            InvokeCommand(unchanged, NewWord);
        }
        if (mode == 1 || mode == 3) {
            const auto original_word = ControlText(Child(unchanged, EditWordInput));
            const auto original_code = ControlText(Child(unchanged, EditCodeInput));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), L"临时更改"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), original_word.c_str()));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditCodeInput), original_code.c_str()));
        }
        if (mode == 4) {
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditWordInput), L"关闭时放弃更改"));
            QIQI_TEST_CHECK(::SetWindowTextW(Child(unchanged, EditCodeInput), L"exit"));
        }
        QIQI_TEST_CHECK(ControlText(Child(unchanged, Status)).starts_with(L"* ") == (mode == 4));
        const UINT_PTR dismiss = ::SetTimer(unchanged.Handle(), 19, 20, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            const HWND popup = ::GetLastActivePopup(owner);
            if (popup != owner) {
                ::KillTimer(owner, id);
                ::PostMessageW(popup, WM_COMMAND, IDNO, 0);
            }
        });
        QIQI_TEST_CHECK(dismiss != 0);
        const HWND handle = unchanged.Handle();
        ::SendMessageW(handle, WM_CLOSE, 0, 0);
        if (::IsWindow(handle)) ::KillTimer(handle, dismiss);
        QIQI_TEST_CHECK(!::IsWindow(handle));
        DrainQuitMessages();
    }
    QIQI_TEST_CHECK(peer.SynchronizeUserDictionary(sync::InputBoundary::NewInput) == S_OK);
    QIQI_TEST_CHECK(peer.LookupExact(L"exit").empty());
    QIQI_TEST_CHECK(std::ranges::contains(peer.LookupExact(L"test"), L"中国"));
}

void ExerciseGuiCommands(const DictManagerWindow& window) {
    // Type a phrase into EditWordInput -> verify AutoCode fills EditCodeInput with "kwwl"
    InvokeCommand(window, NewWord, true);
    QIQI_TEST_CHECK(::SetWindowTextW(Child(window, EditWordInput), L"中华人民共和国"));
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(EditWordInput), EN_CHANGE), 0);
    QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");

    InvokeCommand(window, SaveWord, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) >= 1);

    // Select first row, promote phrase frequency, and filter by PhraseFreq source (combo index 4)
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Promote, true);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);

    // Pin entry and verify Pinned source filter (combo index 2).
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Pin, true);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);

    // Query LVN_GETDISPINFOW for all 4 columns
    const HWND list = Child(window, List);
    for (int col = std::to_underlying(ColumnId::Code); col <= std::to_underlying(ColumnId::Rank); ++col) {
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

    // Exercise filters, menu/toolbar commands, keyboard shortcuts, and display changes.
    const auto search = [&](const wchar_t* query, int count) {
        QIQI_TEST_CHECK(::SetWindowTextW(Child(window, SearchEdit), query));
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == count);
        QIQI_TEST_CHECK(ControlText(Child(window, EditWordInput)) == L"中华人民共和国");
        QIQI_TEST_CHECK(ControlText(Child(window, EditCodeInput)) == L"kwwl");
    };
    search(L" KW ", 1);
    search(L"人民", 1);
    for (const auto language : {LanguageEn, LanguageZh}) {
        InvokeCommand(window, language);
        QIQI_TEST_CHECK(ControlText(Child(window, SearchEdit)) == L"人民");
        QIQI_TEST_CHECK(ListView_GetItemCount(list) == 1);
        QIQI_TEST_CHECK(ControlText(Child(window, SearchLabel)) == (language == LanguageEn ? L"Search" : L"搜索"));
        std::array<wchar_t, 128> cue{};
        QIQI_TEST_CHECK(::SendMessageW(Child(window, SearchEdit), EM_GETCUEBANNER,
            reinterpret_cast<WPARAM>(cue.data()), gsl::narrow_cast<LPARAM>(cue.size())));
        QIQI_TEST_CHECK(std::wstring_view(cue.data()) == (language == LanguageEn
            ? L"Wubi code or word (letters search codes)" : L"五笔编码或词条（纯字母按编码搜索）"));
    }
    search(L"不存在", 0);
    search(L"KW人民", 0);
    search(L"k w", 0);
    search(L"KWWL", 1);
    search(L" \t ", 1);
    search(L"", 1);

    InvokeCommand(window, AutoCode);
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, SelectAll);
    QIQI_TEST_CHECK(ListView_GetSelectedCount(list) == gsl::narrow_cast<UINT>(ListView_GetItemCount(list)));
    InvokeCommand(window, Copy, true);
    InvokeCommand(window, Reload, true);
    ::SendMessageW(window.Handle(), WM_TIMER, 1, 0);

    // Keyboard shortcuts on ListView (Ctrl+A, Ctrl+C, VK_DELETE)
    {
        std::array<BYTE, 256> keys{};
        QIQI_TEST_CHECK(::GetKeyboardState(keys.data()));
        const auto restore_keys = gsl::finally([&keys]() noexcept { ::SetKeyboardState(keys.data()); });
        std::array<BYTE, 256> ctrl_down = keys;
        gsl::at(ctrl_down, VK_CONTROL) = 0x80;
        QIQI_TEST_CHECK(::SetKeyboardState(ctrl_down.data()));
        ::SendMessageW(list, WM_KEYDOWN, 'A', 0);
        ::SendMessageW(list, WM_KEYDOWN, 'C', 0);
    }

    // Context menu open & cancel
    {
        const UINT_PTR cancel_timer = ::SetTimer(window.Handle(), 9, 15, [](HWND owner, UINT, UINT_PTR id, DWORD) noexcept {
            ::KillTimer(owner, id);
            ::SendMessageW(owner, WM_CANCELMODE, 0, 0);
        });
        QIQI_TEST_CHECK(cancel_timer != 0);
        const auto kill_timer = gsl::finally([&window, cancel_timer]() noexcept {
            ::KillTimer(window.Handle(), cancel_timer);
        });
        ::SendMessageW(window.Handle(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(list), MAKELPARAM(-1, -1));
    }

    // DPI, theme, and display recovery messages
    RECT wnd_rect{};
    ::GetWindowRect(window.Handle(), &wnd_rect);
    ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&wnd_rect));
    ::SendMessageW(window.Handle(), WM_DISPLAYCHANGE, 0, 0);
    ::SendMessageW(window.Handle(), WM_SETTINGCHANGE, SPI_SETWORKAREA, 0);
    ::SendMessageW(window.Handle(), WM_THEMECHANGED, 0, 0);

    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 2, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Pin); // Unpin when already pinned.
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    // Switch language between EnUs and ZhCn
    InvokeCommand(window, LanguageEn);
    QIQI_TEST_CHECK(MenuText(::GetMenu(window.Handle()), Reload) == L"Reload");
    InvokeCommand(window, LanguageZh);
    QIQI_TEST_CHECK(MenuText(::GetMenu(window.Handle()), Reload) == L"刷新");

    // Clear frequencies, switch back to UserDict (combo index 1), delete entry
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 4, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    InvokeCommand(window, ClearFreq);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);
    ::SendMessageW(Child(window, SourceCombo), CB_SETCURSEL, 1, 0);
    ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(std::to_underlying(SourceCombo), CBN_SELCHANGE), 0);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 1);
    ListView_SetItemState(Child(window, List), 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    InvokeCommand(window, Delete, true);
    QIQI_TEST_CHECK(ListView_GetItemCount(Child(window, List)) == 0);

    InvokeCommand(window, Exit);
    DrainQuitMessages();
}

} // namespace

int main() {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    TestVersionResource();
    TestSmartSearch();
    TestDictionarySourceSelection();
    TestModelAndCrossProcessSync();
    TestBackupRestoreResults();
    TestEditorState();
    TestGuiWindow();
    TestInlineEditing();
    std::cout << "[PASS] QiQiDictManagerTest\n";
    return 0;
}
