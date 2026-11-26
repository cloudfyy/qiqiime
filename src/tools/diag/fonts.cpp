// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/diag/internal.h"

#include "common/fileutil.h"

#include <gsl/gsl>

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <vector>

namespace qiqi::diag::detail {
namespace {

struct FontState {
    std::vector<std::wstring> faces;
    HRESULT collection_failure = S_OK;
};

int CALLBACK EnumChineseFontsCallback(const LOGFONTW* font, const TEXTMETRICW*, DWORD,
                                      LPARAM parameter) noexcept {
    auto* state = std::bit_cast<FontState*>(parameter);
    try {
        if (!font) {
            return 1;
        }
        const std::wstring_view face = Win32StringView(font->lfFaceName);
        if (face.empty() || face.front() == L'@') {
            return 1;
        }
        const std::wstring name(face);
        if (std::ranges::none_of(
                state->faces, [&](const auto& face) { return _wcsicmp(face.c_str(), name.c_str()) == 0; })) {
            state->faces.push_back(name);
        }
        return 1;
    } catch (const std::bad_alloc&) {
        state->collection_failure = E_OUTOFMEMORY;
        return 0;
    } catch (const std::length_error&) {
        state->collection_failure = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        return 0;
    }
}

Json LogFontJson(const LOGFONTW& font, UINT dpi) {
    const int64_t absolute_height = font.lfHeight < 0 ? -gsl::narrow_cast<int64_t>(font.lfHeight)
                                                      : gsl::narrow_cast<int64_t>(font.lfHeight);
    return Json{
        {"face", WideToUtf8(Win32StringView(font.lfFaceName))},
        {"height", font.lfHeight},
        {"point_size", font.lfHeight == 0 ? 0 : absolute_height * 72 / dpi},
        {"weight", font.lfWeight},
        {"italic", font.lfItalic != FALSE},
        {"charset", gsl::narrow_cast<uint32_t>(font.lfCharSet)},
    };
}

} // namespace

void CollectFontInformation(Json& stage) {
    Json fonts = Json::object();
    const UINT dpi = std::max(96U, ::GetDpiForSystem());
    NONCLIENTMETRICSW metrics{.cbSize = sizeof(NONCLIENTMETRICSW)};
    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0) != FALSE) {
        fonts["system_message_font"] = LogFontJson(metrics.lfMessageFont, dpi);
        AddCheck(stage, CheckResult("system_font_metrics", S_OK));
    } else {
        AddCheck(stage, CheckResult("system_font_metrics", HRESULT_FROM_WIN32(::GetLastError()), "warning"));
    }

    const HFONT gui_font = std::bit_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    if (gui_font) {
        LOGFONTW gui_log_font{};
        if (::GetObjectW(gui_font, sizeof(gui_log_font), &gui_log_font) != 0) {
            fonts["default_gui_font"] = LogFontJson(gui_log_font, dpi);
        }
    }

    ScopedMemoryDC display_dc(::CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr));
    if (!display_dc) {
        AddCheck(stage, CheckResult("chinese_font_enumeration", HRESULT_FROM_WIN32(::GetLastError())));
        stage["fonts"] = std::move(fonts);
        return;
    }
    FontState font_state;
    LOGFONTW query{.lfCharSet = GB2312_CHARSET};
    ::EnumFontFamiliesExW(display_dc.Get(), &query, EnumChineseFontsCallback,
                          std::bit_cast<LPARAM>(&font_state), 0);
    if (FAILED(font_state.collection_failure)) {
        AddCheck(stage, CheckResult("chinese_font_enumeration", font_state.collection_failure));
    } else {
        AddCheck(stage, CheckResult("chinese_font_enumeration", S_OK));
    }
    std::ranges::sort(font_state.faces, [](const auto& left, const auto& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    Json names = Json::array();
    for (const auto& face : font_state.faces) {
        names.push_back(WideToUtf8(face));
    }
    fonts["chinese_font_count"] = names.size();
    fonts["chinese_font_faces"] = std::move(names);
    stage["fonts"] = std::move(fonts);
}

} // namespace qiqi::diag::detail
