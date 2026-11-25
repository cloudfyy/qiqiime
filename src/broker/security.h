// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace qiqi::broker {

enum class CallerAccess {
    Read,
    Mutation,
};

struct CallerIdentity {
    bool user_match = false;
    bool session_match = false;
    bool app_container = false;
};

constexpr bool IsCallerIdentityAllowed(
    CallerAccess access,
    const CallerIdentity& identity) noexcept {
    const bool supported_access =
        access == CallerAccess::Read ||
        access == CallerAccess::Mutation;
    return supported_access &&
        identity.user_match &&
        (identity.session_match ||
         identity.app_container);
}

} // namespace qiqi::broker
