// WowType 设置（wowtype-settings.exe）：和输入法分开的进程（输入法装在别人的进程里，不在那里开窗口）。
//
// 页面照 Linux 设置界面、macOS 设置窗口分：常规、候选、输入、模糊音、标点与表情、语音、翻译。控件按 common/config 的
// 设置项表生成（开关是勾选框、枚举是下拉框、数字和字符串是输入框），所以 spec/config.json 里 Windows 认的项这里都能改。
// 保存时只写改过的键（Config::merged：窗口开着时别处改了文件，别处改的留着），注释、不认识的键原样留着，
// 输入法下一次按键时自己重新读。互译的 API Key 存在同一个目录的 translate.key，不写进 wowtype.conf。

#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "custom_words.h"
#include "utf.h"
#include "wowtype_core.h"

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
        {L"常规", {"InputScheme", "ShuangpinScheme", "ShuangpinShowKeys", "WubiAutoCommit", "WubiHint", "InitialMode",
                   "ShiftToggle", "ToggleKey", "Traditional", "Lexicon", "TypingStats"}},
        {L"候选", {"CandidateLayout", "PageSize", "CandidateFontSize", "ArrowKeys", "PageKeysMinusEqual",
                   "PageKeysBracket", "PageKeysCommaPeriod", "PageKeysTab", "SelectKeys", "PickChar", "Association"}},
        {L"输入", {"TypoCorrection", "RareExtChars", "DateTime", "Calculator", "AutoSpace", "AutoSpaceLetters",
                   "AutoSpaceDigits", "EmojiSuggest"}},
        {L"模糊音", {"Fuzzy", "FuzzyZh", "FuzzyCh", "FuzzySh", "FuzzyNL", "FuzzyFH", "FuzzyRL", "FuzzyAn", "FuzzyEn",
                     "FuzzyIn", "FuzzyIan", "FuzzyUan", "FuzzyUn", "FuzzyAnAi", "FuzzyHuangWang", "FuzzyUnIong",
                     "FuzzyEngOng"}},
        {L"标点与表情", {"PunctuationStyle", "AutoPairPunct", "EmojiKey", "Toolbar", "ClipboardHistory"}},
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
        {"WubiAutoCommit", L"五笔四码唯一时自动上屏"},
        {"WubiHint", L"五笔候选后面显示还要打的码"},
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
        {"PunctuationStyle", L"标点"},
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
        {"InitialMode/Last", L"和上次一样"},      {"ShiftToggle/Both", L"左右 Shift 都行"},
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

struct Row {
    const SettingDef *def;
    HWND label = nullptr;
    HWND control = nullptr;
    int page = 0;
};

class SettingsWindow {
public:
    bool create(HINSTANCE instance);

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
    // 自定义词页
    void loadWords();
    bool saveWords();
    void addWord();
    void removeWord();
    void importWords();
    void wordChanged();
    void importNnlm();
    void importVoiceModel();
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
    HWND voiceModel_ = nullptr;
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
    for (const auto &[button, title] : {ButtonOnPage{nnlm_, L"输入"}, ButtonOnPage{voiceModel_, L"语音"}}) {
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
    bottom = std::max(bottom, top + scale(360));
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
    const int tabHeight = bottom - tabTop + margin;
    MoveWindow(tabs_, margin, tabTop, width - margin * 2, tabHeight, FALSE);
    const int buttons = tabTop + tabHeight + margin;
    MoveWindow(dataDirButton_, margin, buttons, scale(110), scale(28), FALSE);
    MoveWindow(status_, margin + scale(118), buttons + scale(5), width - scale(118) - margin * 2 - scale(176), scale(22), FALSE);
    MoveWindow(save_, width - margin - scale(168), buttons, scale(80), scale(28), FALSE);
    MoveWindow(close_, width - margin - scale(80), buttons, scale(80), scale(28), FALSE);
    RECT r{0, 0, width, buttons + scale(28) + margin};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
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
    ShowWindow(voiceModel_, page == pageNamed(L"语音") ? SW_SHOW : SW_HIDE);
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
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // 已经开着：切过去
    if (HWND existing = FindWindowW(kClassName, nullptr)) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    SettingsWindow window;
    if (!window.create(instance)) {
        return 1;
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
