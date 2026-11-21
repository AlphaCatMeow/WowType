#pragma once

// 编辑区的显示属性：应用照它给正在打的拼音画虚线下划线（ITfDisplayAttributeProvider 交出去的）。

#include <windows.h>

#include <msctf.h>

namespace wowtype::tsf {

class DisplayAttributeInfo final : public ITfDisplayAttributeInfo {
public:
    DisplayAttributeInfo();
    ~DisplayAttributeInfo();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP GetGUID(GUID *guid) override;
    STDMETHODIMP GetDescription(BSTR *description) override;
    STDMETHODIMP GetAttributeInfo(TF_DISPLAYATTRIBUTE *attribute) override;
    STDMETHODIMP SetAttributeInfo(const TF_DISPLAYATTRIBUTE *attribute) override;
    STDMETHODIMP Reset() override;

private:
    LONG refs_ = 1;
};

class EnumDisplayAttributeInfo final : public IEnumTfDisplayAttributeInfo {
public:
    EnumDisplayAttributeInfo();
    ~EnumDisplayAttributeInfo();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP Clone(IEnumTfDisplayAttributeInfo **clone) override;
    STDMETHODIMP Next(ULONG count, ITfDisplayAttributeInfo **info, ULONG *fetched) override;
    STDMETHODIMP Reset() override;
    STDMETHODIMP Skip(ULONG count) override;

private:
    LONG refs_ = 1;
    ULONG position_ = 0;
};

} // namespace wowtype::tsf
