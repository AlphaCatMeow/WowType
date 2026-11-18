#!/usr/bin/env bash
# Windows 外壳里和平台无关的部分（Composer、设置、按键翻译）的测试。不依赖 windows.h，Linux 上也跑。
#
#   windows/test.sh [lexicon.tsv]
#
# 先编核心的静态库（不带语音，和 Windows 安装包一样），再用 c++ 把 windows/src/common 和 windows/tests 编成
# 两个可执行文件：按键测试（composer_test.cpp）和设置导出（config_dump.cpp，拿 spec/config.json 对照）。
# Windows 上 CI 用 CMake 编同样的两个（windows/CMakeLists.txt 的 WOWTYPE_WINDOWS_TESTS）。
set -euo pipefail
cd "$(dirname "$0")/.."

TARGET_DIR=${WOWTYPE_WINDOWS_TARGET_DIR:-target/windows-novoice}
OUT=$TARGET_DIR/harness
mkdir -p "$OUT"

cargo build --release -q -p wowtype-core --no-default-features --target-dir "$TARGET_DIR"
LIB=$TARGET_DIR/release/libwowtype_core.a
CXX=${CXX:-c++}
FLAGS=(-std=c++17 -O1 -Wall -Wextra -Icore/include -Iwindows/src/common)

"$CXX" "${FLAGS[@]}" windows/src/common/*.cpp windows/tests/composer_test.cpp "$LIB" -lpthread -ldl -lm \
  -o "$OUT/wowtype-windows-harness"
"$CXX" "${FLAGS[@]}" windows/src/common/config.cpp windows/src/common/keys.cpp windows/tests/config_dump.cpp \
  -o "$OUT/wowtype-windows-config-dump"

"$OUT/wowtype-windows-harness" "$@"
python3 scripts/test-config-contract.py --windows "$OUT/wowtype-windows-config-dump"
