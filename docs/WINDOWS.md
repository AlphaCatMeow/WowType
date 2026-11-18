# Windows 版（第一阶段：能打字的 TSF 输入法）

> 状态（2026-11）：**开发中，还没有在真机上用过**。CI 的「Windows 输入法（x64）」任务在 `windows-latest` 上编核心和输入法 DLL、
> 跑核心测试、按键测试和设置契约，并真的用 `regsvr32` 注册、注销一遍；打字本身还没人在 Windows 上试过，见下面
> [没在真机上验证的](#没在真机上验证的)。没有安装包、没有设置界面，只适合开发者手动装上试。

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
cargo build --release -p wowtype-core --no-default-features --target-dir target\windows
cmake -S windows -B build\windows -A x64 -DWOWTYPE_CORE_LIB=%CD%\target\windows\release\wowtype_core.lib
cmake --build build\windows --config Release
```

产物在 `build\windows\Release\`：`wowtype.dll`（输入法）、`wowtype-windows-harness.exe`（按键测试）、
`wowtype-windows-config-dump.exe`（设置契约：`python scripts\test-config-contract.py --windows build\windows\Release\wowtype-windows-config-dump.exe`）。
核心和 DLL 都静态链 C 运行库，DLL 不依赖 VC++ 运行库。不想自己编的话，CI 每次跑完都有一个 `windows-x64` 的 artifact，里面是 `wowtype.dll`。

和平台无关的部分（组字、设置、按键翻译）在 Linux 上也能测：`windows/test.sh`（要 `c++` 和 Python 3）。

## 装上试试

1. **放好文件**。找一个普通用户、沉浸式应用都读得到的目录，比如 `C:\Program Files\WowType\`（Program Files 默认允许 UWP 应用读，
   放在自己的用户目录下的话，开始菜单的搜索框、商店应用里不会载入），放：

   ```
   C:\Program Files\WowType\wowtype.dll
   C:\Program Files\WowType\data\lexicon.tsv      词库（必须有）
   C:\Program Files\WowType\data\english.txt      英文词表
   C:\Program Files\WowType\data\words.lm         词级语言模型（可选，没有也能打，候选差一些）
   C:\Program Files\WowType\data\nnlm.bin         整句增强模型（可选）
   C:\Program Files\WowType\data\s2t.tsv          简繁表（繁体输出要，仓库里的 core\data\s2t.tsv）
   C:\Program Files\WowType\data\wubi86.tsv       五笔码表（选五笔才要）
   ```

   数据文件和 Linux、macOS 安装包里的是同一份，拿法（任选一种）：CI 任一次运行的 `data` artifact（`scripts/make-data.sh` 和
   `scripts/make-wubi.sh` 的输出，解开就是这些文件）；或者用 7-Zip 打开 [Releases](https://github.com/Gloridust/WowType/releases)
   里的 `wowtype-data_*.deb`，取出 `usr/share/wowtype/` 下的文件；或者在 WSL / Git Bash 里跑 `scripts/make-data.sh "" data-out`。

2. **注册**（管理员的命令提示符）：

   ```bat
   regsvr32 "C:\Program Files\WowType\wowtype.dll"
   ```

   然后在「设置 → 时间和语言 → 语言和区域 → 中文（简体，中国）→ 语言选项 → 键盘 → 添加键盘」里加上 **WowType**
   （注册时已经默认启用的话，这里已经有了）。用 `Win + 空格` 切过去。

3. **卸载**：先在键盘列表里删掉 WowType，再

   ```bat
   regsvr32 /u "C:\Program Files\WowType\wowtype.dll"
   ```

   DLL 被正在运行的应用载入着时删不掉，注销以后注销一次（或重启）再删。

换 DLL（重新编译以后）也要先关掉用着输入法的应用：DLL 装进每个应用的进程里，载入以后不会卸载。

## 设置和数据

数据目录是 `%LOCALAPPDATA%\WowType\`（`WOWTYPE_DATA_DIR` 可以换，测试用）：

| 文件 | 说明 |
|---|---|
| `wowtype.conf` | 设置，**键名、取值、默认值和 Linux、macOS 一样**，文件拷过来就能用（[CONFIG.md](CONFIG.md)）。还没有设置界面：任务栏「中」按钮右键 →「打开设置文件」用记事本打开（没有就先建一个），存了以后下一次按键生效 |
| `user.tsv`、`history.tsv` | 学到的词和输入习惯，格式同另两端 |
| `custom.tsv` | 自定义词，格式同 Linux（[CONFIG.md](CONFIG.md#自定义词)） |
| `emoji-recent.txt` | 最近用过的 emoji |
| `translate.key` | 互译的 API Key（一行） |
| `wetype\nlrgpt2_nnlm.bin` | 导入的微信输入法整句增强模型（有就先用它） |
| `models\HY-MT1.5-1.8B-Q4_K_M.gguf`、`llama\llama-server.exe` | 本地翻译模型和 llama.cpp（`TranslateLlamaServer` 留空时先在 PATH 里找） |
| `stats.json` | 打字统计（`TypingStats=True` 才记，默认关），格式同另两端 |
| `wowtype.log` | 日志（载入词库、设置、出错），超过 1 MB 从头写 |

和另两端一样的：`EmojiKey` 默认是 `Control+period`（Windows 键的组合多半被系统拿走，`Win + .` 是系统的表情面板，
所以和 macOS 一样把 `Super` 当 `Control`）；候选框是自己画的，所以和 macOS 一样认 `CandidateFontSize`（候选字号，默认 17）。

切到别处（点了别的地方、换了输入框、切走输入法）时打到一半的字母原样上屏，和 macOS 一样（Linux 上是丢掉，原因见 macOS 的说明）。

## 这一阶段还没有的

这些在 Linux、macOS 上有，Windows 上还没做（`spec/config.json` 里对应的项 `platforms` 没写 `windows`，读到了也不管）：

- 设置界面（先直接改 `wowtype.conf`）、安装包和自动更新（`CheckUpdate`、`BetaUpdates`）
- 语音输入（`Voice`、`VoiceKey`、`VoiceCopy`）：核心编的时候不带语音
- 打字统计（`TypingStats`）记是记了（`stats.json`，按键按扫描码记，ABI 1.13 起核心认 Windows 的键码），但还没有看统计的界面
- 本地翻译模型、整句增强模型的一键下载、导入（文件自己放进数据目录就能用）
- ARM64、32 位、代码签名
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
- 互译：`curl.exe` 在各种应用进程里能不能起（沙盒里的 UWP 应用起不了子进程，这时会报翻译失败）、本地模型的 `llama-server` 跟着应用退出
- 沉浸式 / UWP 应用里：数据目录会被重定向到应用自己的容器，读不到 `%LOCALAPPDATA%\WowType` 的设置和学到的词（只用自带的词库、默认设置）
- 同一个进程几个界面线程（资源管理器）共用一个引擎（用锁串起来）
- 每个应用进程各载一份词库（几十 MB 内存）、各自存学到的词：几个应用同时开着时，后存的会盖掉先存的。以后要改成一个单独的输入法进程，
  文本服务通过进程间通信用它（和小狼毫、PIME 一样），这一阶段先不做
