#include "app_update.h"

#include <algorithm>
#include <cstring>
#include <cctype>

#include "json.h"

namespace wowtype::update {

namespace {

bool alnum(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digit(char c) { return c >= '0' && c <= '9'; }

std::vector<long long> parts(const std::string &v) {
    std::vector<long long> out;
    std::optional<long long> current;
    for (const char c : v) {
        if (digit(c)) {
            current = current.value_or(0) * 10 + (c - '0');
        } else if (current) {
            out.push_back(*current);
            current.reset();
        }
    }
    if (current) {
        out.push_back(*current);
    }
    return out;
}

std::string trimSpace(const std::string &s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

Release fromJson(const Json &v) {
    Release r;
    if (const Json *tag = v.field("tag_name")) {
        r.tag = tag->str();
    }
    if (const Json *body = v.field("body")) {
        r.notes = body->str();
    }
    if (const Json *p = v.field("prerelease")) {
        r.prerelease = p->truthy();
    }
    if (const Json *d = v.field("draft")) {
        r.draft = d->truthy();
    }
    if (const Json *assets = v.field("assets"); assets && assets->kind == Json::Kind::Array) {
        for (const auto &asset : assets->items) {
            const Json *name = asset.field("name");
            const Json *url = asset.field("browser_download_url");
            if (name && url && !name->str().empty()) {
                r.assets[name->str()] = url->str();
            }
        }
    }
    return r;
}

} // namespace

std::optional<std::string> version(const std::string &tag) {
    std::string v = tag;
    if (!v.empty() && v[0] == 'v') {
        v.erase(0, 1);
    }
    if (v.empty() || !alnum(v[0])) {
        return std::nullopt;
    }
    for (const char c : v) {
        if (!alnum(c) && std::string("._+~^-").find(c) == std::string::npos) {
            return std::nullopt;
        }
    }
    return v;
}

std::string packageName(const std::string &v, const std::string &arch) {
    return "WowType-" + v + "-windows-" + arch + "-setup.exe";
}

bool isRelease(const std::string &v) { return !v.empty() && digit(v[0]); }

int compareVersions(const std::string &a, const std::string &b) {
    const auto x = parts(a);
    const auto y = parts(b);
    for (std::size_t i = 0; i < x.size() && i < y.size(); ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
}

bool isNewer(const std::string &latest, const std::string &current) {
    return isRelease(current) && isRelease(latest) && compareVersions(latest, current) > 0;
}

bool isAppTag(const std::string &tag) {
    if (tag.size() < 2 || (tag[0] != 'v' && tag[0] != 'V') || !digit(tag[1]) || tag.back() == '.') {
        return false;
    }
    for (std::size_t i = 1; i < tag.size(); ++i) {
        if (!digit(tag[i]) && tag[i] != '.') {
            return false;
        }
    }
    return true;
}

std::optional<Release> parseRelease(const std::string &text) {
    Json root;
    if (!parseJson(text, root) || root.kind != Json::Kind::Object || !root.field("tag_name")) {
        return std::nullopt;
    }
    return fromJson(root);
}

std::vector<Release> parseReleaseList(const std::string &text) {
    std::vector<Release> out;
    Json root;
    if (!parseJson(text, root) || root.kind != Json::Kind::Array) {
        return out;
    }
    for (const auto &item : root.items) {
        if (item.kind == Json::Kind::Object) {
            out.push_back(fromJson(item));
        }
    }
    return out;
}

std::optional<std::size_t> pickNewest(const std::vector<Release> &releases) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < releases.size(); ++i) {
        const auto &r = releases[i];
        if (r.draft || !isAppTag(r.tag)) {
            continue;
        }
        if (!best) {
            best = i;
            continue;
        }
        const int c = compareVersions(r.tag, releases[*best].tag);
        if (c > 0 || (c == 0 && releases[*best].prerelease && !r.prerelease)) {
            best = i;
        }
    }
    return best;
}

namespace {

/// 网址里的一段（tag、文件名）：字母数字和 -._~ 原样，别的按 UTF-8 字节转成 %XX
std::string urlPart(const std::string &s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += ch;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

/// releases/tag/ 后面的 tag：到 stop 里的字符为止（再把 %XX 解回来）
std::string tagAfter(const std::string &s, std::size_t from, const char *stop) {
    std::string raw;
    for (std::size_t i = from; i < s.size() && !std::strchr(stop, s[i]); ++i) {
        raw += s[i];
    }
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
            out += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += raw[i];
        }
    }
    return out;
}

constexpr char kTagMarker[] = "/releases/tag/";

} // namespace

std::string downloadUrl(const std::string &tag, const std::string &file) {
    return std::string(kRepoPage) + "/releases/download/" + urlPart(tag) + "/" + urlPart(file);
}

std::string tagPage(const std::string &tag) { return std::string(kRepoPage) + "/releases/tag/" + urlPart(tag); }

std::optional<std::string> tagFromLatestUrl(const std::string &url) {
    const std::size_t at = url.find(kTagMarker);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t from = at + std::strlen(kTagMarker);
    const std::string tag = tagAfter(url, from, "?#/");
    // 和 Linux 一样：tag 要到地址末尾（…/releases/tag/v0.6.0/xxx 不认）
    std::size_t end = from;
    while (end < url.size() && !std::strchr("?#/", url[end])) {
        ++end;
    }
    if (tag.empty() || (end < url.size() && url[end] == '/')) {
        return std::nullopt;
    }
    return tag;
}

std::vector<std::string> webCandidates(const std::optional<std::string> &latest, const std::optional<std::string> &feed) {
    std::vector<std::string> tags;
    if (latest && isAppTag(*latest)) {
        tags.push_back(*latest);
    }
    if (feed) {
        std::size_t at = 0;
        while ((at = feed->find(kTagMarker, at)) != std::string::npos) {
            at += std::strlen(kTagMarker);
            const std::string tag = tagAfter(*feed, at, "\"'<>/?#");
            if (isAppTag(tag) && std::find(tags.begin(), tags.end(), tag) == tags.end()) {
                tags.push_back(tag);
            }
        }
    }
    std::stable_sort(tags.begin(), tags.end(), [&](const std::string &a, const std::string &b) {
        const int c = compareVersions(a, b);
        if (c != 0) {
            return c > 0;
        }
        return latest && a == *latest && b != *latest;
    });
    return tags;
}

std::vector<std::string> assetNames(const std::string &sums) {
    std::vector<std::string> names;
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    std::size_t start = 0;
    while (start <= sums.size()) {
        std::size_t nl = sums.find('\n', start);
        if (nl == std::string::npos) {
            nl = sums.size();
        }
        std::string line = sums.substr(start, nl - start);
        start = nl + 1;
        std::size_t i = 0;
        while (i < line.size() && blank(line[i])) {
            ++i;
        }
        const std::size_t hashStart = i;
        while (i < line.size() && !blank(line[i])) {
            ++i;
        }
        const std::string hash = line.substr(hashStart, i - hashStart);
        bool ok = hash.size() == 64;
        for (const char c : hash) {
            ok = ok && std::isxdigit(static_cast<unsigned char>(c));
        }
        while (i < line.size() && blank(line[i])) {
            ++i;
        }
        std::string name = line.substr(i);
        while (!name.empty() && blank(name.back())) {
            name.pop_back();
        }
        if (!name.empty() && name.front() == '*') {
            name.erase(0, 1);
        }
        if (ok && !name.empty() && name.find('/') == std::string::npos) {
            names.push_back(name);
        }
        if (nl == sums.size()) {
            break;
        }
    }
    return names;
}

Release webRelease(const std::string &tag, const std::string &sums, bool prerelease) {
    Release release;
    release.tag = tag;
    release.prerelease = prerelease;
    release.notes = "GitHub 接口这会儿访问次数到了上限，没取到更新说明，见发布页：" + tagPage(tag);
    release.assets[kChecksumFile] = downloadUrl(tag, kChecksumFile);
    for (const auto &name : assetNames(sums)) {
        release.assets[name] = downloadUrl(tag, name);
    }
    return release;
}

namespace {

/// 接口限流时改走网页（和 Linux 的 fetch_latest_web 一样）
std::optional<Release> fetchLatestWeb(const Fetch &get, bool beta, std::string *error) {
    const HttpResult page = get(kLatestPage);
    if (page.status != 200) {
        *error = page.status == 0 ? page.error : "发布页回了 " + std::to_string(page.status);
        return std::nullopt;
    }
    const std::optional<std::string> latest = tagFromLatestUrl(page.finalUrl);
    std::optional<std::string> feed;
    if (beta) {
        const HttpResult atom = get(kAtomFeed);
        if (atom.status != 200) {
            *error = atom.status == 0 ? atom.error : "发布列表回了 " + std::to_string(atom.status);
            return std::nullopt;
        }
        feed = atom.body;
    }
    for (const auto &tag : webCandidates(latest, feed)) {
        const bool stable = latest && tag == *latest;
        const HttpResult sums = get(downloadUrl(tag, kChecksumFile));
        if (sums.status == 200) {
            return webRelease(tag, sums.body, !stable);
        }
        // 只推了 tag、还没发出来（正在构建）的版本没有 SHA256SUMS：往下试；最新正式版没有就是出错了
        if (sums.status != 404 || stable) {
            *error = sums.status == 0 ? sums.error : "SHA256SUMS 回了 " + std::to_string(sums.status);
            return std::nullopt;
        }
    }
    *error = "GitHub 上找不到发布的版本";
    return std::nullopt;
}

} // namespace

std::optional<Release> fetchLatest(const Fetch &get, bool beta, std::string *error) {
    const auto failed = [&](const HttpResult &r) {
        *error = r.status == 0 ? r.error : "服务器回了 " + std::to_string(r.status);
        return std::nullopt;
    };
    if (beta) {
        const HttpResult list = get(kListApi);
        if (rateLimited(list.status)) {
            return fetchLatestWeb(get, beta, error);
        }
        if (list.status != 200) {
            return failed(list);
        }
        const auto releases = parseReleaseList(list.body);
        if (const auto i = pickNewest(releases)) {
            return releases[*i];
        }
    }
    const HttpResult one = get(kLatestApi);
    if (rateLimited(one.status)) {
        return fetchLatestWeb(get, beta, error);
    }
    if (one.status != 200) {
        return failed(one);
    }
    auto release = parseRelease(one.body);
    if (!release) {
        *error = "发布页上的内容看不懂";
    }
    return release;
}

std::optional<ContentRange> parseContentRange(const std::string &header) {
    // bytes <起点>-<终点>/<总长或 *>
    std::size_t i = 0;
    const auto skipSpaces = [&] {
        while (i < header.size() && header[i] == ' ') {
            ++i;
        }
    };
    const auto number = [&]() -> std::optional<unsigned long long> {
        const std::size_t from = i;
        unsigned long long n = 0;
        while (i < header.size() && std::isdigit(static_cast<unsigned char>(header[i]))) {
            if (n > (~0ULL - 9) / 10) {
                return std::nullopt;
            }
            n = n * 10 + static_cast<unsigned>(header[i] - '0');
            ++i;
        }
        return i > from ? std::optional<unsigned long long>(n) : std::nullopt;
    };
    skipSpaces();
    if (header.compare(i, 5, "bytes") != 0) {
        return std::nullopt;
    }
    i += 5;
    skipSpaces();
    const auto start = number();
    if (!start || i >= header.size() || header[i] != '-') {
        return std::nullopt;
    }
    ++i;
    const auto end = number();
    if (!end || *end < *start || i >= header.size() || header[i] != '/') {
        return std::nullopt;
    }
    ++i;
    ContentRange range;
    range.start = *start;
    if (i < header.size() && header[i] == '*') {
        ++i;
    } else {
        const auto total = number();
        if (!total || *total <= *end) {
            return std::nullopt;
        }
        range.total = *total;
    }
    skipSpaces();
    return i == header.size() ? std::optional<ContentRange>(range) : std::nullopt;
}

std::optional<std::string> checksum(const std::string &sums, const std::string &name, std::string *error) {
    std::optional<std::string> found;
    std::size_t start = 0;
    while (start < sums.size()) {
        std::size_t nl = sums.find('\n', start);
        if (nl == std::string::npos) {
            nl = sums.size();
        }
        const std::string line = sums.substr(start, nl - start);
        start = nl + 1;
        const std::size_t sep = line.find_first_of(" \t");
        if (sep == std::string::npos) {
            continue;
        }
        const std::string digest = line.substr(0, sep);
        std::string file = trimSpace(line.substr(sep + 1));
        if (!file.empty() && file[0] == '*') {
            file.erase(0, 1);
        }
        if (file != name) {
            continue;
        }
        bool hex = digest.size() == 64;
        for (const char c : digest) {
            hex = hex && std::isxdigit(static_cast<unsigned char>(c));
        }
        if (found || !hex) {
            if (error) {
                *error = "SHA256SUMS 中 " + name + " 的校验值重复或无效，不安装";
            }
            return std::nullopt;
        }
        std::string lower = digest;
        for (char &c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        found = lower;
    }
    if (!found && error) {
        *error = "SHA256SUMS 里没有 " + name + "，不安装";
    }
    return found;
}

State State::parse(const std::string &text) {
    State state;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t nl = text.find_first_of("\r\n", start);
        if (nl == std::string::npos) {
            nl = text.size();
        }
        const std::string line = text.substr(start, nl - start);
        start = nl + 1;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "checked") {
            bool ok = !value.empty();
            for (const char c : value) {
                ok = ok && digit(c);
            }
            if (ok && value.size() < 19) {
                state.checked = std::stoll(value);
            }
        } else if (key == "tried") {
            bool ok = !value.empty();
            for (const char c : value) {
                ok = ok && digit(c);
            }
            if (ok && value.size() < 19) {
                state.tried = std::stoll(value);
            }
        } else if (key == "latest" && version(value)) {
            state.latest = value;
        }
    }
    return state;
}

std::string State::text() const {
    return "checked=" + std::to_string(checked) + "\nlatest=" + latest + "\ntried=" + std::to_string(tried) + "\n";
}

bool due(const State &state, int64_t now) {
    const bool stale = now - state.checked >= kCheckInterval || now < state.checked;
    const bool rested = now - state.tried >= kRetryInterval || now < state.tried;
    return stale && rested;
}

std::string highlights(const std::string &notes) {
    const std::string all = trimSpace(notes);
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = all.find('\n', start);
        std::string line = all.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    const auto heading = [](const std::string &line) {
        std::size_t i = 0;
        while (i < 2 && i < line.size() && line[i] == '#') {
            ++i;
        }
        return i > 0 && i < line.size() && (line[i] == ' ' || line[i] == '\t');
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string stripped = trimSpace(lines[i]);
        if (stripped.rfind("##", 0) != 0 || trimSpace(stripped.substr(2)) != "这一版") {
            continue;
        }
        std::string body;
        for (std::size_t j = i + 1; j < lines.size() && !heading(lines[j]); ++j) {
            body += (body.empty() && j == i + 1 ? "" : "\n") + lines[j];
        }
        body = trimSpace(body);
        return body.empty() ? all : body;
    }
    return all;
}

} // namespace wowtype::update
