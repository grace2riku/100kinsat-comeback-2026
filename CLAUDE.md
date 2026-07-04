# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## このリポジトリの位置づけ

種子島ロケットコンテスト 2026 大会 **CanSat 部門 種目5「自律制御カムバック」**（以下「**本種目**」）に出場するための 100kinSAT 開発リポジトリ。

現時点ではソースコードは未着手で、**実体はドキュメント／仕様の整備リポジトリ**。フライトソフトウェア（Spresense + Arduino）の参照実装・サンプルコードは外部リポジトリ <https://github.com/100kinsat/100kinsat-spresense> にあり、本リポジトリの `doc/cansat_specification/` はそれを整理した仕様書である。本リポジトリで本種目向けのソフトウェアを新規に `src/` 配下へ実装し、外部リポジトリは参照元（サンプル・ライブラリの出典）として扱う。

作業・コミュニケーションはすべて日本語で行う。

## 目的
* 本種目のルールを守る。
* 本種目の優勝チームの記録を上回ることを目的とする。
  * 優勝チームの記録: 24.4点（0.37m,157s）
  * ポイント計算は `CanSat競技の計測と評価の細則_CanSat_appendix_2026.md` を参照のこと

## 目的達成のための手順
* スタートからいきなり本種目の実装を始めないこと。
* 各種機能を設計、実装、テストと細かく段階を踏み開発していくこと。最終的に本種目のルールを守った制御を行う。
* ハードウェアの仕様は doc/cansat_specification/hardware.md を参照のこと
* ソフトウェアの開発環境、各機能のサンプルコードは doc/cansat_specification/software.md を参照のこと
* 最初から優勝チームの記録を上回ることを目標にしないこと。機能実装のフェーズ、機能実装完了後に性能向上のフェーズに入り、優勝チームの記録を上回ることを目標にソフトウェアの改善を行う。

### ⚠️ マイルストーン（Phase）の順序を飛ばさない（重要・厳守）
GitHub マイルストーンは **Phase1 環境構築 → Phase2 単体機能の設計・実装・確認 → Phase3 自律制御の結合（確実にゴール）→ Phase4 シミュレーター環境 → Phase5 性能改善（優勝記録超え）** の順で進める。
* **現在は Phase2（単体機能）。Phase3 以降の結合系 Issue（例: #17 ミッションステートマシン, #18 自律走行 など milestone "Phase3"）には、Phase2（milestone "Phase2"）の未完了 Issue を先に片付けてから着手する。**
* 次の実装対象を提案・着手するときは、**まず Phase2 の open Issue を優先**すること。Phase を先取りしない（結合や性能改善に飛ばない）。
* Phase3 以降の作業で得た知見は、該当 Issue にコメントで**申し送り**として残し、着手はマイルストーン順に行う。
* 現在の各 Phase の進捗は `gh api repos/{owner}/{repo}/milestones` で確認できる。

## テスト戦略
* テスト駆動開発で開発をおこなう。
* ハードウェアに依存しない機能のテストはホストPCでテストが完結するようにテスト環境を構築する。
* ハードウェアに依存する機能であれば、NT-Shellなどのシェルを組込み、シリアルコンソールからテストコマンドを送信しテストするなどテスト効率化の環境構築をおこなう。
* 機能実装の過程でソフトウェアのデグレードなどをいち早く検出できるように CI/CD の環境構築を行う


## ディレクトリ構成
- `src/` — Arduino スケッチ・ソースコード本体
  - `src/<sketch>/<sketch>.ino` — Spresense 向けスケッチ（`blink_led` / `shell` / `flight`）
  - `src/lib/core/` — **HW非依存ロジック**（`geo` 距離方位計算、`cli` コマンド解析 等）。ホストPCでユニットテスト可能
  - `src/lib/hal/` — HW依存の抽象化層（ハードウェア抽象化）
- `libraries/` — 同梱サードパーティライブラリ（`ntshell` 等）。`--libraries` で探索される
- `test/` — ホストPC ユニットテスト（doctest）。`test/vendor/` は doctest 本体
- `tools/` — 開発スクリプト（ビルド/テスト/書込/補完DB生成/PR前チェック）
- `doc/development/` — 開発環境・ワークフローの手順書
  - `spresense_setup.md` / `build_arduino_cli.md` / `serial_shell.md` — 環境構築手順
  - `development_workflow.md` — **Issue/ブランチ/PR/レビューゲート運用ルール**
  - `spresense_gotchas.md` — **Spresense 実装の落とし穴集（実装前チェックリスト）**
- `doc/種子島ロケットコンテスト_2026年大会/`
  - `official/` — 大会公式 PDF（原典・編集不可の一次資料）
  - `pdf2md/` — PDF を Markdown 化したもの（**編集対象はこちら**）
    - `参加者向け大会要領_contest2026_rule.md` - 参加者向け大会要領
    - `CanSat競技の計測と評価の細則_CanSat_appendix_2026.md` - CanSat競技の計測と評価の細則
- `doc/cansat_specification/`
  - `hardware.md` — 100kinSAT ハードウェア仕様（系統構成・PCB 発注・BOM・ピンアサイン・組立）
  - `software.md` — 100kinSAT ソフトウェア仕様（開発環境・各モジュール制御・ライブラリ・TWELITE 設定）

## 開発コマンド（実装フェーズ）
すべてリポジトリルートから実行する。

| 目的 | コマンド | 備考 |
|---|---|---|
| ホストユニットテスト | `tools/test.sh` | cmake + ctest + doctest。実機不要。`-V` で詳細 |
| Spresense ビルド（compileのみ） | `tools/build.sh src/<sketch>` | 例: `tools/build.sh src/flight` |
| 実機へ書き込み | `tools/upload.sh src/<sketch>` | arduino-cli。ポート自動検出 |
| シリアルモニタ | `tools/monitor.sh` | 115200 bps |
| clangd 用補完DB生成 | `tools/gen_compile_commands.sh src/<sketch>` | `.arduino-build/` を永続化 |
| **PR前チェック（一括）** | `tools/precheck.sh` | test+静的解析+整形をローカル先取り。`PRECHECK_CODEX=1` で Codex 差分レビューも |

## コーディング規約
- 整形は `.clang-format`（clang-format 22.1.5）に従う。`src/lib`・`test` が対象（`.ino` と `test/vendor` は対象外）。
- 静的解析 cppcheck（`--enable=warning,performance,portability`、C++17）を `src/lib/core` に適用。CI で強制。
- **HW非依存ロジックは `src/lib/core` に切り出し、ホストテストを書く**（TDD）。`.ino` には HW 結線・薄い呼び出しのみ。
- シリアルは全例題で **115200 bps** 固定。
- 実装前に必ず `doc/development/spresense_gotchas.md` を参照（ターゲット≠ホスト、入力検証、整合性の落とし穴）。

## 開発ワークフロー（必読）
- 進め方の正本は **`doc/development/development_workflow.md`**（Issue→ブランチ→TDD→**PR前レビューゲート**→PR）。
- **PR を出す前に必ず `tools/precheck.sh` を通し、`spresense_gotchas.md` のセルフレビューチェックリストで照合する**。これがレビューの往復を減らす中核。
- Codex の PR 自動レビューで受けた妥当な指摘は、対応のうえ **`spresense_gotchas.md` に1行追加して資産化**する（同じ不具合を二度とPRに出さない）。

## Claude Code 運用方針（MCP / スキル / サブエージェント）
- **役割分担**: 実装(TDD)とPR前セルフレビューは Claude（メイン会話）、**PR前の独立レビューは `code-reviewer` サブエージェント**（外部依存ゼロの事前批評役）、汎用バグ観点は `/code-review`、PR自動レビューは Codex(GitHub)。詳細は `development_workflow.md` §5-3 / §7。
- **実装力強化の中核**: 「実装 → 独立レビュー(`code-reviewer`) → 修正 → 指摘を `spresense_gotchas.md` に資産化」の閉ループを毎回回す。これが考慮漏れを構造的に減らす。
- **サブエージェント**: 独立レビューは `.claude/agents/code-reviewer.md`（Spresense gotchas を焼き込んだ敵対的レビュー担当）。広いコード探索・調査は `Explore`/`general-purpose` に委譲し結論だけ受け取る。実装方針の検討は `Plan`。
- **スキル**: 差分レビューは `/code-review`（`ultra` で多エージェントのクラウドレビュー）、品質整理は `/simplify`、ライブラリ調査は context7（MCP）を優先。
- **MCP**: ライブラリ/SDK/CLI の仕様確認は context7 を使う（記憶に頼らず最新ドキュメントを引く）。別ベンダーのモデルをレビューに使う場合も MCP/HTTP 経由（Gatekeeper非依存）。
- **権限**: よく使うプロジェクトコマンドは `.claude/settings.json`（チーム共有・チェックイン）の許可リストに登録済み。個人固有の許可は `.claude/settings.local.json`（gitignore対象）。


## 将来ソフトウェアを実装する際の前提（仕様書より）

- ターゲット: SONY Spresense メインボード、**Arduino IDE 2.3.2 / Spresense Boards 3.0.0**
- シリアルは全例題で **115200 bps** 固定
- 方式: ローバ（走行型）。パラシュート減速 → 着地後に電熱線（`D06`）でパラシュート切り離し → 9軸センサ（BNO055, I2C）と GNSS で自律走行し目標地点へ
- モータは TB6612FNG 経由（Spresense GPIO は ~6mA のため直接駆動不可）
- ピンアサイン・BOM・各モジュールのサンプルコード対応は `doc/cansat_specification/` を一次情報として参照すること
