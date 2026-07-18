#ifndef CORE_RELEASE_DETECT_H
#define CORE_RELEASE_DETECT_H

#include <cstdint>

// release_detect - 放出（上空放出）検知の純粋ロジック（ハードウェア非依存）。
//
// CdS 照度センサ（GL5528, A0）の明るさ変化から「放出機構内（暗）→ 上空放出（明）」を検知し、
// ミッション開始トリガにする（Issue #11 / software.md §5.2）。実機の analogRead は src/shell の
// HAL 側が担い、得たアナログ生値（0..kAdcMax）を本モジュールへ逐次与える。ホストPCでユニット
// テストできる（test/core/test_release_detect.cpp）。放出検知はロギング開始（#14）・ミッション
// ステートマシン（#17）の起点になる。
//
// ⚠️ タイマ起動を避ける（ルール推奨）方針の反映（Issue #11 DoD）:
//   放出の判定は「打上げから N 秒後に自動で開始」（タイマ起動）ではなく、照度センサが実測した
//   明るさ変化を確定させる**センサベース検知**で行う。requiredMs は「放出側の明るさが連続した
//   ことの確認時間」であって、時間経過そのものを放出とみなすものではない。これによりタイマ起動
//   （放出前に走り出す／放出を取りこぼす事故）を避ける。
//
// 判定の考え方（チャタリング耐性を二段で持つ）:
//   1) 極性（polarity）: CdS は分圧の向きで「明→生値が小さい／暗→生値が大きい」のどちらにもなる
//      （software.md §5.2「分圧の向きに依存」）。放出後が明側(Brighter)か暗側(Darker)かを設定で
//      切り替える。既定は Brighter（放出後は明るい＝生値が小さい）だが、実機の分圧向きで確定する。
//   2) ヒステリシス（hysteresis）: 放出側条件の ON/OFF に別しきい値を使い、閾値ちょうど付近の微振動
//      で条件がばたつくのを防ぐ。ON になった後は復帰しきい値を跨ぐまで ON を維持する。
//   3) 継続時間（requiredMs）: ヒステリシス込みの放出側条件が requiredMs 連続したら放出確定しラッチ
//      する。落雷的な閃光・電気ノイズの単発は継続時間に達せず弾かれる（誤検知防止）。
//
// 注意（gotchas A: ターゲット≠ホスト）:
//   ここで検証できるのは「与えた生値系列に対しロジックが正しく放出/未放出を判定すること」まで。
//   しきい値（threshold・hysteresis・requiredMs・polarity）の物理的妥当性は実機（bring-up）で調整
//   する。defaultReleaseConfig() の値は暫定で、装置内／上空の実測生値の中点などで詰めること
//   （release_detect_bringup.md のキャリブレーション手順）。

namespace release_detect {

// ADC 生値の上限。Spresense/Arduino の analogRead は 10bit = 0..1023（software.md §5.2）。
// 範囲外の異常入力（負値・上限超過）は update() でこの範囲へクランプする。
constexpr int kAdcMax = 1023;

// 放出後（released）とみなす明るさの向き。CdS の分圧の向きで決まる（software.md §5.2）。
//   Brighter: 放出後は明るい＝生値が小さい（raw <= threshold で放出側）
//   Darker  : 放出後は暗い  ＝生値が大きい（raw >= threshold で放出側）
enum class Polarity : uint8_t { Brighter, Darker };

// 放出検知の設定。しきい値は暫定（実機 bring-up で調整。gotchas A）。
struct ReleaseConfig {
  int threshold;      // 放出側と判定する主しきい値（生値 0..kAdcMax）
  int hysteresis;     // ヒステリシス幅（生値, >=0）。ON→OFF 復帰はこの分だけ手前でチャタリング吸収
  Polarity polarity;  // 放出後が明側(Brighter)か暗側(Darker)か
  double requiredMs;  // 放出側条件が連続したら放出確定とする時間 [ms]（>=0）
};

// 既定設定（暫定値）。実機で装置内／上空の生値を測り調整する前提。
ReleaseConfig defaultReleaseConfig();

// threshold を [0, kAdcMax]、requiredMs を [0, ∞)（NaN は 0）にクランプし、さらに hysteresis を
// 実効 OFF(復帰) しきい値（threshold ± hysteresis）が ADC レンジ内に収まる範囲へクランプした
// 安全な設定を返す（純粋関数）。設定ミス（桁間違い・負値・NaN）でも破綻しない。
//   ※ 復帰しきい値がレンジ外（例 Brighter で threshold+hysteresis > kAdcMax）だと復帰条件が到達
//     不能になり「一度 ON→復帰不能」でヒステリシス帯が無限化し、単発ノイズを誤ラッチする（gotchas
//     B19）。
ReleaseConfig sanitizeConfig(const ReleaseConfig& cfg);

// 生値の素の閾値判定（ヒステリシスなし。閾値の境界テスト用の純粋関数）。
//   Brighter: raw <= threshold なら放出側（明るい）
//   Darker  : raw >= threshold なら放出側（暗い）
bool isReleasedLevel(int raw, int threshold, Polarity polarity);

// 放出検知器。照度センサ生値を update() で逐次与え、ヒステリシス込みの放出側条件が requiredMs
// 継続したら hasReleased() が true にラッチする。ラッチ後は暗へ戻っても true を保持（放出は一度
// 確定したらミッションを次段へ進めるため）。reset() で初期化する。
//
// アーミング（暗→明の「遷移」を要求する安全機構）:
//   ラッチには「反対状態＝OFF(暗) を一度観測したこと」を前提とする（armed）。放出検知の本質は
//   「装置内(暗)→上空(明)への遷移」なので、暗を経ずに明側が続くだけでは確定させない。これにより
//   (1) 電源投入時に既に明るい（遮光漏れ・早期放出）、(2) 断線で生値が明側へ張り付く、といった
//   ケースの不可逆な誤ラッチを安全側に弾く（release_detect_bringup.md §0 の断線リスク対策）。
class ReleaseDetector {
 public:
  explicit ReleaseDetector(const ReleaseConfig& cfg = defaultReleaseConfig());

  // 継続時間・ヒステリシス状態・ラッチを初期化する（設定は保持）。
  void reset();

  // 照度センサ生値を1サンプル投入する。raw は [0, kAdcMax] にクランプして扱う。dtMs は前サンプル
  // からの経過時間 [ms]（<=0 は時間経過なしと扱う）。ヒステリシス込みの放出側条件が連続する間だけ
  // 継続時間を積算し、requiredMs に達したら放出確定。
  void update(int raw, double dtMs);

  // ヒステリシス込みの瞬間の放出側条件を満たすか（ラッチではない現在状態）。
  bool isReleasedNow() const;

  // 放出側条件が連続している時間 [ms]（条件が崩れると 0 に戻る）。
  double releasedElapsedMs() const;

  // 放出確定（アーム済み かつ 放出側条件が requiredMs 継続した）。一度 true になると reset()
  // まで保持。
  bool hasReleased() const;

  // アーム済みか（反対状態＝OFF(暗) を一度観測したか）。false の間はラッチしない（観測・表示用）。
  bool isArmed() const;

  // 直近に投入したクランプ済み生値（観測・表示用）。未投入時は -1。
  int lastRaw() const;

  // クランプ後の実効設定。
  const ReleaseConfig& config() const { return cfg_; }

 private:
  // ヒステリシス込みの OFF(復帰) 側しきい値を跨いだか（極性依存）。ON 側は isReleasedLevel を使う。
  bool isOffLevel(int raw) const;

  ReleaseConfig cfg_;  // sanitizeConfig 済み（安全値）
  int lastRaw_;        // 直近のクランプ済み生値（-1=未投入）
  bool conditionOn_;   // ヒステリシス込みの放出側条件が ON か
  double elapsedMs_;   // conditionOn_ が連続している時間 [ms]
  bool wasOn_;         // 直前サンプルが ON だったか（継続時間の起点判定用）
  bool armed_;         // 反対状態(暗=OFF) を一度観測したか（暗→明の遷移を要求する安全機構）
  bool released_;      // 放出確定ラッチ
};

}  // namespace release_detect

#endif  // CORE_RELEASE_DETECT_H
