#ifndef HAL_BNO055_H
#define HAL_BNO055_H

#include <Adafruit_BNO055.h>
#include <Adafruit_Sensor.h>
#include <Arduino.h>
#include <Wire.h>

#include "compass.h"
#include "landing.h"

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
  explicit Bno055Compass(uint8_t addr = 0x28) : bno_(-1, addr, &Wire), addr_(addr) {}

  // 検出・初期化し外部クロックを有効化する。false=未検出（I2C 配線/アドレス/電源を疑う）。
  // 既定動作モードは NDOF（加速+地磁気+ジャイロ融合、地磁気校正あり）。
  // 注意（ハングしうる）: Adafruit の begin() は「最初から不在」なら ~850ms のタイムアウトで false
  // を
  //   返すが、一度 ACK した後のソフトリセット→CHIP_ID 待ち（内部 while ループ）で応答が戻らないと
  //   無限ブロックし得る（ブラウンアウト/活線抜け）。このため呼び出し側は **setup() では呼ばず**、
  //   'imu init' のオンデマンド実行にしてシェル起動を止めないこと（gotchas
  //   B9）。復帰はボードリセット。
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

  // 方位角 [0,360)[deg]。未初期化、または I2C 読み取り失敗（リトライ尽き）なら kInvalidHeading。
  // Euler ヘディングレジスタ(0x1A=LSB,0x1B=MSB)を自前 Wire 読みし、トランザクションの成否を
  // endTransmission()/requestFrom() の戻り値で判定する（Spresense Wire: 0=成功 / requestFrom は
  // 失敗時 0）。値の合成・正規化は compass::eulerHeadingFromRaw（ホストテスト済）に委ねる。
  // なぜ Adafruit getVector を使わないか（gotchas B8/B11）: getVector は readLen
  // の成否(bool)を捨て、
  //   読み取り失敗時もゼロ値→0.0(=真北と区別不能) を返す。走行制御に 0.0 を流すと「北を向いている」
  //   と誤認するため、本ラッパは読み取り失敗を検知して **kInvalidHeading** を返す（呼び出し側は
  //   isValidHeading で弾ける）。走行中の振動/EMI による瞬断（imu_bringup 手順B
  //   参照）に対する保険。
  // 注意: Wire コアは失敗時に "ERROR: Failed to read from i2c" を自前 printf
  // する（抑止不可）。その行は
  //   リトライ毎に出るが、最終的な健全判断は本戻り値（無効値か否か）で行う。基準(北=0)・回転向きの
  //   物理的正しさは未検証で、オフセット/符号の吸収は #18（imu_bringup 手順C）。
  double heading() {
    if (!begun_) {
      return compass::kInvalidHeading;
    }
    for (uint8_t attempt = 0; attempt < kReadAttempts; ++attempt) {
      double h = 0.0;
      if (tryReadHeading(h)) {
        return h;
      }
      if (attempt + 1 < kReadAttempts) {
        delay(kRetryDelayMs);  // 一過性の瞬断はわずかな待ちで回復しうる
      }
    }
    return compass::kInvalidHeading;  // 全リトライ失敗 → 無効値（0.0=北 を返さない）
  }

  // 方位を1回だけ読む（リトライ・delay なし）。成功時 true で out に [0,360)、失敗時 false（out
  // 不変）。 リトライ方針を呼び出し側の制御周期予算に委ねたい用途（#18
  // のナビ制御など）はこちらを使い、 「周期内で1回試行→失敗なら次周期で再試行」にして HAL 内 delay
  // でブロックしないこと （gotchas B5: 停止応答性／`delay` 一括回避, B11:
  // リトライ方針は周期予算を知る層に出す）。 heading() はこれを kReadAttempts
  // 回まで包んだ簡便版（mon/単発読み向け）。
  bool tryReadHeading(double& out) {
    if (!begun_) {
      return false;
    }
    uint8_t lsb = 0;
    uint8_t msb = 0;
    if (!readEulerHeadingRaw(lsb, msb)) {
      return false;
    }
    out = compass::eulerHeadingFromRaw(lsb, msb);
    return true;
  }

  // 加速度3軸 [m/s^2]（重力を含む生の加速度）を読む。成功時 true で out を埋め、未初期化 or
  // I2C 読み取り失敗（NACK/バイト不足）は false（out 不変）。着地(静止)検知 core/landing へ渡す。
  // heading() と同じく Adafruit getVector を使わず自前 Wire 読みで成否を返す（gotchas B8/B11）:
  //   getVector は読み取り失敗時もゼロ→(0,0,0)=|a|0 を返すため、失敗を静止判定へ流すと危険
  //   （※ landing 側は |a|≒g を要求するので (0,0,0) では誤着地しないが、瞬断を静止扱いしないため
  //     成否を明示的に返して呼び出し側でゲートできるようにする）。値の換算は
  //   landing::accelFromRaw（ホストテスト済）に委ねる。
  bool readAcceleration(landing::Accel3& out) {
    if (!begun_) {
      return false;
    }
    // ACCEL_DATA_X_LSB(0x08) から 6 バイト（X/Y/Z の LSB,MSB）。PAGE 0 前提（heading と同様）。
    constexpr uint8_t kAccelDataXLsbReg = 0x08;  // BNO055_ACCEL_DATA_X_LSB_ADDR
    Wire.beginTransmission(addr_);
    Wire.write(kAccelDataXLsbReg);
    if (Wire.endTransmission(false) != 0) {  // repeated start。0=成功
      return false;
    }
    if (Wire.requestFrom(addr_, static_cast<uint8_t>(6)) != 6) {
      while (Wire.available()) {
        Wire.read();
      }
      return false;
    }
    uint8_t b[6];
    for (uint8_t i = 0; i < 6; ++i) {
      int v = Wire.read();
      if (v < 0) {  // available 不足（requestFrom==6 なら来ないが防御的に）
        return false;
      }
      b[i] = static_cast<uint8_t>(v);
    }
    out.x = landing::accelFromRaw(b[0], b[1]);
    out.y = landing::accelFromRaw(b[2], b[3]);
    out.z = landing::accelFromRaw(b[4], b[5]);
    return true;
  }

  // キャリブレーション状態（各0-3）。未初期化なら全0を返す。
  // 注意: Adafruit の getCalibration は void で I2C 読み取り失敗を通知しない。読めなかった場合は
  //       前回値/0 が残るため、「全0(未校正)」と「読み取り失敗」を本APIでは区別できない。
  //       読み取り健全性は healthy()/systemStatus() で別途確認すること（gotchas B8）。
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

  // BNO055 のシステム状態（データシート §4.3.58/59）。読み取り健全性の判定に使う。
  //   status:   0=Idle/1=SystemError/2=InitPeripherals/3=SystemInit/4=SelfTest/
  //             5=Fusion稼働中/6=Fusion無しで稼働
  //   selfTest: 各ビット 1=合格（0x0F で全合格）
  //   error:    0=エラー無し（非0はデータシート §4.3.59 のエラーコード）
  struct SystemStatus {
    uint8_t status;
    uint8_t selfTest;
    uint8_t error;
  };

  // システム状態を読む。未初期化なら全0（=Idle相当）。
  // 注意: Adafruit の getSystemStatus は内部に delay(200) を持つ。方位ホットループには入れず、
  //       初期化確認や定期ヘルスチェックなど明示的な用途に使う。
  SystemStatus systemStatus() {
    uint8_t status = 0;
    uint8_t selftest = 0;
    uint8_t error = 0;
    if (begun_) {
      bno_.getSystemStatus(&status, &selftest, &error);
    }
    return SystemStatus{status, selftest, error};
  }

  // 融合アルゴリズムが正常稼働中か（system_status==5）。読み取り失敗/未準備/未初期化なら false。
  // heading() の値からは検知できないハード故障（活線抜け・ブラウンアウト）を別経路で捉えるための
  // 健全性プローブ（gotchas B8）。delay(200) を伴うため毎フレーム呼ばないこと。
  bool healthy() { return systemStatus().status == 5; }

 private:
  // Euler ヘディング(レジスタ 0x1A:LSB, 0x1B:MSB)を自前 Wire で読む。I2C
  // トランザクションが成功すれば true を返し lsb/msb を埋める。失敗（NACK/読み取り不能）は
  // false。Adafruit の private read を使わず
  // 最小限のレジスタ読みを自前実装し、成否を呼び出し側に返せるようにする（gotchas B11）。
  // 手順: レジスタポインタを書き込み（endTransmission(false)=repeated start, 0=成功）→ 2バイト要求
  //   （requestFrom は成功時 2・失敗時 0 を返す, Wire.cpp）。
  bool readEulerHeadingRaw(uint8_t& lsb, uint8_t& msb) {
    // 前提: デバイスは PAGE 0（0x1A は PAGE 0 のレジスタ）。Adafruit の begin()/config 操作が
    // PAGE 0 を担保する。将来 PAGE 1（config レジスタ）を触る機能を足すなら、戻し忘れると本読みが
    // 別レジスタを int16 解釈して「もっともらしい誤方位」を無効値にせず返すため、PAGE
    // 戻しを保証すること。
    constexpr uint8_t kEulerHeadingLsbReg = 0x1A;  // BNO055_EULER_H_LSB_ADDR
    Wire.beginTransmission(addr_);
    Wire.write(kEulerHeadingLsbReg);
    if (Wire.endTransmission(false) != 0) {  // 0=TWI_SUCCESS。NACK 等は非0
      return false;
    }
    if (Wire.requestFrom(addr_, static_cast<uint8_t>(2)) != 2) {
      // Spresense Wire は失敗時 rx バッファを再投入せず available()==0 のため実質 no-op だが、
      // 他コア（標準 AVR Wire 等）移植時に端数を持ち越さない保険として残す。
      while (Wire.available()) {
        Wire.read();
      }
      return false;
    }
    int lo = Wire.read();
    int hi = Wire.read();
    if (lo < 0 || hi < 0) {  // available 不足（requestFrom==2 なら来ないが防御的に）
      return false;
    }
    lsb = static_cast<uint8_t>(lo);
    msb = static_cast<uint8_t>(hi);
    return true;
  }

  // 読み取りリトライ回数と間隔。一過性の瞬断（振動/EMI）を吸収しつつ、ホットループを長く
  // ブロックしない範囲（最悪 (kReadAttempts-1)*kRetryDelayMs ≒ 10ms）に抑える。
  static constexpr uint8_t kReadAttempts = 3;
  static constexpr uint8_t kRetryDelayMs = 5;

  Adafruit_BNO055 bno_;
  uint8_t addr_;  // bno_ 内部アドレスと同一値（自前 Wire 読み readEulerHeadingRaw 用に保持）
  bool begun_ = false;
};

}  // namespace hal

#endif  // HAL_BNO055_H
