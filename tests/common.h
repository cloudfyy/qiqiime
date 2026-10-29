#pragma once
#include <cstdlib>
#include <iostream>

#define QIQI_TEST_CHECK(expr)                                                                   \
    do {                                                                                        \
        if (!(expr)) {                                                                          \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << ": " << #expr << "\n";     \
            std::abort();                                                                       \
        }                                                                                       \
    } while (false)

namespace qiqi::test {

// Common & Engine test suites
void RunCommonTests();
void RunEngineDictTests();
void RunEngineInputTests();

// UI & TSF test suites
void RunCandidateUITests();
void RunTsfComTests();
void RunTsfServiceTests();

} // namespace qiqi::test
