// 端到端打字测试（只在 Windows 上，CI 的 Windows 任务装好安装程序以后跑）：开一个带输入框的窗口，用 TSF 把这个进程的
// 输入法切到 WowType，用 SendInput 按真的键，再读输入框里的字。走的是和真实应用一样的路：系统的按键 → TSF → wowtype.dll
// （文本服务、Composer、核心的会话）→ 组字和上屏 → 输入框。另外看候选框窗口有没有出来、在不在输入框附近。
//
//   wowtype-windows-typing-test.exe
//
// 要已经注册好输入法（安装程序或 regsvr32）、装了真实词库；要有能用的桌面（GitHub 的 Windows runner 有）。
// 结果一行一个 ok / FAIL，退出码是没过的条数。

#include <windows.h>

#include <msctf.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// 和 windows/src/tsf/globals.cpp 一样
const CLSID kClsid = {0xa895b83f, 0x8f60, 0x4990, {0xa4, 0x8f, 0x17, 0x82, 0x1e, 0x3b, 0xc1, 0x4a}};
const GUID kProfile = {0x8106db80, 0x2dc6, 0x4558, {0xb3, 0x68, 0x86, 0xd2, 0x6f, 0x78, 0xec, 0x68}};
constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

HWND g_window = nullptr;
HWND g_edit = nullptr;
int g_failures = 0;

std::string narrow(const std::wstring &w) {
    if (w.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

void check(bool ok, const char *name, const std::wstring &detail = L"") {
    if (ok) {
        std::printf("ok   %s\n", name);
    } else {
        ++g_failures;
        std::printf("FAIL %s  [%s]\n", name, narrow(detail).c_str());
    }
    std::fflush(stdout);
}

/// 处理一会儿消息（输入法的编辑会话、候选框、我们自己的窗口都在这个线程上）。
void pump(DWORD ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    while (GetTickCount64() < until) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    }
}

void sendKey(WORD vk, bool up) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    SendInput(1, &input, sizeof input);
}

void tap(WORD vk) {
    sendKey(vk, false);
    pump(40);
    sendKey(vk, true);
    pump(60);
}

/// 按一串键：字母、数字、空格、逗号、句号照键盘上的键按；Shift 要的字符这里不用。
void typeKeys(const char *keys) {
    for (const char *p = keys; *p; ++p) {
        const char c = *p;
        WORD vk = 0;
        if (c >= 'a' && c <= 'z') {
            vk = static_cast<WORD>('A' + (c - 'a'));
        } else if (c >= '0' && c <= '9') {
            vk = static_cast<WORD>(c);
        } else if (c == ' ') {
            vk = VK_SPACE;
        } else if (c == ',') {
            vk = VK_OEM_COMMA;
        } else if (c == '.') {
            vk = VK_OEM_PERIOD;
        }
        tap(vk);
    }
}

std::wstring text() {
    const int n = GetWindowTextLengthW(g_edit);
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(g_edit, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(n));
    return s;
}

void clear() {
    SetWindowTextW(g_edit, L"");
    pump(100);
}

void focus() {
    // 抢到前台：从后台起的进程直接 SetForegroundWindow 常被系统拦，借前台线程的输入状态一下
    const DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD me = GetCurrentThreadId();
    if (fg != me) {
        AttachThreadInput(fg, me, TRUE);
    }
    ShowWindow(g_window, SW_SHOW);
    BringWindowToTop(g_window);
    SetForegroundWindow(g_window);
    SetFocus(g_edit);
    if (fg != me) {
        AttachThreadInput(fg, me, FALSE);
    }
    pump(200);
}

HWND candidateWindow() { return FindWindowW(L"WowTypeCandidateWindow", nullptr); }

LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // 输入法把每个按键回调记进日志：没过的时候 CI 打出来看
    SetEnvironmentVariableW(L"WOWTYPE_DEBUG_KEYS", L"1");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WowTypeTypingTest";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    g_window = CreateWindowExW(0, wc.lpszClassName, L"WowType 打字测试", WS_OVERLAPPEDWINDOW, 100, 100, 640, 240,
                               nullptr, nullptr, wc.hInstance, nullptr);
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL, 10, 10,
                             600, 160, g_window, nullptr, wc.hInstance, nullptr);
    focus();

    // 安装程序已经把 WowType 加进了当前用户的键盘列表；再加一次也没关系（runner 上可能是别的用户装的）
    {
        using InstallLayoutOrTipFn = BOOL(WINAPI *)(LPCWSTR, DWORD);
        if (HMODULE input = LoadLibraryW(L"input.dll")) {
            if (const auto install = reinterpret_cast<InstallLayoutOrTipFn>(GetProcAddress(input, "InstallLayoutOrTip"))) {
                check(install(L"0804:{A895B83F-8F60-4990-A48F-17821E3BC14A}{8106DB80-2DC6-4558-B368-86D26F78EC68}", 0) != FALSE,
                      "WowType 在键盘列表里");
            }
        }
    }
    // 这个线程上先起 TSF（输入框会用同一个线程管理器），再把这个进程的输入法切到 WowType
    ComPtr<ITfThreadMgr> threadMgr;
    TfClientId client = TF_CLIENTID_NULL;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&threadMgr)))) {
        threadMgr->Activate(&client);
    }
    ComPtr<ITfInputProcessorProfileMgr> profiles;
    HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&profiles));
    check(SUCCEEDED(hr), "建 ITfInputProcessorProfileMgr");
    if (FAILED(hr)) {
        return 1;
    }
    ComPtr<ITfInputProcessorProfiles> legacy;
    profiles.As(&legacy);

    // 诊断：装了哪些键盘布局、WowType 的配置在不在、开没开
    {
        HKL layouts[32] = {};
        const int n = GetKeyboardLayoutList(32, layouts);
        std::printf("键盘布局：");
        for (int i = 0; i < n; ++i) {
            std::printf(" %08llx", static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(layouts[i])));
        }
        std::printf("\n");
        TF_INPUTPROCESSORPROFILE info{};
        if (SUCCEEDED(profiles->GetProfile(TF_PROFILETYPE_INPUTPROCESSOR, kLangId, kClsid, kProfile, nullptr, &info))) {
            std::printf("WowType 配置：flags=%lx（1 是开着）\n", static_cast<unsigned long>(info.dwFlags));
        } else {
            std::printf("WowType 配置：取不到\n");
        }
        std::fflush(stdout);
    }

    const auto isActive = [&] {
        TF_INPUTPROCESSORPROFILE active{};
        return SUCCEEDED(profiles->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &active)) &&
               IsEqualCLSID(active.clsid, kClsid);
    };
    // 一个一个试系统给的几种切法，切上了就停
    const auto attempt = [&](const char *how, HRESULT result) {
        pump(300);
        std::printf("  %s：hr=%08lx，现在%s WowType\n", how, static_cast<unsigned long>(result), isActive() ? "是" : "不是");
        std::fflush(stdout);
        return isActive();
    };
    bool switched = attempt("ActivateProfile", profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, kLangId, kClsid,
                                                                          kProfile, nullptr,
                                                                          TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE |
                                                                              TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE));
    if (!switched && legacy) {
        legacy->EnableLanguageProfile(kClsid, kLangId, kProfile, TRUE);
        legacy->ChangeCurrentLanguage(kLangId);
        switched = attempt("ActivateLanguageProfile", legacy->ActivateLanguageProfile(kClsid, kLangId, kProfile));
    }
    if (!switched) {
        // 先把这个进程的键盘布局换成简体中文，再切
        const HKL chinese = LoadKeyboardLayoutW(L"00000804", KLF_ACTIVATE | KLF_SETFORPROCESS);
        std::printf("  LoadKeyboardLayout(00000804)：%p\n", static_cast<void *>(chinese));
        switched = attempt("换了布局以后 ActivateProfile",
                           profiles->ActivateProfile(TF_PROFILETYPE_INPUTPROCESSOR, kLangId, kClsid, kProfile, nullptr,
                                                     TF_IPPMF_FORPROCESS | TF_IPPMF_ENABLEPROFILE));
    }
    check(switched, "切到 WowType");
    focus();

    // 词库在后台载入：先按一个拼音试试，出来的还是字母就是还没载好，等一会儿再试（最多 60 秒）
    bool loaded = false;
    for (int attempt = 0; attempt < 30 && !loaded; ++attempt) {
        clear();
        focus();
        typeKeys("nihao ");
        pump(300);
        loaded = text() == L"你好";
        if (!loaded) {
            pump(2000);
        }
    }
    check(loaded, "nihao 空格 → 你好（词库载好、按键经过 TSF、上屏到输入框）", text());

    // 打字途中有编辑区、有候选框，候选框在输入框附近
    clear();
    focus();
    typeKeys("shi");
    pump(300);
    HWND candidates = candidateWindow();
    const bool shown = candidates && IsWindowVisible(candidates);
    check(shown, "打字途中候选框出来了");
    if (shown) {
        RECT box{};
        RECT edit{};
        GetWindowRect(candidates, &box);
        GetWindowRect(g_edit, &edit);
        const bool nearby = box.top >= edit.top - 400 && box.top <= edit.bottom + 400 && box.left >= edit.left - 400 &&
                          box.left <= edit.right + 400;
        check(nearby, "候选框在输入框附近",
              std::to_wstring(box.left) + L"," + std::to_wstring(box.top) + L" / " + std::to_wstring(edit.left) + L"," +
                  std::to_wstring(edit.top));
    }
    tap('2');
    pump(300);
    check(text().size() == 1 && text() != L"s", "数字 2 选第二个候选", text());
    check(!candidateWindow() || !IsWindowVisible(candidateWindow()), "上屏以后候选框收起");

    clear();
    focus();
    typeKeys("nihao,");
    pump(300);
    check(text() == L"你好，", "打字途中按逗号：先上屏候选再上屏中文逗号", text());
    typeKeys(".");
    pump(200);
    check(text() == L"你好，。", "没在打字时句号是中文句号", text());

    clear();
    focus();
    typeKeys("nihao");
    tap(VK_RETURN);
    pump(300);
    check(text() == L"nihao", "回车上屏原始字母", text());

    clear();
    focus();
    typeKeys("nihh");
    tap(VK_BACK);
    typeKeys("ao ");
    pump(300);
    check(text() == L"你好", "退格删一个字母再接着打", text());

    clear();
    focus();
    typeKeys("nih");
    tap(VK_ESCAPE);
    pump(300);
    check(text().empty(), "Esc 清空不上屏", text());

    // 单按 Shift 切英文：字母原样进输入框；再按一下切回中文
    clear();
    focus();
    tap(VK_LSHIFT);
    pump(300);
    typeKeys("abc ");
    pump(200);
    check(text() == L"abc ", "单按 Shift 切英文，字母原样上屏", text());
    tap(VK_LSHIFT);
    pump(300);
    typeKeys("nihao ");
    pump(300);
    check(text() == L"abc 你好", "再单按 Shift 切回中文", text());

    // 打到一半点了别处（失去焦点）：打的字母原样上屏（和 macOS 一样）
    clear();
    focus();
    typeKeys("ceshi");
    pump(200);
    SetFocus(nullptr);
    pump(400);
    SetFocus(g_edit);
    pump(200);
    check(text() == L"ceshi" || text() == L"测试", "打到一半失去焦点：不会留下编辑区", text());

    DestroyWindow(g_window);
    pump(200);
    std::printf(g_failures ? "\n%d 项没过\n" : "\nwindows typing ok\n", g_failures);
    CoUninitialize();
    return g_failures;
}
