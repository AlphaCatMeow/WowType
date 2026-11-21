// 下载续传的测试（CI 配 tests/range_server.py 跑）：调设置窗口下载模型、新版安装程序用的同一个 win::httpDownload。
//
//   wowtype-windows-download-test <网址> <存到> <要的 SHA256>
//
// 下好了而且 SHA256 对得上退出码是 0；没下完是 1（.part 留着，再跑一次接着下）；下好了内容不对是 2。

#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/common/utf.h"
#include "../src/win/updater.h"

int wmain(int argc, wchar_t **argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc != 4) {
        std::printf("用法：wowtype-windows-download-test <网址> <存到> <SHA256>\n");
        return 3;
    }
    const std::wstring path = argv[2];
    const std::wstring wantW = argv[3];
    const std::string want = wowtype::utf16To8(std::u16string(wantW.begin(), wantW.end()));
    std::string error;
    unsigned long long first = ~0ULL;
    unsigned long long last = 0;
    unsigned long long total = 0;
    const bool ok = wowtype::win::httpDownload(
        argv[1], path,
        [&](unsigned long long done, unsigned long long all) {
            if (first == ~0ULL) {
                first = done;
            }
            last = done;
            total = all;
        },
        &error);
    std::printf("从 %llu 开始，下到 %llu / %llu\n", first == ~0ULL ? 0ULL : first, last, total);
    if (!ok) {
        std::printf("没下完：%s\n", error.c_str());
        return 1;
    }
    const std::string got = wowtype::win::sha256File(path);
    if (got != want) {
        std::printf("FAIL 内容不对：%s，要 %s\n", got.c_str(), want.c_str());
        return 2;
    }
    std::printf("ok   下好了，SHA256 对得上\n");
    return 0;
}
