# src/

本種目（CanSat 種目5「自律制御カムバック」）向けに本リポジトリで新規開発するソフトウェアの置き場。

> 注: ディレクトリ構成の正式なルール（HW依存層／非依存層の分離方針、`arduino-cli` ビルド等）は Issue #4 で確定する。現状は動作確認用の最小スケッチのみを置いている。

## スケッチ一覧

| ディレクトリ | 内容 | 関連 Issue |
|---|---|---|
| `blink_led/` | Spresense 内蔵LEDのLチカ＋シリアル疎通確認（環境構築の動作確認用） | #3 |

## Arduino スケッチの約束ごと

- Arduino IDE はスケッチを「フォルダ名と同名の `.ino`」で管理する（例: `blink_led/blink_led.ino`）。
- シリアルのボーレートは全スケッチ **115200 bps** で統一する。

## 環境構築・書き込み手順

[doc/development/spresense_setup.md](../doc/development/spresense_setup.md) を参照。
