#include "custom_words.h"

#include <algorithm>

#include "wowtype_core.h"

namespace wowtype {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

std::string strip(std::string s) {
    while (!s.empty() && isSpace(s.front())) {
        s.erase(s.begin());
    }
    while (!s.empty() && isSpace(s.back())) {
        s.pop_back();
    }
    return s;
}

} // namespace

const char *CustomWords::header() {
    return "# WowType 自定义词：编码<TAB>词。编码是拼音（空格分开音节），也可以是 mail 这类字母；\n"
           "# 词里写 \\n 表示换行。由 WowType 设置维护，也可以手动编辑。";
}

CustomWords::CustomWords(const std::optional<std::string> &text) {
    std::string body = text ? *text : std::string(header());
    if (body.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        body.erase(0, 3);
    }
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t nl = body.find('\n', start);
        parts.push_back(body.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    if (!parts.empty() && parts.back().empty()) {
        parts.pop_back();
    }
    for (auto &raw : parts) {
        lines_.push_back(Line{text ? parseLine(raw) : std::nullopt, raw});
    }
}

std::optional<CustomWords::Entry> CustomWords::parseLine(const std::string &raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    if ((!line.empty() && line.front() == '#') || strip(line).empty()) {
        return std::nullopt;
    }
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t tab2 = line.find('\t', tab + 1);
    const std::string word = strip(line.substr(tab + 1, tab2 == std::string::npos ? std::string::npos : tab2 - tab - 1));
    if (word.empty()) {
        return std::nullopt;
    }
    return Entry{strip(line.substr(0, tab)), word};
}

std::vector<CustomWords::Entry> CustomWords::entries() const {
    std::vector<Entry> out;
    for (const auto &line : lines_) {
        if (line.entry) {
            out.push_back(*line.entry);
        }
    }
    return out;
}

std::string CustomWords::text() const {
    std::string out;
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        out += (i ? "\n" : "") + lines_[i].raw;
    }
    return out + "\n";
}

bool CustomWords::add(const std::string &code, const std::string &word) {
    const bool replaced = !remove(word).empty();
    lines_.push_back(Line{Entry{code, word}, code + "\t" + word});
    return replaced;
}

std::vector<CustomWords::Entry> CustomWords::remove(const std::string &word) {
    std::vector<Entry> removed;
    lines_.erase(std::remove_if(lines_.begin(), lines_.end(),
                                [&](const Line &line) {
                                    if (line.entry && line.entry->word == word) {
                                        removed.push_back(*line.entry);
                                        return true;
                                    }
                                    return false;
                                }),
                 lines_.end());
    return removed;
}

std::pair<int, int> CustomWords::merge(const std::vector<Entry> &incoming) {
    int added = 0;
    int updated = 0;
    for (const auto &item : incoming) {
        std::vector<Entry> old;
        for (const auto &e : entries()) {
            if (e.word == item.word) {
                old.push_back(e);
            }
        }
        if (old.size() == 1 && old[0] == item) {
            continue;
        }
        if (old.empty()) {
            ++added;
        } else {
            ++updated;
        }
        add(item.code, item.word);
    }
    return {added, updated};
}

std::vector<CustomWords::Entry> CustomWords::parseImport(const std::string &text) {
    std::vector<Entry> out;
    for (const auto &e : CustomWords(text).entries()) {
        if (const auto code = normalizeCode(e.code)) {
            out.push_back(Entry{*code, e.word});
        }
    }
    return out;
}

std::string CustomWords::shown(const std::string &word) {
    std::string out;
    bool backslash = false;
    for (const char c : word) {
        if (backslash) {
            backslash = false;
            if (c == 'n') {
                out += "⏎";
                continue;
            }
            out += '\\';
        }
        if (c == '\\') {
            backslash = true;
        } else {
            out += c;
        }
    }
    return backslash ? out + "\\" : out;
}

bool CustomWords::isValidWord(const std::string &word) {
    return !word.empty() && word.find_first_of("\t\n\r") == std::string::npos;
}

std::optional<std::string> CustomWords::normalizeCode(const std::string &text) {
    const char *code = wowtype_custom_code(text.c_str(), 0);
    return code ? std::optional<std::string>(code) : std::nullopt;
}

std::optional<std::string> CustomWords::guessPinyin(const std::string &word, const std::string &lexicon) {
    if (word.empty() || lexicon.empty()) {
        return std::nullopt;
    }
    const char *pinyin = wowtype_guess_pinyin(lexicon.data(), lexicon.size(), word.c_str());
    return pinyin ? std::optional<std::string>(pinyin) : std::nullopt;
}

} // namespace wowtype
