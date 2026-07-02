#include "landing.h"

#include <cmath>

namespace landing {

const double kStandardGravity = 9.80665;

double Accel3::magnitude() const { return vectorMagnitude(x, y, z); }

StillConfig defaultStillConfig() {
  // 暫定値（実機 bring-up で調整。gotchas A）。
  //   gravity/gTolerance: 据置時 |a| は g 近傍。据置ノイズ・傾き分を ±1.5 m/s^2 で見込む。
  //   maxVariance: 据置の微振動を許容しつつ、パラ揺動・転動は弾く上限（暫定 0.5 (m/s^2)^2）。
  //   windowSize/requiredStillMs: 100Hz 読みで 0.2s 窓・1.0s 継続を想定（周期は呼び出し側依存）。
  StillConfig c;
  c.gravity = kStandardGravity;
  c.gTolerance = 1.5;
  c.maxVariance = 0.5;
  c.windowSize = 20;
  c.requiredStillMs = 1000.0;
  return c;
}

double vectorMagnitude(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }

double mean(const double* v, uint8_t n) {
  if (n == 0) {
    return 0.0;
  }
  double sum = 0.0;
  for (uint8_t i = 0; i < n; ++i) {
    sum += v[i];
  }
  return sum / n;
}

double variance(const double* v, uint8_t n) {
  if (n == 0) {
    return 0.0;
  }
  const double m = mean(v, n);
  double sq = 0.0;
  for (uint8_t i = 0; i < n; ++i) {
    const double d = v[i] - m;
    sq += d * d;
  }
  return sq / n;  // 母分散（非負）
}

double accelFromRaw(uint8_t lsb, uint8_t msb) {
  // リトルエンディアンで 16bit を組み、符号付き int16 として解釈する。
  // uint16 で合成してから int16_t へキャストし、実装依存の符号拡張を避ける（gotchas A）。
  const uint16_t raw = static_cast<uint16_t>(static_cast<uint16_t>(msb) << 8 | lsb);
  const int16_t signedRaw = static_cast<int16_t>(raw);
  return signedRaw / 100.0;  // 1 m/s^2 = 100 LSB
}

namespace {
// windowSize を [2, kMaxWindow] にクランプする（動的確保しないための上限保証）。
uint8_t clampWindow(uint8_t w) {
  if (w < 2) {
    return 2;
  }
  if (w > kMaxWindow) {
    return kMaxWindow;
  }
  return w;
}
}  // namespace

LandingDetector::LandingDetector(const StillConfig& cfg) : cfg_(cfg) {
  cfg_.windowSize = clampWindow(cfg.windowSize);
  reset();
}

void LandingDetector::reset() {
  count_ = 0;
  head_ = 0;
  stillElapsedMs_ = 0.0;
  wasStill_ = false;
  landed_ = false;
}

void LandingDetector::update(const Accel3& accel, double dtMs) {
  // 1) |a| をリングバッファへ投入（最新 windowSize 個を保持）。
  buf_[head_] = accel.magnitude();
  head_ = static_cast<uint8_t>((head_ + 1) % cfg_.windowSize);
  if (count_ < cfg_.windowSize) {
    ++count_;
  }

  // 2) 静止条件の連続時間を積算する。最初の静止サンプルは起点（0）とし、以降の
  //    静止サンプルで dt を積む（条件が崩れたら 0 に戻す）。dtMs<=0 は時間を進めない。
  if (isStillNow()) {
    if (wasStill_ && dtMs > 0.0) {
      stillElapsedMs_ += dtMs;
    }
    wasStill_ = true;
  } else {
    stillElapsedMs_ = 0.0;
    wasStill_ = false;
  }

  // 3) 継続時間が閾値に達したら着地確定（ラッチ）。一度立ったら reset() まで保持。
  if (stillElapsedMs_ >= cfg_.requiredStillMs) {
    landed_ = true;
  }
}

bool LandingDetector::windowFull() const { return count_ == cfg_.windowSize; }

double LandingDetector::meanMagnitude() const { return mean(buf_, count_); }

double LandingDetector::variance() const { return landing::variance(buf_, count_); }

bool LandingDetector::isStillNow() const {
  if (!windowFull()) {
    return false;  // 判定は窓が満ちてから
  }
  const bool lowVariance = landing::variance(buf_, count_) <= cfg_.maxVariance;
  const bool nearGravity = std::fabs(meanMagnitude() - cfg_.gravity) <= cfg_.gTolerance;
  return lowVariance && nearGravity;
}

double LandingDetector::stillElapsedMs() const { return stillElapsedMs_; }

bool LandingDetector::hasLanded() const { return landed_; }

}  // namespace landing
