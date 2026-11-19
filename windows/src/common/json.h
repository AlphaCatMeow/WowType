#pragma once

// 够用的 JSON 读法（stats.json、GitHub 发布页的 JSON）：对象、数组、字符串、数字，\uXXXX 只认 BMP 的。不依赖 windows.h。

#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "utf.h"

namespace wowtype {


struct Json {
    enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::map<std::string, Json> fields;

    bool truthy() const { return kind == Kind::Bool && number != 0; }
    std::string str() const { return kind == Kind::String ? text : std::string(); }

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
                out.kind = w == "null" ? Json::Kind::Null : Json::Kind::Bool;
                out.number = w == "true" ? 1 : 0;
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
                case 'u': {
                    // 发布说明里有中文：\uXXXX 解出来（代理对拼成一个字）
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        const uint32_t low = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return false;
    }
    uint32_t hex4() {
        uint32_t v = 0;
        for (int k = 0; k < 4 && i_ < s_.size(); ++k, ++i_) {
            const char c = s_[i_];
            v = v * 16 + static_cast<uint32_t>(c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        }
        return v;
    }
    const std::string &s_;
    std::size_t i_ = 0;
};


inline bool parseJson(const std::string &text, Json &out) {
    Parser parser(text);
    return parser.value(out);
}

} // namespace wowtype
