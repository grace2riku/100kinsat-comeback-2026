#ifndef HAL_SPRESENSE_CAMERA_H
#define HAL_SPRESENSE_CAMERA_H

#include <Camera.h>

// spresense_camera.h - Spresense カメラボードの実機ラッパ（hal層, Camera.h 依存）
//
// Spresense Camera ライブラリ（theCamera シングルトン）を薄く包み、赤コーン検出
// （src/lib/core/cone_detect, ホストテスト済）へ渡す QVGA YUV422(UYVY) フレームと、
// bring-up / 学習データ収集用の JPEG スナップを同期取得する。Camera.h（Spresense
// Boards 同梱）に依存するため実機ビルド（arduino-cli）でのみ使い、NT-Shell の cam
// コマンドで実機確認する（camera_bringup.md）。
//
// 設計（Issue #52）:
//   - カメラボードはメインボード専用コネクタ（CXD5602 パラレル CIF）接続で、
//     既存ピンアサイン（モータ/I2C/D06/D09 等）とは競合しない（hardware.md）。
//   - ストリーミング（startStreaming）は使わない。コールバックスレッドとの共有・
//     排他を避け、takePicture() の同期取得だけで単体機能を成立させる（結合 #17/#18
//     で周期が足りなければストリーミング化を再検討）。
//   - begin() は起動経路で呼ばない（gotchas B9: デバイス初期化は UI 応答後に
//     オンデマンド。`cam init` から呼ぶ）。begin(buff_num=1, 5fps, QVGA, YUV422) で
//     ビデオバッファを最小(約150KB)に抑える。メモリ共存（GNSS/TWELITE/datalog）は
//     bring-up で確認する。
//   - CAM_IMAGE_PIX_FMT_YUV422 は V4L2_PIX_FMT_UYVY（UYVY バイト順）。core 側の
//     前提と一致させる（cone_detect.h 冒頭）。
//   - 屋外運用ではホワイトバランスを AWB 任せにすると、コーンが画面を占める至近で
//     赤に引っ張られ色相が回る恐れがあるため、DAYLIGHT 固定を用意する
//     （setDaylightWhiteBalance。cam init が呼び、結果はコマンド側で表示）。
//   - takePicture() は CamErr を返さない（CamImage.isAvailable() で成否判定）。
//     CamErr を返す API の失敗は lastError()/lastErrorName() に保持する。

namespace hal {

class SpresenseCamera {
 public:
  // 検出用フレーム寸法（QVGA）。core 側 cone::kMaxDetectWidth/Height と一致する。
  static constexpr int kDetectWidth = CAM_IMGSIZE_QVGA_H;   // 320
  static constexpr int kDetectHeight = CAM_IMGSIZE_QVGA_V;  // 240
  // スナップ（JPEG）寸法。目視確認・学習データ用途で VGA を使う。
  static constexpr int kSnapWidth = CAM_IMGSIZE_VGA_H;   // 640
  static constexpr int kSnapHeight = CAM_IMGSIZE_VGA_V;  // 480

  // カメラデバイス初期化。二重呼びは何もせず成功。失敗時 false（lastError 参照）。
  // ビデオストリームは最小構成（バッファ1面・5fps・QVGA/YUV422）で開くだけで、
  // startStreaming はしない。still フォーマットは capture 側で必要時に設定する。
  bool begin() {
    if (begun_) {
      return true;
    }
    lastErr_ =
        theCamera.begin(1, CAM_VIDEO_FPS_5, kDetectWidth, kDetectHeight, CAM_IMAGE_PIX_FMT_YUV422);
    if (lastErr_ != CAM_ERR_SUCCESS) {
      return false;
    }
    begun_ = true;
    stillWidth_ = 0;  // still フォーマット未設定
    return true;
  }

  // begin() が成功済みか。
  bool ready() const { return begun_; }

  // 屋外（昼光）向けにホワイトバランスを DAYLIGHT 固定にする。失敗時 false。
  // 検出の色閾値を安定させるため cam init から呼ぶ（失敗しても撮像自体は可能）。
  bool setDaylightWhiteBalance() {
    if (!begun_) {
      return false;
    }
    lastErr_ = theCamera.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_DAYLIGHT);
    return lastErr_ == CAM_ERR_SUCCESS;
  }

  // 検出用 QVGA YUV422(UYVY) フレームを同期取得する。失敗時 false（out は不変）。
  // 成功時 out のバッファはライブラリ管理領域で、次の capture まで有効（コピー不要で
  // cone::detect へ渡せる。保持したい場合は呼び出し側でコピーする）。
  bool captureDetectFrame(CamImage& out) {
    if (!ensureStillFormat(kDetectWidth, kDetectHeight, CAM_IMAGE_PIX_FMT_YUV422)) {
      return false;
    }
    CamImage img = theCamera.takePicture();
    if (!img.isAvailable()) {
      return false;
    }
    // 想定寸法・フォーマットであることを検証してから core へ流す（gotchas B7:
    // センサ由来の値は使用前に検査。想定外のバッファを検出器に渡さない）。
    if (img.getWidth() != kDetectWidth || img.getHeight() != kDetectHeight ||
        img.getPixFormat() != CAM_IMAGE_PIX_FMT_YUV422) {
      return false;
    }
    out = img;
    return true;
  }

  // VGA JPEG を同期取得する（SD 保存は hal::SdImageStore が担当）。失敗時 false。
  bool captureJpeg(CamImage& out) {
    if (!ensureStillFormat(kSnapWidth, kSnapHeight, CAM_IMAGE_PIX_FMT_JPG)) {
      return false;
    }
    CamImage img = theCamera.takePicture();
    if (!img.isAvailable()) {
      return false;
    }
    out = img;
    return true;
  }

  // カメラ停止（メモリ解放）。再開は begin() から。
  void end() {
    if (begun_) {
      theCamera.end();
      begun_ = false;
      stillWidth_ = 0;
    }
  }

  // CamErr を返した直近 API の結果（takePicture の失敗はここに乗らない点に注意）。
  CamErr lastError() const { return lastErr_; }
  const char* lastErrorName() const { return camErrName(lastErr_); }

  // CamErr の表示名（シリアルでの実機切り分け用）。
  static const char* camErrName(CamErr err) {
    switch (err) {
      case CAM_ERR_SUCCESS:
        return "SUCCESS";
      case CAM_ERR_NO_DEVICE:
        return "NO_DEVICE";
      case CAM_ERR_ILLEGAL_DEVERR:
        return "ILLEGAL_DEVERR";
      case CAM_ERR_ALREADY_INITIALIZED:
        return "ALREADY_INITIALIZED";
      case CAM_ERR_NOT_INITIALIZED:
        return "NOT_INITIALIZED";
      case CAM_ERR_NOT_STILL_INITIALIZED:
        return "NOT_STILL_INITIALIZED";
      case CAM_ERR_CANT_CREATE_THREAD:
        return "CANT_CREATE_THREAD";
      case CAM_ERR_INVALID_PARAM:
        return "INVALID_PARAM";
      case CAM_ERR_NO_MEMORY:
        return "NO_MEMORY";
      case CAM_ERR_USR_INUSED:
        return "USR_INUSED";
      case CAM_ERR_NOT_PERMITTED:
        return "NOT_PERMITTED";
      default:
        return "UNKNOWN";
    }
  }

 private:
  // still フォーマットが (w,h,fmt) でなければ設定し直す（同一なら再設定しない）。
  bool ensureStillFormat(int width, int height, CAM_IMAGE_PIX_FMT fmt) {
    if (!begun_) {
      lastErr_ = CAM_ERR_NOT_INITIALIZED;
      return false;
    }
    if (stillWidth_ == width && stillFmt_ == fmt) {
      return true;
    }
    lastErr_ = theCamera.setStillPictureImageFormat(width, height, fmt);
    if (lastErr_ != CAM_ERR_SUCCESS) {
      stillWidth_ = 0;
      return false;
    }
    stillWidth_ = width;
    stillFmt_ = fmt;
    return true;
  }

  CamErr lastErr_ = CAM_ERR_SUCCESS;
  bool begun_ = false;
  int stillWidth_ = 0;  // 0 = still フォーマット未設定
  CAM_IMAGE_PIX_FMT stillFmt_ = CAM_IMAGE_PIX_FMT_NONE;
};

}  // namespace hal

#endif  // HAL_SPRESENSE_CAMERA_H
