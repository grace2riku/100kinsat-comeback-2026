#ifndef CORE_SEPARATOR_H
#define CORE_SEPARATOR_H

#include <cstdint>

#include "gpio.h"

// separator - パラシュート切り離し（電熱線 D06）の加熱シーケンス制御。ハードウェア非依存。
//
// 着地確定（Issue #12）後に電熱線（ニクロム線, D06→FETゲート→6V直結）を一定時間 HIGH にして
// パラシュート保持部を溶断する（hardware.md「電熱線駆動 D06」/ software.md §5.8）。実際のピン
// 書き込みは hal::Gpio に委譲するため、加熱時間の上限・多重起動防止・再試行ポリシーを純粋ロジック
// としてホストPCでユニットテストできる（test/core/test_separator.cpp）。
//
// ⚠️ 安全最優先（過加熱＝発火・電池消耗の防止, Issue #13 DoD）:
//   - 最大加熱時間の上限ガード: update() の累積が heatMs 以上で自動 LOW。加えて設定 heatMs は
//     kMaxHeatMsCap にクランプし、設定ミス（例: 桁間違い）でも加熱が暴走しない。
//   - 多重起動防止: 加熱中の start() は拒否する（二重加熱・タイマ乱れを防ぐ）。
//   - 安全初期化: begin() で D06 を出力かつ LOW にし、起動直後の誤加熱を防ぐ。begin 前 start
//   は拒否。
//   - 緊急停止: abort() でいつでも LOW（安全側）にし、端末状態 Done へ遷移する。
//
// 注意（gotchas A: ターゲット≠ホスト / B5 停止応答性）:
//   ここで検証できるのは「与えた時間経過に対し D06 の HIGH/LOW
//   とタイマ・状態遷移が正しいこと」まで。 実際に溶断できる heatMs
//   は電熱線の抵抗・電圧・保持部材で変わるため実機 bring-up で詰める （defaultSeparatorConfig
//   の値は暫定）。また update() は呼び出し側が周期的に駆動する前提で、 MCU ハング等で update()
//   が止まると D06 が HIGH のままになりうる（ソフト上限が効かない）。この
//   残留リスクはハード側（ワンショット/ウォッチドッグ）で別途手当てする想定（separator_bringup.md）。

namespace separator {

// 加熱シーケンス設定。しきい値は暫定（実機 bring-up で調整。gotchas A）。
struct SeparatorConfig {
  double heatMs;  // 1回の加熱パルス長 [ms]（D06 を HIGH にする時間）。[1, kMaxHeatMsCap] にクランプ
  uint8_t maxAttempts;  // 加熱パルスの総回数上限（再試行ポリシー。1=単発）。1 未満は 1 にクランプ
};

// 安全ハード上限 [ms]: どんな設定でもこの時間を超えて 1
// パルスを加熱しない（過加熱防止の最後の砦）。 heatMs
// はこの値にクランプされる。実機の想定溶断時間（数秒）に対し十分な余裕を持たせた天井。
constexpr double kMaxHeatMsCap = 10000.0;

// 既定設定（暫定）。実機の分離実験で heatMs を詰める。
SeparatorConfig defaultSeparatorConfig();

// heatMs を [1, kMaxHeatMsCap]、maxAttempts を [1, 255]
// にクランプした安全な設定を返す（純粋関数）。
SeparatorConfig sanitizeConfig(const SeparatorConfig& cfg);

// パラシュート切り離しの加熱シーケンス器。start() で 1 パルス開始、update(dt) で時間を進め、
// heatMs 到達で自動的に LOW（加熱停止）する。過加熱防止のため加熱は常にソフト上限で止まる。
class ParachuteSeparator {
 public:
  //   Idle:    非加熱・待機（次の start() を受け付ける）
  //   Heating: 加熱中（D06=HIGH）。この間の start() は拒否（多重起動防止）
  //   Done:    端末（試行回数を使い切った or abort()）。以後 start() は拒否
  enum class State : uint8_t { Idle, Heating, Done };

  // gpio: 出力先（実機=ArduinoGpio / テスト=MockGpio）。pin: 電熱線駆動ピン（D06）。
  ParachuteSeparator(hal::Gpio& gpio, uint8_t pin,
                     const SeparatorConfig& cfg = defaultSeparatorConfig());

  // 安全ネット（RAII）: 破棄時に begin 済みなら D06 を LOW にする。呼び出し側が abort()/update() を
  // 撃ち漏らしても（将来のフライトSM #17 等）、スコープを抜ければ必ず非加熱へ倒れる構造保証。
  // 前提: gpio_ は本オブジェクトより長命（実機=グローバル g_gpio / テスト=先に宣言した MockGpio）。
  ~ParachuteSeparator();

  // D06 を出力に設定し LOW（非加熱）にする。起動直後の誤加熱を防ぐため setup() で必ず呼ぶ。
  void begin();

  // 加熱パルスを開始する。開始できたら true で D06=HIGH・状態=Heating。
  // 拒否して false（副作用なし）: 未 begin / 加熱中（多重起動防止）/ 試行回数を使い切り（Done）。
  bool start();

  // 経過時間 dtMs [ms] を与えて状態を進める（<=0 は経過なしと扱う）。加熱中に累積が heatMs 以上へ
  // 達したら D06 を LOW にして加熱を止める（最大加熱時間の上限ガード）。残り試行があれば Idle、
  // 無ければ Done へ遷移する。Idle/Done では何もしない。
  void update(double dtMs);

  // 緊急停止。いつでも D06 を LOW にし、端末状態 Done へ遷移する（以後 start() 不可・安全側）。
  void abort();

  // ---- 観測用 ----
  State state() const { return state_; }
  bool isHeating() const { return state_ == State::Heating; }
  bool isDone() const { return state_ == State::Done; }
  double heatElapsedMs() const { return heatElapsedMs_; }  // 現パルスの加熱経過 [ms]
  uint8_t attemptsUsed() const { return attemptsUsed_; }   // これまでに開始した加熱パルス回数
  bool hasFired() const { return attemptsUsed_ > 0; }      // 1 回以上加熱したか
  const SeparatorConfig& config() const { return cfg_; }   // クランプ後の実効設定

 private:
  void energize(bool on);  // D06 を HIGH/LOW にする唯一の書き込み点

  hal::Gpio& gpio_;
  uint8_t pin_;
  SeparatorConfig cfg_;  // sanitizeConfig 済み（安全値）
  State state_;
  double heatElapsedMs_;
  uint8_t attemptsUsed_;
  bool begun_;
};

}  // namespace separator

#endif  // CORE_SEPARATOR_H
