#pragma once

// 编辑会话：TSF 只在编辑会话里让输入法读写应用的文字。把要做的事包成一个函数交给 RequestEditSession。

#include <windows.h>

#include <msctf.h>

#include <functional>
#include <new>

#include "globals.h"

namespace wowtype::tsf {

class EditSession final : public ITfEditSession {
public:
    using Body = std::function<HRESULT(TfEditCookie)>;

    explicit EditSession(Body body) : body_(std::move(body)) { dllAddRef(); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) {
            return E_INVALIDARG;
        }
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, __uuidof(ITfEditSession))) {
            *ppv = static_cast<ITfEditSession *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) {
            delete this;
        }
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return body_ ? body_(ec) : S_OK; }

    /// 在 context 上跑 body。flags 是 TF_ES_*（TF_ES_READ / TF_ES_READWRITE，加 TF_ES_SYNC 或
    /// TF_ES_ASYNCDONTCARE）。返回 RequestEditSession 的结果；sessionResult 是 body 的返回值（同步跑了的话）。
    static HRESULT run(ITfContext *context, TfClientId client, DWORD flags, Body body, HRESULT *sessionResult = nullptr) {
        if (!context) {
            return E_INVALIDARG;
        }
        auto *session = new (std::nothrow) EditSession(std::move(body));
        if (!session) {
            return E_OUTOFMEMORY;
        }
        HRESULT result = E_FAIL;
        const HRESULT hr = context->RequestEditSession(client, session, flags, &result);
        session->Release();
        if (sessionResult) {
            *sessionResult = result;
        }
        return hr;
    }

private:
    ~EditSession() { dllRelease(); }
    LONG refs_ = 1;
    Body body_;
};

} // namespace wowtype::tsf
