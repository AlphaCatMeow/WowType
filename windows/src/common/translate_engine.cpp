#include "translate_engine.h"

#include <algorithm>
#include <cmath>

namespace wowtype::translate {

namespace {

std::string trimmed(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

} // namespace

const std::vector<EngineInfo> &engines() {
    static const std::vector<EngineInfo> e = {
        {Engine::Local, "local", "本地翻译（离线）", "不联网、不用账号，打的字不出这台电脑；一句话等 1–3 秒"},
        {Engine::Bilibili, "bilibili", "哔哩哔哩（免费）",
         "哔哩哔哩的 Index-Translate，不用注册、不用 API Key；要联网，一句话不到 1 秒"},
        {Engine::Custom, "custom", "自定义翻译服务", "填你自己的翻译接口（DeepSeek、通义千问、Ollama……），要联网"},
    };
    return e;
}

const std::vector<BilibiliModel> &bilibiliModels() {
    static const std::vector<BilibiliModel> m = {
        {"Index-Translate-2B", "Index-Translate-2B（最快）"},
        {"Index-Translate-9B", "Index-Translate-9B"},
        {"Index-Translate-35B-A3B", "Index-Translate-35B-A3B（预览）"},
    };
    return m;
}

std::string urlHost(std::string_view url) {
    std::string s;
    for (const char c : url) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            s += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    if (const auto scheme = s.find("://"); scheme != std::string::npos) {
        s.erase(0, scheme + 3);
    }
    if (const auto end = s.find_first_of("/?#"); end != std::string::npos) {
        s.erase(end);
    }
    if (const auto at = s.rfind('@'); at != std::string::npos) {
        s.erase(0, at + 1);
    }
    if (!s.empty() && s.front() == '[') {
        const auto close = s.find(']');
        return s.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    }
    if (const auto colon = s.find(':'); colon != std::string::npos) {
        s.erase(colon);
    }
    return s;
}

bool isBilibili(std::string_view url) { return urlHost(url) == kBilibiliHost; }

Engine engineOf(const Config &config) {
    if (config.flag("TranslateLocal")) {
        return Engine::Local;
    }
    return isBilibili(config.value("TranslateBaseUrl")) ? Engine::Bilibili : Engine::Custom;
}

std::string testSummary(std::string_view text, double seconds) {
    // 第一行不空的（推理模型的说明、多余的换行不要）
    std::string_view first = text;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t end = std::min(text.find('\n', at), text.size());
        if (end > at) {
            first = text.substr(at, end - at);
            break;
        }
        at = end + 1;
    }
    const long tenths = std::lround(seconds * 10);
    std::string message = "✓ 你好 → " + std::string(first) + "（" + std::to_string(tenths / 10) + "." +
                          std::to_string(tenths % 10) + " 秒）";
    if (seconds > 2.5) {
        message += "，有点慢，可以换个小一点的模型";
    }
    return message;
}

void EngineChoice::load(const Config &config) {
    engine_ = engineOf(config);
    const std::string url = config.value("TranslateBaseUrl");
    const std::string model = config.value("TranslateModel");
    if (isBilibili(url)) {
        // 用着哔哩哔哩，或者用着它时切去了本地：模型是上次给它选的；自定义的框不是它，不动
        remote_ = Engine::Bilibili;
        bilibiliUrl_ = url;
        bilibiliModel_ = trimmed(model);
        const auto &known = bilibiliModels();
        if (!bilibiliModel_.empty() && std::none_of(known.begin(), known.end(), [&](const BilibiliModel &m) {
                return bilibiliModel_ == m.id;
            })) {
            extraModel_ = bilibiliModel_;
        }
    } else {
        remote_ = Engine::Custom;
        customUrl_ = url;
        customModel_ = model;
    }
}

void EngineChoice::select(Engine engine) {
    if (engine == engine_) {
        return;
    }
    engine_ = engine;
    if (engine == Engine::Bilibili) {
        // 写上哔哩哔哩的地址和下拉框里的模型（Linux 换过来时写 BILIBILI_URL 和选中的那一个）
        remote_ = Engine::Bilibili;
        bilibiliUrl_ = kBilibiliUrl;
        bilibiliModel_ = bilibiliSelected();
    } else if (engine == Engine::Custom) {
        // 自定义的地址、模型照框里的写回
        remote_ = Engine::Custom;
    }
}

std::vector<std::pair<std::string, std::string>> EngineChoice::bilibiliOptions() const {
    std::vector<std::pair<std::string, std::string>> options;
    for (const auto &m : bilibiliModels()) {
        options.emplace_back(m.id, m.name);
    }
    if (!extraModel_.empty()) {
        options.emplace_back(extraModel_, extraModel_);
    }
    return options;
}

int EngineChoice::bilibiliIndex() const {
    const auto options = bilibiliOptions();
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (options[i].first == bilibiliModel_) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

void EngineChoice::pickBilibili(int index) {
    const auto options = bilibiliOptions();
    if (index >= 0 && static_cast<std::size_t>(index) < options.size()) {
        bilibiliModel_ = options[static_cast<std::size_t>(index)].first;
    }
}

std::string EngineChoice::bilibiliSelected() const {
    return bilibiliOptions()[static_cast<std::size_t>(bilibiliIndex())].first;
}

void EngineChoice::apply(Config &config) const {
    config.setValue("TranslateLocal", engine_ == Engine::Local ? "True" : "False");
    if (remote_ == Engine::Bilibili) {
        config.setValue("TranslateBaseUrl", bilibiliUrl_);
        config.setValue("TranslateModel", bilibiliModel_);
    } else {
        config.setValue("TranslateBaseUrl", trimmed(customUrl_));
        config.setValue("TranslateModel", trimmed(customModel_));
    }
}

} // namespace wowtype::translate
