#include "separator.h"

namespace separator {

namespace {

// heatMs を [minMs, kMaxHeatMsCap] に収める。0/負/NaN 相当は最小値へ寄せる（最小加熱は無害）。
double clampHeatMs(double heatMs) {
  constexpr double kMinHeatMs = 1.0;
  if (!(heatMs >= kMinHeatMs)) {  // NaN・0・負は false になり最小へ（安全側）
    return kMinHeatMs;
  }
  if (heatMs > kMaxHeatMsCap) {
    return kMaxHeatMsCap;
  }
  return heatMs;
}

// maxAttempts を [1, 255] に収める。0 は 1 へ（分離は必ず 1 回は撃てるように）。
uint8_t clampAttempts(uint8_t maxAttempts) { return maxAttempts < 1 ? 1 : maxAttempts; }

}  // namespace

SeparatorConfig defaultSeparatorConfig() {
  // 暫定値: 3s 加熱・単発。溶断できる時間は実機（separator_bringup.md）で詰める。
  return SeparatorConfig{3000.0, 1};
}

SeparatorConfig sanitizeConfig(const SeparatorConfig& cfg) {
  return SeparatorConfig{clampHeatMs(cfg.heatMs), clampAttempts(cfg.maxAttempts)};
}

ParachuteSeparator::ParachuteSeparator(hal::Gpio& gpio, uint8_t pin, const SeparatorConfig& cfg)
    : gpio_(gpio),
      pin_(pin),
      cfg_(sanitizeConfig(cfg)),
      state_(State::Idle),
      heatElapsedMs_(0.0),
      attemptsUsed_(0),
      begun_(false) {}

ParachuteSeparator::~ParachuteSeparator() {
  if (begun_) {
    energize(false);  // RAII 安全ネット: 破棄時に必ず非加熱へ（abort/update 撃ち漏らしの保険）
  }
}

void ParachuteSeparator::energize(bool on) {
  gpio_.writeDigital(pin_, on ? hal::Level::High : hal::Level::Low);
}

void ParachuteSeparator::begin() {
  gpio_.setOutput(pin_);
  energize(false);  // 起動直後は必ず非加熱（誤加熱防止）
  state_ = State::Idle;
  heatElapsedMs_ = 0.0;
  begun_ = true;
}

bool ParachuteSeparator::start() {
  // 未 begin / 加熱中（多重起動防止）/ 試行を使い切り（Done）は拒否。副作用なし。
  if (!begun_ || state_ == State::Heating || state_ == State::Done) {
    return false;
  }
  if (attemptsUsed_ >= cfg_.maxAttempts) {
    return false;
  }
  ++attemptsUsed_;
  heatElapsedMs_ = 0.0;
  state_ = State::Heating;
  energize(true);
  return true;
}

void ParachuteSeparator::update(double dtMs) {
  if (state_ != State::Heating) {
    return;  // Idle/Done は何もしない（誤加熱しない）
  }
  if (dtMs > 0.0) {
    heatElapsedMs_ += dtMs;
  }
  if (heatElapsedMs_ >= cfg_.heatMs) {
    energize(false);  // 上限ガード: 加熱を止める
    // 残り試行があれば Idle（再試行可）、無ければ Done（端末）。
    state_ = (attemptsUsed_ < cfg_.maxAttempts) ? State::Idle : State::Done;
  }
}

void ParachuteSeparator::abort() {
  energize(false);       // いつでも安全側（LOW）
  state_ = State::Done;  // 緊急停止は端末（以後 start 不可）
}

}  // namespace separator
