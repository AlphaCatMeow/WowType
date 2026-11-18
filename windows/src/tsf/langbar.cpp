#include "langbar.h"

#include <olectl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "globals.h"
#include "text_service.h"

namespace wowtype::tsf {

namespace {

constexpr DWORD kSinkCookie = 0x5757; // 只收一个 sink

/// 任务栏是深色（Windows 10 默认、Windows 11 深色模式）时用白字。
bool taskbarDark() {
    DWORD value = 0;
    DWORD size = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return true;
    }
    return value == 0;
}

} // namespace

HICON makeTextIcon(const wchar_t *text) {
    const int size = GetSystemMetrics(SM_CXSMICON) > 0 ? GetSystemMetrics(SM_CXSMICON) : 16;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    void *bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        DeleteDC(dc);
        return nullptr;
    }
    HGDIOBJ oldBitmap = SelectObject(dc, color);
    std::memset(bits, 0, static_cast<std::size_t>(size) * size * 4);
    // 灰度抗锯齿（不要 ClearType 的彩边），白字画在黑底上，亮度就是不透明度
    HFONT font = CreateFontW(-(size * 15 / 16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH,
                             L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r{0, 0, size, size};
    DrawTextW(dc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    const uint32_t fg = taskbarDark() ? 0xFFFFFF : 0x000000;
    auto *pixels = static_cast<uint32_t *>(bits);
    for (int i = 0; i < size * size; ++i) {
        const uint32_t p = pixels[i];
        const uint32_t a = std::max({p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF});
        // 预乘 alpha
        const uint32_t rr = ((fg >> 16) & 0xFF) * a / 255;
        const uint32_t gg = ((fg >> 8) & 0xFF) * a / 255;
        const uint32_t bb = (fg & 0xFF) * a / 255;
        pixels[i] = (a << 24) | (rr << 16) | (gg << 8) | bb;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBitmap);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO iconInfo{};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = mask;
    iconInfo.hbmColor = color;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask);
    DeleteObject(color);
    DeleteDC(dc);
    return icon;
}

LangBarButton::LangBarButton(TextService *owner) : owner_(owner) { dllAddRef(); }

LangBarButton::~LangBarButton() {
    if (sink_) {
        sink_->Release();
    }
    dllRelease();
}

STDMETHODIMP LangBarButton::QueryInterface(REFIID riid, void **ppv) {
    if (!ppv) {
        return E_INVALIDARG;
    }
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(ITfLangBarItem)) ||
        IsEqualIID(riid, __uuidof(ITfLangBarItemButton))) {
        *ppv = static_cast<ITfLangBarItemButton *>(this);
    } else if (IsEqualIID(riid, __uuidof(ITfSource))) {
        *ppv = static_cast<ITfSource *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) LangBarButton::AddRef() { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

STDMETHODIMP_(ULONG) LangBarButton::Release() {
    const LONG n = InterlockedDecrement(&refs_);
    if (n == 0) {
        delete this;
    }
    return static_cast<ULONG>(n);
}

void LangBarButton::update() {
    if (sink_) {
        sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
    }
}

STDMETHODIMP LangBarButton::GetInfo(TF_LANGBARITEMINFO *info) {
    if (!info) {
        return E_INVALIDARG;
    }
    info->clsidService = kClsidTextService;
    info->guidItem = kGuidLbiInputMode;
    info->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    info->ulSort = 0;
    wcsncpy_s(info->szDescription, L"WowType 输入模式", _TRUNCATE);
    return S_OK;
}

STDMETHODIMP LangBarButton::GetStatus(DWORD *status) {
    if (!status) {
        return E_INVALIDARG;
    }
    *status = 0;
    return S_OK;
}

STDMETHODIMP LangBarButton::Show(BOOL) { return E_NOTIMPL; }

STDMETHODIMP LangBarButton::GetTooltipString(BSTR *tooltip) {
    if (!tooltip) {
        return E_INVALIDARG;
    }
    const std::wstring text = owner_ ? owner_->modeTooltip() : std::wstring(L"WowType");
    *tooltip = SysAllocString(text.c_str());
    return *tooltip ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::OnClick(TfLBIClick click, POINT, const RECT *) {
    if (click == TF_LBI_CLK_LEFT && owner_) {
        owner_->toggleModeFromButton();
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::InitMenu(ITfMenu *menu) {
    if (!menu || !owner_) {
        return E_INVALIDARG;
    }
    for (const auto &item : owner_->menuItems()) {
        menu->AddMenuItem(item.id, item.flags, nullptr, nullptr, item.text.c_str(),
                          static_cast<ULONG>(item.text.size()), nullptr);
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::OnMenuSelect(UINT id) {
    if (owner_) {
        owner_->menuCommand(id);
    }
    return S_OK;
}

STDMETHODIMP LangBarButton::GetIcon(HICON *icon) {
    if (!icon) {
        return E_INVALIDARG;
    }
    const std::wstring label = owner_ ? owner_->modeLabel() : std::wstring(L"中");
    *icon = makeTextIcon(label.c_str());
    return *icon ? S_OK : E_FAIL;
}

STDMETHODIMP LangBarButton::GetText(BSTR *text) {
    if (!text) {
        return E_INVALIDARG;
    }
    const std::wstring label = owner_ ? owner_->modeLabel() : std::wstring(L"中");
    *text = SysAllocString(label.c_str());
    return *text ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LangBarButton::AdviseSink(REFIID riid, IUnknown *sink, DWORD *cookie) {
    if (!sink || !cookie) {
        return E_INVALIDARG;
    }
    if (!IsEqualIID(riid, __uuidof(ITfLangBarItemSink))) {
        return CONNECT_E_CANNOTCONNECT;
    }
    if (sink_) {
        return CONNECT_E_ADVISELIMIT;
    }
    if (FAILED(sink->QueryInterface(__uuidof(ITfLangBarItemSink), reinterpret_cast<void **>(&sink_)))) {
        sink_ = nullptr;
        return E_NOINTERFACE;
    }
    *cookie = kSinkCookie;
    return S_OK;
}

STDMETHODIMP LangBarButton::UnadviseSink(DWORD cookie) {
    if (cookie != kSinkCookie || !sink_) {
        return CONNECT_E_NOCONNECTION;
    }
    sink_->Release();
    sink_ = nullptr;
    return S_OK;
}

} // namespace wowtype::tsf
