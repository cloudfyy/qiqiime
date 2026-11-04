#include "tools/dict_manager/internal.h"
#include "common/about_dialog.h"
#include "common/file_util.h"
#include "common/theme.h"
#include "common/tool_window.h"
#include "common/version.h"
#include "common/win32_ui_util.h"

#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <commdlg.h>
#include <format>
#include <stdexcept>
#include <span>
#include <system_error>
#include <uxtheme.h>

namespace qiqi::dict_manager {
using enum ControlId;

namespace {
constexpr std::array toolbar_commands{
    Reload, NewWord, Copy, Import, Export, Pin, Delete, Promote};

[[noreturn]] void ThrowMenuError(const char* operation, DWORD error) {
    QIQI_LOG_ERROR("DictManager {} failed (error={})", operation, error);
    throw std::system_error(error, std::system_category(), operation);
}

void DrawToolbarIcon(HDC dc, const RECT& rect, ControlId command, int size, int stroke, COLORREF color) noexcept {
    const ScopedGdiHandle<HPEN> pen(::CreatePen(PS_SOLID, stroke, color));
    if (!pen) {
        QIQI_LOG_ERROR("DictManager toolbar icon pen creation failed");
        return;
    }
    const ScopedSelectObject selected_pen(dc, pen.Get());
    const ScopedSelectObject selected_brush(dc, ::GetStockObject(NULL_BRUSH));
    if (!selected_pen || !selected_brush) {
        QIQI_LOG_ERROR("DictManager toolbar icon GDI object selection failed");
        return;
    }
    const int left = rect.left + (rect.right - rect.left - size) / 2;
    const int top = rect.top + (rect.bottom - rect.top - size) / 2;
    const auto x = [&](int value) noexcept { return left + ::MulDiv(value, size, 24); };
    const auto y = [&](int value) noexcept { return top + ::MulDiv(value, size, 24); };
    bool drawn = true;
    const auto path = [&](std::initializer_list<POINT> points) noexcept {
        std::array<POINT, 8> scaled{};
        size_t count = 0;
        for (const auto& point : points) {
            gsl::at(scaled, count++) = {x(point.x), y(point.y)};
        }
        if (!::Polyline(dc, scaled.data(), gsl::narrow_cast<int>(count))) drawn = false;
    };
    const auto box = [&](int l, int t, int r, int b) noexcept {
        if (!::Rectangle(dc, x(l), y(t), x(r), y(b))) drawn = false;
    };
    switch (command) {
    case NewWord:
        path({{12, 3}, {12, 21}});
        path({{3, 12}, {21, 12}});
        break;
    case Reload:
        if (!::Arc(dc, x(4), y(4), x(20), y(20), x(20), y(4), x(20), y(12))) drawn = false;
        path({{16, 8}, {20, 12}, {23, 8}});
        break;
    case Copy:
        box(4, 8, 16, 21);
        path({{9, 8}, {9, 3}, {21, 3}, {21, 16}, {16, 16}});
        break;
    case Import:
        path({{4, 16}, {4, 21}, {20, 21}, {20, 16}});
        path({{12, 3}, {12, 16}});
        path({{7, 11}, {12, 16}, {17, 11}});
        break;
    case Export:
        path({{4, 16}, {4, 21}, {20, 21}, {20, 16}});
        path({{12, 16}, {12, 3}});
        path({{7, 8}, {12, 3}, {17, 8}});
        break;
    case SaveWord:
        path({{4, 3}, {17, 3}, {21, 7}, {21, 21}, {4, 21}, {4, 3}});
        box(8, 3, 16, 10);
        box(8, 14, 17, 21);
        break;
    case Pin:
        path({{8, 3}, {16, 3}, {15, 11}, {19, 16}, {5, 16}, {9, 11}, {8, 3}});
        path({{12, 16}, {12, 23}});
        break;
    case Delete:
        path({{3, 6}, {21, 6}});
        path({{8, 6}, {8, 3}, {16, 3}, {16, 6}});
        path({{6, 6}, {7, 21}, {17, 21}, {18, 6}});
        path({{10, 10}, {10, 17}});
        path({{14, 10}, {14, 17}});
        break;
    case Promote:
        path({{4, 21}, {4, 15}, {8, 15}, {8, 21}});
        path({{11, 21}, {11, 11}, {15, 11}, {15, 21}});
        path({{20, 21}, {20, 3}});
        path({{16, 7}, {20, 3}, {24, 7}});
        break;
    default:
        QIQI_LOG_ERROR("DictManager toolbar icon command is not supported");
        return;
    }
    if (!drawn) QIQI_LOG_ERROR("DictManager toolbar icon drawing failed");
}
}

const wchar_t* DictManagerWindow::Impl::CommandText(ControlId id) const noexcept {
    switch (id) {
    case Reload: return Text(L"刷新", L"Reload");
    case Copy: return Text(L"复制", L"Copy");
    case Import: return Text(L"导入词库...", L"Import...");
    case Export: return Text(L"导出快照...", L"Export...");
    case Backup: return Text(L"备份全部...", L"Backup...");
    case Restore: return Text(L"恢复备份...", L"Restore...");
    case ClearFreq: return Text(L"清空词频", L"Clear Frequencies");
    case About: return Text(L"关于...", L"About...");
    case AutoCode: return Text(L"自动取码", L"Auto Code");
    case NewWord: return Text(L"新增词条", L"New Word");
    case CancelEdit: return Text(L"取消编辑\tEsc", L"Cancel Edit\tEsc");
    case SaveWord: return Text(L"保存用户词", L"Save Word");
    case Pin: return Text(L"置顶 / 取消置顶", L"Pin / Unpin");
    case Delete: return Text(L"删除 / 屏蔽 / 恢复", L"Delete / Block / Restore");
    case Promote: return Text(L"词频置顶", L"Promote Frequency");
    case SelectAll: return Text(L"全选", L"Select All");
    case LanguageAuto: return GetUiStrings(ui_language).dlg_lang_auto;
    case LanguageZh: return GetUiStrings(ui_language).dlg_lang_zh;
    case LanguageEn: return GetUiStrings(ui_language).dlg_lang_en;
    case Exit: return Text(L"退出", L"Exit");
    default: return L"";
    }
}

const wchar_t* DictManagerWindow::Impl::MenuHeading(size_t index) const noexcept {
    switch (index) {
    case 0: return Text(L"文件(&F)", L"&File");
    case 1: return Text(L"编辑(&E)", L"&Edit");
    case 2: return Text(L"语言(&L)", L"&Language");
    default: return Text(L"帮助(&H)", L"&Help");
    }
}

void DictManagerWindow::Impl::UpdateCommands() {
    ScopedMenu menu(::CreateMenu());
    if (!menu) ThrowMenuError("CreateMenu", ::GetLastError());
    const auto append = [](HMENU target, UINT flags, UINT_PTR id, const wchar_t* text) {
        if (!::AppendMenuW(target, flags, id, text)) {
            ThrowMenuError("AppendMenuW", ::GetLastError());
        }
    };
    const auto group = [&](size_t index, std::initializer_list<ControlId> commands) {
        ScopedMenu popup(::CreatePopupMenu());
        if (!popup) ThrowMenuError("CreatePopupMenu", ::GetLastError());
        for (const auto id : commands) {
            if (id == Separator) {
                append(popup.Get(), MF_SEPARATOR, 0, nullptr);
            } else {
                append(popup.Get(), MF_STRING, std::to_underlying(id), CommandText(id));
            }
        }
        if (index == 2 && !::CheckMenuRadioItem(
                popup.Get(), std::to_underlying(LanguageAuto), std::to_underlying(LanguageEn),
                std::to_underlying(LanguageAuto) + std::to_underlying(ui_language), MF_BYCOMMAND)) {
            ThrowMenuError("CheckMenuRadioItem", ::GetLastError());
        }
        append(menu.Get(), MF_POPUP, reinterpret_cast<UINT_PTR>(popup.Get()), MenuHeading(index));
        std::ignore = popup.Detach();
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA;
        info.fType = MFT_OWNERDRAW;
        info.dwItemData = index;
        if (!::SetMenuItemInfoW(menu.Get(), gsl::narrow_cast<UINT>(index), TRUE, &info)) {
            ThrowMenuError("SetMenuItemInfoW", ::GetLastError());
        }
    };
    group(0, {Reload, Separator, Import, Export, Separator, Backup, Restore, Separator, Exit});
    group(1, {NewWord, SaveWord, CancelEdit, AutoCode, Separator, Copy, SelectAll, Pin, Delete, Promote, Separator, ClearFreq});
    group(2, {LanguageAuto, LanguageZh, LanguageEn});
    group(3, {About});
    const HMENU previous = ::GetMenu(hwnd);
    if (!::SetMenu(hwnd, menu.Get())) {
        ThrowMenuError("SetMenu", ::GetLastError());
    }
    std::ignore = menu.Detach(); // The window owns the attached menu.
    const ScopedMenu old_menu(previous);

    constexpr std::array save_commands{SaveWord};
    for (const HWND toolbar : {Child(Toolbar), Child(InlineSave)}) {
        while (::SendMessageW(toolbar, TB_BUTTONCOUNT, 0, 0) > 0) {
            if (!::SendMessageW(toolbar, TB_DELETEBUTTON, 0, 0)) {
                QIQI_LOG_ERROR("DictManager TB_DELETEBUTTON failed");
                throw std::runtime_error("TB_DELETEBUTTON failed");
            }
        }
        const auto commands = toolbar == Child(Toolbar)
            ? std::span<const ControlId>(toolbar_commands) : std::span<const ControlId>(save_commands);
        for (const auto id : commands) {
            TBBUTTON item{};
            item.iBitmap = I_IMAGENONE;
            item.idCommand = std::to_underlying(id);
            item.fsState = TBSTATE_ENABLED;
            item.fsStyle = BTNS_BUTTON;
            item.iString = reinterpret_cast<INT_PTR>(CommandText(id));
            if (!::SendMessageW(toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&item))) {
                QIQI_LOG_ERROR("DictManager TB_ADDBUTTONSW failed");
                throw std::runtime_error("TB_ADDBUTTONSW failed");
            }
        }
        ::SendMessageW(toolbar, TB_AUTOSIZE, 0, 0);
    }
    UpdateCommandState();
    ::DrawMenuBar(hwnd);
}

void DictManagerWindow::Impl::Failure(std::wstring_view operation, ULONG error) {
    QIQI_LOG_CAT_ERROR(
        log::Keyword::Dictionary,
        "DictManager failure: op='{}', error=0x{:08X}",
        WideToUtf8(operation),
        error);
    const auto message = std::format(L"{}\r\n{}={} (0x{:08X})", operation, Text(L"错误码", L"Error"), error, error);
    ::MessageBoxW(hwnd, message.c_str(), Text(L"奇奇五笔词库管理", L"QiQiIME Dictionary Manager"), MB_OK | MB_ICONERROR);
}

void DictManagerWindow::Impl::UpdateIcons() noexcept {
    std::ignore = UpdateWindowBrandIcons(hwnd, instance, theme, small_icon, big_icon);
}

bool DictManagerWindow::Impl::Add(const wchar_t* cls, const wchar_t* text, DWORD style, ControlId id) {
    const auto* child = CreateFluentChildControl(
        hwnd, instance, font.Get(), &theme, cls, text, style, 0, 0, 10, 10, std::to_underlying(id));
    if (!child) {
        const DWORD error = ::GetLastError();
        Failure(L"CreateFluentChildControl", error);
        return false;
    }
    return true;
}

LRESULT CALLBACK DictManagerWindow::Impl::ListProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, ListProcedure, subclass);
    } else if (message == WM_KEYDOWN && self && self->ready) {
        try {
            if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (wparam == 'C' || wparam == VK_INSERT) {
                    self->CopySelection();
                    return 0;
                }
                if (wparam == 'A') {
                    self->SelectAllRows();
                    return 0;
                }
            } else if (wparam == VK_DELETE) {
                self->Command(std::to_underlying(Delete), 0);
                return 0;
            } else if (wparam == VK_F2) {
                self->BeginEdit(ListView_GetNextItem(window, -1, LVNI_FOCUSED), 1);
                return 0;
            } else if (wparam == VK_ESCAPE && self->editing) {
                self->CancelEditing();
                return 0;
            }
        } catch (const std::exception&) {
            QIQI_LOG_CAT_ERROR(log::Keyword::Dictionary, "DictManager ListProcedure shortcut failed with an exception");
            return 0;
        }
    } else if (message == WM_NOTIFY && self) {
        if (const auto drawn = TryCustomDrawDarkListViewHeader(window, lparam, self->font.Get(), self->theme)) {
            return *drawn;
        }
    }
    const LRESULT result = ::DefSubclassProc(window, message, wparam, lparam);
    if (self && self->ready && (message == WM_HSCROLL || message == WM_VSCROLL ||
            message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL || message == WM_SIZE ||
            message == WM_NOTIFY || message == LVM_SCROLL || message == LVM_ENSUREVISIBLE)) {
        self->PositionEditor();
    }
    return result;
}

LRESULT CALLBACK DictManagerWindow::Impl::EditorProcedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) noexcept {
    auto* self = reinterpret_cast<Impl*>(context);
    if (message == WM_NCDESTROY) {
        ::RemoveWindowSubclass(window, EditorProcedure, subclass);
    } else if (self) {
        if (message == WM_IME_STARTCOMPOSITION) self->ime_composing = true;
        if (message == WM_IME_ENDCOMPOSITION) self->ime_composing = false;
        if (message == WM_GETDLGCODE && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            return ::DefSubclassProc(window, message, wparam, lparam) | DLGC_WANTMESSAGE;
        }
        if (message == WM_CHAR && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) return 0;
        if (message == WM_KEYDOWN && !self->ime_composing && (wparam == VK_RETURN || wparam == VK_ESCAPE)) {
            try {
                if (wparam == VK_RETURN) self->ExecuteAddOrUpdate();
                else self->CancelEditing();
            } catch (const std::exception&) {
                QIQI_LOG_ERROR("DictManager inline editor keyboard operation failed");
                ::SetWindowTextW(self->Child(Status), self->Text(L"编辑操作失败，请重试。", L"Edit operation failed; please retry."));
            }
            return 0;
        }
    }
    return ::DefSubclassProc(window, message, wparam, lparam);
}

bool DictManagerWindow::Impl::CreateControls() {
    theme.Init(::GetDpiForWindow(hwnd));
    font = CreateUiFont(theme.ScalePx(14));
    if (!font) {
        Failure(L"CreateUiFont", ::GetLastError());
        return false;
    }
    if (!Add(TOOLBARCLASSNAMEW, L"", TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_WRAPABLE |
                 TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NODIVIDER | WS_TABSTOP, Toolbar) ||
        !Add(L"STATIC", Text(L"来源", L"Source"), SS_LEFT, SourceLabel) ||
        !Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, SourceCombo) ||
        !Add(L"STATIC", Text(L"搜索", L"Search"), SS_LEFT, SearchLabel) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, SearchEdit) ||
        !Add(WC_LISTVIEWW, L"", LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, List) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditWordInput) ||
        !Add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, EditCodeInput) ||
        !Add(TOOLBARCLASSNAMEW, L"", TBSTYLE_FLAT | TBSTYLE_TOOLTIPS | CCS_NORESIZE |
                 CCS_NOPARENTALIGN | CCS_NODIVIDER | WS_TABSTOP, InlineSave) ||
        !Add(L"STATIC", L"", SS_LEFT, Status)) {
        return false;
    }
    for (const auto id : {Toolbar, InlineSave}) {
        ::SendMessageW(Child(id), TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        ::SendMessageW(Child(id), TB_SETMAXTEXTROWS, 0, 0);
        ::SendMessageW(Child(id), TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
    }
    // The row toolbar has no bitmap; avoid the native default bitmap's minimum button size.
    ::SendMessageW(Child(InlineSave), TB_SETBITMAPSIZE, 0, MAKELPARAM(1, 1));
    for (const auto id : {EditWordInput, EditCodeInput, InlineSave}) {
        ::ShowWindow(Child(id), SW_HIDE);
        if (!::SetWindowSubclass(Child(id), EditorProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
            QIQI_LOG_ERROR("DictManager inline editor subclass installation failed");
            return false;
        }
    }

    ListView_SetExtendedListViewStyle(Child(List), LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    if (!::SetWindowSubclass(Child(List), ListProcedure, 1, reinterpret_cast<DWORD_PTR>(this))) {
        Failure(L"SetWindowSubclass(list)", ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    std::array<const wchar_t*, 5> columns{
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank"), Text(L"操作", L"Action")};
    constexpr std::array widths{120, 320, 180, 76, 52};
    for (size_t i = 0; i < columns.size(); ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        std::wstring text(gsl::at(columns, i));
        column.pszText = text.data();
        column.cx = theme.ScalePx(gsl::at(widths, i));
        if (ListView_InsertColumn(Child(List), gsl::narrow_cast<int>(i), &column) < 0) {
            Failure(L"ListView_InsertColumn", ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
    }

    ready = true;
    UpdateIcons();
    ApplyLanguage();
    UpdateTheme();
    RebuildRows();
    UpdateStatus();
    if (!::SetTimer(hwnd, 1, 500, nullptr)) {
        Failure(L"SetTimer", ::GetLastError());
        return false;
    }
    return true;
}

void DictManagerWindow::Impl::Layout() noexcept {
    if (!ready) return;
    RECT rect{};
    ::GetClientRect(hwnd, &rect);
    const auto s = [this](int value) noexcept { return theme.ScalePx(value); };
    const int width = rect.right;
    const auto move = [&](ControlId id, int x, int y, int w, int h) noexcept {
        ::MoveWindow(Child(id), x, y, std::max(1, w), std::max(1, h), TRUE);
    };

    const int button_size = s(36);
    ::SendMessageW(Child(Toolbar), TB_SETBUTTONSIZE, 0, MAKELPARAM(button_size, button_size));
    ::SendMessageW(Child(Toolbar), TB_SETBUTTONWIDTH, 0, MAKELPARAM(button_size, button_size));
    ::SendMessageW(Child(Toolbar), TB_AUTOSIZE, 0, 0);
    RECT toolbar_rect{};
    ::GetWindowRect(Child(Toolbar), &toolbar_rect);
    const int filter_top = toolbar_rect.bottom - toolbar_rect.top + s(12);
    move(SourceLabel, s(12), filter_top + s(4), s(56), s(24));
    move(SourceCombo, s(72), filter_top, s(165), s(220));
    move(SearchLabel, s(250), filter_top + s(4), s(64), s(24));
    move(SearchEdit, s(318), filter_top, width - s(330), s(28));

    const int list_top = filter_top + s(40);
    move(List, s(12), list_top, width - s(24), std::max(s(80), gsl::narrow_cast<int>(rect.bottom) - s(40) - list_top));

    move(Status, s(12), rect.bottom - s(32), width - s(24), s(24));
    PositionEditor();
}

void DictManagerWindow::Impl::UpdateTheme() noexcept {
    if (!ready) return;
    const std::array controls{
        Child(List), Child(SourceCombo),
        Child(SearchEdit),
        Child(EditWordInput), Child(EditCodeInput)};
    std::ignore = ApplyToolWindowListAndControlsTheme(hwnd, Child(List), theme, controls);
    const bool contrast = IsHighContrastEnabled();
    const auto& palette = GetCurrentThemePalette();
    ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(
        contrast ? ::GetSysColor(COLOR_MENUBAR) : palette.window_bg));
    if (background && ::GetMenu(hwnd)) {
        MENUINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = MIM_BACKGROUND;
        info.hbrBack = background.Get();
        if (::SetMenuInfo(::GetMenu(hwnd), &info)) {
            menu_brush = std::move(background);
        } else {
            QIQI_LOG_ERROR("DictManager SetMenuInfo failed (error={})", ::GetLastError());
        }
    } else if (!background) {
        QIQI_LOG_ERROR("DictManager menu background brush creation failed");
    }
    ::DrawMenuBar(hwnd);
    ::InvalidateRect(Child(Toolbar), nullptr, TRUE);
    ::InvalidateRect(Child(InlineSave), nullptr, TRUE);
    for (const auto id : {Toolbar, InlineSave}) {
        const auto tooltip = reinterpret_cast<HWND>(::SendMessageW(Child(id), TB_GETTOOLTIPS, 0, 0));
        if (tooltip) {
            std::ignore = log::CheckHResult(
                ::SetWindowTheme(tooltip, contrast ? nullptr : (palette.is_dark ? L"DarkMode_Explorer" : L"Explorer"), nullptr),
                "DictManager toolbar tooltip theme");
        }
    }
}

LRESULT DictManagerWindow::Impl::DrawToolbar(NMCUSTOMDRAW& draw) const noexcept {
    const bool contrast = IsHighContrastEnabled();
    const auto& palette = GetCurrentThemePalette();
    const auto fill = [&](const RECT& rect, COLORREF color) noexcept {
        ::SetDCBrushColor(draw.hdc, color);
        ::FillRect(draw.hdc, &rect, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
    };
    if (draw.dwDrawStage == CDDS_PREPAINT) {
        fill(draw.rc, contrast ? ::GetSysColor(COLOR_BTNFACE) : palette.window_bg);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw.dwDrawStage == CDDS_ITEMPREPAINT) {
        const bool hot = (draw.uItemState & (CDIS_HOT | CDIS_SELECTED)) != 0;
        const bool disabled = (draw.uItemState & CDIS_DISABLED) != 0;
        const COLORREF background = contrast
            ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_BTNFACE)
            : (hot ? palette.hover_bg : palette.window_bg);
        const COLORREF foreground = contrast
            ? ::GetSysColor(disabled ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT))
            : (disabled ? palette.grip_dot : palette.text_primary);
        const ScopedSaveDC save(draw.hdc);
        fill(draw.rc, background);
        const bool inline_save = draw.hdr.hwndFrom == Child(InlineSave);
        const int icon_size = inline_save
            ? std::max(1, std::min(theme.ScalePx(16), gsl::narrow_cast<int>(draw.rc.bottom - draw.rc.top) - theme.ScalePx(4)))
            : theme.ScalePx(20);
        DrawToolbarIcon(draw.hdc, draw.rc, gsl::narrow_cast<ControlId>(draw.dwItemSpec),
            icon_size, std::max(1, theme.ScalePx(inline_save ? 1 : 2)), foreground);
        if (draw.uItemState & CDIS_FOCUS) {
            RECT focus_rect = draw.rc;
            ::InflateRect(&focus_rect, -2, -2);
            ::DrawFocusRect(draw.hdc, &focus_rect);
        }
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
}

void DictManagerWindow::Impl::ApplyLanguage() {
    if (!ready) return;
    const auto title = std::format(
        L"{} v{} ({})",
        Text(L"奇奇五笔词库与词频管理", L"QiQiIME Dictionary & Frequency Manager"),
        version::kVersionW,
        version::kArchW);
    ::SetWindowTextW(hwnd, title.c_str());

    UpdateCommands();
    for (const auto& [id, text] : std::array{
             std::pair{SourceLabel, Text(L"来源", L"Source")},
             std::pair{SearchLabel, Text(L"搜索", L"Search")}}) {
        ::SetWindowTextW(Child(id), text);
    }
    if (!::SendMessageW(Child(SearchEdit), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(
            Text(L"五笔编码或词条（纯字母按编码搜索）", L"Wubi code or word (letters search codes)")))) {
        QIQI_LOG_WARN("DictManager search cue banner could not be set");
    }
    for (const auto& [id, cue] : std::array{
             std::pair{EditCodeInput, Text(L"编码", L"Code")}, std::pair{EditWordInput, Text(L"词条", L"Word")}}) {
        if (!::SendMessageW(Child(id), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(cue))) {
            QIQI_LOG_WARN("DictManager inline editor cue banner could not be set");
        }
    }

    const std::array sources{
        SourceText(DictEntrySource::All),
        SourceText(DictEntrySource::UserDict),
        SourceText(DictEntrySource::Pinned),
        SourceText(DictEntrySource::Blocked),
        SourceText(DictEntrySource::PhraseFreq),
        SourceText(DictEntrySource::PrimaryDict),
        SourceText(DictEntrySource::ExtraDict)};
    std::ignore = ReplaceComboBoxItems(Child(SourceCombo), sources);

    const std::array<const wchar_t*, 5> columns{
        Text(L"五笔编码", L"Wubi Code"),
        Text(L"词条内容", L"Word / Phrase"),
        Text(L"数据来源", L"Source"),
        Text(L"同码顺位", L"Rank"), Text(L"操作", L"Action")};
    for (size_t i = 0; i < columns.size(); ++i) {
        std::wstring text(gsl::at(columns, i));
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT;
        column.pszText = text.data();
        ListView_SetColumn(Child(List), gsl::narrow_cast<int>(i), &column);
    }
    UpdateStatus();
    Layout();
    UpdateTheme();
    ::InvalidateRect(Child(List), nullptr, FALSE);
}

void DictManagerWindow::Impl::UpdateDpi(UINT dpi, const RECT& proposed) {
    if (!ready) return;
    const UINT previous = theme.dpi;
    if (dpi != previous) {
        theme.Init(dpi);
        if (auto next_font = CreateUiFont(theme.ScalePx(14))) {
            ::EnumChildWindows(
                hwnd,
                [](HWND child, LPARAM handle) noexcept -> BOOL {
                    ::SendMessageW(child, WM_SETFONT, handle, TRUE);
                    return TRUE;
                },
                reinterpret_cast<LPARAM>(next_font.Get()));
            font = std::move(next_font);
        }
        for (int i = 0; i < 5; ++i) {
            ListView_SetColumnWidth(
                Child(List),
                i,
                ::MulDiv(ListView_GetColumnWidth(Child(List), i), gsl::narrow_cast<int>(dpi), gsl::narrow_cast<int>(previous)));
        }
        for (const auto id : {SourceCombo}) {
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, gsl::narrow_cast<WPARAM>(-1), theme.ScalePx(22));
            ::SendMessageW(Child(id), CB_SETITEMHEIGHT, 0, theme.ScalePx(22));
        }
        UpdateCommands();
        UpdateIcons();
    }
    const POINT center{
        proposed.left + (proposed.right - proposed.left) / 2,
        proposed.top + (proposed.bottom - proposed.top) / 2};
    const auto monitor = QueryMonitorForPoint(center, hwnd);
    const RECT rect = ClampWindowRectToWorkArea(proposed, monitor.work_area);
    ::SetWindowPos(
        hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    UpdateTheme();
}

void DictManagerWindow::Impl::RecoverDisplay() {
    if (!ready || ::IsIconic(hwnd) || ::IsZoomed(hwnd)) return;
    RECT rect{};
    if (!::GetWindowRect(hwnd, &rect)) return;
    const POINT center{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2};
    UpdateDpi(QueryMonitorForPoint(center, hwnd).dpi, rect);
}

void DictManagerWindow::Impl::Refilter() {
    const LRESULT src_sel = ::SendMessageW(Child(SourceCombo), CB_GETCURSEL, 0, 0);
    filter.source = SourceFromComboIndex(src_sel);
    filter.SetQuery(ReadWindowTextString(Child(SearchEdit)));
    RebuildRows();
    UpdateStatus();
}

void DictManagerWindow::Impl::RebuildRows() {
    updating_rows = true;
    const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
    std::vector<uint64_t> next;
    next.reserve(model.Records().size());
    for (const auto& rec : model.Records()) {
        if (filter.Matches(rec)) {
            next.push_back(rec.id);
        }
    }
    if (editing) {
        const auto* original = FindOriginal();
        const auto row = original ? std::ranges::find(next, original->id) : next.end();
        if (row != next.end()) {
            editing_row = gsl::narrow_cast<int>(row - next.begin());
        } else {
            editing_row = std::clamp(editing_row, 0, gsl::narrow_cast<int>(next.size()));
            next.insert(next.begin() + editing_row, 0);
        }
    }
    rows.swap(next);
    const HWND list = Child(List);
    ListView_SetItemCountEx(list, gsl::narrow_cast<int>(rows.size()), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    selected_ids.clear();
    selected_id = 0;
    ::InvalidateRect(list, nullptr, FALSE);
    if (editing) {
        ListView_SetItemState(list, editing_row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    PositionEditor();
}

void DictManagerWindow::Impl::SyncSelectionFromList() {
    selected_ids.clear();
    const HWND list = Child(List);
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < rows.size()) {
            selected_ids.push_back(gsl::at(rows, gsl::narrow_cast<size_t>(index)));
        }
    }
    const int focused = ListView_GetNextItem(list, -1, LVNI_FOCUSED | LVNI_SELECTED);
    if (focused >= 0 && gsl::narrow_cast<size_t>(focused) < rows.size()) {
        selected_id = gsl::at(rows, gsl::narrow_cast<size_t>(focused));
    } else {
        selected_id = selected_ids.empty() ? 0 : selected_ids.back();
    }
}

void DictManagerWindow::Impl::SelectAllRows() {
    const HWND list = Child(List);
    if (rows.empty()) return;
    {
        updating_rows = true;
        const auto restore = gsl::finally([this]() noexcept { updating_rows = false; });
        ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED);
    }
    SyncSelectionFromList();
}

const DictRecord* DictManagerWindow::Impl::FindOriginal() const noexcept {
    if (!edit_original) return nullptr;
    const auto& records = model.Records();
    const auto found = std::ranges::find_if(records, [&](const DictRecord& rec) {
        return rec.source == edit_original->source && rec.code == edit_original->code && rec.word == edit_original->word;
    });
    return found == records.end() ? nullptr : &*found;
}

void DictManagerWindow::Impl::UpdateCommandState() noexcept {
    for (const auto id : {Reload, NewWord, Import, Restore, Pin, Delete, Promote, ClearFreq}) {
        ::EnableMenuItem(::GetMenu(hwnd), std::to_underlying(id), MF_BYCOMMAND | (editing ? MF_GRAYED : MF_ENABLED));
        ::SendMessageW(Child(Toolbar), TB_ENABLEBUTTON, std::to_underlying(id), MAKELONG(!editing, 0));
    }
    for (const auto id : {SaveWord, CancelEdit, AutoCode}) {
        ::EnableMenuItem(::GetMenu(hwnd), std::to_underlying(id), MF_BYCOMMAND | (editing ? MF_ENABLED : MF_GRAYED));
    }
    ::EnableWindow(Child(SearchEdit), !editing);
    ::EnableWindow(Child(SourceCombo), !editing);
}

void DictManagerWindow::Impl::PositionEditor() noexcept {
    const auto hide = [&]() noexcept {
        for (const auto id : {EditCodeInput, EditWordInput, InlineSave}) ::ShowWindow(Child(id), SW_HIDE);
    };
    if (!editing || editing_row < 0) {
        hide();
        return;
    }
    const HWND list = Child(List);
    RECT client{}, header{};
    ::GetClientRect(list, &client);
    ::GetWindowRect(ListView_GetHeader(list), &header);
    ::MapWindowPoints(nullptr, list, reinterpret_cast<POINT*>(&header), 2);
    std::array<RECT, 3> cells{};
    constexpr std::array columns{0, 1, 4};
    for (size_t i = 0; i < cells.size(); ++i) {
        auto& cell = gsl::at(cells, i);
        const int column = gsl::at(columns, i);
#pragma warning(suppress : 26477) // ListView_GetSubItemRect expands to a null-pointer check.
        if (!ListView_GetSubItemRect(list, editing_row, column, column == 0 ? LVIR_LABEL : LVIR_BOUNDS, &cell) ||
            cell.top < header.bottom || cell.bottom > client.bottom || cell.left < 0 || cell.right > client.right) {
            hide();
            return;
        }
        ::MapWindowPoints(list, hwnd, reinterpret_cast<POINT*>(&cell), 2);
    }
    constexpr std::array controls{EditCodeInput, EditWordInput};
    for (size_t i = 0; i < controls.size(); ++i) {
        const auto& cell = gsl::at(cells, i);
        ::SetWindowPos(Child(gsl::at(controls, i)), nullptr,
            cell.left + 1, cell.top + 1, std::max(1L, cell.right - cell.left - 2),
            std::max(1L, cell.bottom - cell.top - 2), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    const auto& action = cells.back();
    const int size = std::max(1, std::min({theme.ScalePx(26),
        gsl::narrow_cast<int>(action.right - action.left - 2),
        gsl::narrow_cast<int>(action.bottom - action.top - 2)}));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONSIZE, 0, MAKELPARAM(size, size));
    ::SendMessageW(Child(InlineSave), TB_SETBUTTONWIDTH, 0, MAKELPARAM(size, size));
    ::SetWindowPos(Child(InlineSave), nullptr, action.left + (action.right - action.left - size) / 2,
        action.top, size, action.bottom - action.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DictManagerWindow::Impl::BeginEdit(int row, int column) {
    if (editing) {
        ListView_EnsureVisible(Child(List), editing_row, FALSE);
        PositionEditor();
        ::SetFocus(Child(column == 0 ? EditCodeInput : EditWordInput));
        if (row != editing_row) {
            last_message = Text(L"请先保存当前编辑，或按 Esc 取消。", L"Save the current edit or press Esc to cancel first.");
            UpdateStatus();
        }
        return;
    }
    if (row < 0 || gsl::narrow_cast<size_t>(row) >= rows.size()) return;
    const auto* record = model.FindById(gsl::at(rows, gsl::narrow_cast<size_t>(row)));
    if (!record) return;
    edit_original = *record;
    editing = true;
    editing_row = row;
    code_edited = false;
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditWordInput), record->word.c_str());
    ::SetWindowTextW(Child(EditCodeInput), record->code.c_str());
    UpdateCommandState();
    ListView_EnsureVisible(Child(List), row, FALSE);
    PositionEditor();
    ::SetFocus(Child(column == 0 ? EditCodeInput : EditWordInput));
    ::SendMessageW(Child(column == 0 ? EditCodeInput : EditWordInput), EM_SETSEL, 0, -1);
    last_message = record->source == DictEntrySource::UserDict
        ? Text(L"行内编辑：Enter 保存，Esc 取消。", L"Inline edit: Enter to save, Esc to cancel.")
        : Text(L"行内编辑：另存为用户词条；Enter 保存，Esc 取消。", L"Inline edit: save as a user word; Enter to save, Esc to cancel.");
    UpdateStatus();
}

void DictManagerWindow::Impl::BeginNew() {
    if (editing) {
        BeginEdit(editing_row, 1);
        return;
    }
    editing = true;
    edit_original.reset();
    editing_row = 0;
    code_edited = false;
    updating_editor = true;
    ::SetWindowTextW(Child(EditWordInput), L"");
    ::SetWindowTextW(Child(EditCodeInput), L"");
    updating_editor = false;
    RebuildRows();
    UpdateCommandState();
    ListView_EnsureVisible(Child(List), editing_row, FALSE);
    PositionEditor();
    ::SetFocus(Child(EditWordInput));
    last_message = Text(L"新增词条：输入词条可自动取码；Enter 保存，Esc 取消。",
        L"New word: type a word for auto code; Enter to save, Esc to cancel.");
    UpdateStatus();
}

void DictManagerWindow::Impl::CancelEditing() {
    if (!editing) return;
    editing = false;
    edit_original.reset();
    editing_row = -1;
    ime_composing = false;
    RebuildRows();
    UpdateCommandState();
    ::SetFocus(Child(List));
    last_message = Text(L"已取消编辑，未保存更改。", L"Edit cancelled; no changes saved.");
    UpdateStatus();
    Tick();
}

void DictManagerWindow::Impl::Tick() {
    if (editing) return;
    if (model.PollExternalChanges()) {
        RebuildRows();
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::UpdateStatus() {
    auto status = std::format(
        L"{}: {} / {} | {}: {} | {}: {} | {}: {} | {}: {} | {}: {}",
        Text(L"当前显示", L"Visible"),
        rows.size() - (editing && std::ranges::find(rows, uint64_t{0}) != rows.end() ? 1 : 0),
        model.Records().size(),
        Text(L"用户词", L"User"),
        model.UserEntryCount(),
        Text(L"置顶", L"Pinned"),
        model.PinnedEntryCount(),
        Text(L"已屏蔽", L"Blocked"),
        model.BlockedEntryCount(),
        Text(L"动态词频", L"Phrase freq"),
        model.PhraseFreqCount(),
        Text(L"系统词库", L"System dict"),
        model.SystemEntryCount());
    if (!last_message.empty()) {
        status = last_message + L" | " + status;
    }
    ::SetWindowTextW(Child(Status), status.c_str());
}

std::wstring DictManagerWindow::Impl::Cell(const DictRecord& record, int column) const {
    switch (column) {
    case 0: return record.code;
    case 1: return record.word;
    case 2: return SourceText(record.source);
    case 3: return std::to_wstring(record.rank_in_code);
    default: return {};
    }
}

void DictManagerWindow::Impl::Notify(NMHDR& header) {
    if (header.idFrom != gsl::narrow_cast<UINT_PTR>(std::to_underlying(List))) return;
    if (header.code == LVN_GETDISPINFOW) {
        auto& info = *reinterpret_cast<NMLVDISPINFOW*>(&header);
        if ((info.item.mask & LVIF_TEXT) && info.item.iItem >= 0 &&
            gsl::narrow_cast<size_t>(info.item.iItem) < rows.size()) {
            const auto id = gsl::at(rows, gsl::narrow_cast<size_t>(info.item.iItem));
            const auto* rec = id == 0 && edit_original ? &*edit_original : model.FindById(id);
            {
                const auto text = rec ? Cell(*rec, info.item.iSubItem)
                    : (info.item.iSubItem == 2 ? Text(L"新增用户词", L"New User Word") : L"");
                if (info.item.pszText && info.item.cchTextMax > 0) {
                    wcsncpy_s(
                        info.item.pszText,
                        gsl::narrow_cast<size_t>(info.item.cchTextMax),
                        text.c_str(),
                        _TRUNCATE);
                }
            }
        }
    } else if (header.code == LVN_ITEMCHANGED && !updating_rows) {
        const auto& change = *reinterpret_cast<const NMLISTVIEW*>(&header);
        if ((change.uChanged & LVIF_STATE) && ((change.uOldState ^ change.uNewState) & LVIS_SELECTED)) {
            SyncSelectionFromList();
        }
    } else if (header.code == LVN_ODSTATECHANGED && !updating_rows) {
        SyncSelectionFromList();
    } else if (header.code == NM_DBLCLK) {
        const auto& click = *reinterpret_cast<const NMITEMACTIVATE*>(&header);
        if (click.iSubItem == 0 || click.iSubItem == 1) BeginEdit(click.iItem, click.iSubItem);
    } else if (header.code == LVN_ENDSCROLL) {
        PositionEditor();
    }
}

std::vector<DictRecord> DictManagerWindow::Impl::CollectSelectedRecords() const {
    std::vector<DictRecord> result;
    const HWND list = Child(List);
    int index = -1;
    while ((index = ListView_GetNextItem(list, index, LVNI_SELECTED)) >= 0) {
        if (gsl::narrow_cast<size_t>(index) < rows.size()) {
            if (const auto* rec = model.FindById(gsl::at(rows, gsl::narrow_cast<size_t>(index)))) {
                result.push_back(*rec);
            }
        }
    }
    if (result.empty() && selected_id != 0) {
        if (const auto* rec = model.FindById(selected_id)) {
            result.push_back(*rec);
        }
    }
    return result;
}

void DictManagerWindow::Impl::AutoFillCodeFromWord() {
    if (!editing) return;
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    const std::wstring auto_code = model.ComputeAutoCode(word);
    updating_editor = true;
    const auto restore = gsl::finally([this]() noexcept { updating_editor = false; });
    ::SetWindowTextW(Child(EditCodeInput), auto_code.c_str());
}

void DictManagerWindow::Impl::ExecuteAddOrUpdate() {
    if (!editing || ime_composing) return;
    const std::wstring word = ReadWindowTextString(Child(EditWordInput));
    std::wstring code = ReadWindowTextString(Child(EditCodeInput));
    if (code.empty() && !word.empty() && !edit_original && !code_edited) {
        code = model.ComputeAutoCode(word);
        if (!code.empty()) {
            ::SetWindowTextW(Child(EditCodeInput), code.c_str());
        }
    }
    if (model.PollExternalChanges()) RebuildRows();
    if (edit_original && !FindOriginal()) {
        last_message = Text(L"原词条已被其他进程修改或删除；草稿已保留，请取消后重新编辑。",
            L"The original entry changed or was removed externally; draft preserved. Cancel and edit again.");
        UpdateStatus();
        return;
    }
    bool ok = false;
    if (edit_original && edit_original->source == DictEntrySource::UserDict) {
        ok = model.ReplaceUserEntry(edit_original->code, edit_original->word, code, word);
    } else {
        ok = model.AddOrUpdateUserEntry(code, word);
    }
    if (!ok) {
        last_message = Text(L"保存失败，草稿已保留；请检查编码、词条及外部更改。",
            L"Save failed; draft preserved. Check code, word and external changes.");
    } else {
        editing = false;
        edit_original.reset();
        editing_row = -1;
        last_message = Text(L"已保存用户词条并同步", L"Saved user entry and synchronized");
        RebuildRows();
        UpdateCommandState();
        ::SetFocus(Child(List));
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePinSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    const bool pinned = record->source == DictEntrySource::Pinned;
    if (pinned ? model.UnpinUserEntry(record->code, record->word) : model.PinUserEntry(record->code, record->word)) {
        last_message = pinned ? Text(L"已取消固定置顶并同步", L"Unpinned entry and synchronized")
            : Text(L"已固定置顶并同步", L"Pinned entry to top and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"固定置顶失败（请检查五笔编码与词条）", L"Failed to pin entry");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteDeleteSelected() {
    const auto selected = CollectSelectedRecords();
    if (selected.empty()) return;
    size_t changed_count = 0;
    for (const auto& rec : selected) {
        switch (rec.source) {
        case DictEntrySource::UserDict:
            if (model.RemoveUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Pinned:
            if (model.UnpinUserEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::Blocked:
            if (model.UnblockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PhraseFreq:
            if (model.RemovePhraseFrequency(rec.code, rec.word)) ++changed_count;
            break;
        case DictEntrySource::PrimaryDict:
        case DictEntrySource::ExtraDict:
            if (model.BlockSystemEntry(rec.code, rec.word)) ++changed_count;
            break;
        default:
            break;
        }
    }
    if (changed_count > 0) {
        last_message = std::format(
            L"{}: {}",
            Text(L"已处理（删除/屏蔽/恢复）记录数", L"Processed (deleted/blocked/restored) records"),
            changed_count);
        RebuildRows();
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecutePromoteSelected() {
    const auto* record = model.FindById(selected_id);
    if (!record) {
        last_message = Text(L"请先选择词条。", L"Select an entry first.");
        UpdateStatus();
        return;
    }
    if (model.PromotePhraseFrequency(record->code, record->word)) {
        last_message = Text(L"已置顶动态词频并同步", L"Promoted phrase frequency and synchronized");
        RebuildRows();
    } else {
        last_message = Text(L"词频置顶仅支持双字及以上词组", L"Phrase frequency promotion requires a multi-character phrase");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExecuteClearFrequencies() {
    if (model.ClearAllPhraseFrequencies()) {
        last_message = Text(L"已清空全部动态词频记录", L"Cleared all dynamic phrase frequencies");
        RebuildRows();
    } else {
        last_message = Text(L"清空动态词频失败", L"Failed to clear phrase frequencies");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::CopySelection() {
    const auto items = CollectSelectedRecords();
    if (items.empty()) return;
    const auto text = FormatRecordsForClipboard(items);
    const ULONG error = CopyTextToClipboard(hwnd, text);
    if (error != ERROR_SUCCESS) {
        Failure(Text(L"复制到剪贴板失败", L"Copy to clipboard failed"), error);
    } else {
        last_message = std::format(L"{}: {}", Text(L"已复制词条数", L"Copied entries"), items.size());
        UpdateStatus();
    }
}

void DictManagerWindow::Impl::ShowListContextMenu(POINT screen_pt) {
    screen_pt = ResolveListViewContextMenuPoint(Child(List), screen_pt, theme);
    const ScopedMenu menu(::CreatePopupMenu());
    if (!menu) ThrowMenuError("CreatePopupMenu", ::GetLastError());
    const auto selected = CollectSelectedRecords();
    const bool has_sel = !selected.empty();
    for (const auto id : {Copy, SelectAll, Pin, Promote, Delete}) {
        std::wstring text(CommandText(id));
        if (id == Copy) text += L"\tCtrl+C";
        if (id == SelectAll) text += L"\tCtrl+A";
        if (id == Delete) text += L"\tDel";
        const bool enabled = (id == SelectAll ? !rows.empty() : has_sel) && (!editing || id == Copy || id == SelectAll);
        if (!::AppendMenuW(menu.Get(), MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED),
                std::to_underlying(id), text.c_str())) {
            ThrowMenuError("AppendMenuW", ::GetLastError());
        }
    }
    const BOOL cmd = ::TrackPopupMenu(
        menu.Get(), TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, hwnd, nullptr);
    if (cmd) Command(cmd, 0);
}

void DictManagerWindow::Impl::ImportUserDictDialog() {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"导入用户词库文件 (UTF-8)", L"Import User Dictionary File (UTF-8)");
    dialog.lpstrFilter = Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&dialog)) return;
    size_t imported = 0;
    if (model.ImportUserEntriesFromFile(path.data(), imported)) {
        last_message = std::format(L"{}: {}", Text(L"成功导入词条数", L"Imported entries"), imported);
        RebuildRows();
    } else {
        last_message = Text(L"导入失败或无有效词条", L"Import failed or no valid entries");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::ExportSnapshotDialog() {
    std::array<wchar_t, 32768> path{};
    wcscpy_s(path.data(), path.size(), L"qiqi_user_dict_export.txt");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"导出当前筛选词条 (UTF-8)", L"Export Filtered Entries (UTF-8)");
    dialog.lpstrFilter = Text(L"文本词库 (*.txt)\0*.txt\0所有文件\0*.*\0", L"Text Dictionary (*.txt)\0*.txt\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&dialog)) return;
    std::vector<DictRecord> snapshot;
    snapshot.reserve(rows.size());
    for (const uint64_t id : rows) {
        if (const auto* rec = model.FindById(id)) {
            snapshot.push_back(*rec);
        }
    }
    if (model.ExportRecordsToFile(path.data(), snapshot)) {
        last_message = std::format(L"{}: {}", Text(L"已导出词条数", L"Exported entries"), snapshot.size());
    } else {
        last_message = Text(L"导出文件失败", L"Failed to export file");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::BackupBundleDialog() {
    std::array<wchar_t, 32768> path{};
    wcscpy_s(path.data(), path.size(), L"qiqiime_backup.qbak");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"备份配置、用户词库与词频 (.qbak)", L"Backup Config, User Dict & Phrase Freq (.qbak)");
    dialog.lpstrFilter = Text(
        L"奇奇五笔备份 (*.qbak)\0*.qbak\0所有文件\0*.*\0",
        L"QiQiIME Backup (*.qbak)\0*.qbak\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.lpstrDefExt = L"qbak";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&dialog)) return;
    if (model.CreateBackupBundle(path.data())) {
        last_message = Text(L"已导出完整备份文件 (.qbak)", L"Exported full backup bundle (.qbak)");
    } else {
        last_message = Text(L"创建备份文件失败", L"Failed to create backup bundle");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::RestoreBundleDialog() {
    std::array<wchar_t, 32768> path{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrTitle = Text(L"选择要恢复的奇奇五笔备份 (.qbak)", L"Select QiQiIME Backup to Restore (.qbak)");
    dialog.lpstrFilter = Text(
        L"奇奇五笔备份 (*.qbak)\0*.qbak\0所有文件\0*.*\0",
        L"QiQiIME Backup (*.qbak)\0*.qbak\0All files\0*.*\0");
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = gsl::narrow_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&dialog)) return;

    const int choice = ::MessageBoxW(
        hwnd,
        Text(
            L"请选择恢复方式：\r\n"
            L"• 点击「是 (Y)」：合并模式（保留现有用户词库与词频，合并备份项）\r\n"
            L"• 点击「否 (N)」：覆盖模式（用备份完全替换现有用户词库与词频）\r\n"
            L"• 点击「取消」：中止恢复",
            L"Choose restore conflict mode:\r\n"
            L"• Yes: Merge (keep existing user words & frequencies, merge backup items)\r\n"
            L"• No: Replace (overwrite existing user dictionary & frequencies with backup)\r\n"
            L"• Cancel: Abort restore"),
        Text(L"恢复配置与词库备份", L"Restore Config & Dictionary Backup"),
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON1);
    if (choice == IDCANCEL) return;

    const BackupConflictMode mode = (choice == IDNO) ? BackupConflictMode::Replace : BackupConflictMode::Merge;
    BackupRestoreSummary summary{};
    if (model.RestoreBackupBundle(path.data(), mode, summary)) {
        last_message = std::format(
            L"{} ({}: {}, {}: {})",
            Text(L"已恢复备份并同步", L"Restored backup and synchronized"),
            Text(L"词库规则", L"Dict rules"),
            summary.user_entries_applied,
            Text(L"词频", L"Freq"),
            summary.phrase_freq_applied);
        RebuildRows();
    } else {
        last_message = Text(
            L"备份文件校验失败（格式错误或校验和不匹配）",
            L"Backup validation failed (invalid format or checksum mismatch)");
    }
    UpdateStatus();
}

void DictManagerWindow::Impl::Command(int id, int notification) {
    if (editing && std::ranges::contains(std::array{Reload, NewWord, Import, Restore, Pin, Delete, Promote, ClearFreq},
            gsl::narrow_cast<ControlId>(id))) {
        last_message = Text(L"请先保存当前编辑，或按 Esc 取消。", L"Save the current edit or press Esc to cancel first.");
        UpdateStatus();
        return;
    }
    switch (gsl::narrow_cast<ControlId>(id)) {
    case NewWord:
        BeginNew();
        break;
    case CancelEdit:
        CancelEditing();
        break;
    case Reload:
        model.ReloadAll();
        last_message = Text(L"已重新加载词库与词频", L"Reloaded dictionaries and phrase frequencies");
        RebuildRows();
        UpdateStatus();
        break;
    case Copy:
        CopySelection();
        break;
    case SelectAll:
        SelectAllRows();
        break;
    case Exit:
        ::SendMessageW(hwnd, WM_CLOSE, 0, 0);
        break;
    case Import:
        ImportUserDictDialog();
        break;
    case Export:
        ExportSnapshotDialog();
        break;
    case Backup:
        BackupBundleDialog();
        break;
    case Restore:
        RestoreBundleDialog();
        break;
    case ClearFreq:
        ExecuteClearFrequencies();
        break;
    case About:
        std::ignore = ShowAboutDialog(instance, hwnd, ui_language, true);
        break;
    case SourceCombo:
        if (!editing && !updating_filters && notification == CBN_SELCHANGE) {
            Refilter();
        }
        break;
    case SearchEdit:
        if (!editing && !updating_filters && notification == EN_CHANGE) {
            Refilter();
        }
        break;
    case EditWordInput:
        if (editing && !edit_original && !code_edited && !updating_editor && notification == EN_CHANGE) {
            AutoFillCodeFromWord();
        }
        break;
    case EditCodeInput:
        if (editing && !updating_editor && notification == EN_CHANGE) code_edited = true;
        break;
    case AutoCode:
        AutoFillCodeFromWord();
        break;
    case SaveWord:
        ExecuteAddOrUpdate();
        break;
    case Pin:
        ExecutePinSelected();
        break;
    case Delete:
        ExecuteDeleteSelected();
        break;
    case Promote:
        ExecutePromoteSelected();
        break;
    case LanguageAuto:
    case LanguageZh:
    case LanguageEn:
        ui_language = gsl::narrow_cast<UiLanguage>(id - std::to_underlying(LanguageAuto));
        ApplyLanguage();
        break;
    default:
        break;
    }
}

LRESULT CALLBACK DictManagerWindow::Impl::Procedure(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* self = BindOrGetWindowUserData<Impl>(window, message, lparam);
    if (!self || message == WM_NCCREATE) return ::DefWindowProcW(window, message, wparam, lparam);
    try {
        if (message == WM_MEASUREITEM && lparam) {
            auto& measure = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (measure.CtlType == ODT_MENU) {
                const ScopedWindowDC dc(window);
                const ScopedSelectObject selected(dc.Get(), self->font.Get());
                RECT bounds{};
                if (!dc || !selected || !::DrawTextW(dc.Get(), self->MenuHeading(measure.itemData), -1,
                        &bounds, DT_CALCRECT | DT_SINGLELINE)) {
                    QIQI_LOG_ERROR("DictManager menu text measurement failed");
                    return FALSE;
                }
                measure.itemWidth = gsl::narrow_cast<UINT>(bounds.right + self->theme.ScalePx(20));
                measure.itemHeight = gsl::narrow_cast<UINT>(bounds.bottom + self->theme.ScalePx(8));
                return TRUE;
            }
        }
        if (message == WM_DRAWITEM && lparam) {
            const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
            if (draw.CtlType == ODT_MENU) {
                const bool contrast = IsHighContrastEnabled();
                const auto& palette = GetCurrentThemePalette();
                const bool hot = (draw.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
                const ScopedSaveDC saved(draw.hDC);
                const ScopedSelectObject selected(draw.hDC, self->font.Get());
                ::SetDCBrushColor(draw.hDC, contrast
                    ? ::GetSysColor(hot ? COLOR_HIGHLIGHT : COLOR_MENUBAR)
                    : (hot ? palette.hover_bg : palette.window_bg));
                ::FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(::GetStockObject(DC_BRUSH)));
                ::SetTextColor(draw.hDC, contrast
                    ? ::GetSysColor(hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT) : palette.text_primary);
                ::SetBkMode(draw.hDC, TRANSPARENT);
                RECT rect = draw.rcItem;
                ::DrawTextW(draw.hDC, self->MenuHeading(draw.itemData), -1, &rect,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE | ((draw.itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
                return TRUE;
            }
        }
        if (message == WM_NOTIFY && lparam) {
            const auto& header = *reinterpret_cast<const NMHDR*>(lparam);
            if ((header.hwndFrom == self->Child(Toolbar) || header.hwndFrom == self->Child(InlineSave)) && header.code == NM_CUSTOMDRAW) {
                return self->DrawToolbar(*reinterpret_cast<NMCUSTOMDRAW*>(lparam));
            }
            if ((header.hwndFrom == self->Child(Toolbar) || header.hwndFrom == self->Child(InlineSave)) && header.code == TBN_GETINFOTIPW) {
                auto& tip = *reinterpret_cast<NMTBGETINFOTIPW*>(lparam);
                if (tip.pszText && tip.cchTextMax > 0) {
                    wcsncpy_s(tip.pszText, gsl::narrow_cast<size_t>(tip.cchTextMax),
                        self->CommandText(gsl::narrow_cast<ControlId>(tip.iItem)), _TRUNCATE);
                }
                return 0;
            }
        }
        if (const auto handled = HandleFluentToolWindowMessage(
                window, message, wparam, lparam, self->theme,
                0, 920, 560)) {
            return *handled;
        }
        switch (message) {
        case WM_SIZE:
            self->Layout();
            return 0;
        case WM_MENUCHAR:
            if (reinterpret_cast<HMENU>(lparam) == ::GetMenu(window)) {
                constexpr std::array keys{L'f', L'e', L'l', L'h'};
                for (size_t i = 0; i < keys.size(); ++i) {
                    if (LOWORD(wparam) == gsl::at(keys, i) || LOWORD(wparam) == gsl::at(keys, i) - L'a' + L'A') {
                        return MAKELRESULT(i, MNC_EXECUTE);
                    }
                }
            }
            break;
        case WM_TIMER:
            if (self->ready) self->Tick();
            return 0;
        case WM_COMMAND:
            if (self->ready) self->Command(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_NOTIFY:
            if (self->ready && lparam) self->Notify(*reinterpret_cast<NMHDR*>(lparam));
            return 0;
        case WM_CONTEXTMENU:
            if (self->ready && reinterpret_cast<HWND>(wparam) == self->Child(List)) {
                self->ShowListContextMenu(PointFromLParam(lparam));
                return 0;
            }
            break;
        case WM_DPICHANGED:
            if (lparam) self->UpdateDpi(HIWORD(wparam), *reinterpret_cast<const RECT*>(lparam));
            return 0;
        case WM_DISPLAYCHANGE:
            self->RecoverDisplay();
            return 0;
        case WM_SETTINGCHANGE:
            if (wparam == SPI_SETWORKAREA) self->RecoverDisplay();
            if (self->ready && self->ui_language == UiLanguage::Auto) self->ApplyLanguage();
            [[fallthrough]];
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
        case WM_DWMCOLORIZATIONCOLORCHANGED:
            self->UpdateTheme();
            return 0;
        case WM_CLOSE:
            if (self->editing && ::MessageBoxW(window,
                    self->Text(L"放弃尚未保存的行内编辑并关闭？", L"Discard the unsaved inline edit and close?"),
                    self->Text(L"未保存的更改", L"Unsaved changes"),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return 0;
            ::DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            ::KillTimer(window, 1);
            ::PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            if (self->owner->window_.Get() == window) std::ignore = self->owner->window_.Detach();
            self->hwnd = nullptr;
            self->ready = false;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    } catch (const std::exception&) {
        QIQI_LOG_ERROR("DictManager window operation failed with an exception");
        ::SetWindowTextW(
            self->Child(Status),
            self->Text(L"操作发生异常，请重试。", L"Operation failed with an exception."));
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

#pragma warning(suppress : 26455)
DictManagerWindow::DictManagerWindow() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
}
DictManagerWindow::~DictManagerWindow() noexcept = default;

bool DictManagerWindow::Create(
    HINSTANCE instance,
    const std::filesystem::path& user_dict_path,
    const std::filesystem::path& phrase_freq_path) {
    if (window_) return true;
    std::ignore = log::EnsureInitialized();
    impl_->instance = instance;
    const std::filesystem::path module_path = GetModuleFilePath(instance);
    impl_->model.InitializePaths(
        module_path.empty() ? std::filesystem::path{} : module_path.parent_path(),
        user_dict_path,
        phrase_freq_path);

    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES};
    if (!::InitCommonControlsEx(&controls)) {
        impl_->Failure(L"InitCommonControlsEx", ERROR_DLL_INIT_FAILED);
        return false;
    }
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = instance;
    cls.lpfnWndProc = &Impl::Procedure;
    cls.lpszClassName = kWindowClass.data();
    cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = ::LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!::RegisterClassExW(&cls)) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            impl_->Failure(L"RegisterClassEx", error);
            return false;
        }
    }
    const auto title = std::format(
        L"{} v{} ({})",
        impl_->Text(L"奇奇五笔词库与词频管理", L"QiQiIME Dictionary & Frequency Manager"),
        version::kVersionW,
        version::kArchW);
    const auto monitor = QueryActiveMonitor(nullptr);
    const auto rect = ComputeCenteredToolWindowRect(monitor, 1020, 680, monitor.dpi);
    window_.Reset(::CreateWindowExW(
        0,
        kWindowClass.data(),
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        instance,
        impl_.get()));
    if (!window_) {
        impl_->Failure(L"CreateWindowEx", ::GetLastError());
        return false;
    }
    impl_->hwnd = window_.Get();
    if (!impl_->CreateControls()) return false;
    impl_->UpdateDpi(impl_->theme.dpi, ComputeCenteredToolWindowRect(monitor, 1020, 680, impl_->theme.dpi));
    impl_->Layout();
    return true;
}

int RunDictManager(HINSTANCE instance, int show) {
    DictManagerWindow window;
    if (!window.Create(instance)) return 1;
    return RunToolWindowMessageLoop(window.Handle(), show);
}

} // namespace qiqi::dict_manager
