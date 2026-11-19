// DLL 入口：DllMain、COM 的类工厂、regsvr32 用的注册函数。

#include <windows.h>

#include <olectl.h>

#include <cwchar>
#include <new>
#include <string>

#include "globals.h"
#include "text_service.h"

namespace wowtype::tsf {

HRESULT registerServer();
void unregisterServer();
HRESULT registerProfile();
void unregisterProfile();
HRESULT registerCategories();
void unregisterCategories();

namespace {

class ClassFactory final : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) {
            return E_INVALIDARG;
        }
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    // 静态对象：引用计数只算 DLL 的
    STDMETHODIMP_(ULONG) AddRef() override {
        dllAddRef();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        dllRelease();
        return 1;
    }
    STDMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override {
        if (!ppv) {
            return E_INVALIDARG;
        }
        *ppv = nullptr;
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        auto *service = new (std::nothrow) TextService();
        if (!service) {
            return E_OUTOFMEMORY;
        }
        const HRESULT hr = service->QueryInterface(riid, ppv);
        service->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL lock) override {
        if (lock) {
            dllAddRef();
        } else {
            dllRelease();
        }
        return S_OK;
    }
};

ClassFactory g_factory;

/// regsvr32 有时没初始化 COM：自己初始化一下（已经初始化过也没关系）。
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() {
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }
};

} // namespace

} // namespace wowtype::tsf

using namespace wowtype::tsf;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, kClsidTextService)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return g_factory.QueryInterface(riid, ppv);
}

STDAPI DllCanUnloadNow() { return dllRefCount() == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer() {
    ComScope com;
    HRESULT hr = registerServer();
    if (SUCCEEDED(hr)) {
        hr = registerProfile();
    }
    if (SUCCEEDED(hr)) {
        hr = registerCategories();
    }
    if (FAILED(hr)) {
        wchar_t code[16] = {};
        swprintf_s(code, L"%08lX", static_cast<unsigned long>(hr));
        wowtype::tsf::log(std::wstring(L"注册失败 0x") + code);
        unregisterCategories();
        unregisterProfile();
        unregisterServer();
    }
    return hr;
}

STDAPI DllUnregisterServer() {
    ComScope com;
    unregisterCategories();
    unregisterProfile();
    unregisterServer();
    return S_OK;
}
