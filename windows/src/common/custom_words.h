#pragma once

// 自定义词（常用语）：%LOCALAPPDATA%\WowType\custom.tsv，格式同 Linux、macOS：一行一个「编码<TAB>词[<TAB>频次]」，
// # 开头是注释。设置窗口的「自定义词」页用：加词、删词、导入。
//
// 和 macOS 的 Shared/CustomWords.swift 一行一行对着写（规则照 Linux 设置界面）：没动过的行（注释、频次列）原样写回去。
// 编码整理（拼音切音节）、猜拼音用核心的（wowtype_custom_code、wowtype_guess_pinyin）。不依赖 windows.h。

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wowtype {

class CustomWords {
public:
    struct Entry {
        std::string code; // 拼音（空格分开音节）或 mail 这类字母
        std::string word; // 转义原样（文件里的 \n 表示换行）
        bool operator==(const Entry &o) const { return code == o.code && word == o.word; }
    };

    /// 新文件开头的说明（和 Linux 设置界面、macOS 写的一样）
    static const char *header();

    /// 读文件的原文；nullopt（还没有文件）是只有说明的空表。
    explicit CustomWords(const std::optional<std::string> &text);

    /// 文件里的词，按文件顺序。
    std::vector<Entry> entries() const;
    /// 写回文件的原文：没动过的行原样，新加的是「编码<TAB>词」。
    std::string text() const;
    /// 加一个词（同一个词原来有就换成新编码，挪到最后）。返回原来有没有这个词。
    bool add(const std::string &code, const std::string &word);
    /// 删掉这个词（文件里写了几行都删），返回删掉的。
    std::vector<Entry> remove(const std::string &word);
    /// 导入：同一个词用新的编码。返回 (新增几个, 更新几个)。
    std::pair<int, int> merge(const std::vector<Entry> &incoming);

    /// 导入的文件：编码整理过（Ni Hǎo → ni hao），编码不对的行不要。
    static std::vector<Entry> parseImport(const std::string &text);
    /// 界面上显示的词：文件里的 \n（换行）画成 ⏎。
    static std::string shown(const std::string &word);
    /// 加词时检查词：不能空，不能有 TAB 和换行（换行写成 \n）。
    static bool isValidWord(const std::string &word);
    /// 编码：拼音整理成空格分开的音节；不是拼音但全是字母的（mail）原样小写。都不是返回 nullopt。
    static std::optional<std::string> normalizeCode(const std::string &text);
    /// 在词库原文里猜词的拼音（设置窗口加词时自动填）；猜不出返回 nullopt。
    static std::optional<std::string> guessPinyin(const std::string &word, const std::string &lexicon);

    bool operator==(const CustomWords &o) const { return lines_ == o.lines_; }

private:
    struct Line {
        std::optional<Entry> entry; // 空的是注释、空行、坏行
        std::string raw;
        bool operator==(const Line &o) const { return entry == o.entry && raw == o.raw; }
    };
    static std::optional<Entry> parseLine(const std::string &raw);
    std::vector<Line> lines_;
};

} // namespace wowtype
