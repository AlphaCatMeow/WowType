// 注册：regsvr32 调 DllRegisterServer（要管理员权限）。
//   1. COM 类：HKCR\CLSID\{文本服务}\InprocServer32 指向这个 DLL（ThreadingModel=Apartment）
//   2. 输入法配置：ITfInputProcessorProfileMgr::RegisterProfile，简体中文（0804）下的「WowType」
//   3. 能力类别：键盘输入法、显示属性、沉浸式（UWP / 开始菜单搜索）、系统托盘、不经 COM 也能载入
// regsvr32 /u 反过来删掉。

#include <windows.h>

#include <msctf.h>
#include <olectl.h>
#include <wrl/client.h>

#include <string>

#include "globals.h"

using Microsoft::WRL::ComPtr;

namespace wowtype::tsf {

namespace {

const GUID *const kCategories[] = {
    &GUID_TFCAT_TIP_KEYBOARD,
    &GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
    // Windows 8 起：沉浸式应用（UWP、开始菜单的搜索框）里也能用；任务栏的输入指示器
    &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
    &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    // 不经 COM 激活也能载入（有的进程没初始化 COM）
    &GUID_TFCAT_TIPCAP_COMLESS,
};

std::wstring guidString(const GUID &guid) {
    wchar_t buffer[64] = {};
    StringFromGUID2(guid, buffer, 64);
    return buffer;
}

bool setString(HKEY key, const wchar_t *name, const std::wstring &value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

const std::wstring clsidKey() { return L"CLSID\\" + guidString(kClsidTextService); }

} // namespace

HRESULT registerServer() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, clsidKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return SELFREG_E_CLASS;
    }
    bool ok = setString(key, nullptr, kDisplayName);
    HKEY server = nullptr;
    if (RegCreateKeyExW(key, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &server, nullptr) == ERROR_SUCCESS) {
        ok = setString(server, nullptr, modulePath()) && setString(server, L"ThreadingModel", L"Apartment") && ok;
        RegCloseKey(server);
    } else {
        ok = false;
    }
    RegCloseKey(key);
    return ok ? S_OK : SELFREG_E_CLASS;
}

void unregisterServer() { RegDeleteTreeW(HKEY_CLASSES_ROOT, clsidKey().c_str()); }

HRESULT registerProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&profiles));
    if (FAILED(hr)) {
        return hr;
    }
    const std::wstring icon = modulePath();
    // 图标是 DLL 资源里的第一个（res/wowtype.ico）
    hr = profiles->RegisterProfile(kClsidTextService, kLangId, kGuidProfile, kDisplayName,
                                   static_cast<ULONG>(wcslen(kDisplayName)), icon.c_str(),
                                   static_cast<ULONG>(icon.size()), 0, nullptr, 0, TRUE, 0);
    return hr;
}

void unregisterProfile() {
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&profiles)))) {
        profiles->UnregisterProfile(kClsidTextService, kLangId, kGuidProfile, 0);
    }
}

HRESULT registerCategories() {
    ComPtr<ITfCategoryMgr> categories;
    HRESULT hr = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories));
    if (FAILED(hr)) {
        return hr;
    }
    for (const GUID *category : kCategories) {
        hr = categories->RegisterCategory(kClsidTextService, *category, kClsidTextService);
        if (FAILED(hr)) {
            return hr;
        }
    }
    return S_OK;
}

void unregisterCategories() {
    ComPtr<ITfCategoryMgr> categories;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&categories)))) {
        for (const GUID *category : kCategories) {
            categories->UnregisterCategory(kClsidTextService, *category, kClsidTextService);
        }
    }
}

} // namespace wowtype::tsf
