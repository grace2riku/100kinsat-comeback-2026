#ifndef CORE_GEO_H
#define CORE_GEO_H

// geo - 測地計算（ハードウェア非依存）
//
// GNSS で得た緯度経度から、目標地点（カラーコーン座標）との
// 距離・方位を求める純粋な計算。Arduino / Spresense API には依存しないため、
// ホストPCでユニットテストできる（test/core/test_geo.cpp）。
//
// ナビゲーション（Issue #10 / #18）の基礎となる。

namespace geo {

// 地球平均半径 [m]（球体近似。CanSat の数百m〜数kmスケールでは十分）
constexpr double kEarthRadiusMeters = 6371000.0;

// 2点間の大圏距離 [m]（haversine 公式）。
// 引数は度 [deg]。同一点なら 0 を返す。距離は向きに依らず対称。
double distanceMeters(double lat1Deg, double lon1Deg,
                      double lat2Deg, double lon2Deg);

// 起点(1)から目標(2)への初期方位 [deg]。
// 北=0、東=90、時計回り。戻り値は 0 <= θ < 360 に正規化する。
// 同一点の場合は 0 を返す。
double bearingDegrees(double lat1Deg, double lon1Deg,
                      double lat2Deg, double lon2Deg);

}  // namespace geo

#endif  // CORE_GEO_H
