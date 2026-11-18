#include "app_update.h"

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
        } else if (key == "latest" && version(value)) {
            state.latest = value;
        }
    }
    return state;
}

std::string State::text() const { return "checked=" + std::to_string(checked) + "\nlatest=" + latest + "\n"; }

bool due(const State &state, int64_t now) { return now - state.checked >= kCheckInterval || now < state.checked; }

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
