#!/usr/bin/env bash
#
# build.sh - arduino-cli で Spresense 向けにスケッチをビルドする
#
# 使い方:
#   tools/build.sh [スケッチのパス]
#   例) tools/build.sh src/blink_led
#       tools/build.sh src/flight
#   引数省略時は src/blink_led をビルドする。
#
# 事前準備（初回のみ）: doc/development/build_arduino_cli.md を参照。
#
set -euo pipefail

FQBN="SPRESENSE:spresense:spresense"
SKETCH="${1:-src/blink_led}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "error: arduino-cli が見つかりません。先に導入してください（doc/development/build_arduino_cli.md）。" >&2
  exit 1
fi

if [ ! -d "${REPO_ROOT}/${SKETCH}" ]; then
  echo "error: スケッチディレクトリが見つかりません: ${SKETCH}" >&2
  exit 1
fi

echo "==> build: ${SKETCH}  (FQBN=${FQBN})"
arduino-cli compile --fqbn "${FQBN}" --warnings all "${REPO_ROOT}/${SKETCH}"
echo "==> OK"
