// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

MIDL_INTERFACE("8E7B9D31-5A42-4C81-9F11-20261029000F")
IQiQiConfigBroker : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Read(
        ULONGLONG known_revision,
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Save(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Reset(
        ULONGLONG expected_revision,
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE PublishSyncDirectory(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
};

MIDL_INTERFACE("8E7B9D31-5A42-4C81-9F11-202610290010")
AsyncIQiQiConfigBroker : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Begin_Read(
        ULONGLONG known_revision,
        ULONG* snapshot_size) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Finish_Read(
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Begin_Save(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Finish_Save(
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Begin_Reset(
        ULONGLONG expected_revision,
        ULONG* snapshot_size) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Finish_Reset(
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Begin_PublishSyncDirectory(
        ULONG request_size,
        byte* request,
        ULONG* snapshot_size) noexcept = 0;
    virtual HRESULT STDMETHODCALLTYPE Finish_PublishSyncDirectory(
        ULONG* snapshot_size,
        byte** snapshot) noexcept = 0;
};
