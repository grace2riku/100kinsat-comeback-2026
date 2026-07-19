// cone_detect（赤カラーコーン検出）の単体テスト
//
// Spresense カメラの YUV422(UYVY) フレームを合成して、赤閾値→列ヒストグラム→
// 区間連結→重心/幅/方位角の検出ロジックを正常系・境界・異常系まで検証する:
//   - 純粋関数 isRedYuv（各閾値の境界を含む側）
//   - sanitizeConfig のクランプ（非正・負値・NaN/範囲外 hfov）
//   - buildColumnHistogram（列カウントの一致・不正入力の拒否）
//   - columnToBearingDeg（中央=0・左右対称・単調・解析的に厳密な既知値）
//   - detect: 中央/左右オフセット/画面端/全面赤/コーン無し/複数赤領域/
//     ギャップ連結/散在ノイズ/しきい値境界/不正入力/矛盾 Config
// 方位角の期待値は hfov=90°・ratio=0.5 → atan(0.5)=26.565051177...° のように
// 解析的に厳密な値になるよう画像を設計する（手計算の丸めに依存しない）。
// 末尾に実機 cam dump の実写ダンプ回帰（test/data/camera/、台帳は同 README.md）もある。
// 屋外での閾値の物理的妥当性の最終確認は bring-up（camera_bringup.md 手順B/C）の範囲。

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "cone_detect.h"
#include "doctest.h"

using cone::Config;
using cone::Detection;

namespace {

// atan(0.5) の厳密値 [deg]（hfov=90° で ratio=0.5 の画角内方位角）
constexpr double kAtanHalfDeg = 26.56505117707799;

// 合成画像で使う代表色（8bit YUV。無彩色は U=V=128）
constexpr uint8_t kRedY = 80, kRedU = 90, kRedV = 200;       // 彩度の高い赤
constexpr uint8_t kBgY = 120, kBgU = 100, kBgV = 90;         // 緑寄りの背景（V が低い）
constexpr uint8_t kGrayY = 128, kGrayU = 128, kGrayV = 128;  // 無彩色

// width x height の UYVY フレームを単色 (y,u,v) で作る。
std::vector<uint8_t> makeFrame(int width, int height, uint8_t y, uint8_t u, uint8_t v) {
  std::vector<uint8_t> buf(static_cast<size_t>(width) * height * 2);
  for (int row = 0; row < height; ++row) {
    for (int pair = 0; pair < width / 2; ++pair) {
      size_t o = (static_cast<size_t>(row) * width + pair * 2) * 2;
      buf[o + 0] = u;
      buf[o + 1] = y;
      buf[o + 2] = v;
      buf[o + 3] = y;
    }
  }
  return buf;
}

// [x0,x1) x [y0,y1) を (y,u,v) で塗る。UYVY は2画素でクロマを共有するため、
// 曖昧さを避けて x0/x1 は偶数（ペア境界）のみ許可する。
void fillRect(std::vector<uint8_t>& buf, int width, int x0, int x1, int y0, int y1, uint8_t y,
              uint8_t u, uint8_t v) {
  REQUIRE(x0 % 2 == 0);
  REQUIRE(x1 % 2 == 0);
  for (int row = y0; row < y1; ++row) {
    for (int x = x0; x < x1; x += 2) {
      size_t o = (static_cast<size_t>(row) * width + x) * 2;
      buf[o + 0] = u;
      buf[o + 1] = y;
      buf[o + 2] = v;
      buf[o + 3] = y;
    }
  }
}

// 小画像向けの決定的な設定（既定値は QVGA 向けで大きすぎるため縮小）。
Config smallCfg() {
  Config c;
  c.minColumnCount = 2;
  c.maxGapColumns = 1;
  c.minWidthColumns = 2;
  c.minRedPixels = 8;
  c.hfovDeg = 66.0;
  return c;
}

}  // namespace

// ---- isRedYuv（閾値境界は含む側） ----

TEST_CASE("isRedYuv: 全閾値の境界ちょうどは赤と判定する（含む側）") {
  Config c;  // 既定: yMin=32 yMax=235 uMax=120 vMin=160
  CHECK(cone::isRedYuv(32, 120, 160, c));
  CHECK(cone::isRedYuv(235, 120, 160, c));
  CHECK(cone::isRedYuv(100, 0, 255, c));  // 帯域内で最も赤い側
}

TEST_CASE("isRedYuv: 各閾値を1だけ外れると赤ではない") {
  Config c;
  CHECK_FALSE(cone::isRedYuv(31, 120, 160, c));   // 輝度下限外（黒つぶれ）
  CHECK_FALSE(cone::isRedYuv(236, 120, 160, c));  // 輝度上限外（白飛び）
  CHECK_FALSE(cone::isRedYuv(100, 121, 160, c));  // Cb 過大（青すぎ）
  CHECK_FALSE(cone::isRedYuv(100, 120, 159, c));  // Cr 不足（赤みが足りない）
}

TEST_CASE("isRedYuv: 無彩色（グレー）と緑背景は赤ではない") {
  Config c;
  CHECK_FALSE(cone::isRedYuv(kGrayY, kGrayU, kGrayV, c));
  CHECK_FALSE(cone::isRedYuv(kBgY, kBgU, kBgV, c));
  CHECK(cone::isRedYuv(kRedY, kRedU, kRedV, c));
}

// ---- sanitizeConfig ----

TEST_CASE("sanitizeConfig: 区間パラメータの非正値は下限へクランプ") {
  Config c;
  c.minColumnCount = 0;
  c.maxGapColumns = -1;
  c.minWidthColumns = -3;
  c.minRedPixels = 0;
  Config s = cone::sanitizeConfig(c);
  CHECK(s.minColumnCount == 1);
  CHECK(s.maxGapColumns == 0);
  CHECK(s.minWidthColumns == 1);
  CHECK(s.minRedPixels == 1);
}

TEST_CASE("sanitizeConfig: hfovDeg の非有限・範囲外は既定値 66.0 へ戻す") {
  Config c;
  c.hfovDeg = 0.0;
  CHECK(cone::sanitizeConfig(c).hfovDeg == 66.0);
  c.hfovDeg = -10.0;
  CHECK(cone::sanitizeConfig(c).hfovDeg == 66.0);
  c.hfovDeg = 180.0;
  CHECK(cone::sanitizeConfig(c).hfovDeg == 66.0);
  c.hfovDeg = std::nan("");
  CHECK(cone::sanitizeConfig(c).hfovDeg == 66.0);
}

TEST_CASE("sanitizeConfig: 正常値は変更しない") {
  Config c = smallCfg();
  c.hfovDeg = 45.0;
  Config s = cone::sanitizeConfig(c);
  CHECK(s.minColumnCount == 2);
  CHECK(s.maxGapColumns == 1);
  CHECK(s.minWidthColumns == 2);
  CHECK(s.minRedPixels == 8);
  CHECK(s.hfovDeg == 45.0);
}

// ---- buildColumnHistogram ----

TEST_CASE("buildColumnHistogram: 合成画像の列ごとの赤画素数が一致する") {
  auto buf = makeFrame(8, 4, kBgY, kBgU, kBgV);
  fillRect(buf, 8, 2, 6, 1, 3, kRedY, kRedU, kRedV);  // 列2..5 に高さ2の赤
  uint16_t hist[8] = {99, 99, 99, 99, 99, 99, 99, 99};
  REQUIRE(cone::buildColumnHistogram(buf.data(), 8, 4, Config{}, hist));
  const uint16_t expected[8] = {0, 0, 2, 2, 2, 2, 0, 0};
  for (int i = 0; i < 8; ++i) {
    CAPTURE(i);
    CHECK(hist[i] == expected[i]);
  }
}

TEST_CASE("buildColumnHistogram: 不正入力（null・奇数/非正の幅・非正の高さ・上限超）は false") {
  auto buf = makeFrame(8, 4, kBgY, kBgU, kBgV);
  uint16_t hist[cone::kMaxDetectWidth];
  Config c;
  CHECK_FALSE(cone::buildColumnHistogram(nullptr, 8, 4, c, hist));
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), 7, 4, c, hist));  // 奇数幅
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), 0, 4, c, hist));
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), -2, 4, c, hist));
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), 8, 0, c, hist));
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), 8, -1, c, hist));
  CHECK_FALSE(cone::buildColumnHistogram(buf.data(), cone::kMaxDetectWidth + 2, 4, c, hist));
  // 高さ上限超は uint16_t の列カウントあふれ防止のため拒否する
  auto tall = makeFrame(8, cone::kMaxDetectHeight + 1, kBgY, kBgU, kBgV);
  CHECK_FALSE(cone::buildColumnHistogram(tall.data(), 8, cone::kMaxDetectHeight + 1, c, hist));
}

TEST_CASE("buildColumnHistogram: 上限高さ kMaxDetectHeight ちょうどは受け付ける") {
  auto buf = makeFrame(4, cone::kMaxDetectHeight, kRedY, kRedU, kRedV);
  uint16_t hist[4] = {0, 0, 0, 0};
  REQUIRE(cone::buildColumnHistogram(buf.data(), 4, cone::kMaxDetectHeight, Config{}, hist));
  CHECK(hist[0] == cone::kMaxDetectHeight);
  CHECK(hist[3] == cone::kMaxDetectHeight);
}

TEST_CASE("buildColumnHistogram: 上限幅 kMaxDetectWidth ちょうどは受け付ける") {
  auto buf = makeFrame(cone::kMaxDetectWidth, 2, kRedY, kRedU, kRedV);
  std::vector<uint16_t> hist(cone::kMaxDetectWidth, 0);
  REQUIRE(cone::buildColumnHistogram(buf.data(), cone::kMaxDetectWidth, 2, Config{}, hist.data()));
  CHECK(hist[0] == 2);
  CHECK(hist[cone::kMaxDetectWidth - 1] == 2);
}

// ---- columnToBearingDeg ----

TEST_CASE("columnToBearingDeg: 画像中央は 0 度") {
  CHECK(cone::columnToBearingDeg(16.0, 32, 66.0) == doctest::Approx(0.0));
  CHECK(cone::columnToBearingDeg(160.0, 320, 66.0) == doctest::Approx(0.0));
}

TEST_CASE("columnToBearingDeg: hfov=90° で半画角の半分の位置は atan(0.5) 度（解析値）") {
  // offset = +8, half = 16 → ratio 0.5。tan(45°)=1 なので atan(0.5) がそのまま答え。
  CHECK(cone::columnToBearingDeg(24.0, 32, 90.0) == doctest::Approx(kAtanHalfDeg));
  CHECK(cone::columnToBearingDeg(8.0, 32, 90.0) == doctest::Approx(-kAtanHalfDeg));
}

TEST_CASE("columnToBearingDeg: 画像端（ratio=±1）はちょうど ±hfov/2") {
  CHECK(cone::columnToBearingDeg(32.0, 32, 66.0) == doctest::Approx(33.0));
  CHECK(cone::columnToBearingDeg(0.0, 32, 66.0) == doctest::Approx(-33.0));
}

TEST_CASE("columnToBearingDeg: 左右対称かつ中央から遠いほど大きい（単調）") {
  const double r = cone::columnToBearingDeg(31.5, 32, 66.0);
  const double l = cone::columnToBearingDeg(0.5, 32, 66.0);
  CHECK(r == doctest::Approx(-l));
  CHECK(cone::columnToBearingDeg(24.0, 32, 66.0) > cone::columnToBearingDeg(20.0, 32, 66.0));
  CHECK(cone::columnToBearingDeg(20.0, 32, 66.0) > 0.0);
}

// ---- detect: 正常系 ----

TEST_CASE("detect: 中央の赤矩形を検出し方位0・幅・密度が解析値と一致する") {
  // 32x24、列 12..19（幅8）× 行 4..20（高さ16）の赤矩形。重心列 = 16.0 = 画像中央。
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 12, 20, 4, 20, kRedY, kRedU, kRedV);
  Detection d = cone::detect(buf.data(), 32, 24, smallCfg());
  CHECK(d.detected);
  CHECK(d.bearingDeg == doctest::Approx(0.0));
  CHECK(d.centerColumn == 16);
  CHECK(d.widthColumns == 8);
  CHECK(d.widthRatio == doctest::Approx(8.0 / 32.0));
  CHECK(d.redPixels == 8 * 16);
  CHECK(d.confidence == doctest::Approx(static_cast<double>(8 * 16) / (8 * 24)));
}

TEST_CASE("detect: 左右にオフセットした矩形の方位角は符号付きの解析値になる") {
  // hfov=90° にして重心が offset=±8（ratio=0.5）になる矩形を置く → ±atan(0.5) 度。
  Config c = smallCfg();
  c.hfovDeg = 90.0;

  auto right = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(right, 32, 20, 28, 4, 20, kRedY, kRedU, kRedV);  // 重心列 24.0
  Detection dr = cone::detect(right.data(), 32, 24, c);
  CHECK(dr.detected);
  CHECK(dr.bearingDeg == doctest::Approx(kAtanHalfDeg));
  CHECK(dr.centerColumn == 24);

  auto left = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(left, 32, 4, 12, 4, 20, kRedY, kRedU, kRedV);  // 重心列 8.0
  Detection dl = cone::detect(left.data(), 32, 24, c);
  CHECK(dl.detected);
  CHECK(dl.bearingDeg == doctest::Approx(-kAtanHalfDeg));
  CHECK(dl.centerColumn == 8);
}

TEST_CASE("detect: 画面右端の単一列でも centerColumn は [0,width) に収まる（Codex P2 回帰）") {
  // UYVY はクロマを2画素で共有するため、最右ペアの Y0 を黒（輝度下限外）・Y1 を赤にして
  // 「列31（最右列）だけが赤」のフレームを作る。重心 31.5 を四捨五入すると 32(=width) となり
  // 契約 [0, width) を破るため、「重心が属する列＝切り捨て」で 31 を返すことを検証する。
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  for (int row = 4; row < 20; ++row) {
    size_t o = (static_cast<size_t>(row) * 32 + 30) * 2;
    buf[o + 0] = kRedU;
    buf[o + 1] = 0;  // 列30: 輝度下限外（黒つぶれ）で赤にしない
    buf[o + 2] = kRedV;
    buf[o + 3] = kRedY;  // 列31: 赤
  }
  Config c = smallCfg();
  c.minWidthColumns = 1;  // 遠距離向けに最小幅を緩めた設定を模す
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK(d.detected);
  CHECK(d.widthColumns == 1);
  CHECK(d.centerColumn == 31);  // width=32 の範囲内（32 になってはならない）
  CHECK(d.bearingDeg > 0.0);
}

TEST_CASE("detect: 単一列の区間の centerColumn はその列自身になる（丸めで隣列へずれない）") {
  // 列4だけが赤（最右端以外の単一列）。重心 4.5 の四捨五入は 5 になってしまうため、
  // 切り捨てで「重心が属する列」= 4 を返すことを検証する。
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  for (int row = 4; row < 20; ++row) {
    size_t o = (static_cast<size_t>(row) * 32 + 4) * 2;
    buf[o + 0] = kRedU;
    buf[o + 1] = kRedY;  // 列4: 赤
    buf[o + 2] = kRedV;
    buf[o + 3] = 0;  // 列5: 輝度下限外（黒つぶれ）で赤にしない
  }
  Config c = smallCfg();
  c.minWidthColumns = 1;
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK(d.detected);
  CHECK(d.widthColumns == 1);
  CHECK(d.centerColumn == 4);
}

TEST_CASE("detect: 画面左端に接する領域も検出できる") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 0, 6, 4, 20, kRedY, kRedU, kRedV);  // 列 0..5
  Detection d = cone::detect(buf.data(), 32, 24, smallCfg());
  CHECK(d.detected);
  CHECK(d.centerColumn == 3);  // 重心 3.0 → 列3
  CHECK(d.widthColumns == 6);
  CHECK(d.bearingDeg < 0.0);  // 左側なので負
}

TEST_CASE("detect: 全面赤は widthRatio=1・confidence=1 で検出する（異常系: 全面赤）") {
  auto buf = makeFrame(32, 24, kRedY, kRedU, kRedV);
  Detection d = cone::detect(buf.data(), 32, 24, smallCfg());
  CHECK(d.detected);
  CHECK(d.widthColumns == 32);
  CHECK(d.widthRatio == doctest::Approx(1.0));
  CHECK(d.confidence == doctest::Approx(1.0));
  CHECK(d.bearingDeg == doctest::Approx(0.0));
}

// ---- detect: 区間選択（複数赤領域・ギャップ） ----

TEST_CASE("detect: 複数の赤領域は赤画素数が最大の区間を選ぶ") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 2, 6, 8, 16, kRedY, kRedU, kRedV);    // 小: 4列 x 8行 = 32 画素
  fillRect(buf, 32, 20, 30, 2, 22, kRedY, kRedU, kRedV);  // 大: 10列 x 20行 = 200 画素
  Detection d = cone::detect(buf.data(), 32, 24, smallCfg());
  CHECK(d.detected);
  CHECK(d.centerColumn == 25);  // 大きい方の重心 (20.5+...+29.5)/10 = 25.0
  CHECK(d.widthColumns == 10);
  CHECK(d.redPixels == 200);
}

TEST_CASE("detect: maxGapColumns 以内の分断は1つの区間として連結する") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 8, 14, 4, 20, kRedY, kRedU, kRedV);   // 列 8..13
  fillRect(buf, 32, 16, 22, 4, 20, kRedY, kRedU, kRedV);  // 列 16..21（ギャップ列 14,15）
  Config c = smallCfg();
  c.maxGapColumns = 2;
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK(d.detected);
  CHECK(d.widthColumns == 14);  // 列 8..21 を1区間として扱う
  CHECK(d.redPixels == 12 * 16);
  CHECK(d.centerColumn == 15);  // 対称なので中心は (8..21) の重心 = 15.0
}

TEST_CASE("detect: maxGapColumns 超の分断は別区間になり大きい方を選ぶ") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 4, 10, 4, 20, kRedY, kRedU, kRedV);   // 6列 x 16行 = 96 画素
  fillRect(buf, 32, 16, 26, 4, 20, kRedY, kRedU, kRedV);  // 10列 x 16行 = 160 画素
  Config c = smallCfg();
  c.maxGapColumns = 1;  // ギャップ6列 > 1 なので連結しない
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK(d.detected);
  CHECK(d.widthColumns == 10);
  CHECK(d.centerColumn == 21);  // (16.5+...+25.5)/10 = 21.0
  CHECK(d.redPixels == 160);
}

TEST_CASE("detect: 幅しきい値未満の高密度区間は捨て、条件を満たす区間を選ぶ") {
  // 幅2列だが全行赤（質量 48）と、幅6列 x 8行（質量 48）。minWidthColumns=4 で
  // 前者は失格。質量が同じでも「条件を満たす区間」から選ぶことを確認する。
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 2, 4, 0, 24, kRedY, kRedU, kRedV);    // 2列 x 24行 = 48 画素
  fillRect(buf, 32, 20, 26, 8, 16, kRedY, kRedU, kRedV);  // 6列 x 8行 = 48 画素
  Config c = smallCfg();
  c.minWidthColumns = 4;
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK(d.detected);
  CHECK(d.widthColumns == 6);
  CHECK(d.centerColumn == 23);  // (20.5+...+25.5)/6 = 23.0
}

// ---- detect: 非検出（コーン無し・ノイズ・しきい値境界） ----

TEST_CASE("detect: コーン無し（背景のみ）は非検出で安全値を返す") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  Detection d = cone::detect(buf.data(), 32, 24, smallCfg());
  CHECK_FALSE(d.detected);
  CHECK(d.bearingDeg == 0.0);
  CHECK(d.centerColumn == -1);
  CHECK(d.widthColumns == 0);
  CHECK(d.widthRatio == 0.0);
  CHECK(d.confidence == 0.0);
  CHECK(d.redPixels == 0);
}

TEST_CASE("detect: 列あたり赤画素数が minColumnCount 未満の散在ノイズは検出しない") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 0, 32, 10, 11, kRedY, kRedU, kRedV);  // 全列に高さ1の赤ライン
  Config c = smallCfg();                                  // minColumnCount=2
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK_FALSE(d.detected);
}

TEST_CASE("detect: minRedPixels の境界（ちょうどは検出・1未満は非検出）") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 12, 16, 8, 12, kRedY, kRedU, kRedV);  // 4列 x 4行 = 16 画素
  Config c = smallCfg();
  c.minRedPixels = 16;
  CHECK(cone::detect(buf.data(), 32, 24, c).detected);
  c.minRedPixels = 17;
  CHECK_FALSE(cone::detect(buf.data(), 32, 24, c).detected);
}

TEST_CASE("detect: minWidthColumns の境界（ちょうどは検出・超過要求は非検出）") {
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  fillRect(buf, 32, 12, 16, 4, 20, kRedY, kRedU, kRedV);  // 幅4列
  Config c = smallCfg();
  c.minWidthColumns = 4;
  CHECK(cone::detect(buf.data(), 32, 24, c).detected);
  c.minWidthColumns = 5;
  CHECK_FALSE(cone::detect(buf.data(), 32, 24, c).detected);
}

// ---- detect: 異常系 ----

TEST_CASE("detect: 不正入力（null・奇数/非正の幅・非正の高さ）は非検出の安全値") {
  auto buf = makeFrame(8, 4, kRedY, kRedU, kRedV);
  Config c = smallCfg();
  Detection d = cone::detect(nullptr, 8, 4, c);
  CHECK_FALSE(d.detected);
  CHECK(d.centerColumn == -1);
  CHECK_FALSE(cone::detect(buf.data(), 7, 4, c).detected);
  CHECK_FALSE(cone::detect(buf.data(), 0, 4, c).detected);
  CHECK_FALSE(cone::detect(buf.data(), 8, 0, c).detected);
  CHECK_FALSE(cone::detect(buf.data(), 8, -1, c).detected);
  CHECK_FALSE(cone::detect(buf.data(), cone::kMaxDetectWidth + 2, 4, c).detected);
  CHECK_FALSE(cone::detect(buf.data(), 8, cone::kMaxDetectHeight + 1, c).detected);
}

TEST_CASE("detect: 矛盾した Config（yMin>yMax）は全画素不合格となり非検出（安全側）") {
  auto buf = makeFrame(32, 24, kRedY, kRedU, kRedV);  // 全面赤でも
  Config c = smallCfg();
  c.yMin = 200;
  c.yMax = 100;
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK_FALSE(d.detected);
}

TEST_CASE("detect: 不正な Config 数値はクランプ後の値で動く（B19: 派生条件の保証）") {
  // minRedPixels=0 を渡してもクランプ(>=1)され、赤1画素の孤立ノイズでは
  // minColumnCount(クランプ後1) と minWidthColumns(クランプ後1) を満たした場合のみ
  // 検出される。ここでは「クランプにより 0 しきい値で常時検出になる事故」が
  // 起きないことを確認する（背景のみ→非検出のまま）。
  auto buf = makeFrame(32, 24, kBgY, kBgU, kBgV);
  Config c = smallCfg();
  c.minColumnCount = 0;
  c.minWidthColumns = 0;
  c.minRedPixels = 0;
  Detection d = cone::detect(buf.data(), 32, 24, c);
  CHECK_FALSE(d.detected);
}

// ---- 実写ダンプ回帰（実機 cam dump で取得。台帳: test/data/camera/README.md） ----

namespace {

// test/data/camera/ の UYVY ダンプ（QVGA, 153,600B）を読む。失敗時は空を返す。
std::vector<uint8_t> loadDump(const char* name) {
  std::string path = std::string(TEST_DATA_DIR "/camera/") + name;
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> buf(static_cast<size_t>(320) * 240 * 2);
  if (!f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()))) {
    return {};
  }
  return buf;
}

}  // namespace

TEST_CASE(
    "実写(室内・電球色・コーン無し): WB=DAYLIGHT固定の色転びで既定閾値は全面誤検出する"
    "（この光条件の既知の性質。屋外昼光が運用前提。camera_bringup.md 手順B）") {
  auto buf = loadDump("indoor_noCone_room-light_001.yuv");
  REQUIRE(buf.size() == 320u * 240 * 2);
  // 実測: V(Cr) 中央値162・画素の59.3%が vMin=160 を超える（2026-07-19 解析）。
  // 既定閾値では「全幅の赤区間」として誤検出する。この実写での挙動を回帰として固定し、
  // 既定閾値を調整した際に本ケースの期待が変わることで影響に気づけるようにする。
  Detection d = cone::detect(buf.data(), 320, 240, Config{});
  CHECK(d.detected);
  CHECK(d.widthRatio > 0.9);  // ほぼ全幅を「赤区間」と誤認している
}

TEST_CASE(
    "実写(室内・電球色・コーン無し): 室内試験向け vMin=190 なら誤検出しない"
    "（この画像の V 最大は 188。camera_bringup.md の室内試験時の目安値）") {
  auto buf = loadDump("indoor_noCone_room-light_001.yuv");
  REQUIRE(buf.size() == 320u * 240 * 2);
  Config c;
  c.vMin = 190;
  CHECK_FALSE(cone::detect(buf.data(), 320, 240, c).detected);
}
