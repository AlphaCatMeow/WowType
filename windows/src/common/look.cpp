#include "look.h"

#include <cmath>
#include <cstdio>

namespace wowtype::look {

const std::vector<Theme> &themes() {
    static const std::vector<Theme> t = {
        {"System", "跟随系统"},
        {"Light", "浅色"},
        {"Dark", "深色"},
    };
    return t;
}

const std::vector<Accent> &accents() {
    static const std::vector<Accent> a = {
        {"Green", "绿色", 0x0EAF76, 0x0A8358, 0x1AB37D},
        {"Blue", "蓝色", 0x3584E4, 0x2D70C2, 0x68A3EB},
        {"Purple", "紫色", 0x9141AC, 0x9141AC, 0xBD8DCD},
        {"Orange", "橙色", 0xE66100, 0xB84E00, 0xEC8840},
        {"Pink", "粉色", 0xD56199, 0xAA4E7A, 0xDD81AD},
    };
    return a;
}

const Accent &accent(const std::string &value) {
    for (const auto &a : accents()) {
        if (value == a.value) {
            return a;
        }
    }
    return accents().front();
}

bool dark(const std::string &theme, bool systemDark) {
    if (theme == "Light") {
        return false;
    }
    if (theme == "Dark") {
        return true;
    }
    return systemDark;
}

uint32_t blend(uint32_t over, uint32_t under, double alpha) {
    uint32_t out = 0;
    for (int shift = 16; shift >= 0; shift -= 8) {
        const double top = (over >> shift) & 0xFF;
        const double bottom = (under >> shift) & 0xFF;
        const auto channel = static_cast<uint32_t>(std::lround(bottom + (top - bottom) * alpha));
        out |= (channel & 0xFF) << shift;
    }
    return out;
}

std::string hex(uint32_t rgb) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%06X", static_cast<unsigned>(rgb & 0xFFFFFF));
    return buf;
}

} // namespace wowtype::look
