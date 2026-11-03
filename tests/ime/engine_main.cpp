#include "common/test_support.h"
#include <iostream>

int main() {
    qiqi::test::RunEngineDictTests();
    qiqi::test::RunEngineInputTests();
    qiqi::test::RunEngineModesPunctTests();
    std::cout << "[PASS] All QiQi Wubi 86 engine tests succeeded.\n";
    return 0;
}
