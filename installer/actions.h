// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>
#include <msi.h>

extern "C" UINT __stdcall PromptRemoveUserData(MSIHANDLE install) noexcept;
