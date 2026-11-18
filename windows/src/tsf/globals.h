#pragma once

// 整个 DLL 共用的东西：模块句柄、COM 引用计数、GUID、路径和日志。

#include <windows.h>

#include <string>

namespace wowtype::tsf {

extern HINSTANCE g_module;

void dllAddRef();
void dllRelease();
LONG dllRefCount();

/// 输入法的 COM 类（文本服务）。改了已经装好的机器就认不出来了，别动。
extern const CLSID kClsidTextService;
/// 输入法的语言配置（简体中文下的「WowType」）。
extern const GUID kGuidProfile;
/// 编辑区的显示属性（下划线）。
extern const GUID kGuidDisplayAttributeInput;
/// 任务栏上「中 / 英」那个按钮（系统规定的 GUID_LBI_INPUTMODE）。
extern const GUID kGuidLbiInputMode;
/// 输入框类型的属性（系统规定的 GUID_PROP_INPUTSCOPE；自己写一份，免得依赖哪个 .lib 里有它）。
extern const GUID kGuidPropInputScope;

inline constexpr LANGID kLangId = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
inline constexpr wchar_t kDisplayName[] = L"WowType";

/// 这个 DLL 的完整路径、所在目录（安装目录）。
std::wstring modulePath();
std::wstring moduleDir();

/// 写一行日志到 %LOCALAPPDATA%\WowType\wowtype.log（超过 1 MB 从头写），也发给 OutputDebugString。
void log(const std::wstring &line);

std::wstring widen(const std::string &utf8);
std::string narrow(const std::wstring &wide);

} // namespace wowtype::tsf
