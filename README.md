# 100kinsat-comeback-2026

[![CI](https://github.com/grace2riku/100kinsat-comeback-2026/actions/workflows/ci.yml/badge.svg)](https://github.com/grace2riku/100kinsat-comeback-2026/actions/workflows/ci.yml)

種子島ロケットコンテスト2026 CanSat部門 種目5「自律制御カムバック」用 100kinSATソフトウェア開発リポジトリ

## CI（GitHub Actions）

push(main) / Pull Request で以下を自動実行する（[ワークフロー](.github/workflows/ci.yml)）。

| ジョブ | 内容 |
|---|---|
| Host unit tests | `src/lib/core` のホストユニットテスト（cmake + ctest + doctest）→ `tools/test.sh` |
| Static analysis | clang-format（整形チェック）/ cppcheck（静的解析） |
| Arduino build (Spresense) | `src/blink_led` / `src/flight` を arduino-cli でビルド（compile）→ `tools/build.sh` |

## 開発ドキュメント

- **開発ワークフロー（Issue/ブランチ/PR/PR前レビューゲート）**: [doc/development/development_workflow.md](doc/development/development_workflow.md)
- **Spresense 実装の落とし穴集（実装前チェックリスト）**: [doc/development/spresense_gotchas.md](doc/development/spresense_gotchas.md)
- 開発環境（IDE・書き込み・実機確認）: [doc/development/spresense_setup.md](doc/development/spresense_setup.md)
- CLIビルド・書き込み・モニタ・clangd: [doc/development/build_arduino_cli.md](doc/development/build_arduino_cli.md)
- オンターゲット試験シェル（NT-Shell）: [doc/development/serial_shell.md](doc/development/serial_shell.md)
- ソース構成・層分け: [src/README.md](src/README.md)
- ホストテスト方針: [test/README.md](test/README.md)

## ローカルでのコマンド

```bash
tools/build.sh   src/blink_led   # Spresense向けビルド
tools/upload.sh  src/blink_led   # ビルド＋書き込み（要ブートローダ導入）
tools/monitor.sh                 # シリアルモニタ 115200bps
tools/test.sh                    # ホストユニットテスト
tools/precheck.sh                # PR前チェック一括（test+静的解析+整形をローカル先取り）
```

> PR を出す前に `tools/precheck.sh` を通すこと。詳細は [開発ワークフロー](doc/development/development_workflow.md) を参照。
