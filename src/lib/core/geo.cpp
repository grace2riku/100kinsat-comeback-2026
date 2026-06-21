#include "geo.h"

#include <cmath>

namespace {

// 度 → ラジアン（M_PI への依存を避けて自前定義）
constexpr double kPi = 3.14159265358979323846;
inline double toRad(double deg) { return deg * kPi / 180.0; }
inline double toDeg(double rad) { return rad * 180.0 / kPi; }

}  // namespace

namespace geo {

double distanceMeters(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg) {
  const double phi1 = toRad(lat1Deg);
  const double phi2 = toRad(lat2Deg);
  const double dPhi = toRad(lat2Deg - lat1Deg);
  const double dLambda = toRad(lon2Deg - lon1Deg);

  const double sinDPhi = std::sin(dPhi / 2.0);
  const double sinDLambda = std::sin(dLambda / 2.0);
  const double a = sinDPhi * sinDPhi + std::cos(phi1) * std::cos(phi2) * sinDLambda * sinDLambda;
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusMeters * c;
}

double bearingDegrees(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg) {
  const double phi1 = toRad(lat1Deg);
  const double phi2 = toRad(lat2Deg);
  const double dLambda = toRad(lon2Deg - lon1Deg);

  const double y = std::sin(dLambda) * std::cos(phi2);
  const double x =
      std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dLambda);
  const double theta = std::atan2(y, x);  // -π..π

  // 0 <= θ < 360 に正規化（負値を畳む）
  return std::fmod(toDeg(theta) + 360.0, 360.0);
}

}  // namespace geo
