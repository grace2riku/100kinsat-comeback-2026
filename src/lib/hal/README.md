# src/lib/hal — ハードウェア依存層（HAL: Hardware Abstraction Layer）

Arduino / Spresense の API を実際に呼び出す**ハードウェア依存コード**を置く層。
`src/lib/core` が定義するインタフェースを、実機ハードウェア向けに実装する。

## ここに置くもの（例・実装は Phase2 以降）

- モータ駆動（TB6612FNG, `analogWrite`/`digitalWrite`）— Issue #8
- 9軸センサ BNO055（I2C, Adafruit_BNO055）— Issue #9
- GNSS 測位（`GNSS.h`）— Issue #10
- 照度センサ CdS（`analogRead`）— Issue #11
- 電熱線駆動（`digitalWrite` D06）— Issue #13
- SDカードログ（`SDHCI.h`）— Issue #14
- スピーカ/LED 通知 — Issue #15
- TWELITE テレメトリ（UART）— Issue #16

## 約束ごと

- 純粋ロジックはここに書かず `src/lib/core` に置く（HAL は薄く保つ）。
- ホストテストでは HAL をモック/スタブに差し替えられるよう、`core` 側のインタフェースに合わせて実装する。

詳細な構成方針は [src/README.md](../../README.md) を参照。
