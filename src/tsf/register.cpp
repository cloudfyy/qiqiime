#include "tsf/register.h"
#include "common/guids.h"
#include "common/logger.h"
#include "common/version.h"

#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <array>
#include <string>

namespace qiqi {

using Microsoft::WRL::ComPtr;

namespace {

struct ScopedRegKey {
    HKEY key = nullptr;
    ScopedRegKey() noexcept = default;
    ~ScopedRegKey() noexcept {
        if (key) {
            ::RegCloseKey(key);
        }
    }
    ScopedRegKey(const ScopedRegKey&) = delete;
    ScopedRegKey& operator=(const ScopedRegKey&) = delete;
    ScopedRegKey(ScopedRegKey&&) = delete;
    ScopedRegKey& operator=(ScopedRegKey&&) = delete;
};

std::wstring GuidToString(REFGUID guid) {
    std::array<wchar_t, 64> buf{};
    ::StringFromGUID2(guid, buf.data(), static_cast<int>(buf.size()));
    return buf.data();
}

bool RegisterComClsid(HINSTANCE instance) {
    std::array<wchar_t, MAX_PATH> module_path{};
    if (::GetModuleFileNameW(instance, module_path.data(), static_cast<DWORD>(module_path.size())) == 0) {
        return false;
    }

    const std::wstring clsid_key = L"CLSID\\" + GuidToString(kClsidQiQiTextService);
    const std::wstring inproc_key = clsid_key + L"\\InProcServer32";

    {
        ScopedRegKey hkey;
        if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey.key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        ::RegSetValueExW(hkey.key, nullptr, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(kTextServiceDesc),
                         static_cast<DWORD>((wcslen(kTextServiceDesc) + 1) * sizeof(wchar_t)));
    }

    {
        ScopedRegKey hkey;
        if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, inproc_key.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey.key, nullptr) != ERROR_SUCCESS) {
            return false;
        }
        ::RegSetValueExW(hkey.key, nullptr, 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(module_path.data()),
                         static_cast<DWORD>((wcslen(module_path.data()) + 1) * sizeof(wchar_t)));
        constexpr const wchar_t* kModel = L"Apartment";
        ::RegSetValueExW(hkey.key, L"ThreadingModel", 0, REG_SZ,
                         reinterpret_cast<const BYTE*>(kModel),
                         static_cast<DWORD>((wcslen(kModel) + 1) * sizeof(wchar_t)));
    }
    return true;
}

void UnregisterComClsid() {
    const std::wstring clsid_key = L"CLSID\\" + GuidToString(kClsidQiQiTextService);
    const std::wstring inproc_key = clsid_key + L"\\InProcServer32";
    ::RegDeleteKeyW(HKEY_CLASSES_ROOT, inproc_key.c_str());
    ::RegDeleteKeyW(HKEY_CLASSES_ROOT, clsid_key.c_str());
}

// Categories declared for modern Windows, UWP/AppContainer, and fullscreen UILess games
const std::array<GUID, 6> kSupportedCategories = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT
};

} // namespace

bool IsNative64BitClsidRegistered() noexcept {
    std::array<wchar_t, 64> guid_buf{};
    if (::StringFromGUID2(kClsidQiQiTextService, guid_buf.data(), static_cast<int>(guid_buf.size())) <= 0) {
        return false;
    }
    std::array<wchar_t, 128> inproc_key{};
    wcscpy_s(inproc_key.data(), inproc_key.size(), L"CLSID\\");
    wcscat_s(inproc_key.data(), inproc_key.size(), guid_buf.data());
    wcscat_s(inproc_key.data(), inproc_key.size(), L"\\InProcServer32");

    ScopedRegKey hkey;
    return ::RegOpenKeyExW(HKEY_CLASSES_ROOT, inproc_key.data(), 0,
                           KEY_READ | KEY_WOW64_64KEY, &hkey.key) == ERROR_SUCCESS;
}

bool IsRunningUnderWow64() noexcept {
    BOOL is_wow64 = FALSE;
    if (::IsWow64Process(::GetCurrentProcess(), &is_wow64)) {
        return is_wow64 != FALSE;
    }
    return false;
}

HRESULT RegisterServer(HINSTANCE instance) {
    QIQI_LOG_INFO("RegisterServer starting (version={}, arch={}, wow64={})",
                  version::kFullVersion, version::kArch, IsRunningUnderWow64());
    if (!RegisterComClsid(instance)) {
        QIQI_LOG_ERROR("RegisterComClsid failed (administrator privileges required)");
        log::Flush();
        return SELFREG_E_CLASS;
    }

    // On 64-bit Windows, when registering the 32-bit (WOW64) DLL after the 64-bit DLL is already
    // registered, only the 32-bit HKCR\WOW6432Node\CLSID\InProcServer32 entry is needed; skip
    // overwriting the shared 64-bit TSF Language Profile icon/module path.
    if (IsRunningUnderWow64() && IsNative64BitClsidRegistered()) {
        QIQI_LOG_INFO("RegisterServer (WOW64): 32-bit COM CLSID registered; native 64-bit TSF profile already active");
        log::Flush();
        return S_OK;
    }

    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_uninit = SUCCEEDED(co_hr);

    HRESULT hr = E_FAIL;
    do {
        ComPtr<ITfInputProcessorProfiles> profiles;
        hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&profiles));
        if (FAILED(hr) || !profiles) {
            break;
        }

        hr = profiles->Register(kClsidQiQiTextService);
        if (FAILED(hr)) {
            break;
        }

        std::array<wchar_t, MAX_PATH> module_path{};
        ::GetModuleFileNameW(instance, module_path.data(), static_cast<DWORD>(module_path.size()));

        hr = profiles->AddLanguageProfile(
            kClsidQiQiTextService,
            kChineseSimplifiedLangId,
            kGuidProfileWubi86,
            kProfileDesc,
            static_cast<ULONG>(wcslen(kProfileDesc)),
            module_path.data(),
            static_cast<ULONG>(wcslen(module_path.data())),
            0);
        if (FAILED(hr)) {
            break;
        }

        ComPtr<ITfCategoryMgr> cat_mgr;
        hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&cat_mgr));
        if (SUCCEEDED(hr) && cat_mgr) {
            for (const auto& cat : kSupportedCategories) {
                cat_mgr->RegisterCategory(kClsidQiQiTextService, cat, kClsidQiQiTextService);
            }
        }
    } while (false);

    if (need_uninit) {
        ::CoUninitialize();
    }
    if (SUCCEEDED(hr)) {
        QIQI_LOG_INFO("RegisterServer succeeded");
    } else {
        QIQI_LOG_ERROR("RegisterServer failed with HRESULT 0x{:08x}", static_cast<unsigned int>(hr));
    }
    log::Flush();
    return hr;
}

HRESULT UnregisterServer() {
    QIQI_LOG_INFO("UnregisterServer starting (arch={}, wow64={})",
                  version::kArch, IsRunningUnderWow64());
    UnregisterComClsid();

    // If unregistering the 32-bit WOW64 DLL while the 64-bit DLL is still registered,
    // keep the shared TSF Language Profile intact until the 64-bit DLL is also unregistered.
    if (IsRunningUnderWow64() && IsNative64BitClsidRegistered()) {
        QIQI_LOG_INFO("UnregisterServer (WOW64): removed 32-bit COM CLSID; keeping active 64-bit TSF profile");
        log::Flush();
        return S_OK;
    }

    const HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool need_uninit = SUCCEEDED(co_hr);

    ComPtr<ITfCategoryMgr> cat_mgr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&cat_mgr))) && cat_mgr) {
        for (const auto& cat : kSupportedCategories) {
            cat_mgr->UnregisterCategory(kClsidQiQiTextService, cat, kClsidQiQiTextService);
        }
    }

    ComPtr<ITfInputProcessorProfiles> profiles;
    if (SUCCEEDED(::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&profiles))) && profiles) {
        profiles->Unregister(kClsidQiQiTextService);
    }

    if (need_uninit) {
        ::CoUninitialize();
    }

    QIQI_LOG_INFO("UnregisterServer completed");
    log::Flush();
    return S_OK;
}

} // namespace qiqi
