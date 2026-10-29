#pragma once
#include <windows.h>

namespace qiqi {

bool IsRunningUnderWow64() noexcept;
bool IsNative64BitClsidRegistered() noexcept;
HRESULT RegisterServer(HINSTANCE instance);
HRESULT UnregisterServer();

} // namespace qiqi
