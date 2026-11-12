// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/logger.h"
#include "ime/engine/engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace qiqi::test::dict_cases {

template <typename T>
void WriteBinaryValue(std::vector<uint8_t>& buffer, size_t offset, const T& value) {
    const std::span<uint8_t> bytes(buffer);
    std::memcpy(bytes.subspan(offset, sizeof(value)).data(), &value, sizeof(value));
}

template <typename T>
void ReadBinaryValue(const std::vector<uint8_t>& buffer, size_t offset, T& value) {
    const std::span<const uint8_t> bytes(buffer);
    std::memcpy(&value, bytes.subspan(offset, sizeof(value)).data(), sizeof(value));
}

void TestPhraseFrequencySaveFailures();
void TestSyncFailureAtomicity();
void TestBinaryDictBufferValidation();
void TestAddEntryAndLookupEdgeCases();
void TestExternalDictFile();
void TestCrossProcessDictAndPhraseFreqSync();
void TestCustomPrimaryDictionaryAndFallback();
bool WriteInvalidIndexQdictCache(
    const std::filesystem::path& source_path,
    const std::filesystem::path& cache_path);

} // namespace qiqi::test::dict_cases
