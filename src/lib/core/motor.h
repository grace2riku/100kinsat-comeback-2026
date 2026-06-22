#ifndef CORE_MOTOR_H
#define CORE_MOTOR_H

#include <cstdint>

#include "gpio.h"

// motor - 2輪差動駆動のモータ制御（TB6612FNG, 2ch）。ハードウェア非依存。
//
// hardware.md「TB6612FNG 入力 → 出力対応」の真理値表に従い、左右モータへ
// IN1/IN2/PWM を出力する純粋ロジック。実際のピン書き込みは hal::Gpio に委譲するため、
// ホストPCでユニットテストできる（test/core/test_motor.cpp）。
//
// 真理値表（1モータあたり IN1/IN2/PWM）:
//   前進: IN1=High, IN2=Low,  PWM=duty
//   後進: IN1=Low,  IN2=High, PWM=duty
//   停止: IN1=Low,  IN2=Low,  PWM=0
//
// 注意（gotchas A: ターゲット≠ホスト）:
//   ここで検証できるのは「電気的にこの真理値表どおり出力されること」まで。
//   物理的な前進/旋回の向きは、左右モータの取付・配線に依存するため、実機で確認し、
//   必要なら left/right の割当（spresense_pins.h）や配線で調整する。

namespace motor {

// 1モータの結線（IN1/IN2/PWM のピン番号）。
struct Pins {
  uint8_t in1;
  uint8_t in2;
  uint8_t pwm;
};

class MotorDriver {
 public:
  // gpio: 出力先（実機=ArduinoGpio / テスト=MockGpio）。
  // left/right: 左右モータのピン割当。
  MotorDriver(hal::Gpio& gpio, const Pins& left, const Pins& right);

  // 左右計6ピンを出力モードに設定する。setup() で一度呼ぶ。
  void begin();

  // duty は PWM デューティ 0-255（uint8_t により範囲は型で保証される）。
  // 注: forward(0) 等は「方向ピンを設定したまま PWM=0」＝駆動なし。stop() は
  //     IN1=IN2=Low+PWM=0。実機ではこの2つの停止挙動（コースト/ブレーキ）の差や、
  //     最低駆動デューティ（デッドゾーン）を計測して確認する（gotchas A）。
  void forward(uint8_t duty);    // 左右とも前進
  void back(uint8_t duty);       // 左右とも後進
  void turnLeft(uint8_t duty);   // 左モータ後進・右モータ前進（左ピボット旋回）
  void turnRight(uint8_t duty);  // 左モータ前進・右モータ後進（右ピボット旋回）
  void stop();                   // 左右とも停止（PWM=0）

 private:
  enum class Spin { Forward, Backward, Stop };
  void drive(const Pins& m, Spin spin, uint8_t duty);

  hal::Gpio& gpio_;
  Pins left_;
  Pins right_;
};

}  // namespace motor

#endif  // CORE_MOTOR_H
