#!/usr/bin/env bash
#
# upload.sh - arduino-cli で Spresense にビルド＋書き込み(upload)する
#
# 使い方:
#   tools/upload.sh [スケッチのパス] [ポート]
#   例) tools/upload.sh src/blink_led
#       tools/upload.sh src/blink_led /dev/cu.SLAB_USBtoUART
#   - スケッチ省略時は src/blink_led
#   - ポート省略時は Spresense らしきシリアルポート(CP210x)を自動検出する。
#     候補が1つに定まらない場合は一覧を表示して終了するので、明示指定すること。
#
# 前提: ブートローダ導入済みであること（初回のみ IDE で実施。doc/development/spresense_setup.md §4）。
#
set -euo pipefail

FQBN="SPRESENSE:spresense:spresense"
SKETCH="${1:-src/blink_led}"
PORT="${2:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "error: arduino-cli が見つかりません（doc/development/build_arduino_cli.md）。" >&2
  exit 1
fi

if [ ! -d "${REPO_ROOT}/${SKETCH}" ]; then
  echo "error: スケッチディレクトリが見つかりません: ${SKETCH}" >&2
  exit 1
fi

# ポート自動検出（Spresense は CP210x のため /dev/cu.SLAB_USBtoUART や usbserial として見える）
# macOS 標準の bash 3.2 では mapfile が無いため while read で移植的に集める。
if [ -z "${PORT}" ]; then
  CANDIDATES=()
  while IFS= read -r line; do
    [ -n "${line}" ] && CANDIDATES+=("${line}")
  done < <(arduino-cli board list 2>/dev/null \
    | awk 'NR>1 && $1 ~ /cu\.(SLAB_USBtoUART|usbserial)/ {print $1}')
  if [ "${#CANDIDATES[@]}" -eq 1 ]; then
    PORT="${CANDIDATES[0]}"
    echo "==> 自動検出したポート: ${PORT}"
  else
    echo "error: ポートを自動検出できません。第2引数で明示してください。" >&2
    echo "       接続中のシリアルポート一覧:" >&2
    arduino-cli board list >&2
    exit 1
  fi
fi

echo "==> compile + upload: ${SKETCH}  (FQBN=${FQBN}, port=${PORT})"
arduino-cli compile --fqbn "${FQBN}" --warnings all -u -p "${PORT}" "${REPO_ROOT}/${SKETCH}"
echo "==> OK（書き込み後にシリアルを見るには: tools/monitor.sh ${PORT}）"
