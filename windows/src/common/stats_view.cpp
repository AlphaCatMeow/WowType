#include "stats_view.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <set>

#include "config.h"

namespace wowtype {

namespace {

// ---------------------------------------------------------------- 够用的 JSON（只认 stats.json 用到的）

struct Json {
    enum class Kind { Null, Number, String, Array, Object, Other } kind = Kind::Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    const Json *field(const std::string &name) const {
        const auto it = fields.find(name);
        return it == fields.end() ? nullptr : &it->second;
    }
};

class Parser {
public:
    explicit Parser(const std::string &s) : s_(s) {}

    bool value(Json &out, int depth = 0) {
        if (depth > 32) {
            return false;
        }
        space();
        if (i_ >= s_.size()) {
            return false;
        }
        const char c = s_[i_];
        if (c == '{') {
            ++i_;
            out.kind = Json::Kind::Object;
            space();
            if (peek('}')) {
                ++i_;
                return true;
            }
            while (true) {
                space();
                Json key;
                if (!string(key.text)) {
                    return false;
                }
                space();
                if (!peek(':')) {
                    return false;
                }
                ++i_;
                Json item;
                if (!value(item, depth + 1)) {
                    return false;
                }
                out.fields[key.text] = std::move(item);
                space();
                if (peek(',')) {
                    ++i_;
                    continue;
                }
                if (peek('}')) {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++i_;
            out.kind = Json::Kind::Array;
            space();
            if (peek(']')) {
                ++i_;
                return true;
            }
            while (true) {
                Json item;
                if (!value(item, depth + 1)) {
                    return false;
                }
                out.items.push_back(std::move(item));
                space();
                if (peek(',')) {
                    ++i_;
                    continue;
                }
                if (peek(']')) {
                    ++i_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            out.kind = Json::Kind::String;
            return string(out.text);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = i_;
            while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || s_[i_] == '-' ||
                                      s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E')) {
                ++i_;
            }
            out.kind = Json::Kind::Number;
            out.number = std::strtod(s_.substr(start, i_ - start).c_str(), nullptr);
            return true;
        }
        for (const char *word : {"true", "false", "null"}) {
            const std::string w(word);
            if (s_.compare(i_, w.size(), w) == 0) {
                i_ += w.size();
                out.kind = Json::Kind::Other;
                return true;
            }
        }
        return false;
    }

private:
    void space() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
    }
    bool peek(char c) const { return i_ < s_.size() && s_[i_] == c; }
    bool string(std::string &out) {
        if (!peek('"')) {
            return false;
        }
        ++i_;
        while (i_ < s_.size()) {
            const char c = s_[i_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\' && i_ < s_.size()) {
                const char e = s_[i_++];
                switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'u': i_ = std::min(s_.size(), i_ + 4); break; // 键名、日期里不会有，跳过
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    const std::string &s_;
    std::size_t i_ = 0;
};

long long integer(const Json *v) {
    if (!v || v->kind != Json::Kind::Number) {
        return 0;
    }
    return std::max(0LL, static_cast<long long>(v->number));
}

std::map<std::string, long long> counts(const Json *v) {
    std::map<std::string, long long> out;
    if (v && v->kind == Json::Kind::Object) {
        for (const auto &[name, item] : v->fields) {
            out[name] = integer(&item);
        }
    }
    return out;
}

// ---------------------------------------------------------------- 日期（YYYY-MM-DD ⇄ 天数）

long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

void civilFromDays(long long z, int &y, unsigned &m, unsigned &d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long yy = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(yy + (m <= 2));
}

bool parseDay(const std::string &key, int &y, unsigned &m, unsigned &d) {
    return key.size() == 10 && std::sscanf(key.c_str(), "%4d-%2u-%2u", &y, &m, &d) == 3 && m >= 1 && m <= 12 &&
           d >= 1 && d <= 31;
}

std::string number(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

} // namespace

StatsDocument StatsDocument::parse(const std::string &text) {
    StatsDocument doc;
    Json root;
    Parser parser(text);
    if (!parser.value(root) || root.kind != Json::Kind::Object) {
        return doc;
    }
    const Json *days = root.field("days");
    if (!days || days->kind != Json::Kind::Object) {
        return doc;
    }
    for (const auto &[key, value] : days->fields) {
        if (value.kind != Json::Kind::Object) {
            continue;
        }
        StatsDay day;
        day.chars = integer(value.field("chars"));
        if (const Json *kinds = value.field("kinds")) {
            day.han = integer(kinds->field("han"));
            day.latin = integer(kinds->field("latin"));
            day.number = integer(kinds->field("number"));
            day.punct = integer(kinds->field("punct"));
            day.emoji = integer(kinds->field("emoji"));
            day.other = integer(kinds->field("other"));
        }
        day.activeMs = integer(value.field("activeMs"));
        day.keys = counts(value.field("keys"));
        day.sources = counts(value.field("sources"));
        if (const Json *ranks = value.field("selections"); ranks && ranks->kind == Json::Kind::Array) {
            for (std::size_t i = 0; i < ranks->items.size() && i < 9; ++i) {
                day.selections[i] = integer(&ranks->items[i]);
            }
        }
        day.beyond = integer(value.field("beyond"));
        doc.days[key] = day;
    }
    return doc;
}

namespace stats {

double cpm(long long characters, long long activeMs) {
    if (characters <= 0 || activeMs <= 0) {
        return 0;
    }
    return static_cast<double>(characters) / (static_cast<double>(activeMs) / 60000.0);
}

std::string formatActive(long long ms) {
    if (ms <= 0) {
        return "0分";
    }
    const long long minutes = ms / 60000;
    if (minutes == 0) {
        return std::to_string(std::max(1LL, (ms + 500) / 1000)) + "秒";
    }
    const long long hours = minutes / 60;
    const long long rest = minutes % 60;
    if (hours == 0) {
        return std::to_string(rest) + "分";
    }
    return rest == 0 ? std::to_string(hours) + "小时" : std::to_string(hours) + "小时" + std::to_string(rest) + "分";
}

std::string dayLabel(const std::string &key) {
    int y = 0;
    unsigned m = 0;
    unsigned d = 0;
    if (!parseDay(key, y, m, d)) {
        return key;
    }
    return std::to_string(m) + "月" + std::to_string(d) + "日";
}

std::string shift(const std::string &key, int days) {
    int y = 0;
    unsigned m = 0;
    unsigned d = 0;
    if (!parseDay(key, y, m, d)) {
        return key;
    }
    civilFromDays(daysFromCivil(y, m, d) + days, y, m, d);
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u", y, m, d);
    return buffer;
}

StatsMetrics metrics(const StatsDocument &document, const std::string &today) {
    StatsMetrics out;
    out.recordedDays = static_cast<int>(document.days.size());
    long long totalReadable = 0;
    for (const auto &[key, day] : document.days) {
        out.totalChars += day.chars;
        if (day.activeMs > 0) {
            out.totalActive += day.activeMs;
            totalReadable += day.readable();
        }
        if (day.chars > out.bestChars) {
            out.bestChars = day.chars;
            out.bestDay = key;
        }
        // 一天里最快的速度至少要有 1 分钟活跃时间，免得几秒钟的输入霸榜
        if (day.activeMs >= 60000) {
            const double speed = cpm(day.readable(), day.activeMs);
            if (speed > out.fastestSpeed) {
                out.fastestSpeed = speed;
                out.fastestDay = key;
            }
        }
    }
    const auto it = document.days.find(today);
    if (it != document.days.end()) {
        out.todayChars = it->second.chars;
        out.todayActive = it->second.activeMs;
        out.todaySpeed = cpm(it->second.readable(), it->second.activeMs);
    }
    out.averageSpeed = cpm(totalReadable, out.totalActive);
    out.averagePerDay = document.days.empty() ? 0 : static_cast<double>(out.totalChars) / document.days.size();
    // 连续天数：今天没打字从昨天算起
    std::string cursor = document.days.count(today) ? today : shift(today, -1);
    while (document.days.count(cursor)) {
        ++out.streak;
        cursor = shift(cursor, -1);
    }
    int run = 0;
    std::optional<std::string> previous;
    for (const auto &entry : document.days) {
        run = previous && shift(*previous, 1) == entry.first ? run + 1 : 1;
        out.longestStreak = std::max(out.longestStreak, run);
        previous = entry.first;
    }
    return out;
}

std::vector<std::pair<std::string, long long>> topKeys(const StatsDocument &document, std::size_t limit) {
    std::map<std::string, long long> totals;
    for (const auto &entry : document.days) {
        for (const auto &[key, count] : entry.second.keys) {
            totals[key] += count;
        }
    }
    std::vector<std::pair<std::string, long long>> out(totals.begin(), totals.end());
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

std::string summary(const StatsDocument &document, const std::string &today) {
    if (document.days.empty()) {
        return "还没有统计。在「常规」页打开「打字统计」以后，输入法只在本机按天记计数（字数、按键次数），不记打的内容。";
    }
    const StatsMetrics m = metrics(document, today);
    std::string out;
    out += "今天：" + std::to_string(m.todayChars) + " 字，打字 " + formatActive(m.todayActive) + "，每分钟 " +
           number(m.todaySpeed, 0) + " 字\r\n";
    out += "一共：" + std::to_string(m.totalChars) + " 字，" + std::to_string(m.recordedDays) + " 天，平均每天 " +
           number(m.averagePerDay, 0) + " 字，打字 " + formatActive(m.totalActive) + "，平均每分钟 " +
           number(m.averageSpeed, 0) + " 字\r\n";
    if (m.bestDay) {
        out += "打得最多的一天：" + dayLabel(*m.bestDay) + "，" + std::to_string(m.bestChars) + " 字\r\n";
    }
    if (m.fastestDay) {
        out += "最快的一天：" + dayLabel(*m.fastestDay) + "，每分钟 " + number(m.fastestSpeed, 0) + " 字\r\n";
    }
    out += "连续打字：" + std::to_string(m.streak) + " 天（最长 " + std::to_string(m.longestStreak) + " 天）\r\n";
    // 最近 7 天
    out += "\r\n最近 7 天：\r\n";
    for (int offset = -6; offset <= 0; ++offset) {
        const std::string key = shift(today, offset);
        const auto it = document.days.find(key);
        out += "  " + dayLabel(key) + "  " + std::to_string(it == document.days.end() ? 0 : it->second.chars) + " 字\r\n";
    }
    const auto keys = topKeys(document, 10);
    if (!keys.empty()) {
        out += "\r\n按得最多的键：";
        for (std::size_t i = 0; i < keys.size(); ++i) {
            out += (i ? "、" : "") + keys[i].first + " " + std::to_string(keys[i].second);
        }
        out += "\r\n";
    }
    return out;
}

} // namespace stats

namespace diagnostics {

namespace {

std::string trimmed(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.erase(s.begin());
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

bool looksSecret(std::string key) {
    for (char &c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const char *word : {"key", "token", "secret", "password"}) {
        if (key.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool knownKey(const std::string &key) {
    for (const auto &def : settingDefs()) {
        if (key == def.key) {
            return true;
        }
    }
    // Windows 还没有、别的端有的设置项也认识（不打码）
    return key == "CheckUpdate" || key == "BetaUpdates";
}

} // namespace

std::string maskUrl(const std::string &url) {
    std::string s = trimmed(url);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    if (s.empty()) {
        return "";
    }
    const std::size_t scheme = s.find("://");
    if (scheme == std::string::npos || scheme == 0) {
        return "（已打码）";
    }
    std::string netloc = s.substr(scheme + 3);
    netloc = netloc.substr(0, netloc.find_first_of("/?#"));
    const std::size_t at = netloc.rfind('@');
    const std::string host = at == std::string::npos ? netloc : netloc.substr(at + 1);
    if (host.empty()) {
        return "（已打码）";
    }
    return s.substr(0, scheme) + "://" + host + "/…";
}

std::string maskConfig(const std::string &text) {
    std::string out;
    std::size_t start = 0;
    bool first = true;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() + 1 : nl + 1;
        std::string result = line;
        const std::string body = trimmed(line);
        const std::size_t eq = line.find('=');
        if (!body.empty() && body[0] != '#' && body[0] != ';' && body[0] != '[' && eq != std::string::npos) {
            std::string key;
            for (const char c : line.substr(0, eq)) {
                if (c != ' ' && c != '\t') {
                    key += c;
                }
            }
            const std::string value = trimmed(line.substr(eq + 1));
            if (key == "TranslateBaseUrl") {
                result = key + "=" + maskUrl(value);
            } else if (!knownKey(key) && looksSecret(key) && !value.empty() && value != "\"\"") {
                result = key + "=（已打码）";
            }
        }
        out += (first ? "" : "\n") + result;
        first = false;
    }
    return out;
}

} // namespace diagnostics

} // namespace wowtype
