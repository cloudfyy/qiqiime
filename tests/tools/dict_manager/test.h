// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/test_support.h"
#include "common/config.h"
#include "common/command_ui.h"
#include "common/d2d_controls.h"
#include "common/file_util.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/theme.h"
#include "common/version.h"
#include "common/win32_ui_util.h"
#include "ime/engine/engine.h"
#include "tools/dict_manager/model.h"
#include "tools/dict_manager/editor.h"
#include "tools/dict_manager/window.h"
#include "tools/dict_manager/internal.h"

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

HWND Child(const DictManagerWindow& window, ControlId id) noexcept;
RECT ToolbarItemRect(const DictManagerWindow& window, ControlId id);
void InvokeCommand(const DictManagerWindow& window, ControlId id, bool toolbar = false);
std::wstring MenuText(HMENU menu, ControlId id);
std::wstring ControlText(HWND hwnd);
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
