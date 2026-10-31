#include "ui/d2d_controls.h"

#include "common/logger.h"
#include "common/theme.h"
#include "common/win32_ui_util.h"

#include <commctrl.h>
#include <gsl/gsl>
#include <cwchar>
#include <tuple>

namespace qiqi {

int FluentD2DThemeContext::ScalePx(int px) const noexcept {
    return ScalePxByDpi(px, dpi);
}

float FluentD2DThemeContext::ScaleF(int px) const noexcept {
    return static_cast<float>(ScalePx(px));
}

void FluentD2DThemeContext::Init(UINT monitor_dpi) noexcept {
    dpi = monitor_dpi > 0 ? monitor_dpi : 96u;
    if (!CreateD2DDcTargetAndDWrite(d2d_factory, dwrite_factory, d2d_target)) {
        QIQI_LOG_ERROR("Fluent Direct2D/DirectWrite context creation failed");
    }
    if (dwrite_factory) {
        std::ignore = dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(14), L"zh-CN", fmt_body.Put());
        std::ignore = dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(12), L"zh-CN", fmt_caption.Put());
        std::ignore = dwrite_factory->CreateTextFormat(
            L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, ScaleF(14), L"zh-CN", fmt_btn.Put());
        if (!fmt_body || !fmt_caption || !fmt_btn) {
            QIQI_LOG_ERROR("Fluent DirectWrite text format creation failed");
        }
        if (fmt_body) {
            fmt_body->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fmt_body->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (fmt_btn) {
            fmt_btn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt_btn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fmt_btn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }
    RefreshThemeBrushes();
}

void FluentD2DThemeContext::RefreshThemeBrushes() noexcept {
    const bool is_dark = IsSystemDarkTheme();
    surface_brush.Reset(::CreateSolidBrush(is_dark ? RGB(43, 46, 52) : RGB(255, 255, 255)));
    input_brush.Reset(::CreateSolidBrush(is_dark ? RGB(54, 58, 68) : RGB(255, 255, 255)));
    if (!surface_brush || !input_brush) {
        QIQI_LOG_ERROR("Fluent theme brush creation failed (error={})", ::GetLastError());
    }
}

void DrawFluentCardGroup(
    const FluentD2DThemeContext& d2d,
    std::span<const FluentCardSpec> cards,
    bool is_en,
    bool is_dark) noexcept {
    if (!d2d.d2d_target) {
        return;
    }
    const auto card_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(64, 68, 78) : MakeD2DColor(222, 227, 236));
    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(76, 160, 255) : MakeD2DColor(0, 103, 192));
    const float radius = d2d.ScaleF(8);

    for (const auto& card : cards) {
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(card.rect, radius, radius);
        if (card_brush) {
            d2d.d2d_target->FillRoundedRectangle(rr, card_brush.Get());
        }
        if (border_brush) {
            d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
        }
        if (accent_brush) {
            const D2D1_ROUNDED_RECT pill = D2D1::RoundedRect(
                D2D1::RectF(card.rect.left + d2d.ScaleF(12), card.rect.top + d2d.ScaleF(10),
                            card.rect.left + d2d.ScaleF(16), card.rect.top + d2d.ScaleF(26)),
                d2d.ScaleF(2), d2d.ScaleF(2));
            d2d.d2d_target->FillRoundedRectangle(pill, accent_brush.Get());
        }
        if (d2d.fmt_caption && accent_brush) {
            const wchar_t* hdr = is_en ? card.title_en : card.title_zh;
            if (hdr) {
                const D2D1_RECT_F hdr_rc = D2D1::RectF(
                    card.rect.left + d2d.ScaleF(24), card.rect.top + d2d.ScaleF(9),
                    card.rect.right - d2d.ScaleF(12), card.rect.top + d2d.ScaleF(29));
                d2d.d2d_target->DrawTextW(
                    hdr, gsl::narrow_cast<UINT32>(wcslen(hdr)), d2d.fmt_caption.Get(), hdr_rc, accent_brush.Get());
            }
        }
    }
}

void DrawFluentPushButton(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_primary,
    D2D1_COLOR_F outer_bg) noexcept {
    if (!d2d.d2d_target || !dis.hDC) {
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_pressed = (dis.itemState & ODS_SELECTED) != 0;

    D2D1_COLOR_F fill_col{};
    D2D1_COLOR_F border_col{};
    D2D1_COLOR_F text_col{};
    if (is_primary) {
        fill_col = is_pressed ? MakeD2DColor(0, 84, 166) : MakeD2DColor(0, 103, 192);
        border_col = MakeD2DColor(40, 130, 220);
        text_col = MakeD2DColor(255, 255, 255);
    } else if (is_dark) {
        fill_col = is_pressed ? MakeD2DColor(48, 52, 60) : MakeD2DColor(56, 60, 70);
        border_col = MakeD2DColor(82, 88, 100);
        text_col = MakeD2DColor(238, 240, 245);
    } else {
        fill_col = is_pressed ? MakeD2DColor(232, 236, 242) : MakeD2DColor(255, 255, 255);
        border_col = MakeD2DColor(204, 210, 220);
        text_col = MakeD2DColor(28, 32, 40);
    }

    const D2D1_RECT_F rc = D2D1::RectF(
        1.0f, 1.0f,
        static_cast<float>(dis.rcItem.right - dis.rcItem.left) - 1.0f,
        static_cast<float>(dis.rcItem.bottom - dis.rcItem.top) - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(rc, d2d.ScaleF(6), d2d.ScaleF(6));

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(outer_bg);

    const auto fill_brush = CreateSolidBrush(d2d.d2d_target.Get(), fill_col);
    const auto border_brush = CreateSolidBrush(d2d.d2d_target.Get(), border_col);
    const auto txt_brush = CreateSolidBrush(d2d.d2d_target.Get(), text_col);
    if (fill_brush) {
        d2d.d2d_target->FillRoundedRectangle(rr, fill_brush.Get());
    }
    if (border_brush) {
        d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
    }

    wchar_t btn_text[64]{};
    ::GetWindowTextW(dis.hwndItem, btn_text, 64);
    if (d2d.fmt_btn && txt_brush && btn_text[0] != L'\0') {
        d2d.d2d_target->DrawTextW(
            btn_text,
            gsl::narrow_cast<UINT32>(wcslen(btn_text)),
            d2d.fmt_btn.Get(),
            rc,
            txt_brush.Get());
    }
}

void DrawFluentToggleOrRadioRow(
    const DRAWITEMSTRUCT& dis,
    const FluentD2DThemeContext& d2d,
    bool is_radio) noexcept {
    if (!d2d.d2d_target || !dis.hDC) {
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_checked = (::SendMessageW(dis.hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED);
    const bool is_enabled = (::IsWindowEnabled(dis.hwndItem) != FALSE);

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));

    const float h = static_cast<float>(dis.rcItem.bottom - dis.rcItem.top);
    const float w = static_cast<float>(dis.rcItem.right - dis.rcItem.left);
    const float cy = h * 0.5f;

    const auto accent_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(76, 160, 255) : MakeD2DColor(0, 103, 192));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(130, 138, 152) : MakeD2DColor(138, 146, 158));
    const auto knob_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        is_checked ? MakeD2DColor(255, 255, 255)
                   : (is_dark ? MakeD2DColor(186, 194, 206) : MakeD2DColor(96, 104, 118)));
    const auto text_brush = CreateSolidBrush(
        d2d.d2d_target.Get(),
        !is_enabled ? (is_dark ? MakeD2DColor(110, 116, 128) : MakeD2DColor(150, 156, 166))
                    : (is_dark ? MakeD2DColor(238, 240, 245) : MakeD2DColor(28, 32, 40)));

    float text_left = d2d.ScaleF(4);
    float text_right = w - d2d.ScaleF(4);

    if (is_radio) {
        const float r_outer = d2d.ScaleF(8);
        const D2D1_ELLIPSE outer = D2D1::Ellipse(D2D1::Point2F(d2d.ScaleF(10), cy), r_outer, r_outer);
        if (is_checked && accent_brush) {
            d2d.d2d_target->FillEllipse(outer, accent_brush.Get());
            if (knob_brush) {
                d2d.d2d_target->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(d2d.ScaleF(10), cy), d2d.ScaleF(4), d2d.ScaleF(4)), knob_brush.Get());
            }
        } else if (border_brush) {
            d2d.d2d_target->DrawEllipse(outer, border_brush.Get(), 1.5f);
        }
        text_left = d2d.ScaleF(26);
    } else {
        const float sw_right = w - d2d.ScaleF(6);
        const float sw_left = sw_right - d2d.ScaleF(36);
        const D2D1_ROUNDED_RECT track = D2D1::RoundedRect(
            D2D1::RectF(sw_left, cy - d2d.ScaleF(9), sw_right, cy + d2d.ScaleF(9)),
            d2d.ScaleF(9), d2d.ScaleF(9));
        if (is_checked && accent_brush) {
            d2d.d2d_target->FillRoundedRectangle(track, accent_brush.Get());
        } else if (border_brush) {
            d2d.d2d_target->DrawRoundedRectangle(track, border_brush.Get(), 1.4f);
        }
        const float knob_x = is_checked ? (sw_right - d2d.ScaleF(9)) : (sw_left + d2d.ScaleF(9));
        if (knob_brush) {
            d2d.d2d_target->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(knob_x, cy), d2d.ScaleF(6), d2d.ScaleF(6)), knob_brush.Get());
        }
        text_right = sw_left - d2d.ScaleF(8);
    }

    wchar_t label_text[128]{};
    ::GetWindowTextW(dis.hwndItem, label_text, 128);
    if (d2d.fmt_body && text_brush && label_text[0] != L'\0') {
        const D2D1_RECT_F txt_rc = D2D1::RectF(text_left, 0.0f, text_right, h);
        d2d.d2d_target->DrawTextW(
            label_text,
            gsl::narrow_cast<UINT32>(wcslen(label_text)),
            d2d.fmt_body.Get(),
            txt_rc,
            text_brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }
}

void PaintFluentComboBoxFrame(HWND hwnd, HDC hdc, const FluentD2DThemeContext& d2d) noexcept {
    if (!d2d.d2d_target || !hdc) {
        return;
    }
    RECT rc{};
    ::GetClientRect(hwnd, &rc);
    const bool is_dark = IsSystemDarkTheme();

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), hdc, rc);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(is_dark ? MakeD2DColor(43, 46, 52) : MakeD2DColor(255, 255, 255));

    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    const D2D1_RECT_F box_rc = D2D1::RectF(1.0f, 1.0f, w - 1.0f, h - 1.0f);
    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(box_rc, d2d.ScaleF(5), d2d.ScaleF(5));

    const auto fill_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(56, 60, 70) : MakeD2DColor(248, 250, 253));
    const auto border_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(86, 92, 106) : MakeD2DColor(204, 210, 222));
    const auto txt_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(238, 240, 245) : MakeD2DColor(28, 32, 40));
    const auto chev_brush = CreateSolidBrush(
        d2d.d2d_target.Get(), is_dark ? MakeD2DColor(186, 194, 208) : MakeD2DColor(96, 104, 118));

    if (fill_brush) {
        d2d.d2d_target->FillRoundedRectangle(rr, fill_brush.Get());
    }
    if (border_brush) {
        d2d.d2d_target->DrawRoundedRectangle(rr, border_brush.Get(), 1.0f);
    }

    const LRESULT cur_sel = ::SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
    if (cur_sel >= 0 && d2d.fmt_body && txt_brush) {
        wchar_t item_text[128]{};
        const LRESULT len = ::SendMessageW(hwnd, CB_GETLBTEXTLEN, static_cast<WPARAM>(cur_sel), 0);
        if (len > 0 && len < 127) {
            ::SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(cur_sel), reinterpret_cast<LPARAM>(item_text));
            const D2D1_RECT_F txt_rc = D2D1::RectF(d2d.ScaleF(10), 0.0f, w - d2d.ScaleF(26), h);
            d2d.d2d_target->DrawTextW(
                item_text,
                gsl::narrow_cast<UINT32>(wcslen(item_text)),
                d2d.fmt_body.Get(),
                txt_rc,
                txt_brush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
        }
    }

    if (chev_brush) {
        const float cx = w - d2d.ScaleF(14);
        const float cy = h * 0.5f + d2d.ScaleF(1);
        const float arm = d2d.ScaleF(4);
        d2d.d2d_target->DrawLine(
            D2D1::Point2F(cx - arm, cy - arm * 0.6f),
            D2D1::Point2F(cx, cy + arm * 0.5f),
            chev_brush.Get(),
            1.6f);
        d2d.d2d_target->DrawLine(
            D2D1::Point2F(cx, cy + arm * 0.5f),
            D2D1::Point2F(cx + arm, cy - arm * 0.6f),
            chev_brush.Get(),
            1.6f);
    }
}

void DrawFluentComboBoxItem(const DRAWITEMSTRUCT& dis, const FluentD2DThemeContext& d2d) noexcept {
    if (!d2d.d2d_target || !dis.hDC || dis.itemID == static_cast<UINT>(-1)) {
        return;
    }
    if ((dis.itemState & ODS_COMBOBOXEDIT) != 0) {
        const ScopedWindowDC full_dc(dis.hwndItem);
        PaintFluentComboBoxFrame(dis.hwndItem, full_dc.Get(), d2d);
        return;
    }
    const bool is_dark = IsSystemDarkTheme();
    const bool is_selected = (dis.itemState & ODS_SELECTED) != 0;

    const D2D1_COLOR_F bg_col = is_selected
        ? (is_dark ? MakeD2DColor(0, 96, 180) : MakeD2DColor(220, 234, 252))
        : (is_dark ? MakeD2DColor(54, 58, 68) : MakeD2DColor(255, 255, 255));
    const D2D1_COLOR_F txt_col = is_dark ? MakeD2DColor(238, 240, 245) : MakeD2DColor(28, 32, 40);

    const ScopedD2DDcDraw draw_guard(d2d.d2d_target.Get(), dis.hDC, dis.rcItem);
    if (!draw_guard) {
        return;
    }
    d2d.d2d_target->Clear(bg_col);

    wchar_t item_text[128]{};
    const LRESULT len = ::SendMessageW(dis.hwndItem, CB_GETLBTEXTLEN, dis.itemID, 0);
    if (len > 0 && len < 127) {
        ::SendMessageW(dis.hwndItem, CB_GETLBTEXT, dis.itemID, reinterpret_cast<LPARAM>(item_text));
    }
    const auto txt_brush = CreateSolidBrush(d2d.d2d_target.Get(), txt_col);
    if (d2d.fmt_body && txt_brush && item_text[0] != L'\0') {
        const D2D1_RECT_F rc = D2D1::RectF(
            6.0f, 0.0f,
            static_cast<float>(dis.rcItem.right - dis.rcItem.left) - 4.0f,
            static_cast<float>(dis.rcItem.bottom - dis.rcItem.top));
        d2d.d2d_target->DrawTextW(
            item_text,
            gsl::narrow_cast<UINT32>(wcslen(item_text)),
            d2d.fmt_body.Get(),
            rc,
            txt_brush.Get());
    }
}

namespace {

LRESULT CALLBACK FluentToggleSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const int radio_range_pack = gsl::narrow_cast<int>(dwRefData);
    const bool is_radio = (radio_range_pack != 0);
    switch (msg) {
        case BM_GETCHECK:
            return ::GetPropW(hwnd, L"QiQiChecked") ? BST_CHECKED : BST_UNCHECKED;
        case BM_SETCHECK: {
            if (wparam == BST_CHECKED) {
                if (!::SetPropW(hwnd, L"QiQiChecked", reinterpret_cast<HANDLE>(1))) {
                    QIQI_LOG_ERROR("Fluent toggle SetPropW failed (error={})", ::GetLastError());
                }
            } else {
                ::RemovePropW(hwnd, L"QiQiChecked");
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_KEYUP: {
            if (msg == WM_KEYUP && wparam != VK_SPACE) {
                break;
            }
            if ((::SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) == 0) {
                break;
            }
            if (msg == WM_LBUTTONUP) {
                RECT rect{};
                ::GetClientRect(hwnd, &rect);
                if (!::PtInRect(&rect, PointFromLParam(lparam))) {
                    break;
                }
            }
            if (::IsWindowEnabled(hwnd)) {
                const HWND parent = ::GetParent(hwnd);
                const int id = ::GetDlgCtrlID(hwnd);
                if (is_radio) {
                    const int first_id = LOWORD(radio_range_pack);
                    const int last_id = HIWORD(radio_range_pack);
                    for (int rid = first_id; rid <= last_id; ++rid) {
                        if (const HWND rctrl = ::GetDlgItem(parent, rid)) {
                            ::SendMessageW(rctrl, BM_SETCHECK, (rid == id) ? BST_CHECKED : BST_UNCHECKED, 0);
                        }
                    }
                } else {
                    const bool cur = (::SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    ::SendMessageW(hwnd, BM_SETCHECK, cur ? BST_UNCHECKED : BST_CHECKED, 0);
                }
            }
            break;
        }
        case WM_NCDESTROY:
            ::RemovePropW(hwnd, L"QiQiChecked");
            ::RemoveWindowSubclass(hwnd, &FluentToggleSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK FluentComboBoxSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) noexcept {
    const auto* d2d = reinterpret_cast<const FluentD2DThemeContext*>(dwRefData);
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            if (!d2d || !d2d->d2d_target) {
                break;
            }
            const ScopedPaintDC paint_dc(hwnd);
            PaintFluentComboBoxFrame(hwnd, paint_dc.Get(), *d2d);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case CB_SETCURSEL: {
            const LRESULT res = ::DefSubclassProc(hwnd, msg, wparam, lparam);
            DWORD_PTR current_ref = 0;
            // A parent notification may destroy the child and its borrowed context.
            if (::GetWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass, &current_ref) &&
                current_ref == dwRefData && d2d && d2d->d2d_target) {
                const ScopedWindowDC dc(hwnd);
                PaintFluentComboBoxFrame(hwnd, dc.Get(), *d2d);
            }
            return res;
        }
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &FluentComboBoxSubclassProc, uIdSubclass);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wparam, lparam);
}

} // namespace

HWND CreateFluentChildControl(
    HWND parent,
    HINSTANCE inst,
    HFONT font,
    const FluentD2DThemeContext* d2d_ctx,
    const wchar_t* cls,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    int id,
    DWORD_PTR radio_group_range) noexcept {
    const bool is_button = (_wcsicmp(cls, L"BUTTON") == 0);
    const DWORD button_type = style & BS_TYPEMASK;
    const bool is_radio = is_button && button_type == BS_AUTORADIOBUTTON;
    const bool is_check = is_button && button_type == BS_AUTOCHECKBOX;
    const bool is_combo = (_wcsicmp(cls, L"COMBOBOX") == 0);
    if (is_radio && (radio_group_range == 0 || id < LOWORD(radio_group_range) || id > HIWORD(radio_group_range))) {
        QIQI_LOG_ERROR("Fluent radio control {} has an invalid group range", id);
        return nullptr;
    }
    DWORD actual_style = style;
    if (is_radio || is_check) {
        actual_style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW | WS_TABSTOP;
    }
    ScopedWindow ctrl(::CreateWindowExW(
        0,
        cls,
        text,
        WS_CHILD | WS_VISIBLE | actual_style,
        x,
        y,
        w,
        h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        inst,
        nullptr));
    if (!ctrl) {
        QIQI_LOG_ERROR("CreateFluentChildControl {} failed (error={})", id, ::GetLastError());
        return nullptr;
    }
    if (font) {
        ::SendMessageW(ctrl.Get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (is_radio || is_check) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentToggleSubclassProc, 1, is_radio ? radio_group_range : 0)) {
            QIQI_LOG_ERROR("Fluent toggle {} SetWindowSubclass failed (error={})", id, ::GetLastError());
            return nullptr;
        }
    } else if (is_combo && d2d_ctx) {
        if (!::SetWindowSubclass(ctrl.Get(), &FluentComboBoxSubclassProc, 2, reinterpret_cast<DWORD_PTR>(d2d_ctx))) {
            QIQI_LOG_ERROR("Fluent combo {} SetWindowSubclass failed (error={})", id, ::GetLastError());
            return nullptr;
        }
    }
    return ctrl.Detach();
}

} // namespace qiqi
