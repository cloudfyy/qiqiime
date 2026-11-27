// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/internal.h"
#include "common/dlgutil.h"
#include "common/i18n.h"
#include "common/uiutil.h"
#include "common/window.h"
#include <cwchar>
#include <format>
#include <memory>
#include <utility>

namespace qiqi {
namespace {

using namespace settings_detail;

struct SkinPickerContext {
    ImeSkinCatalog catalog;
    SkinSelectionState* state = nullptr;
    UiLanguage language = UiLanguage::Auto;
    DisplayPreferences display;
    CandidateFontPreferences candidate_font;
    FluentD2DThemeContext theme;
    ScopedGdiHandle<HFONT> font;
    ScopedGdiHandle<HFONT> candidate_preview_font;
    ScopedImageList rows;
    std::array<ImeSkinAppearance, 22> appearances{};
    int title_height = 0;
    bool window_owned = false;
};

ImeSkinId PickerSkinId(const SkinPickerContext& context, size_t index) noexcept {
    return index == 0 ? ImeSkinId::Default : gsl::at(context.catalog.skins, index - 1).id;
}

constexpr int kPickerFontSize = 15;

bool RefreshPickerAppearance(HWND hwnd, SkinPickerContext& context, UINT dpi) noexcept {
    if (!UpdateToolWindowDisplay(hwnd, context.theme, context.font, context.display, dpi,
            context.language, {}, kPickerFontSize)) {
        QIQI_LOG_ERROR("Skin picker font creation failed");
        return false;
    }
    auto candidate_font = CreateCandidateFont(
        context.candidate_font, kCandidateFontDefaultHeightPx,
        context.theme.dpi, context.display.text_scale);
    if (!candidate_font) {
        QIQI_LOG_ERROR("Skin picker candidate preview font creation failed");
        return false;
    }
    context.candidate_preview_font = std::move(candidate_font);
    for (size_t index = 0; index < context.appearances.size(); ++index) {
        std::optional<ImeSkinSnapshot> snapshot;
        if (index > 0) {
            const auto& skin = gsl::at(context.catalog.skins, index - 1);
            snapshot = ImeSkinSnapshot{skin.id, skin.palettes};
        }
        gsl::at(context.appearances, index) = ApplyImeSkinAppearance(context.theme.appearance, snapshot);
    }
    const HWND list = ::GetDlgItem(hwnd, kIdSkinPickerList);
    const std::array controls{list};
    std::ignore = ApplyToolWindowListAndControlsTheme(hwnd, list, context.theme, controls);
    ApplyDisplayThemeToWindow(hwnd, context.theme.appearance);
    return true;
}

void LayoutPicker(HWND hwnd, SkinPickerContext& context) noexcept {
    RECT client{};
    if (!::GetClientRect(hwnd, &client)) return;
    const int inset = context.theme.ScaleDipPx(12);
    const int gap = context.theme.ScaleDipPx(8);
    const int button_height = FluentControlHeight(context.theme, 32);
    const int button_width = std::min(std::max(1, (gsl::narrow_cast<int>(client.right) - 2 * inset - gap) / 2),
        context.theme.ScalePx(160));
    const int top = std::max(inset, gsl::narrow_cast<int>(client.bottom) - inset - button_height);
    const HWND list = ::GetDlgItem(hwnd, kIdSkinPickerList);
    if (!list) return;
    ::SetWindowPos(list, nullptr, inset, inset, std::max(1, gsl::narrow_cast<int>(client.right) - 2 * inset),
        std::max(1, top - inset - gap), SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(::GetDlgItem(hwnd, IDCANCEL), nullptr, client.right - inset - button_width, top,
        button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(::GetDlgItem(hwnd, IDOK), nullptr, client.right - inset - 2 * button_width - gap, top,
        button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT area{};
    ::GetClientRect(list, &area);
    const int width = std::max(1, gsl::narrow_cast<int>(area.right) - ::GetSystemMetrics(SM_CXVSCROLL));
    ListView_SetColumnWidth(list, 0, width);
    const ScopedWindowDC dc(list);
    const ScopedSelectObject selected_font(dc.Get(), context.font.Get());
    TEXTMETRICW metrics{};
    if (!dc || !::GetTextMetricsW(dc.Get(), &metrics)) {
        QIQI_LOG_WARN("Skin picker row measurement failed (error={})", ::GetLastError());
        return;
    }
    context.title_height = metrics.tmHeight + 2 * gap;
    int sample_height = 0;
    {
        const ScopedSelectObject selected_candidate_font(dc.Get(), context.candidate_preview_font.Get());
        sample_height = MeasureCandidateSample(dc.Get(), context.candidate_preview_font.Get(),
            std::max(1, width - 2 * gap), context.theme.dpi, context.language, context.display);
    }
    const int height = context.title_height + sample_height + 2 * gap;
    std::ignore = SetFluentListRowHeight(list, context.theme, context.rows, height);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void PaintPickerItem(const NMLVCUSTOMDRAW& draw, SkinPickerContext& context) noexcept {
    const size_t index = draw.nmcd.dwItemSpec;
    if (index >= context.appearances.size()) return;
    const HWND list = draw.nmcd.hdr.hwndFrom;
    RECT bounds{};
    if (!GetListViewItemBounds(list, gsl::narrow_cast<int>(index), bounds)) return;
    const ScopedSaveDC saved(draw.nmcd.hdc);
    const auto& theme = context.theme.appearance.palette;
    const bool selected =
        (ListView_GetItemState(list, gsl::narrow_cast<int>(index), LVIS_SELECTED) & LVIS_SELECTED) != 0;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(
        selected ? theme.selection_bg : theme.input_bg));
    const ScopedGdiHandle<HBRUSH> frame(::CreateSolidBrush(
        selected || (draw.nmcd.uItemState & CDIS_FOCUS) ? theme.focus_color : theme.border_color));
    if (!saved || !background || !frame) {
        QIQI_LOG_ERROR("Skin picker drawing resources failed (error={})", ::GetLastError());
        return;
    }
    ::FillRect(draw.nmcd.hdc, &bounds, background.Get());
    ::FrameRect(draw.nmcd.hdc, &bounds, frame.Get());
    const int gap = context.theme.ScaleDipPx(8);
    RECT title{bounds.left + gap, bounds.top + gap, bounds.right - gap, bounds.top + context.title_height};
    const ScopedSelectObject font(draw.nmcd.hdc, context.font.Get());
    ::SetBkMode(draw.nmcd.hdc, TRANSPARENT);
    ::SetTextColor(draw.nmcd.hdc, selected ? theme.selection_text : theme.text_primary);
    if (selected) {
        const wchar_t* marker = LocalizedUiText(context.language, L"已选中", L"Selected");
        const int length = gsl::narrow_cast<int>(std::wcslen(marker));
        SIZE size{};
        if (!::GetTextExtentPoint32W(draw.nmcd.hdc, marker, length, &size)) {
            QIQI_LOG_WARN("Skin picker selection label measurement failed (error={})", ::GetLastError());
            return;
        }
        RECT marker_bounds{std::max(title.left, title.right - size.cx),
            title.top, title.right, title.bottom};
        if (!::DrawTextW(draw.nmcd.hdc, marker, length, &marker_bounds,
                DT_SINGLELINE | DT_NOPREFIX | DT_RIGHT)) {
            QIQI_LOG_WARN("Skin picker selection label drawing failed");
        }
        title.right = std::max(title.left, marker_bounds.left - gap);
    }
    const auto name = ImeSkinDisplayName(context.catalog, PickerSkinId(context, index), context.language);
    ::DrawTextW(draw.nmcd.hdc, name.data(), gsl::narrow_cast<int>(name.size()), &title,
        DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    const RECT preview{bounds.left + gap, bounds.top + context.title_height,
        bounds.right - gap, bounds.bottom - gap};
    std::ignore = PaintCandidateSample(draw.nmcd.hdc, preview,
        context.candidate_preview_font.Get(), context.theme.dpi,
        context.language, context.display, gsl::at(context.appearances, index).colors);
}

LRESULT CALLBACK SkinPickerWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto* context = BindOrGetWindowUserData<SkinPickerContext>(hwnd, message, lparam);
    if (!context || message == WM_NCCREATE) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    switch (message) {
        case WM_SIZE:
            LayoutPicker(hwnd, *context);
            return 0;
        case WM_GETMINMAXINFO:
            if (lparam) {
                auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
                info->ptMinTrackSize = {context->theme.ScaleDipPx(320), context->theme.ScaleDipPx(280)};
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            std::ignore = RefreshPickerAppearance(hwnd, *context, context->theme.dpi);
            LayoutPicker(hwnd, *context);
            return 0;
        case WM_DPICHANGED:
            std::ignore = RefreshPickerAppearance(hwnd, *context, HIWORD(wparam));
            if (lparam) {
                const auto& bounds = *reinterpret_cast<const RECT*>(lparam);
                ::SetWindowPos(hwnd, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                    bounds.bottom - bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            LayoutPicker(hwnd, *context);
            return 0;
        case WM_NOTIFY:
            if (lparam) {
                const auto* header = reinterpret_cast<const NMHDR*>(lparam);
                if (header->idFrom == kIdSkinPickerList && header->code == NM_CUSTOMDRAW) {
                    const auto& draw = *reinterpret_cast<const NMLVCUSTOMDRAW*>(lparam);
                    if (draw.nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                    if (draw.nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                        PaintPickerItem(draw, *context);
                        return CDRF_SKIPDEFAULT;
                    }
                }
            }
            break;
        case WM_KEYDOWN:
            if (wparam == VK_TAB) {
                ::SetFocus(::GetNextDlgTabItem(hwnd, ::GetFocus(), (::GetKeyState(VK_SHIFT) & 0x8000) != 0));
                return 0;
            }
            if (wparam == VK_RETURN || wparam == VK_ESCAPE) {
                ::SendMessageW(hwnd, WM_COMMAND,
                    wparam == VK_ESCAPE || ::GetDlgCtrlID(::GetFocus()) == IDCANCEL ? IDCANCEL : IDOK, 0);
                return 0;
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wparam) == IDOK) {
                const int selected = ListView_GetNextItem(::GetDlgItem(hwnd, kIdSkinPickerList), -1, LVNI_SELECTED);
                if (selected < 0 || std::cmp_greater_equal(selected, context->appearances.size())) {
                    QIQI_LOG_WARN("Skin picker confirmation rejected without a valid selection");
                    return 0;
                }
                context->state->selected = PickerSkinId(*context, gsl::narrow_cast<size_t>(selected));
                context->state->accepted = true;
                context->state->done = true;
                ::PostMessageW(hwnd, WM_NULL, 0, 0);
                return 0;
            }
            if (LOWORD(wparam) == IDCANCEL) {
                context->state->done = true;
                ::PostMessageW(hwnd, WM_NULL, 0, 0);
                return 0;
            }
            break;
        case WM_CLOSE:
            context->state->done = true;
            ::PostMessageW(hwnd, WM_NULL, 0, 0);
            return 0;
        case WM_DRAWITEM:
            if (lparam) {
                const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
                DrawFluentPushButton(draw, context->theme, draw.CtlID == IDOK,
                    ColorRefToD2DColor(context->theme.appearance.palette.window_bg));
                return TRUE;
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(ApplyFluentControlColors(
                reinterpret_cast<HDC>(wparam), context->theme, message == WM_CTLCOLORLISTBOX));
        case WM_PAINT:
            return PaintWindowClient(hwnd, [context](HDC dc, const RECT& bounds) noexcept {
                const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(context->theme.appearance.palette.window_bg));
                if (background) ::FillRect(dc, &bounds, background.Get());
                else QIQI_LOG_WARN("Skin picker background brush creation failed (error={})", ::GetLastError());
            });
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            if (context->window_owned) delete context;
            return ::DefWindowProcW(hwnd, message, wparam, lparam);
        default: break;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
}

} // namespace

ScopedWindow CreateSkinSelectionWindow(HINSTANCE instance, HWND parent, const ImeSkinCatalog& catalog,
    DisplayPreferences display, UiLanguage language, SkinSelectionState* state, HRESULT* result,
    const CandidateFontPreferences& candidate_font) noexcept {
    const auto fail = [result](HRESULT error, std::string_view operation) noexcept -> ScopedWindow {
        if (result) *result = error;
        QIQI_LOG_ERROR("{} failed (hr=0x{:08X})", operation, static_cast<unsigned long>(error));
        return {};
    };
    const auto last_error = []() noexcept {
        const DWORD error = ::GetLastError();
        return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
    };
    const HRESULT valid = state ? ValidateImeSkinCatalog(catalog) : E_POINTER;
    if (FAILED(valid)) return fail(valid, "Skin picker validation");
    if (!dialog_detail::RegisterDialogWindowClass(instance, kSkinPickerWndClass, SkinPickerWndProc))
        return fail(last_error(), "Skin picker registration");
    try {
        auto context = std::make_unique<SkinPickerContext>();
        context->catalog = catalog;
        context->state = state;
        context->display = NormalizeDisplayPreferences(display);
        context->language = language;
        context->candidate_font = candidate_font;
        const auto monitor = QueryActiveMonitor(parent);
        if (!UpdateToolWindowDisplay(nullptr, context->theme, context->font, context->display, monitor.dpi,
                language, {}, kPickerFontSize))
            return fail(E_FAIL, "Skin picker font initialization");
        constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN;
        const RECT bounds = ComputeCenteredWindowBounds(context->theme.ScalePx(720), context->theme.ScalePx(540),
            style, 0, monitor.work_area);
        ScopedWindow window(::CreateWindowExW(0, kSkinPickerWndClass,
            LocalizedUiText(language, L"选择皮肤", L"Choose a skin"), style,
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            parent, nullptr, instance, context.get()));
        if (!window) {
            return fail(last_error(), "Skin picker window creation");
        }
        auto& picker = *context;
        picker.window_owned = true;
        std::ignore = context.release();
        state->accepted = false;
        state->done = false;
        const auto create = [&](const wchar_t* cls, const wchar_t* text, DWORD child_style, int id) noexcept {
            return CreateFluentChildControl(window.Get(), instance, picker.font.Get(), &picker.theme,
                cls, text, child_style, 0, 0, 1, 1, id);
        };
        const HWND list = create(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_NOCOLUMNHEADER |
            LVS_SHOWSELALWAYS | WS_TABSTOP, kIdSkinPickerList);
        if (!list || !create(L"BUTTON", LocalizedUiText(language, L"使用此皮肤", L"Use this skin"),
                BS_OWNERDRAW | WS_TABSTOP, IDOK) ||
            !create(L"BUTTON", GetUiStrings(language).dlg_btn_cancel, BS_OWNERDRAW | WS_TABSTOP, IDCANCEL))
            return fail(last_error(), "Skin picker control creation");
        ListView_SetExtendedListViewStyle(list, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
        LVCOLUMNW column{};
        column.mask = LVCF_WIDTH;
        column.cx = 1;
        if (ListView_InsertColumn(list, 0, &column) < 0) {
            return fail(E_FAIL, "Skin picker column creation");
        }
        if (!RefreshPickerAppearance(window.Get(), picker, monitor.dpi))
            return fail(E_FAIL, "Skin picker appearance initialization");
        LayoutPicker(window.Get(), picker);
        int selected = 0;
        for (size_t index = 0; index < picker.appearances.size(); ++index) {
            const auto id = PickerSkinId(picker, index);
            std::wstring name(ImeSkinDisplayName(picker.catalog, id, language));
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = gsl::narrow_cast<int>(index);
            item.pszText = name.data();
            if (ListView_InsertItem(list, &item) < 0) {
                return fail(E_FAIL, "Skin picker item creation");
            }
            if (id == state->selected) selected = item.iItem;
        }
        ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selected, FALSE);
        ::SetFocus(list);
        if (result) *result = S_OK;
        return window;
    } catch (const std::bad_alloc&) {
        return fail(E_OUTOFMEMORY, "Skin picker allocation");
    }
}

HRESULT ShowSkinSelectionDialog(HINSTANCE instance, HWND parent, const ImeSkinCatalog& catalog,
    DisplayPreferences display, UiLanguage language, ImeSkinId& selection,
    const CandidateFontPreferences& candidate_font) noexcept {
    SkinSelectionState state{selection};
    HRESULT result = S_OK;
    auto window = CreateSkinSelectionWindow(
        instance, parent, catalog, display, language, &state, &result, candidate_font);
    if (!window) return result;
    if (!dialog_detail::RunModalDialogLoop(window.Get(), state.done, true)) return E_ABORT;
    if (!state.accepted) return S_FALSE;
    selection = state.selected;
    return S_OK;
}

namespace settings_detail {

void ReportSkinConfigurationFailure(HWND hwnd, UiLanguage language, HRESULT result) noexcept {
    try {
        const auto message = std::format(L"{}\r\nHRESULT: 0x{:08X}",
            LocalizedUiText(language, L"皮肤配置不可用；未应用此更改。",
                L"Skin configuration is unavailable; this change was not applied."),
            static_cast<unsigned long>(result));
        if (!::MessageBoxW(hwnd, message.c_str(), GetUiStrings(language).dlg_title, MB_OK | MB_ICONERROR))
            QIQI_LOG_ERROR("Skin error dialog failed (error={})", ::GetLastError());
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("Skin error message allocation failed (hr=0x{:08X})", static_cast<unsigned long>(result));
    }
}

void PopulateSettingsSkinCombo(HWND hwnd, DialogContext& context) noexcept {
    const auto language = GetSelectedUiLanguageFromCombo(hwnd, context);
    ::SetDlgItemTextW(hwnd, kIdLblImeSkin, FAILED(context.skin_catalog_result) ?
        LocalizedUiText(language, L"皮肤（配置不可用）", L"Skin (unavailable)") :
        LocalizedUiText(language, L"皮肤", L"Skin"));
    context.quick_skin_count = 0;
    const auto add = [&](ImeSkinId id) noexcept {
        gsl::at(context.quick_skin_choices, context.quick_skin_count++) = id;
    };
    add(ImeSkinId::Default);
    if (!context.skin_catalog.skins.empty()) for (const auto id : kCommonImeSkins) add(id);
    const auto choices = std::span(context.quick_skin_choices).first(context.quick_skin_count);
    if (!std::ranges::contains(choices, context.preview_skin_id)) add(context.preview_skin_id);
    std::array<const wchar_t*, 8> names{};
    for (size_t i = 0; i < context.quick_skin_count; ++i)
        gsl::at(names, i) = ImeSkinDisplayName(context.skin_catalog,
            gsl::at(context.quick_skin_choices, i), language).data();
    gsl::at(names, context.quick_skin_count) = LocalizedUiText(language, L"更多…", L"More...");
    const HWND combo = ::GetDlgItem(hwnd, kIdComboImeSkin);
    std::ignore = log::CheckHResult(ReplaceComboBoxItems(combo,
        std::span(names).first(context.quick_skin_count + 1)), "Settings::SkinCombo");
    for (size_t i = 0; i < context.quick_skin_count; ++i) {
        if (gsl::at(context.quick_skin_choices, i) == context.preview_skin_id)
            ::SendMessageW(combo, CB_SETCURSEL, i, 0);
    }
}

void UpdateSettingsSkinPreview(HWND hwnd, DialogContext& context) noexcept {
    if (context.preview_skin_id == ImeSkinId::Default) context.preview_skin_snapshot.reset();
    else if (const auto* skin = FindImeSkin(context.skin_catalog, context.preview_skin_id))
        context.preview_skin_snapshot = ImeSkinSnapshot{skin->id, skin->palettes};
    else QIQI_LOG_WARN("Skin preview retained its last valid colors (hr=0x{:08X})",
        static_cast<unsigned long>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA)));
    const auto appearance = ApplyImeSkinAppearance(context.d2d.appearance, context.preview_skin_snapshot);
    context.candidate_preview_palette = appearance.display.palette;
    context.candidate_skin_colors = appearance.colors;
    ::InvalidateRect(::GetDlgItem(hwnd, kIdCandidatePreview), nullptr, FALSE);
}

void HandleSettingsSkinSelection(HWND hwnd, DialogContext& context) noexcept {
    const LRESULT index = ::SendDlgItemMessageW(hwnd, kIdComboImeSkin, CB_GETCURSEL, 0, 0);
    if (index < 0 || std::cmp_greater(index, context.quick_skin_count)) {
        QIQI_LOG_WARN("Settings rejected an invalid skin combo selection");
        return;
    }
    const auto choice = gsl::narrow_cast<size_t>(index);
    if (choice < context.quick_skin_count) {
        context.preview_skin_id = gsl::at(context.quick_skin_choices, choice);
        UpdateSettingsSkinPreview(hwnd, context);
        return;
    }
    PopulateSettingsSkinCombo(hwnd, context);
    if (FAILED(context.skin_catalog_result)) {
        ReportSkinConfigurationFailure(hwnd, GetSelectedUiLanguageFromCombo(hwnd, context), context.skin_catalog_result);
        return;
    }
    QiQiConfig draft;
    ReadSettingsFromWindowControls(hwnd, draft);
    const auto original = &context;
    context.transfer_in_progress = true;
    const auto restore = gsl::finally([hwnd, original]() noexcept {
        if (::IsWindow(hwnd) && reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)) == original) {
            original->transfer_in_progress = false;
            ::PostMessageW(hwnd, kSettingsExternalConfigChangedMessage, 0, 0);
        }
    });
    ImeSkinId selected = context.preview_skin_id;
    const HRESULT result = ShowSkinSelectionDialog(context.instance, hwnd, context.skin_catalog,
        GetDisplayPreferences(draft), draft.ui_language, selected,
        context.preview_candidate_font);
    if (!::IsWindow(hwnd) ||
        reinterpret_cast<DialogContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA)) != original) return;
    if (result == S_OK) {
        context.preview_skin_id = selected;
        PopulateSettingsSkinCombo(hwnd, context);
        UpdateSettingsSkinPreview(hwnd, context);
    } else if (FAILED(result)) ReportSkinConfigurationFailure(hwnd, draft.ui_language, result);
}

} // namespace settings_detail
} // namespace qiqi
