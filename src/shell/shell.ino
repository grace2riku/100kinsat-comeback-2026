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
 *   - help / led / cds / beep / motor / imu / gnss / land は実動作
 *   - log は Phase2 で hal モジュールを結線して実装するスタブ
 *
 * シリアル: 115200 bps
 */

#include <Arduino.h>

#include "arduino_gpio.h"
#include "bno055.h"
#include "cli.h"
#include "compass.h"
#include "landing.h"
#include "motor.h"
#include "ntshell.h"
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

// GNSS（Spresense 内蔵）。begin() で COLD_START、update() でノンブロッキングに最新測位を読む。
static hal::SpresenseGnss g_gnss;

// ---- コマンドハンドラ（HW依存。0=成功）----
static int cmd_help(int argc, char** argv);
static int cmd_led(int argc, char** argv);
static int cmd_cds(int argc, char** argv);
static int cmd_beep(int argc, char** argv);
static int cmd_motor(int argc, char** argv);
static int cmd_imu(int argc, char** argv);
static int cmd_gnss(int argc, char** argv);
static int cmd_land(int argc, char** argv);
static int cmd_todo(int argc, char** argv);  // Phase2 で実装予定のスタブ

static const cli::Command kCommands[] = {
    {"help", "コマンド一覧を表示", cmd_help},
    {"led", "led <0-3> <on|off> : 内蔵LEDを点灯/消灯", cmd_led},
    {"cds", "cds : 照度センサ(A0)の生値を表示", cmd_cds},
    {"beep", "beep <freq_hz> <ms> : スピーカ(D09)を鳴らす", cmd_beep},
    {"motor", "motor <forward|back|left|right|stop> [duty 0-255] [ms] : モータ駆動", cmd_motor},
    {"imu", "imu [init|stat|cal|mon [n]|i2cdiag [n]] : 9軸センサ(BNO055)の方位/校正/状態/I2C診断", cmd_imu},
    {"gnss", "gnss [init|mon [n]] : GNSS測位の状態/位置/品質を読む", cmd_gnss},
    {"land", "land [mon [n]] : 加速度から着地(静止)を検知（要 imu init）", cmd_land},
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

  // 9軸センサは setup() で初期化しない。Adafruit begin() はソフトリセット後の CHIP_ID 待ちが
  // 無限ループになり得る（一度 ACK 後にブラウンアウト/活線抜けで応答が戻らない場合）。ここで呼ぶと
  // シェル全体が起動前にハングし、無関係なコマンドも使えなくなる。初期化は 'imu init' のオンデマンドに
  // して、起動（プロンプト表示）を絶対に止めないようにする（gotchas B9）。
  Serial.println("imu: 9軸センサ(BNO055)は 'imu init' で初期化してください");
  // GNSS も同様に setup() で測位を待ち切らない（COLD_START は FIX まで数十秒〜数分。gotchas B15）。
  Serial.println("gnss: 内蔵GNSSは 'gnss init' で初期化してください（FIX まで屋外で数十秒〜数分）");

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
