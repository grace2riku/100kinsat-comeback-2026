#ifndef HAL_SD_IMAGE_STORE_H
#define HAL_SD_IMAGE_STORE_H

#include <File.h>
#include <SDHCI.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

// sd_image_store.h - カメラ画像の microSD 保存（hal層, SDHCI 依存）
//
// cam snap（JPEG）/ cam dump（YUV422 生データ）の保存先。sd_log_sink.h と同じ
// 「空き連番を採番して新規ファイルへ書く」方式で、既存ファイルへの追記事故を防ぐ。
//
// 設計（Issue #52 / sd_log_sink.h の流儀を踏襲）:
//   - begin(): SD マウント → 画像ディレクトリ作成。
//   - save(): cam/imgNNN.<ext> の未使用番号を拡張子ごとに採番し、新規に書いて close。
//     全番号が埋まったら明示的に失敗を返す（FILE_WRITE は追記モードのため、既存
//     ファイルを開くと別画像のデータが混在する。gotchas C7）。
//   - 屋外で取得する実写ダンプは「距離・光条件・時刻」のメタデータ付きで管理する
//     （記録手順は camera_bringup.md。ファイル名の番号と条件表を対で残す）。

namespace hal {

class SdImageStore {
 public:
  SdImageStore() { path_[0] = '\0'; }

  // SD をマウントし画像ディレクトリを作る。失敗時 false。
  bool begin() {
    if (!sd_.begin()) {
      return false;
    }
    sd_.mkdir(kImageDir);  // 既存なら何もしない
    return true;
  }

  // data の len バイトを cam/imgNNN.<ext> の空き番号へ保存する。成功時 true で
  // path() に保存先が入る。data が null・len が 0・空き番号なし・書き込み不足
  // （SD 満杯/抜け）は false。ext は "jpg"/"yuv" 等の拡張子（ドット無し）。
  bool save(const uint8_t* data, size_t len, const char* ext) {
    path_[0] = '\0';
    if (data == nullptr || len == 0 || ext == nullptr || ext[0] == '\0') {
      return false;
    }
    bool found = false;
    for (uint16_t index = 0; index < kMaxImageFiles; ++index) {
      std::snprintf(path_, sizeof(path_), "%s/img%03u.%s", kImageDir,
                    static_cast<unsigned>(index), ext);
      if (!sd_.exists(path_)) {
        found = true;
        break;
      }
    }
    if (!found) {  // 全番号が埋まっている: 運用で SD を空にする（gotchas C7）
      path_[0] = '\0';
      return false;
    }
    File file = sd_.open(path_, FILE_WRITE);
    if (!file) {
      path_[0] = '\0';
      return false;
    }
    const size_t written = file.write(data, len);
    file.close();
    return written == len;
  }

  // 直近に save が書いたファイルのパス（成功時のみ有効）。
  const char* path() const { return path_; }

 private:
  static constexpr const char* kImageDir = "cam";
  static constexpr uint16_t kMaxImageFiles = 1000;  // img000..img999 を走査

  SDClass sd_;
  char path_[24];  // "cam/img999.yuv" + 余裕
};

}  // namespace hal

#endif  // HAL_SD_IMAGE_STORE_H
