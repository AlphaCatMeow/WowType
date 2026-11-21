#pragma once

// 任务栏输入指示器上的「中 / 英 / 译」按钮（GUID_LBI_INPUTMODE）：左键点切中英文，右键菜单切模式、
// 打开设置文件和数据目录（这一阶段还没有设置界面）。和 macOS 菜单栏的输入法菜单、fcitx5 托盘对应。

#include <windows.h>

#include <msctf.h>
#include <ctfutb.h>

namespace wowtype::tsf {

class TextService;

class LangBarButton final : public ITfLangBarItemButton, public ITfSource {
public:
    explicit LangBarButton(TextService *owner);

    /// 文本服务停用时调：之后点按钮什么也不做。
    void detach() { owner_ = nullptr; }
    /// 模式变了：让任务栏重新取图标、文字。
    void update();

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfLangBarItem
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO *info) override;
    STDMETHODIMP GetStatus(DWORD *status) override;
    STDMETHODIMP Show(BOOL show) override;
    STDMETHODIMP GetTooltipString(BSTR *tooltip) override;

    // ITfLangBarItemButton
    STDMETHODIMP OnClick(TfLBIClick click, POINT pt, const RECT *area) override;
    STDMETHODIMP InitMenu(ITfMenu *menu) override;
    STDMETHODIMP OnMenuSelect(UINT id) override;
    STDMETHODIMP GetIcon(HICON *icon) override;
    STDMETHODIMP GetText(BSTR *text) override;

    // ITfSource
    STDMETHODIMP AdviseSink(REFIID riid, IUnknown *sink, DWORD *cookie) override;
    STDMETHODIMP UnadviseSink(DWORD cookie) override;

private:
    ~LangBarButton();
    LONG refs_ = 1;
    TextService *owner_;
    ITfLangBarItemSink *sink_ = nullptr;
};

/// 画一个写着 text 的小图标（任务栏用，跟着任务栏的深浅色）。调用的人负责 DestroyIcon。
HICON makeTextIcon(const wchar_t *text);

} // namespace wowtype::tsf
