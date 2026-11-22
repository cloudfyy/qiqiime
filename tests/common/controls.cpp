// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/controltest.h"

namespace qiqi::test {

void RunD2DControlsTests() {
    controltest::RunThemeTests();
    controltest::RunPaintTests();
    controltest::RunInfoDialogTests();
}

} // namespace qiqi::test
