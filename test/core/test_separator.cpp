// separator（パラシュート切り離し電熱線 D06）の単体テスト
//
// hal::Gpio をモック(MockGpio)に差し替え、加熱シーケンスが「正しいピンへ正しい値」を出力し、
// 安全要件（最大加熱時間の上限ガード・多重起動防止・安全初期化・暴走ガード）と再試行ポリシーを
// 満たすことを検証する（Issue #13 DoD / hardware.md D06 / software.md §5.8）。
// 境界値（heatMs 直前/丁度）と異常系（begin 前 start・巨大 dt・設定クランプ）も網羅する。

#include <cstdint>
#include <map>
#include <vector>

#include "doctest.h"
#include "gpio.h"
#include "separator.h"
#include "spresense_pins.h"  // 実機と同じ電熱線ピン定数（単一情報源）

namespace {

// Gpio のモック。pin ごとの最新 digital 書き込みと出力設定済みピン、書き込み回数を記録する。
class MockGpio : public hal::Gpio {
 public:
  void setOutput(uint8_t pin) override { outputs_.push_back(pin); }
  void writeDigital(uint8_t pin, hal::Level level) override {
    digital_[pin] = (level == hal::Level::High) ? 1 : 0;
    ++writeCount_[pin];
  }
  void writePwm(uint8_t pin, uint8_t duty) override { pwm_[pin] = duty; }

  bool isOutput(uint8_t pin) const {
    for (uint8_t p : outputs_) {
      if (p == pin) {
        return true;
      }
    }
    return false;
  }
  int digital(uint8_t pin) const {
    auto it = digital_.find(pin);
    return (it == digital_.end()) ? -1 : it->second;
  }
  int writeCount(uint8_t pin) const {
    auto it = writeCount_.find(pin);
    return (it == writeCount_.end()) ? 0 : it->second;
  }

 private:
  std::vector<uint8_t> outputs_;
  std::map<uint8_t, int> digital_;
  std::map<uint8_t, int> writeCount_;
  std::map<uint8_t, int> pwm_;
};

constexpr uint8_t kPin = hal::kHeatingWirePin;  // 実機と同じ D06（単一情報源）

// 加熱中(HIGH)か。
bool isHigh(const MockGpio& g) { return g.digital(kPin) == 1; }
// 非加熱(LOW)か。
bool isLow(const MockGpio& g) { return g.digital(kPin) == 0; }

}  // namespace

TEST_CASE("begin: D06 を出力に設定し LOW（安全初期化）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  CHECK(g.isOutput(kPin));
  CHECK(isLow(g));  // 起動直後に加熱していない
  CHECK(sep.state() == separator::ParachuteSeparator::State::Idle);
  CHECK_FALSE(sep.hasFired());
}

TEST_CASE("begin 前の start は拒否（副作用なし）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  CHECK_FALSE(sep.start());
  CHECK_FALSE(sep.isHeating());
  CHECK(sep.attemptsUsed() == 0);
  CHECK(g.writeCount(kPin) == 0);  // 一切書いていない
}

TEST_CASE("start: D06 を HIGH にして加熱開始") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  CHECK(sep.start());
  CHECK(isHigh(g));
  CHECK(sep.isHeating());
  CHECK(sep.attemptsUsed() == 1);
  CHECK(sep.hasFired());
}

TEST_CASE("多重起動防止: 加熱中の start は拒否し HIGH を維持") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 2});  // 試行余地があっても加熱中は不可
  sep.begin();
  REQUIRE(sep.start());
  int writesAfterStart = g.writeCount(kPin);
  CHECK_FALSE(sep.start());                       // 加熱中の再 start は拒否
  CHECK(isHigh(g));                               // HIGH のまま
  CHECK(sep.attemptsUsed() == 1);                 // 回数は増えない
  CHECK(g.writeCount(kPin) == writesAfterStart);  // 余計な書き込みもしない
}

TEST_CASE("上限ガード: heatMs 到達で自動 LOW（過加熱防止）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  REQUIRE(sep.start());
  sep.update(500.0);
  CHECK(isHigh(g));  // 途中はまだ加熱
  CHECK(sep.isHeating());
  sep.update(500.0);  // 累積 1000ms == heatMs
  CHECK(isLow(g));    // 自動停止
  CHECK_FALSE(sep.isHeating());
  CHECK(sep.heatElapsedMs() >= 1000.0);
}

TEST_CASE("境界: heatMs 直前は加熱継続・丁度で停止") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  REQUIRE(sep.start());
  sep.update(999.0);
  CHECK(isHigh(g));  // 直前
  CHECK(sep.isHeating());
  sep.update(1.0);  // 丁度 1000ms
  CHECK(isLow(g));
  CHECK_FALSE(sep.isHeating());
}

TEST_CASE("暴走ガード: 巨大 dt でも即停止し過加熱しない") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  REQUIRE(sep.start());
  sep.update(1.0e9);  // 1 サンプルで莫大な経過を与える
  CHECK(isLow(g));
  CHECK_FALSE(sep.isHeating());
}

TEST_CASE("設定クランプ: heatMs はハード上限 kMaxHeatMsCap を超えない") {
  MockGpio g;
  // 桁間違いのような巨大 heatMs を与えても、実効値は kMaxHeatMsCap にクランプされる。
  separator::ParachuteSeparator sep(g, kPin, {1.0e9, 1});
  CHECK(sep.config().heatMs == doctest::Approx(separator::kMaxHeatMsCap));
  sep.begin();
  REQUIRE(sep.start());
  sep.update(separator::kMaxHeatMsCap - 1.0);
  CHECK(isHigh(g));  // 上限直前はまだ加熱
  sep.update(1.0);   // 上限到達
  CHECK(isLow(g));   // 上限で必ず止まる
}

TEST_CASE("設定クランプ: 不正な heatMs/maxAttempts を安全値へ寄せる") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {-5.0, 0});
  CHECK(sep.config().heatMs >= 1.0);  // 0/負は最小 1ms 以上へ
  CHECK(sep.config().heatMs <= separator::kMaxHeatMsCap);
  CHECK(sep.config().maxAttempts >= 1);  // 0 回は 1 回以上へ（分離は必ず 1 回は撃てる）
}

TEST_CASE("dt<=0 は経過なし（加熱継続）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  REQUIRE(sep.start());
  sep.update(0.0);
  sep.update(-100.0);
  CHECK(isHigh(g));
  CHECK(sep.isHeating());
  CHECK(sep.heatElapsedMs() == doctest::Approx(0.0));
}

TEST_CASE("単発（既定 maxAttempts=1）: 完了後の start は拒否・Done") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  REQUIRE(sep.start());
  sep.update(1000.0);
  CHECK(sep.isDone());
  CHECK_FALSE(sep.start());  // 再加熱不可
  CHECK(isLow(g));
}

TEST_CASE("再試行ポリシー: maxAttempts=2 は完了後にもう 1 回だけ撃てる") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 2});
  sep.begin();
  // 1 回目
  REQUIRE(sep.start());
  sep.update(1000.0);
  CHECK_FALSE(sep.isDone());  // まだ試行が残る
  CHECK(isLow(g));
  // 2 回目
  REQUIRE(sep.start());
  CHECK(isHigh(g));
  CHECK(sep.attemptsUsed() == 2);
  sep.update(1000.0);
  CHECK(sep.isDone());  // 使い切り
  CHECK(isLow(g));
  // 3 回目は拒否
  CHECK_FALSE(sep.start());
}

TEST_CASE("緊急停止 abort: いつでも LOW にし端末 Done（以後 start 不可）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 2});  // 試行が残っていても
  sep.begin();
  REQUIRE(sep.start());
  sep.update(300.0);
  sep.abort();
  CHECK(isLow(g));
  CHECK(sep.isDone());
  CHECK_FALSE(sep.isHeating());
  CHECK_FALSE(sep.start());  // abort は端末（誤動作時の安全側）
}

TEST_CASE("Idle/Done での update は無操作（誤加熱しない）") {
  MockGpio g;
  separator::ParachuteSeparator sep(g, kPin, {1000.0, 1});
  sep.begin();
  int writesAfterBegin = g.writeCount(kPin);
  sep.update(5000.0);  // Idle のまま時間だけ与える
  CHECK(isLow(g));
  CHECK_FALSE(sep.isHeating());
  CHECK(g.writeCount(kPin) == writesAfterBegin);  // 書き込みが増えない
}
