#include "keys.h"

#include <array>
#include <cctype>
#include <vector>

namespace wowtype {

namespace {

/// 0x20–0x7e 的 X keysym 名字（字母、数字是它自己）。
const std::vector<std::string> &asciiNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n = {"space",     "exclam",     "quotedbl", "numbersign", "dollar",   "percent",
                                      "ampersand", "apostrophe", "parenleft", "parenright", "asterisk", "plus",
                                      "comma",     "minus",      "period",   "slash"};
        for (char c = '0'; c <= '9'; ++c) {
            n.emplace_back(1, c);
        }
        for (const char *s : {"colon", "semicolon", "less", "equal", "greater", "question", "at"}) {
            n.emplace_back(s);
        }
        for (char c = 'A'; c <= 'Z'; ++c) {
            n.emplace_back(1, c);
        }
        for (const char *s : {"bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "grave"}) {
            n.emplace_back(s);
        }
        for (char c = 'a'; c <= 'z'; ++c) {
            n.emplace_back(1, c);
        }
        for (const char *s : {"braceleft", "bar", "braceright", "asciitilde"}) {
            n.emplace_back(s);
        }
        return n;
    }();
    return names;
}

uint32_t fold(uint32_t keysym) { return keysym >= 'A' && keysym <= 'Z' ? keysym | 0x20 : keysym; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char &c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string_view trimSpaces(std::string_view s) {
    while (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ') {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<uint32_t> keysymNamed(std::string_view name) {
    if (name.size() == 1 && name[0] > 0x20 && name[0] <= 0x7e) {
        return static_cast<uint32_t>(static_cast<unsigned char>(name[0]));
    }
    const std::string wanted = lower(name);
    const auto &names = asciiNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (lower(names[i]) == wanted) {
            return static_cast<uint32_t>(0x20 + i);
        }
    }
    return std::nullopt;
}

} // namespace

bool isModifierKey(uint32_t v) {
    switch (v) {
    case vk::shift:
    case vk::control:
    case vk::menu:
    case vk::capital:
    case vk::lwin:
    case vk::rwin:
    case vk::lshift:
    case vk::rshift:
    case vk::lcontrol:
    case vk::rcontrol:
    case vk::lmenu:
    case vk::rmenu:
    case 0x90: // NumLock
    case 0x91: // ScrollLock
        return true;
    default:
        return false;
    }
}

std::optional<KeyInput> translateVirtualKey(uint32_t v, char32_t character, uint32_t modifiers, bool extended) {
    std::optional<uint32_t> special;
    switch (v) {
    case vk::returnKey: special = extended ? keysym::keypadEnter : keysym::returnKey; break;
    case vk::tab: special = keysym::tab; break;
    case vk::space: special = 0x20; break;
    case vk::back: special = keysym::backspace; break;
    case vk::del: special = keysym::del; break;
    case vk::escape: special = keysym::escape; break;
    case vk::left: special = keysym::left; break;
    case vk::right: special = keysym::right; break;
    case vk::up: special = keysym::up; break;
    case vk::down: special = keysym::down; break;
    case vk::prior: special = keysym::pageUp; break;
    case vk::next: special = keysym::pageDown; break;
    case vk::home: special = keysym::home; break;
    case vk::end: special = keysym::end; break;
    default: break;
    }
    if (special) {
        return KeyInput{*special, modifiers};
    }
    if (v >= vk::numpad0 && v <= vk::numpad9) {
        // 小键盘数字（NumLock 开着才是 VK_NUMPAD*）：和 macOS 一样当数字，选词、进算式都一样
        return KeyInput{static_cast<uint32_t>('0' + (v - vk::numpad0)), modifiers};
    }
    if (character < 0x20 || character > 0x7e) {
        return std::nullopt;
    }
    return KeyInput{static_cast<uint32_t>(character), modifiers};
}

Shortcut Shortcut::make(uint32_t sym, uint32_t modifiers) {
    return Shortcut{fold(sym), modifiers & (mod::shift | mod::control | mod::alt)};
}

std::optional<Shortcut> Shortcut::parse(std::string_view name) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = name.find('+', start);
        parts.push_back(trimSpaces(name.substr(start, plus == std::string_view::npos ? std::string_view::npos : plus - start)));
        if (plus == std::string_view::npos) {
            break;
        }
        start = plus + 1;
    }
    const std::string_view key = parts.back();
    if (key.empty()) {
        return std::nullopt;
    }
    uint32_t modifiers = 0;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string part = lower(parts[i]);
        if (part == "control" || part == "ctrl" || part == "super") {
            modifiers |= mod::control;
        } else if (part == "alt" || part == "option") {
            modifiers |= mod::alt;
        } else if (part == "shift") {
            modifiers |= mod::shift;
        } else {
            return std::nullopt;
        }
    }
    const auto sym = keysymNamed(key);
    if (!sym) {
        return std::nullopt;
    }
    return make(*sym, modifiers);
}

std::string Shortcut::name() const {
    std::string out;
    if (modifiers & mod::control) {
        out += "Control+";
    }
    if (modifiers & mod::alt) {
        out += "Alt+";
    }
    if (modifiers & mod::shift) {
        out += "Shift+";
    }
    // 按着 Shift 的字母写大写（和 fcitx5 一样：Control+Shift+E）
    const bool shifted = (modifiers & mod::shift) && keysym >= 'a' && keysym <= 'z';
    const long index = static_cast<long>(shifted ? keysym - 0x20 : keysym) - 0x20;
    const auto &names = asciiNames();
    out += index >= 0 && index < static_cast<long>(names.size()) ? names[static_cast<std::size_t>(index)] : "?";
    return out;
}

bool Shortcut::matches(const KeyInput &key) const {
    return (key.modifiers & (mod::shift | mod::control | mod::alt | mod::super)) == modifiers &&
           fold(key.keysym) == keysym;
}

} // namespace wowtype
