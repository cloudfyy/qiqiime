// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/ui/candobj.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/theme.h"
#include "ime/fsm/fsm.h"
#include <gsl/gsl>
#include <oleauto.h>
#include <dwmapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>

namespace qiqi {
namespace {
constexpr float kTextMeasurementWidth = 4096.0f;
constexpr int kGdiFontHeight = 18;
constexpr int kFallbackCharacterWidth = 12;
constexpr int kHorizontalMargin = 10;
constexpr int kCandidateGap = 16;
constexpr int kHeaderTop = 6;
constexpr int kCandidateRowTop = 32;
constexpr int kBottomPadding = 4;
constexpr int kPageButtonWidth = 20;
constexpr int kPageButtonGap = 4;
constexpr int kCaretGapPx = 4;

const char* ExceptionMessageNoThrow(const std::exception& error) noexcept {
    try {
        return error.what();
    } catch (...) {
        return "exception message unavailable";
    }
}

HRESULT MakeBstrVariant(std::wstring_view text, VARIANT* value) noexcept {
    if (!value) return E_POINTER;
    ::VariantInit(value);
    value->vt = VT_BSTR;
    value->bstrVal = ::SysAllocStringLen(text.data(), gsl::narrow_cast<UINT>(text.size()));
    return value->bstrVal ? S_OK : E_OUTOFMEMORY;
}

HRESULT MakeRuntimeId(HWND hwnd, long child_id, SAFEARRAY** out) noexcept {
    if (!out) return E_POINTER;
    *out = nullptr;
    const bool has_child = child_id >= 0;
    SAFEARRAYBOUND bound{has_child ? 3ul : 2ul, 0};
    *out = ::SafeArrayCreate(VT_I4, 1, &bound);
    if (!*out) return E_OUTOFMEMORY;
    const std::array<long, 3> indices{UiaAppendRuntimeId, gsl::narrow_cast<long>(
        reinterpret_cast<uintptr_t>(hwnd) & 0x7fffffff), child_id};
    for (LONG i = 0; i < gsl::narrow_cast<LONG>(bound.cElements); ++i) {
        LONG index = i;
        long value = gsl::at(indices, gsl::narrow_cast<size_t>(i));
        if (FAILED(::SafeArrayPutElement(*out, &index, &value))) {
            ::SafeArrayDestroy(*out);
            *out = nullptr;
            return E_FAIL;
        }
    }
    return S_OK;
}

std::wstring_view StripCodeAnnotation(std::wstring_view text) noexcept {
    if (text.size() < 3 || text.back() != L')') return text;
    const size_t open = text.rfind(L'(');
    if (open == std::wstring_view::npos || open == 0) return text;
    std::wstring_view code = text.substr(open + 1, text.size() - open - 2);
    if (code.starts_with(L'~')) code.remove_prefix(1);
    if (code.empty() || code.size() > 4) return text;
    if (!std::ranges::all_of(code, [](wchar_t ch) noexcept {
            return ch >= L'a' && ch <= L'y';
        })) {
        return text;
    }
    return text.substr(0, open);
}
}

class CandidateAutomationItem final : public ComObjectBase<
    CandidateAutomationItem,
    IRawElementProviderSimple,
    IRawElementProviderFragment,
    ISelectionItemProvider> {
public:
    CandidateAutomationItem(ComPtr<CandidateUI> owner, size_t page_index) noexcept
        : owner_(std::move(owner)), page_index_(page_index) {}

    STDMETHODIMP get_ProviderOptions(ProviderOptions* options) noexcept override {
        if (!options) return E_INVALIDARG;
        *options = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    STDMETHODIMP GetPatternProvider(PATTERNID pattern_id, IUnknown** provider) noexcept override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        if (pattern_id == UIA_SelectionItemPatternId) {
            return QueryInterface(__uuidof(ISelectionItemProvider),
                                  reinterpret_cast<void**>(provider));
        }
        return S_OK;
    }
    STDMETHODIMP GetPropertyValue(PROPERTYID property_id, VARIANT* value) noexcept override {
        if (!value) return E_INVALIDARG;
        ::VariantInit(value);
        switch (property_id) {
            case UIA_ControlTypePropertyId:
                value->vt = VT_I4;
                value->lVal = UIA_ListItemControlTypeId;
                return S_OK;
            case UIA_NamePropertyId:
                return MakeBstrVariant(owner_->AccessibleCandidateName(page_index_), value);
            case UIA_HelpTextPropertyId:
                try {
                    return MakeBstrVariant(
                        owner_->AccessibleCandidateHelp(page_index_), value);
                } catch (const std::bad_alloc&) {
                    return E_OUTOFMEMORY;
                } catch (...) {
                    return E_FAIL;
                }
            case UIA_IsEnabledPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = VARIANT_TRUE;
                return S_OK;
            case UIA_IsOffscreenPropertyId: {
                value->vt = VT_BOOL;
                const RECT candidate_rect = owner_->CandidateRect(page_index_);
                value->boolVal =
                    !owner_->is_shown_ || !owner_->WindowHandle() ||
                    !::IsWindowVisible(owner_->WindowHandle()) ||
                    candidate_rect.bottom <= 0 ||
                    candidate_rect.top >= owner_->CurrentHeight()
                    ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            case UIA_SelectionItemIsSelectedPropertyId:
                value->vt = VT_BOOL;
                value->boolVal = owner_->SelectedPageIndex() == page_index_
                    ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            default:
                return S_OK;
        }
    }
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** provider) noexcept override {
        if (!provider) return E_INVALIDARG;
        *provider = nullptr;
        return S_OK;
    }
    STDMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** fragment) noexcept override {
        if (!fragment) return E_INVALIDARG;
        *fragment = nullptr;
        if (!owner_) return S_OK;
        switch (direction) {
            case NavigateDirection_Parent:
                return owner_->QueryInterface(__uuidof(IRawElementProviderFragment),
                                              reinterpret_cast<void**>(fragment));
            case NavigateDirection_PreviousSibling:
                return page_index_ > 0
                    ? owner_->CreateAutomationItem(page_index_ - 1, fragment)
                    : S_OK;
            case NavigateDirection_NextSibling:
                return page_index_ + 1 < owner_->AccessibleCandidateCount()
                    ? owner_->CreateAutomationItem(page_index_ + 1, fragment)
                    : S_OK;
            default:
                return S_OK;
        }
    }
    STDMETHODIMP GetRuntimeId(SAFEARRAY** runtime_id) noexcept override {
        if (page_index_ > gsl::narrow_cast<size_t>(std::numeric_limits<long>::max())) {
            return E_BOUNDS;
        }
        return MakeRuntimeId(
            owner_->WindowHandle(), gsl::narrow_cast<long>(page_index_), runtime_id);
    }
    STDMETHODIMP get_BoundingRectangle(UiaRect* bounds) noexcept override {
        if (!bounds) return E_INVALIDARG;
        *bounds = {};
        if (page_index_ >= owner_->AccessibleCandidateCount()) return S_OK;
        const RECT rect = owner_->CandidateRect(page_index_);
        POINT top_left{rect.left, rect.top};
        POINT bottom_right{rect.right, rect.bottom};
        if (owner_->WindowHandle() &&
            ::ClientToScreen(owner_->WindowHandle(), &top_left) &&
            ::ClientToScreen(owner_->WindowHandle(), &bottom_right)) {
            bounds->left = static_cast<double>(top_left.x);
            bounds->top = static_cast<double>(top_left.y);
            bounds->width = static_cast<double>(bottom_right.x) -
                static_cast<double>(top_left.x);
            bounds->height = static_cast<double>(bottom_right.y) -
                static_cast<double>(top_left.y);
        }
        return S_OK;
    }
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override {
        if (!roots) return E_INVALIDARG;
        *roots = nullptr;
        return S_OK;
    }
    STDMETHODIMP SetFocus() noexcept override {
        return Select();
    }
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override {
        if (!root) return E_INVALIDARG;
        *root = nullptr;
        return owner_->QueryInterface(
            __uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void**>(root));
    }
    STDMETHODIMP Select() noexcept override {
        if (!owner_ || page_index_ >= owner_->AccessibleCandidateCount()) return E_INVALIDARG;
        try {
            if (const auto& callback = owner_->select_callback_; callback) {
                callback(page_index_);
            }
            return S_OK;
        } catch (const std::bad_alloc&) {
            QIQI_LOG_ERROR(
                "CandidateAutomationItem::Select allocation failed (hr=0x{:08x})",
                gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
            return E_OUTOFMEMORY;
        } catch (const std::exception& exception) {
            QIQI_LOG_ERROR("CandidateAutomationItem::Select failed (error={})",
                           ExceptionMessageNoThrow(exception));
            return E_FAIL;
        } catch (...) {
            QIQI_LOG_ERROR("CandidateAutomationItem::Select failed with unknown exception");
            return E_FAIL;
        }
    }
    STDMETHODIMP AddToSelection() noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RemoveFromSelection() noexcept override { return E_NOTIMPL; }
    STDMETHODIMP get_IsSelected(BOOL* selected) noexcept override {
        if (!selected) return E_INVALIDARG;
        *selected = owner_->SelectedPageIndex() == page_index_;
        return S_OK;
    }
    STDMETHODIMP get_SelectionContainer(
        IRawElementProviderSimple** container) noexcept override {
        if (!container) return E_INVALIDARG;
        *container = nullptr;
        return owner_->QueryInterface(
            __uuidof(IRawElementProviderSimple), reinterpret_cast<void**>(container));
    }

private:
    ComPtr<CandidateUI> owner_;
    size_t page_index_;
};

CandidateUI::CandidateUI(HINSTANCE instance) noexcept : instance_(instance) {
    appearance_ = ResolveDisplayAppearance(preferences_, SystemThemeSource::App);
    InitDirect2DAndDirectWrite();
    UpdateDpiAndFont(96);
}

CandidateUI::~CandidateUI() noexcept {
    destroying_object_ = true;
    std::ignore = DestroyPopupWindow();
}

ComPtr<CandidateUI> CandidateUI::RetainForWindowOperation() noexcept {
    if (destroying_object_) return {};
    return ComPtr<CandidateUI>(this);
}

void CandidateUI::InitDirect2DAndDirectWrite() noexcept {
    if (!CreateD2DDcTargetAndDWrite(d2d_factory_, dwrite_factory_, d2d_dc_target_)) {
        QIQI_LOG_WARN("CandidateUI::InitDirect2DAndDirectWrite incomplete (d2d={}, dwrite={}, target={}); GDI fallback active",
                      d2d_factory_.IsValid(), dwrite_factory_.IsValid(), d2d_dc_target_.IsValid());
    }
}

void CandidateUI::UpdateDpiAndFont(UINT new_dpi) noexcept {
    const UINT effective_dpi = new_dpi > 0 ? new_dpi : 96u;
    const int text_scale_percent = TextScalePercent(preferences_.text_scale);
    if (font_.IsValid() && dpi_ == effective_dpi && font_scale_percent_ == text_scale_percent) {
        return;
    }
    dpi_ = effective_dpi;
    font_scale_percent_ = text_scale_percent;
    popup_height_ = ScaleByDpi(kPopupHeight);

    font_ = CreateUiFont(ScaleText(kGdiFontHeight), FW_NORMAL);
    const ScopedWindowDC dc(hwnd_.Get());
    const ScopedSelectObject select_font(dc.Get(), font_.Get());
    TEXTMETRICW metrics{};
    line_height_ = dc && ::GetTextMetricsW(dc.Get(), &metrics)
        ? std::max(ScaleText(24), gsl::narrow_cast<int>(metrics.tmHeight + metrics.tmExternalLeading))
        : ScaleText(24);

    if (dwrite_factory_) {
        LOGFONTW font_details{};
        std::ignore = ::GetObjectW(font_.Get(), sizeof(font_details), &font_details);
        if (log::CheckHResult(dwrite_factory_->CreateTextFormat(
                gsl::at(font_details.lfFaceName, 0) ? std::data(font_details.lfFaceName) : L"Microsoft YaHei UI",
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(ScaleText(kGdiFontHeight)),
                L"zh-CN",
                dwrite_text_format_.Put()), "CandidateUI::CreateTextFormat") &&
            dwrite_text_format_) {
            std::ignore = log::CheckHResult(dwrite_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP),
                                           "CandidateUI::SetWordWrapping");
            std::ignore = log::CheckHResult(dwrite_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR),
                                           "CandidateUI::SetParagraphAlignment");
        }
    }
}

int CandidateUI::MeasureTextWidth(HDC fallback_dc, std::wstring_view text) const noexcept {
    if (text.empty()) {
        return 0;
    }
    int width = 0;
    if (dwrite_factory_ && dwrite_text_format_) {
        ScopedDWriteTextLayout layout;
        if (log::CheckHResult(dwrite_factory_->CreateTextLayout(
                text.data(),
                gsl::narrow_cast<UINT32>(text.size()),
                dwrite_text_format_.Get(),
                kTextMeasurementWidth,
                static_cast<float>(popup_height_),
                layout.Put()), "CandidateUI::CreateTextLayout", log::Level::debug) &&
            layout) {
            DWRITE_TEXT_METRICS metrics{};
            if (log::CheckHResult(layout->GetMetrics(&metrics), "CandidateUI::GetMetrics", log::Level::debug)) {
                width = gsl::narrow_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace));
            }
        }
    }
    if (fallback_dc) {
        SIZE sz{};
        if (::GetTextExtentPoint32W(
                fallback_dc, text.data(), gsl::narrow_cast<int>(text.size()), &sz)) {
            return std::max(width, gsl::narrow_cast<int>(sz.cx));
        }
    }
    return std::max(width, gsl::narrow_cast<int>(text.size()) * ScaleText(kFallbackCharacterWidth));
}

std::wstring CandidateUI::WrapText(HDC dc, std::wstring_view text, int width) const {
    std::wstring wrapped;
    while (!text.empty()) {
        const size_t newline = text.find(L'\n');
        const size_t limit = newline == std::wstring_view::npos ? text.size() : newline;
        size_t low = 0;
        size_t high = limit;
        while (low < high) {
            const size_t middle = low + (high - low + 1) / 2;
            if (MeasureTextWidth(dc, text.substr(0, middle)) <= width) low = middle;
            else high = middle - 1;
        }
        size_t count = std::max<size_t>(1, low);
        count = std::min(count, text.size());
        if (count < text.size() && IS_HIGH_SURROGATE(gsl::at(text, count - 1)) && IS_LOW_SURROGATE(gsl::at(text, count))) {
            count = count > 1 ? count - 1 : std::min<size_t>(2, text.size());
        }
        if (limit == 0) count = 0;
        wrapped.append(text.substr(0, count));
        text.remove_prefix(count);
        if (!text.empty()) {
            wrapped += L'\n';
            if (text.front() == L'\n') text.remove_prefix(1);
        }
    }
    return wrapped;
}

std::wstring CandidateUI::FormatHeaderText() const {
    const auto& str = GetUiStrings(ui_language_);
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    std::wstring header = std::wstring(str.candidate_header_prefix) + raw_code_ + L"]";
    if (total_pages > 1) {
        header += L"  (" + std::to_wstring(current_page_ + 1) + L"/" + std::to_wstring(total_pages) + L")";
    }
    return header;
}

std::wstring CandidateUI::FormatCandidateLabel(size_t index_in_page, std::wstring_view candidate_text) const {
    const std::wstring prefix = std::to_wstring(index_in_page + 1) + L"." + std::wstring(candidate_text);
    return (index_in_page == selected_in_page_) ? (L"[" + prefix + L"]") : prefix;
}

void CandidateUI::RecalculatePopupLayout() {
    candidate_hit_rects_.clear();
    candidate_labels_.clear();
    prev_page_hit_rect_ = {};
    next_page_hit_rect_ = {};
    const ScopedWindowDC screen_dc(hwnd_.Get());
    const ScopedSelectObject sel_font(screen_dc.Get(), font_.Get());

    const int margin_x = std::min(ScaleByDpi(kHorizontalMargin), std::max(0, available_width_ / 8));
    const int item_gap = ScaleByDpi(kCandidateGap);
    const int padding = ScaleByDpi(preferences_.easy_reading ? 8 : 2);
    const int vertical_padding = ScaleByDpi(preferences_.easy_reading ? 8 : 1);
    const int row_height = std::max(line_height_ + vertical_padding * 2, ScaleByDpi(preferences_.easy_reading ? 44 : 28));
    const bool vertical = layout_ == CandidateLayout::Vertical;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    const bool english = ResolveUiLanguage(ui_language_) == UiLanguage::EnUs;
    const std::wstring_view previous_text = preferences_.easy_reading ? (english ? L"Previous" : L"上一页") : L"‹";
    const std::wstring_view next_text = preferences_.easy_reading ? (english ? L"Next" : L"下一页") : L"›";
    const int button_width = preferences_.easy_reading
        ? std::max(ScaleByDpi(44), MeasureTextWidth(screen_dc.Get(), english ? L"Previous" : L"上一页") + padding * 2)
        : ScaleByDpi(kPageButtonWidth);
    const int button_gap = ScaleByDpi(kPageButtonGap);
    const int navigation_width = total_pages > 1 ? button_width * 2 + button_gap : 0;
    const int header_width = MeasureTextWidth(screen_dc.Get(), FormatHeaderText());
    int natural_width = header_width + navigation_width + margin_x * 2 + (navigation_width ? item_gap : 0);
    int candidate_width = vertical ? 0 : margin_x;
    const auto [start, end] = ComputePageRange(all_candidates_.size(), page_size_, current_page_);
    for (size_t i = start; i < end; ++i) {
        const int width = std::max(MeasureTextWidth(screen_dc.Get(), FormatCandidateLabel(i - start, gsl::at(all_candidates_, i))) + padding * 2,
                                   ScaleByDpi(preferences_.easy_reading ? 44 : 0));
        candidate_width = vertical ? std::max(candidate_width, width) : candidate_width + width + item_gap;
    }
    if (start >= end) candidate_width = MeasureTextWidth(screen_dc.Get(), GetUiStrings(ui_language_).candidate_empty);
    natural_width = std::max(natural_width, candidate_width + margin_x * 2);
    if (!vertical) natural_width = std::max(ScaleByDpi(kPopupWidth), natural_width);
    popup_width_ = std::clamp(natural_width, 1, available_width_);

    // A second pass reserves the native scrollbar before wrapping, not after sizing.
    for (const int pass : {0, 1}) {
        const int scrollbar = pass == 1 ? ::GetSystemMetricsForDpi(SM_CXVSCROLL, dpi_) : 0;
        content_width_ = std::max(1, popup_width_ - scrollbar);
        const int inner_width = std::max(1, content_width_ - margin_x * 2);
        const int nav_top = ScaleByDpi(kHeaderTop);
        const bool nav_same_row = navigation_width > 0 && navigation_width + item_gap < inner_width;
        const int header_available = nav_same_row ? inner_width - navigation_width - item_gap : inner_width;
        header_label_ = WrapText(screen_dc.Get(), FormatHeaderText(), header_available);
        const int header_height = (gsl::narrow_cast<int>(std::count(header_label_.begin(), header_label_.end(), L'\n')) + 1) * line_height_;
        header_rect_ = {margin_x, nav_top, margin_x + header_available, nav_top + header_height};
        int row_top = std::max(ScaleByDpi(kCandidateRowTop), nav_top + header_height + vertical_padding);
        if (navigation_width) {
            const int top = nav_same_row ? nav_top : row_top;
            const bool stacked = navigation_width > inner_width;
            const int actual_button_width = stacked ? inner_width : button_width;
            const int right = content_width_ - margin_x;
            prev_page_label_ = WrapText(screen_dc.Get(), previous_text, std::max(1, actual_button_width - padding * 2));
            next_page_label_ = WrapText(screen_dc.Get(), next_text, std::max(1, actual_button_width - padding * 2));
            const int label_lines = 1 + std::max(
                gsl::narrow_cast<int>(std::count(prev_page_label_.begin(), prev_page_label_.end(), L'\n')),
                gsl::narrow_cast<int>(std::count(next_page_label_.begin(), next_page_label_.end(), L'\n')));
            const int navigation_height = preferences_.easy_reading
                ? std::max(row_height, label_lines * line_height_ + padding * 2) : line_height_;
            const int next_top = stacked ? top + navigation_height + vertical_padding : top;
            next_page_hit_rect_ = {right - actual_button_width, next_top, right, next_top + navigation_height};
            const int previous_right = stacked ? right : right - actual_button_width - button_gap;
            prev_page_hit_rect_ = {previous_right - actual_button_width, top, previous_right, top + navigation_height};
            row_top = std::max(row_top, next_top + navigation_height + vertical_padding);
        }
        candidate_hit_rects_.clear();
        candidate_labels_.clear();
        int cursor_x = margin_x;
        int row_bottom = row_top + row_height;
        for (size_t i = start; i < end; ++i) {
            const std::wstring label = FormatCandidateLabel(i - start, gsl::at(all_candidates_, i));
            const int width = vertical ? inner_width : std::min(inner_width,
                std::max(MeasureTextWidth(screen_dc.Get(), label) + padding * 2, ScaleByDpi(preferences_.easy_reading ? 44 : 0)));
            if (cursor_x > margin_x && (vertical || cursor_x + width > content_width_ - margin_x)) {
                row_top = row_bottom;
                cursor_x = margin_x;
            }
            std::wstring wrapped = WrapText(screen_dc.Get(), label, std::max(1, width - padding * 2));
            const int lines = gsl::narrow_cast<int>(std::count(wrapped.begin(), wrapped.end(), L'\n')) + 1;
            const int height = std::max(row_height, lines * line_height_ + vertical_padding * 2);
            candidate_hit_rects_.push_back(RECT{cursor_x, row_top, cursor_x + width, row_top + height});
            candidate_labels_.push_back(std::move(wrapped));
            row_bottom = std::max(row_bottom, row_top + height);
            cursor_x += width + item_gap;
        }
        if (start >= end) {
            empty_label_ = WrapText(screen_dc.Get(), GetUiStrings(ui_language_).candidate_empty, inner_width);
            const int empty_height = std::max(row_height,
                (gsl::narrow_cast<int>(std::count(empty_label_.begin(), empty_label_.end(), L'\n')) + 1) * line_height_ + vertical_padding * 2);
            empty_rect_ = {margin_x, row_top, margin_x + inner_width, row_top + empty_height};
            row_bottom = row_top + empty_height;
        }
        content_height_ = std::max(ScaleByDpi(kPopupHeight), row_bottom + ScaleByDpi(kBottomPadding));
        popup_height_ = std::min(content_height_, available_height_);
        if (content_height_ <= available_height_ || pass == 1) break;
    }
    scroll_offset_ = std::clamp(scroll_offset_, 0, std::max(0, content_height_ - popup_height_));
    if (selected_in_page_ < candidate_hit_rects_.size()) {
        const RECT selected = gsl::at(candidate_hit_rects_, selected_in_page_);
        if (selected.top < scroll_offset_ || selected.bottom - selected.top > popup_height_) {
            scroll_offset_ = selected.top;
        } else if (selected.bottom > scroll_offset_ + popup_height_) {
            scroll_offset_ = selected.bottom - popup_height_;
        }
    }
    scroll_offset_ = std::clamp(scroll_offset_, 0, std::max(0, content_height_ - popup_height_));
}

void CandidateUI::OnLeftButtonDown(POINT pt) const {
    pt.y += scroll_offset_;
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    if (total_pages > 1 && page_turn_callback_) {
        if (current_page_ > 0 && ::PtInRect(&prev_page_hit_rect_, pt)) {
            page_turn_callback_(false);
            return;
        }
        if (current_page_ + 1 < total_pages && ::PtInRect(&next_page_hit_rect_, pt)) {
            page_turn_callback_(true);
            return;
        }
    }
    if (!select_callback_) {
        return;
    }
    for (size_t i = 0; i < candidate_hit_rects_.size(); ++i) {
        if (::PtInRect(&gsl::at(candidate_hit_rects_, i), pt)) {
            select_callback_(i);
            return;
        }
    }
}

STDMETHODIMP CandidateUI::GetDescription(BSTR* pbstrDescription) noexcept {
    return CopyToBstr(L"QiQi Wubi Candidate List", pbstrDescription);
}

STDMETHODIMP CandidateUI::GetGUID(GUID* pguid) noexcept {
    if (!pguid) {
        return E_INVALIDARG;
    }
    *pguid = kGuidCandidateUIElement;
    return S_OK;
}

STDMETHODIMP CandidateUI::get_ProviderOptions(ProviderOptions* options) noexcept {
    if (!options) return E_INVALIDARG;
    *options = ProviderOptions_ServerSideProvider;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetPatternProvider(
    PATTERNID pattern_id, IUnknown** provider) noexcept {
    if (!provider) return E_INVALIDARG;
    *provider = nullptr;
    if (pattern_id == UIA_SelectionPatternId) {
        return QueryInterface(__uuidof(ISelectionProvider), reinterpret_cast<void**>(provider));
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetPropertyValue(
    PROPERTYID property_id, VARIANT* value) noexcept {
    if (!value) return E_INVALIDARG;
    ::VariantInit(value);
    switch (property_id) {
        case UIA_AutomationIdPropertyId:
            return MakeBstrVariant(L"IME_Candidate_Window", value);
        case UIA_ControlTypePropertyId:
            value->vt = VT_I4;
            value->lVal = UIA_ListControlTypeId;
            return S_OK;
        case UIA_NamePropertyId:
            return MakeBstrVariant(L"IME Candidate Window", value);
        case UIA_IsEnabledPropertyId:
            value->vt = VT_BOOL;
            value->boolVal = VARIANT_TRUE;
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            value->vt = VT_BOOL;
            value->boolVal =
                !is_shown_ || !hwnd_ || !::IsWindowVisible(hwnd_.Get())
                ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        default:
            return S_OK;
    }
}

STDMETHODIMP CandidateUI::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) noexcept {
    if (!provider) return E_INVALIDARG;
    *provider = nullptr;
    if (!hwnd_) return S_OK;
    const HRESULT hr = ::UiaHostProviderFromHwnd(hwnd_.Get(), provider);
    if (FAILED(hr)) {
        QIQI_LOG_WARN(
            "CandidateUI::UiaHostProviderFromHwnd failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

STDMETHODIMP CandidateUI::Navigate(
    NavigateDirection direction, IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (direction == NavigateDirection_FirstChild && AccessibleCandidateCount() > 0) {
        return CreateAutomationItem(0, fragment);
    }
    if (direction == NavigateDirection_LastChild && AccessibleCandidateCount() > 0) {
        return CreateAutomationItem(AccessibleCandidateCount() - 1, fragment);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetRuntimeId(SAFEARRAY** runtime_id) noexcept {
    return MakeRuntimeId(hwnd_.Get(), -1, runtime_id);
}

STDMETHODIMP CandidateUI::get_BoundingRectangle(UiaRect* bounds) noexcept {
    if (!bounds) return E_INVALIDARG;
    *bounds = {};
    RECT rect{};
    if (hwnd_ && ::GetWindowRect(hwnd_.Get(), &rect)) {
        bounds->left = static_cast<double>(rect.left);
        bounds->top = static_cast<double>(rect.top);
        bounds->width = static_cast<double>(rect.right) -
            static_cast<double>(rect.left);
        bounds->height = static_cast<double>(rect.bottom) -
            static_cast<double>(rect.top);
        return S_OK;
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept {
    if (!roots) return E_INVALIDARG;
    *roots = nullptr;
    return S_OK;
}

STDMETHODIMP CandidateUI::SetFocus() noexcept {
    return S_OK;
}

STDMETHODIMP CandidateUI::get_FragmentRoot(
    IRawElementProviderFragmentRoot** root) noexcept {
    if (!root) return E_INVALIDARG;
    *root = nullptr;
    return QueryInterface(
        __uuidof(IRawElementProviderFragmentRoot), reinterpret_cast<void**>(root));
}

STDMETHODIMP CandidateUI::ElementProviderFromPoint(
    double x, double y, IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (!hwnd_) return S_OK;
    const POINT point{
        gsl::narrow_cast<LONG>(x),
        gsl::narrow_cast<LONG>(y)
    };
    POINT client = point;
    if (!::ScreenToClient(hwnd_.Get(), &client)) return S_OK;
    for (size_t i = 0; i < AccessibleCandidateCount(); ++i) {
        const RECT rect = CandidateRect(i);
        if (::PtInRect(&rect, client)) {
            return CreateAutomationItem(i, fragment);
        }
    }
    return QueryInterface(
        __uuidof(IRawElementProviderFragment), reinterpret_cast<void**>(fragment));
}

STDMETHODIMP CandidateUI::GetFocus(
    IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (AccessibleCandidateCount() == 0) return S_OK;
    return CreateAutomationItem(SelectedPageIndex(), fragment);
}

STDMETHODIMP CandidateUI::GetSelection(SAFEARRAY** selection) noexcept {
    if (!selection) return E_INVALIDARG;
    *selection = nullptr;
    SAFEARRAYBOUND bound{
        AccessibleCandidateCount() > 0 ? 1ul : 0ul,
        0
    };
    *selection = ::SafeArrayCreate(VT_UNKNOWN, 1, &bound);
    if (!*selection) return E_OUTOFMEMORY;
    if (bound.cElements == 0) return S_OK;
    ComPtr<IRawElementProviderFragment> fragment;
    ComPtr<IRawElementProviderSimple> selected;
    const HRESULT hr = CreateAutomationItem(SelectedPageIndex(), fragment.GetAddressOf());
    if (FAILED(hr) || !fragment || FAILED(fragment.As(&selected)) || !selected) {
        ::SafeArrayDestroy(*selection);
        *selection = nullptr;
        return FAILED(hr) ? hr : E_FAIL;
    }
    LONG index = 0;
    IUnknown* selected_item = selected.Get();
    const HRESULT put_hr = ::SafeArrayPutElement(
        *selection, &index, &selected_item);
    if (FAILED(put_hr)) {
        ::SafeArrayDestroy(*selection);
        *selection = nullptr;
        return put_hr;
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::get_CanSelectMultiple(
    BOOL* can_select_multiple) noexcept {
    if (!can_select_multiple) return E_INVALIDARG;
    *can_select_multiple = FALSE;
    return S_OK;
}

STDMETHODIMP CandidateUI::get_IsSelectionRequired(
    BOOL* selection_required) noexcept {
    if (!selection_required) return E_INVALIDARG;
    *selection_required = AccessibleCandidateCount() > 0;
    return S_OK;
}

STDMETHODIMP CandidateUI::Show(BOOL bShow) noexcept {
    const auto keep_alive = RetainForWindowOperation();
    const bool blocked =
        bShow && (!presentation_enabled_ || destroying_window_ ||
                  searchbox_integration_);
    if (blocked) {
        show_requested_ = false;
        is_shown_ = FALSE;
        if (hwnd_) {
            if (accessibility_open_announced_) {
                accessibility_open_announced_ = false;
                std::ignore = RaiseAccessibilityEvent(UIA_MenuClosedEventId);
            }
            ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
        return S_FALSE;
    }
    show_requested_ = bShow != FALSE;
    is_shown_ = FALSE;
    if (bShow) {
        if (EnsurePopupWindow()) {
            is_shown_ = TRUE;
            if (!::IsWindowVisible(hwnd_.Get())) {
                ::ShowWindow(hwnd_.Get(), SW_SHOWNOACTIVATE);
                ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
            }
            LogPopupVisibility();
            if (!accessibility_open_announced_) {
                accessibility_open_announced_ = true;
                std::ignore = RaiseAccessibilityEvent(UIA_MenuOpenedEventId);
                if (AccessibleCandidateCount() > 0) {
                    std::ignore = RaiseAccessibilityEvent(
                        UIA_SelectionItem_ElementSelectedEventId, SelectedPageIndex());
                }
            }
        } else {
            return S_FALSE;
        }
    } else if (hwnd_) {
        if (accessibility_open_announced_) {
            accessibility_open_announced_ = false;
            std::ignore = RaiseAccessibilityEvent(UIA_MenuClosedEventId);
        }
        ::ShowWindow(hwnd_.Get(), SW_HIDE);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::IsShown(BOOL* pbShow) noexcept {
    if (!pbShow) {
        return E_INVALIDARG;
    }
    *pbShow = is_shown_;
    return S_OK;
}

STDMETHODIMP CandidateUI::GetUpdatedFlags(DWORD* pdwFlags) noexcept {
    if (!pdwFlags) {
        return E_INVALIDARG;
    }
    *pdwFlags = ui_element_lifecycle_.UpdatedFlags();
    return S_OK;
}

STDMETHODIMP CandidateUI::GetDocumentMgr(ITfDocumentMgr** ppdim) noexcept {
    if (!ppdim) {
        return E_INVALIDARG;
    }
    *ppdim = nullptr;
    if (!ui_element_lifecycle_.DocumentManager()) {
        return E_FAIL;
    }
    *ppdim = ui_element_lifecycle_.DocumentManager();
#pragma warning(suppress : 26447)
    (*ppdim)->AddRef();
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCount(UINT* puCount) noexcept {
    if (!puCount) {
        return E_INVALIDARG;
    }
    *puCount = gsl::narrow_cast<UINT>(all_candidates_.size());
    return S_OK;
}

STDMETHODIMP CandidateUI::GetSelection(UINT* puIndex) noexcept {
    if (!puIndex) {
        return E_INVALIDARG;
    }
    if (all_candidates_.empty()) {
        return S_FALSE;
    }
    const size_t global_idx = current_page_ * page_size_ + selected_in_page_;
    *puIndex = gsl::narrow_cast<UINT>(std::min(global_idx, all_candidates_.size() - 1));
    return S_OK;
}

STDMETHODIMP CandidateUI::GetString(UINT uIndex, BSTR* pstr) noexcept {
    if (!pstr) {
        return E_INVALIDARG;
    }
    *pstr = nullptr;
    const auto& candidates =
        searchbox_integration_ &&
                integrated_candidates_.size() == all_candidates_.size()
        ? integrated_candidates_
        : all_candidates_;
    if (uIndex >= candidates.size()) {
        return E_INVALIDARG;
    }
    return CopyToBstr(gsl::at(candidates, uIndex), pstr);
}

STDMETHODIMP CandidateUI::GetPageIndex(UINT* pIndex, UINT uSize, UINT* puPageCnt) noexcept {
    if (!puPageCnt) {
        return E_INVALIDARG;
    }
    const size_t total_pages = ComputeTotalPages(all_candidates_.size(), page_size_);
    *puPageCnt = gsl::narrow_cast<UINT>(total_pages);
    if (!pIndex) {
        return S_OK;
    }
    const std::span<UINT> out_indices(pIndex, uSize);
    const size_t count_to_copy = std::min<size_t>(out_indices.size(), total_pages);
    for (size_t i = 0; i < count_to_copy; ++i) {
        gsl::at(out_indices, i) = gsl::narrow_cast<UINT>(i * page_size_);
    }
    return S_OK;
}

STDMETHODIMP CandidateUI::SetPageIndex(UINT* /*pIndex*/, UINT /*uPageCnt*/) noexcept {
    return S_OK;
}

STDMETHODIMP CandidateUI::GetCurrentPage(UINT* puPage) noexcept {
    if (!puPage) {
        return E_INVALIDARG;
    }
    *puPage = gsl::narrow_cast<UINT>(current_page_);
    return S_OK;
}

HRESULT CandidateUI::InvokeActionCallback(
    CandidateUiAction action, size_t index) noexcept {
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = action_callback_;
        if (!callback) return E_NOTIMPL;
        return callback(action, index);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI action callback allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP CandidateUI::SetSelection(UINT index) noexcept {
    if (index >= all_candidates_.size()) return E_INVALIDARG;
    return InvokeActionCallback(CandidateUiAction::SetSelection, index);
}

STDMETHODIMP CandidateUI::Finalize() noexcept {
    return InvokeActionCallback(CandidateUiAction::Finalize, 0);
}

STDMETHODIMP CandidateUI::Abort() noexcept {
    return InvokeActionCallback(CandidateUiAction::Abort, 0);
}

STDMETHODIMP CandidateUI::SetIntegrationStyle(
    GUID integration_style) noexcept {
    searchbox_integration_ =
        IsEqualGUID(integration_style, kGuidIntegrationStyleSearchbox);
    if (searchbox_integration_) {
        const HRESULT hide_hr = Show(FALSE);
        if (FAILED(hide_hr)) return hide_hr;
    }
    return searchbox_integration_ ? S_OK : E_NOTIMPL;
}

STDMETHODIMP CandidateUI::GetSelectionStyle(
    TfIntegratableCandidateListSelectionStyle* selection_style) noexcept {
    if (!selection_style) return E_INVALIDARG;
    *selection_style = STYLE_ACTIVE_SELECTION;
    return S_OK;
}

STDMETHODIMP CandidateUI::OnKeyDown(
    WPARAM wparam, LPARAM lparam, BOOL* eaten) noexcept {
    if (!eaten) return E_INVALIDARG;
    *eaten = FALSE;
    if (!searchbox_integration_) return E_NOTIMPL;
    const ComPtr<CandidateUI> self_holder(this);
    try {
        const auto callback = integrated_key_down_callback_;
        if (!callback) return E_NOTIMPL;
        return callback(wparam, lparam, eaten);
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR(
            "CandidateUI::OnKeyDown allocation failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(E_OUTOFMEMORY));
        return E_OUTOFMEMORY;
    }
}

STDMETHODIMP CandidateUI::ShowCandidateNumbers(BOOL* show) noexcept {
    if (!show) return E_INVALIDARG;
    *show = TRUE;
    return S_OK;
}

STDMETHODIMP CandidateUI::FinalizeExactCompositionString() noexcept {
    return InvokeActionCallback(CandidateUiAction::FinalizeExact, 0);
}

void CandidateUI::SetCandidates(std::wstring code,
                                std::vector<std::wstring> all_candidates,
                                size_t page_size,
                                size_t current_page,
                                size_t selected_in_page,
                                ITfDocumentMgr* document_manager,
                                const std::vector<std::wstring>* raw_candidates) {
    const size_t effective_page_size = page_size > 0 ? page_size : 5;
    const size_t old_page_count = ComputeTotalPages(all_candidates_.size(), page_size_);
    const size_t new_page_count = ComputeTotalPages(all_candidates.size(), effective_page_size);
    const size_t previous_global_selection =
        current_page_ * page_size_ + selected_in_page_;
    const size_t new_global_selection =
        current_page * effective_page_size + selected_in_page;
    const bool previous_had_selection = !all_candidates_.empty();
    const bool new_had_selection = !all_candidates.empty();
    std::vector<std::wstring> integrated_candidates;
    integrated_candidates.reserve(all_candidates.size());
    if (raw_candidates &&
        raw_candidates->size() == all_candidates.size()) {
        integrated_candidates = *raw_candidates;
    } else {
        for (const auto& candidate : all_candidates) {
            integrated_candidates.emplace_back(
                StripCodeAnnotation(candidate));
        }
    }
    const bool integrated_strings_changed =
        integrated_candidates_ != integrated_candidates;
    const bool candidate_tree_changed =
        all_candidates_ != all_candidates ||
        page_size_ != effective_page_size ||
        current_page_ != current_page;
    const bool document_changed =
        ui_element_lifecycle_.DocumentManager() != document_manager;
    const bool count_changed = all_candidates_.size() != all_candidates.size();
    const bool strings_changed =
        all_candidates_ != all_candidates || integrated_strings_changed;
    const bool page_index_changed =
        page_size_ != effective_page_size || old_page_count != new_page_count;
    const bool page_changed = current_page_ != current_page;
    const bool selection_changed =
        previous_had_selection != new_had_selection ||
        (new_had_selection &&
         std::min(previous_global_selection, all_candidates_.empty()
             ? 0 : all_candidates_.size() - 1) !=
         std::min(new_global_selection, all_candidates.empty()
             ? 0 : all_candidates.size() - 1));
    raw_code_ = std::move(code);
    all_candidates_ = std::move(all_candidates);
    integrated_candidates_ = std::move(integrated_candidates);
    page_size_ = effective_page_size;
    current_page_ = current_page;
    selected_in_page_ = selected_in_page;
    ui_element_lifecycle_.SetDocumentManager(document_manager);
    DWORD changed_flags = 0;
    if (document_changed) changed_flags |= TF_CLUIE_DOCUMENTMGR;
    if (count_changed) changed_flags |= TF_CLUIE_COUNT | TF_CLUIE_PAGEINDEX;
    if (selection_changed) changed_flags |= TF_CLUIE_SELECTION;
    if (strings_changed) changed_flags |= TF_CLUIE_STRING;
    if (page_index_changed) changed_flags |= TF_CLUIE_PAGEINDEX;
    if (page_changed) changed_flags |= TF_CLUIE_CURRENTPAGE;
    ui_element_lifecycle_.MarkCandidateUpdate(changed_flags);

    RefreshPopupLayout();
    if (is_shown_ && hwnd_ && candidate_tree_changed) {
        std::ignore = RaiseAccessibilityStructureChanged();
    }
    if (is_shown_ && hwnd_ && AccessibleCandidateCount() > 0) {
        if (candidate_tree_changed || selection_changed) {
            std::ignore = RaiseAccessibilityEvent(
                UIA_SelectionItem_ElementSelectedEventId, SelectedPageIndex());
        }
    }
}

HRESULT CandidateUI::CreateAutomationItem(
    size_t page_index, IRawElementProviderFragment** fragment) noexcept {
    if (!fragment) return E_INVALIDARG;
    *fragment = nullptr;
    if (page_index >= AccessibleCandidateCount()) return S_OK;
    return CreateComObjectToOut<CandidateAutomationItem>(
        fragment, ComPtr<CandidateUI>(this), page_index);
}

size_t CandidateUI::SelectedPageIndex() const noexcept {
    const size_t count = AccessibleCandidateCount();
    return count > 0 ? std::min(selected_in_page_, count - 1) : 0;
}

std::wstring_view CandidateUI::AccessibleCandidateName(size_t page_index) const noexcept {
    const size_t global_index = current_page_ * page_size_ + page_index;
    if (global_index >= all_candidates_.size()) return {};
    return StripCodeAnnotation(gsl::at(all_candidates_, global_index));
}

std::wstring CandidateUI::AccessibleCandidateHelp(size_t page_index) const {
    const size_t global_index = current_page_ * page_size_ + page_index;
    if (global_index >= all_candidates_.size()) return {};
    const std::wstring_view display = gsl::at(all_candidates_, global_index);
    const std::wstring_view name = StripCodeAnnotation(display);
    if (name.size() == display.size()) return {};
    return std::wstring(display.substr(name.size()));
}

HRESULT CandidateUI::RaiseAccessibilityEvent(
    EVENTID event_id, size_t page_index) noexcept {
    try {
        if (accessibility_event_observer_for_test_) {
            accessibility_event_observer_for_test_(event_id);
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityEvent observer failed (event={})", event_id);
    }
    if (!hwnd_ || (!is_shown_ && event_id != UIA_MenuClosedEventId)) return S_OK;
    const bool observe_dispatch = accessibility_dispatch_observer_for_test_ != nullptr;
    if ((!detail::UiAutomationEventsEnabledForTest() || !::UiaClientsAreListening()) &&
        !observe_dispatch) {
        return S_OK;
    }
    ComPtr<IRawElementProviderSimple> provider;
    HRESULT hr = E_FAIL;
    if (event_id == UIA_SelectionItem_ElementSelectedEventId) {
        if (page_index >= AccessibleCandidateCount()) return S_OK;
        ComPtr<IRawElementProviderFragment> item;
        hr = CreateAutomationItem(page_index, item.GetAddressOf());
        if (SUCCEEDED(hr) && item) {
            hr = item.As(&provider);
        }
    } else {
        hr = QueryInterface(
            __uuidof(IRawElementProviderSimple),
            reinterpret_cast<void**>(provider.GetAddressOf()));
    }
    if (FAILED(hr) || !provider) {
        QIQI_LOG_WARN(
            "CandidateUI::RaiseAccessibilityEvent provider failed (event={}, hr=0x{:08x})",
            event_id, gsl::narrow_cast<unsigned int>(hr));
        return hr;
    }
    try {
        if (accessibility_dispatch_observer_for_test_) {
            accessibility_dispatch_observer_for_test_(
                event_id,
                provider.Get() == this);
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityEvent dispatch observer failed (event={})",
            event_id);
    }
    if (!detail::UiAutomationEventsEnabledForTest() ||
        !::UiaClientsAreListening()) {
        return S_OK;
    }
    hr = ::UiaRaiseAutomationEvent(provider.Get(), event_id);
    if (FAILED(hr)) {
        QIQI_LOG_DEBUG(
            "CandidateUI::UiaRaiseAutomationEvent failed (event={}, hr=0x{:08x})",
            event_id, gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

HRESULT CandidateUI::RaiseAccessibilityStructureChanged() noexcept {
    try {
        if (accessibility_structure_observer_for_test_) {
            accessibility_structure_observer_for_test_();
        }
    } catch (...) {
        QIQI_LOG_ERROR(
            "CandidateUI::RaiseAccessibilityStructureChanged observer failed");
    }
    if (!hwnd_ || !is_shown_ || !detail::UiAutomationEventsEnabledForTest() ||
        !::UiaClientsAreListening()) {
        return S_OK;
    }
    const HRESULT hr = ::UiaRaiseStructureChangedEvent(
        this, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    if (FAILED(hr)) {
        QIQI_LOG_DEBUG(
            "CandidateUI::UiaRaiseStructureChangedEvent failed (hr=0x{:08x})",
            gsl::narrow_cast<unsigned int>(hr));
    }
    return hr;
}

void CandidateUI::MoveWindowTo(
    const RECT& text_rect, HWND host_window, bool require_owned_window) {
    const auto keep_alive = RetainForWindowOperation();
    const HWND owner = (host_window && ::IsWindow(host_window))
        ? ::GetAncestor(host_window, GA_ROOT)
        : nullptr;
    if (require_owned_window && (!host_window || !::IsWindow(host_window) || !owner || !::IsWindow(owner))) {
        QIQI_LOG_WARN(
            "CandidateUI::MoveWindowTo owner unavailable (require_owned={}, host_valid={}, root_valid={}); presentation deferred",
            require_owned_window, host_window && ::IsWindow(host_window), owner && ::IsWindow(owner));
    }
    require_owned_window_ = require_owned_window;
    if (require_owned_window && (!owner || !::IsWindow(owner))) {
        const bool retry = show_requested_;
        if (hwnd_) {
            std::ignore = ::ShowWindow(hwnd_.Get(), SW_HIDE);
        }
        is_shown_ = FALSE;
        if (DestroyPopupWindow()) {
            show_requested_ = retry;
        }
        popup_owner_ = nullptr;
        text_rect_ = text_rect;
        RefreshPopupLayout();
        return;
    }
    if (owner != popup_owner_ && hwnd_) {
        // Establish the host relationship at creation, including for immersive hosts.
        const bool retry = show_requested_;
        if (!DestroyPopupWindow()) return;
        show_requested_ = retry;
    }
    popup_owner_ = owner;
    text_rect_ = text_rect;
    RefreshPopupLayout();
    if (show_requested_ && !is_shown_) {
        std::ignore = Show(TRUE);
    }
}

void CandidateUI::SetLayout(CandidateLayout layout) {
    if (layout_ == layout) return;
    layout_ = layout;
    RefreshPopupLayout();
}

void CandidateUI::SetDisplayPreferences(DisplayPreferences preferences) {
    const auto keep_alive = RetainForWindowOperation();
    if (preferences_ == preferences) return;
    preferences_ = preferences;
    appearance_ = ResolveDisplayAppearance(preferences_, SystemThemeSource::App);
    RefreshPopupLayout();
    if (hwnd_) ApplyDisplayThemeToWindow(hwnd_.Get(), appearance_);
}

void CandidateUI::SetUiLanguage(UiLanguage lang) {
    if (ui_language_ == lang) return;
    ui_language_ = lang;
    RefreshPopupLayout();
}

RECT CandidateUI::ViewportRect(RECT rect) const noexcept {
    if (rect.right > rect.left) ::OffsetRect(&rect, 0, -scroll_offset_);
    return rect;
}

void CandidateUI::UpdateScrollBar() const noexcept {
    if (!hwnd_) return;
    if (content_height_ <= popup_height_) {
        return;
    }
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    info.nMin = 0;
    info.nMax = std::max(0, content_height_ - 1);
    info.nPage = gsl::narrow_cast<UINT>(popup_height_);
    info.nPos = scroll_offset_;
    SCROLLINFO current{};
    current.cbSize = sizeof(current);
    current.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    if (::GetScrollInfo(hwnd_.Get(), SB_VERT, &current)) {
        if (current.nMin == info.nMin && current.nMax == info.nMax &&
            current.nPage == info.nPage && current.nPos == info.nPos) return;
    } else {
        QIQI_LOG_WARN("CandidateUI::GetScrollInfo failed (error={})", ::GetLastError());
    }
    ::SetScrollInfo(hwnd_.Get(), SB_VERT, &info, TRUE);
}

void CandidateUI::ScrollContent(int position) {
    const int next = std::clamp(position, 0, std::max(0, content_height_ - popup_height_));
    if (scroll_offset_ == next) return;
    scroll_offset_ = next;
    UpdateScrollBar();
    if (hwnd_) ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
}

void CandidateUI::RefreshPopupLayout(UINT dpi) {
    const auto keep_alive = RetainForWindowOperation();
    const POINT anchor = text_rect_
        ? POINT{text_rect_->left, text_rect_->bottom + kCaretGapPx}
        : popup_pos_;
    const MonitorMetrics mon = QueryMonitorForPoint(anchor, hwnd_.Get());
    available_width_ = std::max(1L, mon.work_area.right - mon.work_area.left);
    available_height_ = std::max(1L, mon.work_area.bottom - mon.work_area.top);
    UpdateDpiAndFont(dpi > 0 ? dpi : mon.dpi);
    RecalculatePopupLayout();
    popup_pos_ = anchor;
    if (text_rect_ && popup_pos_.y + popup_height_ > mon.work_area.bottom) {
        popup_pos_.y = text_rect_->top - popup_height_ - kCaretGapPx;
    }
    popup_pos_ = ClampPointToWorkArea(popup_pos_, popup_width_, popup_height_, mon.work_area);

    const RECT text_rect = text_rect_.value_or(RECT{anchor.x, anchor.y, anchor.x, anchor.y});
    QIQI_LOG_DEBUG("CandidatePositionPopup(stage=move, rect=({}, {}, {}, {}), target=({}, {}), size={}x{}, dpi={}, has_window={}, shown={})",
                   text_rect.left, text_rect.top, text_rect.right, text_rect.bottom,
                   popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_, hwnd_.Get() != nullptr, is_shown_ != FALSE);
    if (hwnd_) {
        const LONG_PTR style = ::GetWindowLongPtrW(hwnd_.Get(), GWL_STYLE);
        const LONG_PTR desired_style = content_height_ > popup_height_ ? style | WS_VSCROLL : style & ~WS_VSCROLL;
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER;
        if (style != desired_style) {
            ::SetLastError(ERROR_SUCCESS);
            if (!::SetWindowLongPtrW(hwnd_.Get(), GWL_STYLE, desired_style) &&
                ::GetLastError() != ERROR_SUCCESS) {
                QIQI_LOG_ERROR("CandidateUI::SetWindowLongPtrW(style) failed (error={})", ::GetLastError());
                return;
            }
            flags |= SWP_FRAMECHANGED;
        }
        RECT current{};
        if (::GetWindowRect(hwnd_.Get(), &current)) {
            if (current.left == popup_pos_.x && current.top == popup_pos_.y) flags |= SWP_NOMOVE;
            if (current.right - current.left == popup_width_ &&
                current.bottom - current.top == popup_height_) flags |= SWP_NOSIZE;
        } else {
            QIQI_LOG_WARN("CandidateUI::GetWindowRect failed (error={})", ::GetLastError());
        }
        const bool update_window = (flags & SWP_FRAMECHANGED) ||
            !(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE);
        if (update_window && !::SetWindowPos(hwnd_.Get(), nullptr, popup_pos_.x, popup_pos_.y,
                                            popup_width_, popup_height_, flags)) {
            QIQI_LOG_ERROR("CandidatePositionPopup::SetWindowPos failed (error={})", ::GetLastError());
        } else if (log::ShouldLog(log::Level::debug)) {
            RECT actual{};
            if (::GetWindowRect(hwnd_.Get(), &actual)) {
                QIQI_LOG_DEBUG("CandidatePositionPopup(stage=moved, rect=({}, {}, {}, {}))",
                               actual.left, actual.top, actual.right, actual.bottom);
            } else {
                QIQI_LOG_ERROR("CandidatePositionPopup::GetWindowRect failed (error={})", ::GetLastError());
            }
        }
        UpdateScrollBar();
        ::InvalidateRect(hwnd_.Get(), nullptr, FALSE);
    }
}

bool CandidateUI::EnsurePopupWindow() noexcept {
    if (!presentation_enabled_ || destroying_window_) return false;
    if (hwnd_ && ::IsWindow(hwnd_.Get())) {
        if (require_owned_window_ && (!popup_owner_ || !::IsWindow(popup_owner_))) {
            QIQI_LOG_WARN(
                "CandidateUI::EnsurePopupWindow existing popup owner is invalid; presentation deferred");
            return false;
        }
        return true;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_IME;
    wc.lpfnWndProc = &CandidateUI::WndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kCandidateWndClass;
    ::RegisterClassExW(&wc);

    if (!popup_owner_ && require_owned_window_) {
        QIQI_LOG_WARN("CandidateUI::EnsurePopupWindow owner missing; presentation deferred");
        return false;
    }
    if (popup_owner_ && !::IsWindow(popup_owner_)) {
        QIQI_LOG_WARN(
            "CandidateUI::EnsurePopupWindow owner became invalid before creation; presentation deferred");
        return false;
    }
    hwnd_.Reset(::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kCandidateWndClass,
        L"",
        WS_POPUP | (content_height_ > popup_height_ ? WS_VSCROLL : 0),
        popup_pos_.x, popup_pos_.y, popup_width_, popup_height_,
        popup_owner_, nullptr, instance_, this));

    if (!hwnd_) {
        QIQI_LOG_ERROR("CandidateUI::EnsurePopupWindow CreateWindowExW failed (error={})", ::GetLastError());
    } else {
        ApplyDisplayThemeToWindow(hwnd_.Get(), appearance_);
        UpdateScrollBar();
        QIQI_LOG_DEBUG("CandidateUI popup window created (pos=({}, {}), size={}x{}, dpi={})",
                       popup_pos_.x, popup_pos_.y, popup_width_, popup_height_, dpi_);
    }
    return hwnd_.IsValid();
}

void CandidateUI::NotifyPopupWindowState(HWND window, bool destroyed) noexcept {
    DWORD event = EVENT_OBJECT_IME_HIDE;
    if (destroyed || !::IsWindowVisible(window)) {
        if (!announced_popup_rect_) return;
        announced_popup_rect_.reset();
    } else {
        RECT rect{};
        if (!::GetWindowRect(window, &rect)) {
            QIQI_LOG_ERROR("CandidateUI::NotifyPopupWindowState GetWindowRect failed (error={})",
                           ::GetLastError());
            return;
        }
        if (!announced_popup_rect_) {
            event = EVENT_OBJECT_IME_SHOW;
        } else if (!::EqualRect(&rect, &*announced_popup_rect_)) {
            event = EVENT_OBJECT_IME_CHANGE;
        } else {
            return;
        }
        announced_popup_rect_ = rect;
    }
    // In-context WinEvent clients may reenter before NotifyWinEvent returns.
    QIQI_LOG_CAT_DEBUG(log::Keyword::UI, "CandidateImeEvent(event=0x{:04x}, object={}, child={})",
                       event, OBJID_CLIENT, CHILDID_SELF);
    ::NotifyWinEvent(event, window, OBJID_CLIENT, CHILDID_SELF);
}

void CandidateUI::LogPopupVisibility() const noexcept {
    if (!log::ShouldLog(log::Level::debug) || !hwnd_) return;
    WINDOWINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetWindowInfo(hwnd_.Get(), &info)) {
        QIQI_LOG_ERROR("CandidateUI::LogPopupVisibility GetWindowInfo failed (error={})", ::GetLastError());
        return;
    }
    DWORD cloaked = 0;
    const HRESULT cloak_hr = ::DwmGetWindowAttribute(hwnd_.Get(), DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    std::ignore = log::CheckHResult(cloak_hr, "CandidateUI::DwmGetWindowAttribute(DWMWA_CLOAKED)", log::Level::debug);
    QIQI_LOG_DEBUG("CandidateVisibility(visible={}, topmost={}, has_owner={}, cloaked={}, cloak_hr=0x{:08x}, rect=({}, {}, {}, {}))",
                   ::IsWindowVisible(hwnd_.Get()) != FALSE, (info.dwExStyle & WS_EX_TOPMOST) != 0,
                   ::GetWindow(hwnd_.Get(), GW_OWNER) != nullptr,
                   SUCCEEDED(cloak_hr) ? gsl::narrow_cast<int>(cloaked) : -1,
                   gsl::narrow_cast<unsigned int>(cloak_hr),
                   info.rcWindow.left, info.rcWindow.top, info.rcWindow.right, info.rcWindow.bottom);
}

bool CandidateUI::DestroyPopupWindow() noexcept {
    if (destroying_window_) return true;
    if (!CheckWindowOwnerThread(hwnd_.Get(), "CandidateUI::DestroyPopupWindow")) return false;
    const auto keep_alive = RetainForWindowOperation();
    destroying_window_ = true;
    const auto finish = gsl::finally([this]() noexcept { destroying_window_ = false; });
    if (accessibility_open_announced_) {
        accessibility_open_announced_ = false;
        std::ignore = RaiseAccessibilityEvent(UIA_MenuClosedEventId);
    }
    is_shown_ = FALSE;
    show_requested_ = false;
    const HWND hwnd = hwnd_.Get();
    const bool provider_returned =
        std::exchange(accessibility_provider_returned_, false);
    if (provider_returned) {
        std::ignore = log::CheckHResult(
            ::UiaDisconnectProvider(this),
            "CandidateUI::UiaDisconnectProvider");
    }
    const bool destroyed = DestroyWindowOnOwnerThread(
        hwnd_, "CandidateUI::DestroyPopupWindow");
    if (provider_returned && destroyed && hwnd) {
        std::ignore = ::UiaReturnRawElementProvider(
            hwnd, 0, 0, nullptr);
    }
    return destroyed;
}

std::vector<CandidateUI::PaintItem> CandidateUI::BuildPaintItems() const {
    std::vector<PaintItem> items;
    items.push_back({ViewportRect(header_rect_), header_label_, false, true});
    for (size_t i = 0; i < candidate_labels_.size(); ++i) {
        items.push_back({ViewportRect(gsl::at(candidate_hit_rects_, i)), gsl::at(candidate_labels_, i),
                         i == selected_in_page_});
    }
    if (candidate_labels_.empty()) {
        items.push_back({ViewportRect(empty_rect_), empty_label_});
    }
    if (ComputeTotalPages(all_candidates_.size(), page_size_) > 1) {
        items.push_back({PrevPageButtonRect(), prev_page_label_,
                         false, true, current_page_ > 0, true});
        items.push_back({NextPageButtonRect(), next_page_label_,
                         false, true, current_page_ + 1 < ComputeTotalPages(all_candidates_.size(), page_size_), true});
    }
    return items;
}

bool CandidateUI::PaintPopupDirect2D(HDC hdc, const RECT& client_rect) const {
    if (!hdc || !d2d_dc_target_ || !dwrite_text_format_) return false;
    const ScopedSelectObject select_font(hdc, font_.Get());
    const auto& palette = appearance_.palette;
    const auto brush = CreateSolidBrush(d2d_dc_target_.Get(), ColorRefToD2DColor(palette.text_primary));
    if (!brush) return false;
    const auto as_d2d = [](const RECT& rect) noexcept {
        return D2D1::RectF(static_cast<float>(rect.left), static_cast<float>(rect.top),
                           static_cast<float>(rect.right), static_cast<float>(rect.bottom));
    };
    HRESULT end_hr = S_OK;
    {
        const ScopedD2DDcDraw draw_guard(d2d_dc_target_.Get(), hdc, client_rect, &end_hr);
        if (!draw_guard) return false;
        d2d_dc_target_->Clear(ColorRefToD2DColor(palette.window_bg));
        for (const PaintItem& item : BuildPaintItems()) {
            if (item.rect.bottom <= 0 || item.rect.top >= client_rect.bottom) continue;
            if (item.selected) {
                brush->SetColor(ColorRefToD2DColor(palette.selection_bg));
                d2d_dc_target_->FillRectangle(as_d2d(item.rect), brush.Get());
                brush->SetColor(ColorRefToD2DColor(palette.focus_color));
                d2d_dc_target_->DrawRectangle(as_d2d(item.rect), brush.Get(), static_cast<float>(ScaleByDpi(2)));
            }
            const COLORREF color = item.selected ? palette.selection_text : !item.enabled ? palette.text_secondary
                : item.accent ? palette.text_accent : palette.text_primary;
            brush->SetColor(ColorRefToD2DColor(color));
            const int padding = item.accent ? 0 : ScaleByDpi(preferences_.easy_reading ? 8 : 2);
            const int vertical_padding = item.accent ? 0 : ScaleByDpi(preferences_.easy_reading ? 8 : 1);
            int top = item.rect.top + vertical_padding;
            if (item.centered) {
                const int lines = gsl::narrow_cast<int>(std::count(item.text.begin(), item.text.end(), L'\n')) + 1;
                top += std::max(0, (gsl::narrow_cast<int>(item.rect.bottom - item.rect.top) - lines * line_height_) / 2);
            }
            std::wstring_view remaining = item.text;
            do {
                const size_t end = remaining.find(L'\n');
                const std::wstring_view line = remaining.substr(0, end);
                RECT line_rect{item.rect.left + padding, top, item.rect.right - padding, top + line_height_};
                if (item.centered) {
                    line_rect.left += std::max(0, (gsl::narrow_cast<int>(line_rect.right - line_rect.left) -
                        MeasureTextWidth(hdc, line)) / 2);
                }
                d2d_dc_target_->DrawTextW(line.data(), gsl::narrow_cast<UINT32>(line.size()),
                    dwrite_text_format_.Get(), as_d2d(line_rect), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
                if (end == std::wstring_view::npos) break;
                remaining.remove_prefix(end + 1);
                top += line_height_;
            } while (!remaining.empty());
        }
        brush->SetColor(ColorRefToD2DColor(palette.border_color));
        d2d_dc_target_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, static_cast<float>(client_rect.right) - 0.5f,
            static_cast<float>(client_rect.bottom) - 0.5f), brush.Get());
    }
    if (FAILED(end_hr)) {
        QIQI_LOG_WARN("CandidateUI::PaintPopupDirect2D EndDraw failed (hr=0x{:08x}); falling back to GDI rendering",
                      gsl::narrow_cast<unsigned int>(end_hr));
        d2d_dc_target_.Reset();
        return false;
    }
    return true;
}

void CandidateUI::PaintPopup(HDC hdc, const RECT& client_rect) const {
    const int width = client_rect.right - client_rect.left;
    const int height = client_rect.bottom - client_rect.top;
    if (width <= 0 || height <= 0) return;
    const ScopedMemoryDC buffer(::CreateCompatibleDC(hdc));
    if (!buffer) {
        QIQI_LOG_WARN("CandidateUI::CreateCompatibleDC failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    const ScopedGdiHandle<HBITMAP> bitmap(::CreateCompatibleBitmap(hdc, width, height));
    if (!bitmap) {
        QIQI_LOG_WARN("CandidateUI::CreateCompatibleBitmap failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    const ScopedSelectObject select_bitmap(buffer.Get(), bitmap.Get());
    if (!select_bitmap) {
        QIQI_LOG_WARN("CandidateUI::SelectObject(buffer) failed (error={}); painting without buffer", select_bitmap.Error());
        PaintPopupContent(hdc, client_rect);
        return;
    }
    // Both renderers finish the complete frame before touching the visible DC.
    PaintPopupContent(buffer.Get(), client_rect);
    if (!::BitBlt(hdc, client_rect.left, client_rect.top, width, height,
                  buffer.Get(), client_rect.left, client_rect.top, SRCCOPY)) {
        QIQI_LOG_WARN("CandidateUI::BitBlt failed (error={}); painting without buffer", ::GetLastError());
        PaintPopupContent(hdc, client_rect);
    }
}

void CandidateUI::PaintPopupContent(HDC hdc, const RECT& client_rect) const {
    if (PaintPopupDirect2D(hdc, client_rect)) return;
    const ScopedSaveDC save_dc(hdc);
    const auto& palette = appearance_.palette;
    const ScopedGdiHandle<HBRUSH> background(::CreateSolidBrush(palette.window_bg));
    const ScopedGdiHandle<HBRUSH> selection(::CreateSolidBrush(palette.selection_bg));
    const ScopedGdiHandle<HBRUSH> outline(::CreateSolidBrush(palette.focus_color));
    const ScopedGdiHandle<HBRUSH> border(::CreateSolidBrush(palette.border_color));
    ::FillRect(hdc, &client_rect, background.Get());
    const ScopedSelectObject select_font(hdc, font_.Get());
    ::SetBkMode(hdc, TRANSPARENT);
    for (const PaintItem& item : BuildPaintItems()) {
        if (item.rect.bottom <= 0 || item.rect.top >= client_rect.bottom) continue;
        if (item.selected) {
            ::FillRect(hdc, &item.rect, selection.Get());
            RECT frame = item.rect;
            for (int i = 0; i < ScaleByDpi(2); ++i) {
                ::FrameRect(hdc, &frame, outline.Get());
                ::InflateRect(&frame, -1, -1);
            }
        }
        ::SetTextColor(hdc, item.selected ? palette.selection_text :
            !item.enabled ? palette.text_secondary : item.accent ? palette.text_accent : palette.text_primary);
        const int padding = item.accent ? 0 : ScaleByDpi(preferences_.easy_reading ? 8 : 2);
        const int vertical_padding = item.accent ? 0 : ScaleByDpi(preferences_.easy_reading ? 8 : 1);
        int top = item.rect.top + vertical_padding;
        if (item.centered) {
            const int lines = gsl::narrow_cast<int>(std::count(item.text.begin(), item.text.end(), L'\n')) + 1;
            top += std::max(0, (gsl::narrow_cast<int>(item.rect.bottom - item.rect.top) - lines * line_height_) / 2);
        }
        std::wstring_view remaining = item.text;
        do {
            const size_t end = remaining.find(L'\n');
            const std::wstring_view line = remaining.substr(0, end);
            RECT line_rect{item.rect.left + padding, top, item.rect.right - padding, top + line_height_};
            ::DrawTextW(hdc, line.data(), gsl::narrow_cast<int>(line.size()), &line_rect,
                        DT_SINGLELINE | DT_NOPREFIX | (item.centered ? DT_CENTER : DT_LEFT));
            if (end == std::wstring_view::npos) break;
            remaining.remove_prefix(end + 1);
            top += line_height_;
        } while (!remaining.empty());
    }
    ::FrameRect(hdc, &client_rect, border.Get());
}

LRESULT CALLBACK CandidateUI::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = BindOrGetWindowUserData<CandidateUI>(hwnd, msg, lparam);
    if (!self || msg == WM_NCCREATE) {
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    const auto keep_alive = self->RetainForWindowOperation();

    switch (msg) {
        case WM_GETOBJECT:
            if (lparam == UiaRootObjectId) {
                if (self->destroying_window_) {
                    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
                }
                const LRESULT result =
                    ::UiaReturnRawElementProvider(
                        hwnd, wparam, lparam, self);
                if (result != 0) {
                    self->accessibility_provider_returned_ = true;
                }
                return result;
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_NCDESTROY:
            if (self->hwnd_.Get() == hwnd) {
                if (self->accessibility_open_announced_) {
                    self->accessibility_open_announced_ = false;
                    std::ignore = self->RaiseAccessibilityEvent(UIA_MenuClosedEventId);
                }
                std::ignore = self->hwnd_.Detach();
                self->is_shown_ = FALSE;
                self->popup_owner_ = nullptr;
                self->NotifyPopupWindowState(hwnd, true);
            }
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_WINDOWPOSCHANGED: {
            const LRESULT result = ::DefWindowProcW(hwnd, msg, wparam, lparam);
            if (self->hwnd_.Get() == hwnd) self->NotifyPopupWindowState(hwnd);
            return result;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
            self->OnLeftButtonDown(PointFromLParam(lparam));
            return 0;
        case WM_MOUSEWHEEL: {
            self->wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
            const int steps = self->wheel_delta_ / WHEEL_DELTA;
            self->wheel_delta_ %= WHEEL_DELTA;
            self->ScrollContent(self->scroll_offset_ - steps * self->line_height_ * 3);
            return 0;
        }
        case WM_VSCROLL: {
            int position = self->scroll_offset_;
            switch (LOWORD(wparam)) {
                case SB_TOP: position = 0; break;
                case SB_BOTTOM: position = self->content_height_; break;
                case SB_LINEUP: position -= self->line_height_; break;
                case SB_LINEDOWN: position += self->line_height_; break;
                case SB_PAGEUP: position -= self->popup_height_; break;
                case SB_PAGEDOWN: position += self->popup_height_; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    SCROLLINFO info{};
                    info.cbSize = sizeof(info);
                    info.fMask = SIF_TRACKPOS;
                    if (::GetScrollInfo(hwnd, SB_VERT, &info)) position = info.nTrackPos;
                    break;
                }
                default: break;
            }
            self->ScrollContent(position);
            return 0;
        }
        case WM_DPICHANGED:
            if (!self->text_rect_ && lparam != 0) {
                const auto* suggested = reinterpret_cast<const RECT*>(lparam);
                self->popup_pos_.x = suggested->left;
                self->popup_pos_.y = suggested->top;
            }
            self->RefreshPopupLayout(HIWORD(wparam));
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            self->appearance_ = ResolveDisplayAppearance(self->preferences_, SystemThemeSource::App);
            self->RefreshPopupLayout();
            ApplyDisplayThemeToWindow(hwnd, self->appearance_);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            return PaintWindowClient(hwnd, [self](HDC hdc, const RECT& rc) { self->PaintPopup(hdc, rc); });
        case WM_PRINTCLIENT: {
            RECT rect{};
            if (wparam && ::GetClientRect(hwnd, &rect)) self->PaintPopup(reinterpret_cast<HDC>(wparam), rect);
            return 0;
        }
        default:
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace qiqi
