#pragma once

// TSF 文本服务：每个界面线程一个。按键翻译成 KeyInput 交给 Composer（和 macOS 的 IMK 输入控制器对应），
// Composer 通过 TextClient 回来写字、画候选框。
//
// TSF 只在编辑会话里让输入法改应用的文字，而且按键先「问」（OnTestKeyDown，要不要这个键）后「给」
// （OnKeyDown）。会话处理按键是有副作用的，没法只问不做：所以在 OnTestKeyDown 里就交给 Composer 处理，
// 记下结果；紧接着来的 OnKeyDown 照记下的答，不再处理一遍。Composer 要做的改动（上屏、编辑区、挪光标）
// 先排进 ops_，处理完一个键在一个编辑会话里按顺序做掉，顺便摆候选框的位置（要编辑会话才问得到字在屏幕哪里）。

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../common/composer.h"
#include "candidate_window.h"

namespace wowtype::tsf {

class LangBarButton;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfThreadMgrEventSink,
                          public ITfKeyEventSink,
                          public ITfCompositionSink,
                          public ITfThreadFocusSink,
                          public ITfDisplayAttributeProvider,
                          public TextClient {
public:
    TextService();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfTextInputProcessor(Ex)
    STDMETHODIMP Activate(ITfThreadMgr *threadMgr, TfClientId clientId) override;
    STDMETHODIMP ActivateEx(ITfThreadMgr *threadMgr, TfClientId clientId, DWORD flags) override;
    STDMETHODIMP Deactivate() override;

    // ITfThreadMgrEventSink
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr *) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr *) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr *focus, ITfDocumentMgr *previous) override;
    STDMETHODIMP OnPushContext(ITfContext *) override { return S_OK; }
    STDMETHODIMP OnPopContext(ITfContext *) override { return S_OK; }

    // ITfKeyEventSink
    STDMETHODIMP OnSetFocus(BOOL) override { return S_OK; }
    STDMETHODIMP OnTestKeyDown(ITfContext *context, WPARAM wParam, LPARAM lParam, BOOL *eaten) override;
    STDMETHODIMP OnTestKeyUp(ITfContext *context, WPARAM wParam, LPARAM lParam, BOOL *eaten) override;
    STDMETHODIMP OnKeyDown(ITfContext *context, WPARAM wParam, LPARAM lParam, BOOL *eaten) override;
    STDMETHODIMP OnKeyUp(ITfContext *context, WPARAM wParam, LPARAM lParam, BOOL *eaten) override;
    STDMETHODIMP OnPreservedKey(ITfContext *, REFGUID, BOOL *eaten) override {
        *eaten = FALSE;
        return S_OK;
    }

    // ITfCompositionSink
    STDMETHODIMP OnCompositionTerminated(TfEditCookie ec, ITfComposition *composition) override;

    // ITfThreadFocusSink
    STDMETHODIMP OnSetThreadFocus() override;
    STDMETHODIMP OnKillThreadFocus() override;

    // ITfDisplayAttributeProvider
    STDMETHODIMP EnumDisplayAttributeInfo(IEnumTfDisplayAttributeInfo **enumInfo) override;
    STDMETHODIMP GetDisplayAttributeInfo(REFGUID guid, ITfDisplayAttributeInfo **info) override;

    // TextClient（Composer 回调）
    void insertText(const std::string &text) override;
    void setMarkedText(const std::string &text, int cursorBytes) override;
    void showCandidates(const CandidatePage *page) override;
    uint32_t previousCharacter() override;
    bool previousCharacterIsReliable() override { return around_.known; }
    bool allowsTranslation() override { return !around_.password; }
    uint32_t inputHints() override;
    bool canEditAroundCaret() override { return around_.known && around_.editable; }
    std::optional<uint32_t> nextCharacter() override { return around_.known ? around_.next : std::nullopt; }
    bool moveCaret(int delta) override;
    bool deletePair() override;

    // 任务栏按钮（langbar.cpp）
    std::wstring modeLabel();
    std::wstring modeTooltip();
    void toggleModeFromButton();
    struct MenuItem {
        UINT id;
        DWORD flags;
        std::wstring text;
    };
    std::vector<MenuItem> menuItems();
    void menuCommand(UINT id);

private:
    ~TextService();

    /// 编辑会话里要做的一件事（按顺序做）。
    struct Op {
        enum class Kind { Insert, Mark, MoveCaret, DeletePair } kind;
        std::wstring text;
        int value = 0; // Mark：光标（UTF-16）；MoveCaret：挪几格
    };
    /// 按键之前读到的应用里的情况（同步的只读编辑会话）。读不到时 known 是 false。
    struct Surroundings {
        bool known = false;
        uint32_t prev = 0;
        std::optional<uint32_t> next;
        bool editable = false;
        bool password = false;
        bool noAutoSpace = false;
    };
    struct PendingKey {
        WPARAM wParam;
        LPARAM lParam;
    };

    void sync();
    bool processKeyDown(ITfContext *context, WPARAM wParam, LPARAM lParam);
    void processKeyUp(ITfContext *context, WPARAM wParam, LPARAM lParam);
    void readSurroundings(ITfContext *context);
    /// 把排着的 ops_ 和候选框交给一个编辑会话做掉（能同步就同步）。
    void flush(ITfContext *context);
    void applyOps(TfEditCookie ec, ITfContext *context);
    bool startComposition(TfEditCookie ec, ITfContext *context);
    void commitText(TfEditCookie ec, ITfContext *context, const std::wstring &text);
    void setComposition(TfEditCookie ec, ITfContext *context, const std::wstring &text, int cursor);
    void applyCaretMove(TfEditCookie ec, ITfContext *context, int delta);
    void applyDeletePair(TfEditCookie ec, ITfContext *context);
    void setAttribute(TfEditCookie ec, ITfContext *context, ITfRange *range, bool on);
    void updateWindow(TfEditCookie ec, ITfContext *context);
    /// 结束组字：打的字母原样上屏（换输入框、点了别处、切走输入法），和 macOS 一样。
    void endComposition();
    void modeChanged();
    void showModeHint();
    void onTimer(UINT_PTR id);
    bool keyboardDisabled(ITfContext *context);
    bool keyboardOpen();
    void setKeyboardOpen(bool open);
    ITfContext *currentContext();

    LONG refs_ = 1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> threadMgr_;
    TfClientId clientId_ = TF_CLIENTID_NULL;
    DWORD threadMgrSinkCookie_ = TF_INVALID_COOKIE;
    DWORD threadFocusSinkCookie_ = TF_INVALID_COOKIE;
    bool keySinkAdvised_ = false;
    TfGuidAtom inputAttribute_ = TF_INVALID_GUIDATOM;

    Microsoft::WRL::ComPtr<ITfComposition> composition_;
    Microsoft::WRL::ComPtr<ITfContext> compositionContext_;
    Microsoft::WRL::ComPtr<ITfContext> focusContext_;
    /// OnCompositionTerminated 里：组字已经被应用结束了，只能改它的字，不能再 EndComposition
    bool terminating_ = false;

    std::unique_ptr<Composer> composer_;
    std::unique_ptr<CandidateWindow> window_;
    LangBarButton *langBar_ = nullptr;
    uint64_t configGeneration_ = 0;

    std::vector<Op> ops_;
    std::optional<CandidatePage> page_;
    bool pageDirty_ = false;
    bool modeHint_ = false;
    RECT anchor_{};
    Surroundings around_;
    /// 最近上屏或交给应用的最后一个字。读不到光标前的字时用它兜底（和 macOS 一样）。
    uint32_t lastInserted_ = 0;
    std::optional<PendingKey> pendingDown_;
    std::optional<PendingKey> handledUp_;
};

} // namespace wowtype::tsf
