#include "tsf/register.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/version.h"

#include <gsl/gsl>
#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>
#include <array>
#include <string>
#include <utility>

namespace qiqi {

using Microsoft::WRL::ComPtr;

namespace {

std::wstring GuidToString(REFGUID guid) {
    std::array<wchar_t, 64> buf{};
    ::StringFromGUID2(guid, buf.data(), gsl::narrow_cast<int>(buf.size()));
    return buf.data();
}

std::pair<std::wstring, std::wstring> GetClsidRegistryPaths() {
    std::wstring clsid_key = L"CLSID\\" + GuidToString(kClsidQiQiTextService);
    std::wstring inproc_key = clsid_key + L"\\InProcServer32";
    return {std::move(clsid_key), std::move(inproc_key)};
}

void SetRegSzValue(HKEY key, const wchar_t* value_name, std::wstring_view value) noexcept {
    ::RegSetValueExW(
        key,
        value_name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.data()),
        gsl::narrow_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

bool RegisterComClsid(HINSTANCE instance) {
    const std::filesystem::path module_path = log::GetModuleFilePath(instance);
    if (module_path.empty()) {
        return false;
    }

    const auto [clsid_key, inproc_key] = GetClsidRegistryPaths();

    {
        ScopedRegKey hkey;
        if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, hkey.Put(), nullptr) != ERROR_SUCCESS) {
            return false;
        }
        SetRegSzValue(hkey.Get(), nullptr, kTextServiceDesc);
    }

    {
        ScopedRegKey hkey;
        if (::RegCreateKeyExW(HKEY_CLASSES_ROOT, inproc_key.c_str(), 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, hkey.Put(), nullptr) != ERROR_SUCCESS) {
            return false;
        }
        SetRegSzValue(hkey.Get(), nullptr, module_path.native());
        SetRegSzValue(hkey.Get(), L"ThreadingModel", L"Apartment");
    }
    return true;
}

void UnregisterComClsid() {
    const auto [clsid_key, inproc_key] = GetClsidRegistryPaths();
    ::RegDeleteKeyW(HKEY_CLASSES_ROOT, inproc_key.c_str());
    ::RegDeleteKeyW(HKEY_CLASSES_ROOT, clsid_key.c_str());
}

// Categories declared for modern Windows, UWP/AppContainer, and fullscreen UILess games
const std::array<GUID, 7> kSupportedCategories = {
    GUID_TFCAT_TIP_KEYBOARD,
    GUID_TFCAT_TIPCAP_SECUREMODE,
    GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
    GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
    GUID_TFCAT_TIPCAP_COMLESS,
    GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER
};

} // namespace

bool IsNative64BitClsidRegistered() noexcept {
    std::array<wchar_t, 64> guid_buf{};
    if (::StringFromGUID2(kClsidQiQiTextService, guid_buf.data(), gsl::narrow_cast<int>(guid_buf.size())) <= 0) {
        return false;
    }
    std::array<wchar_t, 128> inproc_key{};
    wcscpy_s(inproc_key.data(), inproc_key.size(), L"CLSID\\");
    wcscat_s(inproc_key.data(), inproc_key.size(), guid_buf.data());
    wcscat_s(inproc_key.data(), inproc_key.size(), L"\\InProcServer32");

    ScopedRegKey hkey;
    return ::RegOpenKeyExW(HKEY_CLASSES_ROOT, inproc_key.data(), 0,
                           KEY_READ | KEY_WOW64_64KEY, hkey.Put()) == ERROR_SUCCESS;
}

bool IsRunningUnderWow64() noexcept {
    BOOL is_wow64 = FALSE;
    if (::IsWow64Process(::GetCurrentProcess(), &is_wow64)) {
        return is_wow64 != FALSE;
    }
    return false;
}

HRESULT EnableCurrentUserProfile(ITfInputProcessorProfiles* custom_profiles) {
    ComPtr<ITfInputProcessorProfiles> profiles = custom_profiles;
    HRESULT hr = S_OK;
    if (!profiles) {
        hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&profiles));
    }
    if (SUCCEEDED(hr)) {
        hr = profiles->EnableLanguageProfile(
            kClsidQiQiTextService, kChineseSimplifiedLangId, kGuidProfileWubi86, TRUE);
    }
    if (FAILED(hr)) {
        QIQI_LOG_ERROR("EnableCurrentUserProfile failed with HRESULT 0x{:08x}",
                       gsl::narrow_cast<unsigned int>(hr));
    } else {
        QIQI_LOG_INFO("QiQi Wubi profile enabled for the current user");
    }
    log::Flush();
    return hr;
}

HRESULT RegisterServerWithManagers(
    HINSTANCE instance,
    ITfInputProcessorProfiles* custom_profiles,
    ITfCategoryMgr* custom_cat_mgr,
    std::optional<bool> override_wow64) {
    const bool is_wow64 = override_wow64.value_or(IsRunningUnderWow64());
    QIQI_LOG_INFO("RegisterServer starting (version={}, arch={}, wow64={})",
                  version::kFullVersion, version::kArch, is_wow64);
    if (!RegisterComClsid(instance)) {
        QIQI_LOG_ERROR("RegisterComClsid failed (administrator privileges required)");
        log::Flush();
        return SELFREG_E_CLASS;
    }

    // On 64-bit Windows, when registering the 32-bit (WOW64) DLL after the 64-bit DLL is already
    // registered, only the 32-bit HKCR\WOW6432Node\CLSID\InProcServer32 entry is needed; skip
    // overwriting the shared 64-bit TSF Language Profile icon/module path.
    if (is_wow64 && IsNative64BitClsidRegistered()) {
        QIQI_LOG_INFO("RegisterServer (WOW64): 32-bit COM CLSID registered; native 64-bit TSF profile already active");
        log::Flush();
        return S_OK;
    }

    HRESULT hr = E_FAIL;
    do {
        ComPtr<ITfInputProcessorProfiles> profiles = custom_profiles;
        if (!profiles) {
            hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&profiles));
            if (FAILED(hr) || !profiles) {
                break;
            }
        }

        hr = profiles->Register(kClsidQiQiTextService);
        if (FAILED(hr)) {
            break;
        }

        const std::wstring module_path = log::GetModuleFilePath(instance).wstring();
        const wchar_t* const profile_desc =
            (ResolveUiLanguage(UiLanguage::Auto) == UiLanguage::EnUs) ? kProfileDescEn : kProfileDescZh;

        hr = profiles->AddLanguageProfile(
            kClsidQiQiTextService,
            kChineseSimplifiedLangId,
            kGuidProfileWubi86,
            profile_desc,
            gsl::narrow_cast<ULONG>(wcslen(profile_desc)),
            module_path.c_str(),
            gsl::narrow_cast<ULONG>(module_path.size()),
            0);
        if (FAILED(hr)) {
            break;
        }

        // Also register MUI display name resource (ID 100) and icon index 0 via ITfInputProcessorProfileMgr
        ComPtr<ITfInputProcessorProfileMgr> profile_mgr;
        if (SUCCEEDED(profiles.As(&profile_mgr)) && profile_mgr) {
            hr = profile_mgr->RegisterProfile(
                kClsidQiQiTextService,
                kChineseSimplifiedLangId,
                kGuidProfileWubi86,
                profile_desc,
                gsl::narrow_cast<ULONG>(wcslen(profile_desc)),
                module_path.c_str(),
                gsl::narrow_cast<ULONG>(module_path.size()),
                0,
                nullptr,
                0,
                TRUE,
                0);
            if (FAILED(hr)) {
                break;
            }
        }

        // Set localized MUI resource string ID 100 on the language profile so Windows displays
        // "QiQi Wubi" on English UI and "奇奇五笔" on Chinese UI.
        ComPtr<ITfInputProcessorProfilesEx> profiles_ex;
        if (SUCCEEDED(profiles.As(&profiles_ex)) && profiles_ex) {
            profiles_ex->SetLanguageProfileDisplayName(
                kClsidQiQiTextService,
                kChineseSimplifiedLangId,
                kGuidProfileWubi86,
                module_path.c_str(),
                gsl::narrow_cast<ULONG>(module_path.size()),
                100);
        }

        hr = profiles->EnableLanguageProfileByDefault(
            kClsidQiQiTextService, kChineseSimplifiedLangId, kGuidProfileWubi86, TRUE);
        if (FAILED(hr)) {
            break;
        }
        hr = EnableCurrentUserProfile(profiles.Get());
        if (FAILED(hr)) {
            break;
        }

        ComPtr<ITfCategoryMgr> cat_mgr = custom_cat_mgr;
        if (!cat_mgr) {
            hr = ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&cat_mgr));
        } else {
            hr = S_OK;
        }
        if (SUCCEEDED(hr) && cat_mgr) {
            for (const auto& cat : kSupportedCategories) {
                cat_mgr->RegisterCategory(kClsidQiQiTextService, cat, kClsidQiQiTextService);
            }
        }
    } while (false);

    if (SUCCEEDED(hr)) {
        QIQI_LOG_INFO("RegisterServer succeeded");
    } else {
        QIQI_LOG_ERROR("RegisterServer failed with HRESULT 0x{:08x}", gsl::narrow_cast<unsigned int>(hr));
    }
    log::Flush();
    return hr;
}

HRESULT UnregisterServerWithManagers(
    ITfInputProcessorProfiles* custom_profiles,
    ITfCategoryMgr* custom_cat_mgr,
    std::optional<bool> override_wow64) {
    const bool is_wow64 = override_wow64.value_or(IsRunningUnderWow64());
    QIQI_LOG_INFO("UnregisterServer starting (arch={}, wow64={})",
                  version::kArch, is_wow64);
    const bool keep_native_profile = is_wow64 && IsNative64BitClsidRegistered();
    UnregisterComClsid();

    // If unregistering the 32-bit WOW64 DLL while the 64-bit DLL is still registered,
    // keep the shared TSF Language Profile intact until the 64-bit DLL is also unregistered.
    if (keep_native_profile) {
        QIQI_LOG_INFO("UnregisterServer (WOW64): removed 32-bit COM CLSID; keeping active 64-bit TSF profile");
        log::Flush();
        return S_OK;
    }

    ComPtr<ITfCategoryMgr> cat_mgr = custom_cat_mgr;
    if (!cat_mgr) {
        ::CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&cat_mgr));
    }
    if (cat_mgr) {
        for (const auto& cat : kSupportedCategories) {
            cat_mgr->UnregisterCategory(kClsidQiQiTextService, cat, kClsidQiQiTextService);
        }
    }

    ComPtr<ITfInputProcessorProfiles> profiles = custom_profiles;
    if (!profiles) {
        ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&profiles));
    }
    if (profiles) {
        // Deactivate without persisting an HKCU disable override across reinstalls.
        ComPtr<ITfInputProcessorProfileMgr> profile_mgr;
        if (SUCCEEDED(profiles.As(&profile_mgr)) && profile_mgr) {
            const HRESULT deactivate_hr = profile_mgr->DeactivateProfile(
                TF_PROFILETYPE_INPUTPROCESSOR,
                kChineseSimplifiedLangId,
                kClsidQiQiTextService,
                kGuidProfileWubi86,
                nullptr,
                TF_IPPMF_FORSESSION);
            if (FAILED(deactivate_hr)) {
                QIQI_LOG_WARN("DeactivateProfile failed with HRESULT 0x{:08x}; continuing unregistration",
                              gsl::narrow_cast<unsigned int>(deactivate_hr));
            }
            profile_mgr->UnregisterProfile(
                kClsidQiQiTextService,
                kChineseSimplifiedLangId,
                kGuidProfileWubi86,
                0);
            profile_mgr->ReleaseInputProcessor(kClsidQiQiTextService, 0);
        }
        profiles->RemoveLanguageProfile(
            kClsidQiQiTextService, kChineseSimplifiedLangId, kGuidProfileWubi86);
        profiles->Unregister(kClsidQiQiTextService);
    }
    ::CoFreeUnusedLibraries();

    QIQI_LOG_INFO("UnregisterServer completed");
    log::Flush();
    return S_OK;
}

HRESULT RegisterServer(HINSTANCE instance) {
    return RegisterServerWithManagers(instance);
}

HRESULT UnregisterServer() {
    return UnregisterServerWithManagers();
}

} // namespace qiqi
