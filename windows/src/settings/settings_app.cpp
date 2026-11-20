// WowType 设置（wowtype-settings.exe）：和输入法分开的进程（输入法装在别人的进程里，不在那里开窗口）。
//
// 页面照 Linux 设置界面、macOS 设置窗口分：常规、候选、输入、模糊音、标点与表情、语音、翻译。控件按 common/config 的
// 设置项表生成（开关是勾选框、枚举是下拉框、数字和字符串是输入框），所以 spec/config.json 里 Windows 认的项这里都能改。
// 保存时只写改过的键（Config::merged：窗口开着时别处改了文件，别处改的留着），注释、不认识的键原样留着，
// 输入法下一次按键时自己重新读。互译的 API Key 存在同一个目录的 translate.key，不写进 wowtype.conf。

#include <windows.h>

#include <bcrypt.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app_update.h"
#include "config.h"
#include "custom_words.h"
#include "stats_view.h"
#include "utf.h"
#include "wowtype_core.h"
#include "../win/updater.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace wowtype;

namespace {

constexpr wchar_t kClassName[] = L"WowTypeSettingsWindow";
constexpr int kIdTabs = 100;
constexpr int kIdSave = 101;
constexpr int kIdClose = 102;
constexpr int kIdCustom = 103;
constexpr int kIdDataDir = 104;
constexpr int kIdApiKey = 105;
constexpr int kIdWordList = 106;
constexpr int kIdWord = 107;
constexpr int kIdCode = 108;
constexpr int kIdWordAdd = 109;
constexpr int kIdWordRemove = 110;
constexpr int kIdWordImport = 111;
constexpr int kIdNnlm = 112;
constexpr int kIdVoiceModel = 113;
constexpr int kIdStatsText = 114;
constexpr int kIdStatsRefresh = 115;
constexpr int kIdStatsClear = 116;
constexpr int kIdDiagnostics = 117;
constexpr int kIdVoiceDownload = 118;
constexpr UINT kMessageVoiceDone = WM_APP + 1;

// 离线语音包（微信输入法资源 CDN，和 voice/src/wtlocal/pack.rs、Linux 设置界面的一样；windows/test.sh 核对）。
// 模型是腾讯的，不随安装包分发，用户自己点下载
constexpr wchar_t kVoicePackUrl[] =
    L"https://download.z.weixin.qq.com/publish_words/v1/ime_asr_decoder_model/20260710220526250_ime_asr_decoder_model.apk";
constexpr unsigned long long kVoicePackSize = 102920639ULL;
constexpr char kVoicePackMd5[] = "28386481eb94ff73b28ee3949c0cf690";

constexpr int kIdLocalModel = 119;
constexpr int kIdUpdateCheck = 120;
constexpr int kIdUpdateInstall = 121;
constexpr UINT kMessageUpdateChecked = WM_APP + 4;
constexpr UINT kMessageUpdateDownloaded = WM_APP + 5;
constexpr UINT kMessageStartUpdate = WM_APP + 6;
constexpr UINT kMessageLocalDone = WM_APP + 2;
constexpr UINT kMessageProgress = WM_APP + 3;

// 本地翻译模型（腾讯混元 HY-MT 1.5 1.8B，GGUF）和 llama.cpp 的 Windows 包：和 Linux 设置界面（settings/wowtype-config 的
// LOCAL_MODEL_*、LLAMA_BUILD）、macOS 的 LocalModel.swift 同一个模型、同一个 llama.cpp 构建（windows/test.sh 核对）
constexpr char kLocalModelFile[] = "HY-MT1.5-1.8B-Q4_K_M.gguf";
constexpr wchar_t kLocalModelUrl[] =
    L"https://huggingface.co/tencent/HY-MT1.5-1.8B-GGUF/resolve/main/HY-MT1.5-1.8B-Q4_K_M.gguf";
constexpr char kLocalModelSha256[] = "4383ac0c3c8e476de98ff979c2a3f069f8c4fb385e7860cf2d28da896cc477c7";
constexpr char kLlamaBuild[] = "b12110";
#if defined(_M_ARM64)
constexpr wchar_t kLlamaUrl[] = L"https://github.com/ggml-org/llama.cpp/releases/download/b12110/llama-b12110-bin-win-cpu-arm64.zip";
constexpr char kLlamaSha256[] = "f8d20fb07d0b499988ced8963afcd5ded2d2cdd9983b757fbc7e51c4abd722e8";
#else
constexpr wchar_t kLlamaUrl[] = L"https://github.com/ggml-org/llama.cpp/releases/download/b12110/llama-b12110-bin-win-cpu-x64.zip";
constexpr char kLlamaSha256[] = "98901e327c252875accaf2d1bf364e10e868843b0b63468baad1aa945cdc5da2";
#endif

#ifndef WOWTYPE_VERSION
#define WOWTYPE_VERSION "dev"
#endif
using namespace wowtype;
constexpr int kIdFirstSetting = 1000;

std::wstring widen(const std::string &s) {
    const std::u16string u = utf8To16(s);
    return std::wstring(u.begin(), u.end());
}

std::string narrow(const std::wstring &s) { return utf16To8(std::u16string(s.begin(), s.end())); }

struct Page {
    const wchar_t *title;
    std::vector<const char *> keys;
};

const std::vector<Page> &pages() {
    static const std::vector<Page> p = {
        {L"常规", {"InputScheme", "ShuangpinScheme", "ShuangpinShowKeys", "ShuangpinAbbrev", "WubiAutoCommit", "WubiHint",
                   // 默认状态（和 Linux、macOS 设置一样，#254）：换窗口时的中 / 英、全角还是半角标点
                   "WindowMode", "PunctuationStyle", "InitialMode", "ShiftToggle", "ToggleKey", "Traditional",
                   "Lexicon", "TypingStats"}},
        {L"候选", {"CandidateLayout", "PageSize", "CandidateFontSize", "ArrowKeys", "PageKeysMinusEqual",
                   "PageKeysBracket", "PageKeysCommaPeriod", "PageKeysTab", "SelectKeys", "PickChar", "Association"}},
        {L"输入", {"TypoCorrection", "RareExtChars", "DateTime", "Calculator", "AutoSpace", "AutoSpaceLetters",
                   "AutoSpaceDigits", "EmojiSuggest"}},
        {L"模糊音", {"Fuzzy", "FuzzyZh", "FuzzyCh", "FuzzySh", "FuzzyNL", "FuzzyFH", "FuzzyRL", "FuzzyAn", "FuzzyEn",
                     "FuzzyIn", "FuzzyIan", "FuzzyUan", "FuzzyUn", "FuzzyAnAi", "FuzzyHuangWang", "FuzzyUnIong",
                     "FuzzyEngOng"}},
        {L"标点与表情", {"AutoPairPunct", "EmojiKey", "Toolbar", "ClipboardHistory"}},
        {L"语音", {"Voice", "VoiceKey", "VoiceCopy"}},
        {L"翻译", {"Translate", "TranslateModeKey", "TranslateTarget", "TranslateLocal", "TranslateLocalModel",
                   "TranslateLlamaServer", "TranslateIdleMinutes", "TranslateBaseUrl", "TranslateModel",
                   "TranslateFormal"}},
    };
    return p;
}

/// 设置项的名字（和 Linux 设置界面、macOS 设置窗口的叫法一样）。
const std::map<std::string, std::wstring> &labels() {
    static const std::map<std::string, std::wstring> l = {
        {"InputScheme", L"输入方案"},
        {"ShuangpinScheme", L"双拼方案"},
        {"ShuangpinShowKeys", L"双拼编辑区显示打的键（不翻成全拼）"},
        {"ShuangpinAbbrev", L"双拼简拼（只打声母，sm 出「什么」）"},
        {"WubiAutoCommit", L"五笔四码唯一时自动上屏"},
        {"WubiHint", L"五笔候选后面显示还要打的码"},
        {"WindowMode", L"默认中/英文（换到新窗口、新应用时）"},
        {"InitialMode", L"切到 WowType 时"},
        {"ShiftToggle", L"单按 Shift 切换中 / 英"},
        {"ToggleKey", L"另设切换中 / 英的快捷键"},
        {"Traditional", L"输出繁体字"},
        {"Lexicon", L"词库文件（留空用自带的）"},
        {"TypingStats", L"打字统计（只在本机记计数，不记内容）"},
        {"CandidateLayout", L"候选排列"},
        {"PageSize", L"每页候选个数（3–9）"},
        {"CandidateFontSize", L"候选字号（10–40）"},
        {"ArrowKeys", L"打字途中方向键"},
        {"PageKeysMinusEqual", L"- = 翻页"},
        {"PageKeysBracket", L"[ ] 翻页"},
        {"PageKeysCommaPeriod", L", . 翻页"},
        {"PageKeysTab", L"Tab、Shift+Tab 翻页"},
        {"SelectKeys", L"分号、引号选第 2、3 个"},
        {"PickChar", L"以词定字（[ ] 取首字、末字）"},
        {"Association", L"上屏以后的联想"},
        {"TypoCorrection", L"按错键纠错"},
        {"RareExtChars", L"出生僻字（CJK 扩展区）"},
        {"DateTime", L"日期时间候选（riqi、shijian）"},
        {"Calculator", L"V 模式计算器"},
        {"AutoSpace", L"中英文之间自动加空格"},
        {"AutoSpaceLetters", L"　汉字和字母之间"},
        {"AutoSpaceDigits", L"　汉字和数字之间"},
        {"EmojiSuggest", L"打字时推荐 emoji"},
        {"Fuzzy", L"打开模糊音"},
        {"FuzzyZh", L"z = zh"},
        {"FuzzyCh", L"c = ch"},
        {"FuzzySh", L"s = sh"},
        {"FuzzyNL", L"n = l"},
        {"FuzzyFH", L"f = h"},
        {"FuzzyRL", L"r = l"},
        {"FuzzyAn", L"an = ang"},
        {"FuzzyEn", L"en = eng"},
        {"FuzzyIn", L"in = ing"},
        {"FuzzyIan", L"ian = iang"},
        {"FuzzyUan", L"uan = uang"},
        {"FuzzyUn", L"un = ong"},
        {"FuzzyAnAi", L"an = ai"},
        {"FuzzyHuangWang", L"huang = wang"},
        {"FuzzyUnIong", L"un = iong"},
        {"FuzzyEngOng", L"eng = ong"},
        {"PunctuationStyle", L"默认全角/半角"},
        {"AutoPairPunct", L"中文标点自动配对"},
        {"EmojiKey", L"表情面板快捷键"},
        {"Toolbar", L"v + 空格打开工具栏"},
        {"ClipboardHistory", L"记剪贴板历史（只在内存里）"},
        {"Voice", L"语音输入"},
        {"VoiceKey", L"语音键（Alt_R、Control_R、F8……）"},
        {"VoiceCopy", L"识别结果也复制到剪贴板"},
        {"Translate", L"互译（Shift 在中 → 英 → 译之间切）"},
        {"TranslateModeKey", L"进出「译」的快捷键"},
        {"TranslateTarget", L"译成"},
        {"TranslateLocal", L"用本地翻译模型（离线）"},
        {"TranslateLocalModel", L"本地模型文件（留空用数据目录里的）"},
        {"TranslateLlamaServer", L"llama-server（留空自己找）"},
        {"TranslateIdleMinutes", L"本地模型闲置几分钟后释放（0 不释放）"},
        {"TranslateBaseUrl", L"翻译服务地址（OpenAI 格式）"},
        {"TranslateModel", L"模型"},
        {"TranslateFormal", L"译文用正式语气"},
    };
    return l;
}

/// 枚举值的叫法；没列的照原样显示。
std::wstring valueName(const std::string &key, const std::string &value) {
    static const std::map<std::string, std::wstring> names = {
        {"InputScheme/Quanpin", L"拼音输入"},     {"InputScheme/Shuangpin", L"双拼输入"},
        {"InputScheme/Wubi86", L"五笔输入"},      {"ShuangpinScheme/Ziranma", L"自然码"},
        {"ShuangpinScheme/Xiaohe", L"小鹤双拼"},  {"ShuangpinScheme/Microsoft", L"微软双拼"},
        {"ShuangpinScheme/Sogou", L"搜狗双拼"},   {"ShuangpinScheme/ABC", L"智能 ABC"},
        {"ShuangpinScheme/Ziguang", L"紫光双拼"}, {"ShuangpinScheme/PinyinJiajia", L"拼音加加"},
        {"InitialMode/Chinese", L"中文"},         {"InitialMode/English", L"英文"},
        {"InitialMode/Last", L"和上次一样"},      {"WindowMode/Same", L"和上次一样"},
        {"WindowMode/Chinese", L"中文"},          {"WindowMode/English", L"英文"},
        {"ShiftToggle/Both", L"左右 Shift 都行"},
        {"ShiftToggle/Left", L"只用左 Shift"},    {"ShiftToggle/Right", L"只用右 Shift"},
        {"ShiftToggle/Off", L"不用 Shift 切换"},  {"CandidateLayout/Horizontal", L"横排"},
        {"CandidateLayout/Vertical", L"竖排"},    {"ArrowKeys/Candidates", L"都用来选词、翻页"},
        {"ArrowKeys/Caret", L"左右键移动拼音里的光标"}, {"SelectKeys/Off", L"不用"},
        {"SelectKeys/Semicolon", L"分号选第 2 个"}, {"SelectKeys/SemicolonQuote", L"分号、引号选第 2、3 个"},
        {"PunctuationStyle/Fullwidth", L"中文标点（全角）"}, {"PunctuationStyle/Halfwidth", L"英文标点（半角）"},
        {"TranslateTarget/en", L"英语"},          {"TranslateTarget/ja", L"日语"},
        {"TranslateTarget/ko", L"韩语"},          {"TranslateTarget/fr", L"法语"},
        {"TranslateTarget/de", L"德语"},          {"TranslateTarget/es", L"西班牙语"},
        {"TranslateTarget/ru", L"俄语"},          {"TranslateTarget/pt", L"葡萄牙语"},
        {"TranslateTarget/it", L"意大利语"},      {"TranslateTarget/vi", L"越南语"},
        {"TranslateTarget/th", L"泰语"},          {"TranslateTarget/id", L"印尼语"},
        {"TranslateTarget/ar", L"阿拉伯语"},
    };
    const auto it = names.find(key + "/" + value);
    return it == names.end() ? widen(value) : it->second;
}

/// 设置窗口在安装目录里，词库在旁边的 data\（猜拼音用）。
std::wstring installDir() {
    wchar_t buffer[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    std::wstring path(buffer, n);
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring dataDir() {
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"WOWTYPE_DATA_DIR", buffer, MAX_PATH) > 0) {
        return buffer;
    }
    wchar_t *local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        dir = std::wstring(local) + L"\\WowType";
        CoTaskMemFree(local);
    }
    return dir;
}

std::optional<std::string> readFile(const std::wstring &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);
    }
    return text;
}

/// 整份写进去：先写临时文件再改名，不会留下写了一半的设置文件（输入法随时会读）。
bool writeFile(const std::wstring &path, const std::string &text) {
    const std::wstring temporary = path + L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << text;
        if (!out) {
            return false;
        }
    }
    return MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---------------------------------------------------------------- 统计页的图：最近 30 天的柱子、键盘热力图（和 macOS 的统计页一样）

constexpr wchar_t kChartClass[] = L"WowTypeStatsChart";

struct ChartData {
    std::vector<std::pair<std::string, long long>> days;
    std::map<std::string, long long> keys;
};
ChartData g_chart;

LRESULT CALLBACK chartProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message != WM_PAINT) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    const UINT dpi = GetDpiForWindow(hwnd);
    const auto px = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));
    HFONT font = CreateFontW(-px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    const int width = client.right - client.left;

    // 最近 30 天：每天一根柱子，最高的那天顶满
    RECT title{0, 0, width, px(18)};
    DrawTextW(dc, L"最近 30 天上屏的字数", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    const int height = client.bottom - client.top;
    const int chartTop = px(20);
    const int chartHeight = std::max(px(36), std::min(px(70), height / 4));
    long long most = 1;
    for (const auto &day : g_chart.days) {
        most = std::max(most, day.second);
    }
    HBRUSH bar = CreateSolidBrush(RGB(0x2F, 0x6F, 0xD6));
    const int n = static_cast<int>(g_chart.days.size());
    for (int i = 0; i < n; ++i) {
        const int left = width * i / std::max(n, 1);
        const int right = width * (i + 1) / std::max(n, 1) - px(2);
        const int h = static_cast<int>(chartHeight * g_chart.days[static_cast<std::size_t>(i)].second / most);
        RECT r{left, chartTop + chartHeight - std::max(h, g_chart.days[static_cast<std::size_t>(i)].second ? px(1) : 0),
               right, chartTop + chartHeight};
        FillRect(dc, &r, bar);
    }
    DeleteObject(bar);
    if (n > 0) {
        RECT first{0, chartTop + chartHeight + px(2), width / 2, chartTop + chartHeight + px(18)};
        RECT last{width / 2, first.top, width, first.bottom};
        DrawTextW(dc, widen(stats::dayLabel(g_chart.days.front().first)).c_str(), -1, &first, DT_LEFT | DT_SINGLELINE);
        DrawTextW(dc, L"今天", -1, &last, DT_RIGHT | DT_SINGLELINE);
    }

    // 键盘热力图：按得越多颜色越深（五档，和 macOS 一样）
    const int mapTop = chartTop + chartHeight + px(28);
    RECT mapTitle{0, mapTop - px(4), width, mapTop + px(14)};
    DrawTextW(dc, L"按键热力图（全部日子）", -1, &mapTitle, DT_LEFT | DT_SINGLELINE);
    long long hottest = 0;
    for (const auto &row : stats::keyboardRows()) {
        for (const auto &cap : row) {
            if (!cap.code.empty()) {
                const auto it = g_chart.keys.find(cap.code);
                hottest = std::max(hottest, it == g_chart.keys.end() ? 0LL : it->second);
            }
        }
    }
    const COLORREF levels[] = {RGB(0xEE, 0xEE, 0xEE), RGB(0xC6, 0xE2, 0xFF), RGB(0x8C, 0xC4, 0xFF),
                               RGB(0x4A, 0x9B, 0xF5), RGB(0x1F, 0x6F, 0xD6)};
    const double unit = width / 15.25;
    // 键的高度照剩下的地方算（6 排），最多是宽的 0.8 倍
    const int keyHeight = std::max(px(14), std::min(static_cast<int>(unit * 0.8), (height - mapTop - px(20)) / 6));
    int y = mapTop + px(18);
    for (const auto &row : stats::keyboardRows()) {
        double x = 0;
        for (const auto &cap : row) {
            const int left = static_cast<int>(x);
            x += cap.weight * unit;
            if (cap.code.empty()) {
                continue;
            }
            const auto it = g_chart.keys.find(cap.code);
            const int heat = stats::level(it == g_chart.keys.end() ? 0 : it->second, hottest);
            RECT r{left + px(1), y, static_cast<int>(x) - px(1), y + keyHeight - px(2)};
            HBRUSH brush = CreateSolidBrush(levels[heat]);
            FillRect(dc, &r, brush);
            DeleteObject(brush);
            SetTextColor(dc, heat >= 3 ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x33, 0x33, 0x33));
            DrawTextW(dc, widen(cap.label).c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        y += keyHeight;
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    EndPaint(hwnd, &ps);
    return 0;
}

struct Row {
    const SettingDef *def;
    HWND label = nullptr;
    HWND control = nullptr;
    int page = 0;
};

class SettingsWindow {
public:
    bool create(HINSTANCE instance);
    /// 输入法菜单里点了「有新版本…」：打开「常规」页，查一下、下载、装
    void startUpdate() { checkUpdate(true); }
    /// 打开某一页（--page 统计）：页名是标签上的字
    void openPage(const std::wstring &title) {
        int page = pageNamed(title.c_str());
        if (title == L"自定义词") {
            page = customPage();
        } else if (title == L"统计") {
            page = statsPage();
        }
        if (page >= 0) {
            TabCtrl_SetCurSel(tabs_, page);
            showPage(page);
        }
    }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);
    void build();
    void layout();
    void showPage(int page);
    void load();
    Config fromControls() const;
    bool save();
    int scale(int dip) const { return MulDiv(dip, static_cast<int>(dpi_), 96); }
    int customPage() const { return static_cast<int>(pages().size()); }
    int statsPage() const { return static_cast<int>(pages().size()) + 1; }
    void refreshStats();
    void clearStats();
    void copyDiagnostics();
    // 自定义词页
    void loadWords();
    bool saveWords();
    void addWord();
    void removeWord();
    void importWords();
    void wordChanged();
    void importNnlm();
    void importVoiceModel();
    void downloadVoiceModel();
    void downloadLocalModel();
    void showUpdateState();
    void checkUpdate(bool thenInstall);
    void installUpdate();
    bool updateAfterCheck_ = false;
    std::optional<update::Release> release_;
    int pageNamed(const wchar_t *title) const;
    HWND child(const wchar_t *cls, const wchar_t *text, DWORD style, int id);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HWND apiKeyLabel_ = nullptr;
    HWND apiKey_ = nullptr;
    HWND save_ = nullptr;
    HWND close_ = nullptr;
    HWND custom_ = nullptr;
    HWND dataDirButton_ = nullptr;
    HWND status_ = nullptr;
    HWND wordList_ = nullptr;
    HWND wordLabel_ = nullptr;
    HWND word_ = nullptr;
    HWND codeLabel_ = nullptr;
    HWND code_ = nullptr;
    HWND addWord_ = nullptr;
    HWND removeWord_ = nullptr;
    HWND importWords_ = nullptr;
    HWND nnlm_ = nullptr;
    HWND statsText_ = nullptr;
    HWND statsChart_ = nullptr;
    HWND statsRefresh_ = nullptr;
    HWND statsClear_ = nullptr;
    HWND diagnostics_ = nullptr;
    HWND voiceModel_ = nullptr;
    HWND voiceDownload_ = nullptr;
    HWND localModel_ = nullptr;
    HWND updateCheck_ = nullptr;
    HWND updateInstall_ = nullptr;
    bool downloading_ = false;
    CustomWords words_{std::nullopt};
    std::optional<std::string> lexicon_;
    bool codeTouched_ = false;
    bool settingCode_ = false;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    std::vector<Row> rows_;
    int page_ = 0;
    Config base_;
    std::string apiKeyLoaded_;
};

HWND SettingsWindow::child(const wchar_t *cls, const wchar_t *text, DWORD style, int id) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | style, 0, 0, 10, 10, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return h;
}

bool SettingsWindow::create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(0, kClassName, L"WowType 设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 10, 10, nullptr, nullptr, instance, this);
    if (!hwnd_) {
        return false;
    }
    dpi_ = GetDpiForWindow(hwnd_);
    font_ = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    build();
    load();
    layout();
    showPage(0);
    showUpdateState();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    return true;
}

void SettingsWindow::build() {
    tabs_ = child(WC_TABCONTROLW, L"", WS_VISIBLE | WS_CLIPSIBLINGS, kIdTabs);
    for (std::size_t i = 0; i < pages().size(); ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(pages()[i].title);
        SendMessageW(tabs_, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }
    int id = kIdFirstSetting;
    for (std::size_t p = 0; p < pages().size(); ++p) {
        for (const char *key : pages()[p].keys) {
            const SettingDef *def = nullptr;
            for (const auto &d : settingDefs()) {
                if (std::string(d.key) == key) {
                    def = &d;
                }
            }
            if (!def) {
                continue;
            }
            Row row{def};
            row.page = static_cast<int>(p);
            const auto label = labels().count(key) ? labels().at(key) : widen(key);
            switch (def->type) {
            case SettingType::Bool:
                row.control = child(L"BUTTON", label.c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, id++);
                break;
            case SettingType::Enum:
                row.label = child(L"STATIC", label.c_str(), SS_LEFT, 0);
                row.control = child(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, id++);
                for (const auto &value : def->values) {
                    SendMessageW(row.control, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(valueName(key, value).c_str()));
                }
                break;
            default:
                row.label = child(L"STATIC", label.c_str(), SS_LEFT, 0);
                row.control = child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP |
                                                      (def->type == SettingType::Int ? ES_NUMBER : 0),
                                    id++);
                break;
            }
            rows_.push_back(row);
        }
    }
    apiKeyLabel_ = child(L"STATIC", L"API Key（存在 translate.key，不写进设置文件）", SS_LEFT, 0);
    apiKey_ = child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, kIdApiKey);
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(L"自定义词");
        SendMessageW(tabs_, TCM_INSERTITEMW, pages().size(), reinterpret_cast<LPARAM>(&item));
    }
    wordList_ = child(WC_LISTVIEWW, L"", WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                      kIdWordList);
    SendMessageW(wordList_, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT);
    for (int i = 0; i < 2; ++i) {
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = const_cast<wchar_t *>(i == 0 ? L"编码" : L"词");
        column.cx = scale(i == 0 ? 160 : 300);
        SendMessageW(wordList_, LVM_INSERTCOLUMNW, i, reinterpret_cast<LPARAM>(&column));
    }
    wordLabel_ = child(L"STATIC", L"词（换行写成 \\n）", SS_LEFT, 0);
    word_ = child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kIdWord);
    codeLabel_ = child(L"STATIC", L"编码（拼音，自动猜；也可以是 mail 这类字母）", SS_LEFT, 0);
    code_ = child(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kIdCode);
    addWord_ = child(L"BUTTON", L"添加", BS_PUSHBUTTON | WS_TABSTOP, kIdWordAdd);
    removeWord_ = child(L"BUTTON", L"删除选中的", BS_PUSHBUTTON | WS_TABSTOP, kIdWordRemove);
    importWords_ = child(L"BUTTON", L"导入…", BS_PUSHBUTTON | WS_TABSTOP, kIdWordImport);
    custom_ = child(L"BUTTON", L"用记事本编辑", BS_PUSHBUTTON | WS_TABSTOP, kIdCustom);
    {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(L"统计");
        SendMessageW(tabs_, TCM_INSERTITEMW, pages().size() + 1, reinterpret_cast<LPARAM>(&item));
    }
    statsText_ = child(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, kIdStatsText);
    {
        WNDCLASSEXW chart{};
        chart.cbSize = sizeof chart;
        chart.lpfnWndProc = chartProc;
        chart.hInstance = instance_;
        chart.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        chart.lpszClassName = kChartClass;
        RegisterClassExW(&chart);
        statsChart_ = child(kChartClass, L"", 0, 0);
    }
    statsRefresh_ = child(L"BUTTON", L"刷新", BS_PUSHBUTTON | WS_TABSTOP, kIdStatsRefresh);
    statsClear_ = child(L"BUTTON", L"清空统计", BS_PUSHBUTTON | WS_TABSTOP, kIdStatsClear);
    updateCheck_ = child(L"BUTTON", L"检查更新", BS_PUSHBUTTON | WS_TABSTOP, kIdUpdateCheck);
    updateInstall_ = child(L"BUTTON", L"下载并安装新版本", BS_PUSHBUTTON | WS_TABSTOP, kIdUpdateInstall);
    diagnostics_ = child(L"BUTTON", L"复制诊断信息（版本、数据文件、打码后的设置、最近的日志）", BS_PUSHBUTTON | WS_TABSTOP,
                         kIdDiagnostics);
    voiceDownload_ = child(L"BUTTON", L"下载语音模型（约 100 MB，从微信输入法的服务器）…", BS_PUSHBUTTON | WS_TABSTOP,
                           kIdVoiceDownload);
    localModel_ = child(L"BUTTON", L"下载本地翻译模型（1.13 GB）和 llama-server…", BS_PUSHBUTTON | WS_TABSTOP, kIdLocalModel);
    nnlm_ = child(L"BUTTON", L"导入整句增强模型（微信输入法的 nlrgpt2_nnlm.bin）…", BS_PUSHBUTTON | WS_TABSTOP, kIdNnlm);
    voiceModel_ = child(L"BUTTON", L"导入语音模型（含 .xnet 和 dict.decoder.utf8.txt 的文件夹）…",
                        BS_PUSHBUTTON | WS_TABSTOP, kIdVoiceModel);
    dataDirButton_ = child(L"BUTTON", L"打开数据目录", BS_PUSHBUTTON | WS_VISIBLE | WS_TABSTOP, kIdDataDir);
    status_ = child(L"STATIC", L"", SS_LEFT | WS_VISIBLE, 0);
    save_ = child(L"BUTTON", L"保存", BS_DEFPUSHBUTTON | WS_VISIBLE | WS_TABSTOP, kIdSave);
    close_ = child(L"BUTTON", L"关闭", BS_PUSHBUTTON | WS_VISIBLE | WS_TABSTOP, kIdClose);
}

void SettingsWindow::layout() {
    const int width = scale(560);
    const int margin = scale(12);
    const int rowHeight = scale(30);
    const int tabTop = margin;
    const int top = tabTop + scale(36);
    std::vector<int> y(pages().size(), top);
    // 模糊音这一页的分组排两列
    std::vector<int> fuzzyIndex(pages().size(), 0);
    int bottom = top;
    for (auto &row : rows_) {
        int &at = y[static_cast<std::size_t>(row.page)];
        const bool twoColumns = std::wstring(pages()[static_cast<std::size_t>(row.page)].title) == L"模糊音" &&
                                std::string(row.def->key) != "Fuzzy";
        if (twoColumns) {
            int &n = fuzzyIndex[static_cast<std::size_t>(row.page)];
            const int col = n % 2;
            const int x = margin * 2 + col * (width / 2 - margin);
            MoveWindow(row.control, x, at, width / 2 - margin * 3, scale(24), FALSE);
            if (col == 1) {
                at += rowHeight;
            }
            ++n;
            bottom = std::max(bottom, at + rowHeight);
            continue;
        }
        if (row.label) {
            MoveWindow(row.label, margin * 2, at + scale(4), scale(250), scale(22), FALSE);
            const int height = row.def->type == SettingType::Enum ? scale(300) : scale(24);
            MoveWindow(row.control, margin * 2 + scale(260), at, width - margin * 4 - scale(260), height, FALSE);
        } else {
            MoveWindow(row.control, margin * 2, at, width - margin * 4, scale(24), FALSE);
        }
        at += rowHeight;
        bottom = std::max(bottom, at);
    }
    // 输入页、语音页最后各有一个导入模型的按钮
    using ButtonOnPage = std::pair<HWND, const wchar_t *>;
    for (const auto &[button, title] :
         {ButtonOnPage{nnlm_, L"输入"}, ButtonOnPage{voiceDownload_, L"语音"}, ButtonOnPage{voiceModel_, L"语音"},
          ButtonOnPage{localModel_, L"翻译"},
          ButtonOnPage{updateCheck_, L"常规"}, ButtonOnPage{updateInstall_, L"常规"}, ButtonOnPage{diagnostics_, L"常规"}}) {
        const auto page = static_cast<std::size_t>(pageNamed(title));
        MoveWindow(button, margin * 2, y[page] + scale(6), width - margin * 4, scale(28), FALSE);
        y[page] += rowHeight + scale(6);
        bottom = std::max(bottom, y[page]);
    }
    // 翻译页最后加 API Key
    const std::size_t translatePage = pages().size() - 1;
    MoveWindow(apiKeyLabel_, margin * 2, y[translatePage] + scale(4), scale(250), scale(22), FALSE);
    MoveWindow(apiKey_, margin * 2 + scale(260), y[translatePage], width - margin * 4 - scale(260), scale(24), FALSE);
    bottom = std::max(bottom, y[translatePage] + rowHeight);
    // 自定义词页：上面是列表，下面是加词的两个框和按钮
    // 1366×768 的屏幕上也要放得下（CI 的截图里 500 太高，下面的按钮被任务栏挡住）
    bottom = std::max(bottom, top + scale(420));
    const int inner = width - margin * 4;
    const int formTop = bottom - rowHeight * 3;
    MoveWindow(wordList_, margin * 2, top, inner, formTop - top - scale(8), FALSE);
    MoveWindow(wordLabel_, margin * 2, formTop + scale(4), scale(250), scale(22), FALSE);
    MoveWindow(word_, margin * 2 + scale(260), formTop, inner - scale(260), scale(24), FALSE);
    MoveWindow(codeLabel_, margin * 2, formTop + rowHeight + scale(4), scale(250), scale(22), FALSE);
    MoveWindow(code_, margin * 2 + scale(260), formTop + rowHeight, inner - scale(260), scale(24), FALSE);
    const int buttonRow = formTop + rowHeight * 2;
    MoveWindow(addWord_, margin * 2, buttonRow, scale(80), scale(26), FALSE);
    MoveWindow(removeWord_, margin * 2 + scale(88), buttonRow, scale(100), scale(26), FALSE);
    MoveWindow(importWords_, margin * 2 + scale(196), buttonRow, scale(80), scale(26), FALSE);
    MoveWindow(custom_, margin * 2 + scale(284), buttonRow, scale(110), scale(26), FALSE);
    // 统计页：一段摘要，下面两个按钮
    // 上面一段文字摘要，下面画图
    const int textHeight = scale(96);
    MoveWindow(statsText_, margin * 2, top, inner, textHeight, FALSE);
    MoveWindow(statsChart_, margin * 2, top + textHeight + scale(8), inner, buttonRow - top - textHeight - scale(16), FALSE);
    MoveWindow(statsRefresh_, margin * 2, buttonRow, scale(80), scale(26), FALSE);
    MoveWindow(statsClear_, margin * 2 + scale(88), buttonRow, scale(100), scale(26), FALSE);
    const int tabHeight = bottom - tabTop + margin;
    // 标签控件只当一排标签用：Windows 11 主题下它的页面底色是 #F9F9F9，和白底的控件对不上（CI 截图看出来一条条的），
    // 页面直接画在白色的窗口上
    MoveWindow(tabs_, margin, tabTop, width - margin * 2, scale(30), FALSE);
    const int buttons = tabTop + tabHeight + margin;
    MoveWindow(dataDirButton_, margin, buttons, scale(110), scale(28), FALSE);
    MoveWindow(status_, margin + scale(118), buttons + scale(5), width - scale(118) - margin * 2 - scale(176), scale(22), FALSE);
    MoveWindow(save_, width - margin - scale(168), buttons, scale(80), scale(28), FALSE);
    MoveWindow(close_, width - margin - scale(80), buttons, scale(80), scale(28), FALSE);
    RECT r{0, 0, width, buttons + scale(28) + margin};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi_);
    // 放在屏幕（去掉任务栏）中间，整个窗口都在屏幕里
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT work = monitor.rcWork;
    const int windowWidth = r.right - r.left;
    const int windowHeight = r.bottom - r.top;
    const int windowX =
        std::max(static_cast<int>(work.left), static_cast<int>(work.left + (work.right - work.left - windowWidth) / 2));
    const int windowY =
        std::max(static_cast<int>(work.top), static_cast<int>(work.top + (work.bottom - work.top - windowHeight) / 2));
    SetWindowPos(hwnd_, nullptr, windowX, windowY, windowWidth, windowHeight, SWP_NOZORDER);
}

void SettingsWindow::showPage(int page) {
    page_ = page;
    for (const auto &row : rows_) {
        const int show = row.page == page ? SW_SHOW : SW_HIDE;
        ShowWindow(row.control, show);
        if (row.label) {
            ShowWindow(row.label, show);
        }
    }
    const int translate = static_cast<int>(pages().size()) - 1;
    ShowWindow(apiKeyLabel_, page == translate ? SW_SHOW : SW_HIDE);
    ShowWindow(apiKey_, page == translate ? SW_SHOW : SW_HIDE);
    ShowWindow(nnlm_, page == pageNamed(L"输入") ? SW_SHOW : SW_HIDE);
    ShowWindow(diagnostics_, page == pageNamed(L"常规") ? SW_SHOW : SW_HIDE);
    ShowWindow(updateCheck_, page == pageNamed(L"常规") ? SW_SHOW : SW_HIDE);
    ShowWindow(updateInstall_, page == pageNamed(L"常规") ? SW_SHOW : SW_HIDE);
    for (HWND h : {statsText_, statsChart_, statsRefresh_, statsClear_}) {
        ShowWindow(h, page == statsPage() ? SW_SHOW : SW_HIDE);
    }
    if (page == statsPage()) {
        refreshStats();
    }
    ShowWindow(voiceModel_, page == pageNamed(L"语音") ? SW_SHOW : SW_HIDE);
    ShowWindow(voiceDownload_, page == pageNamed(L"语音") ? SW_SHOW : SW_HIDE);
    ShowWindow(localModel_, page == pageNamed(L"翻译") ? SW_SHOW : SW_HIDE);
    for (HWND h : {wordList_, wordLabel_, word_, codeLabel_, code_, addWord_, removeWord_, importWords_, custom_}) {
        ShowWindow(h, page == customPage() ? SW_SHOW : SW_HIDE);
    }
    if (page == customPage()) {
        loadWords();
    }
}

void SettingsWindow::load() {
    const auto text = readFile(dataDir() + L"\\wowtype.conf");
    base_ = text ? Config::parse(*text) : Config();
    for (const auto &row : rows_) {
        const std::string value = base_.value(row.def->key);
        switch (row.def->type) {
        case SettingType::Bool:
            SendMessageW(row.control, BM_SETCHECK, value == "True" ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case SettingType::Enum: {
            const auto &values = row.def->values;
            const auto it = std::find(values.begin(), values.end(), value);
            SendMessageW(row.control, CB_SETCURSEL, it == values.end() ? 0 : it - values.begin(), 0);
            break;
        }
        default:
            SetWindowTextW(row.control, widen(value).c_str());
            break;
        }
    }
    apiKeyLoaded_ = readFile(dataDir() + L"\\translate.key").value_or("");
    while (!apiKeyLoaded_.empty() && (apiKeyLoaded_.back() == '\n' || apiKeyLoaded_.back() == '\r')) {
        apiKeyLoaded_.pop_back();
    }
    SetWindowTextW(apiKey_, widen(apiKeyLoaded_).c_str());
}

Config SettingsWindow::fromControls() const {
    Config config = base_;
    for (const auto &row : rows_) {
        switch (row.def->type) {
        case SettingType::Bool:
            config.setValue(row.def->key,
                            SendMessageW(row.control, BM_GETCHECK, 0, 0) == BST_CHECKED ? "True" : "False");
            break;
        case SettingType::Enum: {
            const LRESULT i = SendMessageW(row.control, CB_GETCURSEL, 0, 0);
            if (i >= 0 && static_cast<std::size_t>(i) < row.def->values.size()) {
                config.setValue(row.def->key, row.def->values[static_cast<std::size_t>(i)]);
            }
            break;
        }
        default: {
            wchar_t buffer[2048] = {};
            GetWindowTextW(row.control, buffer, 2048);
            config.setValue(row.def->key, narrow(buffer));
            break;
        }
        }
    }
    return config;
}

bool SettingsWindow::save() {
    CreateDirectoryW(dataDir().c_str(), nullptr);
    const std::wstring path = dataDir() + L"\\wowtype.conf";
    const Config wanted = fromControls();
    const auto current = readFile(path);
    const std::string text = wanted.merged(base_, current ? &*current : nullptr);
    if (!current || text != *current) {
        if (!writeFile(path, text)) {
            SetWindowTextW(status_, L"保存失败：写不了设置文件");
            return false;
        }
    }
    wchar_t buffer[1024] = {};
    GetWindowTextW(apiKey_, buffer, 1024);
    const std::string key = narrow(buffer);
    if (key != apiKeyLoaded_) {
        if (!writeFile(dataDir() + L"\\translate.key", key.empty() ? std::string() : key + "\n")) {
            SetWindowTextW(status_, L"保存失败：写不了 translate.key");
            return false;
        }
        apiKeyLoaded_ = key;
    }
    // 读回来：写进去的就是窗口里的（数字夹到范围里、写错的快捷键保持原样）
    load();
    SetWindowTextW(status_, L"已保存，下一次按键时生效");
    return true;
}

void SettingsWindow::loadWords() {
    words_ = CustomWords(readFile(dataDir() + L"\\custom.tsv"));
    SendMessageW(wordList_, LVM_DELETEALLITEMS, 0, 0);
    int i = 0;
    for (const auto &entry : words_.entries()) {
        const std::wstring code = widen(entry.code);
        const std::wstring word = widen(CustomWords::shown(entry.word));
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t *>(code.c_str());
        SendMessageW(wordList_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        item.iSubItem = 1;
        item.pszText = const_cast<wchar_t *>(word.c_str());
        SendMessageW(wordList_, LVM_SETITEMTEXTW, i, reinterpret_cast<LPARAM>(&item));
        ++i;
    }
}

bool SettingsWindow::saveWords() {
    CreateDirectoryW(dataDir().c_str(), nullptr);
    if (!writeFile(dataDir() + L"\\custom.tsv", words_.text())) {
        SetWindowTextW(status_, L"保存失败：写不了 custom.tsv");
        return false;
    }
    loadWords();
    return true;
}

void SettingsWindow::wordChanged() {
    // 编码框没自己改过：照词库猜拼音（和 Linux、macOS 设置一样，词库没有的字猜不出就空着）
    if (codeTouched_) {
        return;
    }
    if (!lexicon_) {
        lexicon_ = readFile(installDir() + L"\\data\\lexicon.tsv").value_or("");
    }
    wchar_t buffer[1024] = {};
    GetWindowTextW(word_, buffer, 1024);
    const auto guess = CustomWords::guessPinyin(narrow(buffer), *lexicon_);
    settingCode_ = true;
    SetWindowTextW(code_, guess ? widen(*guess).c_str() : L"");
    settingCode_ = false;
}

void SettingsWindow::addWord() {
    wchar_t buffer[1024] = {};
    GetWindowTextW(word_, buffer, 1024);
    const std::string word = narrow(buffer);
    GetWindowTextW(code_, buffer, 1024);
    const std::string rawCode = narrow(buffer);
    if (!CustomWords::isValidWord(word)) {
        SetWindowTextW(status_, L"词不能空，也不能有 TAB（换行写成 \\n）");
        return;
    }
    const auto code = CustomWords::normalizeCode(rawCode);
    if (!code) {
        SetWindowTextW(status_, L"编码要是拼音（ni hao）或者字母（mail）");
        return;
    }
    const bool replaced = words_.add(*code, word);
    if (saveWords()) {
        SetWindowTextW(word_, L"");
        settingCode_ = true;
        SetWindowTextW(code_, L"");
        settingCode_ = false;
        codeTouched_ = false;
        SetWindowTextW(status_, replaced ? L"换了编码，下一次按键时生效" : L"加好了，下一次按键时生效");
    }
}

void SettingsWindow::removeWord() {
    const auto selected = static_cast<int>(SendMessageW(wordList_, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
    const auto entries = words_.entries();
    if (selected < 0 || static_cast<std::size_t>(selected) >= entries.size()) {
        SetWindowTextW(status_, L"先在列表里选一个词");
        return;
    }
    words_.remove(entries[static_cast<std::size_t>(selected)].word);
    if (saveWords()) {
        SetWindowTextW(status_, L"删掉了，下一次按键时生效");
    }
}

void SettingsWindow::importWords() {
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW open{};
    open.lStructSize = sizeof open;
    open.hwndOwner = hwnd_;
    open.lpstrFilter = L"自定义词（*.tsv;*.txt）\0*.tsv;*.txt\0所有文件\0*.*\0";
    open.lpstrFile = file;
    open.nMaxFile = static_cast<DWORD>(std::size(file));
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&open)) {
        return;
    }
    const auto text = readFile(file);
    const auto incoming = CustomWords::parseImport(text.value_or(""));
    const auto counts = words_.merge(incoming);
    if (saveWords()) {
        SetWindowTextW(status_, (L"导入：新增 " + std::to_wstring(counts.first) + L" 个，更新 " +
                                 std::to_wstring(counts.second) + L" 个")
                                    .c_str());
    }
}

namespace {

/// 文件的摘要（BCRYPT_MD5_ALGORITHM、BCRYPT_SHA256_ALGORITHM，小写十六进制）；读不了是空串。
std::string hashFile(const std::wstring &path, LPCWSTR algorithmName) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string out;
    if (BCryptOpenAlgorithmProvider(&algorithm, algorithmName, nullptr, 0) != 0) {
        return out;
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> buffer(1 << 20);
        bool ok = static_cast<bool>(in);
        while (ok && in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<ULONG>(in.gcount());
            if (n > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), n, 0) != 0) {
                ok = false;
            }
        }
        DWORD length = 0;
        ULONG got = 0;
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&length), sizeof length, &got, 0);
        std::vector<unsigned char> digest(length);
        if (ok && length > 0 && BCryptFinishHash(hash, digest.data(), length, 0) == 0) {
            for (const unsigned char b : digest) {
                char hex[3];
                std::snprintf(hex, sizeof hex, "%02x", b);
                out += hex;
            }
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return out;
}

/// 用系统自带的 tar.exe（Windows 10 1803 起有，认 zip）解包；不弹黑框。
bool runTar(const std::wstring &archive, const std::wstring &dir, const std::wstring &member) {
    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(system) + L"\\tar.exe\" -xf \"" + archive + L"\" -C \"" + dir + L"\"";
    if (!member.empty()) {
        command += L" " + member;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process)) {
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return code == 0;
}

/// 在 dir 里（含子目录）找模型（.xnet）和词表，拷到 target；返回拷了几个。
int collectVoiceFiles(const std::wstring &dir, const std::wstring &target) {
    int copied = 0;
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring path = dir + L"\\" + name;
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            copied += collectVoiceFiles(path, target);
        } else if ((name.size() > 5 && name.compare(name.size() - 5, 5, L".xnet") == 0) ||
                   name == L"dict.decoder.utf8.txt") {
            copied += CopyFileW(path.c_str(), (target + L"\\" + name).c_str(), FALSE) ? 1 : 0;
        }
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return copied;
}

void removeTree(const std::wstring &dir) {
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = found.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            const std::wstring path = dir + L"\\" + name;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeTree(path);
            } else {
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(dir.c_str());
}

/// 下载进度：每过一个百分点给窗口发一次（窗口在状态栏里写「语音模型：已下载 x%」）。
std::function<void(unsigned long long, unsigned long long)> reportProgress(HWND window, const wchar_t *what) {
    auto last = std::make_shared<int>(-1);
    return [window, what, last](unsigned long long done, unsigned long long total) {
        const int percent = total > 0 ? static_cast<int>(100ULL * done / total) : -1;
        if (percent >= 0 && percent != *last) {
            *last = percent;
            auto *text = new std::wstring(std::wstring(what) + L"：已下载 " + std::to_wstring(percent) + L"%");
            PostMessageW(window, kMessageProgress, 0, reinterpret_cast<LPARAM>(text));
        }
    };
}

/// llama-server 在不在：设置里写了路径、PATH 里有、数据目录的 llama\ 里有（和核心找的顺序一样）。
bool llamaServerFound(const std::wstring &dir, const std::string &configured) {
    if (!configured.empty()) {
        return GetFileAttributesW(widen(configured).c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"llama-server.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
        return true;
    }
    return GetFileAttributesW((dir + L"\\llama\\llama-server.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

void SettingsWindow::downloadLocalModel() {
    if (downloading_) {
        return;
    }
    if (MessageBoxW(hwnd_,
                    L"本地翻译用腾讯混元 HY-MT 1.5 1.8B（1.13 GB），从 Hugging Face 下载，WowType 不分发它。\n"
                    L"模型用的是《腾讯混元社区许可协议》：不适用于欧盟、英国和韩国境内，月活超过 1 亿的产品要另外向腾讯申请。\n"
                    L"找不到 llama-server 时一起下载 llama.cpp 官方的 Windows 程序（MIT 许可）。继续吗？",
                    L"下载本地翻译模型", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        return;
    }
    downloading_ = true;
    EnableWindow(localModel_, FALSE);
    SetWindowTextW(status_, L"正在下载本地翻译模型……");
    const std::wstring dir = dataDir();
    const Config config = fromControls();
    std::wstring model = widen(config.value("TranslateLocalModel"));
    if (model.empty()) {
        model = dir + L"\\models\\" + widen(kLocalModelFile);
    }
    const bool needLlama = !llamaServerFound(dir, config.value("TranslateLlamaServer"));
    HWND window = hwnd_;
    std::thread([dir, model, needLlama, window] {
        auto *result = new std::wstring(L"本地翻译模型装好了，在这一页打开「互译」和「用本地翻译模型」就能用");
        CreateDirectoryW(dir.c_str(), nullptr);
        if (GetFileAttributesW(model.c_str()) == INVALID_FILE_ATTRIBUTES) {
            const auto slash = model.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                SHCreateDirectoryExW(nullptr, model.substr(0, slash).c_str(), nullptr);
            }
            // 先下成 .download（断了留着 .download.part，下次接着下），校验过了才改成模型的名字
            const std::wstring staged = model + L".download";
            std::string error;
            if (!win::httpDownload(kLocalModelUrl, staged, reportProgress(window, L"本地翻译模型"), &error)) {
                *result = L"下载模型失败：" + widen(error);
            } else if (hashFile(staged, BCRYPT_SHA256_ALGORITHM) != kLocalModelSha256) {
                DeleteFileW(staged.c_str());
                *result = L"下载的模型校验不对（SHA256 不一致），已删除，请重试";
            } else if (!MoveFileExW(staged.c_str(), model.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                *result = L"模型下好了，但放不到 " + model;
            }
        }
        if (needLlama && result->find(L"装好了") != std::wstring::npos) {
            const std::wstring zip = dir + L"\\llama.zip";
            const std::wstring target = dir + L"\\llama";
            CreateDirectoryW(target.c_str(), nullptr);
            std::string error;
            if (!win::httpDownload(kLlamaUrl, zip, reportProgress(window, L"llama-server"), &error)) {
                *result = L"模型装好了，但下载 llama-server 失败：" + widen(error);
            } else if (hashFile(zip, BCRYPT_SHA256_ALGORITHM) != kLlamaSha256) {
                *result = L"下载的 llama-server 校验不对，已删除，请重试";
            } else if (!runTar(zip, target, L"")) {
                *result = L"解开 llama-server 失败（要 Windows 10 1803 以后自带的 tar.exe）";
            }
            DeleteFileW(zip.c_str());
        }
        PostMessageW(window, kMessageLocalDone, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void SettingsWindow::downloadVoiceModel() {
    if (downloading_) {
        return;
    }
    if (MessageBoxW(hwnd_,
                    L"语音识别用的是微信输入法的离线模型（腾讯的，不随 WowType 分发）。\n"
                    L"现在从微信输入法的服务器下载（约 100 MB），校验以后放到数据目录的 wtlocal\\。继续吗？",
                    L"下载语音模型", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        return;
    }
    downloading_ = true;
    EnableWindow(voiceDownload_, FALSE);
    SetWindowTextW(status_, L"正在下载语音模型（约 100 MB）……");
    const std::wstring dir = dataDir();
    HWND window = hwnd_;
    std::thread([dir, window] {
        auto *result = new std::wstring;
        CreateDirectoryW(dir.c_str(), nullptr);
        const std::wstring pack = dir + L"\\wtlocal.apk";
        const std::wstring tmp = dir + L"\\wtlocal.tmp";
        const std::wstring target = dir + L"\\wtlocal";
        WIN32_FILE_ATTRIBUTE_DATA info{};
        std::string error;
        if (!win::httpDownload(kVoicePackUrl, pack, reportProgress(window, L"语音模型"), &error)) {
            *result = L"下载失败：" + widen(error);
        } else if (!GetFileAttributesExW(pack.c_str(), GetFileExInfoStandard, &info) ||
                   ((static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow) != kVoicePackSize ||
                   hashFile(pack, BCRYPT_MD5_ALGORITHM) != kVoicePackMd5) {
            *result = L"下载的语音包校验不对，再试一次";
        } else {
            removeTree(tmp);
            CreateDirectoryW(tmp.c_str(), nullptr);
            // 外层是签名过的 zip（.apk），只取里面的模型包，再解开模型包
            const std::wstring inner = tmp + L"\\ime_asr_decoder_model.zip";
            const std::wstring unpacked = tmp + L"\\model";
            CreateDirectoryW(unpacked.c_str(), nullptr);
            CreateDirectoryW(target.c_str(), nullptr);
            if (!runTar(pack, tmp, L"ime_asr_decoder_model.zip") || !runTar(inner, unpacked, L"") ||
                collectVoiceFiles(unpacked, target) < 2) {
                *result = L"解包失败（要 Windows 10 1803 以后自带的 tar.exe）";
            } else {
                *result = L"语音模型装好了：按住语音键说话";
            }
            removeTree(tmp);
        }
        DeleteFileW(pack.c_str());
        PostMessageW(window, kMessageVoiceDone, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void SettingsWindow::showUpdateState() {
    // 输入法每天查过一次的结果也在 update-state 里：有新版就把「下载并安装」点亮
    const auto latest = update::version(win::readState(dataDir()).latest);
    const bool newer = latest && update::isNewer(*latest, WOWTYPE_VERSION);
    EnableWindow(updateInstall_, newer);
    SetWindowTextW(updateInstall_, newer ? (L"下载并安装 " + widen(*latest)).c_str() : L"下载并安装新版本");
}

void SettingsWindow::checkUpdate(bool thenInstall) {
    if (downloading_) {
        return;
    }
    downloading_ = true;
    updateAfterCheck_ = thenInstall;
    EnableWindow(updateCheck_, FALSE);
    SetWindowTextW(status_, L"正在检查更新……");
    const std::wstring dir = dataDir();
    const bool beta = fromControls().flag("BetaUpdates");
    HWND window = hwnd_;
    std::thread([dir, beta, window] {
        std::string error;
        auto release = win::fetchLatest(dir, beta, WOWTYPE_VERSION, &error);
        auto *result = new std::pair<std::optional<update::Release>, std::string>(std::move(release), error);
        PostMessageW(window, kMessageUpdateChecked, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void SettingsWindow::installUpdate() {
    if (!release_) {
        checkUpdate(true);
        return;
    }
    const auto version = update::version(release_->tag);
    if (!version) {
        return;
    }
    const std::string name = update::packageName(*version, win::arch());
    const auto exe = release_->assets.find(name);
    const auto sums = release_->assets.find(update::kChecksumFile);
    if (exe == release_->assets.end()) {
        SetWindowTextW(status_, widen(release_->tag + " 的发布页上没有 Windows 安装程序 " + name).c_str());
        return;
    }
    if (sums == release_->assets.end()) {
        SetWindowTextW(status_, L"发布页上缺少 SHA256SUMS，不安装");
        return;
    }
    const std::string notes = update::highlights(release_->notes);
    const std::wstring ask = L"WowType " + widen(*version) + L"（现在 " + widen(WOWTYPE_VERSION) + L"）\n\n" +
                             widen(notes.size() > 2000 ? notes.substr(0, 2000) + "…" : notes) +
                             L"\n\n下载新版的安装程序并运行吗？安装时会问一次管理员权限；正在用着输入法的应用重新打开以后用新版。";
    if (MessageBoxW(hwnd_, ask.c_str(), L"更新 WowType", MB_OKCANCEL | MB_ICONINFORMATION) != IDOK) {
        return;
    }
    downloading_ = true;
    EnableWindow(updateInstall_, FALSE);
    const std::wstring folder = dataDir() + L"\\update";
    CreateDirectoryW(dataDir().c_str(), nullptr);
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring path = folder + L"\\" + widen(name);
    const std::wstring exeUrl = widen(exe->second);
    const std::wstring sumsUrl = widen(sums->second);
    HWND window = hwnd_;
    std::thread([name, path, exeUrl, sumsUrl, window] {
        auto *result = new std::wstring;
        std::string error;
        const auto text = win::httpGet(sumsUrl, &error);
        const auto expected = text ? update::checksum(*text, name, &error) : std::nullopt;
        if (!expected) {
            *result = L"!" + widen(error);
        } else {
            DeleteFileW(path.c_str());
            int last = -1;
            const bool ok = win::httpDownload(
                exeUrl, path,
                [&](unsigned long long done, unsigned long long total) {
                    const int percent = total ? static_cast<int>(100 * done / total) : -1;
                    if (percent != last && percent >= 0) {
                        last = percent;
                        auto *p = new std::wstring(L"正在下载新版本：" + std::to_wstring(percent) + L"%");
                        PostMessageW(window, kMessageProgress, 0, reinterpret_cast<LPARAM>(p));
                    }
                },
                &error);
            if (!ok) {
                *result = L"!" + widen(error);
            } else if (win::sha256File(path) != *expected) {
                DeleteFileW(path.c_str());
                *result = L"!下载的安装程序校验不对（SHA256 不一致），已删除，请重试";
            } else {
                *result = path;
            }
        }
        PostMessageW(window, kMessageUpdateDownloaded, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void SettingsWindow::refreshStats() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char today[16];
    std::snprintf(today, sizeof today, "%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
    const auto doc = StatsDocument::parse(readFile(dataDir() + L"\\stats.json").value_or(""));
    SetWindowTextW(statsText_, widen(stats::summary(doc, today)).c_str());
    g_chart.days = stats::recentDays(doc, 30, today);
    g_chart.keys = stats::keyTotals(doc);
    InvalidateRect(statsChart_, nullptr, TRUE);
}

void SettingsWindow::clearStats() {
    if (MessageBoxW(hwnd_, L"清空所有打字统计？清空以后找不回来。", L"WowType", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
        return;
    }
    // 和另两端一样由核心清：清除计数加一（开着的输入法丢掉攒着的旧计数），再删掉 stats.json
    if (wowtype_stats_clear(narrow(dataDir() + L"\\stats.json").c_str()) != 0) {
        SetWindowTextW(status_, L"清空失败");
        return;
    }
    refreshStats();
    SetWindowTextW(status_, L"统计清空了");
}

void SettingsWindow::copyDiagnostics() {
    std::string text = "WowType " WOWTYPE_VERSION "（Windows）\n";
    {
        wchar_t build[64] = {};
        DWORD size = sizeof build;
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild",
                     RRF_RT_REG_SZ, nullptr, build, &size);
        text += "系统：Windows build " + narrow(build) + "\n";
    }
    text += "安装目录：" + narrow(installDir()) + "\n数据目录：" + narrow(dataDir()) + "\n\n数据文件：\n";
    for (const auto &[dir, names] :
         {std::pair<std::wstring, std::vector<const wchar_t *>>{installDir() + L"\\data",
                                                                 {L"lexicon.tsv", L"english.txt", L"words.lm", L"nnlm.bin",
                                                                  L"s2t.tsv", L"wubi86.tsv", L"DATA-VERSION"}},
          std::pair<std::wstring, std::vector<const wchar_t *>>{dataDir(),
                                                                 {L"user.tsv", L"history.tsv", L"custom.tsv",
                                                                  L"words.domain.lm", L"wetype\\nlrgpt2_nnlm.bin",
                                                                  L"wtlocal\\dict.decoder.utf8.txt", L"stats.json"}}}) {
        for (const wchar_t *name : names) {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            const std::wstring path = dir + L"\\" + name;
            if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
                const unsigned long long bytes = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
                text += "  " + narrow(name) + "  " + std::to_string(bytes) + " 字节\n";
            } else {
                text += "  " + narrow(name) + "  没有\n";
            }
        }
    }
    text += "\n设置（打过码）：\n" + diagnostics::maskConfig(readFile(dataDir() + L"\\wowtype.conf").value_or("（没有设置文件，全用默认值）")) + "\n";
    // 日志最后 80 行（核心的错误、载入词库的情况；日志里不记打的字）
    const std::string log = readFile(dataDir() + L"\\wowtype.log").value_or("");
    std::size_t at = log.size();
    for (int lines = 0; at > 0 && lines <= 80; ) {
        --at;
        if (log[at] == '\n') {
            ++lines;
        }
    }
    text += "\n最近的日志：\n" + log.substr(at == 0 ? 0 : at + 1);
    const std::wstring wide = widen(text);
    if (OpenClipboard(hwnd_)) {
        EmptyClipboard();
        const SIZE_T bytes = (wide.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void *p = GlobalLock(memory)) {
                std::memcpy(p, wide.c_str(), bytes);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                    GlobalFree(memory);
                }
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
        SetWindowTextW(status_, L"诊断信息复制好了，贴到问题里（看一眼有没有不想给别人看的）");
    }
}

int SettingsWindow::pageNamed(const wchar_t *title) const {
    for (std::size_t i = 0; i < pages().size(); ++i) {
        if (std::wstring(pages()[i].title) == title) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SettingsWindow::importNnlm() {
    // 和 Linux、macOS 一样：核心解析得了就能用，不核对版本号；放到数据目录的 wetype\，输入法下次载入词库时用它
    wchar_t file[MAX_PATH * 2] = {};
    OPENFILENAMEW open{};
    open.lStructSize = sizeof open;
    open.hwndOwner = hwnd_;
    open.lpstrFilter = L"整句增强模型（*.bin）\0*.bin\0所有文件\0*.*\0";
    open.lpstrFile = file;
    open.nMaxFile = static_cast<DWORD>(std::size(file));
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&open)) {
        return;
    }
    if (wowtype_nnlm_check(narrow(file).c_str()) < 0) {
        const char *error = wowtype_last_error();
        SetWindowTextW(status_, (L"读不出来：" + widen(error ? error : "")).c_str());
        return;
    }
    const std::wstring dir = dataDir() + L"\\wetype";
    CreateDirectoryW(dataDir().c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!CopyFileW(file, (dir + L"\\nlrgpt2_nnlm.bin").c_str(), FALSE)) {
        SetWindowTextW(status_, L"复制失败");
        return;
    }
    SetWindowTextW(status_, L"导入好了：重新打开应用（输入法重新载入词库）以后用它");
}

void SettingsWindow::importVoiceModel() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"选语音模型所在的文件夹（Linux 的 ~/.local/share/wowtype/wtlocal 拷过来的）");
    ComPtr<IShellItem> item;
    PWSTR picked = nullptr;
    if (FAILED(dialog->Show(hwnd_)) || FAILED(dialog->GetResult(&item)) ||
        FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) {
        return;
    }
    const std::wstring source = picked;
    CoTaskMemFree(picked);
    // 要有模型（.xnet）和词表，和核心的 wtlocal::is_ready 一样
    const bool hasDict = GetFileAttributesW((source + L"\\dict.decoder.utf8.txt").c_str()) != INVALID_FILE_ATTRIBUTES;
    WIN32_FIND_DATAW found{};
    HANDLE find = FindFirstFileW((source + L"\\*.xnet").c_str(), &found);
    const bool hasModel = find != INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE) {
        FindClose(find);
    }
    if (!hasDict || !hasModel) {
        SetWindowTextW(status_, L"这个文件夹里没有 .xnet 模型和 dict.decoder.utf8.txt");
        return;
    }
    const std::wstring target = dataDir() + L"\\wtlocal";
    CreateDirectoryW(dataDir().c_str(), nullptr);
    CreateDirectoryW(target.c_str(), nullptr);
    int copied = 0;
    find = FindFirstFileW((source + L"\\*").c_str(), &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                CopyFileW((source + L"\\" + found.cFileName).c_str(), (target + L"\\" + found.cFileName).c_str(), FALSE)) {
                ++copied;
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    SetWindowTextW(status_, (L"导入了 " + std::to_wstring(copied) + L" 个文件，按语音键就能说话").c_str());
}

LRESULT CALLBACK SettingsWindow::proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        static_cast<SettingsWindow *>(create->lpCreateParams)->hwnd_ = hwnd;
    }
    auto *self = reinterpret_cast<SettingsWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsWindow::handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        // 标签页是白底：说明文字、勾选框也用白底（不然是一条条灰的）
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    case WM_NOTIFY: {
        const auto *header = reinterpret_cast<NMHDR *>(lParam);
        if (header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
            showPage(static_cast<int>(SendMessageW(tabs_, TCM_GETCURSEL, 0, 0)));
        }
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdSave:
            save();
            return 0;
        case kIdClose:
            DestroyWindow(hwnd_);
            return 0;
        case kIdCustom: {
            // 自定义词：custom.tsv（拼音<TAB>词），格式和 Linux、macOS 一样；没有就建一个带说明的
            CreateDirectoryW(dataDir().c_str(), nullptr);
            const std::wstring path = dataDir() + L"\\custom.tsv";
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
                writeFile(path, "# 自定义词：一行一个，拼音<TAB>词[<TAB>频次]，例如\n# wo de you xiang\tme@example.com\n");
            }
            ShellExecuteW(hwnd_, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case kIdStatsRefresh:
            refreshStats();
            return 0;
        case kIdStatsClear:
            clearStats();
            return 0;
        case kIdDiagnostics:
            copyDiagnostics();
            return 0;
        case kIdUpdateCheck:
            checkUpdate(false);
            return 0;
        case kIdUpdateInstall:
            installUpdate();
            return 0;
        case kIdLocalModel:
            downloadLocalModel();
            return 0;
        case kIdVoiceDownload:
            downloadVoiceModel();
            return 0;
        case kIdNnlm:
            importNnlm();
            return 0;
        case kIdVoiceModel:
            importVoiceModel();
            return 0;
        case kIdWordAdd:
            addWord();
            return 0;
        case kIdWordRemove:
            removeWord();
            return 0;
        case kIdWordImport:
            importWords();
            return 0;
        case kIdWord:
            if (HIWORD(wParam) == EN_CHANGE) {
                wordChanged();
            }
            return 0;
        case kIdCode:
            if (HIWORD(wParam) == EN_CHANGE && !settingCode_) {
                codeTouched_ = GetWindowTextLengthW(code_) > 0;
            }
            return 0;
        case kIdDataDir:
            CreateDirectoryW(dataDir().c_str(), nullptr);
            ShellExecuteW(hwnd_, L"open", dataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        default:
            break;
        }
        break;
    case WM_ACTIVATE:
        // 切回窗口时别处改了文件（记事本、另一个设置窗口）：没改过的控件跟上文件
        if (LOWORD(wParam) != WA_INACTIVE && fromControls() == base_) {
            load();
        }
        break;
    case kMessageProgress: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring *>(lParam));
        SetWindowTextW(status_, text->c_str());
        return 0;
    }
    case kMessageStartUpdate:
        TabCtrl_SetCurSel(tabs_, pageNamed(L"常规"));
        showPage(pageNamed(L"常规"));
        startUpdate();
        return 0;
    case kMessageUpdateChecked: {
        std::unique_ptr<std::pair<std::optional<update::Release>, std::string>> result(
            reinterpret_cast<std::pair<std::optional<update::Release>, std::string> *>(lParam));
        downloading_ = false;
        EnableWindow(updateCheck_, TRUE);
        release_ = result->first;
        showUpdateState();
        if (!release_) {
            SetWindowTextW(status_, (L"检查更新失败：" + widen(result->second)).c_str());
            return 0;
        }
        const auto latest = update::version(release_->tag);
        if (latest && update::isNewer(*latest, WOWTYPE_VERSION)) {
            SetWindowTextW(status_, (L"有新版本 " + widen(*latest)).c_str());
            if (updateAfterCheck_) {
                installUpdate();
            }
        } else {
            SetWindowTextW(status_, (L"已经是最新版（" + widen(WOWTYPE_VERSION) + L"）").c_str());
        }
        return 0;
    }
    case kMessageUpdateDownloaded: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring *>(lParam));
        downloading_ = false;
        showUpdateState();
        if (!result->empty() && (*result)[0] == L'!') {
            SetWindowTextW(status_, (L"更新失败：" + result->substr(1)).c_str());
            return 0;
        }
        // 运行安装程序（它自己问管理员权限），设置窗口退出：安装程序要换掉这个程序
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", result->c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
            SetWindowTextW(status_, (L"打不开安装程序：" + *result).c_str());
            return 0;
        }
        DestroyWindow(hwnd_);
        return 0;
    }
    case kMessageLocalDone: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring *>(lParam));
        downloading_ = false;
        EnableWindow(localModel_, TRUE);
        SetWindowTextW(status_, result->c_str());
        return 0;
    }
    case kMessageVoiceDone: {
        std::unique_ptr<std::wstring> result(reinterpret_cast<std::wstring *>(lParam));
        downloading_ = false;
        EnableWindow(voiceDownload_, TRUE);
        SetWindowTextW(status_, result->c_str());
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace

/// 把 WowType 加进（或移出）当前用户的键盘列表（「设置 → 时间和语言 → 语言」里中文下面的键盘），没有中文就连中文一起加。
/// 安装程序装完、卸载前以原来的用户身份调（--enable-input / --disable-input），和 macOS 装完加进输入法、Linux 加进 fcitx5 一样。
/// 用系统 input.dll 的 InstallLayoutOrTip（Windows 文档里给输入法安装程序用的）。
int changeInputList(bool enable) {
    using InstallLayoutOrTipFn = BOOL(WINAPI *)(LPCWSTR, DWORD);
    HMODULE input = LoadLibraryW(L"input.dll");
    const auto install =
        input ? reinterpret_cast<InstallLayoutOrTipFn>(GetProcAddress(input, "InstallLayoutOrTip")) : nullptr;
    if (!install) {
        return 2;
    }
    // 0804:{文本服务 CLSID}{配置 GUID}（和 windows/src/tsf/globals.cpp 一样）
    const wchar_t *tip = L"0804:{A895B83F-8F60-4990-A48F-17821E3BC14A}{8106DB80-2DC6-4558-B368-86D26F78EC68}";
    constexpr DWORD kUninstall = 0x00000001; // ILOT_UNINSTALL
    const BOOL ok = install(tip, enable ? 0 : kUninstall);
    FreeLibrary(input);
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (wcsstr(GetCommandLineW(), L"--enable-input")) {
        return changeInputList(true);
    }
    if (wcsstr(GetCommandLineW(), L"--disable-input")) {
        return changeInputList(false);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // 已经开着：切过去
    if (HWND existing = FindWindowW(kClassName, nullptr)) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        if (wcsstr(GetCommandLineW(), L"--update")) {
            PostMessageW(existing, kMessageStartUpdate, 0, 0);
        }
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    SettingsWindow window;
    if (!window.create(instance)) {
        return 1;
    }
    if (wcsstr(GetCommandLineW(), L"--update")) {
        window.startUpdate();
    }
    // --page <页名>：打开就在那一页（菜单、CI 截图用）
    {
        int argc = 0;
        LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; argv && i + 1 < argc; ++i) {
            if (std::wstring(argv[i]) == L"--page") {
                window.openPage(argv[i + 1]);
            }
        }
        LocalFree(argv);
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(GetActiveWindow(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
