// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace qiqi {

enum class DictionaryLineError : uint8_t {
    InvalidCode,
    MissingWord,
    InvalidCharacter,
    InvalidWeight,
    UnterminatedHeader
};

struct DictionaryLineIssue {
    size_t line = 0;
    DictionaryLineError error = DictionaryLineError::InvalidCode;
};

struct DictionaryTextDiagnostics {
    size_t valid_lines = 0;
    size_t invalid_lines = 0;
    std::vector<DictionaryLineIssue> issues;
};

} // namespace qiqi
