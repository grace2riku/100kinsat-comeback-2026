#include "notifier.h"

#include <cmath>

namespace notifier {

namespace {

// ---- 通知パターンの実体（状態→通知マッピングの一元定義）----
// 音程・時間は暫定（実機 bring-up で聞こえ方を確認して調整。notifier_bringup.md）。
// 3パターンは「性格」を変えて聞き分け・見分けができるようにする（テストは相互差分のみ検証）。

// 起動音: 上昇4音（C5→E5→G5→C6）を1回。LED が下位から順に増えて完了を示す。
constexpr Step kBootSteps[] = {
    {523, 0x01, 120},   // C5
    {659, 0x03, 120},   // E5
    {784, 0x07, 120},   // G5
    {1047, 0x0F, 240},  // C6（終止音は少し長く）
};

// ゴール通知: 単音（A5）の断続ビープ＋全LED点滅。細則 §1 の「音や光で客観的に停止を示す」ため
// stop() まで繰り返す。ON/OFF が半々のゆっくりした点滅＝正常完了の性格。
constexpr Step kGoalSteps[] = {
    {880, 0x0F, 300},  // A5 + 全点灯
    {0, 0x00, 300},    // 無音 + 消灯
};

// エラー通知: 高低交互（2オクターブ差）の速い警告音＋LED 交互点滅。ゴールより速く不協和な
// 性格で異常の周知に使う。
constexpr Step kErrorSteps[] = {
    {1200, 0x05, 150},  // 高音 + LED0/2
    {600, 0x0A, 150},   // 低音 + LED1/3
};

constexpr Pattern kBootPattern{kBootSteps, sizeof(kBootSteps) / sizeof(kBootSteps[0]), false};
constexpr Pattern kGoalPattern{kGoalSteps, sizeof(kGoalSteps) / sizeof(kGoalSteps[0]), true};
constexpr Pattern kErrorPattern{kErrorSteps, sizeof(kErrorSteps) / sizeof(kErrorSteps[0]), true};

// パターン合計時間 [ms]。
double patternTotalMs(const Pattern& p) {
  double total = 0.0;
  for (int i = 0; i < p.count; i++) {
    total += static_cast<double>(p.steps[i].durationMs);
  }
  return total;
}

}  // namespace

const Pattern* patternFor(Notice notice) {
  switch (notice) {
    case Notice::Boot:
      return &kBootPattern;
    case Notice::Goal:
      return &kGoalPattern;
    case Notice::Error:
      return &kErrorPattern;
    case Notice::None:
    default:
      return nullptr;
  }
}

Notifier::Notifier()
    : pattern_(nullptr),
      notice_(Notice::None),
      positionMs_(0.0),
      totalMs_(0.0),
      active_(false),
      toneHz_(0),
      ledMask_(0) {}

bool Notifier::start(Notice notice) { return startPattern(patternFor(notice), notice); }

bool Notifier::startPattern(const Pattern* pattern, Notice notice) {
  // 不正パターンは開始せず停止へ倒す（置き換え失敗で古い通知が鳴り続けないよう安全側）。
  // 合計時間0は repeat の剰余計算が定義できず再生が前進しないため弾く（無限ループ防止）。
  if (pattern == nullptr || pattern->steps == nullptr || pattern->count == 0 ||
      patternTotalMs(*pattern) <= 0.0) {
    stop();
    return false;
  }
  pattern_ = pattern;
  notice_ = notice;
  totalMs_ = patternTotalMs(*pattern);
  positionMs_ = 0.0;
  active_ = true;
  applyPosition();
  return true;
}

void Notifier::stop() {
  pattern_ = nullptr;
  notice_ = Notice::None;
  positionMs_ = 0.0;
  totalMs_ = 0.0;
  active_ = false;
  toneHz_ = 0;
  ledMask_ = 0;
}

void Notifier::update(double dtMs) {
  if (!active_) {
    return;
  }
  // dt<=0 は経過なし（landing/release_detect と同じ規約）。NaN は !(x>0) で弾け、無限大は
  // isfinite で弾く（入力 inf で位置が inf になり fmod が NaN を返すのを防ぐ）。累積側は
  // repeat なら毎回 [0,total) へ正規化、1回再生なら末尾で停止するため、有限 dt の連続で
  // positionMs_ が発散することはない。
  if (!(dtMs > 0.0) || !std::isfinite(dtMs)) {
    return;
  }
  positionMs_ += dtMs;
  if (pattern_->repeat) {
    // 剰余で 1 周期内へ正規化する。巨大 dt（例: 呼び出し間隔の乱れ）でもステップ走査は
    // 1 周期分で済み、周回数に比例したループを回さない（ハング防止）。
    if (positionMs_ >= totalMs_) {
      positionMs_ = std::fmod(positionMs_, totalMs_);
    }
  } else {
    // 1回再生は末尾到達で自動停止（無音・消灯へ戻る）。
    if (positionMs_ >= totalMs_) {
      stop();
      return;
    }
  }
  applyPosition();
}

void Notifier::applyPosition() {
  // positionMs_（< totalMs_ が保証済み）が属するステップを先頭から線形に探す。ステップ数は
  // 高々数個なので走査コストは無視できる。区間は [開始, 開始+長さ) — 境界丁度は次ステップ。
  // 0ms ステップは区間が空なので自然に飛ばされる（滞在しない）。
  double acc = 0.0;
  for (int i = 0; i < pattern_->count; i++) {
    const Step& s = pattern_->steps[i];
    acc += static_cast<double>(s.durationMs);
    if (positionMs_ < acc) {
      toneHz_ = s.toneHz;
      ledMask_ = s.ledMask;
      return;
    }
  }
  // ここへは到達しない（positionMs_ < totalMs_ = 全ステップ合計）。防御的に無音へ倒す。
  toneHz_ = 0;
  ledMask_ = 0;
}

}  // namespace notifier
