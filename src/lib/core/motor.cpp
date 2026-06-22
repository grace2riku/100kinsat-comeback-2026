#include "motor.h"

namespace motor {

MotorDriver::MotorDriver(hal::Gpio& gpio, const Pins& left, const Pins& right)
    : gpio_(gpio), left_(left), right_(right) {}

void MotorDriver::begin() {
  const Pins* motors[] = {&left_, &right_};
  for (const Pins* m : motors) {
    gpio_.setOutput(m->in1);
    gpio_.setOutput(m->in2);
    gpio_.setOutput(m->pwm);
  }
}

// 1モータを真理値表（hardware.md §TB6612FNG）どおりに駆動する。
void MotorDriver::drive(const Pins& m, Spin spin, uint8_t duty) {
  switch (spin) {
    case Spin::Forward:
      gpio_.writeDigital(m.in1, hal::Level::High);
      gpio_.writeDigital(m.in2, hal::Level::Low);
      gpio_.writePwm(m.pwm, duty);
      break;
    case Spin::Backward:
      gpio_.writeDigital(m.in1, hal::Level::Low);
      gpio_.writeDigital(m.in2, hal::Level::High);
      gpio_.writePwm(m.pwm, duty);
      break;
    case Spin::Stop:
      gpio_.writeDigital(m.in1, hal::Level::Low);
      gpio_.writeDigital(m.in2, hal::Level::Low);
      gpio_.writePwm(m.pwm, 0);
      break;
  }
}

void MotorDriver::forward(uint8_t duty) {
  drive(left_, Spin::Forward, duty);
  drive(right_, Spin::Forward, duty);
}

void MotorDriver::back(uint8_t duty) {
  drive(left_, Spin::Backward, duty);
  drive(right_, Spin::Backward, duty);
}

void MotorDriver::turnLeft(uint8_t duty) {
  drive(left_, Spin::Backward, duty);
  drive(right_, Spin::Forward, duty);
}

void MotorDriver::turnRight(uint8_t duty) {
  drive(left_, Spin::Forward, duty);
  drive(right_, Spin::Backward, duty);
}

void MotorDriver::stop() {
  drive(left_, Spin::Stop, 0);
  drive(right_, Spin::Stop, 0);
}

}  // namespace motor
