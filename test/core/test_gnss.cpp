// gnss（GNSS 測位データの妥当性・品質判定）の単体テスト
//
// 「測位できているか(hasPositionFix)」「走行に使える品質か(isUsableForNavigation)」を、
// 正常系だけでなく境界・異常系（測位不能 posDataExist=0 / Invalid FIX / 範囲外座標 /
// 低精度 HDOP / HDOP=0 の無効値 / NaN）まで網羅して検証する。
// 実際の測位の物理的正しさは実機（gnss_bringup）で確認する範囲外。

#include <cmath>
#include <limits>

#include "doctest.h"
#include "gnss_fix.h"

// 妥当な 3D FIX のひな形（種子島付近）。各テストで一部だけ書き換えて使う。
static gnss::GnssFix makeGoodFix() {
  gnss::GnssFix f;
  f.posDataExist = true;
  f.fixMode = gnss::kFix3D;
  f.numSatellites = 8;
  f.latitude = 30.401;  // 種子島宇宙センター付近
  f.longitude = 130.971;
  f.velocity = 0.0f;
  f.direction = 0.0f;
  f.hdop = 1.2f;
  return f;
}

// ---- isValidCoordinate ----

TEST_CASE("isValidCoordinate: 通常の座標（種子島）は妥当") {
  CHECK(gnss::isValidCoordinate(30.401, 130.971));
}

TEST_CASE("isValidCoordinate: 範囲の境界値（±90/±180）は包含する") {
  CHECK(gnss::isValidCoordinate(90.0, 180.0));
  CHECK(gnss::isValidCoordinate(-90.0, -180.0));
  CHECK(gnss::isValidCoordinate(0.0, 0.0));
}

TEST_CASE("isValidCoordinate: 緯度の範囲外は弾く") {
  CHECK_FALSE(gnss::isValidCoordinate(90.1, 0.0));
  CHECK_FALSE(gnss::isValidCoordinate(-90.1, 0.0));
}

TEST_CASE("isValidCoordinate: 経度の範囲外は弾く") {
  CHECK_FALSE(gnss::isValidCoordinate(0.0, 180.1));
  CHECK_FALSE(gnss::isValidCoordinate(0.0, -180.1));
}

TEST_CASE("isValidCoordinate: NaN/Inf は弾く（未測位の残骸を流さない）") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  CHECK_FALSE(gnss::isValidCoordinate(nan, 130.0));
  CHECK_FALSE(gnss::isValidCoordinate(30.0, nan));
  CHECK_FALSE(gnss::isValidCoordinate(inf, 130.0));
  CHECK_FALSE(gnss::isValidCoordinate(30.0, -inf));
}

// ---- hasPositionFix ----

TEST_CASE("hasPositionFix: 3D FIX・座標妥当なら true") {
  CHECK(gnss::hasPositionFix(makeGoodFix()));
}

TEST_CASE("hasPositionFix: 2D FIX でも位置は使える") {
  gnss::GnssFix f = makeGoodFix();
  f.fixMode = gnss::kFix2D;
  CHECK(gnss::hasPositionFix(f));
}

TEST_CASE("hasPositionFix: posDataExist=0（測位不能）は false") {
  gnss::GnssFix f = makeGoodFix();
  f.posDataExist = false;
  CHECK_FALSE(gnss::hasPositionFix(f));
}

TEST_CASE("hasPositionFix: Invalid FIX(1) は false") {
  gnss::GnssFix f = makeGoodFix();
  f.fixMode = gnss::kFixInvalid;
  CHECK_FALSE(gnss::hasPositionFix(f));
}

TEST_CASE("hasPositionFix: 化けた fixMode(>=4) を FIX と誤認しない") {
  gnss::GnssFix f = makeGoodFix();
  f.fixMode = 4;
  CHECK_FALSE(gnss::hasPositionFix(f));
  f.fixMode = 255;
  CHECK_FALSE(gnss::hasPositionFix(f));
}

TEST_CASE("hasPositionFix: 座標が範囲外なら（FIXフラグが立っていても）false") {
  gnss::GnssFix f = makeGoodFix();
  f.latitude = 200.0;  // 化け値
  CHECK_FALSE(gnss::hasPositionFix(f));
}

TEST_CASE("hasPositionFix: FIX主張でも座標(0,0)(Null Island)は未確定値として弾く") {
  // posDataExist=1・3D FIX なのに座標フィールドに 0 が残った異常を、値レベルで拒否する。
  gnss::GnssFix f = makeGoodFix();
  f.latitude = 0.0;
  f.longitude = 0.0;
  CHECK_FALSE(gnss::hasPositionFix(f));
  // 片側だけ 0（例: 0,131）は実在し得る座標なので弾かない（境界の取り違え防止）。
  f.longitude = 130.971;
  CHECK(gnss::hasPositionFix(f));
}

// ---- isUsableForNavigation ----

TEST_CASE("isUsableForNavigation: FIXあり・HDOP良好なら true") {
  CHECK(gnss::isUsableForNavigation(makeGoodFix()));
}

TEST_CASE("isUsableForNavigation: HDOP が既定上限(5.0)ちょうどは許容") {
  gnss::GnssFix f = makeGoodFix();
  f.hdop = gnss::kDefaultMaxHdop;  // 5.0
  CHECK(gnss::isUsableForNavigation(f));
}

TEST_CASE("isUsableForNavigation: HDOP が上限超(5.1)は低精度として弾く") {
  gnss::GnssFix f = makeGoodFix();
  f.hdop = 5.1f;
  CHECK_FALSE(gnss::isUsableForNavigation(f));
}

TEST_CASE("isUsableForNavigation: HDOP=0（無効値）は高精度と誤認せず弾く") {
  gnss::GnssFix f = makeGoodFix();
  f.hdop = 0.0f;
  CHECK_FALSE(gnss::isUsableForNavigation(f));
}

TEST_CASE("isUsableForNavigation: HDOP が負/NaN は弾く") {
  gnss::GnssFix f = makeGoodFix();
  f.hdop = -1.0f;
  CHECK_FALSE(gnss::isUsableForNavigation(f));
  f.hdop = std::numeric_limits<float>::quiet_NaN();
  CHECK_FALSE(gnss::isUsableForNavigation(f));
}

TEST_CASE("isUsableForNavigation: 測位不能(posDataExist=0)なら HDOP 良好でも false") {
  gnss::GnssFix f = makeGoodFix();
  f.posDataExist = false;
  f.hdop = 1.0f;
  CHECK_FALSE(gnss::isUsableForNavigation(f));
}

TEST_CASE("isUsableForNavigation: maxHdop を厳しく指定すると中精度を弾ける") {
  gnss::GnssFix f = makeGoodFix();
  f.hdop = 3.0f;
  CHECK(gnss::isUsableForNavigation(f, 5.0f));        // 既定なら許容
  CHECK_FALSE(gnss::isUsableForNavigation(f, 2.0f));  // 厳しめなら弾く
}
