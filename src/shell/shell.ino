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
 *   - help / led / cds / beep は実動作
 *   - motor / imu / gnss / log は Phase2 で hal モジュールを結線して実装するスタブ
 *
 * シリアル: 115200 bps
 */

#include <Arduino.h>

#include "cli.h"
#include "ntshell.h"
extern "C" {
#include "ntshell_spresense_arduino.h"
}

#define PROMPT_STR "100kinsat> "

static ntshell_t ntshell;
static const int LED_PINS[] = {LED0, LED1, LED2, LED3};

// ---- コマンドハンドラ（HW依存。0=成功）----
static int cmd_help(int argc, char** argv);
static int cmd_led(int argc, char** argv);
static int cmd_cds(int argc, char** argv);
static int cmd_beep(int argc, char** argv);
static int cmd_todo(int argc, char** argv);  // Phase2 で実装予定のスタブ

static const cli::Command kCommands[] = {
    {"help", "コマンド一覧を表示", cmd_help},
    {"led", "led <0-3> <on|off> : 内蔵LEDを点灯/消灯", cmd_led},
    {"cds", "cds : 照度センサ(A0)の生値を表示", cmd_cds},
    {"beep", "beep <freq_hz> <ms> : スピーカ(D09)を鳴らす", cmd_beep},
    {"motor", "motor : モータ駆動テスト（Issue #8 で実装）", cmd_todo},
    {"imu", "imu : 9軸センサ読み取り（Issue #9 で実装）", cmd_todo},
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
  int idx = atoi(argv[1]);
  if (idx < 0 || idx > 3) {
    Serial.println("LED index は 0-3");
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
  int freq = atoi(argv[1]);
  int ms = atoi(argv[2]);
  if (freq <= 0 || ms <= 0) {
    Serial.println("freq/ms は正の整数");
    return -1;
  }
  tone(9, freq);  // D09: スピーカ
  delay(ms);
  noTone(9);
  Serial.println("beep done");
  return 0;
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
static int func_read(char* buf, int cnt, void* /*extobj*/) {
  if (Serial.available()) {
    return Serial.readBytes(buf, cnt);
  }
  return 0;
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
