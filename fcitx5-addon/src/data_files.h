#pragma once

// 数据文件（词库、英文词表……）在用户目录和系统目录里都有时用哪一份。不依赖 fcitx5，按键测试直接测它。

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace wowtype {

/// 安装包自带、WowType 自己生成的数据文件的第一行。用户目录里的同名文件第一行是这个，
/// 说明是标准数据的一份拷贝，不是用户自己做的。
inline std::string_view bundledDataMarker(std::string_view fileName) {
    if (fileName == "lexicon.tsv") {
        return "# 由 tools/rime2tsv.py 生成"; // scripts/make-data.sh 生成的雾凇词库
    }
    if (fileName == "english.txt") {
        return "# Rime dictionary"; // 雾凇的英文词表原样拼接
    }
    return {};
}

/// 用户目录里的这份是不是老版本按说明拷进来的自带数据，该让给系统目录里安装包带的那份。
///
/// 0.3.4 以前词库不在安装包里，README 让大家把 lexicon.tsv、english.txt 拷进 ~/.local/share/wowtype/；
/// 后来安装包自带了，用户目录那份却照样优先，装了新版也一直用旧词库。
/// 算旧副本的条件：第一行是 WowType 生成的标准数据（见 bundledDataMarker），系统目录里也有一份，
/// 而且用户目录这份不比系统目录的新。系统目录的修改时间早于 2000 年（Nix 之类可复现构建会把时间戳
/// 统一设成 1970 年）时看不出新旧，也算旧副本。
/// 用户自己做的词库（第一行不是标记、或者自己重新生成了更新的一份）照旧优先；
/// 想指定词库的正路是设置里的 Lexicon 或环境变量 WOWTYPE_LEXICON。
inline bool isStaleBundledCopy(const std::filesystem::path &user, const std::filesystem::path &system,
                               std::string_view fileName) {
    const std::string_view marker = bundledDataMarker(fileName);
    if (marker.empty()) {
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(system, ec) || std::filesystem::equivalent(user, system, ec)) {
        return false;
    }
    std::ifstream in(user, std::ios::binary);
    std::string first;
    if (!in || !std::getline(in, first) || first.rfind(marker, 0) != 0) {
        return false;
    }
    std::error_code userEc;
    std::error_code systemEc;
    const auto userTime = std::filesystem::last_write_time(user, userEc);
    const auto systemTime = std::filesystem::last_write_time(system, systemEc);
    if (userEc || systemEc) {
        return true;
    }
    const auto toSystemClock = [](std::filesystem::file_time_type t) {
        return std::chrono::time_point_cast<std::chrono::seconds>(
            t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    };
    // 2000-01-01T00:00:00Z
    const auto sane = std::chrono::system_clock::time_point(std::chrono::seconds(946684800));
    if (toSystemClock(systemTime) < sane) {
        return true;
    }
    return userTime <= systemTime;
}

} // namespace wowtype
