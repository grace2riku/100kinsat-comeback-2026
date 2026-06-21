#!/usr/bin/env bash
#
# test.sh - ホストPCで HW非依存ロジックのユニットテストを実行する
#
# CMake で設定→ビルド→ctest を一括実行する。実機・Spresense は不要。
#
# 使い方:
#   tools/test.sh            # 全テスト実行
#   tools/test.sh -V         # 詳細表示（ctest に渡す追加引数）
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/host-test"

if ! command -v cmake >/dev/null 2>&1; then
  echo "error: cmake が見つかりません。" >&2
  exit 1
fi

echo "==> configure"
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" >/dev/null

echo "==> build"
cmake --build "${BUILD_DIR}"

echo "==> ctest"
# ctest の --test-dir は CTest 3.20+ でのみ利用可能。古い ctest でも動くよう
# サブシェルでビルドディレクトリに入って実行する（互換性優先）。
( cd "${BUILD_DIR}" && ctest --output-on-failure "$@" )
