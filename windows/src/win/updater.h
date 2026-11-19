#pragma once

// Windows 上检查更新、下载新版安装程序（输入法和设置窗口共用）。规矩在 common/app_update（和 macOS 的 AppUpdate 一样），
// 这里只管联网（WinHTTP，不走 IE 的缓存）、读写数据目录里的 update-state、本机是什么架构。
//
// 输入法每天最多查一次 GitHub 的发布页（后台线程，不挡打字），有新版时任务栏菜单多一项；设置窗口「常规」页能查、
// 下载 WowType-<版本>-windows-<架构>-setup.exe、按同一页的 SHA256SUMS 校验、运行它（系统问一次管理员权限）。

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

#include "../common/app_update.h"

namespace wowtype::win {

/// 这个程序是哪个架构编的（安装程序的名字里用）：x64 或 arm64。
const char *arch();

/// GET 一个小文件（发布页的 JSON、SHA256SUMS），返回内容；失败返回 nullopt，error 是给人看的原因。
std::optional<std::string> httpGet(const std::wstring &url, std::string *error);
/// 下载到 path（先写 path.part，下完改名）；progress(已下, 总共) 每收到一块调一次（总共可能是 0）。
bool httpDownload(const std::wstring &url, const std::wstring &path,
                  const std::function<void(unsigned long long, unsigned long long)> &progress, std::string *error);

/// 发布页上的最新版（beta：Beta 计划，预发布也算）。查到了顺手记进 update-state。
std::optional<update::Release> fetchLatest(const std::wstring &dataDir, bool beta, std::string *error);

update::State readState(const std::wstring &dataDir);
void writeState(const std::wstring &dataDir, const update::State &state);

/// 文件的 SHA256（小写十六进制）；读不了是空串。
std::string sha256File(const std::wstring &path);

} // namespace wowtype::win
