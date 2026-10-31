#include "Ime/tsf/register.h"
#include "common/config.h"
#include "common/file_util.h"
#include "common/guids.h"
#include "common/i18n.h"
#include "common/logger.h"
#include "common/scoped_handle.h"
#include "common/version.h"
#include "Ime/engine/binary_dict.h"

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
    const std::filesystem::path module_path = GetModuleFilePath(instance);
    if (module_path.empty()) {
        QIQI_LOG_ERROR("RegisterComClsid: GetModuleFilePath returned empty path");
        return false;
    }

    const auto [clsid_key, inproc_key] = GetClsidRegistryPaths();

    {
        ScopedRegKey hkey;
        const LSTATUS st = ::RegCreateKeyExW(
            HKEY_CLASSES_ROOT, clsid_key.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, hkey.Put(), nullptr);
        if (st != ERROR_SUCCESS) {
            QIQI_LOG_ERROR("RegisterComClsid: RegCreateKeyExW('{}') failed (status={})",
                           WideToUtf8(clsid_key), st);
            return false;
        }
        SetRegSzValue(hkey.Get(), nullptr, kTextServiceDesc);
    }

    {
        ScopedRegKey hkey;
        const LSTATUS st = ::RegCreateKeyExW(
            HKEY_CLASSES_ROOT, inproc_key.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, hkey.Put(), nullptr);
        if (st != ERROR_SUCCESS) {
            QIQI_LOG_ERROR("RegisterComClsid: RegCreateKeyExW('{}') failed (status={})",
                           WideToUtf8(inproc_key), st);
            return false;
        }
        SetRegSzValue(hkey.Get(), nullptr, module_path.native());
        SetRegSzValue(hkey.Get(), L"ThreadingModel", L"Apartment");
    }
    QIQI_LOG_DEBUG("RegisterComClsid succeeded for '{}'", WideToUtf8(module_path.wstring()));
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

constexpr const wchar_t* kSettingsUriScheme = L"qiqiime-settings";
constexpr const wchar_t* kSettingsUriCommandKey = L"qiqiime-settings\\shell\\open\\command";

void RegisterSettingsUriProtocol(HINSTANCE instance) noexcept {
    try {
        const std::filesystem::path module_path = GetModuleFilePath(instance);
        if (module_path.empty()) {
            return;
        }
        const std::filesystem::path exe_path =
            ResolveBundledDataFile(module_path.parent_path(), L"QiQiIMESettings.exe");

        {
            ScopedRegKey proto_key;
            if (::RegCreateKeyExW(
                    HKEY_CLASSES_ROOT,
                    kSettingsUriScheme,
                    0,
                    nullptr,
                    REG_OPTION_NON_VOLATILE,
                    KEY_WRITE,
                    nullptr,
                    proto_key.Put(),
                    nullptr) != ERROR_SUCCESS) {
                return;
            }
            SetRegSzValue(proto_key.Get(), nullptr, L"URL:QiQiIME Settings Protocol");
            SetRegSzValue(proto_key.Get(), L"URL Protocol", L"");
        }

        {
            ScopedRegKey cmd_key;
            if (::RegCreateKeyExW(
                    HKEY_CLASSES_ROOT,
                    kSettingsUriCommandKey,
                    0,
                    nullptr,
                    REG_OPTION_NON_VOLATILE,
                    KEY_WRITE,
                    nullptr,
                    cmd_key.Put(),
                    nullptr) == ERROR_SUCCESS) {
                const std::wstring cmd_str = L"\"" + exe_path.wstring() + L"\"";
                SetRegSzValue(cmd_key.Get(), nullptr, cmd_str);
            }
        }
        QIQI_LOG_DEBUG("Registered qiqiime-settings: URI protocol -> '{}'",
                       WideToUtf8(exe_path.wstring()));
    } catch (...) {
    }
}

void UnregisterSettingsUriProtocol() noexcept {
    ::RegDeleteTreeW(HKEY_CLASSES_ROOT, kSettingsUriScheme);
}

void PrebuildBundledDictionaries(HINSTANCE instance) noexcept {
    try {
        const std::filesystem::path module_path = GetModuleFilePath(instance);
        if (module_path.empty()) {
            return;
        }
        const std::filesystem::path dll_dir = module_path.parent_path();
        for (const auto dict_name : kBundledDictFileNames) {
            const std::filesystem::path txt_path = ResolveBundledDataFile(dll_dir, dict_name);
            std::filesystem::path qdict_path = txt_path;
            qdict_path.replace_extension(L".qdict");
            if (WubiMappedDict::CompileTextFileToQdict(txt_path, qdict_path)) {
                QIQI_LOG_INFO("Prebuilt binary dictionary cache '{}'", WideToUtf8(qdict_path.wstring()));
            } else {
                QIQI_LOG_DEBUG("Skipped prebuilding '{}' (source '{}' not present or read-only)",
                               WideToUtf8(qdict_path.wstring()), WideToUtf8(txt_path.wstring()));
            }
        }
    } catch (...) {
    }
}

void RemovePrebuiltBundledDictionaries(HINSTANCE instance) noexcept {
    try {
        const std::filesystem::path module_path = GetModuleFilePath(instance);
        if (module_path.empty()) {
            return;
        }
        const std::filesystem::path dll_dir = module_path.parent_path();
        std::error_code ec;
        for (const auto dict_name : kBundledDictFileNames) {
            std::filesystem::path qdict_path = ResolveBundledDataFile(dll_dir, dict_name);
            qdict_path.replace_extension(L".qdict");
            if (std::filesystem::remove(qdict_path, ec)) {
                QIQI_LOG_DEBUG("Removed prebuilt dictionary cache '{}'", WideToUtf8(qdict_path.wstring()));
            }
        }
    } catch (...) {
    }
}

} // namespace

bool IsNative64BitClsidRegistered() noexcept {
    try {
        const auto [clsid_key, inproc_key] = GetClsidRegistryPaths();
        ScopedRegKey hkey;
        return ::RegOpenKeyExW(HKEY_CLASSES_ROOT, inproc_key.c_str(), 0,
                               KEY_READ | KEY_WOW64_64KEY, hkey.Put()) == ERROR_SUCCESS;
    } catch (...) {
        return false;
    }
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
        return SELFREG_E_CLASS;
    }

    // On 64-bit Windows, when registering the 32-bit (WOW64) DLL after the 64-bit DLL is already
    // registered, only the 32-bit HKCR\WOW6432Node\CLSID\InProcServer32 entry is needed; skip
    // overwriting the shared 64-bit TSF Language Profile icon/module path.
    if (is_wow64 && IsNative64BitClsidRegistered()) {
        QIQI_LOG_INFO("RegisterServer (WOW64): 32-bit COM CLSID registered; native 64-bit TSF profile already active");
        return S_OK;
    }

    HRESULT hr = E_FAIL;
    do {
        ComPtr<ITfInputProcessorProfiles> profiles = custom_profiles;
        if (!profiles) {
            hr = ::CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&profiles));
            if (FAILED(hr) || !profiles) {
                QIQI_LOG_ERROR("RegisterServer: CoCreateInstance(CLSID_TF_InputProcessorProfiles) failed (hr=0x{:08x})",
                               gsl::narrow_cast<unsigned int>(hr));
                break;
            }
        }

        hr = profiles->Register(kClsidQiQiTextService);
        if (FAILED(hr)) {
            QIQI_LOG_ERROR("RegisterServer: ITfInputProcessorProfiles::Register failed (hr=0x{:08x})",
                           gsl::narrow_cast<unsigned int>(hr));
            break;
        }

        const std::wstring module_path = GetModuleFilePath(instance).wstring();
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
            QIQI_LOG_ERROR("RegisterServer: AddLanguageProfile failed (hr=0x{:08x})",
                           gsl::narrow_cast<unsigned int>(hr));
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
                QIQI_LOG_ERROR("RegisterServer: ITfInputProcessorProfileMgr::RegisterProfile failed (hr=0x{:08x})",
                               gsl::narrow_cast<unsigned int>(hr));
                break;
            }
        }

        // Set localized MUI resource string ID 100 on the language profile so Windows displays
        // "QiQi Wubi" on English UI and "奇奇五笔" on Chinese UI.
        ComPtr<ITfInputProcessorProfilesEx> profiles_ex;
        if (SUCCEEDED(profiles.As(&profiles_ex)) && profiles_ex) {
            const HRESULT disp_hr = profiles_ex->SetLanguageProfileDisplayName(
                kClsidQiQiTextService,
                kChineseSimplifiedLangId,
                kGuidProfileWubi86,
                module_path.c_str(),
                gsl::narrow_cast<ULONG>(module_path.size()),
                100);
            if (FAILED(disp_hr)) {
                QIQI_LOG_WARN("RegisterServer: SetLanguageProfileDisplayName failed (hr=0x{:08x})",
                              gsl::narrow_cast<unsigned int>(disp_hr));
            }
        }

        hr = profiles->EnableLanguageProfileByDefault(
            kClsidQiQiTextService, kChineseSimplifiedLangId, kGuidProfileWubi86, TRUE);
        if (FAILED(hr)) {
            QIQI_LOG_ERROR("RegisterServer: EnableLanguageProfileByDefault failed (hr=0x{:08x})",
                           gsl::narrow_cast<unsigned int>(hr));
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
                const HRESULT cat_hr = cat_mgr->RegisterCategory(kClsidQiQiTextService, cat, kClsidQiQiTextService);
                if (FAILED(cat_hr)) {
                    QIQI_LOG_WARN("RegisterServer: RegisterCategory failed (hr=0x{:08x})",
                                  gsl::narrow_cast<unsigned int>(cat_hr));
                }
            }
        } else {
            QIQI_LOG_WARN("RegisterServer: ITfCategoryMgr unavailable (hr=0x{:08x})",
                          gsl::narrow_cast<unsigned int>(hr));
        }
        RegisterSettingsUriProtocol(instance);
        PrebuildBundledDictionaries(instance);
    } while (false);

    if (SUCCEEDED(hr)) {
        QIQI_LOG_INFO("RegisterServer succeeded");
    } else {
        QIQI_LOG_ERROR("RegisterServer failed with HRESULT 0x{:08x}", gsl::narrow_cast<unsigned int>(hr));
    }
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
    UnregisterSettingsUriProtocol();
    RemovePrebuiltBundledDictionaries(g_dll_instance);

    QIQI_LOG_INFO("UnregisterServer completed");
    return S_OK;
}

HRESULT RegisterServer(HINSTANCE instance) {
    return RegisterServerWithManagers(instance);
}

HRESULT UnregisterServer() {
    return UnregisterServerWithManagers();
}

} // namespace qiqi
