#include "compass.h"

#include <cmath>
#include <limits>

namespace compass {

// 無効方位の番兵は quiet NaN。NaN は normalizeHeading/shortestDelta を素通りし、
// isValidHeading() で false になる（演算へ混入しても破綻せず無効が伝播する）。
const double kInvalidHeading = std::numeric_limits<double>::quiet_NaN();

bool isValidHeading(double headingDeg) { return std::isfinite(headingDeg); }

double normalizeHeading(double deg) {
  if (std::isnan(deg)) {
    return deg;  // 無効値は素通し
  }
  // fmod は被除数の符号を残す（負入力で負を返す）ため、負側は +360 で正へ折り返す。
  // geo の bearingDegrees は atan2 由来で入力が必ず [-180,180] のため「+360 してから fmod」で
  // 足りる。本関数は任意角を受ける一般正規化なので、どんな周回数でも成立するこの式を使う。
  double r = std::fmod(deg, 360.0);
  if (r < 0.0) {
    r += 360.0;
  }
  if (r == 0.0) {
    r = 0.0;  // fmod が生む -0.0 を +0.0 に正規化（不変条件 0 <= θ を厳密に満たす）
  }
  return r;
}

double shortestDelta(double fromDeg, double toDeg) {
  if (std::isnan(fromDeg) || std::isnan(toDeg)) {
    return kInvalidHeading;
  }
  double d = normalizeHeading(toDeg - fromDeg);  // [0, 360)
  if (d > 180.0) {
    d -= 360.0;  // 180超は反対回りの方が短い → (-180, 0) へ
  }
  return d;  // (-180, 180]
}

uint8_t minCalibration(const CalibrationStatus& c) {
  uint8_t m = c.system;
  if (c.gyro < m) {
    m = c.gyro;
  }
  if (c.accel < m) {
    m = c.accel;
  }
  if (c.mag < m) {
    m = c.mag;
  }
  return m;
}

// 校正値は仕様上 0-3。I2C 化け・未初期化で 4 以上が混入しても「完了」と誤判定して
// HW を駆動しないよう、>=3 ではなく ==3（厳密一致）で判定する（B2: 範囲外を安全側に弾く）。
bool isFullyCalibrated(const CalibrationStatus& c) {
  return c.system == 3 && c.gyro == 3 && c.accel == 3 && c.mag == 3;
}

bool isHeadingReady(const CalibrationStatus& c) { return c.system == 3 && c.mag == 3; }

}  // namespace compass
