# Windows 输入法（TSF 文本服务 DLL）。只在 Windows 上用 MSVC 编，步骤见 docs/WINDOWS.md：
#
#   set RUSTFLAGS=-C target-feature=+crt-static
#   cargo build --release -p wowtype-core
#   cmake -S windows -B build/windows -A x64
#   cmake --build build/windows --config Release
#
# 产物：build/windows/Release/wowtype.dll（输入法，regsvr32 注册）、wowtype-windows-harness.exe（按键测试）、
# wowtype-windows-config-dump.exe（设置契约，scripts/test-config-contract.py --windows 用）。
# 核心和 DLL 都静态链 C 运行库（/MT），输入法装进别人的进程里，不指望那边有 VC++ 运行库。
cmake_minimum_required(VERSION 3.21)
project(wowtype_windows LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "windows/ 只能用 MSVC 编（Linux 上测和平台无关的部分用 windows/test.sh）")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(WOWTYPE_CORE_LIB "${REPO}/target/release/wowtype_core.lib" CACHE FILEPATH
    "核心的静态库（cargo build --release -p wowtype-core，RUSTFLAGS 带 +crt-static）")
set(WOWTYPE_CORE_EXTRA_LIBS "" CACHE STRING
    "核心带语音（默认）时 cpal 要的 windows crate 的导入库 windows.0.52.0.lib；留空在 cargo 的 registry 里找")
option(WOWTYPE_WINDOWS_TESTS "编按键测试和设置导出" ON)

if(NOT EXISTS "${WOWTYPE_CORE_LIB}")
  message(FATAL_ERROR "没有 ${WOWTYPE_CORE_LIB}：先编核心（见文件开头）")
endif()

# 版本号跟着 core/Cargo.toml
file(STRINGS "${REPO}/core/Cargo.toml" version_line REGEX "^version = \"[0-9.]+\"" LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" WOWTYPE_VERSION "${version_line}")
string(REPLACE "." "," WOWTYPE_VERSION_NUM "${WOWTYPE_VERSION},0")

add_compile_definitions(UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc;/permissive-;/wd4100>")

# 核心（Rust 静态库）要的系统库：cargo rustc ... --print native-static-libs；
# synchronization 是 Rust 标准库的锁用的 WaitOnAddress / WakeByAddress*
set(CORE_SYSTEM_LIBS kernel32 ntdll userenv ws2_32 dbghelp legacy_stdio_definitions advapi32 bcrypt synchronization)
if(NOT WOWTYPE_CORE_EXTRA_LIBS)
  # 语音（cpal 的 WASAPI）经 windows crate 调系统 API，导入库在 windows_x86_64_msvc 这个 crate 里
  if(DEFINED ENV{CARGO_HOME})
    set(cargo_home "$ENV{CARGO_HOME}")
  else()
    set(cargo_home "$ENV{USERPROFILE}/.cargo")
  endif()
  file(TO_CMAKE_PATH "${cargo_home}" cargo_home)
  file(GLOB windows_import_libs "${cargo_home}/registry/src/*/windows_x86_64_msvc-0.52.*/lib/windows.0.52.0.lib")
  if(windows_import_libs)
    list(GET windows_import_libs 0 WOWTYPE_CORE_EXTRA_LIBS)
    message(STATUS "windows crate 的导入库：${WOWTYPE_CORE_EXTRA_LIBS}")
  else()
    message(STATUS "没找到 windows.0.52.0.lib：核心不带语音编的话不要紧")
  endif()
endif()
list(APPEND CORE_SYSTEM_LIBS ${WOWTYPE_CORE_EXTRA_LIBS} ole32 oleaut32 avrt winmm)

add_library(wowtype_common STATIC
  src/common/composer.cpp
  src/common/config.cpp
  src/common/keys.cpp
  src/common/voice.cpp)
target_include_directories(wowtype_common PUBLIC ${REPO}/core/include src/common)

add_library(wowtype SHARED
  src/tsf/candidate_window.cpp
  src/tsf/display_attribute.cpp
  src/tsf/dllmain.cpp
  src/tsf/engine_host.cpp
  src/tsf/globals.cpp
  src/tsf/langbar.cpp
  src/tsf/register.cpp
  src/tsf/text_service.cpp
  src/tsf/wowtype.def
  src/tsf/wowtype.rc)
target_link_libraries(wowtype PRIVATE wowtype_common "${WOWTYPE_CORE_LIB}" ${CORE_SYSTEM_LIBS}
  d2d1 dwrite dwmapi ole32 oleaut32 uuid user32 gdi32 shell32 advapi32)
target_compile_definitions(wowtype PRIVATE
  "$<$<COMPILE_LANGUAGE:RC>:WOWTYPE_VERSION_NUM=${WOWTYPE_VERSION_NUM}>"
  "$<$<COMPILE_LANGUAGE:RC>:WOWTYPE_VERSION_STR=\"${WOWTYPE_VERSION}\">")

if(WOWTYPE_WINDOWS_TESTS)
  add_executable(wowtype-windows-harness tests/composer_test.cpp)
  target_link_libraries(wowtype-windows-harness PRIVATE wowtype_common "${WOWTYPE_CORE_LIB}" ${CORE_SYSTEM_LIBS})
  add_executable(wowtype-windows-config-dump tests/config_dump.cpp)
  target_link_libraries(wowtype-windows-config-dump PRIVATE wowtype_common)
endif()
