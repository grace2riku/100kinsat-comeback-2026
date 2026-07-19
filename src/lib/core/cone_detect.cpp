// cone_detect - 赤カラーコーン検出（実装）
// 設計・入力フォーマット（UYVY バイト順）の詳細は cone_detect.h を参照。

#include "cone_detect.h"

#include <cmath>

namespace cone {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultHfovDeg = 66.0;

// 入力バッファ・寸法の妥当性（UYVY は2画素単位なので幅は偶数のみ）。
bool isValidFrame(const uint8_t* uyvy, int width, int height) {
  if (uyvy == nullptr) return false;
  if (width <= 0 || width > kMaxDetectWidth || (width % 2) != 0) return false;
  if (height <= 0 || height > kMaxDetectHeight) return false;
  return true;
}

}  // namespace

Config sanitizeConfig(const Config& cfg) {
  Config s = cfg;
  if (s.minColumnCount < 1) s.minColumnCount = 1;
  if (s.maxGapColumns < 0) s.maxGapColumns = 0;
  if (s.minWidthColumns < 1) s.minWidthColumns = 1;
  if (s.minRedPixels < 1) s.minRedPixels = 1;
  if (!std::isfinite(s.hfovDeg) || s.hfovDeg <= 0.0 || s.hfovDeg >= 180.0) {
    s.hfovDeg = kDefaultHfovDeg;
  }
  return s;
}

bool isRedYuv(uint8_t y, uint8_t u, uint8_t v, const Config& cfg) {
  return y >= cfg.yMin && y <= cfg.yMax && u <= cfg.uMax && v >= cfg.vMin;
}

bool buildColumnHistogram(const uint8_t* uyvy, int width, int height, const Config& cfg,
                          uint16_t* histogram) {
  if (!isValidFrame(uyvy, width, height) || histogram == nullptr) return false;

  for (int col = 0; col < width; ++col) {
    histogram[col] = 0;
  }
  // UYVY: 2画素4バイト [U0, Y0, V0, Y1]。U/V はペアの2画素で共有する。
  for (int row = 0; row < height; ++row) {
    const uint8_t* line = uyvy + static_cast<size_t>(row) * width * 2;
    for (int pair = 0; pair < width / 2; ++pair) {
      const uint8_t* px = line + pair * 4;
      const uint8_t u = px[0];
      const uint8_t v = px[2];
      if (isRedYuv(px[1], u, v, cfg)) {
        ++histogram[pair * 2];
      }
      if (isRedYuv(px[3], u, v, cfg)) {
        ++histogram[pair * 2 + 1];
      }
    }
  }
  return true;
}

double columnToBearingDeg(double columnCenter, int width, double hfovDeg) {
  const double half = width / 2.0;
  const double ratio = (columnCenter - half) / half;
  const double tanHalfFov = std::tan(hfovDeg / 2.0 * kPi / 180.0);
  return std::atan(ratio * tanHalfFov) * 180.0 / kPi;
}

Detection detect(const uint8_t* uyvy, int width, int height, const Config& cfg) {
  Detection best;  // detected=false の安全値
  const Config c = sanitizeConfig(cfg);

  uint16_t histogram[kMaxDetectWidth];
  if (!buildColumnHistogram(uyvy, width, height, c, histogram)) return best;

  // アクティブ列（赤画素数 >= minColumnCount）を maxGapColumns までギャップ許容で
  // 連結した区間を左から走査し、しきい値（幅・赤画素数）を満たす区間の中で
  // 赤画素数が最大のもの（同数なら左）を選ぶ。
  // 赤画素数の最大値は kMaxDetectWidth*height だが height は int なので、
  // QVGA 実寸(320x240=76800)では int(32bit) で桁あふれしない。
  int bestPixels = -1;
  int start = -1;       // 現在の区間の開始列
  int lastActive = -1;  // 現在の区間で最後にアクティブだった列

  auto closeSegment = [&](int endCol) {
    // 区間 [start, endCol] を確定し、候補なら best を更新する。
    const int widthColumns = endCol - start + 1;
    int pixels = 0;
    double moment = 0.0;  // Σ (列中心 i+0.5) * hist[i]
    for (int i = start; i <= endCol; ++i) {
      pixels += histogram[i];
      moment += (i + 0.5) * histogram[i];
    }
    if (widthColumns < c.minWidthColumns || pixels < c.minRedPixels) return;
    if (pixels <= bestPixels) return;
    bestPixels = pixels;
    const double centroid = moment / static_cast<double>(pixels);
    best.detected = true;
    best.bearingDeg = columnToBearingDeg(centroid, width, c.hfovDeg);
    best.centerColumn = static_cast<int>(centroid + 0.5);
    best.widthColumns = widthColumns;
    best.widthRatio = static_cast<double>(widthColumns) / width;
    best.redPixels = pixels;
    best.confidence = static_cast<double>(pixels) / (static_cast<double>(widthColumns) * height);
  };

  for (int col = 0; col < width; ++col) {
    if (histogram[col] < c.minColumnCount) continue;
    if (start >= 0 && (col - lastActive - 1) <= c.maxGapColumns) {
      lastActive = col;  // ギャップ許容内: 区間を継続
      continue;
    }
    if (start >= 0) closeSegment(lastActive);  // ギャップ超過: 前の区間を確定
    start = col;
    lastActive = col;
  }
  if (start >= 0) closeSegment(lastActive);

  return best;
}

}  // namespace cone
