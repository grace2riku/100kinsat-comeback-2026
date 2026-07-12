#include "release_detect.h"

#include <cmath>

namespace release_detect {

ReleaseConfig defaultReleaseConfig() {
  // 暫定値（実機 bring-up で調整。gotchas A）。
  //   threshold: 装置内(暗)と上空(明)の中点想定。実機で両者の生値を測り中点へ寄せる。
  //   hysteresis: 閾値付近の微振動でON/OFFがばたつくのを吸収する幅（暫定 50）。
  //   polarity: 既定は Brighter（放出後は明るい＝生値が小さい）。分圧の向きは実機で確定する。
  //   requiredMs: 放出側の明るさが 0.5s 連続で確定。閃光/ノイズの単発を弾く（実機で調整）。
  ReleaseConfig c;
  c.threshold = 512;
  c.hysteresis = 50;
  c.polarity = Polarity::Brighter;
  c.requiredMs = 500.0;
  return c;
}

namespace {
// v を [lo, hi] にクランプする。
int clampInt(int v, int lo, int hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}
}  // namespace

ReleaseConfig sanitizeConfig(const ReleaseConfig& cfg) {
  ReleaseConfig c = cfg;
  c.threshold = clampInt(c.threshold, 0, kAdcMax);
  c.hysteresis = clampInt(c.hysteresis, 0, kAdcMax);
  // 実効 OFF(復帰) しきい値（threshold ± hysteresis）を ADC レンジ内へ収める。範囲外だと復帰条件が
  // 到達不能になり「一度 ON→復帰不能」でヒステリシス帯が無限化し単発ノイズを誤ラッチする（gotchas
  // B19）。
  //   Brighter の復帰は raw>=threshold+hysteresis → threshold+hysteresis<=kAdcMax
  //   Darker   の復帰は raw<=threshold-hysteresis → threshold-hysteresis>=0
  if (c.polarity == Polarity::Brighter) {
    if (c.hysteresis > kAdcMax - c.threshold) {
      c.hysteresis = kAdcMax - c.threshold;
    }
  } else {
    if (c.hysteresis > c.threshold) {
      c.hysteresis = c.threshold;
    }
  }
  // requiredMs は負値・NaN を 0 に。!(x>=0) は NaN でも真になり弾ける（gotchas: 非有限入力）。
  if (!(c.requiredMs >= 0.0)) {
    c.requiredMs = 0.0;
  }
  return c;
}

bool isReleasedLevel(int raw, int threshold, Polarity polarity) {
  // 放出側の素の閾値判定（ヒステリシスなし）。境界は放出側に含める（<= / >=）。
  if (polarity == Polarity::Brighter) {
    return raw <= threshold;  // 明るい＝生値が小さい
  }
  return raw >= threshold;  // Darker: 暗い＝生値が大きい
}

ReleaseDetector::ReleaseDetector(const ReleaseConfig& cfg) : cfg_(sanitizeConfig(cfg)) { reset(); }

void ReleaseDetector::reset() {
  lastRaw_ = -1;
  conditionOn_ = false;
  elapsedMs_ = 0.0;
  wasOn_ = false;
  armed_ = false;
  released_ = false;
}

bool ReleaseDetector::isOffLevel(int raw) const {
  // 極性ごとの OFF(復帰) しきい値を跨いだか。sanitizeConfig で復帰しきい値はレンジ内に収めている。
  if (cfg_.polarity == Polarity::Brighter) {
    return raw >= cfg_.threshold + cfg_.hysteresis;  // 明側放出→暗く戻る
  }
  return raw <= cfg_.threshold - cfg_.hysteresis;  // 暗側放出→明るく戻る
}

void ReleaseDetector::update(int raw, double dtMs) {
  // 1) 生値を [0, kAdcMax] にクランプして保持（範囲外の異常入力を正規化）。
  lastRaw_ = clampInt(raw, 0, kAdcMax);

  // 2) ヒステリシス込みの放出側条件を更新する。ON エッジは isReleasedLevel（純粋関数・単一出典）、
  //    OFF(復帰)
  //    はヒステリシス分手前の別しきい値で判定し、閾値ちょうど付近の微振動でばたつかせない。
  //    どちらのしきい値も跨がない帯内は前状態を維持する（据え置き）。
  if (isReleasedLevel(lastRaw_, cfg_.threshold, cfg_.polarity)) {
    conditionOn_ = true;
  } else if (isOffLevel(lastRaw_)) {
    conditionOn_ = false;
  }

  // 3) アーミング: 反対状態(暗=OFF) を一度観測したら arm
  // する。暗→明の「遷移」を要求する安全機構で、
  //    電源投入時に既に明側／断線で明側へ張り付いた場合の不可逆な誤ラッチを弾く（暗を経ずには確定しない）。
  if (!conditionOn_) {
    armed_ = true;
  }

  // 4) 放出側条件の連続時間を積算する。最初の ON サンプルは起点(0)とし、以降の ON サンプルで
  //    dt を積む（条件が崩れたら 0 に戻す）。dtMs<=0 は時間を進めない（landing と同じ規約）。
  if (conditionOn_) {
    if (wasOn_ && dtMs > 0.0) {
      elapsedMs_ += dtMs;
    }
    wasOn_ = true;
  } else {
    elapsedMs_ = 0.0;
    wasOn_ = false;
  }

  // 5) 「現在も放出側条件を満たし(conditionOn_)」かつアーム済みかつ継続時間が閾値に達したら放出確定
  //    （ラッチ）。conditionOn_ を課さないと、requiredMs=0（負値/NaN の sanitize 結果を含む）のとき
  //    暗(OFF)サンプルで armed かつ elapsedMs_=0>=0
  //    が成立し、放出前(装置内)なのに誤ラッチする（Codex P2）。 一度立ったら reset() まで保持。
  if (conditionOn_ && armed_ && elapsedMs_ >= cfg_.requiredMs) {
    released_ = true;
  }
}

bool ReleaseDetector::isReleasedNow() const { return conditionOn_; }

bool ReleaseDetector::isArmed() const { return armed_; }

double ReleaseDetector::releasedElapsedMs() const { return elapsedMs_; }

bool ReleaseDetector::hasReleased() const { return released_; }

int ReleaseDetector::lastRaw() const { return lastRaw_; }

}  // namespace release_detect
