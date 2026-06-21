# Spresense + Arduino IDE 開発環境 構築手順

本書は Issue #3「Spresense + Arduino IDE 開発環境の構築と Lチカ動作確認」の手順書。
Spresense メインボードに対して **ビルド → 書き込み → シリアル出力** が一通り通る状態を作ることを目的とする。

- 対象OS: macOS（本リポジトリの開発環境）。Windows / Linux でも基本手順は同じ。
- 確認バージョン: **Arduino IDE 2.3.2 / Spresense Boards 3.0.0**
- 一次情報（最新はこちらを優先）: [Spresense Arduino 開発環境 セットアップ（公式・日本語）](https://developer.sony.com/spresense/development-guides/arduino_set_up_ja.html)

> 仕様の出典: `doc/cansat_specification/software.md` §1（開発環境）, §2（シリアル共通設定）, §4（外部ライブラリ）

---

## 0. 用意するもの

- Spresense メインボード
- USB ケーブル（**データ通信対応**のもの。給電専用ケーブルは不可）
- microSD は本手順では不要（Lチカのみ）

---

## 1. USB シリアルドライバの導入（macOS）

Spresense メインボードの USB-シリアル変換は **CP210x（Silicon Labs）**。macOS のバージョンによっては手動でドライバ導入が必要。

1. [Silicon Labs CP210x VCP Driver](https://jp.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) を入手しインストール
2. インストール後、必要に応じて「システム設定 → プライバシーとセキュリティ」でドライバ（Silicon Labs）を**許可**
3. ボードを接続し、ポートが見えることを確認:
   ```
   ls /dev/cu.*
   ```
   `/dev/cu.SLAB_USBtoUART` もしくは `/dev/cu.usbserial-XXXX` のように表示されればOK。

> 近年の macOS では標準ドライバで認識される場合もある。先に接続して `ls /dev/cu.*` で確認し、見えなければドライバを入れる。

---

## 2. Arduino IDE のインストール

1. [Arduino IDE 公式](https://www.arduino.cc/en/software) から **2.3.2** を入手・インストール
2. 起動して日本語表示等は任意で設定

---

## 3. Spresense Boards（コア）の導入

1. Arduino IDE の **環境設定 → 追加のボードマネージャの URL** に以下を追加:
   ```
   https://github.com/sonydevworld/spresense-arduino-compatible/releases/download/generic/package_spresense_index.json
   ```
2. **ボードマネージャ**（左サイドバーのボードアイコン）で `Spresense` を検索し、**Spresense Boards 3.0.0** をインストール
3. **ツール → ボード → Spresense Boards → Spresense** を選択

> サンプルやボードが一覧に出ないときは、`ツール → ボード → Spresense Boards → Spresense` を選び直す。すでに選択済みなら一度別ボードを選んでから再選択する（software.md §1）。

---

## 4. ブートローダの書き込み（初回のみ）

Spresense を初めて使う場合、または Spresense Boards（ボードパッケージ）を更新した場合は、ブートローダの書き込み／更新が必要。

1. ボードを USB 接続し、**ツール → ボード** が `Spresense` になっていることを確認
2. **ツール → ポート** で 手順1 で確認したポート（`/dev/cu.SLAB_USBtoUART` 等）を選択
   - Firmware Updater は**選択中のシリアルポート**に対して動作するため、ブートローダ書き込み前に必ず選ぶ。複数デバイス接続時は特に対象ポートを取り違えないよう注意
3. **ツール → 書き込み装置（Programmer） → Spresense Firmware Updater** を選択
4. **ツール → ブートローダを書き込む（Burn Bootloader）** を実行
5. 初回は EULA（使用許諾）の同意ダイアログが出るので、内容を確認して同意する
6. 画面の指示に従い完了させる

> 「書き込み装置」で `Spresense Firmware Updater` を選ばずに「ブートローダを書き込む」を実行しても、正しく書き込めない。手順3→手順4の順を守ること。
>
> 詳細・最新手順は公式セットアップガイドを参照。

---

## 5. 書き込み速度などの設定（必要時）

1. **ツール → Core** / **Memory** などは既定のままでよい
2. 書き込みが遅い/失敗する場合は **ツール → Speed**（Upload speed）を下げて再試行（例: 115200 へ）

> ポートは手順4で選択済み。別のボードに差し替えた場合は **ツール → ポート** を選び直す。

---

## 6. 必要ライブラリの導入（記録）

本プロジェクトで使う外部ライブラリ。Lチカでは不要だが、今後のモジュール実装で使うため導入手順を記録しておく。

| ライブラリ | 用途 | 入手元 | 備考 |
|---|---|---|---|
| `GNSS.h` | Spresense 内蔵 GNSS | Spresense Boards に同梱 | 追加インストール不要 |
| `SDHCI.h` / `File.h` | microSD アクセス | Spresense Boards に同梱 | 追加インストール不要 |
| `Adafruit_BNO055` | 9軸センサ BNO055 | ライブラリマネージャで検索 | 確認バージョン **1.6.3**。依存の `Adafruit Unified Sensor` も併せて入る |

導入手順（`Adafruit_BNO055`）:

1. **ツール → ライブラリを管理**（ライブラリマネージャ）を開く
2. `Adafruit BNO055` を検索してインストール（依存ライブラリも一緒に入れる）
3. 例題は **ファイル → スケッチ例 → Adafruit BNO055 → rawdata** から開ける

---

## 7. Lチカの書き込みと動作確認

1. 本リポジトリの [`src/blink_led/blink_led.ino`](../../src/blink_led/blink_led.ino) を Arduino IDE で開く
2. **検証（✓）** でビルドが通ることを確認
3. **書き込み（→）** で Spresense へ転送
4. 転送後、メインボードの **4個の LED がナイトライダー風に往復点灯**することを目視確認

---

## 8. シリアル出力の確認（115200bps）

1. **ツール → シリアルモニタ** を開く
2. 右下のボーレートを **115200** に設定
3. **メインボードのリセットボタンを押す**（書き込み直後はモニタを開く前に起動バナーが流れてしまうため。リセットで再起動し、バナーを先頭から確認できる）
4. 起動バナーと、約1秒ごとのハートビートが出力されることを確認:
   ```
   =============================================
    100kinSAT comeback 2026 / blink_led
    Spresense build & serial sanity check
    baud: 115200 bps
    LED count: 4
   =============================================
   [heartbeat] count=1 uptime_ms=1234
   [heartbeat] count=2 uptime_ms=2480
   ...
   ```

> Spresense の `Serial` は USB-UART(CP210x) で、モニタの接続有無を検知しない（スケッチ側で待てない）。そのためリセットせずに開くと**バナーは見えずハートビートだけ**が見える場合がある。バナーが見えなくても、ハートビートが流れていればシリアル疎通（115200bps）は確認できている。
>
> 文字化けする場合はボーレートが 115200 になっているか確認する。

---

## 9. 完了チェックリスト（Issue #3 DoD 対応）

- [ ] Arduino IDE 2.3.2 / Spresense Boards 3.0.0 をセットアップした（手順2〜3）
- [ ] 必要ライブラリの導入手順を記録した（手順6 ＝ 本書に記載済み）
- [ ] `blink_led` を書き込み、LED の点灯を確認した（手順7）
- [ ] シリアルモニタ 115200bps で出力を確認した（手順8）
- [ ] 手順を doc 化した（＝本書）

> `arduino-cli` によるコマンドラインビルド（CI 連携の前提）は Issue #4 で整備する。

---

## トラブルシュート

| 症状 | 対処 |
|---|---|
| ポートが `/dev/cu.*` に出ない | データ通信対応ケーブルか確認 → 手順1のドライバ導入と許可 |
| 書き込みが途中で失敗 | Upload speed を下げる（手順5） / ケーブル・ポートを変える |
| シリアルが文字化け | シリアルモニタのボーレートを 115200 に設定 |
| ボード一覧に Spresense が出ない | `ツール → ボード → Spresense Boards → Spresense` を選び直す（手順3の注記） |
| ビルドは通るが LED が光らない | ブートローダ書き込み（手順4）が済んでいるか確認 |
