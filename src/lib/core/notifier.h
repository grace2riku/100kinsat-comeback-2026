#ifndef CORE_NOTIFIER_H
#define CORE_NOTIFIER_H

#include <cstdint>

// notifier - 状態通知（スピーカ D09 + 内蔵LED×4）のパターン再生ロジック。ハードウェア非依存。
//
// 起動・ゴール・エラー等のイベントを音と光のパターンで通知する（Issue #15 / software.md §5.1,
// §5.3）。特にゴール通知は、細則 §1「機体から音や光を出すなど、客観的に分かるのが望ましい」の
// 停止提示に使うため、stop() されるまで**繰り返し**鳴動・点滅し続ける。
//
// 設計（ノンブロッキング再生）:
//   サンプル speaker2.ino の beep() は delay() でブロックするため、飛行中の制御ループ（#17/#18）
//   からは使えない。本モジュールは「パターン＝ステップ（周波数・LEDマスク・長さ）の列」を
//   update(dtMs) のポーリングで進める再生器で、呼び出し側は toneHz()/ledMask() の現在値を
//   実 HW（tone()/noTone()/digitalWrite）へ**変化時のみ**適用する（.ino 側の薄い結線）。
//   これにより制御ループを止めずに通知でき、ホストPCで再生系列をユニットテストできる
//   （test/core/test_notifier.cpp）。
//
// 状態→通知のマッピング（Issue #15 DoD）:
//   Notice（Boot/Goal/Error）→ patternFor() が対応パターンを返す純粋関数。ミッション側（#17）は
//   状態遷移時に start(Notice) を呼ぶだけでよい。パターンの実体（音程・点滅）は notifier.cpp に
//   一元定義し、聞き分けられるよう性格を変えている:
//     Boot : 上昇4音（ドミソド）を1回 ＋ LED が順に増える（電源投入・自己診断完了の確認）
//     Goal : 単音の断続ビープ ＋ 全LED点滅の繰り返し（ゴール停止の客観的提示。stop() まで継続）
//     Error: 高低交互の速い警告音 ＋ LED 交互点滅の繰り返し（要救助・異常の周知）
//
// 注意（gotchas A: ターゲット≠ホスト）:
//   ここで検証できるのは「時間経過に対し出力系列（周波数・LEDマスク）が正しいこと」まで。
//   実際の音量・聞き取りやすさ・LED 視認性は実機 bring-up で確認する（notifier_bringup.md）。

namespace notifier {

// パターンの1ステップ。durationMs の間、toneHz と ledMask を出力する。
struct Step {
  uint16_t toneHz;      // スピーカ周波数 [Hz]。0 = 無音
  uint8_t ledMask;      // 内蔵LEDのビットマスク（bit0..3 = LED0..LED3）
  uint16_t durationMs;  // このステップの長さ [ms]
};

// 通知パターン（ステップ列）。repeat=true は stop()/start() されるまで先頭へ戻り続ける。
struct Pattern {
  const Step* steps;  // ステップ配列（静的領域を指すこと）
  uint8_t count;      // ステップ数
  bool repeat;        // true: 繰り返し（ゴール/エラー） / false: 1回再生（起動音）
};

// 通知イベント種別（状態→通知マッピングのキー）。
enum class Notice : uint8_t {
  None,   // 通知なし（patternFor は nullptr）
  Boot,   // 起動音（1回）
  Goal,   // ゴール通知（繰り返し・停止の客観的提示）
  Error,  // エラー通知（繰り返し）
};

// 状態→通知のマッピング。Notice に対応するパターンを返す（None/未知は nullptr）。
const Pattern* patternFor(Notice notice);

// 通知パターン再生器。start() で再生開始、update(dtMs) で時間を進め、toneHz()/ledMask() の
// 現在値を呼び出し側が実 HW へ適用する（ポーリング型・ノンブロッキング）。
class Notifier {
 public:
  Notifier();

  // Notice に対応するパターンの再生を先頭から開始する。再生中なら置き換える。
  // None（パターンなし）は stop() と等価。開始できたら true。
  bool start(Notice notice);

  // 任意パターンの再生を先頭から開始する（notice は current() で観測されるタグ）。
  // 不正パターン（nullptr / ステップ0個 / 合計時間0）は開始せず stop() する（false）。
  // 合計時間0を弾くのは、repeat でステップを進める処理が前進しなくなるため（無限ループ防止）。
  bool startPattern(const Pattern* pattern, Notice notice);

  // 再生を止め、出力を無音・消灯（toneHz=0, ledMask=0）へ戻す。
  void stop();

  // 経過時間 dtMs [ms] を与えて再生位置を進める。<=0・NaN・無限大は時間経過なしと扱う。
  // 1回再生（repeat=false）は末尾に達すると自動 stop()。繰り返しは先頭へ戻る（巨大 dt でも
  // 剰余で位置を求めるため停滞しない）。
  void update(double dtMs);

  // ---- 出力（呼び出し側が変化時のみ実 HW へ適用する）----
  uint16_t toneHz() const { return toneHz_; }   // 現在のスピーカ周波数 [Hz]（0=無音）
  uint8_t ledMask() const { return ledMask_; }  // 現在の LED マスク（bit0..3）

  // ---- 観測用 ----
  bool isActive() const { return active_; }          // 再生中か
  Notice current() const { return notice_; }         // 再生中の通知（停止中は None）
  double positionMs() const { return positionMs_; }  // パターン先頭からの再生位置 [ms]

 private:
  void applyPosition();  // positionMs_ から現在ステップの出力（toneHz_/ledMask_）を求める

  const Pattern* pattern_;  // 再生中パターン（停止中は nullptr）
  Notice notice_;           // 再生中の通知タグ
  double positionMs_;       // パターン先頭からの位置 [ms]（repeat は 1 周期内に正規化）
  double totalMs_;          // パターン合計時間 [ms]（startPattern で検証済み > 0）
  bool active_;             // 再生中か
  uint16_t toneHz_;         // 現在の出力周波数
  uint8_t ledMask_;         // 現在の LED マスク
};

}  // namespace notifier

#endif  // CORE_NOTIFIER_H
