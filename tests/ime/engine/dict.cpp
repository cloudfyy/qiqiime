// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/engine/dict/test.h"

namespace qiqi::test {

void RunEngineDictTests() {
    using namespace dict_cases;
    TestPhraseFrequencySaveFailures();
    TestSyncFailureAtomicity();
    TestBinaryDictBufferValidation();
    TestAddEntryAndLookupEdgeCases();
    TestExternalDictFile();
    TestCrossProcessDictAndPhraseFreqSync();
    TestCustomPrimaryDictionaryAndFallback();
}

} // namespace qiqi::test
