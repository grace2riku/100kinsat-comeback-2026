#ifndef HAL_SPRESENSE_CAMERA_H
#define HAL_SPRESENSE_CAMERA_H

#include <Arduino.h>
#include <Camera.h>

#include <atomic>
#include <cstring>
#include <new>

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
//   - **YUV フレームはビデオストリーミング経由で取得する**（gotchas B23）。still 撮影
//     （setStillPictureImageFormat + takePicture）は JPEG では成功するが YUV422 では
//     set が SUCCESS でも takePicture が失敗する（実機確認 2026-07-19。ISX012 の
//     キャプチャ系は JPEG 前提で、YUV は公式サンプル同様ストリーミングが実績パス）。
//     begin() が startStreaming(true, callback) で常時ストリーミングを開始し、
//     コールバック（別スレッド）は「要求フラグが立っているときだけ」内部バッファへ
//     コピーする。captureDetectFrame() は要求を立ててコピー完了をタイムアウト付きで
//     待つ同期 I/F（呼び出し側にスレッドを見せない）。
//   - still（takePicture）は JPEG スナップ専用（captureJpeg）。ストリーミング中の
//     takePicture は公式サンプル camera.ino と同じ実績パス。
//   - begin() は起動経路で呼ばない（gotchas B9: デバイス初期化は UI 応答後に
//     オンデマンド。`cam init` から呼ぶ）。ビデオバッファ1面（約150KB）＋検出用
//     内部バッファ（153,600B, 静的確保）を使う。メモリ共存は bring-up で確認する。
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
  static constexpr size_t kDetectFrameBytes =
      static_cast<size_t>(kDetectWidth) * kDetectHeight * 2;  // UYVY: 2 bytes/px
  // スナップ（JPEG）寸法。目視確認・学習データ用途で VGA を使う。
  static constexpr int kSnapWidth = CAM_IMGSIZE_VGA_H;   // 640
  static constexpr int kSnapHeight = CAM_IMGSIZE_VGA_V;  // 480
  // captureDetectFrame の既定タイムアウト。ビデオは 5fps（フレーム間隔 200ms）なので
  // 2フレーム分＋余裕（ストリーム起動直後の初回遅延を吸収）。
  static constexpr unsigned long kCaptureTimeoutMs = 1000;

  // カメラデバイス初期化＋ビデオストリーミング開始。二重呼びは何もせず成功。
  // 失敗時 false（lastError 参照）。ビデオは最小構成（バッファ1面・5fps・QVGA/YUV422）。
  // fps を上げると検出応答は速くなるが消費電力も増える（変更判断は #18 の制御周期設計で）。
  bool begin() {
    if (begun_) {
      return true;
    }
    lastErr_ =
        theCamera.begin(1, CAM_VIDEO_FPS_5, kDetectWidth, kDetectHeight, CAM_IMAGE_PIX_FMT_YUV422);
    if (lastErr_ != CAM_ERR_SUCCESS) {
      return false;
    }
    // 前回セッションのタイムアウト済み要求（requestSeq > doneSeq）を清算してから
    // ストリーミングを開始する（再 begin 直後にコールバックが古い要求を拾わないように）。
    Shared& s = shared();
    s.doneSeq.store(s.requestSeq.load(std::memory_order_relaxed), std::memory_order_relaxed);
    lastErr_ = theCamera.startStreaming(true, streamCallback);
    if (lastErr_ != CAM_ERR_SUCCESS) {
      theCamera.end();  // 中途半端な初期化を残さない
      return false;
    }
    begun_ = true;
    stillWidth_ = 0;  // still フォーマット未設定
    stillHeight_ = 0;
    return true;
  }

  // begin() が成功済みか。
  bool ready() const { return begun_; }

  // CamImage ハンドルを安全に「空」へ戻す（保持しているフレーム参照を解放する）。
  // 注意: `img = CamImage();` の空代入で解放してはならない。Camera ライブラリの
  // operator=/コピーコンストラクタは RHS の内部ポインタを**無条件に incRef** するため、
  // 空ハンドル（img_buff==NULL）からの代入は NULL 参照で即クラッシュする
  // （gotchas B25 / Codex P1）。ここではデストラクタ明示呼び出し＋placement new の
  // 再構築で解放する（~CamImage は参照解放で NULL 安全、CamImage() は公式ドキュメントに
  // 「空の CamImage インスタンス」と明記された公開コンストラクタ）。
  static void releaseImage(CamImage& img) {
    img.~CamImage();
    new (&img) CamImage();
  }

  // 屋外（昼光）向けにホワイトバランスを DAYLIGHT 固定にする。失敗時 false。
  // 検出の色閾値を安定させるため cam init から呼ぶ（失敗しても撮像自体は可能）。
  bool setDaylightWhiteBalance() {
    if (!begun_) {
      return false;
    }
    lastErr_ = theCamera.setAutoWhiteBalanceMode(CAM_WHITE_BALANCE_DAYLIGHT);
    return lastErr_ == CAM_ERR_SUCCESS;
  }

  // 検出用 QVGA YUV422(UYVY) フレームを同期取得する。
  // 成功時は内部バッファ（kDetectFrameBytes バイト）へのポインタを返す。ポインタは
  // **次の captureDetectFrame 呼び出しまで有効**（保持したい場合は呼び出し側でコピー。
  // タイムアウトで失敗した呼び出しでも、遅延したコールバックが旧バッファ内容を
  // 上書きしうるため「呼び出した時点で」旧ポインタは無効とみなす）。
  // 失敗時（未初期化 / timeoutMs 内にフレームが来ない）は nullptr。
  // 実装: 世代番号で要求を識別し、コールバック側の「自分の世代の完了」だけを受理して
  // ポーリング待ちする（delay(1) 刻み・上限 timeoutMs の有限ブロック。gotchas B5 の
  // 「上限のある待ち」）。同期設計の詳細は Shared のコメント（gotchas B24）。
  const uint8_t* captureDetectFrame(unsigned long timeoutMs = kCaptureTimeoutMs) {
    if (!begun_) {
      return nullptr;
    }
    Shared& s = shared();
    // 新しい要求世代を発行（requestSeq != doneSeq の間だけコールバックがコピーする）
    const uint32_t seq = s.requestSeq.fetch_add(1, std::memory_order_release) + 1;
    const unsigned long start = millis();
    while (millis() - start < timeoutMs) {
      if (s.doneSeq.load(std::memory_order_acquire) == seq) {
        return s.buf;  // 自分の世代の完了のみ受理（古い世代の遅延完了は無視）
      }
      delay(1);
    }
    return nullptr;  // タイムアウト（遅延完了しても doneSeq==旧世代 のため次回要求とは交錯しない）
  }

  // VGA JPEG を同期取得する（SD 保存は hal::SdImageStore が担当）。失敗時 false。
  // still（takePicture）は JPEG 専用（YUV still は実機で失敗する。gotchas B23）。
  // 注意: still バッファは1面のみで、CamImage の参照が残っている間は再キューされない。
  // そのため本関数は最初に out の旧フレーム参照を解放する（同じ変数の再利用で連続取得
  // するループを成立させるため。失敗時も out は無効になる）。out 以外の変数に前の
  // JPEG を保持したまま呼ぶと takePicture が失敗する（同時に保持できるのは1枚）。
  bool captureJpeg(CamImage& out) {
    // lastErr_ を明示リセットする。takePicture の空応答は CamErr に乗らないため、
    // 過去の別 API の失敗値が残っていると呼び出し側の「空応答か API 失敗か」の
    // 切り分け表示を誤らせる（残留値による誤診断の防止）。
    lastErr_ = CAM_ERR_SUCCESS;
    // 旧フレームの参照を先に解放してから撮る（参照が残ると still バッファが再キュー
    // されず、次の takePicture が失敗する。Codex P2 / gotchas B22）。
    // 空 CamImage の代入による解放は NULL 参照になるため releaseImage を使う（B25）。
    releaseImage(out);
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

  // カメラ停止（ストリーミング停止＋メモリ解放）。再開は begin() から。
  void end() {
    if (begun_) {
      theCamera.startStreaming(false);
      theCamera.end();
      begun_ = false;
      stillWidth_ = 0;
      stillHeight_ = 0;
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
  // ストリーミングコールバックと共有する状態（コールバックは Camera ライブラリの
  // 別スレッドから呼ばれる）。同期は std::atomic の世代番号ハンドシェイク（gotchas B24。
  // volatile はメモリ順序を保証せず、2フラグ方式はタイムアウト後の遅延コールバックが
  // 次の要求と交錯して stale フレーム返却/バッファ tearing を起こすため使わない）:
  //   要求側: requestSeq を +1（release）し、doneSeq（acquire）が自世代になるのを待つ
  //   CB 側 : requestSeq（acquire）を読み、doneSeq と異なる（未完了要求あり）ときだけ
  //           検証→memcpy→doneSeq に要求世代を store（release。コピー完了の公開）
  // acquire/release 対で「memcpy 完了 → doneSeq 更新」の可視順序を保証する。
  // 古い世代の遅延完了は doneSeq==旧世代 となり要求側が受理しないため交錯しない。
  // 関数ローカル static で単一実体を保証する（ヘッダオンリーのため。theCamera が
  // シングルトンでストリーミングコールバックは1つしか登録できないので、状態も1組でよい）。
  // 注意: buf（約150KB）は BSS に常時確保される（cam init 前でも消費。メモリ見積りに含める）。
  struct Shared {
    std::atomic<uint32_t> requestSeq{0};  // 要求世代（captureDetectFrame が発行）
    std::atomic<uint32_t> doneSeq{0};     // 完了世代（コールバックがコピー完了時に更新）
    uint8_t buf[kDetectFrameBytes];       // 検出用フレームのコピー先（約150KB, BSS）
  };
  static Shared& shared() {
    static Shared s;
    return s;
  }

  // ストリーミングコールバック（別スレッド）。未完了の要求があるときだけ、想定寸法・
  // フォーマットを検証（gotchas B7）してから内部バッファへコピーする。
  // CamImage のバッファはコールバック中のみ有効なため、必ずコピーして渡す。
  static void streamCallback(CamImage img) {
    Shared& s = shared();
    const uint32_t req = s.requestSeq.load(std::memory_order_acquire);
    if (s.doneSeq.load(std::memory_order_relaxed) == req) {
      return;  // 未完了の要求なし
    }
    if (!img.isAvailable() || img.getWidth() != kDetectWidth || img.getHeight() != kDetectHeight ||
        img.getPixFormat() != CAM_IMAGE_PIX_FMT_YUV422 || img.getImgSize() < kDetectFrameBytes) {
      return;  // 想定外フレームはコピーせず次のフレームを待つ
    }
    std::memcpy(s.buf, img.getImgBuff(), kDetectFrameBytes);
    s.doneSeq.store(req, std::memory_order_release);  // コピー完了を要求世代とともに公開
  }

  // still フォーマットが (w,h,fmt) でなければ設定し直す（同一なら再設定しない）。
  bool ensureStillFormat(int width, int height, CAM_IMAGE_PIX_FMT fmt) {
    if (!begun_) {
      lastErr_ = CAM_ERR_NOT_INITIALIZED;
      return false;
    }
    // キャッシュキーは判定に使う全パラメータ（幅・高さ・フォーマット）を含める。
    // 幅だけで判定すると「同幅・異高さ」の解像度を将来足したとき古い設定を掴む（gotchas B20）。
    if (stillWidth_ == width && stillHeight_ == height && stillFmt_ == fmt) {
      return true;
    }
    lastErr_ = theCamera.setStillPictureImageFormat(width, height, fmt);
    if (lastErr_ != CAM_ERR_SUCCESS) {
      stillWidth_ = 0;
      stillHeight_ = 0;
      return false;
    }
    stillWidth_ = width;
    stillHeight_ = height;
    stillFmt_ = fmt;
    return true;
  }

  CamErr lastErr_ = CAM_ERR_SUCCESS;
  bool begun_ = false;
  int stillWidth_ = 0;   // 0 = still フォーマット未設定
  int stillHeight_ = 0;  // 同上
  CAM_IMAGE_PIX_FMT stillFmt_ = CAM_IMAGE_PIX_FMT_NONE;
};

}  // namespace hal

#endif  // HAL_SPRESENSE_CAMERA_H
