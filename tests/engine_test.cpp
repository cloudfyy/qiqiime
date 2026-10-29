#include "engine/wubi_engine.h"
#include "common/logger.h"
#include "common/version.h"
#include <windows.h>
#include <cassert>
#include <fstream>
#include <iostream>

using namespace qiqi;

namespace {

void TestFirstLevelShortCodes() {
    WubiEngine engine;
    // 'a' -> 工, Space commits
    auto r1 = engine.ProcessKey('A');
    assert(r1.action == KeyActionType::UpdateState);
    assert(r1.composition_text == L"a");
    assert(!r1.page_candidates.empty() && r1.page_candidates[0] == L"工");

    auto r2 = engine.ProcessKey(VK_SPACE);
    assert(r2.action == KeyActionType::Commit);
    assert(r2.committed_text == L"工");
    assert(!engine.IsComposing());
}

void TestFourCodeUniqueAutoCommit() {
    WubiEngine engine;
    // "wqvb" -> "你好" (unique 4-code exact match -> auto commits on 4th key!)
    assert(engine.ProcessKey('W').action == KeyActionType::UpdateState);
    assert(engine.ProcessKey('Q').action == KeyActionType::UpdateState);
    assert(engine.ProcessKey('V').action == KeyActionType::UpdateState);
    auto r4 = engine.ProcessKey('B');
    assert(r4.action == KeyActionType::Commit);
    assert(r4.committed_text == L"你好");
    assert(!engine.IsComposing());
}

void TestFifthKeyPushAutoCommit() {
    WubiEngine engine;
    // Add two words sharing 4-code "abcd" so 4th key doesn't auto-commit
    engine.AddEntry(L"abcd", L"甲");
    engine.AddEntry(L"abcd", L"乙");

    engine.ProcessKey('A');
    engine.ProcessKey('B');
    engine.ProcessKey('C');
    auto r4 = engine.ProcessKey('D');
    assert(r4.action == KeyActionType::UpdateState);
    assert(r4.page_candidates.size() >= 2);

    // Typing 5th key 'Q' ("我") should auto-commit "甲" and start composing "q"
    auto r5 = engine.ProcessKey('Q');
    assert(r5.action == KeyActionType::Commit);
    assert(r5.committed_text == L"甲");
    assert(r5.composition_text == L"q");
    assert(engine.IsComposing());
}

void TestWildcardAndBackspaceAndCancel() {
    WubiEngine engine;
    // "wqz" wildcard query should match "你" (wqi / wqiy)
    engine.ProcessKey('W');
    engine.ProcessKey('Q');
    auto rz = engine.ProcessKey('Z');
    assert(rz.action == KeyActionType::UpdateState);
    assert(!rz.page_candidates.empty());

    // Backspace removes 'z'
    auto rb = engine.ProcessKey(VK_BACK);
    assert(rb.action == KeyActionType::UpdateState);
    assert(rb.composition_text == L"wq");

    // Escape cancels composition
    auto resc = engine.ProcessKey(VK_ESCAPE);
    assert(resc.action == KeyActionType::Cancel);
    assert(!engine.IsComposing());
}

void TestExternalDictFile() {
    WubiEngine engine;
    // Load data/wubi86.txt if available
    if (std::filesystem::exists("wubi86.txt")) {
        assert(engine.LoadDictionaryFile("wubi86.txt"));
    }
}

void TestSpdlogLogger() {
    const auto temp_log = std::filesystem::temp_directory_path() / L"qiqiime_unit_test.log";
    std::error_code ec;
    std::filesystem::remove(temp_log, ec);

    log::InitLogger(temp_log, spdlog::level::debug);
    QIQI_LOG_INFO("Unit test log entry: {}", log::WideToUtf8(L"五笔日志测试"));
    log::Flush();

    assert(std::filesystem::exists(temp_log));
    {
        std::ifstream ifs(temp_log);
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        assert(content.find("Unit test log entry:") != std::string::npos);
    }
    log::InitLogger();
    std::filesystem::remove(temp_log, ec);
}

void TestVersionMechanism() {
    static_assert(version::kCurrentVersion >= version::SemanticVersion{0, 1, 0});
    static_assert(version::SemanticVersion{1, 0, 0} > version::kCurrentVersion);
    assert(!version::kVersion.empty());
    assert(!version::kFullVersion.empty());
    assert(!version::kGitCommit.empty());
    assert(version::kFullVersion.starts_with(version::kVersion));
}

} // namespace

int main() {
    TestVersionMechanism();
    TestSpdlogLogger();
    TestFirstLevelShortCodes();
    TestFourCodeUniqueAutoCommit();
    TestFifthKeyPushAutoCommit();
    TestWildcardAndBackspaceAndCancel();
    TestExternalDictFile();
    std::cout << "[PASS] All QiQi Wubi 86 engine tests succeeded.\n";
    return 0;
}
