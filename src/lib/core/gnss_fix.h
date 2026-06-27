#ifndef CORE_GNSS_FIX_H
#define CORE_GNSS_FIX_H

#include <cstdint>

// gnss_fix - GNSS 測位データの妥当性・品質判定（ハードウェア非依存）
//
// ファイル名を gnss_fix.h としているのは、Spresense システムヘッダ <GNSS.h> と
// 大文字小文字を区別しないファイルシステム（macOS 等）で衝突するのを避けるため。
// core/gnss.h だと hal の `#include <GNSS.h>` がこちらへ誤解決される（gotchas C5）。
//
// Spresense 内蔵 GNSS（Issue #10）から得た測位スナップショット（SpNavData 相当）を、
// Spresense GNSS.h に依存せず「走行に使える位置か」を判定する純粋ロジック。
// ホストPCでユニットテストできる（test/core/test_gnss.cpp）。実機の測位取得自体は
// src/lib/hal の Gnss ラッパが担い、得た SpNavData を本モジュールの GnssFix に詰めて判定する。
//
// 役割（Issue #10 DoD のうち HW非依存な部分）:
//   - 緯度経度の物理的な範囲・有限性チェック（化け値を弾く）
//   - 「位置が測位できているか」の判定（posDataExist / posFixMode を安全側にゲート）
//   - 「走行に使える品質か」の判定（HDOP による低精度の排除）
//   - 目標との距離・方位そのものは geo（距離=haversine, 方位=bearing）が担う。
//
// 注意（gotchas A: ターゲット≠ホスト / B: 入力検証）:
//   ここで検証できるのは「与えた測位値を正しくゲートすること」まで。実際の測位の物理的
//   正しさ（屋外でのFIX取得・精度の十分性）は実機（gnss_bringup）で確認する。
//   posDataExist=0 のとき latitude/longitude は不定（前回値や 0 が残りうる）ため、
//   座標を使う前に必ず hasPositionFix() を通すこと。

namespace gnss {

// 緯度経度の物理的な妥当範囲 [deg]（境界は包含）。
constexpr double kMinLatitude = -90.0;
constexpr double kMaxLatitude = 90.0;
constexpr double kMinLongitude = -180.0;
constexpr double kMaxLongitude = 180.0;

// posFixMode の値（SpNavData 準拠, software.md §5.5）。1:Invalid / 2:2D FIX / 3:3D FIX。
constexpr uint8_t kFixInvalid = 1;
constexpr uint8_t kFix2D = 2;
constexpr uint8_t kFix3D = 3;

// HDOP（水平精度劣化指数）の既定上限。小さいほど高精度（理想<=1, 良好<=2, 中<=5）。
// CanSat の数百m走行では緩めに 5.0 を上限とする（実機 gnss_bringup で調整可）。
constexpr float kDefaultMaxHdop = 5.0f;

// HAL（SpNavData）から詰める測位スナップショット（HW非依存表現）。
// 既定値は「測位不能」を表す安全側（posDataExist=false, fixMode=Invalid）。
struct GnssFix {
  bool posDataExist = false;      // 位置データの有無（SpNavData.posDataExist: 0/1）
  uint8_t fixMode = kFixInvalid;  // 1:Invalid / 2:2D / 3:3D（SpNavData.posFixMode）
  uint8_t numSatellites = 0;      // 見えている衛星数（SpNavData.numSatellites）
  double latitude = 0.0;          // 緯度 [deg]（SpNavData.latitude）
  double longitude = 0.0;         // 経度 [deg]（SpNavData.longitude）
  float velocity = 0.0f;          // 対地速度 [m/s]（SpNavData.velocity）
  float direction = 0.0f;         // 進行方位 [deg]（SpNavData.direction）
  float hdop = 0.0f;              // 水平精度劣化指数（SpNavData.hdop）
};

// 緯度経度が物理的に妥当な範囲（境界包含）かつ有限（NaN/Inf でない）か。
// 化けた値・未測位の残骸（Inf 等）を走行制御へ流さないための番兵。
bool isValidCoordinate(double latDeg, double lonDeg);

// 位置が測位できているか（座標を使ってよいか）。
//   posDataExist==true かつ fixMode が 2D/3D かつ 座標が妥当範囲。
// posFixMode は仕様上 1/2/3 のみ。化けた値（4以上）を「FIX」と誤認しないよう
// >= ではなく 2D..3D の範囲一致で判定する（compass::isFullyCalibrated と同じ安全側）。
bool hasPositionFix(const GnssFix& fix);

// 走行に使える品質か。hasPositionFix かつ HDOP が (0, maxHdop] の範囲。
// HDOP==0 は「未取得/無効」を表す典型値（FIX 前に 0 が残りうる）なので高精度と誤認せず弾く。
// hdop が NaN/負値の場合も範囲外として false（>0 が偽になる）。
bool isUsableForNavigation(const GnssFix& fix, float maxHdop = kDefaultMaxHdop);

}  // namespace gnss

#endif  // CORE_GNSS_FIX_H
