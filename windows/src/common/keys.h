#pragma once

// Windows 按键 → 核心的键码（XKB keysym 子集）和修饰键，以及设置里的快捷键（EmojiKey 等）。
//
// 和 macOS 的 Shared/Keys.swift 一一对应。不依赖 windows.h：虚拟键码（VK_*）就是整数，
// 按键测试在 Linux 上也编得过。TSF 外壳（tsf/text_service.cpp）把 ITfKeyEventSink 收到的
// wParam（虚拟键码）、ToUnicode 打出的字符、修饰键传进来。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wowtype {

/// 核心认的键码（和 X11 keysym 取值一样）。
namespace keysym {
inline constexpr uint32_t backspace = 0xff08;
inline constexpr uint32_t tab = 0xff09;
inline constexpr uint32_t returnKey = 0xff0d;
inline constexpr uint32_t escape = 0xff1b;
inline constexpr uint32_t home = 0xff50;
inline constexpr uint32_t left = 0xff51;
inline constexpr uint32_t up = 0xff52;
inline constexpr uint32_t right = 0xff53;
inline constexpr uint32_t down = 0xff54;
inline constexpr uint32_t pageUp = 0xff55;
inline constexpr uint32_t pageDown = 0xff56;
inline constexpr uint32_t end = 0xff57;
inline constexpr uint32_t keypadEnter = 0xff8d;
inline constexpr uint32_t shiftLeft = 0xffe1;
inline constexpr uint32_t shiftRight = 0xffe2;
inline constexpr uint32_t del = 0xffff;
} // namespace keysym

/// 核心的修饰键位掩码（WOWTYPE_MOD_*）。Windows 键当 Super。
namespace mod {
inline constexpr uint32_t shift = 1u << 0;
inline constexpr uint32_t caps = 1u << 1;
inline constexpr uint32_t control = 1u << 2;
inline constexpr uint32_t alt = 1u << 3;
inline constexpr uint32_t numlock = 1u << 4;
inline constexpr uint32_t super = 1u << 6;
/// 组合键：带这几个的不当打字
inline constexpr uint32_t shortcut = control | alt | super;
} // namespace mod

/// Windows 的虚拟键码（winuser.h 的 VK_*），只列用到的。
namespace vk {
inline constexpr uint32_t back = 0x08;
inline constexpr uint32_t tab = 0x09;
inline constexpr uint32_t returnKey = 0x0D;
inline constexpr uint32_t shift = 0x10;
inline constexpr uint32_t control = 0x11;
inline constexpr uint32_t menu = 0x12; // Alt
inline constexpr uint32_t capital = 0x14;
inline constexpr uint32_t escape = 0x1B;
inline constexpr uint32_t space = 0x20;
inline constexpr uint32_t prior = 0x21; // Page Up
inline constexpr uint32_t next = 0x22;  // Page Down
inline constexpr uint32_t end = 0x23;
inline constexpr uint32_t home = 0x24;
inline constexpr uint32_t left = 0x25;
inline constexpr uint32_t up = 0x26;
inline constexpr uint32_t right = 0x27;
inline constexpr uint32_t down = 0x28;
inline constexpr uint32_t del = 0x2E;
inline constexpr uint32_t lwin = 0x5B;
inline constexpr uint32_t rwin = 0x5C;
inline constexpr uint32_t numpad0 = 0x60;
inline constexpr uint32_t numpad9 = 0x69;
inline constexpr uint32_t lshift = 0xA0;
inline constexpr uint32_t rshift = 0xA1;
inline constexpr uint32_t lcontrol = 0xA2;
inline constexpr uint32_t rcontrol = 0xA3;
inline constexpr uint32_t lmenu = 0xA4;
inline constexpr uint32_t rmenu = 0xA5;
/// 右 Shift 的扫描码（左边是 0x2A）
inline constexpr uint32_t rightShiftScan = 0x36;
} // namespace vk

/// 一次按键，已经翻译成核心的键码。
struct KeyInput {
    uint32_t keysym = 0;
    uint32_t modifiers = 0;
    bool release = false;

    std::optional<uint8_t> ascii() const {
        if (keysym >= 0x20 && keysym <= 0x7e) {
            return static_cast<uint8_t>(keysym);
        }
        return std::nullopt;
    }
    bool isLetter() const {
        const auto c = ascii();
        return c && ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z'));
    }
    bool isDigit() const {
        const auto c = ascii();
        return c && *c >= '0' && *c <= '9';
    }
    bool hasShortcutModifier() const { return (modifiers & mod::shortcut) != 0; }

    static KeyInput character(char32_t c, uint32_t modifiers = 0) { return KeyInput{static_cast<uint32_t>(c), modifiers}; }
    static KeyInput key(uint32_t sym, uint32_t modifiers = 0) { return KeyInput{sym, modifiers}; }
};

/// 是不是修饰键本身（Shift、Ctrl、Alt、Win、CapsLock）：这些不当打字，Shift 另走 shiftChanged。
bool isModifierKey(uint32_t virtualKey);

/// 按下的一个键 → KeyInput。character 是 ToUnicode 打出的字符（考虑了 Shift、CapsLock；
/// 带 Ctrl / Alt / Win 时外壳要用去掉这几个修饰键的键盘状态算，免得拿到控制字符），打不出字时给 0。
/// extended 是 lParam 的扩展键位（小键盘的回车）。翻不出来（F 键、非 ASCII 的字符）返回 nullopt。
std::optional<KeyInput> translateVirtualKey(uint32_t virtualKey, char32_t character, uint32_t modifiers,
                                            bool extended);

/// 快捷键（设置里的 EmojiKey、ToggleKey、TranslateModeKey）。键名和 fcitx5 一样：修饰键 + 键，用 + 连起来，
/// 比如 Control+period、Control+Shift+E、Alt+semicolon。键只能是可见的 ASCII 字符（X keysym 名字，或者字符本身）。
///
/// Windows 键的组合多半被系统先拿走（Win+. 是系统的表情面板），到不了输入法，所以 Super 当 Control，
/// 和 macOS 一样：Linux 的默认值 Super+period 拷过来就是 Control+.。
struct Shortcut {
    uint32_t keysym = 0;   // 可见 ASCII；字母存小写
    uint32_t modifiers = 0; // 只有 shift、control、alt

    static Shortcut make(uint32_t keysym, uint32_t modifiers);
    /// 解析 fcitx5 的键名；认不出返回 nullopt（设置里写错了就保持默认）。
    static std::optional<Shortcut> parse(std::string_view name);
    /// 写回设置文件、记日志用的名字（Control+period）。
    std::string name() const;
    /// 这次按键是不是这个快捷键。CapsLock 不算；字母不分大小写。
    bool matches(const KeyInput &key) const;
    bool operator==(const Shortcut &o) const { return keysym == o.keysym && modifiers == o.modifiers; }
};

} // namespace wowtype
