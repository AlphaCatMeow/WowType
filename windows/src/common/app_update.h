#pragma once

// 检查更新的规矩（Windows）：发布页的 tag 怎么变成版本号、Windows 安装程序叫什么、什么时候算有新版、多久查一次。
// 和 macOS 的 Shared/AppUpdate.swift、LexiconPack.compareVersions / checksums 一条一条对着写（Linux 设置的 parse_version、
// pick_release 也是这套规矩）。网络、下载、运行安装程序在设置窗口和输入法里，这里不依赖 windows.h，测试在 Linux 上也跑。

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wowtype::update {

inline constexpr char kReleasesPage[] = "https://github.com/Gloridust/WowType/releases";
inline constexpr char kLatestApi[] = "https://api.github.com/repos/Gloridust/WowType/releases/latest";
inline constexpr char kListApi[] = "https://api.github.com/repos/Gloridust/WowType/releases?per_page=30";
inline constexpr char kChecksumFile[] = "SHA256SUMS";
/// 输入法最多隔这么久查一次（秒）：一天
inline constexpr int64_t kCheckInterval = 24 * 3600;

/// tag（v0.4.1 或 0.4.1）里的版本号；不像版本号的是 nullopt（和 install.sh 一样）。
std::optional<std::string> version(const std::string &tag);
/// 发布页上的 Windows 安装程序：WowType-<版本>-windows-<架构>-setup.exe
std::string packageName(const std::string &version, const std::string &arch);
/// 正式版的版本号（数字开头）；开发时编出来的是 dev
bool isRelease(const std::string &version);
/// 按数字段逐段比（0.10.0 > 0.9.9），返回负数、0、正数
int compareVersions(const std::string &a, const std::string &b);
/// latest 比现在装的 current 新。current 不是正式版（dev）时不算
bool isNewer(const std::string &latest, const std::string &current);
/// v0.5.0 这样的 tag（v 加数字和点）
bool isAppTag(const std::string &tag);

struct Release {
    std::string tag;
    std::string notes;
    bool prerelease = false;
    bool draft = false;
    std::map<std::string, std::string> assets; // 文件名 → 下载地址
};
/// GitHub 发布页的 JSON（releases/latest 的一个对象）；读不出来是 nullopt。
std::optional<Release> parseRelease(const std::string &json);
/// 发布列表（releases?per_page=30 的数组）。
std::vector<Release> parseReleaseList(const std::string &json);
/// Beta 计划：列表里版本号最大的那个的下标，预发布也算，一样大时要正式版；只看 v 加数字的 tag，草稿不算。
std::optional<std::size_t> pickNewest(const std::vector<Release> &releases);

/// SHA256SUMS 里 name 的校验值（小写）；没有、重复、写坏了返回 nullopt，error 是原因。
std::optional<std::string> checksum(const std::string &sums, const std::string &name, std::string *error = nullptr);

/// 续传（下载断了从断的地方接着下）：服务器回 206 时 Content-Range 头（"bytes 100-199/1000"）里的起点和总长。
/// 总长是 * 时 total 为 0。看不懂是 nullopt。
struct ContentRange {
    unsigned long long start = 0;
    unsigned long long total = 0;
};
std::optional<ContentRange> parseContentRange(const std::string &header);

/// 数据目录里 update-state 记的：上次查成功的时间（Unix 秒）、那时发布页上最新的 tag。输入法和设置窗口都读写它。
struct State {
    int64_t checked = 0;
    std::string latest;
    static State parse(const std::string &text);
    std::string text() const;
    bool operator==(const State &o) const { return checked == o.checked && latest == o.latest; }
};
/// 该查了：上次查过一天了（时钟往回拨了也算）
bool due(const State &state, int64_t now);

/// 发布说明里只留「## 这一版」那一节；没有这一节就整个原样。
std::string highlights(const std::string &notes);

} // namespace wowtype::update
