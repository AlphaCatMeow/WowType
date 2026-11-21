#pragma once

// UTF-8 ⇄ UTF-16。核心只认 UTF-8，TSF 和 Win32 只认 UTF-16。不依赖 windows.h，Linux 上也能编（测试）。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wowtype {

/// 从 UTF-8 取下一个码位，i 往后挪；坏字节当 U+FFFD，挪一个字节。
inline uint32_t nextCodepoint(std::string_view s, std::size_t &i) {
    const auto b = [&](std::size_t k) { return static_cast<uint8_t>(s[k]); };
    const uint8_t c = b(i);
    const auto cont = [&](std::size_t k) { return k < s.size() && (b(k) & 0xC0) == 0x80; };
    if (c < 0x80) {
        i += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | (b(i + 1) & 0x3Fu);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | ((b(i + 1) & 0x3Fu) << 6) | (b(i + 2) & 0x3Fu);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        const uint32_t cp = ((c & 0x07u) << 18) | ((b(i + 1) & 0x3Fu) << 12) | ((b(i + 2) & 0x3Fu) << 6) |
                            (b(i + 3) & 0x3Fu);
        i += 4;
        return cp;
    }
    i += 1;
    return 0xFFFD;
}

inline void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline std::u16string utf8To16(std::string_view s) {
    std::u16string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const uint32_t cp = nextCodepoint(s, i);
        if (cp >= 0x10000) {
            out += static_cast<char16_t>(0xD800 + ((cp - 0x10000) >> 10));
            out += static_cast<char16_t>(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

inline std::string utf16To8(std::u16string_view s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (std::size_t i = 0; i < s.size(); ++i) {
        uint32_t cp = s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        } else if (cp >= 0xD800 && cp < 0xE000) {
            cp = 0xFFFD;
        }
        appendUtf8(out, cp);
    }
    return out;
}

/// UTF-8 里前 byteOffset 个字节相当于多少个 UTF-16 单元（编辑区光标：会话给字节，TSF 要 UTF-16）。
inline int utf16Offset(std::string_view s, int byteOffset) {
    const std::size_t end = byteOffset < 0 ? 0 : std::min<std::size_t>(static_cast<std::size_t>(byteOffset), s.size());
    int units = 0;
    for (std::size_t i = 0; i < end;) {
        units += nextCodepoint(s, i) >= 0x10000 ? 2 : 1;
    }
    return units;
}

/// 最后一个码位，空串是 0。
inline uint32_t lastCodepoint(std::string_view s) {
    uint32_t last = 0;
    for (std::size_t i = 0; i < s.size();) {
        last = nextCodepoint(s, i);
    }
    return last;
}

} // namespace wowtype
