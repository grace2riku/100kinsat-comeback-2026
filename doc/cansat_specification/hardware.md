# 100kinSAT ハードウェア仕様

本書は [SpresenseではじめるCanSat開発【準備編】](https://zenn.dev/ymt117/books/100kinsat-spr-build) と [基礎編](https://zenn.dev/ymt117/books/100kinsat-spr-basic) に基づき、100kinSAT のハードウェア仕様 (システム構成、基板、機構部品、電子部品 BOM、電源、ピンアサイン、組立) を整理したものです。

ソースリポジトリ: <https://github.com/100kinsat/100kinsat-spresense>

---

## 1. 概要

- **100kinSAT (ヒャッキンサット)**: 種子島ロケットコンテストのレギュレーションを満たす、初心者向け CanSat 開発キット
- **方式**: ローバ方式 (走行型) — パラシュート減速・着地後にパラシュートを切り離し、センサ値を元に自律走行で目標地点へ向かう
- **概算材料費**: 約 2 万円強 (新版 / Spresense ベース)
- **構造**: 基板自体が機体ボディを兼ねる

## 2. システム構成 (5 系統)

| 系統 | 要素 |
|---|---|
| 制御系 | Spresense メインボード (SONY 製) |
| ミッション系 | モータ、電熱線、各種センサ (9 軸 / GPS / 照度) |
| 電源系 | 単 4 アルカリ乾電池 × 4 (直列 6V) |
| 通信系 | TWELITE (無線)、I2C / UART / SDIO (有線) |
| 構造系 | 基板 (ボディ兼用)、3D プリントタイヤ・スタビライザー、パラシュート |

> 補助系: LED (Spresense 内蔵) とスピーカは状態通知・デバッグに使用 (起動音、エラー時のビープ等)

回路図 PDF (top / bottom):
- <https://github.com/100kinsat/100kinsat-spresense/blob/main/pcb/circuit/2024-03-06/edusat-spresense-pcb-top.pdf>
- <https://github.com/100kinsat/100kinsat-spresense/blob/main/pcb/circuit/2024-03-06/edusat-spresense-pcb-bottom.pdf>

## 3. 基板 (PCB)

100kinSAT は **top 基板** と **bottom 基板** の 2 枚構成。発注先は **JLCPCB** (<https://jlcpcb.com/>)。

### 設計データ

リリース: <https://github.com/100kinsat/100kinsat-spresense/releases/tag/2024-02-29>

`pcb-2024-02-29.zip` を展開後、以下を発注に使用:

- top: `GERBER-edusat-spresense-pcb.zip`, `BOM-edusat-spresense-pcb.csv`, `CPL-edusat-spresense-pcb.csv`
- bottom: `GERBER-edusat-spresense-pcb-bottom.zip`, `BOM-edusat-spresense-pcb-bottom.csv`, `CPL-edusat-spresense-pcb-bottom.csv`

### JLCPCB 発注仕様 (top 基板)

| 項目 | 値 |
|---|---|
| Base Material | FR-4 |
| Layers | 2 |
| Dimensions | 100 × 50 mm |
| PCB Qty | 5 (最小) |
| Product Type | Industrial/Consumer electronics |
| Different Design | 1 |
| Delivery Format | Single PCB |
| PCB Thickness | 1.6 mm |
| PCB Color | Green (色で価格・納期変動あり) |
| Silkscreen | White |
| Surface Finish | HASL(with lead) |
| Outer Copper Weight | 1 oz |
| Via Covering | Tented |
| Board Outline Tolerance | ±0.2 mm (Regular) |
| Flying Probe Test | Fully Test |
| PCB Assembly | ON |
| PCBA Type | Economic |
| Assembly Side | Top Side |
| PCBA Qty | 5 (実装枚数。減らすと安価) |
| Tooling holes | Added by JLCPCB |
| Product Description | Research/Education/DIY/Entertainment > Toy - HS Code 950300 |

- BOM 処理時、`J2` と `J6` のチェックを外す
- 「Project has unselected parts」ダイアログでは "Do not place" を選択
- モータドライバ IC は配置確認画面で向き修正 (右クリック → Rotate)
- "shortfall" 部品がある場合は在庫補充を待つか手配する
- bottom 基板は PCBA 不要 — ガーバーのみアップロードしてカートに追加
- 5 枚送料込みで約 1 万円。DHL Express で最短 1 週間
- 中国の春節 (旧正月) 期間の発注は遅延注意

## 4. 3D プリント部品

発注先: **JLC3DP** (<https://jlc3dp.com/>)。データは PCB と同じリリースに同梱 (`3d-data-2024-02-29.zip`)。

### 機体 1 機分の数量

| ファイル | 数量 | 用途 |
|---|---|---|
| `camera-mount-20240219.stl` | 1 | カメラマウント |
| `servo-bracket-20240114.stl` | 2 | モータ (サーボ) ブラケット |
| `stabilizer-20240219.stl` | 2 | スタビライザー |
| `wheel-for-fs90-20240219.stl` | 2 | FS90 / FM90 用車輪 |

### 材料・プロセス注意

- 「3D Technology」「Material」は基本任意選択可
- **`wheel-for-fs90-20240219.stl` は MJF (PA12-HP Nylon) ではサーボホーンの歯と寸法が合わなかった実績あり** — 別材質推奨
- 発注レビュー後に `wheel-for-fs90` で "File Error" が出るのは破損リスクの受諾確認 → Yes / Confirm で OK

## 5. 電子部品 BOM (秋月電子通商主体)

### 制御・センサ・通信

| 名称 | 型番 | 数 | 単価 (税込) | 秋月コード |
|---|---|---:|---:|---|
| SONY Spresense メインボード | — | 1 | 6,050 | 114584 |
| BNO055 9 軸センサーフュージョンモジュールキット | BNO055 | 1 | 2,450 | 116996 |
| TWELITE ワイヤレスモジュール アンテナ別付タイプ | TWE-Lite | 1 | 1,450 | 108263 |
| USB スティック MONOSTICK ブルー | — | 1 | 3,030 | 111931 |
| FEETECH ギヤードモータ | FM90 | 2 | 330 | 114801 |
| 基板取付用小型ダイナミックスピーカ | UGCM0603APE | 1 | 60 | 110128 |
| CdS セル (1MΩ) | GL5528 | 1 | 100 | 105886 |

- TWELITE 代替: 表面実装が難しい場合 [TWELITE BLUE UART アンテナ内蔵タイプ (M-16829)](https://akizukidenshi.com/catalog/g/gM-16829/) + [ピンヘッダ 1x40 (C-00167)](https://akizukidenshi.com/catalog/g/gC-00167/) で可

### 電源・コネクタ・スイッチ

| 名称 | 型番 / 仕様 | 数 | 単価 (税込) | 秋月コード |
|---|---|---:|---:|---|
| 低損失三端子レギュレータ 3.3V 800mA | NJM2845DL1-33 | 1 | 50 | 111299 |
| 低損失三端子レギュレータ 5V 800mA | NJM2845DL1-05 | 1 | 50 | 111298 |
| 電池ボックス 単 4×1 本 ピン | — | 4 | 40 | 102670 |
| スライドスイッチ 1 回路 2 接点 基板用 横向き | — | 2 | 25 | 115703 |
| DIP スイッチ 2P | — | 1 | 70 | 108922 |
| XH コネクタ ベース付ポスト 2P | B2B-XH-A(LF)(SN) | 1 | 10 | 112247 |
| コネクタ付コード 2P (D) 赤黒 | — | 1 | 30 | 105682 |
| ターミナルブロック 2.54mm 2P 緑 縦 | — | 1 | 25 | 114217 |
| ピンヘッダ 1×40 (40P) | — | 1 | 35 | 100167 |
| ピンヘッダ (オスL型) 1×40 (40P) | — | 1 | 50 | 101627 |
| ジャンパーピン 黒 (2.54mm ピッチ) | — | 1 | 100 | 103687 |
| 分割ロングピンソケット 1×42 (42P) | — | 1 | 80 | 105779 |

### 機構・記録媒体

| 名称 | 型番 / 仕様 | 数 | 単価 (税込) | 入手先 |
|---|---|---:|---:|---|
| スペーサ M3 20mm | TP-20 | 4 | 45 | 秋月 (107570) |
| 3mm プラネジ 12mm + 六角ナットセット M3 (20 個入) | — | 1 | 200 | 秋月 (102743) |
| マイクロ SD カード 16GB 100MB/s | KIOXIA EXCERIA (microSDHC, FAT32) | 1 | 680 | 秋月 (115845) |
| アルカリ乾電池 単 4 形 | — | 4 | — | 家電量販店 / ダイソー |
| ニクロム線 5m (パラシュート切り離し用) | 朝日電器 ELPA HK-NK05H | 1 | 295 | ヨドバシカメラ |

### パラシュート部材 (ダイソー)

| 品名 | 価格 |
|---|---|
| ミニバイクカバー (傘布) | 110 |
| 釣り糸ナイロン (ハリス用, 1.5 号 60m) | 110 |
| とじ穴補修シール (白) 315 枚 | 110 |
| ボールベアリングスイベル (3 号) | 110 |
| たこ糸 (20/5×3, 約 40m) | 110 |
| ストレートミニストロー (パステル, 180 本) | 110 |

> 取り扱い終了商品が多いため、店頭で類似品代用を想定する。

## 6. 電源仕様

- 入力: 単 4 アルカリ乾電池 × 4 直列 = **6V** 公称
- レギュレータ 1: **NJM2845DL1-05** (5V 800mA) → Spresense 電源
- レギュレータ 2: **NJM2845DL1-33** (3.3V 800mA) → 周辺センサ (BNO055 など)
- モータドライバ・電熱線: **6V を直接供給**
- リチウムポリマー電池は採用しない (発火リスク回避方針)

## 7. 信号インターフェース (Spresense ⇔ 各デバイス)

| 通信規格 | 接続 |
|---|---|
| SDIO | Spresense ⇔ microSD カード |
| I2C | Spresense ⇔ BNO055 (9 軸) |
| UART | Spresense ⇔ TWELITE |
| (内蔵) | Spresense GNSS 受信機 |

### ピンアサイン一覧

| 機能 | Spresense ピン | 接続先 |
|---|---|---|
| 照度センサ (CdS) | A0 | 分圧出力 |
| スピーカ | D09 (PWM) | UGCM0603APE |
| モータ A IN1 | D08 | TB6612FNG `AIN1` |
| モータ A IN2 | D04 | TB6612FNG `AIN2` |
| モータ A PWM | D05 (PWM) | TB6612FNG `PWMA` |
| モータ B IN1 | D07 | TB6612FNG `BIN1` |
| モータ B IN2 | D02 | TB6612FNG `BIN2` |
| モータ B PWM | D03 (PWM) | TB6612FNG `PWMB` |
| 9 軸センサ SDA | D14 | BNO055 `SDA` |
| 9 軸センサ SCL | D15 | BNO055 `SCL` |
| TWELITE TX | D00 | TWELITE `RX` 側 |
| TWELITE RX | D01 | TWELITE `TX` 側 |
| 電熱線駆動 | D06 | FET ゲート |
| SD カード | SDIO (CLK / CMD / DAT0-3 / CD) | microSD ソケット (Spresense メインボード搭載) |

> モータドライバ `STBY` は 10kΩ プルアップで常時 HIGH。`AO1/AO2` → モータ A、`BO1/BO2` → モータ B に接続。SpresenseGPIO 最大電流 ~6mA のため、モータ駆動 (数百 mA) は TB6612FNG 経由必須。

### TB6612FNG 入力 → 出力対応

| IN1 | IN2 | PWM | 出力 |
|---|---|---|---|
| LOW | HIGH | HIGH or PWM | 反時計回り (CCW) |
| HIGH | LOW | HIGH or PWM | 時計回り (CW) |
| LOW | LOW | HIGH or PWM | ストップ |

データシート: <https://akizukidenshi.com/goodsaffix/TB6612FNG_datasheet_ja_20141001.pdf>

## 8. 組立

### 必要工具

| 工具 | 備考 |
|---|---|
| はんだごて | 温度調整可能なもの推奨 (HAKKO / goot) |
| こて台 | — |
| はんだ | — |
| ニッパー | — |
| ピンセット | — |
| マスキングテープ | 仮固定用 |

組立動画:
- <https://www.youtube.com/watch?v=HDIxuZUPCaY>
- <https://www.youtube.com/watch?v=lvHDDWhyALk>
- <https://www.youtube.com/watch?v=YhJEej4QNnk>

### top 基板の部品とシルク対応

| # | 部品 | シルク |
|---:|---|---|
| 1 | 三端子レギュレータ NJM2845DL1-05 (5V) | U2 |
| 2 | 三端子レギュレータ NJM2845DL1-33 (3.3V) | U3 |
| 3 | CdS セル GL5528 | D6 |
| 4 | スライドスイッチ | — (シルクなし。フットプリント形状で判定) |
| 5 | XH コネクタ B2B-XH-A | J1 |
| 6 | スピーカ UGCM0603APE | BZ1 |
| 7 | ピンヘッダ 1×40 | SW2 |
| 8 | BNO055 9 軸センサモジュール | U10 |
| 9 | DIP スイッチ 2P | SW3 |
| 10 | ピンヘッダ オスL型 1×40 | J4, J5 |
| 11 | ターミナルブロック 2.54mm 2P | J9 |
| 12 | TWELITE (TWE-Lite アンテナ別付) | — (シルクなし) |
| 13 | 分割ロングピンソケット 1×42 | J8 |

### bottom 基板

| # | 部品 | シルク |
|---:|---|---|
| 1 | 電池ボックス 単 4 × 1 本 | BT1, BT2, BT3, BT4 (極性注意) |
| 2 | コネクタ付コード 2P 赤黒 | J1 (赤=＋, 黒=−) |

### はんだ付け留意点

- **U2 / U3 (レギュレータ)**: 1 端予備はんだ → こてで温めながらピンセットで IC を滑らせて位置決め → 残ピン
- **J1 (XH コネクタ)**: 切り欠きが基板外側を向く方向
- **BZ1 (スピーカ)**: シルク「＋」と部品「＋」を一致
- **U10 (BNO055)**: シルクの `VIN / GND / SDA / SCL` 表記をセンサ側と一致させる
- **bottom J1**: コード色 (赤=＋ / 黒=−) を遵守

### 機構組立

| ステップ | 部品 |
|---|---|
| モータ取付 | 3D「モータマウント (servo-bracket)」 + M3×12mm プラネジ・六角ナット |
| スタビライザー取付 | 3D「スタビライザー」 + M3×12mm プラネジ・六角ナット |
| top ↔ bottom 結合 | M3×12mm プラネジ・六角ナット + M3 20mm スペーサ (TP-20) |

---

## 9. 関連リンク

- 100kinSAT GitHub Org: <https://github.com/100kinsat>
- 旧 100kinSAT (ESP32 ベース): <https://100kinsat.github.io/>
- Spresense 開発者ポータル: <https://developer.sony.com/ja/spresense/>
- TWELITE 製品ページ: <https://mono-wireless.com/jp/products/TWE-LITE/index.html>
