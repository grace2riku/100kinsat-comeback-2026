// landing（着地=静止検知）の単体テスト
//
// 「降下中→着地衝撃→静止」を加速度の大きさ |a| の窓内分散＋平均で判定するロジックを、
// 正常系だけでなく境界・異常系まで網羅して検証する:
//   - 純粋関数（大きさ/平均/分散）の値
//   - 据置（|a|≒g・分散小）が継続時間で着地確定（ラッチ）すること
//   - 自由落下（|a|≒0・分散小）を静止と誤判定しないこと（重力基準の併用）
//   - パラ降下の揺動（g 近傍だが分散大）を静止と誤判定しないこと
//   - 着地衝撃（スパイク）が継続時間をリセットし、単発では確定しないこと
//   - 各しきい値ちょうどの境界（<= 判定）
// 実際のしきい値の物理的妥当性は実機（bring-up）で確認する範囲外。

#include <cmath>

#include "doctest.h"
#include "landing.h"

using landing::Accel3;
using landing::LandingDetector;
using landing::StillConfig;

// テスト用の決定的な設定: 窓4サンプル・50ms周期・静止100ms継続で確定。
static StillConfig testCfg() {
  StillConfig c;
  c.gravity = 9.8;
  c.gTolerance = 1.0;    // |平均-9.8|<=1.0 で据置相当
  c.maxVariance = 0.25;  // 分散<=0.25 で揺れ無し相当
  c.windowSize = 4;
  c.requiredStillMs = 100;  // 50ms×2サンプル分、窓充填後に必要
  return c;
}

// 大きさ m の加速度を鉛直下向き(z)に置いた3軸（向きは大きさに影響しないので便宜的）。
static Accel3 axialZ(double m) { return Accel3{0.0, 0.0, m}; }

// 静止(据置)サンプルを n 回、dtMs 周期で投入する。
static void feedStill(LandingDetector& d, int n, double dtMs, double mag = 9.8) {
  for (int i = 0; i < n; ++i) {
    d.update(axialZ(mag), dtMs);
  }
}

// ---- 純粋関数 ----

TEST_CASE("vectorMagnitude: 3-4-0 は 5、ゼロは 0、符号は大きさに影響しない") {
  CHECK(landing::vectorMagnitude(3.0, 4.0, 0.0) == doctest::Approx(5.0));
  CHECK(landing::vectorMagnitude(0.0, 0.0, 0.0) == doctest::Approx(0.0));
  CHECK(landing::vectorMagnitude(0.0, 0.0, -9.8) == doctest::Approx(9.8));
  CHECK(landing::vectorMagnitude(-1.0, -2.0, -2.0) == doctest::Approx(3.0));
}

TEST_CASE("Accel3::magnitude: struct からも同じ大きさが出る") {
  CHECK(Accel3{1.0, 2.0, 2.0}.magnitude() == doctest::Approx(3.0));
}

TEST_CASE("mean/variance: 既知系列で平均と母分散が一致、空は 0") {
  const double v[4] = {1.0, 2.0, 3.0, 4.0};
  CHECK(landing::mean(v, 4) == doctest::Approx(2.5));
  // 母分散 = ((1.5^2)+(0.5^2)+(0.5^2)+(1.5^2))/4 = 5/4 = 1.25
  CHECK(landing::variance(v, 4) == doctest::Approx(1.25));
  CHECK(landing::mean(v, 0) == doctest::Approx(0.0));
  CHECK(landing::variance(v, 0) == doctest::Approx(0.0));
}

TEST_CASE("variance: 一定値の分散は 0（非負）") {
  const double v[3] = {9.8, 9.8, 9.8};
  CHECK(landing::variance(v, 3) == doctest::Approx(0.0));
  CHECK(landing::variance(v, 3) >= 0.0);
}

// ---- 生バイト→加速度成分 変換（ターゲット依存の罠をホストで固める） ----

TEST_CASE("accelFromRaw: 100 LSB = 1 m/s^2、ゼロは 0") {
  CHECK(landing::accelFromRaw(0x00, 0x00) == doctest::Approx(0.0));
  CHECK(landing::accelFromRaw(100, 0) == doctest::Approx(1.0));      // 100 LSB
  CHECK(landing::accelFromRaw(0xD4, 0x03) == doctest::Approx(9.8));  // 980 LSB
}

TEST_CASE("accelFromRaw: 負値（2の補数 int16）を正しく復元する") {
  // -100 = 0xFF9C（LSB=0x9C, MSB=0xFF）→ -1.0 m/s^2
  CHECK(landing::accelFromRaw(0x9C, 0xFF) == doctest::Approx(-1.0));
  // -980 = 0xFC2C（LSB=0x2C, MSB=0xFC）→ -9.8 m/s^2
  CHECK(landing::accelFromRaw(0x2C, 0xFC) == doctest::Approx(-9.8));
}

// ---- 初期状態・窓充填 ----

TEST_CASE("初期状態: 未着地・窓未充填・値は0") {
  LandingDetector d(testCfg());
  CHECK_FALSE(d.hasLanded());
  CHECK_FALSE(d.windowFull());
  CHECK_FALSE(d.isStillNow());
  CHECK(d.stillElapsedMs() == doctest::Approx(0.0));
}

TEST_CASE("窓が満ちるまで isStillNow は false（充填後に判定開始）") {
  LandingDetector d(testCfg());  // windowSize=4
  feedStill(d, 3, 50.0);
  CHECK_FALSE(d.windowFull());
  CHECK_FALSE(d.isStillNow());
  feedStill(d, 1, 50.0);  // 4個目で充填
  CHECK(d.windowFull());
  CHECK(d.isStillNow());
}

// ---- 据置 → 着地確定（ラッチ） ----

TEST_CASE("据置が継続時間に達すると着地確定しラッチする") {
  LandingDetector d(testCfg());  // 窓4・要静止100ms
  feedStill(d, 4, 50.0);         // 充填完了（この時点で静止条件成立）
  CHECK(d.isStillNow());
  // 充填後、静止が継続時間(100ms=50ms×2)に達するまでは未確定
  feedStill(d, 1, 50.0);  // 50ms 経過
  CHECK_FALSE(d.hasLanded());
  feedStill(d, 1, 50.0);  // 累計100ms 到達
  CHECK(d.hasLanded());
}

TEST_CASE("着地確定後は動きが再開してもラッチを保持する") {
  LandingDetector d(testCfg());
  feedStill(d, 6, 50.0);
  REQUIRE(d.hasLanded());
  // 大きく揺らす（分散大）→ isStillNow は落ちるが hasLanded は保持
  for (int i = 0; i < 4; ++i) {
    d.update(axialZ(i % 2 == 0 ? 2.0 : 18.0), 50.0);
  }
  CHECK_FALSE(d.isStillNow());
  CHECK(d.hasLanded());
}

TEST_CASE("reset で窓・継続時間・ラッチが初期化される") {
  LandingDetector d(testCfg());
  feedStill(d, 6, 50.0);
  REQUIRE(d.hasLanded());
  d.reset();
  CHECK_FALSE(d.hasLanded());
  CHECK_FALSE(d.windowFull());
  CHECK(d.stillElapsedMs() == doctest::Approx(0.0));
}

// ---- 誤判定防止（DoD の核心） ----

TEST_CASE("自由落下(|a|≒0・分散小)は静止と誤判定しない（重力基準の併用）") {
  LandingDetector d(testCfg());
  feedStill(d, 8, 50.0, /*mag=*/0.0);  // ずっと無重力
  CHECK(d.windowFull());
  CHECK(d.variance() == doctest::Approx(0.0));  // 分散は小さいが…
  CHECK_FALSE(d.isStillNow());                  // 平均が g から遠いので静止でない
  CHECK_FALSE(d.hasLanded());
}

TEST_CASE("パラ降下の揺動(g近傍だが分散大)は静止と誤判定しない") {
  LandingDetector d(testCfg());
  // 平均は 9.8 付近だが振れ幅が大きい（分散 > maxVariance）
  for (int i = 0; i < 8; ++i) {
    d.update(axialZ(i % 2 == 0 ? 7.0 : 12.6), 50.0);  // 平均9.8, 分散≈7.84
  }
  CHECK(d.windowFull());
  CHECK(d.variance() > testCfg().maxVariance);
  CHECK_FALSE(d.isStillNow());
  CHECK_FALSE(d.hasLanded());
}

TEST_CASE("着地衝撃(スパイク)は継続時間をリセットし単発では確定しない") {
  LandingDetector d(testCfg());
  feedStill(d, 4, 50.0);  // 充填し静止成立
  feedStill(d, 1, 50.0);  // 50ms 静止継続（あと50msで確定するところ）
  CHECK_FALSE(d.hasLanded());
  // ここで着地衝撃スパイク → 分散が跳ね上がり静止条件が崩れ、継続時間リセット
  d.update(axialZ(40.0), 50.0);
  CHECK_FALSE(d.isStillNow());
  CHECK(d.stillElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasLanded());
  // スパイクの影響が窓から抜け、再び静止が継続時間に達して初めて確定する
  feedStill(d, 4, 50.0);  // 窓を静止値で満たし直す
  feedStill(d, 2, 50.0);  // 100ms 静止継続
  CHECK(d.hasLanded());
}

// ---- 境界値 ----

TEST_CASE("分散が上限ちょうどは静止に含める（<= 判定）") {
  StillConfig c = testCfg();
  // 大きさ {9.3,10.3,9.3,10.3} → 平均9.8, 母分散=0.25=maxVariance
  LandingDetector d(c);
  const double seq[4] = {9.3, 10.3, 9.3, 10.3};
  for (double m : seq) {
    d.update(axialZ(m), 50.0);
  }
  CHECK(d.variance() == doctest::Approx(0.25));
  CHECK(d.meanMagnitude() == doctest::Approx(9.8));
  CHECK(d.isStillNow());  // ちょうど上限は静止に含める
}

TEST_CASE("平均と g の差が許容ちょうどは静止に含める（<= 判定）") {
  StillConfig c = testCfg();  // gTolerance=1.0
  LandingDetector d(c);
  feedStill(d, 4, 50.0, /*mag=*/8.8);  // |8.8-9.8|=1.0 ちょうど、分散0
  CHECK(d.meanMagnitude() == doctest::Approx(8.8));
  CHECK(d.isStillNow());
}

TEST_CASE("平均と g の差が許容を僅かに超えると静止でない") {
  StillConfig c = testCfg();  // gTolerance=1.0
  LandingDetector d(c);
  feedStill(d, 4, 50.0, /*mag=*/8.79);  // |8.79-9.8|=1.01 > 1.0
  CHECK_FALSE(d.isStillNow());
}

TEST_CASE("継続時間ちょうど(>=)で確定する。最初の静止サンプルは0起点") {
  StillConfig c = testCfg();  // requiredStillMs=100
  LandingDetector d(c);
  feedStill(d, 4, 50.0);  // 4個目で充填＝最初の静止サンプル（継続時間の起点=0）
  CHECK(d.isStillNow());
  CHECK(d.stillElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasLanded());
  feedStill(d, 1, 50.0);  // +50ms
  CHECK(d.stillElapsedMs() == doctest::Approx(50.0));
  CHECK_FALSE(d.hasLanded());
  feedStill(d, 1, 50.0);  // +50ms = 100ms ちょうど
  CHECK(d.stillElapsedMs() == doctest::Approx(100.0));
  CHECK(d.hasLanded());  // 継続時間ちょうどで確定（>=）
}

// ---- 設定のクランプ・異常入力 ----

TEST_CASE("windowSize は 2..kMaxWindow にクランプされる") {
  StillConfig c = testCfg();
  c.windowSize = 200;  // 上限超過
  LandingDetector d(c);
  // kMaxWindow(=64) 個入れれば充填されるはず（201個目まで待たされない）
  feedStill(d, landing::kMaxWindow, 10.0);
  CHECK(d.windowFull());
}

TEST_CASE("dtMs<=0 は時間経過なしとして継続時間を進めない") {
  LandingDetector d(testCfg());
  feedStill(d, 4, 0.0);  // 充填はされるが時間は進まない
  CHECK(d.isStillNow());
  CHECK(d.stillElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasLanded());
}
