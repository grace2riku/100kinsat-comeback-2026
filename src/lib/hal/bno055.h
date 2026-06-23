#ifndef HAL_BNO055_H
#define HAL_BNO055_H

#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <Wire.h>

#include "compass.h"

// bno055.h - 9軸センサ BNO055 の実機ラッパ（hal層, Arduino/Adafruit 依存）
//
// Adafruit_BNO055 を薄く包み、HW非依存ロジック src/lib/core/compass で正規化・判定できる形
// （方位角 [0,360) と compass::CalibrationStatus）に変換して返す。Arduino.h / Wire /
// Adafruit ライブラリに依存するため実機ビルド（arduino-cli）でのみ使う。ロジックのホストテストは
// compass 側（test/core/test_compass.cpp）で行い、本ラッパは NT-Shell の imu コマンドで実機確認する
// （doc/development/imu_bringup.md）。
//
// 設計（gotchas A: ターゲット≠ホスト）:
//   - センサ未検出は begin() が false を返す（DoD: 未検出時のエラーハンドリング）。
//   - 未初期化での heading() は番兵 kInvalidHeading を返し、呼び出し側が isValidHeading で弾ける。
//   - getCalibration へ渡す変数は1つずつゼロ初期化する（gotchas B7: 部分初期化の罠を避ける）。

namespace hal {

class Bno055Compass {
 public:
  // addr: I2C アドレス（秋月電子の出荷時 0x28、ジャンパ変更で 0x29）。software.md §5.6。
  explicit Bno055Compass(uint8_t addr = 0x28) : bno_(-1, addr, &Wire) {}

  // 検出・初期化し外部クロックを有効化する。false=未検出（I2C 配線/アドレス/電源を疑う）。
  // 既定動作モードは NDOF（加速+地磁気+ジャイロ融合、地磁気校正あり）。
  // Adafruit の begin() は検出を最大 850ms のタイムアウトループで試すため、未検出でも
  // 無限ハングはせず false を返す（imu_bringup 手順Aで実測確認）。
  bool begin() {
    if (!bno_.begin()) {  // 既定 OPERATION_MODE_NDOF
      begun_ = false;
      return false;
    }
    // setExtCrystalUse 前の安定待ち（Adafruit rawdata 例に倣う追加待ち。begin() 内蔵の
    // 検出/リセット待ちとは別で、成功パスでは重複しない。実機で短縮余地を確認してよい）。
    delay(1000);
    bno_.setExtCrystalUse(true);  // 秋月版は外部クロック付き（方位精度の向上）
    begun_ = true;
    return true;
  }

  // begin() が成功済みか。
  bool ready() const { return begun_; }

  // 方位角 [0,360)[deg]。未初期化なら kInvalidHeading（NaN）を返す。
  // Euler ベクトルの x() を heading として扱い、compass で [0,360) の不変条件を担保する。
  // 注意: 基準(北=0)・回転の向き(時計回りで増加か)は BNO055 の軸定義と基板取付向きに依存し、
  //       未検証。実機で確認し（imu_bringup 手順C）、オフセット/符号の吸収は #18 で行う。
  double heading() {
    if (!begun_) {
      return compass::kInvalidHeading;
    }
    imu::Vector<3> euler = bno_.getVector(Adafruit_BNO055::VECTOR_EULER);
    // Adafruit の getVector は I2C 読み取り失敗（ブラウンアウト/活線抜け/一過性エラー）でも
    // readLen の戻り値を無視し、ゼロ初期化バッファ（=全軸 0.0）をそのまま返す。これを「有効な
    // 北(0°)」と誤認すると下流（imu 表示・#18 ナビ）が偽の方位で動くため、Euler 3軸が厳密に
    // 全て 0.0 のときは読み取り失敗とみなし kInvalidHeading を返す（gotchas B8）。実機の姿勢が
    // 3軸とも厳密に 0.0 になることは事実上なく、正常値の取りこぼしは無視できる。
    if (euler.x() == 0.0 && euler.y() == 0.0 && euler.z() == 0.0) {
      return compass::kInvalidHeading;
    }
    return compass::normalizeHeading(euler.x());
  }

  // キャリブレーション状態（各0-3）。未初期化なら全0を返す。
  // 注意: Adafruit の getCalibration は void で I2C 読み取り失敗を通知しない。読めなかった場合は
  //       前回値/0 が残るため、「全0(未校正)」と「読み取り失敗」を本APIでは区別できない。
  //       校正の信頼性は heading() の有効性（isValidHeading）と併せて判断すること。
  compass::CalibrationStatus calibration() {
    // gotchas B7: 複数変数はゼロ初期化を1つずつ明示（`uint8_t a,b,c,d=0;`
    // の部分初期化罠を避ける）。
    uint8_t system = 0;
    uint8_t gyro = 0;
    uint8_t accel = 0;
    uint8_t mag = 0;
    if (begun_) {
      bno_.getCalibration(&system, &gyro, &accel, &mag);
    }
    return compass::CalibrationStatus{system, gyro, accel, mag};
  }

 private:
  Adafruit_BNO055 bno_;
  bool begun_ = false;
};

}  // namespace hal

#endif  // HAL_BNO055_H
