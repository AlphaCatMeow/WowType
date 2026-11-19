#pragma once

// 检查更新的规矩（Windows）：发布页的 tag 怎么变成版本号、Windows 安装程序叫什么、什么时候算有新版、多久查一次。
// 和 macOS 的 Shared/AppUpdate.swift、LexiconPack.compareVersions / checksums 一条一条对着写（Linux 设置的 parse_version、
// pick_release 也是这套规矩）。网络、下载、运行安装程序在设置窗口和输入法里，这里不依赖 windows.h，测试在 Linux 上也跑。

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wowtype::update {

inline constexpr char kReleasesPage[] = "https://github.com/Gloridust/WowType/releases";
inline constexpr char kLatestApi[] = "https://api.github.com/repos/Gloridust/WowType/releases/latest";
inline constexpr char kListApi[] = "https://api.github.com/repos/Gloridust/WowType/releases?per_page=30";
inline constexpr char kChecksumFile[] = "SHA256SUMS";
// 接口限流时（不登录每个 IP 每小时 60 次：校园网、公司网、运营商 NAT 后面很多人共用一个 IP）改走网页：
// releases/latest 跳到最新正式版的 tag 页，releases.atom 列着最近的发布，发布页的 SHA256SUMS 列着这一版的全部文件。
// 这些都不算接口次数。和 Linux 设置的 fetch_latest_web、macOS 的 AppUpdate.webCandidates 一样
inline constexpr char kRepoPage[] = "https://github.com/Gloridust/WowType";
inline constexpr char kLatestPage[] = "https://github.com/Gloridust/WowType/releases/latest";
inline constexpr char kAtomFeed[] = "https://github.com/Gloridust/WowType/releases.atom";
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

/// 发布页上 tag 这一版的文件 file 的下载地址、tag 页的地址
std::string downloadUrl(const std::string &tag, const std::string &file);
std::string tagPage(const std::string &tag);
/// releases/latest 跳到的地址（…/releases/tag/v0.6.0）里的 tag；不是这个样子是 nullopt
std::optional<std::string> tagFromLatestUrl(const std::string &url);
/// 网页上要按顺序试的 tag：最新正式版，Beta 计划开着时加上 releases.atom（feed）里的版本；只要 v 加数字的，
/// 版本号大的在前，一样大时正式版在前。atom 里也有只推了 tag、还没发出来的版本，调用方遇到 404 往下试
std::vector<std::string> webCandidates(const std::optional<std::string> &latest, const std::optional<std::string> &feed);
/// SHA256SUMS 里列的文件名（「64 位十六进制  文件名」，文件名前可以带 *，带 / 的不要）
std::vector<std::string> assetNames(const std::string &sums);
/// 不走接口拼出的一版：文件列表从 SHA256SUMS 来，下载地址按发布页的规则拼，更新说明是一句「见发布页」
Release webRelease(const std::string &tag, const std::string &sums, bool prerelease);

/// 一次 HTTP GET 的结果（联网在外面做）：status 0 是连不上（error 是原因）；finalUrl 是跟了跳转以后的地址
struct HttpResult {
    int status = 0;
    std::string body;
    std::string finalUrl;
    std::string error;
};
using Fetch = std::function<HttpResult(const std::string &url)>;
/// 接口限流：403、429
inline bool rateLimited(int status) { return status == 403 || status == 429; }
/// 查发布页上的最新版（beta：Beta 计划，预发布也算）。先问接口；接口限流时改走网页（只是拿不到更新说明），
/// 别的错照旧报。查不到是 nullopt，error 是给人看的原因
std::optional<Release> fetchLatest(const Fetch &get, bool beta, std::string *error);

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
    /// 上次试着查的时间（成没成都记）：Windows 上每个应用进程各有一个输入法，查不成时靠它让所有进程合起来
    /// 一小时最多试一次，不会每个进程各问一遍
    int64_t tried = 0;
    static State parse(const std::string &text);
    std::string text() const;
    bool operator==(const State &o) const { return checked == o.checked && latest == o.latest && tried == o.tried; }
};
/// 查不成以后多久再试（秒）：一小时
inline constexpr int64_t kRetryInterval = 3600;
/// 该查了：上次查成功过了一天（时钟往回拨了也算），而且上次试着查过了一小时
bool due(const State &state, int64_t now);

/// 发布说明里只留「## 这一版」那一节；没有这一节就整个原样。
std::string highlights(const std::string &notes);

} // namespace wowtype::update
