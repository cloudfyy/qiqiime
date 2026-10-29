#include "common.h"
#include <iostream>

int main() {
    qiqi::test::RunCommonTests();
    qiqi::test::RunEngineDictTests();
    qiqi::test::RunEngineInputTests();
    std::cout << "[PASS] All QiQi Wubi 86 engine & common tests succeeded.\n";
    return 0;
}
