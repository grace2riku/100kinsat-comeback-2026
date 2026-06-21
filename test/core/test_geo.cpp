// geo（測地計算）の単体テスト
//
// 既知の解析解（子午線/赤道方向の1度＝約111.2km、四方位、同一点）と
// 実在2地点（東京駅↔品川駅）で、距離・方位の正しさを検証する。
// 正常系だけでなく、同一点（退化ケース）・方位の正規化・対称性も確認する。

#include "doctest.h"
#include "geo.h"

// 距離の解析解: 球面上の 1度 = R * (π/180) ≈ 111194.927 m
static constexpr double kOneDegreeMeters = 111194.927;

TEST_CASE("distanceMeters: 子午線方向の1度は約111.2km") {
  // (0,0) -> (1,0): 緯度1度ぶん北
  CHECK(geo::distanceMeters(0.0, 0.0, 1.0, 0.0) == doctest::Approx(kOneDegreeMeters).epsilon(1e-5));
}

TEST_CASE("distanceMeters: 赤道方向の1度は約111.2km") {
  // (0,0) -> (0,1): 経度1度ぶん東（赤道上）
  CHECK(geo::distanceMeters(0.0, 0.0, 0.0, 1.0) == doctest::Approx(kOneDegreeMeters).epsilon(1e-5));
}

TEST_CASE("distanceMeters: 同一点の距離は0") {
  CHECK(geo::distanceMeters(35.681236, 139.767125, 35.681236, 139.767125) == doctest::Approx(0.0));
}

TEST_CASE("distanceMeters: 距離は方向に依らず対称") {
  double ab = geo::distanceMeters(35.681236, 139.767125, 35.628471, 139.738760);
  double ba = geo::distanceMeters(35.628471, 139.738760, 35.681236, 139.767125);
  CHECK(ab == doctest::Approx(ba));
}

TEST_CASE("distanceMeters: 実在2地点（東京駅→品川駅）は約6402m") {
  // 参照値（同一公式・R=6371000）で 6402.497 m
  CHECK(geo::distanceMeters(35.681236, 139.767125, 35.628471, 139.738760) ==
        doctest::Approx(6402.497).epsilon(1e-4));
}

TEST_CASE("bearingDegrees: 真北は0度") {
  CHECK(geo::bearingDegrees(0.0, 0.0, 1.0, 0.0) == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("bearingDegrees: 真東は90度") {
  CHECK(geo::bearingDegrees(0.0, 0.0, 0.0, 1.0) == doctest::Approx(90.0).epsilon(1e-6));
}

TEST_CASE("bearingDegrees: 真南は180度") {
  CHECK(geo::bearingDegrees(1.0, 0.0, 0.0, 0.0) == doctest::Approx(180.0).epsilon(1e-6));
}

TEST_CASE("bearingDegrees: 真西は270度（負値ではなく0-360に正規化）") {
  double b = geo::bearingDegrees(0.0, 1.0, 0.0, 0.0);
  CHECK(b == doctest::Approx(270.0).epsilon(1e-6));
  CHECK(b >= 0.0);
  CHECK(b < 360.0);
}

TEST_CASE("bearingDegrees: 戻り値は常に[0,360)に収まる") {
  // 任意の組み合わせで範囲不変条件を確認
  const double pts[][2] = {{35.6, 139.7}, {-33.9, 151.2}, {40.7, -74.0}, {0.0, 0.0}};
  for (auto& p1 : pts) {
    for (auto& p2 : pts) {
      double b = geo::bearingDegrees(p1[0], p1[1], p2[0], p2[1]);
      CHECK(b >= 0.0);
      CHECK(b < 360.0);
    }
  }
}

TEST_CASE("bearingDegrees: 同一点は0度（退化ケース）") {
  CHECK(geo::bearingDegrees(35.681236, 139.767125, 35.681236, 139.767125) == doctest::Approx(0.0));
}

TEST_CASE("bearingDegrees: 往路と復路の方位は約180度ずれる") {
  double fwd = geo::bearingDegrees(35.681236, 139.767125, 35.628471, 139.738760);
  double rev = geo::bearingDegrees(35.628471, 139.738760, 35.681236, 139.767125);
  double diff = fwd - rev;
  // -360..360 を 0..360 に畳んでから 180 との差を見る
  while (diff < 0) diff += 360.0;
  CHECK(diff == doctest::Approx(180.0).epsilon(1e-3));
}
