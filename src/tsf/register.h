#pragma once
#include <windows.h>

namespace qiqi {

bool IsRunningUnderWow64() noexcept;
HRESULT RegisterServer(HINSTANCE instance);
HRESULT UnregisterServer();

} // namespace qiqi
