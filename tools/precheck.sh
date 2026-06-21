#!/usr/bin/env bash
#
# precheck.sh - PR を出す前にローカルで CI 相当のチェックをまとめて実行する。
#
# 狙い: Codex/CI の指摘を「PR で受ける」前に「ローカルで潰す」（左シフト）。
#       詳しくは doc/development/development_workflow.md の §5 を参照。
#
# 実行内容（CI の host-test / lint と同じ内容をローカル先取り）:
#   1. ホストユニットテスト  (tools/test.sh)
#   2. 静的解析 cppcheck     (src/lib/core)
#   3. 整形チェック clang-format --dry-run --Werror
#   4. Arduino ビルド        (arduino-cli があれば任意スケッチを compile)
#   5. Codex ローカルレビュー (PRECHECK_CODEX=1 のときのみ・任意)
#
# 使い方:
#   tools/precheck.sh                       # 1〜3 を実行（基本）
#   tools/precheck.sh src/shell src/flight  # 末尾4で指定スケッチもビルド
#   PRECHECK_CODEX=1 tools/precheck.sh       # 末尾5で差分を Codex レビュー
#
# 終了コード: いずれかのチェックが失敗したら非0。全部通れば0。
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

FAILED=()
run_step() {
  local name="$1"; shift
  echo ""
  echo "==================== ${name} ===================="
  if "$@"; then
    echo "---- OK: ${name}"
  else
    echo "---- NG: ${name}" >&2
    FAILED+=("${name}")
  fi
}

# 1. ホストユニットテスト ----------------------------------------------------
run_step "1. host unit tests" tools/test.sh

# 2. 静的解析 cppcheck（CI の lint と同条件）---------------------------------
cppcheck_step() {
  if ! command -v cppcheck >/dev/null 2>&1; then
    echo "skip: cppcheck 未導入（CI でも実行されるが、ローカルでも入れると先取りできる）"
    return 0
  fi
  cppcheck --enable=warning,performance,portability \
    --error-exitcode=1 --std=c++17 --language=c++ \
    --suppress=missingInclude --suppress=missingIncludeSystem \
    --inline-suppr \
    src/lib/core
}
run_step "2. cppcheck" cppcheck_step

# 3. 整形チェック clang-format（CI と同条件・チェックのみ）-------------------
clang_format_step() {
  if ! command -v clang-format >/dev/null 2>&1; then
    echo "skip: clang-format 未導入"
    return 0
  fi
  local files
  files=$(find src/lib test -type f \( -name '*.h' -o -name '*.cpp' \) \
    -not -path 'test/vendor/*')
  if [ -z "${files}" ]; then
    echo "対象ファイルなし"
    return 0
  fi
  echo "対象ファイル:"; echo "${files}"
  # shellcheck disable=SC2086
  clang-format --dry-run --Werror ${files}
}
run_step "3. clang-format" clang_format_step

# 4. Arduino ビルド（任意・引数でスケッチ指定があれば）-----------------------
if [ "$#" -gt 0 ]; then
  if command -v arduino-cli >/dev/null 2>&1; then
    for sketch in "$@"; do
      run_step "4. arduino build: ${sketch}" tools/build.sh "${sketch}"
    done
  else
    echo ""
    echo "skip: arduino-cli 未導入のためスケッチビルドを省略（指定: $*）"
  fi
fi

# 5. Codex ローカルレビュー（任意・PRECHECK_CODEX=1 のときのみ）---------------
# push 前に差分を Codex に見せて、PR で受けるレビューを先取りする。
# codex のバージョンによりサブコマンド/フラグが異なるため、失敗しても致命にしない。
if [ "${PRECHECK_CODEX:-0}" = "1" ]; then
  echo ""
  echo "==================== 5. codex local review ===================="
  if ! command -v codex >/dev/null 2>&1; then
    echo "skip: codex CLI が見つからない"
  else
    DIFF="$(git diff main...HEAD)"
    if [ -z "${DIFF}" ]; then
      echo "skip: main との差分なし"
    else
      PROMPT="この差分を Spresense(32bit ARM) 向け実装としてレビューして。\
特に doc/development/spresense_gotchas.md の観点（整数オーバーフロー(long=32bit)、\
atoi等の入力検証、シリアル改行(CR/LF/CRLF)、read戻り値、ファイル間の整合性、\
ドキュメントと実体の乖離、CMake等のバージョン前提）を重点的に指摘して。"
      # codex exec が無い/失敗した場合に備え、致命扱いにしない（手動レビューへ誘導）。
      if ! printf '%s' "${DIFF}" | codex exec "${PROMPT}"; then
        echo "note: codex exec が失敗。codex --help で対話/非対話の起動方法を確認し、" >&2
        echo "      上記プロンプトで手動レビューしてください。" >&2
      fi
    fi
  fi
fi

# 結果サマリ -----------------------------------------------------------------
echo ""
echo "============================================================"
if [ "${#FAILED[@]}" -eq 0 ]; then
  echo "precheck: 全チェック OK。PR を出せます（セルフレビュー §5-2 も確認）。"
  exit 0
else
  echo "precheck: 次のチェックが NG です。修正してから push してください:" >&2
  for f in "${FAILED[@]}"; do echo "  - ${f}" >&2; done
  exit 1
fi
