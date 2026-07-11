# src/lib/core — ハードウェア非依存ロジック層

Arduino / Spresense の API に依存しない**純粋な C++ ロジック**を置く層。
ホストPC（PC上の g++ + テストフレームワーク）でビルド・ユニットテストできることを必須要件とする（Issue #5）。

## 実装済み

- `geo`（`geo.h` / `geo.cpp`）— 2点間の大圏距離（haversine）と初期方位（bearing）。
  ホストテスト: [test/core/test_geo.cpp](../../../test/core/test_geo.cpp)。ナビゲーション（#10/#18）の基礎。
- `cli`（`cli.h` / `cli.cpp`）— コマンド行のトークン化・コマンド表の探索。
  ホストテスト: [test/core/test_cli.cpp](../../../test/core/test_cli.cpp)。オンターゲット試験シェル（#6）が使用。
- `gpio`（`gpio.h`）— GPIO 抽象インタフェース（`hal::Gpio`）。実機は `src/lib/hal/arduino_gpio.h`、
  テストはモックに差し替える。アクチュエータ系が依存する薄い抽象。
- `motor`（`motor.h` / `motor.cpp`）— 2輪差動駆動のモータ制御（TB6612FNG）。真理値表どおりに
  `hal::Gpio` へ出力。ホストテスト: [test/core/test_motor.cpp](../../../test/core/test_motor.cpp)。Issue #8。
- `compass`（`compass.h` / `compass.cpp`）— 9軸センサ BNO055（Issue #9）の方位ロジック。方位角の
  0-360 正規化・最短回頭角・キャリブレーション(0-3)の完了判定・無効値の番兵。実機の I2C 読み取りは
  後続の `src/lib/hal` BNO055 ラッパが担い、生値を本モジュールで正規化・判定する。
  ホストテスト: [test/core/test_compass.cpp](../../../test/core/test_compass.cpp)。ナビ制御（#18）の基礎。
- `datalog`（`datalog.h` / `datalog.cpp`）— 制御履歴ログ（Issue #14）。制御量（観測値）＋操作量
  （モータ指令）＋状態＋時刻を 12列 CSV へ整形。欠損/非有限のサニタイズ・周期 flush・連番ファイル名
  規約を担い、出力先は `LogSink` 抽象（実機=`hal::SdLogSink`）で注入。ホストテスト:
  [test/core/test_datalog.cpp](../../../test/core/test_datalog.cpp)。順位判定の必須要件（細則§5）。

## ここに置くもの（例・実装は Phase2 以降）

- 方位偏差→モータ操作量への変換（制御則）
- 放出検知・着地検知などの閾値判定・状態判定
- ミッションのステートマシン（遷移ロジック）

## 約束ごと

- `#include <Arduino.h>` やボード固有ヘッダを**含めない**（含めるコードは `src/lib/hal` 側へ）。
- 時間・センサ値などの外部入力は**引数で受け取る**設計にし、グローバルなHW状態に触れない（テスト容易性のため）。
- HW依存層へは「インタフェース（抽象）」越しに依存する。具体的な実装は `hal` が提供する。

詳細な構成方針は [src/README.md](../../README.md) を参照。
