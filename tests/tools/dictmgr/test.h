// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "common/config.h"
#include "common/toolbar.h"
#include "common/controls.h"
#include "common/fileutil.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/RAII.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/uiutil.h"
#include "ime/engine/engine.h"
#include "tools/dictmgr/model.h"
#include "tools/dictmgr/editor.h"
#include "tools/dictmgr/window.h"
#include "tools/dictmgr/internal.h"

#include <commctrl.h>
#include <sddl.h>
#include <winver.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <span>

namespace qiqi::dict_manager::tests {

using namespace qiqi;
using namespace qiqi::dict_manager;
using enum qiqi::dict_manager::ControlId;

namespace layout = dict_manager_layout;

class ConfigRegistryFixture final {
public:
    ConfigRegistryFixture();
    ~ConfigRegistryFixture() noexcept;
    ConfigRegistryFixture(const ConfigRegistryFixture&) = delete;
    ConfigRegistryFixture& operator=(const ConfigRegistryFixture&) = delete;
    ConfigRegistryFixture(ConfigRegistryFixture&&) = delete;
    ConfigRegistryFixture& operator=(ConfigRegistryFixture&&) = delete;

    const std::wstring& Subkey() const noexcept { return subkey_; }
    HKEY Get() const noexcept { return key_.Get(); }
private:
    std::wstring subkey_;
    const wchar_t* previous_;
    ScopedRegKey key_;
};

HWND Child(const DictManagerWindow& window, ControlId id) noexcept;
bool SaveFixtureConfig(const QiQiConfig& config, std::wstring_view expected_subkey);
void TestConfigRegistryIsolation();
RECT ToolbarItemRect(const DictManagerWindow& window, ControlId id);
void InvokeCommand(const DictManagerWindow& window, ControlId id, bool toolbar = false);
std::wstring MenuText(HMENU menu, ControlId id);
using qiqi::test::ControlText;
void DrainQuitMessages() noexcept;
void ClickRowEdit(const DictManagerWindow& window, int row);
void CheckLayout(const DictManagerWindow& window, UINT dpi);
void CheckCommandAppearance(const DictManagerWindow& window, UINT dpi, bool dark, bool contrast);
void TestVersionResource();
void TestDictionarySourceSelection();
void TestModelAndCrossProcessSync();
void TestBackupRestoreResults();
void TestSmartSearch();
void TestGuiWindow();
void TestEditorState();
void TestInlineEditing();
void ExerciseGuiCommands(const DictManagerWindow& window);

} // namespace qiqi::dict_manager::tests
