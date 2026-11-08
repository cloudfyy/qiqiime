#pragma once

#include "common/i18n.h"
#include "common/logger.h"
#include "ime/engine/binary_dict.h"

#include <format>

namespace qiqi::dictionary_ui {

inline std::wstring ValidationDetails(const PrimaryDictValidationResult& validation, UiLanguage language) {
    const bool english = ResolveUiLanguage(language) == UiLanguage::EnUs;
    const auto& diagnostics = validation.diagnostics;
    std::wstring text = english
        ? std::format(L"Valid rows: {}. Invalid rows: {}.\n", diagnostics.valid_lines, diagnostics.invalid_lines)
        : std::format(L"有效行：{}。无效行：{}。\n", diagnostics.valid_lines, diagnostics.invalid_lines);
    for (const auto& issue : diagnostics.issues) {
        const wchar_t* reason = nullptr;
        switch (issue.error) {
            case DictionaryLineError::InvalidCode:
                reason = english ? L"Code must contain 1-4 ASCII letters" : L"编码必须为 1～4 个英文字母";
                break;
            case DictionaryLineError::MissingWord:
                reason = english ? L"Missing word" : L"缺少词语";
                break;
            case DictionaryLineError::InvalidCharacter:
                reason = english ? L"Invalid control character" : L"包含无效控制字符";
                break;
            case DictionaryLineError::InvalidWeight:
                reason = english ? L"Invalid weight or extra columns" : L"权重无效或存在多余列";
                break;
            case DictionaryLineError::UnterminatedHeader:
                reason = english ? L"YAML header has no closing ..." : L"YAML 头部缺少结束标记 ...";
                break;
        }
        text += english ? std::format(L"\nLine {}: {}", issue.line, reason)
                        : std::format(L"\n第 {} 行：{}", issue.line, reason);
    }
    if (diagnostics.invalid_lines > diagnostics.issues.size()) {
        text += english ? L"\nOnly the first 8 errors are shown." : L"\n仅显示前 8 个错误。";
    }
    text += english
        ? L"\n\nLoad only valid entries? Invalid rows will be skipped. Cancel keeps the current selection."
        : L"\n\n是否仅加载有效词条？无效行将跳过。取消则保留当前选择。";
    return text;
}

inline bool ConfirmPartialDictionary(
    HWND owner, UiLanguage language, const PrimaryDictValidationResult& validation,
    gsl::not_null<decltype(&::MessageBoxW)> show = &::MessageBoxW) {
    if (!validation) {
        return false;
    }
    if (validation.diagnostics.invalid_lines == 0) {
        return true;
    }
    const std::wstring details = ValidationDetails(validation, language);
    const int button = show(owner, details.c_str(), GetUiStrings(language).dlg_title,
        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    if (button == 0) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_CAT_ERROR(log::Keyword::UI,
            "Dictionary confirmation MessageBoxW failed (error={})", error);
        return false;
    }
    QIQI_LOG_CAT_INFO(log::Keyword::Dictionary,
        "Partial dictionary confirmation (accepted={}, invalid_lines={})",
        button == IDOK, validation.diagnostics.invalid_lines);
    return button == IDOK;
}

} // namespace qiqi::dictionary_ui
