#pragma once

// 打字统计的读取和派生指标（设置窗口「统计」页），和 macOS 的 Shared/StatsModel.swift（StatsStore.metrics 等）、
// core/src/stats.rs 的口径一样：速度 = 汉字和字母数 ÷ 活跃分钟，最快的一天至少要有 1 分钟活跃时间。
// 还有诊断信息里的设置文件打码（macOS 的 DiagnosticsText、Linux 设置的 mask_url）。不依赖 windows.h。

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wowtype {

struct StatsDay {
    long long chars = 0;
    long long han = 0;
    long long latin = 0;
    long long number = 0;
    long long punct = 0;
    long long emoji = 0;
    long long other = 0;
    long long activeMs = 0;
    std::map<std::string, long long> keys;
    std::map<std::string, long long> sources;
    std::array<long long, 9> selections{};
    long long beyond = 0;
    long long readable() const { return han + latin; }
};

struct StatsDocument {
    std::map<std::string, StatsDay> days; // YYYY-MM-DD → 那一天
    /// stats.json 的原文；读不出来（不是 JSON、不是对象）是空表。
    static StatsDocument parse(const std::string &json);
};

struct StatsMetrics {
    long long todayChars = 0;
    long long todayActive = 0;
    double todaySpeed = 0;
    long long totalChars = 0;
    long long totalActive = 0;
    double averageSpeed = 0;
    int recordedDays = 0;
    double averagePerDay = 0;
    std::optional<std::string> bestDay;
    long long bestChars = 0;
    std::optional<std::string> fastestDay;
    double fastestSpeed = 0;
    int streak = 0;
    int longestStreak = 0;
};

namespace stats {
StatsMetrics metrics(const StatsDocument &document, const std::string &today);
double cpm(long long characters, long long activeMs);
std::string formatActive(long long milliseconds);
std::string dayLabel(const std::string &key);
/// key 往后挪 days 天（负数往前），格式 YYYY-MM-DD；认不出原样返回。
std::string shift(const std::string &key, int days);
/// 全部日子各键按下的次数，多的在前。
std::vector<std::pair<std::string, long long>> topKeys(const StatsDocument &document, std::size_t limit);
/// 给人看的一段摘要（设置窗口「统计」页）。
std::string summary(const StatsDocument &document, const std::string &today);
} // namespace stats

namespace diagnostics {
/// 设置文件原文打码：TranslateBaseUrl 只留协议和主机；不认识的键名字里带 key、token、secret、password 的整个打码。
std::string maskConfig(const std::string &text);
/// 网址只留协议和主机：https://user:key@api.example.com/v1/x → https://api.example.com/…
std::string maskUrl(const std::string &url);
} // namespace diagnostics

} // namespace wowtype
