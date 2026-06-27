// compass（BNO055 方位ロジック）の単体テスト
//
// HW非依存の純粋ロジックを検証する。BNO055 の I2C 読み取り自体はここでは扱わず、
// 「センサから得た値をどう正規化・判定するか」だけをテストする（実機読み取りは
// src/lib/hal の BNO055 ラッパが担い、NT-Shell で別途確認する）。
//
// 重点（Issue #9 DoD）:
//   - 方位角の 0-360 ラップアラウンド正規化（負値・複数周回・境界）
//   - 2方位間の最短回頭角（符号付き・ラップ跨ぎ）
//   - キャリブレーション状態(0-3)と「校正完了」判定
//   - 無効値（センサ未検出/読み取り失敗）の伝播

#include <cmath>

#include "compass.h"
#include "doctest.h"

// ---- normalizeHeading: 0 <= θ < 360 への正規化 ----------------------------

TEST_CASE("normalizeHeading: 範囲内の値はそのまま") {
  CHECK(compass::normalizeHeading(0.0) == doctest::Approx(0.0));
  CHECK(compass::normalizeHeading(90.0) == doctest::Approx(90.0));
  CHECK(compass::normalizeHeading(180.0) == doctest::Approx(180.0));
  CHECK(compass::normalizeHeading(359.9) == doctest::Approx(359.9));
}

TEST_CASE("normalizeHeading: 360 とその倍数は 0 に畳む") {
  CHECK(compass::normalizeHeading(360.0) == doctest::Approx(0.0));
  CHECK(compass::normalizeHeading(720.0) == doctest::Approx(0.0));
}

TEST_CASE("normalizeHeading: 360 以上は周回を畳む") {
  CHECK(compass::normalizeHeading(361.0) == doctest::Approx(1.0));
  CHECK(compass::normalizeHeading(450.0) == doctest::Approx(90.0));
  CHECK(compass::normalizeHeading(725.0) == doctest::Approx(5.0));  // 2周+5
}

TEST_CASE("normalizeHeading: 負値は正方向へ畳む") {
  CHECK(compass::normalizeHeading(-1.0) == doctest::Approx(359.0));
  CHECK(compass::normalizeHeading(-90.0) == doctest::Approx(270.0));
  CHECK(compass::normalizeHeading(-360.0) == doctest::Approx(0.0));
}

TEST_CASE("normalizeHeading: 1周を超える負値も畳む（geo の +360 では不足するケース）") {
  CHECK(compass::normalizeHeading(-361.0) == doctest::Approx(359.0));
  CHECK(compass::normalizeHeading(-725.0) == doctest::Approx(355.0));  // -2周-5
}

TEST_CASE("normalizeHeading: 無効値(NaN)はそのまま伝播する") {
  CHECK(std::isnan(compass::normalizeHeading(NAN)));
}

TEST_CASE("normalizeHeading: 結果は -0.0 ではなく +0.0（不変条件 0<=θ を厳密に満たす）") {
  // -360 や -0.0 入力は fmod が -0.0 を生むが、+0.0 に正規化されること（符号ビットまで確認）。
  CHECK(std::signbit(compass::normalizeHeading(-360.0)) == false);
  CHECK(std::signbit(compass::normalizeHeading(-0.0)) == false);
  CHECK(compass::normalizeHeading(-360.0) == doctest::Approx(0.0));
}

// ---- shortestDelta: 2方位間の最短回頭角（符号付き, (-180,180]）-----------

TEST_CASE("shortestDelta: 同方位は0") {
  CHECK(compass::shortestDelta(123.0, 123.0) == doctest::Approx(0.0));
}

TEST_CASE("shortestDelta: 近接する方位の差分") {
  CHECK(compass::shortestDelta(80.0, 90.0) == doctest::Approx(10.0));
  CHECK(compass::shortestDelta(90.0, 80.0) == doctest::Approx(-10.0));
}

TEST_CASE("shortestDelta: 0/360 のラップを跨いでも最短側を返す") {
  CHECK(compass::shortestDelta(350.0, 10.0) == doctest::Approx(20.0));   // 時計回りに+20
  CHECK(compass::shortestDelta(10.0, 350.0) == doctest::Approx(-20.0));  // 反時計回りに-20
  CHECK(compass::shortestDelta(359.0, 1.0) == doctest::Approx(2.0));
  CHECK(compass::shortestDelta(1.0, 359.0) == doctest::Approx(-2.0));
}

TEST_CASE("shortestDelta: ±180 近傍の境界を正しく振り分ける") {
  CHECK(compass::shortestDelta(0.0, 179.0) == doctest::Approx(179.0));
  CHECK(compass::shortestDelta(0.0, 180.0) == doctest::Approx(180.0));   // 真裏は +180 側
  CHECK(compass::shortestDelta(0.0, 181.0) == doctest::Approx(-179.0));  // 超えたら反対回り
  // 下端 -180 には張り付かない: 差が 181° でも -179° に丸まる（ラップ起点を変えて確認）
  CHECK(compass::shortestDelta(10.0, 191.0) == doctest::Approx(-179.0));
  CHECK(compass::shortestDelta(200.0, 20.0) == doctest::Approx(180.0));  // 差 -180 → +180
}

TEST_CASE("shortestDelta: 結果は常に (-180, 180] に収まる") {
  for (int from = 0; from < 360; from += 13) {
    for (int to = 0; to < 360; to += 17) {
      double d = compass::shortestDelta(static_cast<double>(from), static_cast<double>(to));
      CHECK(d > -180.0);
      CHECK(d <= 180.0);
    }
  }
}

TEST_CASE("shortestDelta: いずれかが無効値(NaN)なら NaN") {
  CHECK(std::isnan(compass::shortestDelta(NAN, 90.0)));
  CHECK(std::isnan(compass::shortestDelta(90.0, NAN)));
}

// ---- isValidHeading: 有効方位の判定 --------------------------------------

TEST_CASE("isValidHeading: 通常の方位は有効") {
  CHECK(compass::isValidHeading(0.0));
  CHECK(compass::isValidHeading(359.9));
}

TEST_CASE("isValidHeading: 無効方位の番兵値は無効") {
  CHECK_FALSE(compass::isValidHeading(compass::kInvalidHeading));
  CHECK_FALSE(compass::isValidHeading(NAN));
}

// ---- キャリブレーション状態(0-3)と校正完了判定 ----------------------------

TEST_CASE("minCalibration: 4要素の最小値（最も遅れている系）を返す") {
  CHECK(compass::minCalibration({3, 3, 3, 3}) == 3);
  CHECK(compass::minCalibration({3, 2, 3, 3}) == 2);
  CHECK(compass::minCalibration({0, 3, 3, 3}) == 0);
  CHECK(compass::minCalibration({2, 1, 3, 2}) == 1);
}

TEST_CASE("isFullyCalibrated: 4要素すべて3で完了（software.md §5.6）") {
  CHECK(compass::isFullyCalibrated({3, 3, 3, 3}));
}

TEST_CASE("isFullyCalibrated: どれか1つでも3未満なら未完了") {
  CHECK_FALSE(compass::isFullyCalibrated({3, 3, 3, 2}));  // mag だけ未達
  CHECK_FALSE(compass::isFullyCalibrated({2, 3, 3, 3}));  // system だけ未達
  CHECK_FALSE(compass::isFullyCalibrated({0, 0, 0, 0}));  // 起動直後
}

TEST_CASE("isHeadingReady: 方位に必要な system と mag が3なら可（gyro/accel 未達でも）") {
  CHECK(compass::isHeadingReady({3, 3, 3, 3}));
  CHECK(compass::isHeadingReady({3, 0, 0, 3}));  // 方位は sys+mag が要
}

TEST_CASE("isHeadingReady: system か mag が3未満なら不可") {
  CHECK_FALSE(compass::isHeadingReady({2, 3, 3, 3}));  // system 未達
  CHECK_FALSE(compass::isHeadingReady({3, 3, 3, 2}));  // mag 未達
  CHECK_FALSE(compass::isHeadingReady({0, 0, 0, 0}));
}

TEST_CASE("校正判定: 仕様外(>3)の化け値を『完了』と誤判定しない（B2: 範囲外を安全側に弾く）") {
  // I2C 化け・未初期化で 4 以上が混入しても、>=3 ではなく ==3 判定なので未完了扱い。
  CHECK_FALSE(compass::isFullyCalibrated({255, 3, 3, 3}));
  CHECK_FALSE(compass::isFullyCalibrated({3, 3, 3, 4}));
  CHECK_FALSE(compass::isHeadingReady({255, 3, 3, 3}));  // system 化け
  CHECK_FALSE(compass::isHeadingReady({3, 3, 3, 9}));    // mag 化け
}

// ---- eulerHeadingFromRaw: 生 Euler レジスタ2バイト → 方位[0,360) ------------
// BNO055 の Euler ヘディングは 1度 = 16 LSB（§3.6.5.4）。HAL が自前 Wire 読みで得た
// 生バイト（LSB, MSB）の合成を、符号/エンディアンの罠（ターゲット≠ホスト, gotchas A）
// が出ないようホストで固定する。値は Adafruit getVector(VECTOR_EULER).x() と一致する。

TEST_CASE("eulerHeadingFromRaw: スケール換算 1度=16LSB（主要方位）") {
  CHECK(compass::eulerHeadingFromRaw(0x00, 0x00) == doctest::Approx(0.0));       // 0 LSB
  CHECK(compass::eulerHeadingFromRaw(0xA0, 0x05) == doctest::Approx(90.0));      // 1440 LSB
  CHECK(compass::eulerHeadingFromRaw(0x40, 0x0B) == doctest::Approx(180.0));     // 2880 LSB
  CHECK(compass::eulerHeadingFromRaw(0xE0, 0x10) == doctest::Approx(270.0));     // 4320 LSB
  CHECK(compass::eulerHeadingFromRaw(0x7F, 0x16) == doctest::Approx(359.9375));  // 5759 LSB(最大)
}

TEST_CASE("eulerHeadingFromRaw: リトルエンディアン（LSB/MSB の取り違えを弾く）") {
  // 第1引数=下位バイト, 第2引数=上位バイト。256 LSB = 16.0°。
  CHECK(compass::eulerHeadingFromRaw(0x00, 0x01) == doctest::Approx(16.0));
  // バイトを入れ替えると 1 LSB = 0.0625°。取り違えれば 16.0 にならず、本テストが検出する。
  CHECK(compass::eulerHeadingFromRaw(0x01, 0x00) == doctest::Approx(0.0625));
}

TEST_CASE("eulerHeadingFromRaw: 負の int16（化け値）も正方向へ正規化して [0,360) を保つ") {
  // 0xFFF0 を符号付き16bitで解釈すると -16 LSB = -1.0° → 正規化で 359.0°。
  // unsigned で誤解釈すると 65520/16=4095° になり、この CHECK で破綻が露見する。
  CHECK(compass::eulerHeadingFromRaw(0xF0, 0xFF) == doctest::Approx(359.0));
}

TEST_CASE("eulerHeadingFromRaw: 360°相当(5760 LSB)は境界で 0 に畳む") {
  CHECK(compass::eulerHeadingFromRaw(0x80, 0x16) == doctest::Approx(0.0));  // 5760 LSB = 360°
}

TEST_CASE("eulerHeadingFromRaw: 戻り値は常に [0,360) に収まる") {
  for (int hi = 0; hi < 256; hi += 17) {
    for (int lo = 0; lo < 256; lo += 17) {
      double h = compass::eulerHeadingFromRaw(static_cast<uint8_t>(lo), static_cast<uint8_t>(hi));
      CHECK(h >= 0.0);
      CHECK(h < 360.0);
    }
  }
}
