#pragma once

// 一个进程一份的东西：引擎（词库、语言模型、学习数据）、设置、翻译器。和 macOS 的 AppState 对应。
//
// TSF 的文本服务是装进每个应用进程里的 DLL，一个界面线程一个实例（资源管理器每个窗口一个线程）。
// 引擎一个进程只载一份，几个线程共用：用 mutex() 把对引擎和会话的调用串起来（核心不承诺同一个引擎句柄
// 能在几个线程里同时用，见 docs/ABI.md「线程」）。
//
// 数据放在哪：
//   安装目录（DLL 旁边的 data\）  lexicon.tsv、english.txt、words.lm、s2t.tsv、wubi86.tsv、nnlm.bin（安装包带的）
//   %LOCALAPPDATA%\WowType\        wowtype.conf、user.tsv、history.tsv、custom.tsv、emoji-recent.txt、translate.key、
//                                   words.domain.lm、wetype\nlrgpt2_nnlm.bin、models\、llama\、wowtype.log
//   （测试时 WOWTYPE_DATA_DIR 可以换数据目录，和 macOS 一样）

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "../common/config.h"
#include "wowtype_core.h"

namespace wowtype::tsf {

class EngineHost {
public:
    static EngineHost &instance();

    /// 调引擎、会话前拿着（递归锁：同一线程里的回调可以再拿）。
    std::recursive_mutex &mutex() { return mutex_; }

    /// 第一次用时在后台载入词库（几十 MB，不卡住应用）；载入好之前 engine() 是 nullptr。
    void ensureLoading();
    WowtypeEngineHandle *engine() const { return engine_.load(); }

    /// 看看设置文件、自定义词改了没有（按键时调，只比文件的修改时间）。改了 configGeneration 加一。
    void checkForChanges();
    const Config &config() const { return config_; }
    uint64_t configGeneration() const { return configGeneration_; }

    /// 互译的翻译器（设置里第一次打开 Translate 时建，之后一直留着，所有输入框共用）。
    WowtypeTranslator *translator() const { return translator_; }

    /// 上屏里有学到的词：攒够 20 次存一次（应用随时可能退出）。忘掉了学错的词：马上存。
    void didCommit();
    void didForget();
    /// 存用户词库和输入习惯（核心的后台线程写）。wait：等写完。
    void save(bool wait = false);

    /// 剪贴板历史（设置里 ClipboardHistory 开着时）：看一眼系统剪贴板，有新的交给核心。
    void sampleClipboard();

    /// 打字统计（设置里 TypingStats 开着时，默认关）：只在本机按天记计数，不记打的内容。
    /// code 是扫描码（扩展键或上 0xE000）；密码框里的不算。统计关着时什么都不做。
    void recordKey(uint32_t code, bool sensitive);
    void recordCommit(const std::string &text, const std::string &source, const std::string &scheme, int index,
                      bool sensitive);
    /// 把攒着的统计交给核心的写文件线程（失焦时），不等它写。
    void flushStats();

    std::wstring dataDir() const;
    std::wstring configPath() const { return dataDir() + L"\\wowtype.conf"; }
    /// 安装包里的数据文件；不在是空串。
    std::wstring resource(const wchar_t *name) const;

private:
    EngineHost() = default;
    void loadConfig();
    void applyEngineSettings(WowtypeEngineHandle *engine);
    void applyTranslateSettings();
    WowtypeEngineHandle *makeEngine();
    std::wstring lexiconPath() const;

    std::recursive_mutex mutex_;
    std::atomic<WowtypeEngineHandle *> engine_{nullptr};
    bool loading_ = false;
    Config config_;
    uint64_t configGeneration_ = 0;
    FILETIME configStamp_{};
    FILETIME customStamp_{};
    bool conversionLoaded_ = false;
    bool wubiLoaded_ = false;
    WowtypeTranslator *translator_ = nullptr;
    WowtypeStats *stats_ = nullptr;
    int commitsSinceSave_ = 0;
    DWORD clipboardSequence_ = 0;
    std::string lastClipboard_;
};

/// 文件的修改时间；不在时全 0。
FILETIME fileStamp(const std::wstring &path);
bool fileExists(const std::wstring &path);

} // namespace wowtype::tsf
