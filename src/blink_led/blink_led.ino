/**
 * blink_led.ino - Spresense 内蔵LED Lチカ + シリアル疎通確認
 *
 * 目的:
 *   開発環境（Arduino IDE 2.3.2 / Spresense Boards 3.0.0）で、実機への
 *   ビルド・書き込み・シリアル出力までが一通り通ることを確認する最小スケッチ。
 *   Issue #3「Spresense + Arduino IDE 開発環境の構築と Lチカ動作確認」用。
 *
 * 動作:
 *   - 起動時に 115200bps でバナーを出力する
 *   - 内蔵LED LED0..LED3 をナイトライダー風に順次点灯する
 *   - 1サイクルごとにハートビート（カウンタ・稼働時間）をシリアル出力する
 *
 * ハードウェア:
 *   - Spresense メインボード搭載の 4個LED（LED0, LED1, LED2, LED3）
 *   - 追加配線は不要（メインボード単体で動作確認可能）
 *
 * シリアル:
 *   - ボーレート 115200 bps（本プロジェクトの全例題で共通）
 */

#include <Arduino.h>

static const int LED_PINS[] = {LED0, LED1, LED2, LED3};
static const int LED_COUNT  = sizeof(LED_PINS) / sizeof(LED_PINS[0]);
static const int STEP_MS    = 120;  // 1ステップあたりの点灯時間 [ms]

static uint32_t heartbeat = 0;

/** 指定 index の LED のみ点灯し、それ以外は消灯する */
static void setOnly(int index) {
  for (int i = 0; i < LED_COUNT; i++) {
    digitalWrite(LED_PINS[i], (i == index) ? HIGH : LOW);
  }
}

void setup() {
  Serial.begin(115200);

  // USB CDC のシリアル接続を最大2秒待つ。未接続でも動作は継続する。
  unsigned long start = millis();
  while (!Serial && (millis() - start) < 2000) {
    ; // wait for serial
  }

  for (int i = 0; i < LED_COUNT; i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
  }

  Serial.println();
  Serial.println("=============================================");
  Serial.println(" 100kinSAT comeback 2026 / blink_led");
  Serial.println(" Spresense build & serial sanity check");
  Serial.println(" baud: 115200 bps");
  Serial.print(  " LED count: ");
  Serial.println(LED_COUNT);
  Serial.println("=============================================");
}

void loop() {
  // ナイトライダー: LED0->LED3->LED0 と 1個ずつ点灯を往復させる
  for (int i = 0; i < LED_COUNT; i++) {
    setOnly(i);
    delay(STEP_MS);
  }
  for (int i = LED_COUNT - 2; i > 0; i--) {
    setOnly(i);
    delay(STEP_MS);
  }

  heartbeat++;
  Serial.print("[heartbeat] count=");
  Serial.print(heartbeat);
  Serial.print(" uptime_ms=");
  Serial.println(millis());
}
