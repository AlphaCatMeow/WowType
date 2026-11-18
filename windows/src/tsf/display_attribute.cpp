#include "display_attribute.h"

#include <new>

#include "globals.h"

namespace wowtype::tsf {

namespace {

/// 正在打的字：虚线下划线，字和背景颜色照应用自己的。
TF_DISPLAYATTRIBUTE inputAttribute() {
    TF_DISPLAYATTRIBUTE a{};
    a.crText.type = TF_CT_NONE;
    a.crBk.type = TF_CT_NONE;
    a.lsStyle = TF_LS_DOT;
    a.fBoldLine = FALSE;
    a.crLine.type = TF_CT_NONE;
    a.bAttr = TF_ATTR_INPUT;
    return a;
}

} // namespace

DisplayAttributeInfo::DisplayAttributeInfo() { dllAddRef(); }
DisplayAttributeInfo::~DisplayAttributeInfo() { dllRelease(); }

STDMETHODIMP DisplayAttributeInfo::QueryInterface(REFIID riid, void **ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(ITfDisplayAttributeInfo))) {
        *ppv = static_cast<ITfDisplayAttributeInfo *>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) DisplayAttributeInfo::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

STDMETHODIMP_(ULONG) DisplayAttributeInfo::Release() {
    const LONG n = InterlockedDecrement(&refs_);
    if (n == 0) {
        delete this;
    }
    return static_cast<ULONG>(n);
}

STDMETHODIMP DisplayAttributeInfo::GetGUID(GUID *guid) {
    if (!guid) {
        return E_INVALIDARG;
    }
    *guid = kGuidDisplayAttributeInput;
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::GetDescription(BSTR *description) {
    if (!description) {
        return E_INVALIDARG;
    }
    *description = SysAllocString(L"WowType 正在输入");
    return *description ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP DisplayAttributeInfo::GetAttributeInfo(TF_DISPLAYATTRIBUTE *attribute) {
    if (!attribute) {
        return E_INVALIDARG;
    }
    *attribute = inputAttribute();
    return S_OK;
}

STDMETHODIMP DisplayAttributeInfo::SetAttributeInfo(const TF_DISPLAYATTRIBUTE *) { return E_NOTIMPL; }

STDMETHODIMP DisplayAttributeInfo::Reset() { return S_OK; }

EnumDisplayAttributeInfo::EnumDisplayAttributeInfo() { dllAddRef(); }
EnumDisplayAttributeInfo::~EnumDisplayAttributeInfo() { dllRelease(); }

STDMETHODIMP EnumDisplayAttributeInfo::QueryInterface(REFIID riid, void **ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(IEnumTfDisplayAttributeInfo))) {
        *ppv = static_cast<IEnumTfDisplayAttributeInfo *>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

STDMETHODIMP_(ULONG) EnumDisplayAttributeInfo::Release() {
    const LONG n = InterlockedDecrement(&refs_);
    if (n == 0) {
        delete this;
    }
    return static_cast<ULONG>(n);
}

STDMETHODIMP EnumDisplayAttributeInfo::Clone(IEnumTfDisplayAttributeInfo **clone) {
    if (!clone) {
        return E_INVALIDARG;
    }
    auto *copy = new (std::nothrow) EnumDisplayAttributeInfo();
    if (!copy) {
        return E_OUTOFMEMORY;
    }
    copy->position_ = position_;
    *clone = copy;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Next(ULONG count, ITfDisplayAttributeInfo **info, ULONG *fetched) {
    if (!info) {
        return E_INVALIDARG;
    }
    ULONG got = 0;
    // 只有一种属性
    if (count > 0 && position_ == 0) {
        info[0] = new (std::nothrow) DisplayAttributeInfo();
        if (!info[0]) {
            return E_OUTOFMEMORY;
        }
        got = 1;
        position_ = 1;
    }
    if (fetched) {
        *fetched = got;
    }
    return got == count ? S_OK : S_FALSE;
}

STDMETHODIMP EnumDisplayAttributeInfo::Reset() {
    position_ = 0;
    return S_OK;
}

STDMETHODIMP EnumDisplayAttributeInfo::Skip(ULONG count) {
    if (count > 0 && position_ == 0) {
        position_ = 1;
        return count == 1 ? S_OK : S_FALSE;
    }
    return count == 0 ? S_OK : S_FALSE;
}

} // namespace wowtype::tsf
