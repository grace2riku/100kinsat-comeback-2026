#ifndef HAL_SPRESENSE_GNSS_H
#define HAL_SPRESENSE_GNSS_H

#include <Arduino.h>
#include <GNSS.h>

#include "gnss.h"  // core: GnssFix と妥当性/品質判定（HW非依存・ホストテスト済）

// spresense_gnss.h - Spresense 内蔵 GNSS の実機ラッパ（hal層, Spresense GNSS.h 依存）
//
// Spresense の SpzGnss を薄く包み、HW非依存ロジック src/lib/core/gnss が判定できる形
// （gnss::GnssFix）へ SpNavData を変換して返す。GNSS.h（Spresense Boards 同梱）に依存するため
// 実機ビルド（arduino-cli）でのみ使う。位置の妥当性・品質判定（FIX/HDOP）のロジックは core 側
// （test/core/test_gnss.cpp でホストテスト）にあり、本ラッパは NT-Shell の gnss コマンドで実機確認する。
//
// ファイル名を hal/spresense_gnss.h としているのは、core/gnss.h と同名衝突を避けるため
// （arduino-cli は src/lib/core と src/lib/hal を両方 include パスへ入れる）。
//
// 型名 SpzGnss / 各 API は 100kinsat-spresense のサンプル（src/basic/gnss/gnss.ino）と
// software.md §5.5 に準拠する。実機ビルドで型名・シグネチャを最終確認すること。
//
// 設計:
//   - start(COLD_START) 直後は FIX しない（屋外で数十秒〜数分）。FIX 待ちは update() の
//     ポーリングで呼び出し側の制御周期に委ね、HAL 内で待ち切らない。
//   - waitUpdate は「秒」単位のタイムアウト。負値=無限待ちは setup/制御ループを固めるため使わず、
//     0（即 return・ノンブロッキング）で「新データの有無」だけ見る。
//   - posDataExist=0 のとき latitude/longitude は不定。判定は必ず core の hasPositionFix を通す。

namespace hal {

class SpresenseGnss {
 public:
  // GNSS デバイス起動 → 測位系選択(GPS/GLONASS/QZSS L1C/A) → COLD_START で測位開始。
  // 戻り値 false = 起動失敗（begin/start が非0）。begin/start は秒オーダーで戻るが FIX はしない。
  bool begin() {
    if (gnss_.begin() != 0) {  // 0=成功（software.md §5.5 / サンプル）
      begun_ = false;
      return false;
    }
    // L1 帯の GPS/GLONASS に準天頂衛星（QZSS L1C/A）を加える（日本での可視性向上）。software.md §5.5。
    gnss_.select(GPS);
    gnss_.select(GLONASS);
    gnss_.select(QZ_L1CA);
    if (gnss_.start(COLD_START) != 0) {  // 受信履歴なしから初期化
      begun_ = false;
      return false;
    }
    begun_ = true;
    return true;
  }

  // begin() が成功済みか。
  bool ready() const { return begun_; }

  // 新しい測位更新があれば out に詰めて true、無ければ false（out は不変）。
  // waitUpdate(0) はブロックせず「更新の有無」だけを見る（負値の無限待ちは使わない）。
  // GNSS は約1Hz 更新なので、呼び出し側は本関数を周期的にポーリングして最新値を取得する。
  // 注意: posDataExist=0 のときも latitude/longitude/hdop は不定値が入りうる。そのまま out に
  //       写すが、座標を使う前に必ず gnss::hasPositionFix(out) でゲートすること。
  bool update(gnss::GnssFix& out) {
    if (!begun_) {
      return false;
    }
    if (!gnss_.waitUpdate(0)) {  // 0 秒 = 即 return。新データが無ければ false
      return false;
    }
    SpNavData nav;
    gnss_.getNavData(&nav);
    out.posDataExist = (nav.posDataExist != 0);
    out.fixMode = nav.posFixMode;
    out.numSatellites = nav.numSatellites;
    out.latitude = nav.latitude;
    out.longitude = nav.longitude;
    out.velocity = nav.velocity;
    out.direction = nav.direction;
    out.hdop = nav.hdop;
    return true;
  }

 private:
  SpzGnss gnss_;
  bool begun_ = false;
};

}  // namespace hal

#endif  // HAL_SPRESENSE_GNSS_H
