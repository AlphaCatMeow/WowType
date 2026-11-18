#!/usr/bin/env bash
# 打 Windows 安装包（zip）：输入法 DLL + 数据 + 安装脚本。CI 的 Windows 任务在 Git Bash 里跑，Linux 上也能跑（只拼文件）。
#
#   windows/package.sh <数据目录> <wowtype.dll> [输出目录]
#
# 数据目录是 scripts/make-data.sh + scripts/make-wubi.sh 的输出（和 macos/build.sh 一样）。
# 产物：<输出目录>/WowType-<版本>-windows-x64/（解开后的样子）和同名 .zip。
# 微信输入法的模型（nlrgpt2_nnlm.bin、wtlocal）、翻译模型不许带；nnlm.bin 是 WowType 自己训的，可以带。
set -euo pipefail
cd "$(dirname "$0")/.."

DATA=${1:?用法：windows/package.sh <数据目录> <wowtype.dll> [输出目录]}
DLL=${2:?用法：windows/package.sh <数据目录> <wowtype.dll> [输出目录]}
OUT=${3:-dist/windows}
VERSION=${WOWTYPE_VERSION:-$(sed -n 's/^version = "\(.*\)"/\1/p' core/Cargo.toml | head -1)}

for f in lexicon.tsv english.txt wubi86.tsv WUBI-LICENSE WUBI-AUTHORS WUBI-SOURCE.txt; do
  [[ -f "$DATA/$f" ]] || { echo "数据目录里没有 $f" >&2; exit 1; }
done

name=WowType-$VERSION-windows-x64
pkg=$OUT/$name
rm -rf "$pkg" "$OUT/$name.zip"
mkdir -p "$pkg/data"
cp "$DLL" "$pkg/wowtype.dll"
cp windows/install.ps1 "$pkg/"
cp "$DATA/lexicon.tsv" "$DATA/english.txt" "$DATA/wubi86.tsv" "$pkg/data/"
for f in words.lm nnlm.bin; do
  if [[ -f "$DATA/$f" ]]; then cp "$DATA/$f" "$pkg/data/"; fi
done
cp core/data/s2t.tsv core/data/OPENCC-LICENSE "$pkg/data/"
for f in LICENSE SOURCE.txt VERSION; do
  if [[ -f "$DATA/$f" ]]; then cp "$DATA/$f" "$pkg/data/DATA-${f}"; fi
done
if [[ -f "$DATA/DATA-MANIFEST" ]]; then cp "$DATA/DATA-MANIFEST" "$pkg/data/"; fi
cp "$DATA/WUBI-LICENSE" "$pkg/data/DATA-WUBI-LICENSE"
cp "$DATA/WUBI-AUTHORS" "$pkg/data/DATA-WUBI-AUTHORS"
cp "$DATA/WUBI-SOURCE.txt" "$pkg/data/DATA-WUBI-SOURCE.txt"
# 说明用 CRLF，记事本老版本也能看
{ printf '\xef\xbb\xbf'; sed 's/$/\r/'; } > "$pkg/README.txt" <<EOF
WowType $VERSION（Windows x64，开发中的第一阶段，没有在很多机器上试过）

安装：以管理员身份打开 PowerShell，进到这个目录，运行
    powershell -ExecutionPolicy Bypass -File .\install.ps1
装到 %ProgramFiles%\WowType，然后用 Win + 空格切到 WowType。
卸载：powershell -ExecutionPolicy Bypass -File .\install.ps1 -Uninstall

设置：任务栏上「中」按钮右键 →「打开设置文件」（键名和 Linux、macOS 一样）。
数据（学到的词、设置、日志）：%LOCALAPPDATA%\WowType
说明和已知问题：https://github.com/Gloridust/WowType/blob/main/docs/WINDOWS.md
EOF

# 安装包里只能有这些文件
allowed='^(wowtype\.dll|install\.ps1|README\.txt|data|data/(lexicon\.tsv|english\.txt|words\.lm|nnlm\.bin|s2t\.tsv|wubi86\.tsv|OPENCC-LICENSE|DATA-LICENSE|DATA-SOURCE\.txt|DATA-VERSION|DATA-MANIFEST|DATA-WUBI-LICENSE|DATA-WUBI-AUTHORS|DATA-WUBI-SOURCE\.txt))$'
bad=$(cd "$pkg" && find . -mindepth 1 | sed 's#^\./##' | grep -Ev "$allowed" || true)
if [[ -n "$bad" ]]; then
  echo "安装包里有不该带的文件：" >&2
  echo "$bad" >&2
  exit 1
fi

if command -v zip >/dev/null; then
  (cd "$OUT" && zip -qr "$name.zip" "$name")
else
  # Windows 的 Git Bash 没有 zip：用 PowerShell
  powershell -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$pkg")' -DestinationPath '$(cygpath -w "$OUT/$name.zip")'"
fi
echo "$OUT/$name.zip"
