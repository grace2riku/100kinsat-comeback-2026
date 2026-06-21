#!/usr/bin/env bash
#
# monitor.sh - arduino-cli のシリアルモニタ（115200bps）
#
# Arduino IDE のシリアルモニタの代替。全スケッチ共通のボーレート 115200 で開く。
#
# 使い方:
#   tools/monitor.sh [ポート]
#   例) tools/monitor.sh
#       tools/monitor.sh /dev/cu.SLAB_USBtoUART
#   - ポート省略時は Spresense らしきシリアルポート(CP210x)を自動検出する。
#
# 終了: Ctrl-C
#
# 注: Spresense の Serial は USB-UART(CP210x)で、モニタの接続有無を検知できない。
#     起動バナーを先頭から見たい場合は、モニタを開いてからボードをリセットする。
#
set -euo pipefail

BAUD=115200
PORT="${1:-}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "error: arduino-cli が見つかりません（doc/development/build_arduino_cli.md）。" >&2
  exit 1
fi

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
    echo "error: ポートを自動検出できません。引数で明示してください。" >&2
    echo "       接続中のシリアルポート一覧:" >&2
    arduino-cli board list >&2
    exit 1
  fi
fi

echo "==> serial monitor: ${PORT} @ ${BAUD}bps （終了: Ctrl-C）"
arduino-cli monitor -p "${PORT}" -c baudrate="${BAUD}"
