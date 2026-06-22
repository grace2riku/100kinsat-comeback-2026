#ifndef HAL_ARDUINO_GPIO_H
#define HAL_ARDUINO_GPIO_H

#include <Arduino.h>

#include "gpio.h"

// arduino_gpio.h - hal::Gpio の Spresense/Arduino 実機実装
//
// pinMode/digitalWrite/analogWrite をそのまま呼ぶ薄いアダプタ。Arduino.h に依存するため
// 実機ビルド（arduino-cli）でのみ使用する。ホストテストでは使わず MockGpio を用いる。

namespace hal {

class ArduinoGpio : public Gpio {
 public:
  void setOutput(uint8_t pin) override { pinMode(pin, OUTPUT); }

  void writeDigital(uint8_t pin, Level level) override {
    digitalWrite(pin, level == Level::High ? HIGH : LOW);
  }

  void writePwm(uint8_t pin, uint8_t duty) override { analogWrite(pin, duty); }
};

}  // namespace hal

#endif  // HAL_ARDUINO_GPIO_H
