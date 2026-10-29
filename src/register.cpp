#include "register.h"
#include "guids.h"
#include "logger.h"

#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <array>
#include <string>

namespace qiqi {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring GuidToString(REFGUID guid) {
    wchar_t buf[64]{};
    ::StringFromGUID2(guid, buf, 64);
    return buf;
}

bool RegisterComClsid(HINSTANCE instance) {
    wchar_t module_path[MAX_PATH]{};
    if (::GetModuleFileNameW(instance, module_path, MAX_PATH) == 0) {
        return false;
    }

    const std::wstring clsid_key = L"CLSID\\" + GuidToString(kClsidQiQiTextService);
    const std::wstring inproc_key = clsid_key + L"\\InProcServer32";

    HKEY hkey = nullptr;
    if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    ::RegSetValueExW(hkey, nullptr, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(kTextServiceDesc),
                     static_cast<DWORD>((wcslen(kTextServiceDesc) + 1) * sizeof(wchar_t)));
    ::RegCloseKey(hkey);

    if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, inproc_key.c_str(), 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hkey, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    ::RegSetValueExW(hkey, nullptr, 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(module_path),
                     static_cast<DWORD>((wcslen(module_path) + 1) * sizeof(wchar_t)));
    constexpr const wchar_t* kModel = L"Apartment";
    ::RegSetValueExW(hkey, L"ThreadingModel", 0, REG_SZ,
                     reinterpret_cast<const BYTE*>(kModel),
                     static_cast<DWORD>((wcslen(kModel) + 1) * sizeof(wchar_t)));
    ::RegCloseKey(hkey);
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

HRESULT RegisterServer(HINSTANCE instance) {
    QIQI_LOG_INFO("RegisterServer starting");
    if (!RegisterComClsid(instance)) {
        QIQI_LOG_ERROR("RegisterComClsid failed (administrator privileges required)");
        log::Flush();
        return SELFREG_E_CLASS;
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

        wchar_t module_path[MAX_PATH]{};
        ::GetModuleFileNameW(instance, module_path, MAX_PATH);

        hr = profiles->AddLanguageProfile(
            kClsidQiQiTextService,
            kChineseSimplifiedLangId,
            kGuidProfileWubi86,
            kProfileDesc,
            static_cast<ULONG>(wcslen(kProfileDesc)),
            module_path,
            static_cast<ULONG>(wcslen(module_path)),
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
    QIQI_LOG_INFO("UnregisterServer starting");
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

    UnregisterComClsid();
    QIQI_LOG_INFO("UnregisterServer completed");
    log::Flush();
    return S_OK;
}

} // namespace qiqi
