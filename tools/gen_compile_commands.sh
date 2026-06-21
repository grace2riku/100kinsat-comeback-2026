#!/usr/bin/env bash
#
# gen_compile_commands.sh - clangd 用 compile_commands.json を生成する
#
# エディタの clangd が Spresense コアのインクルードパスを認識できず
# `Arduino.h が開けません` 等の誤検知を出すのを解消する。
# 生成物 compile_commands.json はマシン依存の絶対パスを含むため Git 管理しない（.gitignore 済み）。
#
# 注意: compile_commands.json の各エントリは生成元のビルドパス内のファイル
#       (sketch/*.ino.cpp, 各 .o, ビルド内 include 等) を参照する。そのため
#       ビルドパスは削除せず .arduino-build/ に永続化する（消すと DB が
#       存在しないファイルを指し、clangd 補完が機能しなくなる）。
#
# 使い方:
#   tools/gen_compile_commands.sh [スケッチのパス]   # 省略時 src/blink_led
#
set -euo pipefail

FQBN="SPRESENSE:spresense:spresense"
SKETCH="${1:-src/blink_led}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_PATH="${REPO_ROOT}/.arduino-build/$(basename "${SKETCH}")"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "error: arduino-cli が見つかりません（doc/development/build_arduino_cli.md）。" >&2
  exit 1
fi

mkdir -p "${BUILD_PATH}"
echo "==> compile (build-path=${BUILD_PATH})"
arduino-cli compile --fqbn "${FQBN}" --build-path "${BUILD_PATH}" "${REPO_ROOT}/${SKETCH}" >/dev/null

cp "${BUILD_PATH}/compile_commands.json" "${REPO_ROOT}/compile_commands.json"
echo "==> generated: ${REPO_ROOT}/compile_commands.json"
echo "    参照先ビルドパス ${BUILD_PATH} は削除しない（.gitignore 済み）。"
echo "    clangd 拡張がリポジトリ直下の compile_commands.json を自動検出する。"
