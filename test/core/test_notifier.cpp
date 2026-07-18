// notifier（状態通知スピーカ/LED パターン再生）の単体テスト
//
// 時間経過（update への dt 系列）に対し、出力（toneHz/ledMask）と再生状態が正しく遷移することを
// 検証する（Issue #15 DoD / software.md §5.1, §5.3 / 細則 §1 ゴール停止の客観的提示）。
// 境界値（ステップ切り替わり丁度・1周期丁度）と異常系（dt<=0/NaN/無限大・不正パターン・巨大 dt）
// も網羅する。パターンの実体（音程・時間）ではなく「マッピングの性質」（1回/繰り返し・相互に
// 異なる）を検証し、音色チューニングでテストが壊れないようにする。

#include <cmath>
#include <limits>

#include "doctest.h"
#include "notifier.h"

using notifier::Notice;
using notifier::Notifier;
using notifier::Pattern;
using notifier::patternFor;
using notifier::Step;

namespace {

// パターン合計時間 [ms]（テスト側の独立実装で照合する）
double totalOf(const Pattern* p) {
  double total = 0.0;
  for (int i = 0; i < p->count; i++) {
    total += p->steps[i].durationMs;
  }
  return total;
}

}  // namespace

// ---- patternFor: 状態→通知のマッピング ----

TEST_CASE("patternFor: Boot/Goal/Error は有効なパターンを返し、None は nullptr") {
  CHECK(patternFor(Notice::Boot) != nullptr);
  CHECK(patternFor(Notice::Goal) != nullptr);
  CHECK(patternFor(Notice::Error) != nullptr);
  CHECK(patternFor(Notice::None) == nullptr);
}

TEST_CASE("patternFor: 起動音は1回再生、ゴール/エラーは繰り返し（停止の客観的提示）") {
  CHECK_FALSE(patternFor(Notice::Boot)->repeat);
  CHECK(patternFor(Notice::Goal)->repeat);
  CHECK(patternFor(Notice::Error)->repeat);
}

TEST_CASE("patternFor: 全パターンはステップを持ち、合計時間 > 0、LEDマスクは bit0..3 のみ") {
  const Notice notices[] = {Notice::Boot, Notice::Goal, Notice::Error};
  for (Notice n : notices) {
    const Pattern* p = patternFor(n);
    REQUIRE(p != nullptr);
    REQUIRE(p->steps != nullptr);
    CHECK(p->count > 0);
    CHECK(totalOf(p) > 0.0);
    for (int i = 0; i < p->count; i++) {
      CHECK((p->steps[i].ledMask & ~0x0F) == 0);  // 内蔵LEDは4個（bit4以上は未使用）
    }
  }
}

namespace {

// 2パターンの出力系列（ステップ列）が異なるか（テスト側の独立実装）。
bool sequencesDiffer(const Pattern* a, const Pattern* b) {
  if (a->count != b->count) {
    return true;
  }
  for (int i = 0; i < a->count; i++) {
    if (a->steps[i].toneHz != b->steps[i].toneHz || a->steps[i].ledMask != b->steps[i].ledMask ||
        a->steps[i].durationMs != b->steps[i].durationMs) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("patternFor: 全パターンの出力系列は相互に異なる（聞き分け・見分けができる）") {
  const Notice notices[] = {Notice::Boot, Notice::Goal, Notice::Error};
  const int kNotices = sizeof(notices) / sizeof(notices[0]);
  for (int i = 0; i < kNotices; i++) {
    for (int j = i + 1; j < kNotices; j++) {
      CAPTURE(i);
      CAPTURE(j);
      CHECK(sequencesDiffer(patternFor(notices[i]), patternFor(notices[j])));
    }
  }
}

// ---- Notifier: 初期状態と開始 ----

TEST_CASE("初期状態は停止（無音・消灯・None）") {
  Notifier n;
  CHECK_FALSE(n.isActive());
  CHECK(n.current() == Notice::None);
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
}

TEST_CASE("start(Boot) で先頭ステップの出力が即時に現れる") {
  Notifier n;
  CHECK(n.start(Notice::Boot));
  CHECK(n.isActive());
  CHECK(n.current() == Notice::Boot);
  const Step& s0 = patternFor(Notice::Boot)->steps[0];
  CHECK(n.toneHz() == s0.toneHz);
  CHECK(n.ledMask() == s0.ledMask);
}

TEST_CASE("start(None) は stop と等価（開始できず無音・消灯）") {
  Notifier n;
  n.start(Notice::Goal);
  CHECK_FALSE(n.start(Notice::None));
  CHECK_FALSE(n.isActive());
  CHECK(n.current() == Notice::None);
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
}

TEST_CASE("再生中の start は新しいパターンへ置き換える（先頭から）") {
  Notifier n;
  n.start(Notice::Goal);
  n.update(patternFor(Notice::Goal)->steps[0].durationMs);  // ゴールの2ステップ目へ
  CHECK(n.start(Notice::Error));
  CHECK(n.current() == Notice::Error);
  const Step& s0 = patternFor(Notice::Error)->steps[0];
  CHECK(n.toneHz() == s0.toneHz);  // エラーの先頭ステップから
  CHECK(n.positionMs() == doctest::Approx(0.0));
}

// ---- Notifier: 時間経過とステップ遷移 ----

TEST_CASE("ステップ長丁度の経過で次ステップへ切り替わる（境界値）") {
  Notifier n;
  n.start(Notice::Boot);
  const Pattern* p = patternFor(Notice::Boot);
  n.update(p->steps[0].durationMs);  // 丁度で次ステップ
  CHECK(n.toneHz() == p->steps[1].toneHz);
  CHECK(n.ledMask() == p->steps[1].ledMask);
}

TEST_CASE("ステップ長の直前では現ステップに留まる（境界値）") {
  Notifier n;
  n.start(Notice::Boot);
  const Pattern* p = patternFor(Notice::Boot);
  n.update(static_cast<double>(p->steps[0].durationMs) - 0.5);
  CHECK(n.toneHz() == p->steps[0].toneHz);
  CHECK(n.ledMask() == p->steps[0].ledMask);
}

TEST_CASE("小刻みな update の累積でもステップ境界を越える") {
  Notifier n;
  n.start(Notice::Boot);
  const Pattern* p = patternFor(Notice::Boot);
  const double half = p->steps[0].durationMs / 2.0;
  n.update(half);
  CHECK(n.toneHz() == p->steps[0].toneHz);
  n.update(half);  // 合計で丁度 → 次ステップ
  CHECK(n.toneHz() == p->steps[1].toneHz);
}

TEST_CASE("1回再生（Boot）は末尾到達で自動停止し、無音・消灯へ戻る") {
  Notifier n;
  n.start(Notice::Boot);
  n.update(totalOf(patternFor(Notice::Boot)));  // 合計時間丁度で終端
  CHECK_FALSE(n.isActive());
  CHECK(n.current() == Notice::None);
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
}

TEST_CASE("1回再生の停止後は update しても出力が現れない") {
  Notifier n;
  n.start(Notice::Boot);
  n.update(totalOf(patternFor(Notice::Boot)) + 1000.0);
  n.update(100.0);
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
}

TEST_CASE("繰り返し（Goal）は1周期丁度で先頭ステップへ戻る") {
  Notifier n;
  n.start(Notice::Goal);
  const Pattern* p = patternFor(Notice::Goal);
  n.update(totalOf(p));  // 1周期丁度
  CHECK(n.isActive());
  CHECK(n.toneHz() == p->steps[0].toneHz);
  CHECK(n.ledMask() == p->steps[0].ledMask);
  CHECK(n.positionMs() == doctest::Approx(0.0));
}

TEST_CASE("繰り返しは何周しても止まらない（周回をまたぐ update）") {
  Notifier n;
  n.start(Notice::Goal);
  const Pattern* p = patternFor(Notice::Goal);
  const double total = totalOf(p);
  for (int i = 0; i < 10; i++) {
    n.update(total * 0.7);  // 周期と非整合な刻みで周回をまたがせる
    CHECK(n.isActive());
  }
  CHECK(n.current() == Notice::Goal);
}

TEST_CASE("巨大 dt でも繰り返しは剰余で正しい位置に着地する（ハングしない）") {
  Notifier n;
  n.start(Notice::Goal);
  const Pattern* p = patternFor(Notice::Goal);
  const double total = totalOf(p);
  // 1000万周期 + 2ステップ目の途中 に相当する巨大 dt
  const double inStep1 = p->steps[0].durationMs + p->steps[1].durationMs * 0.5;
  n.update(total * 1e7 + inStep1);
  CHECK(n.isActive());
  CHECK(n.toneHz() == p->steps[1].toneHz);
  CHECK(n.ledMask() == p->steps[1].ledMask);
}

TEST_CASE("巨大 dt で 1回再生は即終了する（ハングしない）") {
  Notifier n;
  n.start(Notice::Boot);
  n.update(1e15);
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
}

// ---- Notifier: 異常系 ----

TEST_CASE("dt<=0 は時間経過なし（出力・位置が変わらない）") {
  Notifier n;
  n.start(Notice::Boot);
  const Step& s0 = patternFor(Notice::Boot)->steps[0];
  n.update(0.0);
  n.update(-100.0);
  CHECK(n.toneHz() == s0.toneHz);
  CHECK(n.positionMs() == doctest::Approx(0.0));
}

TEST_CASE("dt=NaN/無限大 は時間経過なし（位置が壊れない）") {
  Notifier n;
  n.start(Notice::Goal);
  n.update(std::nan(""));
  n.update(std::numeric_limits<double>::infinity());
  CHECK(n.isActive());
  CHECK(n.positionMs() == doctest::Approx(0.0));
  const Step& s0 = patternFor(Notice::Goal)->steps[0];
  CHECK(n.toneHz() == s0.toneHz);
}

TEST_CASE("停止中の update は何もしない") {
  Notifier n;
  n.update(1000.0);
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
}

TEST_CASE("stop で無音・消灯へ戻り、再 start で先頭から再生できる") {
  Notifier n;
  n.start(Notice::Error);
  n.update(50.0);
  n.stop();
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
  CHECK(n.current() == Notice::None);

  CHECK(n.start(Notice::Error));
  CHECK(n.positionMs() == doctest::Approx(0.0));
  CHECK(n.toneHz() == patternFor(Notice::Error)->steps[0].toneHz);
}

TEST_CASE("再 start（同じ Notice）は位相を先頭へ巻き戻す") {
  Notifier n;
  n.start(Notice::Goal);
  n.update(patternFor(Notice::Goal)->steps[0].durationMs);  // 2ステップ目
  n.start(Notice::Goal);
  CHECK(n.positionMs() == doctest::Approx(0.0));
  CHECK(n.toneHz() == patternFor(Notice::Goal)->steps[0].toneHz);
}

// ---- startPattern: 不正パターンの防御 ----

TEST_CASE("startPattern: nullptr は開始せず停止のまま") {
  Notifier n;
  CHECK_FALSE(n.startPattern(nullptr, Notice::Boot));
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
}

TEST_CASE("startPattern: ステップ0個は開始しない") {
  Notifier n;
  const Pattern empty{nullptr, 0, true};
  CHECK_FALSE(n.startPattern(&empty, Notice::Goal));
  CHECK_FALSE(n.isActive());
}

TEST_CASE("startPattern: 合計時間0（全ステップ0ms）の繰り返しは開始しない（無限ループ防止）") {
  Notifier n;
  static const Step zeroSteps[] = {{440, 0x01, 0}, {0, 0x00, 0}};
  const Pattern zero{zeroSteps, 2, true};
  CHECK_FALSE(n.startPattern(&zero, Notice::Goal));
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
}

TEST_CASE("startPattern: 途中に 0ms ステップが混ざっても飛ばして再生する") {
  Notifier n;
  static const Step steps[] = {{440, 0x01, 100}, {880, 0x02, 0}, {660, 0x04, 100}};
  const Pattern p{steps, 3, false};
  CHECK(n.startPattern(&p, Notice::Boot));
  CHECK(n.toneHz() == 440);
  n.update(100.0);  // 0ms ステップ（880）には滞在せず 660 へ
  CHECK(n.toneHz() == 660);
  CHECK(n.ledMask() == 0x04);
  n.update(100.0);  // 末尾 → 自動停止
  CHECK_FALSE(n.isActive());
}

TEST_CASE("startPattern: 不正パターンで再生中の通知も停止する（置き換え失敗は安全側）") {
  Notifier n;
  n.start(Notice::Goal);
  CHECK_FALSE(n.startPattern(nullptr, Notice::Error));
  CHECK_FALSE(n.isActive());
  CHECK(n.toneHz() == 0);
  CHECK(n.ledMask() == 0);
}
