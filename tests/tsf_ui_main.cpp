#include "common.h"
#include <iostream>

int main() {
    qiqi::test::RunCandidateUITests();
    qiqi::test::RunTsfComTests();
    qiqi::test::RunTsfServiceTests();
    std::cout << "[PASS] All QiQi TSF & UI unit tests succeeded.\n";
    return 0;
}
