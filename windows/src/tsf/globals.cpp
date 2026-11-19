#include "globals.h"

#include <shlobj.h>

#include <cstdio>
#include <mutex>

#include "../common/utf.h"

namespace wowtype::tsf {

HINSTANCE g_module = nullptr;

namespace {
LONG g_refs = 0;
}

void dllAddRef() { InterlockedIncrement(&g_refs); }
void dllRelease() { InterlockedDecrement(&g_refs); }
LONG dllRefCount() { return g_refs; }

// {A895B83F-8F60-4990-A48F-17821E3BC14A}
const CLSID kClsidTextService = {0xa895b83f, 0x8f60, 0x4990, {0xa4, 0x8f, 0x17, 0x82, 0x1e, 0x3b, 0xc1, 0x4a}};
// {8106DB80-2DC6-4558-B368-86D26F78EC68}
const GUID kGuidProfile = {0x8106db80, 0x2dc6, 0x4558, {0xb3, 0x68, 0x86, 0xd2, 0x6f, 0x78, 0xec, 0x68}};
// {8D411575-DB5C-490B-9E32-3A939418E0FA}
const GUID kGuidDisplayAttributeInput = {0x8d411575, 0xdb5c, 0x490b, {0x9e, 0x32, 0x3a, 0x93, 0x94, 0x18, 0xe0, 0xfa}};
// {2C77A81E-41CC-4178-A3A7-5F8A987568E6}：GUID_LBI_INPUTMODE（Windows 8 起任务栏的输入指示器显示这个按钮）
const GUID kGuidLbiInputMode = {0x2c77a81e, 0x41cc, 0x4178, {0xa3, 0xa7, 0x5f, 0x8a, 0x98, 0x75, 0x68, 0xe6}};
// {1713DD5A-68E7-4A5B-9AF6-592A595C778D}：GUID_PROP_INPUTSCOPE
const GUID kGuidPropInputScope = {0x1713dd5a, 0x68e7, 0x4a5b, {0x9a, 0xf6, 0x59, 0x2a, 0x59, 0x5c, 0x77, 0x8d}};

std::wstring modulePath() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(g_module, buffer, static_cast<DWORD>(std::size(buffer)));
    return std::wstring(buffer, n);
}

std::wstring moduleDir() {
    std::wstring path = modulePath();
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring widen(const std::string &utf8) {
    const std::u16string u = utf8To16(utf8);
    return std::wstring(u.begin(), u.end());
}

std::string narrow(const std::wstring &wide) {
    return utf16To8(std::u16string(wide.begin(), wide.end()));
}

void log(const std::wstring &line) {
    OutputDebugStringW((L"WowType: " + line + L"\n").c_str());
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    wchar_t *local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        return;
    }
    std::wstring path = std::wstring(local) + L"\\WowType";
    CoTaskMemFree(local);
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\wowtype.log";
    WIN32_FILE_ATTRIBUTE_DATA info{};
    const bool big = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) && info.nFileSizeLow > (1u << 20);
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), big ? L"wb" : L"ab") != 0 || !f) {
        return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    char stamp[64];
    std::snprintf(stamp, sizeof stamp, "%04d-%02d-%02d %02d:%02d:%02d [%lu] ", t.wYear, t.wMonth, t.wDay, t.wHour,
                  t.wMinute, t.wSecond, GetCurrentProcessId());
    const std::string text = stamp + narrow(line) + "\n";
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

} // namespace wowtype::tsf
