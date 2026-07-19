/**
 * shell.ino - オンターゲット試験シェル（NT-Shell + core/cli）
 *
 * 目的:
 *   HW依存機能を、シリアルコンソールからのテストコマンドで対話的に確認する
 *   試験環境（Issue #6）。
 *
 * 構成:
 *   - 行編集・履歴のフロントエンド: NT-Shell（libraries/ntshell, MIT）
 *   - 確定した1行のトークン化・コマンド探索: src/lib/core/cli（HW非依存・ホストテスト済）
 *   - 各コマンドの実体（HW依存）: 本スケッチ内のハンドラ
 *
 * コマンド:
 *   - help / led / cds / beep / notify / motor / imu / gnss / land / separate / log / cam は実動作
 *
 * シリアル: 115200 bps
 */

#include <Arduino.h>

#include "arduino_gpio.h"
#include "bno055.h"
#include "cli.h"
#include "compass.h"
#include "cone_detect.h"
#include "datalog.h"
#include "landing.h"
#include "motor.h"
#include "notifier.h"
#include "ntshell.h"
#include "release_detect.h"
#include "sd_image_store.h"
#include "sd_log_sink.h"
#include "separator.h"
#include "spresense_camera.h"
#include "spresense_gnss.h"
#include "spresense_pins.h"
extern "C" {
#include "ntshell_spresense_arduino.h"
}

#define PROMPT_STR "100kinsat> "

static ntshell_t ntshell;
static const int LED_PINS[] = {LED0, LED1, LED2, LED3};

// モータ駆動（TB6612FNG）。実機GPIO実装に実機ピン定数を割り当てる。
static hal::ArduinoGpio g_gpio;
static motor::MotorDriver g_motor(g_gpio, hal::kMotorLeft, hal::kMotorRight);

// 9軸センサ（BNO055, I2C 0x28）。begin() の成否を g_imu_ok に保持する。
static hal::Bno055Compass g_imu;

// 着地(静止)検知器（HW非依存ロジック core/landing）。g_imu の加速度を逐次与えて静止→着地を判定。
// 既定設定（窓20・静止1秒で確定）。しきい値は実機で調整する暫定値。
static landing::LandingDetector g_landing;

// 放出(上空放出)検知器（HW非依存ロジック core/release_detect）。A0 の照度生値を逐次与え、
// 「装置内(暗)→上空(明)」の継続で放出を確定する。既定設定（閾値512・ヒステリシス50・0.5s継続）。
// しきい値・極性は実機で調整する暫定値（release_detect_bringup.md のキャリブレーション手順）。
static release_detect::ReleaseDetector g_release;

// GNSS（Spresense 内蔵）。begin() で COLD_START、update() でノンブロッキングに最新測位を読む。
static hal::SpresenseGnss g_gnss;

// カメラ（Spresense カメラボード, 専用コネクタ接続）。begin() はドライバ初期化とバッファ確保
// （QVGA/YUV422 で約150KB）を伴うため setup() では呼ばず、'cam init' でオンデマンド初期化する
// （gotchas B9: 起動経路にハングし得るデバイス初期化を置かない）。
static hal::SpresenseCamera g_camera;

// 赤コーン検出の設定（core/cone_detect）。色閾値は屋外の光条件で 'cam thr' により調整する
// （camera_bringup.md）。既定値は cone::Config の初期値（QVGA 向け）。
static cone::Config g_coneCfg;

// ---- コマンドハンドラ（HW依存。0=成功）----
static int cmd_help(int argc, char** argv);
static int cmd_led(int argc, char** argv);
static int cmd_cds(int argc, char** argv);
static int cmd_beep(int argc, char** argv);
static int cmd_notify(int argc, char** argv);
static int cmd_motor(int argc, char** argv);
static int cmd_imu(int argc, char** argv);
static int cmd_gnss(int argc, char** argv);
static int cmd_land(int argc, char** argv);
static int cmd_separate(int argc, char** argv);
static int cmd_log(int argc, char** argv);
static int cmd_cam(int argc, char** argv);

static const cli::Command kCommands[] = {
    {"help", "コマンド一覧を表示", cmd_help},
    {"led", "led <0-3> <on|off> : 内蔵LEDを点灯/消灯", cmd_led},
    {"cds", "cds [mon [n]] : 照度センサ(A0)の生値/放出検知（暗→明の継続）を表示", cmd_cds},
    {"beep", "beep <freq_hz> <ms> : スピーカ(D09)を鳴らす", cmd_beep},
    {"notify", "notify <boot|goal|error> [sec] | stop : 状態通知パターン再生(スピーカD09+LED)",
     cmd_notify},
    {"motor", "motor <forward|back|left|right|stop> [duty 0-255] [ms] : モータ駆動", cmd_motor},
    {"imu", "imu [init|stat|cal|mon [n]|i2cdiag [n]] : 9軸センサ(BNO055)の方位/校正/状態/I2C診断", cmd_imu},
    {"gnss", "gnss [init|mon [n]] : GNSS測位の状態/位置/品質を読む", cmd_gnss},
    {"land", "land [mon [n]] : 加速度から着地(静止)を検知（要 imu init）", cmd_land},
    {"separate", "separate [ms|stop] : パラシュート切り離し電熱線(D06)を加熱（安全上限あり・⚠高温注意）",
     cmd_separate},
    {"log", "log [n] : 制御履歴(制御量+操作量)をSDへCSV記録(既定5件・ダミー)", cmd_log},
    {"cam", "cam <init|snap|dump|detect|mon [n]|thr [yMin yMax uMax vMin]> : カメラ撮像・赤コーン検出",
     cmd_cam},
};
static const int kCommandCount = sizeof(kCommands) / sizeof(kCommands[0]);

static int cmd_help(int /*argc*/, char** /*argv*/) {
  for (int i = 0; i < kCommandCount; i++) {
    Serial.print("  ");
    Serial.print(kCommands[i].name);
    Serial.print("\t");
    Serial.println(kCommands[i].desc);
  }
  return 0;
}

static int cmd_led(int argc, char** argv) {
  if (argc != 3) {
    Serial.println("usage: led <0-3> <on|off>");
    return -1;
  }
  int idx;
  if (!cli::parseInt(argv[1], idx) || idx < 0 || idx > 3) {
    Serial.println("LED index は 0-3 の数値");
    return -1;
  }
  bool on = (strcmp(argv[2], "on") == 0);
  bool off = (strcmp(argv[2], "off") == 0);
  if (!on && !off) {
    Serial.println("状態は on か off");
    return -1;
  }
  pinMode(LED_PINS[idx], OUTPUT);
  digitalWrite(LED_PINS[idx], on ? HIGH : LOW);
  Serial.print("LED");
  Serial.print(idx);
  Serial.println(on ? " = ON" : " = OFF");
  return 0;
}

// 照度(A0)を1回読み、生値・放出継続時間・放出状態を1行表示し、検知器 g_release へ1サンプル
// 与える。状態表示は core/release_detect の判定（isReleasedNow/hasReleased、ホストテスト済）に基づく。
static void cds_feed_and_print(double dtMs) {
  int v = analogRead(A0);
  g_release.update(v, dtMs);
  Serial.print("cds(A0)=");
  Serial.print(g_release.lastRaw());
  Serial.print(" elapsed=");
  Serial.print(g_release.releasedElapsedMs(), 0);
  Serial.print("ms");
  if (g_release.hasReleased()) {
    Serial.println("  -> 放出検知（確定）");
  } else if (g_release.isReleasedNow()) {
    // 明側だが、暗(OFF)を一度も観測していない（未アーム）と確定しない。暗所起動の運用ミスに気づける。
    if (g_release.isArmed()) {
      Serial.println("  -> 放出側（確定待ち）");
    } else {
      Serial.println("  -> 放出側だが未アーム（電源ONは暗所で。先に暗を観測して起点を取る）");
    }
  } else {
    Serial.println("  -> 放出前（暗）");
  }
}

// cds [mon [n]]
//   cds          : 照度センサ(A0)の生値を1回表示（閾値キャリブレーションの生値確認用）
//   cds mon [n]  : n回（既定100・上限600）約50ms周期で読み、暗→明の継続→放出確定を観測する
// 注: 読み取り専用で HW を駆動しないため安全（gotchas B5）。mon 中も非改行キーで即中断する。
// しきい値/極性は defaultReleaseConfig の暫定値。実機は release_detect_bringup.md で調整する。
static int cmd_cds(int argc, char** argv) {
  if (argc > 3) {
    Serial.println("usage: cds [mon [n]]");
    return -1;
  }

  if (argc == 1) {
    // 単発は生値確認用（放出検知の継続判定は継続サンプルが要るため 'cds mon' を使う）。
    int v = analogRead(A0);
    Serial.print("cds(A0) = ");
    Serial.println(v);
    Serial.println("cds: 放出検知の継続判定は 'cds mon' を使う（単発は生値確認用）");
    return 0;
  }

  if (strcmp(argv[1], "mon") == 0) {
    const int kMaxCount = 600;
    int count = 100;  // 既定回数（約50ms周期で 5 秒）
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > kMaxCount) {
        Serial.print("回数は 1-");
        Serial.print(kMaxCount);
        Serial.println(" の整数");
        return -1;
      }
    }
    g_release.reset();
    unsigned long prev = 0;  // 前サンプルの時刻
    bool havePrev = false;   // prev が有効か（millis()==0 の値と初回未設定を混同しない）
    for (int i = 0; i < count; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(count);
      Serial.print("] ");
      // 前サンプルからの実経過[ms]を dt として与える（初回は 0＝継続時間の起点）。
      unsigned long now = millis();
      double dtMs = havePrev ? static_cast<double>(now - prev) : 0.0;
      prev = now;
      havePrev = true;
      cds_feed_and_print(dtMs);
      // 約50ms インターバル。非改行キーで残りを打ち切る（gotchas B10: CR/LF は読み飛ばす）。
      if (i + 1 < count) {
        unsigned long start = millis();
        bool aborted = false;
        while (millis() - start < 50) {
          if (Serial.available()) {
            int c = Serial.read();
            if (c >= 0 && c != '\r' && c != '\n') {
              while (Serial.available()) {
                Serial.read();
              }
              aborted = true;
              break;
            }
          }
        }
        if (aborted) {
          Serial.println("cds mon: 中断しました");
          break;
        }
      }
    }
    return 0;
  }

  Serial.println("usage: cds [mon [n]]");
  return -1;
}

static int cmd_beep(int argc, char** argv) {
  if (argc != 3) {
    Serial.println("usage: beep <freq_hz> <ms>");
    return -1;
  }
  int freq;
  int ms;
  if (!cli::parseInt(argv[1], freq) || !cli::parseInt(argv[2], ms) || freq <= 0 || ms <= 0) {
    Serial.println("freq/ms は正の整数");
    return -1;
  }
  tone(hal::kSpeakerPin, freq);  // D09: スピーカ
  delay(ms);
  noTone(hal::kSpeakerPin);
  Serial.println("beep done");
  return 0;
}

// notifier の現在出力を実 HW へ適用する（変化時のみ）。tone() は同一周波数でも呼び直すと
// 位相が乱れるため、前回適用値と異なるときだけ tone/noTone・digitalWrite を行う。
static void notify_apply(uint16_t hz, uint8_t mask, uint16_t& lastHz, uint8_t& lastMask) {
  if (hz != lastHz) {
    if (hz == 0) {
      noTone(hal::kSpeakerPin);
    } else {
      tone(hal::kSpeakerPin, hz);
    }
    lastHz = hz;
  }
  if (mask != lastMask) {
    for (int i = 0; i < 4; i++) {
      digitalWrite(LED_PINS[i], ((mask >> i) & 1) ? HIGH : LOW);
    }
    lastMask = mask;
  }
}

// スピーカと全 LED を確実にオフへ（notify のどの終了経路でも最後に呼ぶ）。
static void notify_silence() {
  noTone(hal::kSpeakerPin);
  for (int i = 0; i < 4; i++) {
    digitalWrite(LED_PINS[i], LOW);
  }
}

// notify <boot|goal|error> [sec] | stop
//   notify boot        : 起動音（上昇4音+LED順増）を1回再生する
//   notify goal [sec]  : ゴール通知（断続ビープ+全LED点滅）を sec 秒（既定5・上限30）再生する
//   notify error [sec] : エラー通知（高低交互の警告音+LED交互点滅）を同上
//   notify stop        : 消音・消灯（万一鳴りっぱなしで残った時の保険）
// 実飛行では #17 が状態遷移時に notifier::Notifier を start し続ける（ゴールは stop まで無限
// 繰り返し）。本コマンドは単体確認用のため再生時間を有限に区切り、非改行キーで即中断できる
// （gotchas B5/B10）。パターン再生列は core/notifier（ホストテスト済）、本関数は結線のみ。
static int cmd_notify(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    Serial.println("usage: notify <boot|goal|error> [sec] | stop");
    return -1;
  }

  // LED は led コマンドと共用のため毎回出力設定してから使う。
  for (int i = 0; i < 4; i++) {
    pinMode(LED_PINS[i], OUTPUT);
  }

  if (strcmp(argv[1], "stop") == 0) {
    notify_silence();
    Serial.println("notify stop: 消音・消灯しました");
    return 0;
  }

  notifier::Notice notice;
  if (strcmp(argv[1], "boot") == 0) {
    notice = notifier::Notice::Boot;
  } else if (strcmp(argv[1], "goal") == 0) {
    notice = notifier::Notice::Goal;
  } else if (strcmp(argv[1], "error") == 0) {
    notice = notifier::Notice::Error;
  } else {
    Serial.println("種別は boot|goal|error|stop");
    return -1;
  }

  // 再生時間。1回再生（boot）は完了まで鳴るため秒指定は受け付けない（「秒指定が効く」との
  // 誤解を防ぐ）。繰り返し（goal/error）は既定5秒・上限30秒で必ず終わる。
  const int kMaxSec = 30;
  int sec = 5;
  if (argc == 3) {
    if (notice == notifier::Notice::Boot) {
      Serial.println("boot は1回再生のため sec 指定は不可（goal/error のみ）");
      return -1;
    }
    if (!cli::parseInt(argv[2], sec) || sec < 1 || sec > kMaxSec) {
      Serial.print("sec は 1-");
      Serial.print(kMaxSec);
      Serial.println(" の整数");
      return -1;
    }
  }

  notifier::Notifier player;
  if (!player.start(notice)) {
    notify_silence();
    Serial.println("notify: パターンを開始できませんでした");
    return -1;
  }
  Serial.print("notify ");
  Serial.print(argv[1]);
  const notifier::Pattern* pat = notifier::patternFor(notice);  // start 成功済みなので非 null
  if (pat->repeat) {
    Serial.print(" を ");
    Serial.print(sec);
    Serial.print(" 秒再生します");
  } else {
    Serial.print(" を1回再生します");
  }
  Serial.println("（中断: 非改行キー）");

  // 約10ms 周期で update→出力適用。実経過[ms]を dt に使う（millis ベース）。
  // 終了条件: 1回再生の完了 / 再生時間の上限 / 非改行キー中断（gotchas B10: CR/LF は読み飛ばす）。
  const unsigned long limitMs = static_cast<unsigned long>(sec) * 1000UL;
  uint16_t lastHz = 0;
  uint8_t lastMask = 0;
  unsigned long begin = millis();
  unsigned long prev = begin;
  bool aborted = false;
  while (player.isActive()) {
    unsigned long now = millis();
    player.update(static_cast<double>(now - prev));
    prev = now;
    if (!player.isActive() || (now - begin >= limitMs)) {
      break;  // 1回再生の完了 or 再生時間の上限
    }
    notify_apply(player.toneHz(), player.ledMask(), lastHz, lastMask);
    // 次 tick まで最大10ms、キー入力をポーリングしながら待つ。
    unsigned long start = millis();
    while (millis() - start < 10) {
      if (Serial.available()) {
        int c = Serial.read();
        if (c >= 0 && c != '\r' && c != '\n') {
          while (Serial.available()) {
            Serial.read();
          }
          aborted = true;
          break;
        }
      }
    }
    if (aborted) {
      break;
    }
  }

  notify_silence();  // 終了保証: どの経路でも必ず消音・消灯する
  Serial.println(aborted ? "notify: 中断しました（消音・消灯）" : "notify: 完了（消音・消灯）");
  return 0;
}

// motor <forward|back|left|right|stop> [duty 0-255] [ms]
//   指定方向へ duty で駆動し、ms 経過後に自動停止する（暴走防止）。stop は即時停止。
//   デッドゾーン/最低駆動デューティの実機計測に使う（Issue #8 DoD）。
static int cmd_motor(int argc, char** argv) {
  if (argc < 2) {
    Serial.println("usage: motor <forward|back|left|right|stop> [duty 0-255] [ms]");
    return -1;
  }
  const char* dir = argv[1];

  if (strcmp(dir, "stop") == 0) {
    g_motor.stop();  // 停止は安全側なので余剰引数があっても受理する
    Serial.println("motor stop");
    return 0;
  }

  // 駆動コマンドは余剰引数を弾く（誤入力で HW を駆動しないため。gotchas B6）。
  // 受理する形: motor <dir> [duty] [ms]（最大4トークン）。
  if (argc > 4) {
    Serial.println("引数が多すぎます: motor <forward|back|left|right|stop> [duty 0-255] [ms]");
    return -1;
  }

  uint8_t duty = 150;  // 既定デューティ
  if (argc >= 3) {
    int d;
    if (!cli::parseInt(argv[2], d) || d < 0 || d > 255) {
      Serial.println("duty は 0-255 の整数");
      return -1;
    }
    duty = static_cast<uint8_t>(d);
  }

  // 駆動時間。delay(ms) 中はシェルがブロックし緊急 stop を受理できないため、
  // 上限を設けて長時間の暴走を防ぐ（gotchas B5）。
  const int kMaxDriveMs = 5000;
  int ms = 800;  // 既定駆動時間
  if (argc >= 4) {
    if (!cli::parseInt(argv[3], ms) || ms <= 0 || ms > kMaxDriveMs) {
      Serial.print("ms は 1-");
      Serial.print(kMaxDriveMs);
      Serial.println(" の整数（駆動中は停止コマンドを受理できないため上限あり）");
      return -1;
    }
  }

  if (strcmp(dir, "forward") == 0) {
    g_motor.forward(duty);
  } else if (strcmp(dir, "back") == 0) {
    g_motor.back(duty);
  } else if (strcmp(dir, "left") == 0) {
    g_motor.turnLeft(duty);
  } else if (strcmp(dir, "right") == 0) {
    g_motor.turnRight(duty);
  } else {
    Serial.println("dir は forward|back|left|right|stop");
    return -1;
  }

  Serial.print("motor ");
  Serial.print(dir);
  Serial.print(" duty=");
  Serial.print(duty);
  Serial.print(" for ");
  Serial.print(ms);
  Serial.println("ms");
  delay(ms);
  g_motor.stop();
  Serial.println("motor stop (auto)");
  return 0;
}

// キャリブレーション状態（各0-3）と完了判定の見立てを1行で表示する。
static void print_calibration(const compass::CalibrationStatus& c) {
  Serial.print("  cal sys/gyro/accel/mag = ");
  Serial.print(c.system);
  Serial.print("/");
  Serial.print(c.gyro);
  Serial.print("/");
  Serial.print(c.accel);
  Serial.print("/");
  Serial.print(c.mag);
  if (compass::isFullyCalibrated(c)) {
    Serial.println("  -> 完全校正(全要素3)");
  } else if (compass::isHeadingReady(c)) {
    Serial.println("  -> 方位は使用可(暫定: sys+mag=3)");
  } else {
    Serial.println("  -> 校正未完了（平面に置き、8の字に回す）");
  }
}

// 方位角とキャリブレーションを1回読んで表示する（共通処理）。
static int imu_read_once() {
  double h = g_imu.heading();
  bool valid = compass::isValidHeading(h);
  Serial.print("heading = ");
  if (valid) {
    Serial.print(h, 1);
    Serial.println(" deg");
  } else {
    // 読み取り失敗は 0.0(=真北) ではなく無効として明示する（gotchas B8/B11）。校正行は別 I2C
    // 読みなので、失敗時も続けて表示し mon での校正収束観察を妨げない。
    Serial.println("(invalid: I2C読み取り失敗)");
  }
  print_calibration(g_imu.calibration());
  return valid ? 0 : -1;
}

// imu i2cdiag のワンパス実行（対策A/Bの単位）。加速度I2C読みを count 回「単発試行」し、生失敗率・
// 失敗フェーズ内訳・周期内リトライ(3回×5ms)回復率を1行に集計する。sendStop/clockHz を変えて
// トランザクション方式×クロックを実機比較するために使う。診断用途なので delay ベースで待つ（各パス
// が短時間なため中断ポーリングは省略）。呼び出し後はクロックが clockHz のままになる点に注意（呼び出し
// 側の i2cdiag が最後に 100kHz へ戻す）。
static void imu_i2c_pass(const char* label, int count, unsigned long intervalMs, bool sendStop,
                         uint32_t clockHz) {
  Wire.setClock(clockHz);
  int rawFail = 0;
  int recovered = 0;
  int hardFail = 0;
  int failWriteAddr = 0;
  int failRequestFrom = 0;
  int failShortRead = 0;
  for (int i = 0; i < count; i++) {
    landing::Accel3 a;
    hal::Bno055Compass::AccelReadResult r = g_imu.readAccelerationDiag(a, sendStop);
    if (!r.ok) {
      rawFail++;
      if (r.phase == hal::Bno055Compass::AccelReadPhase::kWriteAddr) {
        failWriteAddr++;
      } else if (r.phase == hal::Bno055Compass::AccelReadPhase::kRequestFrom) {
        failRequestFrom++;
      } else if (r.phase == hal::Bno055Compass::AccelReadPhase::kShortRead) {
        failShortRead++;
      }
      // 周期内リトライ（heading 相当: 追加2回×5ms = 合計3試行）で回復するか
      bool ok = false;
      for (int t = 0; t < 2; t++) {
        delay(5);
        if (g_imu.readAccelerationDiag(a, sendStop).ok) {
          ok = true;
          break;
        }
      }
      if (ok) {
        recovered++;
      } else {
        hardFail++;
      }
    }
    if (i + 1 < count) {
      delay(intervalMs);
    }
  }
  Serial.print("[");
  Serial.print(label);
  Serial.print("] 生失敗 ");
  Serial.print(rawFail);
  Serial.print("/");
  Serial.print(count);
  Serial.print(" (");
  Serial.print(count > 0 ? rawFail * 100.0 / count : 0.0, 1);
  Serial.print("%)  内訳 wAddr=");
  Serial.print(failWriteAddr);
  Serial.print(" reqFrom=");
  Serial.print(failRequestFrom);
  Serial.print(" short=");
  Serial.print(failShortRead);
  Serial.print("  retry回復=");
  Serial.print(recovered);
  Serial.print("/");
  Serial.print(rawFail);
  Serial.print(" hardFail=");
  Serial.println(hardFail);
}

// imu [init|stat|cal|mon [n]|i2cdiag [n]]
//   imu            : 方位角とキャリブレーション状態を1回読む
//   imu init       : センサを(再)初期化する（配線・電源を入れた後に試せる）
//   imu stat       : システム状態（融合稼働/自己診断/エラー）を表示する＝読み取り健全性の確認
//   imu cal        : キャリブレーション状態だけ表示する
//   imu mon [n]    : n回（既定20・上限120）方位/校正を約0.5秒間隔で表示する（校正収束の観察用）
//   imu i2cdiag [n]: 各パス n回（既定100・上限150）の加速度I2C読みを、repeated-start/STOP × 100k/50k の
//                    4通りで連続測定し、生失敗率が最小の対策(方式×クロック)を実機で選ぶ
// 注: mon/i2cdiag は読み取り専用で HW を駆動しないため安全（gotchas B5）。mon は長時間ブロックを避け
//     インターバル中に Enter 以外のキー入力があれば即中断する（millis ベースのポーリング待ち）。
//     CRLF の残留 LF を中断と誤認しないよう CR/LF は読み飛ばす（gotchas B10）。
static int cmd_imu(int argc, char** argv) {
  if (argc > 3) {
    Serial.println("usage: imu [init|stat|cal|mon [n]|i2cdiag [n]]");
    return -1;
  }

  // init は未検出でも試せるよう、ready 判定より前に処理する。
  if (argc >= 2 && strcmp(argv[1], "init") == 0) {
    if (argc != 2) {
      Serial.println("usage: imu init");
      return -1;
    }
    bool ok = g_imu.begin();
    Serial.println(ok ? "imu: BNO055 検出・初期化 OK"
                      : "imu: BNO055 未検出（I2C 0x28 の配線・電源を確認）");
    return ok ? 0 : -1;
  }

  if (!g_imu.ready()) {
    Serial.println("imu: 未初期化。'imu init' で初期化してください（未検出なら配線を確認）");
    return -1;
  }

  if (argc == 1) {
    return imu_read_once();
  }

  if (strcmp(argv[1], "stat") == 0) {
    if (argc != 2) {
      Serial.println("usage: imu stat");
      return -1;
    }
    hal::Bno055Compass::SystemStatus s = g_imu.systemStatus();
    Serial.print("sys_status=");
    Serial.print(s.status);
    Serial.print(" self_test=0x");
    Serial.print(s.selfTest, HEX);
    Serial.print(" sys_error=");
    Serial.print(s.error);
    // status==5（融合稼働中）かつ error==0 を健全とみなす。活線抜け/ブラウンアウトはここで露見する
    // （heading の値では真北0°と区別できないため。gotchas B8）。
    Serial.println((s.status == 5 && s.error == 0) ? "  -> 健全(融合稼働中)"
                                                   : "  -> 異常（配線・電源・初期化を確認）");
    return 0;
  }

  if (strcmp(argv[1], "cal") == 0) {
    if (argc != 2) {
      Serial.println("usage: imu cal");
      return -1;
    }
    print_calibration(g_imu.calibration());
    return 0;
  }

  if (strcmp(argv[1], "i2cdiag") == 0) {
    // I2C 加速度読みの信頼性を切り分ける。原因は既に BNO055 のクロックストレッチと確定（失敗が
    //   requestFrom 相に集中・writeAddr=0・SYS_ERR=0）。ここでは対策候補を実機A/Bする:
    //   トランザクション方式(repeated-start vs STOP) × I2Cクロック(100k vs 50k) の 2×2 を、同一条件
    //   （同じ n・同じ間隔）で連続測定し、生失敗率が最小の組合せを観測で選ぶ。
    // 上限は4パス通しでも中断不能な待ち時間が過大にならない範囲に抑える（本コマンドは各パス間の
    // キー中断を持たないため。150×4パス×~17ms ≒ 10秒台）。
    const int kMaxCount = 150;
    int count = 100;  // 各パスの試行回数（既定100・約17ms間隔で1パス約2秒）
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > kMaxCount) {
        Serial.print("回数は 1-");
        Serial.print(kMaxCount);
        Serial.println(" の整数");
        return -1;
      }
    }
    hal::Bno055Compass::SystemStatus s0 = g_imu.systemStatus();
    Serial.print("i2cdiag: 開始 SYS status=");
    Serial.print(s0.status);
    Serial.print(" self_test=0x");
    Serial.print(s0.selfTest, HEX);
    Serial.print(" sys_error=");
    Serial.println(s0.error);
    Serial.print("i2cdiag: 各パス n=");
    Serial.print(count);
    Serial.println(" interval=17ms retry=3x@5ms（Wireコアの生ERROR行が失敗毎に出るのは正常）");
    Serial.println("---- 対策A/B: 生失敗率が最小の (方式×クロック) を選ぶ ----");
    // 融合更新(~100Hz=10ms)と非同調な間隔で代表的な失敗率を測る（10の整数倍を避ける）。
    const unsigned long kIv = 17;
    imu_i2c_pass("RS  100k", count, kIv, false, 100000);
    imu_i2c_pass("STOP100k", count, kIv, true, 100000);
    imu_i2c_pass("RS   50k", count, kIv, false, 50000);
    imu_i2c_pass("STOP 50k", count, kIv, true, 50000);
    // 運用クロックへ戻す。BNO055 の運用は Adafruit begin() が設定する 50kHz（kI2cClockHz）で、
    // ここを 100kHz 等の想定値へ“戻す”と設定持ち越しで運用クロックが変わる（gotchas A5/B17）。
    Wire.setClock(hal::Bno055Compass::kI2cClockHz);
    hal::Bno055Compass::SystemStatus s1 = g_imu.systemStatus();
    Serial.print("i2cdiag: 終了 SYS status=");
    Serial.print(s1.status);
    Serial.print(" sys_error=");
    Serial.println(s1.error);
    Serial.println("-> wAddr=0 かつ SYS_ERR=0 が保たれていればクロックストレッチ確定。");
    Serial.println("   生失敗率が最小のパスの (方式×クロック) を対策として採用する。");
    return 0;
  }

  if (strcmp(argv[1], "mon") == 0) {
    const int kMaxCount = 120;
    int count = 20;  // 既定回数
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > kMaxCount) {
        Serial.print("回数は 1-");
        Serial.print(kMaxCount);
        Serial.println(" の整数");
        return -1;
      }
    }
    for (int i = 0; i < count; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(count);
      Serial.print("] ");
      imu_read_once();
      // インターバル待ち。Enter 以外のキー入力があれば残りを打ち切る（長時間ブロックの回避）。
      // 注（gotchas B10）: 本コマンドを確定した Enter は、CRLF モニタだと CR で確定された後に LF が
      //   遅れて届く。この残留 LF を中断キーと誤認すると mon が1サンプルで止まる。よって CR/LF は
      //   読み飛ばし、非改行バイトのみを中断トリガーにする。
      if (i + 1 < count) {
        unsigned long start = millis();
        bool aborted = false;
        while (millis() - start < 500) {
          if (Serial.available()) {
            int c = Serial.read();
            if (c >= 0 && c != '\r' && c != '\n') {
              while (Serial.available()) {
                Serial.read();  // 残りの入力も読み捨てて中断
              }
              aborted = true;
              break;
            }
            // CR/LF（コマンド確定の残留改行）は読み飛ばして待機を継続する
          }
        }
        if (aborted) {
          Serial.println("imu mon: 中断しました");
          break;
        }
      }
    }
    return 0;
  }

  Serial.println("usage: imu [init|stat|cal|mon [n]|i2cdiag [n]]");
  return -1;
}

// 最新の測位スナップショットを1行で表示する。座標は hasPositionFix で、走行可否は
// isUsableForNavigation（FIX かつ HDOP 良好）で判定する（いずれも core/gnss_fix のホストテスト済ロジック）。
static void print_gnss(const gnss::GnssFix& fix) {
  Serial.print("sat=");
  Serial.print(fix.numSatellites);
  Serial.print(" fixMode=");
  Serial.print(fix.fixMode);  // 1:Invalid 2:2D 3:3D
  if (gnss::hasPositionFix(fix)) {
    Serial.print(" pos=");
    Serial.print(fix.latitude, 6);
    Serial.print(",");
    Serial.print(fix.longitude, 6);
    Serial.print(" hdop=");
    Serial.print(fix.hdop, 2);
    Serial.print(" vel=");
    Serial.print(fix.velocity, 2);
    if (gnss::isUsableForNavigation(fix)) {
      Serial.println("  -> 走行可（FIX・精度良好）");
    } else {
      Serial.println("  -> FIXあり/精度不足（HDOP が高い or 0）");
    }
  } else {
    Serial.println("  -> No Position（測位不能：屋外・天空視界を確保）");
  }
}

// 最大 timeoutMs だけ update をポーリングし、最初の1更新を表示する。GNSS は約1Hz 更新のため
// 単発取得でも最大1秒程度待つ。非改行キー入力で打ち切り（aborted!=nullptr のとき *aborted=true）。
// CR/LF（コマンド確定の残留改行）は中断トリガーにしない（gotchas B10）。
static int gnss_read_once(unsigned long timeoutMs, bool* aborted) {
  unsigned long start = millis();
  gnss::GnssFix fix;
  while (millis() - start < timeoutMs) {
    if (g_gnss.update(fix)) {
      print_gnss(fix);
      return 0;
    }
    if (Serial.available()) {
      int c = Serial.read();
      if (c >= 0 && c != '\r' && c != '\n') {
        while (Serial.available()) {
          Serial.read();
        }
        if (aborted != nullptr) {
          *aborted = true;
        }
        return -1;
      }
    }
  }
  // waitUpdate(0) で約1Hz の更新をこの2秒窓で1つも拾えなかった場合。FIX 済みでも更新位相の
  // ずれで時々出るため「衛星未捕捉」と断定しない（誤解防止）。未FIX が続く場合のみ天空視界を疑う。
  Serial.println("gnss: この周期は更新なし（約1Hz。未FIXが続く場合は屋外で天空視界を確保し再試行）");
  return -1;
}

// gnss [init|mon [n]]
//   gnss         : 最新の測位を1回読む（最大2秒、更新を1つ待って表示）
//   gnss init    : GNSS を初期化し測位開始（COLD_START。FIX まで屋外で数十秒〜数分）
//   gnss mon [n] : n回（既定20・上限120）測位を読み続ける（FIX 収束の観察用）
// 注: 読み取り専用で HW を駆動しないため安全（gotchas B5）。mon 中も非改行キーで即中断する。
static int cmd_gnss(int argc, char** argv) {
  if (argc > 3) {
    Serial.println("usage: gnss [init|mon [n]]");
    return -1;
  }

  // init は未初期化でも試せるよう、ready 判定より前に処理する。
  if (argc >= 2 && strcmp(argv[1], "init") == 0) {
    if (argc != 2) {
      Serial.println("usage: gnss init");
      return -1;
    }
    bool ok = g_gnss.begin();
    Serial.println(ok ? "gnss: 初期化OK（COLD_START。FIX まで屋外で数十秒〜数分）"
                      : "gnss: 初期化失敗（GNSS begin/start エラー。電源・アンテナを確認）");
    return ok ? 0 : -1;
  }

  if (!g_gnss.ready()) {
    Serial.println("gnss: 未初期化。'gnss init' で初期化してください");
    return -1;
  }

  if (argc == 1) {
    return gnss_read_once(2000, nullptr);  // 最大2秒で1更新
  }

  if (strcmp(argv[1], "mon") == 0) {
    const int kMaxCount = 120;
    int count = 20;  // 既定回数
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > kMaxCount) {
        Serial.print("回数は 1-");
        Serial.print(kMaxCount);
        Serial.println(" の整数");
        return -1;
      }
    }
    for (int i = 0; i < count; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(count);
      Serial.print("] ");
      bool aborted = false;
      gnss_read_once(2000, &aborted);
      if (aborted) {
        Serial.println("gnss mon: 中断しました");
        break;
      }
    }
    return 0;
  }

  Serial.println("usage: gnss [init|mon [n]]");
  return -1;
}

// 加速度を1回読み、大きさ|a|・窓の平均/分散・静止継続時間・着地状態を1行表示し、
// 検知器 g_landing へ1サンプル与える。読み取り失敗時は false（検知器へは与えない）。
// 状態表示は core/landing の判定（isStillNow/hasLanded、ホストテスト済）に基づく。
static bool land_feed_and_print(double dtMs) {
  landing::Accel3 a;
  if (!g_imu.readAcceleration(a)) {
    Serial.println("land: 加速度読み取り失敗（I2C。配線/初期化を確認）");
    return false;
  }
  g_landing.update(a, dtMs);
  Serial.print("|a|=");
  Serial.print(a.magnitude(), 2);
  Serial.print(" mean=");
  Serial.print(g_landing.meanMagnitude(), 2);
  Serial.print(" var=");
  Serial.print(g_landing.variance(), 3);
  Serial.print(" still=");
  Serial.print(g_landing.stillElapsedMs(), 0);
  Serial.print("ms");
  if (g_landing.hasLanded()) {
    Serial.println("  -> 着地検知（静止確定）");
  } else if (g_landing.isStillNow()) {
    Serial.println("  -> 静止中（着地確定待ち）");
  } else if (g_landing.windowFull()) {
    Serial.println("  -> 運動中");
  } else {
    Serial.println("  -> 計測中（窓充填中）");
  }
  return true;
}

// land [mon [n]]
//   land         : 加速度を1回読み、大きさ|a|と状態を表示（検知器をリセットして単発観測）
//   land mon [n] : n回（既定100・上限600）加速度を約50ms周期で読み、静止→着地確定を観測する
// 前提: 加速度は g_imu（BNO055）から読むため 'imu init' が必要。
// 注: 読み取り専用で HW を駆動しないため安全（gotchas B5）。mon 中も非改行キーで即中断する。
static int cmd_land(int argc, char** argv) {
  if (argc > 3) {
    Serial.println("usage: land [mon [n]]");
    return -1;
  }
  if (!g_imu.ready()) {
    Serial.println("land: 9軸センサ未初期化。'imu init' で初期化してください");
    return -1;
  }

  if (argc == 1) {
    // 単発は生 |a| 確認用。reset 直後・窓充填前なので状態は必ず「計測中」になる。
    // 静止/着地の判定は継続サンプルが要るため 'land mon' を使う。
    g_landing.reset();
    bool ok = land_feed_and_print(0.0);
    Serial.println("land: 静止/着地の判定は 'land mon' を使う（単発は |a| 確認用）");
    return ok ? 0 : -1;
  }

  if (strcmp(argv[1], "mon") == 0) {
    const int kMaxCount = 600;
    int count = 100;  // 既定回数（約50ms周期で 5 秒。窓20充填後に静止1秒で着地確定）
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > kMaxCount) {
        Serial.print("回数は 1-");
        Serial.print(kMaxCount);
        Serial.println(" の整数");
        return -1;
      }
    }
    g_landing.reset();
    unsigned long prev = 0;  // 前サンプルの時刻
    bool havePrev = false;   // prev が有効か（millis()==0 の値と初回未設定を混同しない）
    for (int i = 0; i < count; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(count);
      Serial.print("] ");
      // 前サンプルからの実経過[ms]を dt として与える（初回は 0＝継続時間の起点）。
      unsigned long now = millis();
      double dtMs = havePrev ? static_cast<double>(now - prev) : 0.0;
      prev = now;
      havePrev = true;
      land_feed_and_print(dtMs);
      // 約50ms インターバル。非改行キーで残りを打ち切る（gotchas B10: CR/LF は読み飛ばす）。
      if (i + 1 < count) {
        unsigned long start = millis();
        bool aborted = false;
        while (millis() - start < 50) {
          if (Serial.available()) {
            int c = Serial.read();
            if (c >= 0 && c != '\r' && c != '\n') {
              while (Serial.available()) {
                Serial.read();
              }
              aborted = true;
              break;
            }
          }
        }
        if (aborted) {
          Serial.println("land mon: 中断しました");
          break;
        }
      }
    }
    return 0;
  }

  Serial.println("usage: land [mon [n]]");
  return -1;
}

// separate [ms|stop]
//   separate       : 電熱線(D06)を既定時間 加熱してパラシュートを切り離す（⚠加熱部は高温）
//   separate <ms>  : 加熱時間[ms]を上書き（安全ハード上限 kMaxHeatMsCap にクランプ）
//   separate stop  : D06 を LOW（非加熱）へ強制（保険。加熱は下記ループ中の非改行キーでも中断可）
// 安全設計（Issue #13 / gotchas B5）:
//   - core separator が加熱を heatMs で必ず自動停止する（上限ガード。多重起動防止・クランプ込み）。
//   - 本コマンドは毎回ローカルの separator を生成し1パルスを撃つ。**どの終了経路（完了/中断/開始不可）
//     でも最後に必ず D06 を LOW にする**（abort() を冗長に呼ぶ）ため、電熱線を HIGH のまま残さない。
static int cmd_separate(int argc, char** argv) {
  if (argc > 2) {
    Serial.println("usage: separate [ms|stop]");
    return -1;
  }

  // stop: D06 を LOW へ強制（begin() は出力設定＋LOW を行う安全初期化）。
  // 注: separate はブロッキングで加熱中は次コマンドを受け付けないため、加熱の中断は下記ループ中の
  //     非改行キーで行う。この stop は「万一 D06 が HIGH で残った時に手動で LOW へ戻す保険」。
  if (argc == 2 && strcmp(argv[1], "stop") == 0) {
    separator::ParachuteSeparator sep(g_gpio, hal::kHeatingWirePin);
    sep.begin();
    Serial.println("separate stop: D06 を LOW（非加熱）にしました");
    return 0;
  }

  separator::SeparatorConfig cfg = separator::defaultSeparatorConfig();
  if (argc == 2) {
    int ms = 0;
    if (!cli::parseInt(argv[1], ms) || ms < 1) {
      Serial.println("加熱時間[ms]は 1 以上の整数（省略時は既定値）");
      return -1;
    }
    cfg.heatMs = static_cast<double>(ms);  // クランプは separator 側（kMaxHeatMsCap）
  }

  separator::ParachuteSeparator sep(g_gpio, hal::kHeatingWirePin, cfg);
  sep.begin();
  Serial.print("⚠ 電熱線 D06 を加熱します（加熱部は高温・触れない）。heatMs=");
  Serial.print(sep.config().heatMs, 0);
  Serial.println(" ms。中断: 非改行キー");

  if (!sep.start()) {
    sep.abort();  // 念のため LOW を保証
    Serial.println("separate: 開始できませんでした（D06 LOW）");
    return -1;
  }

  // 過加熱の上振れ防止: 更新/中断ポーリングは細かい粒度で行い、かつ「残り加熱時間」で頭打ちにする。
  // 固定 100ms 待ちだと heatMs<100ms（例 separate 1）や端数で最大約1周期ぶん HIGH を延長してしまう
  // （Codex 指摘）。待ち時間を remaining にクランプし、上限到達で即 LOW に倒す。進捗表示は間引く。
  const unsigned long kPollMs = 10;  // 中断応答＆上振れ上限の粒度
  unsigned long prev = millis();
  unsigned long lastPrintMs = prev;
  bool aborted = false;
  while (sep.isHeating()) {
    unsigned long now = millis();
    sep.update(static_cast<double>(now - prev));
    prev = now;
    if (!sep.isHeating()) {
      break;  // このサンプルで上限到達→停止済み
    }
    if (now - lastPrintMs >= 250) {  // 表示は約250ms毎（10ms粒度での洪水を避ける）
      Serial.print("  D06=HIGH 加熱中 elapsed=");
      Serial.print(sep.heatElapsedMs(), 0);
      Serial.println(" ms");
      lastPrintMs = now;
    }
    // 次の update までの待ち = min(kPollMs, 残り加熱時間)。1ms 未満は 1ms（ビジーループ回避）。
    double remaining = sep.config().heatMs - sep.heatElapsedMs();
    unsigned long waitMs = kPollMs;
    if (remaining < static_cast<double>(kPollMs)) {
      waitMs = (remaining > 1.0) ? static_cast<unsigned long>(remaining) : 1;
    }
    // 待機中に非改行キーで中断（gotchas B10: CR/LF は読み飛ばす / B5: delay 一括せずポーリング）。
    unsigned long start = millis();
    while (millis() - start < waitMs) {
      if (Serial.available()) {
        int c = Serial.read();
        if (c >= 0 && c != '\r' && c != '\n') {
          while (Serial.available()) {
            Serial.read();
          }
          aborted = true;
          break;
        }
      }
    }
    if (aborted) {
      break;
    }
  }

  sep.abort();  // 終了保証: どの経路でも必ず D06 を LOW にする（安全側・冗長でも呼ぶ）
  Serial.println(aborted ? "separate: 中断しました（D06 LOW）"
                         : "separate: 加熱完了（D06 LOW）");
  return 0;
}

// log [n]
//   制御履歴（制御量＋操作量）を SD へ CSV 記録する。実センサの結合は #17/#18 で行うため、
//   本コマンドは単体確認用に**ダミーの LogRecord** を n 件（既定5・範囲1..1000）書く。
//   FIX あり/なし・正負モータを混ぜ、欠損表現（空フィールド）とフォーマットを実機で確認できる。
// 設計（Issue #14 / DoD）:
//   - CSV 整形・欠損サニタイズ・周期 flush は core datalog（ホストテスト済）。
//   - SD(FAT32) 追記・連番ファイル名・同時1ファイル制約は hal::SdLogSink。
static int cmd_log(int argc, char** argv) {
  int count = 5;
  if (argc == 2) {
    if (!cli::parseInt(argv[1], count) || count < 1 || count > 1000) {
      Serial.println("usage: log [n]  (n=1..1000)");
      return -1;
    }
  } else if (argc > 2) {
    Serial.println("usage: log [n]");
    return -1;
  }

  hal::SdLogSink sink;
  datalog::FlightLogger logger(sink);
  if (!logger.begin()) {
    Serial.println("log: SD を開けません（カード有無・FAT32 フォーマットを確認）");
    return -1;
  }
  Serial.print("log: 記録先 ");
  Serial.println(sink.path());

  for (int i = 0; i < count; i++) {
    datalog::LogRecord r;
    r.timestampMs = millis();
    r.state = static_cast<uint8_t>(i % 5);  // ダミー状態コード
    // 偶数=FIXあり（位置系を出力）/ 奇数=FIXなし（位置系は空フィールドになる）。
    if ((i % 2) == 0) {
      r.hasFix = true;
      r.latitudeDeg = 35.6812345 + i * 1e-5;
      r.longitudeDeg = 139.7671234 + i * 1e-5;
      r.velocityMps = 1.25f;
      r.courseDeg = 90.5f;
      r.distanceM = 12.75f;
      r.bearingDeg = 95.5f;
    }
    r.headingDeg = static_cast<float>((i * 10) % 360);  // IMU 姿勢は常時出す
    r.motorLeft = static_cast<int16_t>((i % 2) ? -150 : 200);  // 正負を混在
    r.motorRight = 200;
    if (!logger.log(r)) {
      Serial.print("log: 書き込み失敗（");
      Serial.print(i);
      Serial.println(" 件目・SD 満杯/抜けの可能性）");
      return -1;
    }
  }
  logger.flush();
  Serial.print("log: ");
  Serial.print(logger.recordsWritten());
  Serial.println(" 件を記録しました（SD を取り出し CSV を確認）");
  return 0;
}

// ---- cam（カメラ撮像・赤コーン検出, Issue #52）----
//
//   cam init   : カメラ初期化＋ホワイトバランスを DAYLIGHT 固定（オンデマンド。gotchas B9）
//   cam snap   : VGA JPEG を撮影し SD (cam/imgNNN.jpg) へ保存（目視確認・学習データ用）
//   cam dump   : QVGA YUV422(UYVY) 生フレームを SD (cam/imgNNN.yuv) へ保存
//                （ホストテストの実写ダンプ用。PC では ffmpeg -f rawvideo -pix_fmt uyvy422
//                 -video_size 320x240 -i img000.yuv img000.png で閲覧できる）
//   cam detect : 1フレーム取得して赤コーン検出結果と処理時間（撮像/検出）を表示
//   cam mon [n]: 検出を n 回連続表示（既定10・上限1000）。非改行キーで中断（gotchas B10）
//   cam thr    : 検出の色閾値を表示 / cam thr <yMin> <yMax> <uMax> <vMin> で設定（各0-255）
//
// 設計: 検出ロジックは core/cone_detect（合成画像でホストテスト済）。本ハンドラは
//   hal::SpresenseCamera（撮像）と hal::SdImageStore（SD保存）の薄い呼び出しのみ。
//   検出結果 I/F（detected/方位角/近接度/信頼度）は Phase3 のナビ(#18)・ゴール判定(#19)が
//   購読する形をそのまま表示する。閾値・画角の実機校正手順は camera_bringup.md。

// カメラ初期化済みかを検査し、未初期化なら案内を出す。
static bool cam_require_ready() {
  if (!g_camera.ready()) {
    Serial.println("cam: 未初期化です。先に 'cam init' を実行してください");
    return false;
  }
  return true;
}

// 検出結果を1行で表示する（Phase3 が購読するフィールドをそのまま出す）。
static void cam_print_detection(const cone::Detection& d) {
  if (!d.detected) {
    Serial.println("detected=0 (コーン未検出)");
    return;
  }
  Serial.print("detected=1 bearing=");
  Serial.print(d.bearingDeg, 1);
  Serial.print("deg center=");
  Serial.print(d.centerColumn);
  Serial.print(" width=");
  Serial.print(d.widthColumns);
  Serial.print("col ratio=");
  Serial.print(d.widthRatio, 3);
  Serial.print(" conf=");
  Serial.print(d.confidence, 2);
  Serial.print(" pixels=");
  Serial.println(d.redPixels);
}

// 1フレーム取得→検出→表示。処理時間（撮像/検出）も表示する（Phase3 制御周期との整合確認用）。
// 戻り値 0=成功（未検出でも撮像・解析が成立すれば成功）。
// フレームはビデオストリーミング経由（still の YUV422 は実機で失敗する。gotchas B23）。
static int cam_detect_once() {
  const unsigned long t0 = micros();
  const uint8_t* frame = g_camera.captureDetectFrame();
  if (frame == nullptr) {
    Serial.println("cam: フレーム取得失敗（ストリーミング応答なし。カメラ接続を確認し cam init からやり直し）");
    return -1;
  }
  const unsigned long t1 = micros();
  const cone::Detection d = cone::detect(frame, hal::SpresenseCamera::kDetectWidth,
                                         hal::SpresenseCamera::kDetectHeight, g_coneCfg);
  const unsigned long t2 = micros();
  Serial.print("capture=");
  Serial.print((t1 - t0) / 1000UL);
  Serial.print("ms detect=");
  Serial.print((t2 - t1) / 1000UL);
  Serial.print("ms | ");
  cam_print_detection(d);
  return 0;
}

// data の len バイトを SD の cam/imgNNN.<ext> へ保存する共通処理。
static int cam_store_to_sd(const uint8_t* data, size_t len, const char* ext) {
  hal::SdImageStore store;
  if (!store.begin()) {
    Serial.println("cam: SD を開けません（カード有無・FAT32 フォーマットを確認）");
    return -1;
  }
  if (!store.save(data, len, ext)) {
    Serial.println("cam: SD 保存失敗（空き番号なし/満杯の可能性。SD を空にして再実行）");
    return -1;
  }
  Serial.print("cam: 保存 ");
  Serial.print(store.path());
  Serial.print(" (");
  Serial.print(len);
  Serial.println(" bytes)");
  return 0;
}

// cam snap: VGA JPEG を still 撮影して SD へ保存する。
static int cam_save_snap() {
  CamImage img;
  if (!g_camera.captureJpeg(img)) {
    if (g_camera.lastError() != CAM_ERR_SUCCESS) {
      Serial.print("cam: 撮影失敗 (");
      Serial.print(g_camera.lastErrorName());
      Serial.println(") カメラボード接続を確認");
    } else {
      // CamErr を返す API は成功しているのに takePicture が空を返したケース
      //（still バッファ未解放・デバイス状態異常など）。SUCCESS 表示は紛らわしいので区別する。
      Serial.println("cam: 撮影失敗（takePicture が空応答。再実行しても続く場合はリセット）");
    }
    return -1;
  }
  return cam_store_to_sd(img.getImgBuff(), img.getImgSize(), "jpg");
}

// cam dump: 検出用 QVGA YUV422(UYVY) フレームをストリーミング経由で取得し SD へ保存する
//（ホストテストの実写ダンプ・学習データ用）。
static int cam_save_dump() {
  const uint8_t* frame = g_camera.captureDetectFrame();
  if (frame == nullptr) {
    Serial.println("cam: フレーム取得失敗（ストリーミング応答なし。カメラ接続を確認し cam init からやり直し）");
    return -1;
  }
  return cam_store_to_sd(frame, hal::SpresenseCamera::kDetectFrameBytes, "yuv");
}

static int cmd_cam(int argc, char** argv) {
  if (argc == 2 && strcmp(argv[1], "init") == 0) {
    if (g_camera.ready()) {
      Serial.println("cam: 初期化済み");
      return 0;
    }
    if (!g_camera.begin()) {
      Serial.print("cam: 初期化失敗 (");
      Serial.print(g_camera.lastErrorName());
      Serial.println(") カメラボードの接続を確認");
      return -1;
    }
    // 屋外の色閾値を安定させるため WB は DAYLIGHT 固定（AWB だと至近の全面赤で色相が
    // 引っ張られる恐れ）。失敗しても撮像は可能なので警告のみで継続する。
    if (!g_camera.setDaylightWhiteBalance()) {
      Serial.print("cam: 警告: WB(DAYLIGHT) 設定失敗 (");
      Serial.print(g_camera.lastErrorName());
      Serial.println(") AWB のまま続行");
    }
    Serial.println("cam: 初期化完了（QVGA/YUV422, WB=DAYLIGHT）");
    return 0;
  }

  if (argc == 2 && strcmp(argv[1], "snap") == 0) {
    if (!cam_require_ready()) return -1;
    return cam_save_snap();
  }

  if (argc == 2 && strcmp(argv[1], "dump") == 0) {
    if (!cam_require_ready()) return -1;
    return cam_save_dump();
  }

  if (argc == 2 && strcmp(argv[1], "detect") == 0) {
    if (!cam_require_ready()) return -1;
    return cam_detect_once();
  }

  if ((argc == 2 || argc == 3) && strcmp(argv[1], "mon") == 0) {
    int count = 10;  // 既定回数
    if (argc == 3) {
      if (!cli::parseInt(argv[2], count) || count < 1 || count > 1000) {
        Serial.println("回数は 1-1000 の整数");
        return -1;
      }
    }
    if (!cam_require_ready()) return -1;
    Serial.println("cam mon: 連続検出（中断: 非改行キー）");
    for (int i = 0; i < count; i++) {
      Serial.print("[");
      Serial.print(i + 1);
      Serial.print("/");
      Serial.print(count);
      Serial.print("] ");
      if (cam_detect_once() != 0) {
        return -1;  // 撮像失敗が続く状態で回し続けない
      }
      // 撮像自体が数百ms かかり自然なペースになるため待ちは入れず、サイクル間で中断キーだけ
      // 確認する。コマンド確定の残留改行（CRLF の LF）を中断と誤認しないよう CR/LF は
      // 読み飛ばし、非改行バイトのみを中断トリガーにする（gotchas B10）。
      bool aborted = false;
      while (Serial.available()) {
        int c = Serial.read();
        if (c >= 0 && c != '\r' && c != '\n') {
          while (Serial.available()) {
            Serial.read();  // 残りの入力も読み捨てて中断
          }
          aborted = true;
          break;
        }
      }
      if (aborted) {
        Serial.println("cam mon: 中断しました");
        break;
      }
    }
    return 0;
  }

  if ((argc == 2 || argc == 6) && strcmp(argv[1], "thr") == 0) {
    if (argc == 6) {
      // 数値化は厳密パーサで行い（gotchas B1: atoi 禁止）、uint8_t 範囲と
      // yMin<=yMax の整合を検査してから反映する（B2/B7: 下限・上限の両方）。
      int vals[4];
      for (int i = 0; i < 4; i++) {
        if (!cli::parseInt(argv[2 + i], vals[i]) || vals[i] < 0 || vals[i] > 255) {
          Serial.println("usage: cam thr <yMin> <yMax> <uMax> <vMin>  (各 0-255)");
          return -1;
        }
      }
      if (vals[0] > vals[1]) {
        Serial.println("cam thr: yMin <= yMax にしてください");
        return -1;
      }
      g_coneCfg.yMin = static_cast<uint8_t>(vals[0]);
      g_coneCfg.yMax = static_cast<uint8_t>(vals[1]);
      g_coneCfg.uMax = static_cast<uint8_t>(vals[2]);
      g_coneCfg.vMin = static_cast<uint8_t>(vals[3]);
    }
    Serial.print("cam thr: yMin=");
    Serial.print(g_coneCfg.yMin);
    Serial.print(" yMax=");
    Serial.print(g_coneCfg.yMax);
    Serial.print(" uMax=");
    Serial.print(g_coneCfg.uMax);
    Serial.print(" vMin=");
    Serial.print(g_coneCfg.vMin);
    Serial.print(" | minCol=");
    Serial.print(g_coneCfg.minColumnCount);
    Serial.print(" maxGap=");
    Serial.print(g_coneCfg.maxGapColumns);
    Serial.print(" minWidth=");
    Serial.print(g_coneCfg.minWidthColumns);
    Serial.print(" minPixels=");
    Serial.print(g_coneCfg.minRedPixels);
    Serial.print(" hfov=");
    Serial.println(g_coneCfg.hfovDeg, 1);
    return 0;
  }

  Serial.println("usage: cam <init|snap|dump|detect|mon [n]|thr [yMin yMax uMax vMin]>");
  return -1;
}

// NT-Shell が確定した1行をトークン化し、コマンドを実行する。
static int execute_line(const char* text) {
  static char buf[128];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* argv[16];
  int argc = cli::tokenize(buf, argv, 16);
  if (argc == 0) {
    return 0;  // 空行は無視
  }
  int idx = cli::findCommand(kCommands, kCommandCount, argv[0]);
  if (idx < 0) {
    Serial.print("unknown command: ");
    Serial.println(argv[0]);
    Serial.println("type 'help'");
    return -1;
  }
  return kCommands[idx].handler(argc, argv);
}

// ---- NT-Shell コールバック ----
//
// 改行の正規化: NT-Shell は CR(0x0d) のみを「行確定(enter)」として扱う。
// シリアルモニタの改行設定（CR / LF / CRLF）に依らず確定できるよう、
// LF 単独は CR に変換し、CRLF の LF は読み飛ばして二重確定を防ぐ。
static int func_read(char* buf, int cnt, void* /*extobj*/) {
  static char prev = 0;
  int n = 0;
  while (n < cnt && Serial.available()) {
    char raw = (char)Serial.read();
    char c = raw;
    if (raw == '\n') {
      if (prev == '\r') {  // CRLF の LF は読み飛ばす（CR で確定済み）
        prev = raw;
        continue;
      }
      c = '\r';  // LF 単独は CR に変換して確定させる
    }
    prev = raw;
    buf[n++] = c;
  }
  return n;
}

static int func_write(const char* buf, int cnt, void* /*extobj*/) {
  return Serial.write(reinterpret_cast<const uint8_t*>(buf), cnt);
}

static int func_callback(const char* text, void* /*extobj*/) {
  return execute_line(text);
}

void setup() {
  // 最優先（誤加熱・発火防止）: 電源投入後できるだけ早く電熱線 D06 を出力＋LOW（非加熱）へ確定する。
  // GPIO 操作は Serial 非依存なので Serial.begin より前に置き、ソフト側のブート窓を最小化する
  // （gotchas B18）。ただしスケッチ起動前のブート期間は救えないため、恒久対策は FET ゲートの
  // プルダウン抵抗（HW, separator_bringup.md 手順A）。以後の加熱は 'separate' が都度ローカル
  // separator を生成して行う（一時オブジェクトだがピンの電気状態は残る）。
  separator::ParachuteSeparator(g_gpio, hal::kHeatingWirePin).begin();

  Serial.begin(115200);
  delay(500);  // 起動直後の出力安定待ち（Serial は CP210x UART で接続検知不可）

  g_motor.begin();  // モータ6ピンを出力設定（起動直後は停止のまま）

  // 9軸センサは setup() で初期化しない。Adafruit begin() はソフトリセット後の CHIP_ID 待ちが
  // 無限ループになり得る（一度 ACK 後にブラウンアウト/活線抜けで応答が戻らない場合）。ここで呼ぶと
  // シェル全体が起動前にハングし、無関係なコマンドも使えなくなる。初期化は 'imu init' のオンデマンドに
  // して、起動（プロンプト表示）を絶対に止めないようにする（gotchas B9）。
  Serial.println("imu: 9軸センサ(BNO055)は 'imu init' で初期化してください");
  // GNSS も同様に setup() で測位を待ち切らない（COLD_START は FIX まで数十秒〜数分。gotchas B15）。
  Serial.println("gnss: 内蔵GNSSは 'gnss init' で初期化してください（FIX まで屋外で数十秒〜数分）");
  // カメラも同様にオンデマンド初期化（ドライバ初期化＋約150KBのバッファ確保を起動経路に置かない）。
  Serial.println("cam: カメラボードは 'cam init' で初期化してください");

  ntshell_init(&ntshell, func_read, func_write, func_callback, (void*)(&ntshell));
  ntshell_set_prompt(&ntshell, PROMPT_STR);

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" 100kinSAT comeback 2026 / on-target shell");
  Serial.println(" type 'help' for command list. baud=115200");
  Serial.println("=============================================");
  Serial.print(PROMPT_STR);
  Serial.flush();
}

void loop() {
  while (Serial.available()) {
    ntshell_execute_spresense_arduino(&ntshell);
  }
}
