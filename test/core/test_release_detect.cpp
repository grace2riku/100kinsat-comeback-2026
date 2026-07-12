// release_detect（放出=上空放出の検知）の単体テスト
//
// CdS 照度センサ（A0）の明るさ変化「放出機構内(暗)→上空放出(明)」を、極性・ヒステリシス・
// 継続時間で判定するロジックを、正常系だけでなく境界・異常系まで網羅して検証する:
//   - 純粋関数 isReleasedLevel（極性ごとの閾値境界 <= / >=）
//   - sanitizeConfig のクランプ（負値・桁間違い・NaN）
//   - Brighter 極性: 暗→明の継続で放出確定（ラッチ）
//   - Darker 極性: 明→暗の継続で放出確定（DoD「明→暗」異常系）
//   - チャタリング（閾値付近の振動）で誤検知しないこと
//   - ヒステリシスが放出側に入った後の微振動を吸収すること
//   - 単発の閃光では確定しないこと（継続時間リセット）
//   - ラッチ後は暗へ戻っても保持、reset で初期化
//   - dtMs<=0 / 生値の範囲外入力
// 実際のしきい値の物理的妥当性は実機（bring-up）で確認する範囲外。

#include <cmath>

#include "doctest.h"
#include "release_detect.h"

using release_detect::Polarity;
using release_detect::ReleaseConfig;
using release_detect::ReleaseDetector;

// テスト用の決定的な設定: 閾値500・ヒステリシス20・放出条件が100ms継続で確定。
static ReleaseConfig brighterCfg() {
  ReleaseConfig c;
  c.threshold = 500;
  c.hysteresis = 20;
  c.polarity = Polarity::Brighter;  // 放出後は明るい＝生値が小さい
  c.requiredMs = 100.0;
  return c;
}

static ReleaseConfig darkerCfg() {
  ReleaseConfig c = brighterCfg();
  c.polarity = Polarity::Darker;  // 放出後は暗い＝生値が大きい
  return c;
}

// 同じ生値を n 回、dtMs 周期で投入する。
static void feed(ReleaseDetector& d, int raw, int n, double dtMs) {
  for (int i = 0; i < n; ++i) {
    d.update(raw, dtMs);
  }
}

// ---- 純粋関数 isReleasedLevel（極性ごとの境界） ----

TEST_CASE("isReleasedLevel(Brighter): raw<=threshold が放出側（境界は放出に含む）") {
  CHECK(release_detect::isReleasedLevel(499, 500, Polarity::Brighter));
  CHECK(release_detect::isReleasedLevel(500, 500, Polarity::Brighter));  // 境界ちょうど
  CHECK_FALSE(release_detect::isReleasedLevel(501, 500, Polarity::Brighter));
}

TEST_CASE("isReleasedLevel(Darker): raw>=threshold が放出側（境界は放出に含む）") {
  CHECK(release_detect::isReleasedLevel(501, 500, Polarity::Darker));
  CHECK(release_detect::isReleasedLevel(500, 500, Polarity::Darker));  // 境界ちょうど
  CHECK_FALSE(release_detect::isReleasedLevel(499, 500, Polarity::Darker));
}

// ---- sanitizeConfig（クランプ・異常設定） ----

TEST_CASE("sanitizeConfig: threshold は [0, kAdcMax] にクランプ") {
  ReleaseConfig c = brighterCfg();
  c.threshold = -100;
  CHECK(release_detect::sanitizeConfig(c).threshold == 0);
  c.threshold = 5000;
  CHECK(release_detect::sanitizeConfig(c).threshold == release_detect::kAdcMax);
}

TEST_CASE(
    "sanitizeConfig: hysteresis の負値は 0 にクランプ。上限は kAdcMax（threshold=0 のとき）") {
  ReleaseConfig c = brighterCfg();
  c.hysteresis = -5;
  CHECK(release_detect::sanitizeConfig(c).hysteresis == 0);
  // threshold=0 の Brighter では復帰しきい値 = 0+hysteresis なので上限は kAdcMax まで許容される。
  c.threshold = 0;
  c.hysteresis = 9999;
  CHECK(release_detect::sanitizeConfig(c).hysteresis == release_detect::kAdcMax);
}

TEST_CASE("sanitizeConfig: requiredMs は負値・NaN を 0 にクランプ") {
  ReleaseConfig c = brighterCfg();
  c.requiredMs = -50.0;
  CHECK(release_detect::sanitizeConfig(c).requiredMs == doctest::Approx(0.0));
  c.requiredMs = std::nan("");
  CHECK(release_detect::sanitizeConfig(c).requiredMs == doctest::Approx(0.0));
}

// ---- 初期状態 ----

TEST_CASE("初期状態: 未放出・未アーム・条件OFF・継続0・生値未投入(-1)") {
  ReleaseDetector d(brighterCfg());
  CHECK_FALSE(d.hasReleased());
  CHECK_FALSE(d.isArmed());
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK(d.lastRaw() == -1);
}

// ---- Brighter: 暗→明の継続で放出確定（ラッチ） ----

TEST_CASE("Brighter: 暗(大値)のうちは放出しない") {
  ReleaseDetector d(brighterCfg());  // threshold=500
  feed(d, 800, 5, 50.0);             // ずっと暗い
  CHECK_FALSE(d.isReleasedNow());
  CHECK_FALSE(d.hasReleased());
  CHECK(d.lastRaw() == 800);
}

TEST_CASE("Brighter: 明(小値)が継続時間に達すると放出確定しラッチする") {
  ReleaseDetector d(brighterCfg());  // requiredMs=100
  d.update(800, 0.0);                // 暗（放出前）
  CHECK_FALSE(d.isReleasedNow());
  d.update(200, 50.0);  // 明るくなった直後＝放出側条件 ON だが継続の起点(0)
  CHECK(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasReleased());
  d.update(200, 50.0);  // +50ms
  CHECK(d.releasedElapsedMs() == doctest::Approx(50.0));
  CHECK_FALSE(d.hasReleased());
  d.update(200, 50.0);  // +50ms = 100ms 到達
  CHECK(d.releasedElapsedMs() == doctest::Approx(100.0));
  CHECK(d.hasReleased());
}

TEST_CASE("放出確定後は暗へ戻ってもラッチを保持する") {
  ReleaseDetector d(brighterCfg());
  d.update(800, 0.0);
  feed(d, 200, 3, 50.0);  // 明るさ継続で確定
  REQUIRE(d.hasReleased());
  feed(d, 900, 4, 50.0);  // 再び暗く（機構陰・雲）→ 瞬間条件は落ちるがラッチ保持
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.hasReleased());
}

TEST_CASE("reset で継続時間・ヒステリシス状態・ラッチが初期化される") {
  ReleaseDetector d(brighterCfg());
  d.update(800, 0.0);
  feed(d, 200, 3, 50.0);
  REQUIRE(d.hasReleased());
  d.reset();
  CHECK_FALSE(d.hasReleased());
  CHECK_FALSE(d.isArmed());
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK(d.lastRaw() == -1);
}

// ---- Darker: 明→暗の継続で放出確定（DoD「明→暗」の異常系＝分圧が逆向きのHW） ----

TEST_CASE("Darker: 明(小値)→暗(大値)が継続時間に達すると放出確定") {
  ReleaseDetector d(darkerCfg());  // 放出後は暗い＝生値が大きい
  d.update(100, 0.0);              // 明（放出前）
  CHECK_FALSE(d.isReleasedNow());
  feed(d, 900, 1, 50.0);  // 暗くなった直後＝ON 起点
  CHECK(d.isReleasedNow());
  CHECK_FALSE(d.hasReleased());
  feed(d, 900, 2, 50.0);  // 100ms 継続で確定
  CHECK(d.hasReleased());
}

// ---- 誤検知防止（DoD の核心） ----

TEST_CASE("チャタリング: 閾値付近で明暗が振動しても継続時間が溜まらず放出しない") {
  ReleaseDetector d(brighterCfg());  // threshold=500, hysteresis=20
  // ON側(<=500)とOFF側(>=520)を交互に跨ぐ＝ヒステリシス帯も越える純粋な振動。
  // ON になっても次サンプルで OFF に落ち、継続時間が 0 リセットされ続ける。
  for (int i = 0; i < 10; ++i) {
    d.update(495, 50.0);  // 明側（ON 起点、継続は積まれない）
    d.update(600, 50.0);  // 暗側（OFF、継続リセット）
  }
  CHECK_FALSE(d.hasReleased());
}

TEST_CASE("単発の閃光では放出確定しない（継続時間リセット）") {
  ReleaseDetector d(brighterCfg());
  feed(d, 800, 2, 50.0);  // 暗
  d.update(100, 50.0);    // 一瞬の閃光（ON 起点=0）
  CHECK(d.isReleasedNow());
  CHECK_FALSE(d.hasReleased());
  feed(d, 800, 4, 50.0);  // すぐ暗へ → OFF、継続リセット
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasReleased());
}

TEST_CASE("ヒステリシス: 放出側に入った後、帯内(threshold..threshold+hys)の揺れでは条件を維持") {
  ReleaseDetector d(brighterCfg());  // threshold=500, hysteresis=20 → 復帰は raw>=520
  d.update(800, 0.0);                // 暗（OFF）
  d.update(490, 50.0);               // 明側 ON（起点）
  CHECK(d.isReleasedNow());
  // ヒステリシス帯（501..519）に上がっても、復帰しきい値520未満なので ON を維持し継続が積まれる
  d.update(510, 50.0);  // 帯内。ON 維持、+50ms
  CHECK(d.isReleasedNow());
  d.update(515, 50.0);  // 帯内。+50ms = 100ms 到達
  CHECK(d.hasReleased());
}

TEST_CASE("ヒステリシス: 復帰しきい値(threshold+hys)以上に戻ると条件OFFへ復帰する") {
  ReleaseDetector d(brighterCfg());  // 復帰は raw>=520
  d.update(800, 0.0);
  d.update(490, 50.0);  // ON
  CHECK(d.isReleasedNow());
  d.update(520, 50.0);  // 復帰しきい値ちょうど → OFF
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
}

// ---- 境界値 ----

TEST_CASE("継続時間ちょうど(>=)で確定する。最初のON サンプルは0起点") {
  ReleaseDetector d(brighterCfg());  // requiredMs=100
  d.update(800, 0.0);                // OFF
  d.update(100, 50.0);               // ON 起点=0
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasReleased());
  d.update(100, 50.0);  // +50ms
  CHECK_FALSE(d.hasReleased());
  d.update(100, 50.0);  // +50ms = 100ms ちょうど
  CHECK(d.releasedElapsedMs() == doctest::Approx(100.0));
  CHECK(d.hasReleased());  // 継続時間ちょうどで確定（>=）
}

TEST_CASE("閾値ちょうどの生値は放出側に含める（Brighter: raw==threshold）") {
  ReleaseDetector d(brighterCfg());  // threshold=500
  d.update(800, 0.0);
  d.update(500, 50.0);  // ちょうど閾値 → ON
  CHECK(d.isReleasedNow());
}

TEST_CASE("hysteresis=0 でも閾値境界で ON/OFF が破綻しない") {
  ReleaseConfig c = brighterCfg();
  c.hysteresis = 0;
  ReleaseDetector d(c);
  d.update(500, 0.0);  // ちょうど閾値 → ON（<=）
  CHECK(d.isReleasedNow());
  d.update(501, 50.0);  // 閾値超過 → OFF（復帰しきい値=threshold+0=500, raw>=500 は... 501で復帰）
  CHECK_FALSE(d.isReleasedNow());
}

// ---- 異常入力 ----

TEST_CASE("生値は [0, kAdcMax] にクランプされる（範囲外の異常入力）") {
  ReleaseDetector d(brighterCfg());
  d.update(-100, 0.0);  // 負値 → 0 にクランプ（Brighter では明側=ON）
  CHECK(d.lastRaw() == 0);
  CHECK(d.isReleasedNow());
  d.reset();
  d.update(5000, 0.0);  // 上限超過 → kAdcMax にクランプ（暗側=OFF）
  CHECK(d.lastRaw() == release_detect::kAdcMax);
  CHECK_FALSE(d.isReleasedNow());
}

TEST_CASE("dtMs<=0 は時間経過なしとして継続時間を進めない") {
  ReleaseDetector d(brighterCfg());
  d.update(800, 0.0);
  feed(d, 100, 5, 0.0);  // ON だが dt=0
  CHECK(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasReleased());
  feed(d, 100, 5, -50.0);  // 逆行時刻
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
  CHECK_FALSE(d.hasReleased());
}

TEST_CASE("requiredMs=0 でも、暗(OFF)観測後に ON になった最初のサンプルで即確定する") {
  ReleaseConfig c = brighterCfg();
  c.requiredMs = 0.0;
  ReleaseDetector d(c);
  d.update(800, 0.0);  // 暗を観測（arm）
  d.update(100, 0.0);  // ON、継続0 >= 0 で即確定
  CHECK(d.hasReleased());
}

// ---- アーミング（暗→明の遷移を要求する安全機構。電源投入時点灯・断線 stuck-at-bright 対策） ----

TEST_CASE("アーミング: 暗(OFF)を一度も観測せず明が続くだけでは放出確定しない") {
  ReleaseDetector d(brighterCfg());  // requiredMs=100
  // 起動直後から明側（暗を一度も見ない）。継続時間は溜まるがアーム前なのでラッチしない。
  feed(d, 100, 10, 50.0);
  CHECK(d.isReleasedNow());
  CHECK_FALSE(d.isArmed());
  CHECK(d.releasedElapsedMs() > 100.0);  // 継続時間自体は積み上がっている
  CHECK_FALSE(d.hasReleased());          // が、未アームなので確定しない（誤ラッチ防止）
}

TEST_CASE("アーミング: 暗(OFF)を観測後に明が継続時間続けば放出確定する") {
  ReleaseDetector d(brighterCfg());
  feed(d, 100, 5, 50.0);  // 先に明が続いてもラッチしない（未アーム）
  REQUIRE_FALSE(d.hasReleased());
  d.update(800, 50.0);  // 暗を観測 → arm
  CHECK(d.isArmed());
  CHECK_FALSE(d.isReleasedNow());
  feed(d, 100, 3, 50.0);  // 明が 100ms 継続 → 確定
  CHECK(d.hasReleased());
}

// ---- Darker 極性（分圧逆向き HW。DoD「明→暗」異常系）のヒステリシス・チャタリング ----

TEST_CASE("Darker: ヒステリシス帯内(threshold-hys..threshold)の揺れでは放出側条件を維持") {
  ReleaseDetector d(darkerCfg());  // threshold=500, hys=20 → 復帰は raw<=480
  d.update(100, 0.0);              // 明（放出前=OFF）→ arm
  d.update(510, 50.0);             // 暗側 ON（起点）
  CHECK(d.isReleasedNow());
  // 帯内(481..499)に下がっても復帰しきい値480以下でないので ON を維持し継続が積まれる
  d.update(490, 50.0);  // 帯内。ON 維持、+50ms
  CHECK(d.isReleasedNow());
  d.update(485, 50.0);  // 帯内。+50ms = 100ms 到達
  CHECK(d.hasReleased());
}

TEST_CASE("Darker: 復帰しきい値(threshold-hys)以下に戻ると条件OFFへ復帰する") {
  ReleaseDetector d(darkerCfg());  // 復帰は raw<=480
  d.update(100, 0.0);              // arm
  d.update(510, 50.0);             // ON
  CHECK(d.isReleasedNow());
  d.update(480, 50.0);  // 復帰しきい値ちょうど → OFF
  CHECK_FALSE(d.isReleasedNow());
  CHECK(d.releasedElapsedMs() == doctest::Approx(0.0));
}

TEST_CASE("Darker: 閾値付近のチャタリングでは継続時間が溜まらず放出しない") {
  ReleaseDetector d(darkerCfg());  // threshold=500, hys=20
  for (int i = 0; i < 10; ++i) {
    d.update(505, 50.0);  // 暗側 ON（起点）
    d.update(400, 50.0);  // 明側（復帰しきい値480以下）→ OFF、継続リセット
  }
  CHECK_FALSE(d.hasReleased());
}

// ---- sanitizeConfig: 実効 OFF しきい値のレンジ保証（gotchas B19。復帰不能→誤ラッチ防止） ----

TEST_CASE("sanitizeConfig: Brighter で threshold+hysteresis がレンジ超過なら hysteresis を詰める") {
  ReleaseConfig c = brighterCfg();
  c.polarity = Polarity::Brighter;
  c.threshold = 1000;
  c.hysteresis = 100;  // 1000+100=1100 > kAdcMax(1023)
  ReleaseConfig s = release_detect::sanitizeConfig(c);
  CHECK(s.hysteresis == release_detect::kAdcMax - 1000);         // = 23
  CHECK(s.threshold + s.hysteresis <= release_detect::kAdcMax);  // 復帰しきい値はレンジ内
}

TEST_CASE(
    "sanitizeConfig: Darker で threshold-hysteresis が負なら hysteresis を threshold へ詰める") {
  ReleaseConfig c = darkerCfg();
  c.polarity = Polarity::Darker;
  c.threshold = 50;
  c.hysteresis = 200;  // 50-200=-150 < 0
  ReleaseConfig s = release_detect::sanitizeConfig(c);
  CHECK(s.hysteresis == 50);               // threshold へクランプ
  CHECK(s.threshold - s.hysteresis >= 0);  // 復帰しきい値はレンジ内
}

TEST_CASE(
    "復帰しきい値がレンジ内へ収まり、最大生値で確実にOFF復帰する（ヒステリシス帯無限化の防止）") {
  ReleaseConfig c = brighterCfg();
  c.threshold = 1000;
  c.hysteresis =
      100;  // sanitize 前は復帰 1100（到達不能）。コンストラクタの sanitize で 23 に詰まる
  ReleaseDetector d(c);  // 実効復帰しきい値 = 1000+23 = 1023 <= kAdcMax
  d.update(500, 0.0);    // <=1000 で ON
  CHECK(d.isReleasedNow());
  d.update(release_detect::kAdcMax, 50.0);  // 最大生値(1023) → 復帰しきい値到達で OFF に戻れる
  CHECK_FALSE(d.isReleasedNow());  // 未クランプなら永久 ON になり単発ノイズを誤ラッチしていた
}
