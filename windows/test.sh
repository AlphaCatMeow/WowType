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
"$CXX" "${FLAGS[@]}" windows/src/common/config.cpp windows/src/common/keys.cpp windows/src/common/look.cpp \
  windows/tests/config_dump.cpp \
  -o "$OUT/wowtype-windows-config-dump"

# 设置窗口下载的语音包（地址、大小、MD5）和核心的 voice/src/wtlocal/pack.rs 一样
for c in URL SIZE MD5; do
  rust=$(sed -n "s/^pub const $c: [^=]*= \(.*\);/\1/p" voice/src/wtlocal/pack.rs | tr -d '"')
  if [[ $c == SIZE ]]; then rust=${rust//_/}; fi
  grep -q -- "$rust" <(tr -d "'" < windows/src/settings/settings_app.cpp) \
    || { echo "FAIL 语音包的 $c 和 voice/src/wtlocal/pack.rs 对不上（${rust}）"; exit 1; }
done

# 本地翻译模型、llama.cpp 构建号和 Linux 设置界面的一样
for c in LOCAL_MODEL_SHA256 LLAMA_BUILD; do
  py=$(grep -A1 "^$c = " settings/wowtype-config | grep -o '"[0-9a-fb]\{6,\}"' | head -1 | tr -d '"')
  grep -q -- "$py" windows/src/settings/settings_app.cpp \
    || { echo "FAIL 设置窗口的 $c 和 settings/wowtype-config 对不上（${py}）"; exit 1; }
done

"$OUT/wowtype-windows-harness" "$@"
python3 scripts/test-config-contract.py --windows "$OUT/wowtype-windows-config-dump"
