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
#   4. Arduino ビルド        (arduino-cli があれば CI と同じスケッチ群を compile)
#   5. クロスベンダー別モデルレビュー (PRECHECK_CODEX=1 のときのみ・任意・人間が手元で実行)
#
# 使い方:
#   tools/precheck.sh                       # 1〜4 を実行（4はCIと同じ blink_led/flight/shell）
#   tools/precheck.sh src/shell             # 4のビルド対象を指定スケッチで上書き
#   PRECHECK_CODEX=1 tools/precheck.sh       # 末尾5で差分を別モデルにレビューさせる
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

# 3b. 生成由来マーカー混入チェック（ドキュメント衛生）------------------------
# Write/生成ツール由来のテンプレート閉じタグ等がコミット対象に紛れ込むのを検出する
# （gotchas D 系。過去に doc へ混入し Codex 指摘を受けた）。
# 本スクリプト自身は検査用にマーカー文字列を含むため、対象から除外する。
leaked_marker_step() {
  local hits
  # ツール固有で通常コンテンツに出ない文字列のみ対象（autolink `<...>` 等を誤検出しない）。
  hits=$(git grep -n -E '</content>|antml:' \
    -- 'doc/' '*.md' 'src/' 'tools/' '.claude/' ':!tools/precheck.sh' 2>/dev/null || true)
  if [ -n "${hits}" ]; then
    echo "生成由来マーカーの混入を検出:" >&2
    echo "${hits}" >&2
    return 1
  fi
  echo "生成由来マーカーなし"
}
run_step "3b. leaked-marker check" leaked_marker_step

# 4. Arduino ビルド（既定で CI と同じスケッチ群を compile）---------------------
# CI(.github/workflows/ci.yml)が compile する全スケッチを既定対象にし、ローカルゲートを
# CI と一致させる（一部を省くとローカル緑でも PR で落ちるため。gotchas C4）。
# 引数を渡した場合はそのスケッチ群で上書きする。
if [ "$#" -gt 0 ]; then
  SKETCHES=("$@")
else
  SKETCHES=(src/blink_led src/flight src/shell)
fi
if command -v arduino-cli >/dev/null 2>&1; then
  for sketch in "${SKETCHES[@]}"; do
    run_step "4. arduino build: ${sketch}" tools/build.sh "${sketch}"
  done
else
  echo ""
  echo "skip: arduino-cli 未導入のためスケッチビルドを省略（CIで実行・対象: ${SKETCHES[*]}）"
fi

# 5. クロスベンダー別モデルレビュー（任意・PRECHECK_CODEX=1 のときのみ）---------
# push 前に差分を別ベンダーのモデル(codex 等)に見せ、PR で受けるレビューを先取りする。
# 注意: これは「人間が手元で回す」任意ステップ。CI には無い。
#   - PR前の独立レビューの主役は code-reviewer サブエージェント / /code-review（外部依存ゼロ）。
#     本ステップはクロスベンダーの多様性を足したいときの補助。
#   - Claude Code 自身からの codex 起動は macOS Gatekeeper 等で失敗し得る（人間が手元で実行する前提）。
#   - codex のバージョンでサブコマンド/フラグが異なるため、失敗しても致命にしない。
if [ "${PRECHECK_CODEX:-0}" = "1" ]; then
  echo ""
  echo "==================== 5. cross-vendor model review (任意/手動) ===================="
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
