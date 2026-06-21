/**
 * flight.ino - 本種目フライトソフトのエントリポイント（雛形）
 *
 * 位置づけ:
 *   CanSat 種目5「自律制御カムバック」の本番ソフトの最上位スケッチ。
 *   現時点では「最小ビルド可能な main」として、ミッション全体のステートマシンの
 *   骨組みだけを置く。各状態の中身（センサ取得・走行制御・ログ等）は Phase2/Phase3 で
 *   HW依存層(src/lib/hal) と HW非依存層(src/lib/core) を結線して実装する。
 *
 * 設計方針（詳細は src/README.md / doc/development/build_arduino_cli.md）:
 *   - HW非依存ロジック（距離/方位計算・状態遷移・判定）は src/lib/core に置き、
 *     ホストPCでユニットテストする（Issue #5）。
 *   - HW依存コード（モータ/センサ/SD 等の Arduino・Spresense API 呼び出し）は
 *     src/lib/hal に置き、core が定めるインタフェースを実装する。
 *   - 本スケッチは両層を組み立てて loop を回すだけの薄い結線役にする。
 *
 * シリアル: 115200 bps（全スケッチ共通）
 */

#include <Arduino.h>

/** ミッションの状態（骨組み。遷移条件の実装は Issue #17 で行う） */
enum class MissionState {
  Boot,        // 起動・初期化
  Standby,     // 放出待ち（放出機構内）
  Descent,     // 放出〜降下中
  Landed,      // 着地検知済み
  Separate,    // パラシュート切り離し
  Drive,       // 目標へ自律走行
  Approach,    // 終端接近
  Goal,        // ゴール判定・停止
  Giveup       // ギブアップ（タイムアウト等）
};

static MissionState g_state = MissionState::Boot;

void setup() {
  Serial.begin(115200);
  delay(500);  // 起動直後の出力安定待ち（Serial は CP210x UART で接続検知不可）

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" 100kinSAT comeback 2026 / flight (skeleton)");
  Serial.println(" baud: 115200 bps");
  Serial.println("=============================================");

  // TODO(Phase2/3): HAL 初期化（モータ/9軸/GNSS/SD/電熱線/通知）
  g_state = MissionState::Standby;
}

void loop() {
  // TODO(Issue #17): ステートマシン本体。現状は雛形のため状態名を出力するだけ。
  switch (g_state) {
    case MissionState::Boot:     /* fallthrough */
    case MissionState::Standby:
    case MissionState::Descent:
    case MissionState::Landed:
    case MissionState::Separate:
    case MissionState::Drive:
    case MissionState::Approach:
    case MissionState::Goal:
    case MissionState::Giveup:
    default:
      break;
  }

  delay(1000);
}
