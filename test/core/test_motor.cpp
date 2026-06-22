// motor（TB6612FNG 2ch モータ制御）の単体テスト
//
// hal::Gpio をモック(MockGpio)に差し替え、各コマンドが TB6612FNG の真理値表どおりに
// 「正しいピンへ正しい値」を出力することを検証する（hardware.md §TB6612FNG）。
// 境界値（duty=0/255）と異常系（stop の PWM 強制0）も網羅する。

#include <cstdint>
#include <map>
#include <vector>

#include "doctest.h"
#include "gpio.h"
#include "motor.h"
#include "spresense_pins.h"  // 実機と同じピン定数（単一情報源）

namespace {

// Gpio のモック。pin ごとの最新の digital/PWM 書き込みと、出力設定済みピンを記録する。
class MockGpio : public hal::Gpio {
 public:
  void setOutput(uint8_t pin) override { outputs_.push_back(pin); }
  void writeDigital(uint8_t pin, hal::Level level) override {
    digital_[pin] = (level == hal::Level::High) ? 1 : 0;
  }
  void writePwm(uint8_t pin, uint8_t duty) override { pwm_[pin] = duty; }

  // pin が出力モードに設定されたか。
  bool isOutput(uint8_t pin) const {
    for (uint8_t p : outputs_) {
      if (p == pin) {
        return true;
      }
    }
    return false;
  }
  // pin への最新 digital 書き込み（未書き込みは -1）。
  int digital(uint8_t pin) const {
    auto it = digital_.find(pin);
    return (it == digital_.end()) ? -1 : it->second;
  }
  // pin への最新 PWM 書き込み（未書き込みは -1）。
  int pwm(uint8_t pin) const {
    auto it = pwm_.find(pin);
    return (it == pwm_.end()) ? -1 : it->second;
  }

 private:
  std::vector<uint8_t> outputs_;
  std::map<uint8_t, int> digital_;
  std::map<uint8_t, int> pwm_;
};

// テスト用のピン割当（実機定数を流用）。値は spresense_pins.h が単一情報源
// （実機検証で左右を入替済み: 左=モータB{7,2,3} / 右=モータA{8,4,5}）。
// 本テストは「割当てられたピンに真理値表どおり出力する」ことの検証なので、
// 左右の値に依らず成立する。
constexpr motor::Pins kL = hal::kMotorLeft;
constexpr motor::Pins kR = hal::kMotorRight;

// 1モータが「前進(High/Low + duty)」になっているか。
void expectForward(const MockGpio& g, const motor::Pins& m, int duty) {
  CHECK(g.digital(m.in1) == 1);
  CHECK(g.digital(m.in2) == 0);
  CHECK(g.pwm(m.pwm) == duty);
}
// 1モータが「後進(Low/High + duty)」になっているか。
void expectBackward(const MockGpio& g, const motor::Pins& m, int duty) {
  CHECK(g.digital(m.in1) == 0);
  CHECK(g.digital(m.in2) == 1);
  CHECK(g.pwm(m.pwm) == duty);
}
// 1モータが「停止(Low/Low + PWM0)」になっているか。
void expectStopped(const MockGpio& g, const motor::Pins& m) {
  CHECK(g.digital(m.in1) == 0);
  CHECK(g.digital(m.in2) == 0);
  CHECK(g.pwm(m.pwm) == 0);
}

}  // namespace

TEST_CASE("begin: 左右計6ピンを出力モードに設定する") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.begin();
  for (uint8_t pin : {kL.in1, kL.in2, kL.pwm, kR.in1, kR.in2, kR.pwm}) {
    CHECK(g.isOutput(pin));
  }
}

TEST_CASE("forward: 左右とも前進（IN1=High, IN2=Low, PWM=duty）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.forward(100);
  expectForward(g, kL, 100);
  expectForward(g, kR, 100);
}

TEST_CASE("back: 左右とも後進（IN1=Low, IN2=High, PWM=duty）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.back(120);
  expectBackward(g, kL, 120);
  expectBackward(g, kR, 120);
}

TEST_CASE("turnRight: 左前進・右後進（右ピボット）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.turnRight(80);
  expectForward(g, kL, 80);
  expectBackward(g, kR, 80);
}

TEST_CASE("turnLeft: 左後進・右前進（左ピボット）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.turnLeft(80);
  expectBackward(g, kL, 80);
  expectForward(g, kR, 80);
}

TEST_CASE("stop: 左右とも停止（IN1=Low, IN2=Low, PWM=0）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.stop();
  expectStopped(g, kL);
  expectStopped(g, kR);
}

TEST_CASE("境界値: duty=0 は方向ピンを設定しつつ PWM=0（駆動なし）") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.forward(0);
  CHECK(g.digital(kL.in1) == 1);
  CHECK(g.digital(kL.in2) == 0);
  CHECK(g.pwm(kL.pwm) == 0);
}

TEST_CASE("境界値: duty=255 は PWM=255") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.forward(255);
  CHECK(g.pwm(kL.pwm) == 255);
  CHECK(g.pwm(kR.pwm) == 255);
}

TEST_CASE("異常系: 全力前進の直後でも stop は PWM を 0 に強制する") {
  MockGpio g;
  motor::MotorDriver d(g, kL, kR);
  d.forward(255);
  d.stop();
  expectStopped(g, kL);
  expectStopped(g, kR);
}
