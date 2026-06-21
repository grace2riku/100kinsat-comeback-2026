# src/

本種目（CanSat 種目5「自律制御カムバック」）向けに本リポジトリで新規開発するソフトウェアの置き場。

## ディレクトリ構成と層分け

ハードウェアに依存しないロジックを**ホストPCでユニットテスト**できるよう（Issue #5）、
HW非依存層（`core`）と HW依存層（`hal`）を分離する。

| パス | 役割 | テスト | 関連 Issue |
|---|---|---|---|
| `blink_led/` | Spresense 内蔵LEDのLチカ＋シリアル疎通確認（動作確認用スケッチ） | 実機 | #3 |
| `flight/` | 本番フライトソフトのエントリ（ステートマシンの雛形。両層を結線する薄い層） | 実機 | #17 ほか |
| `lib/core/` | **HW非依存**ロジック（距離/方位計算・制御則・判定・状態遷移・ログ整形） | ホストPC | #5, Phase2 |
| `lib/hal/` | **HW依存**コード（モータ/9軸/GNSS/SD/電熱線/通知 の Arduino・Spresense API 実装） | 実機/オンターゲット | Phase2 |

### 層分けの原則

- `core` は `#include <Arduino.h>` やボード固有ヘッダを**含めない**。入力は引数で受け取り、グローバルなHW状態に触れない（テスト容易性）。
- `hal` は `core` が定義するインタフェース（抽象）を実機向けに実装する。純粋ロジックは持たない（薄く保つ）。
- `flight.ino` は `core` と `hal` を組み立てて `loop` を回すだけの結線役にする。
- ホストテストでは `hal` をモック/スタブに差し替え、`core` のロジックを検証する。

```
   flight.ino  ──結線──▶  core (純粋ロジック / ホストテスト対象)
        │                     ▲
        └──────▶ hal ─────────┘  hal が core のインタフェースを実機実装
                 (Arduino/Spresense API)
```

## Arduino スケッチの約束ごと

- Arduino IDE はスケッチを「フォルダ名と同名の `.ino`」で管理する（例: `blink_led/blink_led.ino`）。
- シリアルのボーレートは全スケッチ **115200 bps** で統一する。

## ビルド

- コマンドラインビルド（arduino-cli）: [doc/development/build_arduino_cli.md](../doc/development/build_arduino_cli.md)
  ```bash
  tools/build.sh src/blink_led
  tools/build.sh src/flight
  ```
- IDE での書き込み・実機確認: [doc/development/spresense_setup.md](../doc/development/spresense_setup.md)
