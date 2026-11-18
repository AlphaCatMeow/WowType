# Windows 版（第一阶段：能打字的 TSF 输入法）

> 状态（2026-11）：**预览版，还没在很多真机上用过**。CI 的「Windows 输入法（x64）」任务在 `windows-latest` 上编核心、输入法 DLL
> 和设置窗口，跑核心测试、按键测试和设置契约，打 zip 和安装程序，并真装、升级、卸载一遍；x64 的安装程序随发版进 Release。
> 打字本身还要在真的 Windows 上多试，见下面[没在真机上验证的](#没在真机上验证的)。

## 和另两端的关系

和 macOS 版一样，**输入行为都在 Rust 核心的输入会话里**（[ABI.md](ABI.md)「输入会话」、[PORTING.md](PORTING.md)），
Windows 外壳只做平台的事：把按键翻成会话的键码、照会话给的结果画编辑区和候选框、上屏、读光标旁边的字。
所以打字、选词、翻页、标点、Shift 切中英文、以词定字、工具栏和表情面板、联想、自动配对、互译这些行为和 Linux、macOS 是同一份代码。

| 部分 | 代码 | 说明 |
|---|---|---|
| 组字（和平台无关） | `windows/src/common/composer.*` | macOS `Shared/Composer.swift`（加 `Toolbar.swift` 的画法）的 C++ 版，一个函数一个函数对着写的 |
| 设置 | `windows/src/common/config.*` | 读 `wowtype.conf`，键名、取值、默认值、读法和另两端一样，`spec/config.json` 里 `platforms` 带 `windows` 的项 |
| 按键翻译 | `windows/src/common/keys.*` | 虚拟键码（`VK_*`）+ `ToUnicodeEx` 打出的字符 → 会话的键码（XKB keysym 子集） |
| TSF 文本服务 | `windows/src/tsf/text_service.*` | `ITfTextInputProcessorEx`、按键（`ITfKeyEventSink`）、组字和编辑会话、焦点 |
| 候选框 | `windows/src/tsf/candidate_window.*` | 分层的弹出窗口，Direct2D + DirectWrite 画（彩色 emoji），位置用 `ITfContextView::GetTextExt` |
| 任务栏「中 / 英」 | `windows/src/tsf/langbar.*` | `GUID_LBI_INPUTMODE` 按钮：左键切中英文，右键菜单切模式、打开设置文件和数据目录 |
| 设置窗口 | `windows/src/settings/settings_app.cpp` | 单独的 `wowtype-settings.exe`，控件按设置项表生成，写回用 `Config::merged`（macOS `merged(changesFrom:into:)` 的 C++ 版） |
| 引擎、数据 | `windows/src/tsf/engine_host.*` | 一个进程一份引擎（后台载入），设置文件改了下一次按键生效，学到的词每 20 次上屏存一次 |
| 注册 | `windows/src/tsf/register.cpp`、`dllmain.cpp` | `regsvr32`：COM 类、简体中文下的输入法配置、能力类别（含沉浸式 / UWP） |
| 测试 | `windows/tests/`、`windows/test.sh` | 按键测试照 `macos/Tests/main.swift`、`fcitx5-addon/test/key_harness.cpp` 的同名用例；Linux 上也能跑 |

核心为了在 Windows 上编过加的东西都在 `core/src/platform.rs`：本地时间用 CRT 的 `localtime_s`，家目录、缓存目录用
`%USERPROFILE%`、`%LOCALAPPDATA%`，起 `curl`、`llama-server` 时不弹黑框（`CREATE_NO_WINDOW`），`llama-server` 放进
一个「输入法所在的进程退出就一起杀掉」的 Job Object（Linux 上是 `PR_SET_PDEATHSIG`）。Linux、macOS 上行为不变。
互译和 Linux 一样用系统的 `curl` 发请求：Windows 10 1803 起系统自带 `C:\Windows\System32\curl.exe`，`-K -`（从标准输入读配置）
CI 上的核心测试会真的起 `curl.exe` 对着本机的假服务跑一遍。

## 编译

要 Visual Studio 2022（带「使用 C++ 的桌面开发」）、CMake 3.21+、Rust（`x86_64-pc-windows-msvc`）。在「x64 Native Tools Command Prompt」里：

```bat
set RUSTFLAGS=-C target-feature=+crt-static
cargo build --release -p wowtype-core --target-dir target\windows
cmake -S windows -B build\windows -A x64 -DWOWTYPE_CORE_LIB=%CD%\target\windows\release\wowtype_core.lib
cmake --build build\windows --config Release
```

产物在 `build\windows\Release\`：`wowtype.dll`（输入法）、`wowtype-windows-harness.exe`（按键测试）、
`wowtype-windows-config-dump.exe`（设置契约：`python scripts\test-config-contract.py --windows build\windows\Release\wowtype-windows-config-dump.exe`）。
核心和 DLL 都静态链 C 运行库，DLL 不依赖 VC++ 运行库。不想自己编的话，用 CI 的 `windows-x64` artifact 里的安装包（见下面）。

和平台无关的部分（组字、设置、按键翻译）在 Linux 上也能测：`windows/test.sh`（要 `c++` 和 Python 3）。

## 装上试试

**用安装程序**（推荐）：[Releases](https://github.com/Gloridust/WowType/releases) 里的 `WowType-<版本>-windows-x64-setup.exe`
（Inno Setup，`windows/installer/wowtype.iss`），双击装，会问一次管理员权限。装到 `%ProgramFiles%\WowType`、用 `DllRegisterServer`
注册、开始菜单放「WowType 设置」，「设置 → 应用」里能卸载（学到的词、设置留着）。升级直接装新版：正在用着输入法的应用锁着旧的
`wowtype.dll`，安装程序把它改名换下来、装上新的，这些应用重开以后用新版；下次安装时删掉换下来的。没有代码签名：第一次运行时
SmartScreen 会提示「Windows 已保护你的电脑」，点「更多信息」→「仍要运行」。静默安装：`setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`。
CI 每次都在 runner 上静默装一遍、建一个文本服务对象、在 DLL 被用着时升级一遍、再卸载，检查注册表、开始菜单和卸载项；
发版时 x64 的安装程序进 Release（ARM64 的在 `windows-arm64` artifact 里，没有在真机上试过，不进 Release）。

**用 zip**（开发、试用）：CI 每次跑完都有一个 `windows-x64` 的 artifact，里面是 `WowType-<版本>-windows-x64.zip`
（`windows/package.sh` 打的：`wowtype.dll`、`data\` 下的词库和许可证、`install.ps1`）。解开以后，以管理员身份打开 PowerShell，进到解开的目录：

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1             # 装到 %ProgramFiles%\WowType 并注册
powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall  # 卸载（用户数据留着）
```

装好以后在「设置 → 时间和语言 → 语言和区域 → 中文（简体，中国）→ 语言选项 → 键盘」里能看到 **WowType**（注册时默认启用），
用 `Win + 空格` 切过去。重装、升级直接再跑一遍 `install.ps1`：正在用着输入法的应用锁着旧的 `wowtype.dll`，
脚本把它改名留着、照样装上新的，这些应用重开以后用新的。装到 `%ProgramFiles%` 是因为商店应用、开始菜单的搜索框
只能载入这种位置的输入法。CI 每次都用这个脚本在 runner 上真装、建一个文本服务对象、再卸载一遍。

**手动装**（自己编的 DLL）：照 `install.ps1` 的样子放文件，再注册：

```
C:\Program Files\WowType\wowtype.dll
C:\Program Files\WowType\data\lexicon.tsv      词库（必须有）
C:\Program Files\WowType\data\english.txt      英文词表
C:\Program Files\WowType\data\words.lm         词级语言模型（可选，没有也能打，候选差一些）
C:\Program Files\WowType\data\nnlm.bin         整句增强模型（可选）
C:\Program Files\WowType\data\s2t.tsv          简繁表（繁体输出要，仓库里的 core\data\s2t.tsv）
C:\Program Files\WowType\data\wubi86.tsv       五笔码表（选五笔才要）
```

数据文件和 Linux、macOS 安装包里的是同一份：CI 的 `data` artifact（`scripts/make-data.sh`、`scripts/make-wubi.sh` 的输出），
或者用 7-Zip 打开 [Releases](https://github.com/Gloridust/WowType/releases) 里的 `wowtype-data_*.deb`，取出 `usr/share/wowtype/` 下的文件。

```bat
regsvr32 "C:\Program Files\WowType\wowtype.dll"
regsvr32 /u "C:\Program Files\WowType\wowtype.dll"
```

DLL 装进每个应用的进程里，载入以后不会卸载：换 DLL 前先关掉用着输入法的应用（或者像 `install.ps1` 那样先改名）。

## 检查更新

和 macOS 一样（`windows/src/common/app_update.*` 是 macOS `AppUpdate.swift` 的 C++ 版，`windows/src/win/updater.*` 联网）：
`CheckUpdate` 开着时，输入法每天最多问一次 GitHub 的发布页（切到 WowType 时后台查，不挡打字；`BetaUpdates` 开着时预发布也算），
结果记在数据目录的 `update-state`；有新版时任务栏「中」按钮的右键菜单里多一项「有新版本 …，下载并安装…」，点它打开设置窗口
「常规」页。设置窗口里「检查更新」随时查，「下载并安装」下载 `WowType-<版本>-windows-<架构>-setup.exe` 和同一页的 `SHA256SUMS`、
校验，给你看「这一版」的更新内容，确认后运行安装程序（问一次管理员权限），设置窗口自己退出好让安装程序换掉它。
开发版（版本号不是数字开头）不查。

## 设置和数据

数据目录是 `%LOCALAPPDATA%\WowType\`（`WOWTYPE_DATA_DIR` 可以换，测试用）：

| 文件 | 说明 |
|---|---|
| `wowtype.conf` | 设置，**键名、取值、默认值和 Linux、macOS 一样**，文件拷过来就能用（[CONFIG.md](CONFIG.md)）。用设置窗口改（开始菜单「WowType 设置」或任务栏「中」按钮右键 →「WowType 设置…」），也可以右键 →「打开设置文件」用记事本改；存了以后下一次按键生效 |
| `user.tsv`、`history.tsv` | 学到的词和输入习惯，格式同另两端 |
| `custom.tsv` | 自定义词，格式同 Linux（[CONFIG.md](CONFIG.md#自定义词)） |
| `emoji-recent.txt` | 最近用过的 emoji |
| `translate.key` | 互译的 API Key（一行） |
| `wetype\nlrgpt2_nnlm.bin` | 导入的微信输入法整句增强模型（有就先用它） |
| `models\HY-MT1.5-1.8B-Q4_K_M.gguf`、`llama\llama-server.exe` | 本地翻译模型和 llama.cpp（`TranslateLlamaServer` 留空时先在 PATH 里找） |
| `wtlocal\` | 语音识别模型（微信输入法的离线模型，不随安装包分发，自己放进来） |
| `stats.json` | 打字统计（`TypingStats=True` 才记，默认关），格式同另两端 |
| `update-state`、`update\` | 检查更新的结果、下载的新版安装程序 |
| `wowtype.log` | 日志（载入词库、设置、出错），超过 1 MB 从头写 |

和另两端一样的：`EmojiKey` 默认是 `Control+period`（Windows 键的组合多半被系统拿走，`Win + .` 是系统的表情面板，
所以和 macOS 一样把 `Super` 当 `Control`）；候选框是自己画的，所以和 macOS 一样认 `CandidateFontSize`（候选字号，默认 17）。

切到别处（点了别的地方、换了输入框、切走输入法）时打到一半的字母原样上屏，和 macOS 一样（Linux 上是丢掉，原因见 macOS 的说明）。

## 这一阶段还没有的

这些在 Linux、macOS 上有，Windows 上还没做（`spec/config.json` 里对应的项 `platforms` 没写 `windows`，读到了也不管）：

- 设置窗口（`wowtype-settings.exe`，`windows/src/settings/`）有了基本的：常规、候选、输入、模糊音、标点与表情、语音、翻译、自定义词八页，
  按 `spec/config.json` 里 Windows 认的项生成，只写改过的键（和 macOS 一样，别处改的、注释、不认识的键留着），API Key 存 `translate.key`。
  「自定义词」页能加词（照词库自动猜拼音）、删词、导入，规则和 macOS 的 `CustomWords` 一样（`windows/src/common/custom_words.*`）。
  「输入」页能导入整句增强模型（核心先试着解析，放到 `wetype\`）；「语音」页能一键下载语音模型（和 Linux 设置同一个地址，
  校验大小和 MD5，用系统的 `tar.exe` 解包到 `wtlocal\`），也能导入现成的模型文件夹；「统计」页看打字统计的摘要
  （今天、一共、最快的一天、连续天数、最近 7 天、按得最多的键，口径和 macOS 一样），能清空；「常规」页能复制诊断信息
  （版本、数据文件、打过码的设置、最近的日志）。
  「翻译」页能一键下载本地翻译模型（HY-MT 1.5 1.8B，和另两端同一个文件、同一个 SHA256，先给许可说明）和 llama.cpp 官方的
  Windows 程序（`b12110` 的 CPU 包，钉死 SHA256，解到 `llama\`；PATH 里、设置里有 llama-server 就不下）。
  还没有的：统计的图表和键盘热力图（只有文字摘要）、下载断了接着下（现在断了要重下）
- 检查更新的提醒只在任务栏菜单里，不弹系统通知（和 macOS 一样）
- 代码签名（安装程序和 DLL 都没签，SmartScreen 会拦一下）
- 打字统计（`TypingStats`）记是记了（`stats.json`，按键按扫描码记，ABI 1.13 起核心认 Windows 的键码），但还没有看统计的界面
- 本地翻译模型、整句增强模型的一键下载、导入（文件自己放进数据目录就能用）
- ARM64 只编不跑：CI 编出 `WowType-<版本>-windows-arm64.zip`（`windows-arm64` artifact），但没有 ARM64 的机器试过；
  ARM64 的 Windows 上，原生 ARM64 的应用载入 ARM64 的 DLL，模拟运行的 x64 应用要 x64 的 DLL，两个怎么一起装还没做
- 32 位、代码签名
- 全屏游戏里的候选框（`ITfUIElement`，让游戏自己画）

## 没在真机上验证的

CI 只能证明编得过、注册得上、和平台无关的部分（组字、设置、按键翻译）行为和另两端一样。下面这些都要在真的 Windows 上试：

- 系统能不能认出、切到 WowType；任务栏的输入指示器显示不显示「中 / 英 / 译」、点它能不能切、右键菜单
- 各类应用里打字：记事本、Word、Edge / Chrome（Chromium 的 TSF）、微信 / QQ、VS Code（Electron）、Windows Terminal、商店应用（UWP）、开始菜单的搜索框
- 编辑区显示和虚线下划线（显示属性）、光标在编辑区里的位置、选了一半时的编辑区
- `OnTestKeyDown` / `OnKeyDown` 的配合：同一个键会不会处理两遍或者漏掉（这里照「先问的时候就处理、记下结果」写的）
- 候选框：位置（`GetTextExt` 拿不到时退到系统光标）、多显示器、高 DPI、深色模式、彩色 emoji、鼠标点候选和翻页箭头、滚轮
- 单按 Shift 切中英文（左右 Shift、按住不放的自动重复、和 Ctrl / Alt / Win 一起按）
- 自动配对（`AutoPairPunct`）挪光标、成对删：用 `ITfContext::SetSelection` 和删字实现，各应用支不支持不知道
- 读光标前后的字（自动加空格、数字后的标点、自动配对的核对）、密码框 / 网址框的识别（InputScope）
- 应用结束组字（`OnCompositionTerminated`）时把打的字母原样上屏
- 语音输入：TSF 的按键回调收不收得到右 Alt（`WM_SYSKEYDOWN`），吃掉右 Alt 以后应用的菜单栏还会不会被激活；
  麦克风（「设置 → 隐私 → 麦克风 → 允许桌面应用访问」）；商店应用里没有麦克风权限，用不了
- 互译：`curl.exe` 在各种应用进程里能不能起（沙盒里的 UWP 应用起不了子进程，这时会报翻译失败）、本地模型的 `llama-server` 跟着应用退出
- 沉浸式 / UWP 应用里：数据目录会被重定向到应用自己的容器，读不到 `%LOCALAPPDATA%\WowType` 的设置和学到的词（只用自带的词库、默认设置）
- 同一个进程几个界面线程（资源管理器）共用一个引擎（用锁串起来）
- 每个应用进程各载一份词库（几十 MB 内存）、各自学词。存盘前先把别的进程存过的 `user.tsv`、`history.tsv` 读进来合并
  （用户词取并集，输入习惯同一条留最近用过的，核心的 `load_history` 这样合并），所以几个应用里学到的词都会留下；
  但一个应用里刚学到的词，要等它存盘（每 20 次上屏、切走输入法时）、别的应用再存盘时才合并过去，不像 Linux、macOS 那样马上全局生效；
  在一个应用里 Ctrl+Delete 忘掉的词，别的应用还记着的话下次合并又回来。根本的办法是改成一个单独的输入法进程，
  文本服务通过进程间通信用它（和小狼毫、PIME 一样），还没做
