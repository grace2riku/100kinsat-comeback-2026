# 100kinSAT ソフトウェア仕様

本書は [SpresenseではじめるCanSat開発【準備編】](https://zenn.dev/ymt117/books/100kinsat-spr-build) と [基礎編](https://zenn.dev/ymt117/books/100kinsat-spr-basic) に基づき、100kinSAT のソフトウェア仕様 (開発環境、コード配布、各モジュールの制御仕様、ライブラリ、無線通信のセットアップ) を整理したものです。

ソースリポジトリ: <https://github.com/100kinsat/100kinsat-spresense>

ハードウェア (ピンアサイン・回路) は [hardware.md](./hardware.md) を参照。

---

## 1. 開発環境

| 項目 | バージョン (準備編記載時) |
|---|---|
| OS | Windows (22H2 / OS Build 22621.3155)。MacOS / Linux でも可 |
| IDE | Arduino IDE 2.3.2 |
| Spresense Boards | 3.0.0 |

セットアップ手順 (Spresense 公式):
<https://developer.sony.com/spresense/development-guides/arduino_set_up_ja.html>

### サンプルプログラムが表示されないとき

Arduino IDE 上部メニューの `Tools > Board > Spresense Boards > Spresense` を選択。既に選択されている場合は、一度別ボードを選んでから再選択する。

## 2. シリアル通信の共通設定

- ボーレート: **115200 bps**
- すべての例題は `Serial.begin(115200)` を `setup()` で実行
- シリアルモニタ / シリアルプロッタのボーレートも 115200 に合わせる

## 3. ソースコード配布

各モジュールのサンプルは `100kinsat/100kinsat-spresense` の `src/basic/` 配下にある。

| モジュール | スケッチ |
|---|---|
| LED (Spresense サンプル) | Spresense Examples 内 |
| 照度センサ | [`src/basic/cds/cds.ino`](https://github.com/100kinsat/100kinsat-spresense/blob/main/src/basic/cds/cds.ino) |
| スピーカ (単音列) | [`src/basic/speaker/speaker.ino`](https://github.com/100kinsat/100kinsat-spresense/blob/main/src/basic/speaker/speaker.ino) |
| スピーカ (メロディ関数版) | [`src/basic/speaker2/speaker2.ino`](https://github.com/100kinsat/100kinsat-spresense/blob/main/src/basic/speaker2/speaker2.ino) |
| モータ | [`src/basic/motor/motor.ino`](https://github.com/100kinsat/100kinsat-spresense/blob/main/src/basic/motor/motor.ino) |
| GPS (GNSS) | [`src/basic/gnss/gnss.ino`](https://github.com/100kinsat/100kinsat-spresense/blob/main/src/basic/gnss/gnss.ino) |
| 9 軸センサ | Adafruit BNO055 ライブラリ付属 `rawdata.ino` |
| SD カード | Spresense Examples `SDHCI > read_write` |

## 4. 必要な外部ライブラリ

| ライブラリ | 用途 | 入手元 |
|---|---|---|
| `GNSS.h` | Spresense 内蔵 GNSS | Spresense Boards (デフォルト同梱) |
| `SDHCI.h` / `File.h` | microSD アクセス | Spresense Boards |
| `Adafruit_BNO055` | BNO055 9 軸センサ | Arduino IDE「ライブラリマネージャ」で `Adafruit BNO055` を検索 (確認バージョン: 1.6.3) |

`Adafruit BNO055` をインストールすると `File > Examples > Adafruit BNO055 > rawdata` から例題が開ける。

## 5. モジュール別制御仕様

### 5.1 LED

- Spresense メインボード搭載の 4 個 LED を `digitalWrite` で制御
- Spresense 公式チュートリアル「LEDのスケッチを動かしてみる」を参照

### 5.2 照度センサ (CdS / GL5528) — `A0`

用途: 放出 (上空投下) の検出。投下装置内 / 外で明るさ差が大きいことを利用。

```cpp
const uint8_t cds = A0;

void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println(analogRead(cds));
  delay(1000);
}
```

- `analogRead()` は 0–1023 (10-bit) を返す
- 明るい → 値が小さい / 暗い → 値が大きい (分圧の向きに依存)

### 5.3 スピーカ (UGCM0603APE) — `D09` (PWM)

用途: 起動音、エラー通知、状態通知。

#### 単音 (`speaker.ino`)

`tone()` 関数で矩形波を出力 ([Arduino リファレンス](https://www.arduino.cc/reference/en/language/functions/advanced-io/tone/))。

```cpp
tone(
  sp,   // 出力ピン (D09)
  261   // 周波数 [Hz] (例: ド)
);
delay(150);  // 出力時間 [ms]
```

#### メロディ関数 (`speaker2.ino`)

```cpp
/** メロディを鳴らす関数 */
void beep(float *mm, int m_size, int b_time) {
  for (int i = 0; i < m_size; i++) {
    tone(sp, mm[i]);
    delay(b_time);
  }
  noTone(sp);
}
```

- 第 1 引数: 周波数配列
- 第 2 引数: 配列要素数
- 第 3 引数: 各音の発音時間 [ms]

### 5.4 モータ (TB6612FNG, 2ch) — `D02-D05, D07, D08`

#### 制御ロジック (1 モータあたり 3 ピン: IN1 / IN2 / PWM)

| IN1 | IN2 | PWM | 動作 |
|---|---|---|---|
| LOW | HIGH | HIGH または PWM | 反時計回り |
| HIGH | LOW | HIGH または PWM | 時計回り |
| LOW | LOW | HIGH または PWM | ストップ |

`STBY` は基板上で 10kΩ プルアップ済みのため常時 HIGH。

#### ピン定義と初期化

```cpp
const uint8_t m1[3] = {8, 4, 5};  // モータA: AIN1, AIN2, PWMA
const uint8_t m2[3] = {7, 2, 3};  // モータB: BIN1, BIN2, PWMB

void setup() {
  for (int i = 0; i < 3; i++) {
    pinMode(m1[i], OUTPUT);
    pinMode(m2[i], OUTPUT);
  }
}
```

#### 前進例 (PWM デューティ 100/255)

```cpp
digitalWrite(m1[0], HIGH);
digitalWrite(m1[1], LOW);
analogWrite(m1[2], 100);
digitalWrite(m2[0], HIGH);
digitalWrite(m2[1], LOW);
analogWrite(m2[2], 100);
delay(5000);
```

- PWM は `analogWrite()` で 0–255 を指定
- PWM デューティ ↑ で速度 ↑
- 左右の IN1/IN2 を反転すれば旋回

### 5.5 GPS / GNSS (Spresense 内蔵)

用途: 目標地点との距離・方位の計算。

#### 初期化シーケンス

```cpp
#include <GNSS.h>
#define STRING_BUFFER_SIZE 128
static SpzGnss Gnss;

void setup() {
  Serial.begin(115200);
  sleep(3);
  Gnss.setDebugMode(PrintInfo);
  int result = Gnss.begin();
  if (result != 0) { /* error */ }
  else {
    Gnss.select(GPS);
    Gnss.select(GLONASS);
    Gnss.select(QZ_L1CA);
  }
  result = Gnss.start(COLD_START);
}
```

#### デバッグログレベル (`setDebugMode`)

| 定数 | 内容 |
|---|---|
| `PrintNone` | 出力なし (デフォルト) |
| `PrintError` | Error のみ |
| `PrintWarning` | Warning 以上 |
| `PrintInfo` | Info 以上 |

#### 衛星種別 (`Gnss.select`)

選択可能: `GPS` / `GLONASS` / `SBAS` / `QZ_L1CA` / `QZ_L1S` / `BEIDOU` / `GALILEO`

#### スタートモード (`Gnss.start`)

| 定数 | 用途 |
|---|---|
| `COLD_START` | 受信履歴なしから初期化 |
| `WARM_START` | 中間状態 |
| `HOT_START` | 短時間ぶりの再起動 (デフォルト) |

> 測位速度: HOT > WARM > COLD

#### loop での測位データ取得

```cpp
void loop() {
  if (Gnss.waitUpdate(-1)) {  // -1: タイムアウトなし
    SpNavData NavData;
    Gnss.getNavData(&NavData);
    if (NavData.posDataExist == 0) {
      Serial.printf("numSat: %2d ", NavData.numSatellites);
      Serial.print("No Position");
    } else {
      Serial.print(NavData.latitude, 6);
      Serial.print(",");
      Serial.print(NavData.longitude, 6);
    }
    Serial.println("");
  } else {
    Serial.println("data not update");
  }
}
```

#### `SpNavData` 主要メンバ

| 型 | 名前 | 内容 |
|---|---|---|
| `SpGnssTime` | `time` | 更新時刻 |
| `unsigned char` | `type` | 0:Invalid, 1:GNSS, 2:reserv, 3:user set, 4:previous |
| `unsigned char` | `numSatellites` | 見えている衛星数 |
| `unsigned char` | `posFixMode` | 1:Invalid, 2:2D FIX, 3:3D FIX |
| `unsigned char` | `posDataExist` | 0:なし, 1:あり |
| `unsigned char` | `numSatelliteCalcPos` | 位置計算に使用した衛星数 |
| `unsigned short` | `satelliteType` / `posSatelliteType` | bit0:GPS, bit1:GLONASS, ... |
| `double` | `latitude` / `longitude` | 緯度・経度 [°] |
| `double` | `altitude` | 高度 [m] |
| `float` | `velocity` | 速度 [m/s] |
| `float` | `direction` | 方位 [°] |
| `float` | `pdop` / `hdop` / `vdop` / `tdop` | DOP (Dilution Of Precision) |
| `SpSatellite` | `satellite[24]` | 衛星データ配列 |

#### 実験時の注意

- 屋外 (見通しの良い場所) で計測。屋内は精度劣化または受信不可
- 自宅で計測した結果の SNS 公開は避ける

### 5.6 9 軸センサ (BNO055) — I2C `D14 (SDA) / D15 (SCL)`

I2C アドレス: 秋月電子の出荷時設定は `0x28` (`0x29` も指定可)。

#### インスタンス生成

```cpp
Adafruit_BNO055 bno = Adafruit_BNO055(-1, 0x28, &Wire);
//                                     ^^   ^^^^   ^^^^^
//                                     ID   addr   Wireポインタ
```

#### 初期化

```cpp
if (!bno.begin()) {
  Serial.print("Ooops, no BNO055 detected ... Check your wiring or I2C ADDR!");
  while (1);
}
// 中略
bno.setExtCrystalUse(true);  // 外部クロック有効化 (秋月版は外部クロック付き)
```

引数なし `begin()` のデフォルトは `OPERATION_MODE_NDOF`。

#### 動作モード

| モード | 概要 |
|---|---|
| `OPERATION_MODE_ACCONLY` | 加速度のみ |
| `OPERATION_MODE_MAGONLY` | 地磁気のみ |
| `OPERATION_MODE_GYRONLY` | ジャイロのみ |
| `OPERATION_MODE_ACCMAG` | 加速度+地磁気 |
| `OPERATION_MODE_ACCGYRO` | 加速度+ジャイロ |
| `OPERATION_MODE_MAGGYRO` | 地磁気+ジャイロ |
| `OPERATION_MODE_AMG` | 加速度+地磁気+ジャイロ |
| `OPERATION_MODE_IMUPLUS` | 加速度+ジャイロ融合 |
| `OPERATION_MODE_COMPASS` | 地磁気方位 |
| `OPERATION_MODE_M4G` | 地磁気で回転検出 |
| `OPERATION_MODE_NDOF_FMC_OFF` | 加速+地磁気+ジャイロ (地磁気校正なし) |
| `OPERATION_MODE_NDOF` | 加速+地磁気+ジャイロ (地磁気校正あり, **デフォルト**) |

データシート: <https://cdn-shop.adafruit.com/datasheets/BST_BNO055_DS000_12.pdf>

#### 値の取得

```cpp
imu::Vector<3> euler = bno.getVector(Adafruit_BNO055::VECTOR_EULER);
// euler.x() / euler.y() / euler.z() で各軸値

int8_t temp = bno.getTemp();  // 内蔵温度センサ
```

`getVector()` に渡せる定数で取得値が変わる (VECTOR_ACCELEROMETER, VECTOR_GYROSCOPE, VECTOR_MAGNETOMETER, VECTOR_EULER, VECTOR_LINEARACCEL, VECTOR_GRAVITY 等)。

#### キャリブレーション状態

```cpp
uint8_t system, gyro, accel, mag = 0;
bno.getCalibration(&system, &gyro, &accel, &mag);
```

各値 0–3。**`3` で完全キャリブレーション完了**。平面に置いて起動後、ぐるぐる動かすとキャリブレーションが進行する。

### 5.7 SD カード — SDIO (Spresense メインボード搭載ソケット)

- 通信規格: SDIO
- フォーマット: **FAT32 必須**
- ライブラリ: `SDHCI.h`, `File.h`

#### 基本操作 (`read_write.ino`)

```cpp
#include <Arduino.h>
#include <SDHCI.h>
#include <File.h>

SDClass SD;
File myFile;

// SDマウント待ち
while (!SD.begin()) { ; }

// ディレクトリ作成 (既存なら何もしない)
SD.mkdir("dir/");

// 書き込みモードでオープン
myFile = SD.open("dir/test.txt", FILE_WRITE);
if (myFile) {
  myFile.println("testing 1, 2, 3.");
  myFile.close();
}

// 読み出し
myFile = SD.open("dir/test.txt");
while (myFile.available()) {
  Serial.write(myFile.read());
}
myFile.close();
```

#### モード

| 定数 | 内容 |
|---|---|
| `FILE_READ` | 読み込みのみ |
| `FILE_WRITE` | 読み込み・書き込み・作成 |

> **同時に開けるファイルは 1 個**。別ファイルを開く前に `close()` する。

> SDIO ピン (回路図上の名称): `SPR_SDIO_CLK / CMD / DATA0 / DATA1 / DATA2 / DATA3 / CD`。これらは Spresense 内部割当のため Arduino スケッチからの直接操作は不要。

### 5.8 電熱線 (パラシュート切り離し) — `D06`

**注意: 加熱部は非常に熱くなる。実験時は触れないこと。**

- 大電流が必要なため **FET ドライバ経由**
- `D06` を HIGH にする時間で発熱量を制御 (具体的なシーケンスは [基礎編 chapter 9](https://zenn.dev/ymt117/books/100kinsat-spr-basic/viewer/heating-wire) — 執筆途中)

### 5.9 TWELITE 無線通信 — UART `D00 (TX) / D01 (RX)`

- 機体側: TWELITE (TWE-Lite アンテナ別付タイプ)
- 地上側: MONOSTICK (USB スティック)
- 通信規格: UART (Spresense ⇔ TWELITE)
- 配信ファーム: **App_Uart (CONFIG/TWE UART APP)**

#### セットアップ用ツール

TWELITE STAGE SDK (確認バージョン `MWSTAGE2022_08_30`):
<https://mono-wireless.com/jp/products/stage/index.html>

#### 機体側 TWELITE 書込手順

1. CanSat ⇔ TWELITE R ⇔ PC で接続
2. `TWELITE_Stage.exe` を起動
3. 「TWELITE R2 (xxx)」を選択 (xxx は CanSat 接続後の番号)
4. インタラクティブモードへ切替
5. 現在のアプリ確認 — 「CONFIG/TWE UART APP」ならそのまま終了。それ以外なら次へ
6. アプリ書換モードへ切替
7. 「BIN から選択」→ `App_Uart_TWELITEUART...` で始まるアプリを選択し書き込み
8. 書込完了後 ENTER でインタラクティブモードへ。「CONFIG/TWE UART APP」表示を確認
9. TWELITE STAGE APP 終了

#### 地上側 (MONOSTICK)

書換不要。出荷時設定で動作可。

> プログラム本体については [基礎編 chapter 10](https://zenn.dev/ymt117/books/100kinsat-spr-basic/viewer/twelite) は執筆途中のため、TWELITE 公式ドキュメントおよびリポジトリのサンプルを併用すること。

### 5.10 カメラ (Spresense カメラボード) — 専用コネクタ（Issue #52）

赤コーン終端誘導用の撮像・検出モジュール。ハードウェアは `hardware.md` §7「カメラボード」参照。

#### Camera ライブラリ（Spresense Arduino 同梱）

```cpp
#include <Camera.h>

theCamera.begin(1, CAM_VIDEO_FPS_5, CAM_IMGSIZE_QVGA_H, CAM_IMGSIZE_QVGA_V,
                CAM_IMAGE_PIX_FMT_YUV422);           // ビデオバッファ1面・QVGA/YUV422
theCamera.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_DAYLIGHT);  // 屋外は DAYLIGHT 固定
theCamera.startStreaming(true, camCallback);          // YUV フレームはストリーミングで受ける
theCamera.setStillPictureImageFormat(w, h, CAM_IMAGE_PIX_FMT_JPG);  // still は JPEG 専用
CamImage img = theCamera.takePicture();               // 同期取得（失敗時 isAvailable()=false）
```

- **`CAM_IMAGE_PIX_FMT_YUV422` は `V4L2_PIX_FMT_UYVY`（UYVY バイト順: 2画素4バイト
  [U0, Y0, V0, Y1]）**。一般的な YUYV とは異なる（取り違えると輝度と色差が入れ替わる）。
- **still 撮影（takePicture）は JPEG 専用**。YUV422 を still に指定すると
  `setStillPictureImageFormat` は SUCCESS を返すのに `takePicture` が失敗する（実機確認
  2026-07-19）。YUV フレームは公式サンプルと同じく**ビデオストリーミング経由**で取得する
  （`spresense_gotchas.md` B23。`hal::SpresenseCamera::captureDetectFrame` が同期 I/F に包む）。
- QVGA/YUV422 のフレームは 320×240×2 = 153,600 bytes。`begin()` のビデオバッファに加え、
  HAL が検出用コピー先（153,600B, BSS 静的確保）を持つ。**コピー先は `spresense_camera.h` を
  include するスケッチでは `cam init` を呼ばなくても起動時から常時消費される**点に注意
  （メモリ見積りに含めること）。メモリ共存（GNSS/TWELITE/datalog）は実機で確認する
  （`camera_bringup.md` 手順F）。
- `takePicture()` は CamErr を返さない（`CamImage::isAvailable()` で成否判定）。
  CamErr を返す API のエラー名は `hal::SpresenseCamera::camErrName` で表示できる。
- still バッファは**1面のみ**で、`CamImage` は参照カウント式（最後の参照が解放されて
  初めてバッファが再キューされる）。前の JPEG を保持したまま `takePicture()` を呼ぶと
  失敗するため、`hal::SpresenseCamera::captureJpeg` は出力変数の旧参照を先に解放する
  （同時に保持できるのは1枚。詳細は `spresense_gotchas.md` B22）。

#### 本リポジトリの実装（Issue #52）

| 層 | ファイル | 内容 |
|---|---|---|
| core | `src/lib/core/cone_detect.{h,cpp}` | 赤閾値→列ヒストグラム→区間連結→重心/幅/方位角（HW非依存・ホストテスト済） |
| hal | `src/lib/hal/spresense_camera.h` | Camera ライブラリの薄いラッパ（QVGA YUV 取得 / VGA JPEG / オンデマンド初期化） |
| hal | `src/lib/hal/sd_image_store.h` | 画像の SD 保存（`cam/imgNNN.<ext>` 空き連番採番） |
| shell | `cam` コマンド | init / snap / dump / detect / mon / thr（`serial_shell.md`） |

検出出力 I/F（`cone::Detection`）: `detected` / `bearingDeg`（画角内方位角、右+）/
`widthRatio`（近接度指標）/ `confidence`（区間内赤密度）/ `redPixels`。
Phase3 のナビ（#18）・ゴール判定（#19）がこの I/F を購読する。
色閾値・画角の実機校正手順は `doc/development/camera_bringup.md`。

## 6. 未整備セクション

原典で **「執筆途中」** とされているもの:

- 準備編 chapter 7: トラブルシュート (JLCPCB 在庫切れ対処等)
- 基礎編 chapter 9: 電熱線制御
- 基礎編 chapter 10: TWELITE 制御プログラム本体

これらは将来更新を確認するか、リポジトリの実装を直接参照する。

## 7. 関連リンク

- Spresense Arduino 開発ガイド: <https://developer.sony.com/spresense/development-guides/arduino_set_up_ja.html>
- Spresense IO 電流値: <https://developer.sony.com/spresense/development-guides/hw_docs_ja.html>
- Spresense FAQ (SD カード関連含む): <https://developer.sony.com/spresense/development-guides/faq_ja.html>
- TB6612FNG データシート (秋月): <https://akizukidenshi.com/goodsaffix/TB6612FNG_datasheet_ja_20141001.pdf>
- BNO055 データシート (Adafruit): <https://cdn-shop.adafruit.com/datasheets/BST_BNO055_DS000_12.pdf>
- Arduino `tone()` リファレンス: <https://www.arduino.cc/reference/en/language/functions/advanced-io/tone/>
- Arduino `Serial.print()` リファレンス: <https://www.arduino.cc/reference/en/language/functions/communication/serial/print/>
