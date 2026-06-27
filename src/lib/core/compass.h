#ifndef CORE_COMPASS_H
#define CORE_COMPASS_H

#include <cstdint>

// compass - 方位（ヘディング）まわりの純粋ロジック（ハードウェア非依存）
//
// 9軸センサ BNO055（Issue #9）から得た方位角・キャリブレーション状態を扱うための、
// Arduino/Adafruit ライブラリに依存しない計算・判定。ホストPCでユニットテストできる
// （test/core/test_compass.cpp）。実機の I2C 読み取り自体は src/lib/hal の BNO055
// ラッパが担い、得た生値を本モジュールで正規化・判定する。
//
// 役割（Issue #9 DoD のうち HW非依存な部分）:
//   - 方位角の 0-360 ラップアラウンド正規化（ホストテストで検証）
//   - 2方位間の最短回頭角（符号付き, -180..180）。後段のナビ制御（#18）の基礎
//   - キャリブレーション状態(各0-3)の保持と「校正完了」判定
//   - 無効値（センサ未検出/読み取り失敗）の番兵と判定
//
// 注意（gotchas A: ターゲット≠ホスト）:
//   ここで検証できるのは「与えた値を正しく正規化・判定すること」まで。実際のセンサ
//   読み取り・校正の十分性・方位の物理的正しさは実機（imu_bringup）で確認する。

namespace compass {

// 無効方位を表す番兵値（NaN）。センサ未初期化・読み取り失敗時に hal ラッパが返し、
// 呼び出し側は isValidHeading() で判定する（DoD: 未検出時のエラーハンドリング）。
extern const double kInvalidHeading;

// headingDeg が有効な方位（有限の数値）か。番兵値 kInvalidHeading / NaN は無効。
bool isValidHeading(double headingDeg);

// 任意の角度[deg]を 0 <= θ < 360 に正規化する。
// 負値・360以上・複数周回（±720 等）も畳む。NaN は NaN のまま返す（無効を伝播）。
double normalizeHeading(double deg);

// fromDeg から toDeg へ回頭する最短の符号付き角度[deg]。範囲は (-180, 180]。
// 正 = 方位が増える向き（北0°→東90°… 時計回り）、負 = その逆。ちょうど真裏は +180。
// 例: from=350,to=10 → +20 / from=10,to=350 → -20。
// fromDeg/toDeg いずれかが NaN なら NaN（無効を伝播）。
// ナビ制御（#18）で「目標方位まであと何度どちら回りか」に使う基礎関数。
double shortestDelta(double fromDeg, double toDeg);

// BNO055 Euler ヘディングレジスタ(0x1A=LSB, 0x1B=MSB)の生2バイトを方位[deg, 0..360)へ変換する。
// 1度 = 16 LSB（データシート §3.6.5.4 / Adafruit getVector(VECTOR_EULER).x() と同一換算）。
// lsb=下位, msb=上位 をリトルエンディアンの int16 として合成し、normalizeHeading で [0,360)
// を担保。 HAL（src/lib/hal/bno055）が自前 Wire
// 読みで得た生バイトを渡す。バイト合成の符号/エンディアンは
// ターゲット(32bit)依存の罠になりやすいため、ここで純粋ロジックとしてホストテストする（gotchas
// A）。
double eulerHeadingFromRaw(uint8_t lsb, uint8_t msb);

// BNO055 のキャリブレーション状態。各値 0-3（3=完了。software.md §5.6）。
//   system: 融合系全体 / gyro: ジャイロ / accel: 加速度 / mag: 地磁気
struct CalibrationStatus {
  uint8_t system;
  uint8_t gyro;
  uint8_t accel;
  uint8_t mag;
};

// 4要素の最小値（最も校正の遅れている系のレベル）。表示・進捗の目安に使う。
uint8_t minCalibration(const CalibrationStatus& c);

// 完全キャリブレーション完了（4要素すべて == 3）。software.md §5.6「3 で完全キャリブレーション
// 完了」に対応する、最も安全側のゲート。走行開始の既定条件にはこちらを推奨する。
// 値は仕様上 0-3 だが、化けた値（4以上）を「完了」と扱わないよう >= ではなく == で判定する。
bool isFullyCalibrated(const CalibrationStatus& c);

// 方位用途の暫定ゲート: system==3 && mag==3。方位は地磁気(mag)とシステム融合(system)に強く
// 依存するという経験則による緩めの判定で、isFullyCalibrated より早く true になりうる。
// 注意: §5.6 が規定するのは「全要素3で完全完了」のみで、この per-axis 要件は spec の裏付けが
//       ない独自ヒューリスティック。NDOF では gyro も方位ドリフト補正に効くため、本ゲートが
//       方位精度に十分かは実機（imu_bringup）で gyro 未校正時の方位を確認するまで暫定とする
//       （gotchas A）。確認が済むまでは isFullyCalibrated を既定ゲートにすること。
bool isHeadingReady(const CalibrationStatus& c);

}  // namespace compass

#endif  // CORE_COMPASS_H
