#ifndef CORE_GPIO_H
#define CORE_GPIO_H

#include <cstdint>

// gpio - GPIO 抽象インタフェース（ハードウェア非依存）
//
// アクチュエータ系（モータ・電熱線・スピーカ等）が「ピンを出力に設定し、
// digital/PWM を書き込む」ために依存する薄い抽象。Arduino API には依存しないため、
// HW非依存ロジック（src/lib/core）をホストPCでユニットテストできる。
//
// 実機実装は src/lib/hal（pinMode/digitalWrite/analogWrite を呼ぶ ArduinoGpio）、
// ホストテストではモックに差し替える（test/core/test_motor.cpp の MockGpio）。

namespace hal {

// デジタル出力レベル。Arduino の HIGH/LOW マクロには依存しない独自定義。
enum class Level : uint8_t { Low = 0, High = 1 };

class Gpio {
 public:
  virtual ~Gpio() = default;

  // pin を出力モードに設定する（Arduino: pinMode(pin, OUTPUT)）。
  virtual void setOutput(uint8_t pin) = 0;

  // pin に HIGH/LOW を書き込む（Arduino: digitalWrite）。
  virtual void writeDigital(uint8_t pin, Level level) = 0;

  // pin に PWM デューティ 0-255 を書き込む（Arduino: analogWrite）。
  virtual void writePwm(uint8_t pin, uint8_t duty) = 0;
};

}  // namespace hal

#endif  // CORE_GPIO_H
