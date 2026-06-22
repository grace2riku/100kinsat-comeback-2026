#ifndef HAL_SPRESENSE_PINS_H
#define HAL_SPRESENSE_PINS_H

#include "motor.h"

// spresense_pins.h - 100kinSAT 基板のピン割当（単一情報源）
//
// hardware.md「ピンアサイン一覧」に基づくモータの結線定数。実機スケッチ（src/shell 等）と
// ホストテストの双方がこのヘッダを参照し、ピン番号の二重管理（gotchas C）を防ぐ。
// Arduino.h には依存しないため、ホストテストからも include できる。
//
// 左右の割当（A=左 / B=右）は暫定。物理的な前進/旋回方向は実機で確認し、
// 必要なら入れ替える（gotchas A、motor.h の注意参照）。

namespace hal {

// モータA: AIN1=D08, AIN2=D04, PWMA=D05（左モータに割当）
constexpr motor::Pins kMotorLeft{8, 4, 5};

// モータB: BIN1=D07, BIN2=D02, PWMB=D03（右モータに割当）
constexpr motor::Pins kMotorRight{7, 2, 3};

}  // namespace hal

#endif  // HAL_SPRESENSE_PINS_H
