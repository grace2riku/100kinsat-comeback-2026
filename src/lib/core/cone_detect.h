#ifndef CORE_CONE_DETECT_H
#define CORE_CONE_DETECT_H

// cone_detect - 赤カラーコーン検出（ハードウェア非依存）
//
// Spresense カメラボードで取得した YUV422 フレームから、種目5の目標物
// 「赤色無地カラーコーン（高さ70cm・幅37.5cm）」を検出する純粋ロジック。
// カメラ HW には触れないためホストPCでユニットテストできる
// （test/core/test_cone_detect.cpp。合成画像＋実機で取得した実写ダンプ）。
//
// 入力フォーマット（重要）:
//   Spresense Camera ライブラリの CAM_IMAGE_PIX_FMT_YUV422 は V4L2_PIX_FMT_UYVY、
//   すなわち **UYVY バイト順**（2画素4バイト [U0, Y0, V0, Y1]、U/V は2画素で共有）。
//   一般的な YUYV（[Y0, U0, Y1, V0]）とは異なる点に注意（取り違えると輝度と色差が
//   入れ替わり検出が全滅する）。実写ダンプでバイト順を実機確認する（camera_bringup.md）。
//
// アルゴリズム（Issue #52 設計）:
//   1. 各画素の (Y,U,V) を赤閾値で判定（Cr=V が高く Cb=U が低い。輝度は帯域で
//      黒つぶれ・白飛びを除外）。閾値は Config で屋外光条件に合わせ調整可能。
//   2. 列ごとの赤画素数ヒストグラムを作る（コーンは縦長なので列方向に強く出る）。
//   3. アクティブ列（赤画素数が minColumnCount 以上）を maxGapColumns まで
//      ギャップ許容で連結し、赤画素総数が最大の区間を選ぶ（複数赤領域対策）。
//   4. 区間の重心列から画角内方位角 [deg]（右+/左-）、区間幅から近接度
//      （widthRatio）、区間内赤密度から信頼度（confidence）を出す。
//
// 出力 I/F は Phase3 のナビ（#18）・ゴール判定（#19）が購読する形
// （detected / bearingDeg / widthRatio / confidence）。
//
// 関連: Issue #52（本モジュール）/ #29（Phase5 精度作り込み）
// 参照: doc/development/spresense_gotchas.md（B7 値域検査・B19 sanitize）

#include <stdint.h>

namespace cone {

// detect() が受け付ける最大画像寸法。検出は QVGA(320x240) 前提
// （幅は列ヒストグラムをスタックに置くための上限、高さは uint16_t の列カウントが
// あふれない上限を兼ねる。VGA 以上は解析せず失敗を返す）。
constexpr int kMaxDetectWidth = 320;
constexpr int kMaxDetectHeight = 240;

// 赤判定・区間抽出のパラメータ。
// 既定値は BT.601 系 YCbCr（無彩色 U=V=128）での「彩度の高い赤」の初期値で、
// 屋外の順光/逆光/日陰では実機で調整する（camera_bringup.md に記録）。
struct Config {
  // -- 赤画素判定（8bit YUV。U=Cb, V=Cr） --
  uint8_t yMin = 32;   // 輝度下限（黒つぶれ・影の誤検出除外）
  uint8_t yMax = 235;  // 輝度上限（白飛び除外）
  uint8_t uMax = 120;  // Cb 上限（赤は青成分が小さい）
  uint8_t vMin = 160;  // Cr 下限（赤は赤成分が大きい）
  // -- 列ヒストグラム→区間抽出 --
  int minColumnCount = 8;   // 列をアクティブとみなす最小赤画素数（ノイズ列除去）
  int maxGapColumns = 4;    // 区間連結で許容する非アクティブ列数
  int minWidthColumns = 4;  // 検出とみなす最小区間幅 [列]
  int minRedPixels = 64;    // 検出とみなす区間内の最小赤画素数
  // -- 方位角換算 --
  // 水平画角 [deg]。カメラボード CXD5602PWBCAM1 はレンズ画角 78°±3°（対角）・
  // EFL 2.74mm・1/4型センサから水平は約 66° と見積もれるが、必ず実機で校正する。
  double hfovDeg = 66.0;
};

// 検出結果。非検出時は detected=false で他フィールドは安全値
// （bearingDeg=0 / centerColumn=-1 / 幅・信頼度 0）。
struct Detection {
  bool detected = false;
  double bearingDeg = 0.0;  // 画角内方位角 [deg]。画像中央=0、右+/左-
  int centerColumn = -1;    // 選択区間の重心列 [0, width)（四捨五入）
  int widthColumns = 0;     // 選択区間の幅 [列]
  double widthRatio = 0.0;  // widthColumns / width（近接度指標。近いほど大）
  double confidence = 0.0;  // 区間内赤密度 = redPixels / (widthColumns*height) ∈ [0,1]
  int redPixels = 0;        // 選択区間内の赤画素総数
};

// 設定を安全な範囲へクランプする（B19: 派生条件が破綻しないことを保証）。
//   minColumnCount/minWidthColumns/minRedPixels は 1 以上、maxGapColumns は 0 以上、
//   hfovDeg は有限かつ (0,180) の範囲外なら既定値 66.0 へ戻す。
// yMin>yMax や uMax/vMin の矛盾は「赤画素が存在しない」となり検出なし（安全側）に
// 落ちるため、値の入替はしない。
Config sanitizeConfig(const Config& cfg);

// 1画素の (Y,U,V) が赤か。境界値は「含む」（yMin<=y<=yMax, u<=uMax, v>=vMin）。
bool isRedYuv(uint8_t y, uint8_t u, uint8_t v, const Config& cfg);

// UYVY バッファから列ごとの赤画素数ヒストグラムを作る。
// histogram は呼び出し側が width 要素以上を確保する。
// uyvy が null / width が 0 以下・奇数・kMaxDetectWidth 超 / height が 0 以下・
// kMaxDetectHeight 超の場合は false（histogram は変更しない）。
bool buildColumnHistogram(const uint8_t* uyvy, int width, int height, const Config& cfg,
                          uint16_t* histogram);

// 列位置（連続値。画素 i の中心は i+0.5）→ 画角内方位角 [deg]。
// ピンホールモデルで tan 補正する: atan((x - w/2) / (w/2) * tan(hfov/2))。
double columnToBearingDeg(double columnCenter, int width, double hfovDeg);

// UYVY フレームから赤コーンを検出する。cfg は内部で sanitizeConfig してから使う。
// 入力が不正（buildColumnHistogram と同条件）なら detected=false を返す（安全側）。
Detection detect(const uint8_t* uyvy, int width, int height, const Config& cfg);

}  // namespace cone

#endif  // CORE_CONE_DETECT_H
