#ifndef HAL_SPRESENSE_PINS_H
#define HAL_SPRESENSE_PINS_H

#include "motor.h"

// spresense_pins.h - 100kinSAT 基板のピン割当（単一情報源）
//
// hardware.md「ピンアサイン一覧」に基づくモータの結線定数。実機スケッチ（src/shell 等）と
// ホストテストの双方がこのヘッダを参照し、ピン番号の二重管理（gotchas C）を防ぐ。
// Arduino.h には依存しないため、ホストテストからも include できる。
//
// 左右の割当は実機検証（doc/development/motor_bringup.md）で確定済み。
// 当初 A=左/B=右 で組んだところ旋回が左右反転したため、左右割当を入れ替えた
// （前進/後進は左右対称のため影響なし、旋回が正方向になる。gotchas A）。

namespace hal {

// 左モータ = モータB結線: BIN1=D07, BIN2=D02, PWMB=D03
constexpr motor::Pins kMotorLeft{7, 2, 3};

// 右モータ = モータA結線: AIN1=D08, AIN2=D04, PWMA=D05
constexpr motor::Pins kMotorRight{8, 4, 5};

// 電熱線（パラシュート切り離し）駆動ピン: D06 → FET ゲート（hardware.md「電熱線駆動 D06」）。
// HIGH で加熱、LOW で停止。安全のため起動直後は必ず LOW（separator::ParachuteSeparator::begin）。
constexpr uint8_t kHeatingWirePin = 6;

// スピーカ（UGCM0603APE）駆動ピン: D09 (PWM)。tone()/noTone() で矩形波を出す
// （hardware.md「スピーカ D09」/ software.md §5.3。状態通知 core/notifier の出力先）。
constexpr uint8_t kSpeakerPin = 9;

}  // namespace hal

#endif  // HAL_SPRESENSE_PINS_H
