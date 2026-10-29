#pragma once
#include <windows.h>
#include <msctf.h>
#include <atomic>

namespace qiqi {

// Global DLL instance and active COM object counter for DllCanUnloadNow
extern HINSTANCE g_dll_instance;
extern std::atomic<long> g_com_object_count;

// Shared Win32 window class names (unregistered on dynamic DLL unload)
inline constexpr const wchar_t* kCandidateWndClass = L"QiQiIMECandidatePopupClass";
inline constexpr const wchar_t* kStatusBarWndClass = L"QiQiIMEStatusBarPopupClass";
inline constexpr const wchar_t* kStatusBarTooltipWndClass = L"QiQiIMEStatusBarTooltipClass";
inline constexpr const wchar_t* kSettingsWndClass = L"QiQiIMESettingsDialogClass";
inline constexpr const wchar_t* kAboutWndClass = L"QiQiIMEAboutDialogClass";
inline constexpr const wchar_t* kGitHubRepoUrl = L"https://github.com/cloudfyy/qiqiime";

// {8E7B9D31-5A42-4C81-9F11-202610290001}
inline constexpr CLSID kClsidQiQiTextService = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x01}
};

// {8E7B9D31-5A42-4C81-9F11-202610290002}
inline constexpr GUID kGuidProfileWubi86 = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x02}
};

// {8E7B9D31-5A42-4C81-9F11-202610290003}
inline constexpr GUID kGuidCandidateUIElement = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x03}
};

// {8E7B9D31-5A42-4C81-9F11-202610290004}
inline constexpr GUID kGuidPreservedKeyToggleMode = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x04}
};

// {8E7B9D31-5A42-4C81-9F11-202610290005}
inline constexpr GUID kGuidPreservedKeyToggleFullWidth = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x05}
};

// {8E7B9D31-5A42-4C81-9F11-202610290006}
inline constexpr GUID kGuidPreservedKeyTogglePunctuation = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x06}
};

// {8E7B9D31-5A42-4C81-9F11-202610290007}
inline constexpr GUID kGuidDisplayAttributeInput = {
    0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x07}
};

inline constexpr LANGID kChineseSimplifiedLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
inline constexpr const wchar_t* kTextServiceDesc = L"QiQi 五笔输入法 (TSF)";
inline constexpr const wchar_t* kProfileDesc = L"QiQi 五笔 86";

} // namespace qiqi
