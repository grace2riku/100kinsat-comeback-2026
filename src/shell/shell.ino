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
 *   - help / led / cds / beep / motor / imu は実動作
 *   - gnss / log は Phase2 で hal モジュールを結線して実装するスタブ
 *
 * シリアル: 115200 bps
 */

#include <Arduino.h>

#include "arduino_gpio.h"
#include "bno055.h"
#include "cli.h"
#include "compass.h"
#include "motor.h"
#include "ntshell.h"
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

// ---- コマンドハンドラ（HW依存。0=成功）----
static int cmd_help(int argc, char** argv);
static int cmd_led(int argc, char** argv);
static int cmd_cds(int argc, char** argv);
static int cmd_beep(int argc, char** argv);
static int cmd_motor(int argc, char** argv);
static int cmd_imu(int argc, char** argv);
static int cmd_todo(int argc, char** argv);  // Phase2 で実装予定のスタブ

static const cli::Command kCommands[] = {
    {"help", "コマンド一覧を表示", cmd_help},
    {"led", "led <0-3> <on|off> : 内蔵LEDを点灯/消灯", cmd_led},
    {"cds", "cds : 照度センサ(A0)の生値を表示", cmd_cds},
    {"beep", "beep <freq_hz> <ms> : スピーカ(D09)を鳴らす", cmd_beep},
    {"motor", "motor <forward|back|left|right|stop> [duty 0-255] [ms] : モータ駆動", cmd_motor},
    {"imu", "imu [init|cal|mon [n]] : 9軸センサ(BNO055)の方位/校正を読む", cmd_imu},
    {"gnss", "gnss : GNSS測位（Issue #10 で実装）", cmd_todo},
    {"log", "log : 制御履歴ログ（Issue #14 で実装）", cmd_todo},
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

static int cmd_cds(int /*argc*/, char** /*argv*/) {
  int v = analogRead(A0);
  Serial.print("cds(A0) = ");
  Serial.println(v);
  return 0;
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
  tone(9, freq);  // D09: スピーカ
  delay(ms);
  noTone(9);
  Serial.println("beep done");
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
  if (!compass::isValidHeading(h)) {
    Serial.println("imu: 方位取得に失敗（無効値）");
    return -1;
  }
  Serial.print("heading = ");
  Serial.print(h, 1);
  Serial.println(" deg");
  print_calibration(g_imu.calibration());
  return 0;
}

// imu [init|cal|mon [n]]
//   imu          : 方位角とキャリブレーション状態を1回読む
//   imu init     : センサを(再)初期化する（配線・電源を入れた後に試せる）
//   imu cal      : キャリブレーション状態だけ表示する
//   imu mon [n]  : n回（既定20・上限120）方位/校正を約0.5秒間隔で表示する（校正収束の観察用）
// 注: mon は読み取り専用で HW を駆動しないため安全（gotchas B5）。長時間ブロックを避けるため
//     インターバル中にシリアル入力があれば即中断する（millis ベースのポーリング待ち）。
static int cmd_imu(int argc, char** argv) {
  if (argc > 3) {
    Serial.println("usage: imu [init|cal|mon [n]]");
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

  if (strcmp(argv[1], "cal") == 0) {
    if (argc != 2) {
      Serial.println("usage: imu cal");
      return -1;
    }
    print_calibration(g_imu.calibration());
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
      // インターバル待ち。キー入力があれば残りを打ち切る（長時間ブロックの回避）。
      if (i + 1 < count) {
        unsigned long start = millis();
        bool aborted = false;
        while (millis() - start < 500) {
          if (Serial.available()) {
            while (Serial.available()) {
              Serial.read();  // 入力を読み捨てて中断
            }
            aborted = true;
            break;
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

  Serial.println("usage: imu [init|cal|mon [n]]");
  return -1;
}

static int cmd_todo(int /*argc*/, char** argv) {
  Serial.print(argv[0]);
  Serial.println(" : 未実装。Phase2 の該当 Issue で hal モジュールを結線して実装します。");
  return 0;
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
  Serial.begin(115200);
  delay(500);  // 起動直後の出力安定待ち（Serial は CP210x UART で接続検知不可）

  g_motor.begin();  // モータ6ピンを出力設定（起動直後は停止のまま）

  // 9軸センサを初期化（未検出でもシェルは起動する。後から 'imu init' で再試行可）。
  if (g_imu.begin()) {
    Serial.println("imu: BNO055 検出・初期化 OK");
  } else {
    Serial.println("imu: BNO055 未検出（I2C 0x28 の配線・電源を確認。'imu init' で再試行可）");
  }

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
