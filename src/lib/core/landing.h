#ifndef CORE_LANDING_H
#define CORE_LANDING_H

#include <cstdint>

// landing - 着地（静止）検知の純粋ロジック（ハードウェア非依存）
//
// 9軸センサ BNO055（Issue #9）の加速度から「降下中 → 着地衝撃 → 静止」を判定する、
// Arduino/Adafruit ライブラリに依存しない計算・状態機械。ホストPCでユニットテストできる
// （test/core/test_landing.cpp）。実機の I2C 読み取りは src/lib/hal の BNO055 ラッパが担い、
// 得た加速度3軸を本モジュールへ逐次与える。パラ分離（#13）・走行開始（Phase3）の前提となる
// 「着地した（静止した）」を確定させる（Issue #12）。
//
// 判定の考え方:
//   - 入力は加速度の「大きさ |a|」（3軸合成）。着地姿勢は不定なので、特定軸に依存しない
//     大きさで扱う（どの向きで転がって止まっても静止と判定できる）。
//   - 静止条件 = 移動窓内の |a| 分散が小さい（ほぼ一定）  かつ  平均 |a| が重力 g(≒9.8)
//     近傍にある。この2条件の併用が肝:
//       * 分散だけだと「自由落下（|a|≒0 が続く＝分散も小）」を静止と誤判定する。
//         → 「平均 |a| が g 近傍」を課して自由落下（無重力状態）を除外する。
//       * 平均だけだと「g 近傍で揺れている（パラ降下中の振り子運動）」を静止と誤判定する。
//         → 「分散が小」を課して揺動を除外する。
//   - 上記の静止条件が requiredStillMs 継続したら「着地」を確定しラッチする。着地衝撃
//     （加速度スパイク）は分散を跳ね上げ継続時間をリセットするので、衝撃の単発では確定しない
//     （着地衝撃と静止の誤判定防止・Issue #12 DoD）。
//
// 注意（gotchas A: ターゲット≠ホスト）:
//   ここで検証できるのは「与えた加速度系列に対しロジックが正しく静止/着地を判定すること」まで。
//   しきい値（分散上限・g 許容・継続時間・窓長）の物理的な妥当性は実機（bring-up）で調整する。
//   defaultStillConfig() の値は暫定で、実機の据置ノイズ・着地衝撃の実測で詰めること。

namespace landing {

// 標準重力 [m/s^2]。静止時、加速度センサの大きさ |a| はこの近傍になる。
extern const double kStandardGravity;

// 固定長リングバッファの容量上限（動的確保をしない＝組込みで安全）。windowSize はこれ以下。
constexpr uint8_t kMaxWindow = 64;

// 加速度3軸 [m/s^2]。BNO055 の加速度ベクトル（重力を含む生の加速度）を想定する。
struct Accel3 {
  double x;
  double y;
  double z;

  // ベクトルの大きさ（ユークリッドノルム）。姿勢に依存しない運動量の指標。
  double magnitude() const;
};

// 静止検知の設定。しきい値は暫定（実機 bring-up で調整。gotchas A）。
struct StillConfig {
  double gravity;          // 静止時の基準 |a| [m/s^2]（既定 kStandardGravity）
  double gTolerance;       // |平均|a| - gravity| の許容 [m/s^2]。これ以内なら「据置」相当
  double maxVariance;      // 窓内 |a| 分散の上限 [(m/s^2)^2]。これ以下なら「揺れ無し」相当
  uint8_t windowSize;      // 移動窓サンプル数（2..kMaxWindow にクランプ）
  double requiredStillMs;  // 静止条件が連続したら着地確定とする時間 [ms]
};

// 既定設定（暫定値）。実機で調整する前提。
StillConfig defaultStillConfig();

// ---- 純粋関数（境界テスト用） ----

// 3軸の大きさ（ユークリッドノルム）。負値・ゼロも扱う。
double vectorMagnitude(double x, double y, double z);

// v[0..n) の平均。n==0 は 0 を返す。
double mean(const double* v, uint8_t n);

// v[0..n) の母分散（平均からの二乗平均）。n==0 は 0 を返す。非負。
double variance(const double* v, uint8_t n);

// BNO055 加速度データレジスタの生2バイト(lsb=下位, msb=上位)を加速度成分 [m/s^2] へ変換する。
// 1 m/s^2 = 100 LSB（既定の m/s^2 単位モード。データシート §3.6.5.5 / Adafruit の
// getVector(VECTOR_ACCELEROMETER) と同一換算）。リトルエンディアンの符号付き int16 として合成する。
// バイト合成の符号拡張/エンディアンはターゲット(32bit)依存の罠になりやすいため、ここで純粋
// ロジックとしてホストテストする（gotchas A）。HAL（src/lib/hal/bno055）が自前 Wire 読みで得た
// 生バイトを渡す。
double accelFromRaw(uint8_t lsb, uint8_t msb);

// 着地（静止）検知器。加速度サンプルを update() で逐次与え、静止条件が requiredStillMs
// 継続したら hasLanded() が true にラッチする。ラッチ後は motion が再開しても true を保持
// （着地は一度確定したらミッションを次段へ進めるため）。reset() で初期化する。
class LandingDetector {
 public:
  explicit LandingDetector(const StillConfig& cfg = defaultStillConfig());

  // 窓・継続時間・ラッチを初期化する（設定は保持）。
  void reset();

  // 加速度1サンプルを投入する。dtMs は前サンプルからの経過時間 [ms]（<=0 は時間経過なしと扱う）。
  // 窓が満杯かつ静止条件を満たす間だけ継続時間を積算し、requiredStillMs に達したら着地確定。
  void update(const Accel3& accel, double dtMs);

  // 移動窓が windowSize 個で満たされたか（満たされるまで isStillNow は false）。
  bool windowFull() const;

  // 現在の窓内平均 |a|（充填済みサンプルに対して。空なら 0）。
  double meanMagnitude() const;

  // 現在の窓内分散（充填済みサンプルに対して。空なら 0）。
  double variance() const;

  // 瞬間の静止条件を満たすか（窓満杯 && 分散<=maxVariance && |平均-gravity|<=gTolerance）。
  bool isStillNow() const;

  // 静止条件が連続している時間 [ms]（条件が崩れると 0 に戻る）。
  double stillElapsedMs() const;

  // 着地確定（静止が requiredStillMs 継続した）。一度 true になると reset() まで保持。
  bool hasLanded() const;

 private:
  StillConfig cfg_;
  double buf_[kMaxWindow];  // |a| のリングバッファ
  uint8_t count_;           // 充填済みサンプル数（0..windowSize）
  uint8_t head_;            // 次に書き込む位置
  double stillElapsedMs_;
  bool wasStill_;  // 直前サンプルが静止条件を満たしていたか（継続時間の起点判定用）
  bool landed_;
};

}  // namespace landing

#endif  // CORE_LANDING_H
