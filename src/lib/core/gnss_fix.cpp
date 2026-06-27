#include "gnss_fix.h"

#include <cmath>

namespace gnss {

bool isValidCoordinate(double latDeg, double lonDeg) {
  // NaN/Inf を先に弾く（範囲比較は NaN に対し常に false になるが、Inf を確実に除くため明示）。
  if (!std::isfinite(latDeg) || !std::isfinite(lonDeg)) {
    return false;
  }
  return latDeg >= kMinLatitude && latDeg <= kMaxLatitude && lonDeg >= kMinLongitude &&
         lonDeg <= kMaxLongitude;
}

bool hasPositionFix(const GnssFix& fix) {
  if (!fix.posDataExist) {
    return false;  // 測位不能（座標は不定。前回値/0 が残りうる）
  }
  // posFixMode は仕様上 1/2/3 のみ。化け値(4以上)を FIX と誤認しないよう範囲一致で判定する。
  const bool fixOk = (fix.fixMode == kFix2D || fix.fixMode == kFix3D);
  return fixOk && isValidCoordinate(fix.latitude, fix.longitude);
}

bool isUsableForNavigation(const GnssFix& fix, float maxHdop) {
  if (!hasPositionFix(fix)) {
    return false;
  }
  // HDOP は (0, maxHdop]。0 は未取得/無効の典型値（高精度と誤認しない）。
  // 負値・NaN は hdop > 0 が偽になり弾かれる。
  return fix.hdop > 0.0f && fix.hdop <= maxHdop;
}

}  // namespace gnss
