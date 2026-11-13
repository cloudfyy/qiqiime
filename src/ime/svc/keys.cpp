// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ime/svc/service.h"

namespace qiqi {

STDMETHODIMP QiQiTextService::OnSetFocus(BOOL fForeground) {
    HandleFocusChanged(fForeground != FALSE, FocusNotificationSource::KeyEvent);
    if (fForeground && !password_context_)  ObserveFocusedPasswordContext();
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyDown(
    ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) noexcept {
    if (!pfEaten) return E_INVALIDARG;
    const KeyEvent key = ReadKeyEvent(wParam, lParam);
    HandleTestKeyDown(pic, key, gsl::not_null<BOOL*>(pfEaten));
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyDown(
    ITfContext* pic, WPARAM wParam, LPARAM lParam, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    // Capture before host calls can dispatch a newer keyboard message.
    const KeyEvent key = ReadKeyEvent(wParam, lParam);
    HandleKeyDown(pic, key, gsl::not_null<BOOL*>(pfEaten));
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnTestKeyUp(
    ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) noexcept {
    if (!pfEaten) return E_INVALIDARG;
    HandleTestKeyUp(pic, gsl::narrow_cast<uint32_t>(wParam), gsl::not_null<BOOL*>(pfEaten));
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnKeyUp(
    ITfContext* pic, WPARAM wParam, LPARAM /*lParam*/, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    HandleKeyUp(pic, gsl::narrow_cast<uint32_t>(wParam), gsl::not_null<BOOL*>(pfEaten));
    return S_OK;
}

STDMETHODIMP QiQiTextService::OnPreservedKey(ITfContext* pic, REFGUID rguid, BOOL* pfEaten) {
    if (!pfEaten) return E_INVALIDARG;
    HandlePreservedKey(pic, rguid, gsl::not_null<BOOL*>(pfEaten));
    return S_OK;
}

} // namespace qiqi
