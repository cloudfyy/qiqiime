// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "base.h"

namespace qiqi::test {

class MockTfInputProcessorProfiles final
    : public ITfInputProcessorProfilesEx,
      public ITfInputProcessorProfileMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfInputProcessorProfiles) ||
            IsEqualIID(riid, IID_ITfInputProcessorProfilesEx)) {
            *ppvObj = static_cast<ITfInputProcessorProfilesEx*>(this);
            AddRef();
            return S_OK;
        }
        if (IsEqualIID(riid, IID_ITfInputProcessorProfileMgr) && !fail_qi_profile_mgr_) {
            *ppvObj = static_cast<ITfInputProcessorProfileMgr*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) delete this;
        return c;
    }
    STDMETHODIMP Register(REFCLSID) noexcept override {
        if (fail_register_) return E_FAIL;
        registered_ = true;
        return S_OK;
    }
    STDMETHODIMP Unregister(REFCLSID) noexcept override {
        registered_ = false;
        return S_OK;
    }
    STDMETHODIMP AddLanguageProfile(REFCLSID, LANGID, REFGUID, const WCHAR* desc, ULONG cch, const WCHAR*, ULONG, ULONG) override {
        if (fail_add_profile_) return E_FAIL;
        profile_added_ = true;
        if (desc && cch > 0) {
            last_profile_desc_.assign(desc, cch);
        }
        return S_OK;
    }
    STDMETHODIMP RemoveLanguageProfile(REFCLSID, LANGID, REFGUID) noexcept override { return S_OK; }
    STDMETHODIMP EnumInputProcessorInfo(IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetDefaultLanguageProfile(LANGID, REFGUID, CLSID*, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetDefaultLanguageProfile(LANGID, REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ActivateLanguageProfile(REFCLSID, LANGID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveLanguageProfile(REFCLSID, LANGID*, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageProfileDescription(REFCLSID, LANGID, REFGUID, BSTR*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetCurrentLanguage(LANGID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ChangeCurrentLanguage(LANGID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetLanguageList(LANGID**, ULONG*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumLanguageProfiles(LANGID, IEnumTfLanguageProfiles**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfile(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) noexcept override {
        ++enable_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_) return E_ACCESSDENIED;
        enabled_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP IsEnabledLanguageProfile(REFCLSID, LANGID, REFGUID, BOOL*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnableLanguageProfileByDefault(REFCLSID clsid, LANGID langid, REFGUID profile, BOOL enable) noexcept override {
        ++enable_default_calls_;
        if (!IsEqualCLSID(clsid, kClsidQiQiTextService) || langid != kChineseSimplifiedLangId ||
            !IsEqualGUID(profile, kGuidProfileWubi86)) return E_INVALIDARG;
        if (fail_enable_default_) return E_ACCESSDENIED;
        enabled_by_default_ = enable != FALSE;
        return S_OK;
    }
    STDMETHODIMP SubstituteKeyboardLayout(REFCLSID, LANGID, REFGUID, HKL) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP SetLanguageProfileDisplayName(REFCLSID, LANGID, REFGUID, const WCHAR*, ULONG, ULONG res_id) noexcept override {
        display_name_res_id_ = res_id;
        return S_OK;
    }

    // ITfInputProcessorProfileMgr
    STDMETHODIMP ActivateProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, DWORD) noexcept override {
        ++activate_calls_;
        return S_OK;
    }
    STDMETHODIMP DeactivateProfile(DWORD profile_type, LANGID langid, REFCLSID clsid, REFGUID profile, HKL, DWORD flags) noexcept override {
        ++deactivate_calls_;
        if (profile_type != TF_PROFILETYPE_INPUTPROCESSOR || !IsEqualCLSID(clsid, kClsidQiQiTextService) ||
            langid != kChineseSimplifiedLangId || !IsEqualGUID(profile, kGuidProfileWubi86)) {
            return E_INVALIDARG;
        }
        if (fail_deactivate_) return E_FAIL;
        deactivate_flags_ = flags;
        return S_OK;
    }
    STDMETHODIMP RegisterProfile(REFCLSID, LANGID, REFGUID, const WCHAR*, ULONG, const WCHAR*, ULONG, ULONG, HKL, DWORD, BOOL, DWORD) noexcept override {
        if (fail_register_profile_) return E_FAIL;
        profile_mgr_registered_ = true;
        return S_OK;
    }
    STDMETHODIMP UnregisterProfile(REFCLSID, LANGID, REFGUID, DWORD) noexcept override { return S_OK; }
    STDMETHODIMP GetProfile(DWORD, LANGID, REFCLSID, REFGUID, HKL, TF_INPUTPROCESSORPROFILE*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetActiveProfile(REFGUID, TF_INPUTPROCESSORPROFILE*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumProfiles(LANGID, IEnumTfInputProcessorProfiles**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP ReleaseInputProcessor(REFCLSID, DWORD) noexcept override { return E_NOTIMPL; }

    bool fail_register_ = false;
    bool fail_add_profile_ = false;
    bool fail_register_profile_ = false;
    bool fail_enable_ = false;
    bool fail_enable_default_ = false;
    bool fail_qi_profile_mgr_ = false;
    bool fail_deactivate_ = false;
    bool enabled_ = false;
    bool enabled_by_default_ = false;
    int enable_calls_ = 0;
    int enable_default_calls_ = 0;
    ULONG display_name_res_id_ = 0;
    int activate_calls_ = 0;
    int deactivate_calls_ = 0;
    DWORD deactivate_flags_ = 0;
    bool registered_ = false;
    bool profile_added_ = false;
    bool profile_mgr_registered_ = false;
    std::wstring last_profile_desc_;
private:
    ULONG ref_ = 1;
};

// Minimal mock ITfCategoryMgr for registration and display attribute tests.
class MockTfCategoryMgr final : public ITfCategoryMgr {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) noexcept override {
        if (!ppvObj) return E_INVALIDARG;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCategoryMgr)) {
            *ppvObj = static_cast<ITfCategoryMgr*>(this);
            AddRef();
            return S_OK;
        }
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() noexcept override { return ++ref_; }
    STDMETHODIMP_(ULONG) Release() noexcept override {
        const ULONG c = --ref_;
        if (c == 0) {
            if (on_final_release_) on_final_release_(this, release_context_);
            // COM Release destroys the mock when its reference count reaches zero.
#pragma warning(suppress : 26401 26409)
            delete this;
        }
        return c;
    }
    bool HasCategory(REFGUID category) const noexcept {
        return std::any_of(registered_categories_.begin(), registered_categories_.end(),
                           [&](const GUID& existing) { return IsEqualGUID(existing, category); });
    }
    STDMETHODIMP RegisterCategory(REFCLSID, REFGUID category, REFGUID) override {
        register_category_calls_.push_back(category);
        if (!HasCategory(category)) registered_categories_.push_back(category);
        return S_OK;
    }
    STDMETHODIMP UnregisterCategory(REFCLSID, REFGUID category, REFGUID) override {
        unregister_category_calls_.push_back(category);
        if (FAILED(unregister_category_result_)) return unregister_category_result_;
        std::erase_if(registered_categories_, [&](const GUID& existing) { return IsEqualGUID(existing, category); });
        return S_OK;
    }
    STDMETHODIMP EnumCategoriesInItem(REFGUID, IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP EnumItemsInCategory(REFGUID, IEnumGUID**) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP FindClosestCategory(REFGUID, GUID* category, const GUID** list, ULONG count) noexcept override {
        if (!category || (count != 0 && !list)) return E_INVALIDARG;
        *category = GUID_NULL;
        if (FAILED(find_category_result_)) return find_category_result_;
        if (count == 0 && !registered_categories_.empty()) {
            *category = registered_categories_.front();
            return S_OK;
        }
        for (const auto* candidate : std::span<const GUID* const>(list, count)) {
            if (!candidate) return E_INVALIDARG;
            if (HasCategory(*candidate)) {
                *category = *candidate;
                return S_OK;
            }
        }
        return missing_category_result_;
    }
    STDMETHODIMP RegisterGUIDDescription(REFCLSID, REFGUID, const WCHAR*, ULONG) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDescription(REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDescription(REFGUID, BSTR*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUIDDWORD(REFCLSID, REFGUID, DWORD) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP UnregisterGUIDDWORD(REFCLSID, REFGUID) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP GetGUIDDWORD(REFGUID, DWORD*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP RegisterGUID(REFGUID guid, TfGuidAtom* atom) noexcept override {
        if (!atom) return E_INVALIDARG;
        ++register_guid_calls_;
        registered_guid_ = guid;
        *atom = registered_atom_;
        return register_guid_result_;
    }
    STDMETHODIMP GetGUID(TfGuidAtom, GUID*) noexcept override { return E_NOTIMPL; }
    STDMETHODIMP IsEqualTfGuidAtom(TfGuidAtom, REFGUID, BOOL*) noexcept override { return E_NOTIMPL; }

    std::vector<GUID> registered_categories_;
    std::vector<GUID> register_category_calls_;
    std::vector<GUID> unregister_category_calls_;
    HRESULT find_category_result_ = S_OK;
    HRESULT missing_category_result_ = S_FALSE;
    HRESULT unregister_category_result_ = S_OK;
    size_t register_guid_calls_ = 0;
    GUID registered_guid_{};
    TfGuidAtom registered_atom_ = 73;
    HRESULT register_guid_result_ = E_NOTIMPL;
    void (*on_final_release_)(MockTfCategoryMgr*, void*) noexcept = nullptr;
    void* release_context_ = nullptr;
private:
    ULONG ref_ = 1;
};

} // namespace qiqi::test
