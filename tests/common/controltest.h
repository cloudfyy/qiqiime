// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/support.h"
#include "common/dialog.h"
#include "common/dlgutil.h"
#include "common/i18n.h"
#include "common/dpi.h"
#include "common/theme.h"
#include "common/uiutil.h"
#include "common/window.h"
#include "common/controls.h"

#include <commctrl.h>
#include <cmath>
#include <memory>
#include <type_traits>

namespace qiqi::test::controltest {

void RunThemeTests();
void RunPaintTests();
void RunInfoDialogTests();

void TestFileDialogs(HWND owner);
void TestListViewColumns(HWND owner, HWND label);
void TestComboItemReplacement(HWND combo, HWND label);
void TestComboTextReadBounds(
    HWND combo, HDC dc, const FluentD2DThemeContext& d2d);

} // namespace qiqi::test::controltest
