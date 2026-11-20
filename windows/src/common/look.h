#pragma once

// 设置窗口自己的样子：主题（SettingsTheme）和强调色（SettingsAccent），只管设置窗口，输入法不读。
//
// 选项、名字、颜色和 Linux 的设置界面（SETTINGS_THEMES、SETTINGS_ACCENTS）、macOS（WowtypeConfig.SettingsTheme、
// accentPresets）一样，以 spec/config.json 为准，scripts/test-config-contract.py --windows 对照（config_dump --look）。
// 颜色不自己算，用规格里的表：color 是色板本身，strong 是上面放白字的底（和白色的对比度至少 4.7:1，
// 浅色背景上当字用也是它），light 是深色背景上当字用的颜色。不依赖 windows.h，Linux 上也编得过。

#include <cstdint>
#include <string>
#include <vector>

namespace wowtype::look {

struct Theme {
    const char *value; // 写进文件的样子：System、Light、Dark
    const char *name;  // 设置窗口里的名字（UTF-8）
};

struct Accent {
    const char *value; // Green、Blue……
    const char *name;  // 绿色、蓝色……（UTF-8）
    uint32_t color;    // 0xRRGGBB
    uint32_t strong;
    uint32_t light;
};

const std::vector<Theme> &themes();
const std::vector<Accent> &accents();

/// 这个值对应的强调色；认不出的用第一个（绿色）
const Accent &accent(const std::string &value);

/// 设置窗口该不该用深色：Light、Dark 固定，别的（System、认不出的）跟着系统
bool dark(const std::string &theme, bool systemDark);

/// over 以 alpha（0–1）的不透明度叠在 under 上，返回 0xRRGGBB
uint32_t blend(uint32_t over, uint32_t under, double alpha);

/// 强调色当字用的颜色：深色背景用 light，浅色背景用 strong
inline uint32_t textColor(const Accent &a, bool dark) { return dark ? a.light : a.strong; }

/// 0xRRGGBB 写成 #RRGGBB（大写），和规格里的写法一样
std::string hex(uint32_t rgb);

} // namespace wowtype::look
