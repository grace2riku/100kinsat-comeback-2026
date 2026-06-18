# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## このリポジトリの位置づけ

種子島ロケットコンテスト 2026 大会 **CanSat 部門 種目5「自律制御カムバック」** に出場するための 100kinSAT 開発リポジトリ。

現時点ではソースコードは未着手で、**実体はドキュメント／仕様の整備リポジトリ**。フライトソフトウェア（Spresense + Arduino）の実装は外部リポジトリ <https://github.com/100kinsat/100kinsat-spresense> にあり、本リポジトリの `doc/cansat_specification/` はそれを整理した仕様書である。

作業・コミュニケーションはすべて日本語で行う。

## ディレクトリ構成

- `doc/種子島ロケットコンテスト_2026年大会/`
  - `official/` — 大会公式 PDF（原典・編集不可の一次資料）
  - `pdf2md/` — PDF を Markdown 化したもの（**編集対象はこちら**）
- `doc/cansat_specification/`
  - `hardware.md` — 100kinSAT ハードウェア仕様（系統構成・PCB 発注・BOM・ピンアサイン・組立）
  - `software.md` — 100kinSAT ソフトウェア仕様（開発環境・各モジュール制御・ライブラリ・TWELITE 設定）

## 主要な作業フロー：PDF → Markdown 整形

このリポジトリの中心作業は、公式 PDF を Markdown に変換し、人が読める体裁へ整形することである（git 履歴の大半がこの整形コミット）。

- 画像は `opendataloader-pdf` で `pdf2md/*_images/` へ自動抽出され、**`.gitignore` 済み**（再生成可能なためコミットしない）。
- PDF からの自動抽出テーブルは列が崩れて読みづらい。整形手順は **「崩れた生テーブルの内容を構造化された見出し（`####` 種目名 → `#####` 競技内容／機体条件 など）へ転記し、転記後に元の生テーブルを削除する」**。`参加者向け大会要領_contest2026_rule.md` の各競技種目はこのパターンで整備済み。
- `official/` の PDF は原典なので改変しない。整形は必ず `pdf2md/` 側の `.md` に対して行う。

## コミット規約

- コミットメッセージは日本語。整形作業は「参加者向け大会要領の Markdown 整形」のような件名 + 具体的に何を書いた／修正したかの本文、という形式で運用している。
- ユーザーが「以下のコメントを含めて」と指定した文言は本文にそのまま含める。

## 将来ソフトウェアを実装する際の前提（仕様書より）

- ターゲット: SONY Spresense メインボード、**Arduino IDE 2.3.2 / Spresense Boards 3.0.0**
- シリアルは全例題で **115200 bps** 固定
- 方式: ローバ（走行型）。パラシュート減速 → 着地後に電熱線（`D06`）でパラシュート切り離し → 9軸センサ（BNO055, I2C）と GNSS で自律走行し目標地点へ
- モータは TB6612FNG 経由（Spresense GPIO は ~6mA のため直接駆動不可）
- ピンアサイン・BOM・各モジュールのサンプルコード対応は `doc/cansat_specification/` を一次情報として参照すること
