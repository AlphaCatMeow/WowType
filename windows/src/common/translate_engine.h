#pragma once

// 设置窗口「翻译」页的「翻译引擎」下拉框：本地翻译（离线）/ 哔哩哔哩（免费）/ 自定义翻译服务。
//
// 和 Linux 设置界面（settings/wowtype-config 的 TRANSLATE_ENGINES、translate_engine、on_translate_engine_changed）、
// macOS（WowtypeConfig.TranslateEngine、SettingsModel.setTranslateEngine）一样，选项、说明、哔哩哔哩的地址和模型以
// spec/config.json 的 translateEngines、translateBilibili 为准，scripts/test-config-contract.py --windows 对照
// （config_dump --translate、--engine）。不是单独的设置项：选中哪个从 TranslateLocal 和 TranslateBaseUrl 推出来，
// 换引擎时写上这个引擎的 TranslateLocal、TranslateBaseUrl、TranslateModel。
// API Key 存在 translate.key，换引擎不动它；核心看到哔哩哔哩的地址不带 Key（translate::request_key）。
// 不依赖 windows.h，Linux 上也编得过（windows/test.sh）。

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config.h"

namespace wowtype::translate {

/// 下拉框里的顺序（和规格一样）：0 本地，1 哔哩哔哩，2 自定义
enum class Engine { Local = 0, Bilibili = 1, Custom = 2 };

struct EngineInfo {
    Engine engine;
    const char *id;   // local、bilibili、custom（规格里的 id）
    const char *name; // 下拉框里的名字（UTF-8）
    const char *note; // 选中时下面那一行的说明
};

const std::vector<EngineInfo> &engines();

// 哔哩哔哩 Index-Translate 的免费公开接口（不用 Key），和 Linux 的 BILIBILI_*、macOS 的 WowtypeConfig.bilibili*、
// 核心的 translate::BILIBILI_* 一样
inline constexpr char kBilibiliUrl[] = "https://index-translate.bilibili.com/v1";
inline constexpr char kBilibiliHost[] = "index-translate.bilibili.com";
inline constexpr char kBilibiliModelPrefix[] = "Index-Translate";
inline constexpr char kBilibiliDefaultModel[] = "Index-Translate-2B";
inline constexpr char kBilibiliPrivacy[] = "要翻译的文字会发到哔哩哔哩的服务器（index-translate.bilibili.com）";

struct BilibiliModel {
    const char *id;   // 写进 TranslateModel 的
    const char *name; // 下拉框里的名字
};

/// 哔哩哔哩的模型，第一个是默认的（2B）
const std::vector<BilibiliModel> &bilibiliModels();

/// 网址里的主机名（小写，不带协议、用户名、端口；IPv6 去掉方括号），和核心的 url_host、macOS 的 urlHost 一样。
std::string urlHost(std::string_view url);
/// 接口地址是哔哩哔哩的吗（主机正好是 index-translate.bilibili.com）
bool isBilibili(std::string_view url);
/// 现在用的引擎：TranslateLocal 是本地；否则接口地址是哔哩哔哩的就是哔哩哔哩，别的都是自定义（同 Linux 的 translate_engine）
Engine engineOf(const Config &config);

/// 「测试连接」成功时那一行的字：✓ 你好 → 译文（1.2 秒），慢了加一句（和 macOS 的 TranslateService.testSummary 一样）
std::string testSummary(std::string_view text, double seconds);

/// 设置窗口里「翻译引擎」和它下面几个框的状态（和 Linux 设置的那几个控件、macOS 的 SettingsModel 一样）。
///
/// 自定义的地址、模型：换到哔哩哔哩时留着，换回自定义时照原样写回，只记在这次打开的设置窗口里（Linux 是留在输入框里，
/// macOS 是 customService）。打开时配置里是哔哩哔哩的地址，那不是自己填的，自定义的框空着。
/// 换到本地时只改 TranslateLocal，地址和模型还是上一个联网引擎的（Linux 换到本地时也不写它们）。
class EngineChoice {
public:
    /// 读进文件里的配置（打开窗口、保存后读回来、文件在别处改了）。地址是哔哩哔哩的时候自定义的框不动
    /// （第一次是空的，保存后读回来时还是这次填过的），否则照配置填；哔哩哔哩的模型下拉框反过来。
    void load(const Config &config);

    Engine engine() const { return engine_; }
    /// 下拉框换了引擎（同 Linux 的 on_translate_engine_changed）
    void select(Engine engine);

    const std::string &customUrl() const { return customUrl_; }
    const std::string &customModel() const { return customModel_; }
    void setCustomUrl(std::string url) { customUrl_ = std::move(url); }
    void setCustomModel(std::string model) { customModel_ = std::move(model); }

    /// 哔哩哔哩模型下拉框的选项（id, 名字）：规格里的，加上手写进配置文件的别的模型（列出来，不悄悄换掉）
    std::vector<std::pair<std::string, std::string>> bilibiliOptions() const;
    /// 下拉框选中第几个；模型空着是第一个（2B）
    int bilibiliIndex() const;
    void pickBilibili(int index);
    /// 下拉框选中的模型名（测试连接用）
    std::string bilibiliSelected() const;

    /// 写进 config：TranslateLocal、TranslateBaseUrl、TranslateModel。没动过下拉框和框时写出来的和读进来的一样
    void apply(Config &config) const;

private:
    Engine engine_ = Engine::Local;
    /// 地址和模型现在是哪个联网引擎的（选本地时也有：上一次用的那个）
    Engine remote_ = Engine::Custom;
    std::string customUrl_;
    std::string customModel_;
    /// 哔哩哔哩的地址：读进来的原样（大小写、带不带 /chat/completions），换过来时是 kBilibiliUrl
    std::string bilibiliUrl_ = kBilibiliUrl;
    /// 哔哩哔哩的模型：空着是读进来就空着（核心用 2B），没动过就不写成 2B
    std::string bilibiliModel_;
    /// 配置文件里写的、不在规格里的哔哩哔哩模型
    std::string extraModel_;
};

} // namespace wowtype::translate
