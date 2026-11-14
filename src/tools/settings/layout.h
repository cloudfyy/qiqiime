// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tools/settings/internal.h"

#include <commctrl.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <gsl/gsl>

namespace qiqi::settings_detail {

void LayoutSettingsControls(HWND hwnd, DialogContext& ctx) noexcept {
    if (!ctx.controls_initialized || ctx.laying_out) return;
    ctx.laying_out = true;
    const auto finish = gsl::finally([&ctx]() noexcept { ctx.laying_out = false; });
    RECT client{};
    if (!::GetClientRect(hwnd, &client)) {
        QIQI_LOG_WARN("Settings client bounds query failed (error={})", ::GetLastError());
        return;
    }
    const auto S = [&ctx](int value) noexcept { return ctx.d2d.ScaleDipPx(value); };
    const int gap = S(10);
    const int inset = S(16);
    const int width = std::max(1, gsl::narrow_cast<int>(client.right) - 2 * inset);
    LOGFONTW font{};
    if (::GetObjectW(ctx.font.Get(), sizeof(font), &font) != sizeof(font)) {
        QIQI_LOG_WARN("Settings font metrics query failed");
        return;
    }
    const int font_height = std::max(S(15), std::abs(gsl::narrow_cast<int>(font.lfHeight)));
    const int target = std::max(S(ctx.preview_easy_reading ? 44 : 30), font_height + S(10));
    const ScopedWindowDC dc(hwnd);
    if (!dc) {
        QIQI_LOG_WARN("Settings layout DC acquisition failed");
        return;
    }
    const ScopedSelectObject selected_font(dc.Get(), ctx.font.Get());
    const auto text_height = [&](int id, int available) noexcept {
        std::array<wchar_t, 1024> text{};
        const int length = ::GetDlgItemTextW(hwnd, id, text.data(), gsl::narrow_cast<int>(text.size()));
        RECT bounds{0, 0, std::max(1, available), 0};
        if (length > 0 && !::DrawTextW(dc.Get(), text.data(), length, &bounds,
                                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX)) {
            QIQI_LOG_WARN("Settings control {} text measurement failed", id);
        }
        const bool unavailable = ctx.d2d.appearance.high_contrast &&
            !::IsWindowEnabled(::GetDlgItem(hwnd, id));
        return gsl::narrow_cast<int>(bounds.bottom) + (unavailable ? font_height + S(4) : 0);
    };
    const auto move = [](HWND child, int x, int y, int w, int h) noexcept {
        if (!::SetWindowPos(child, nullptr, x, y, w, h,
                            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW)) {
            QIQI_LOG_WARN("Settings control placement failed (error={})", ::GetLastError());
        }
    };

    const auto preferred_action_width = [&](int id) noexcept {
        int dips = 0;
        switch (id) {
            case kIdBtnHelp: dips = 128; break;
            case kIdBtnAbout:
            case IDOK:
            case IDCANCEL: dips = 104; break;
            case kIdBtnRestoreDefaults: dips = 156; break;
            case kIdBtnBrowsePrimaryDict: dips = 104; break;
            case kIdBtnDictManager: dips = 180; break;
            default: return 0;
        }
        return ctx.d2d.ScalePx(dips);
    };
    std::array<int, kSettingsFooterControls.size()> footer_widths{};
    constexpr size_t utility_count = kSettingsFooterControls.size() - 2;
    int total_width = gap * gsl::narrow_cast<int>(kSettingsFooterControls.size() - 1);
    for (size_t index = 0; index < footer_widths.size(); ++index) {
        const int button = preferred_action_width(gsl::at(kSettingsFooterControls, index));
        gsl::at(footer_widths, index) = button;
        total_width += button;
    }
    const bool footer_wraps = total_width > width;
    int utility_height = target;
    int accept_height = target;
    for (size_t index = 0; index < footer_widths.size(); ++index) {
        int& button = gsl::at(footer_widths, index);
        if (footer_wraps) {
            const int columns = index < utility_count ? gsl::narrow_cast<int>(utility_count) : 2;
            button = std::max(1, std::min(button, (width - (columns - 1) * gap) / columns));
        }
        const int height = std::max(target,
            text_height(gsl::at(kSettingsFooterControls, index), std::max(1, button - S(20))) + S(12));
        if (index < utility_count) utility_height = std::max(utility_height, height);
        else accept_height = std::max(accept_height, height);
    }
    const int footer_height = footer_wraps ? utility_height + gap + accept_height :
        std::max(utility_height, accept_height);
    const int footer_top = std::max(inset, gsl::narrow_cast<int>(client.bottom) - footer_height - inset);
    int footer_x = inset;
    for (size_t index = 0; index < footer_widths.size(); ++index) {
        const int id = gsl::at(kSettingsFooterControls, index);
        const int button = gsl::at(footer_widths, index);
        if (index == utility_count) {
            footer_x = client.right - inset - gsl::at(footer_widths, utility_count) - gap -
                gsl::at(footer_widths, utility_count + 1);
        }
        const int top = footer_wraps && index >= utility_count ? footer_top + utility_height + gap : footer_top;
        const int height = footer_wraps ? (index < utility_count ? utility_height : accept_height) : footer_height;
        const HWND child = ::GetDlgItem(hwnd, id);
        move(child, footer_x, top, button, height);
        ::SetWindowRgn(child, nullptr, FALSE);
        ::ShowWindow(child, SW_SHOWNA);
        footer_x += button + gap;
    }

    const HWND tabs = ::GetDlgItem(hwnd, kIdSettingsTabs);
    int tab_width = S(80);
    const bool english = ResolveUiLanguage(GetSelectedUiLanguageFromCombo(hwnd, ctx)) == UiLanguage::EnUs;
    for (const auto& label : kSettingsPageLabels) {
        const wchar_t* text = english ? label.en : label.zh;
        SIZE size{};
        if (!::GetTextExtentPoint32W(dc.Get(), text, gsl::narrow_cast<int>(std::wcslen(text)), &size)) {
            QIQI_LOG_WARN("Settings tab text measurement failed (error={})", ::GetLastError());
        }
        tab_width = std::max(tab_width, gsl::narrow_cast<int>(size.cx) + S(24));
    }
    tab_width = std::min(tab_width, width);
    const int tab_height = target + S(6);
    move(tabs, inset, gap, width, tab_height);
    ::SendMessageW(tabs, TCM_SETITEMSIZE, 0, MAKELPARAM(tab_width, target));
    ::EnumChildWindows(tabs, [](HWND child, LPARAM data) noexcept -> BOOL {
        std::array<wchar_t, 32> name{};
        ::GetClassNameW(child, name.data(), gsl::narrow_cast<int>(name.size()));
        if (std::wcscmp(name.data(), UPDOWN_CLASSW) == 0 &&
            !::SetWindowSubclass(child, SettingsTabSubclass, 4, static_cast<DWORD_PTR>(data))) {
            QIQI_LOG_WARN("Settings tab scroll subclass installation failed (error={})", ::GetLastError());
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    const int scroll_width = ::GetSystemMetricsForDpi(SM_CXVSCROLL, ctx.d2d.dpi);
    ctx.page_bounds = RECT{inset, gap + tab_height + gap, client.right - inset - scroll_width - gap,
                          std::max(gap + tab_height + gap + 1, footer_top - gap)};
    ctx.viewport_height = ctx.page_bounds.bottom - ctx.page_bounds.top;
    const int content_width = std::max(1,
        gsl::narrow_cast<int>(ctx.page_bounds.right - ctx.page_bounds.left) - 2 * inset);
    const int left = ctx.page_bounds.left + inset;
    const int right = left + content_width;
    const int field_width = std::min(content_width, ctx.d2d.ScalePx(settings_layout::kComboWidth));
    const int label_width = std::max(1, content_width - field_width - gap);
    const bool stacked = ctx.preview_easy_reading || label_width < std::max(S(140), font_height * 10);
    int y = gap;

    for (int page = 0; page < gsl::narrow_cast<int>(SettingsPage::Count); ++page) {
        const bool active = page == static_cast<int>(ctx.active_page);
        for (const int id : SettingsPageControls(static_cast<SettingsPage>(page))) {
            const bool explanation = id == kIdDisplayExplanation && !ctx.d2d.appearance.system_high_contrast;
            ::ShowWindow(::GetDlgItem(hwnd, id), active && !explanation ? SW_SHOWNA : SW_HIDE);
        }
    }

    const auto place = [&](int id, int x, int row, int w, int height, bool combo) noexcept {
        const HWND child = ::GetDlgItem(hwnd, id);
        const int top = ctx.page_bounds.top + row - ctx.scroll_offset;
        if (combo) {
            ::SendMessageW(child, CB_SETITEMHEIGHT, std::numeric_limits<WPARAM>::max(),
                           target);
            ::SendMessageW(child, CB_SETITEMHEIGHT, 0, target);
        }
        move(child, x, top, w, combo ? height + target * 5 : height);
        // A drop-down's closed height includes its native frame, not just its item height.
        RECT actual{};
        if (!::GetWindowRect(child, &actual)) {
            QIQI_LOG_WARN("Settings control {} bounds query failed (error={})", id, ::GetLastError());
            return height;
        }
        const int actual_height = actual.bottom - actual.top;
        const int clip_top = std::clamp(gsl::narrow_cast<int>(ctx.page_bounds.top) - top, 0, actual_height);
        const int clip_bottom = std::clamp(gsl::narrow_cast<int>(ctx.page_bounds.bottom) - top, clip_top, actual_height);
        ScopedGdiHandle<HRGN> region(::CreateRectRgn(0, clip_top, w, clip_bottom));
        if (!region || !::SetWindowRgn(child, region.Get(), FALSE)) {
            QIQI_LOG_WARN("Settings control {} clipping failed (error={})", id, ::GetLastError());
        } else {
            std::ignore = region.Detach();
        }
        return actual_height;
    };
    const auto action_width = [&](int id) noexcept {
        return std::min(content_width, preferred_action_width(id));
    };
    const auto controls = SettingsPageControls(ctx.active_page);
    for (size_t index = 0; index < controls.size(); ++index) {
        const int id = gsl::at(controls, index);
        if (id == kIdDisplayExplanation && !ctx.d2d.appearance.system_high_contrast) continue;
        if (id == kIdRadioCustom && index + 2 < controls.size() &&
            gsl::at(controls, index + 1) == kIdBtnBrowsePrimaryDict &&
            gsl::at(controls, index + 2) == kIdBtnDictManager) {
            const int browse_width = std::min(action_width(kIdBtnBrowsePrimaryDict), (content_width - gap) / 2);
            const int manager_width = std::min(action_width(kIdBtnDictManager), content_width - gap - browse_width);
            const int radio_width = content_width - browse_width - manager_width - 2 * gap;
            const bool same_row = radio_width >= ctx.d2d.ScalePx(180);
            const int label_height = std::max(target,
                text_height(id, std::max(1, (same_row ? radio_width : content_width) - ctx.d2d.ScalePx(40))) + S(12));
            const int buttons_height = std::max({target,
                text_height(kIdBtnBrowsePrimaryDict, std::max(1, browse_width - S(20))) + S(12),
                text_height(kIdBtnDictManager, std::max(1, manager_width - S(20))) + S(12)});
            const int row_height = same_row ? std::max(label_height, buttons_height) : label_height;
            place(id, left, y, same_row ? radio_width : content_width, row_height, false);
            const int buttons_y = same_row ? y : y + row_height + gap;
            place(kIdBtnBrowsePrimaryDict, right - manager_width - gap - browse_width,
                  buttons_y, browse_width, same_row ? row_height : buttons_height, false);
            place(kIdBtnDictManager, right - manager_width, buttons_y, manager_width,
                  same_row ? row_height : buttons_height, false);
            y = buttons_y + (same_row ? row_height : buttons_height) + gap;
            index += 2;
            continue;
        }
        if (action_width(id) != 0) {
            size_t end = index;
            int row_width = 0;
            int row_height = target;
            while (end < controls.size()) {
                const int next = gsl::at(controls, end);
                const int button = action_width(next);
                if (button == 0 || (end > index && row_width + gap + button > content_width)) break;
                row_width += (end > index ? gap : 0) + button;
                row_height = std::max(row_height, text_height(next, std::max(1, button - S(20))) + S(12));
                ++end;
            }
            int x = right - row_width;
            for (size_t button_index = index; button_index < end; ++button_index) {
                const int button_id = gsl::at(controls, button_index);
                const int button = action_width(button_id);
                place(button_id, x, y, button, row_height, false);
                x += button + gap;
            }
            index = end - 1;
            y += row_height + gap;
            continue;
        }
        const bool label = id == kIdLblUiTheme || id == kIdLblUiTextScale || id == kIdLblUiLang ||
            id == kIdLblPageSize || id == kIdLblUnderlineStyle || id == kIdLblEnglishSwitch;
        if (label && index + 1 < controls.size()) {
            const int combo = gsl::at(controls, ++index);
            const int label_height = std::max(font_height + S(6), text_height(id, stacked ? content_width : label_width));
            const int combo_y = stacked ? y + label_height + S(4) : y;
            place(id, left, y, stacked ? content_width : label_width, label_height, false);
            int closed_height = 0;
            if (id == kIdLblPageSize && index + 1 < controls.size() &&
                gsl::at(controls, index + 1) == kIdComboCandidateLayout) {
                const int pair_width = std::min(content_width,
                    std::max(field_width, 2 * ctx.d2d.ScalePx(44) + gap));
                const int count_width = std::min(ctx.d2d.ScalePx(80), (pair_width - gap) / 2);
                const int layout_width = pair_width - gap - count_width;
                closed_height = place(combo, right - pair_width, combo_y, count_width, target, true);
                closed_height = std::max(closed_height,
                    place(kIdComboCandidateLayout, right - layout_width, combo_y, layout_width, target, true));
                ++index;
            } else {
                closed_height = place(combo, right - field_width, combo_y, field_width, target, true);
            }
            y = std::max(y + label_height, combo_y + closed_height) + gap;
            continue;
        }
        const bool preview = id == kIdCandidatePreview;
        const bool static_text = id == kIdLblCustomPrimaryPath || id == kIdDisplayExplanation;
        const int indent = id == kIdChkExtraDict ? std::min(S(24), content_width - 1) : 0;
        const int control_width = content_width - indent;
        const int height = preview ? MeasureSettingsCandidatePreviewHeight(::GetDlgItem(hwnd, id), control_width, ctx) :
            std::max(static_text ? font_height + S(4) : target,
                     text_height(id, control_width - S(static_text ? 0 : 60)) + S(12));
        const int actual_height = place(id, left + indent, y, control_width, height, false);
        y += actual_height + gap;
    }
    ctx.content_height = y;
    const int clamped = std::clamp(ctx.scroll_offset, 0, std::max(0, y - ctx.viewport_height));
    gsl::at(ctx.page_scroll_offsets, static_cast<size_t>(ctx.active_page)) = clamped;
    const HWND scrollbar = ::GetDlgItem(hwnd, kIdPageScroll);
    move(scrollbar, client.right - inset - scroll_width, ctx.page_bounds.top, scroll_width, ctx.viewport_height);
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMax = std::max(0, y - 1);
    info.nPage = gsl::narrow_cast<UINT>(ctx.viewport_height);
    info.nPos = clamped;
    ::SetScrollInfo(scrollbar, SB_CTL, &info, TRUE);
    ::ShowWindow(scrollbar, y > ctx.viewport_height ? SW_SHOWNA : SW_HIDE);
    if (clamped != ctx.scroll_offset) {
        ctx.scroll_offset = clamped;
        ctx.laying_out = false;
        LayoutSettingsControls(hwnd, ctx);
        return;
    }
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SelectSettingsPage(HWND hwnd, DialogContext& ctx, SettingsPage page) noexcept {
    if (page < SettingsPage::Input || page >= SettingsPage::Count) return;
    gsl::at(ctx.page_scroll_offsets, static_cast<size_t>(ctx.active_page)) = ctx.scroll_offset;
    ctx.active_page = page;
    ctx.scroll_offset = gsl::at(ctx.page_scroll_offsets, static_cast<size_t>(page));
    ::SendDlgItemMessageW(hwnd, kIdSettingsTabs, TCM_SETCURSEL, static_cast<WPARAM>(page), 0);
    LayoutSettingsControls(hwnd, ctx);
    ::SetFocus(::GetDlgItem(hwnd, kIdSettingsTabs));
}

void EnsureSettingsControlVisible(HWND hwnd, HWND child, DialogContext& ctx) noexcept {
    if (!child || ::GetParent(child) != hwnd || ctx.laying_out) return;
    const int id = ::GetDlgCtrlID(child);
    if (std::ranges::find(SettingsPageControls(ctx.active_page), id) == SettingsPageControls(ctx.active_page).end()) return;
    RECT bounds{};
    if (!::GetWindowRect(child, &bounds)) {
        QIQI_LOG_WARN("Settings focus bounds query failed (error={})", ::GetLastError());
        return;
    }
    ::MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&bounds), 2);
    const int margin = ctx.d2d.ScaleDipPx(4);
    const int old_offset = ctx.scroll_offset;
    if (bounds.top < ctx.page_bounds.top + margin) ctx.scroll_offset += bounds.top - ctx.page_bounds.top - margin;
    else if (bounds.bottom > ctx.page_bounds.bottom - margin)
        ctx.scroll_offset += bounds.bottom - ctx.page_bounds.bottom + margin;
    ctx.scroll_offset = std::clamp(ctx.scroll_offset, 0, std::max(0, ctx.content_height - ctx.viewport_height));
    if (old_offset != ctx.scroll_offset) LayoutSettingsControls(hwnd, ctx);
}

} // namespace qiqi::settings_detail
