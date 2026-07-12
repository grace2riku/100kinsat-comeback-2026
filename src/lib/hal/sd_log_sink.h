#ifndef HAL_SD_LOG_SINK_H
#define HAL_SD_LOG_SINK_H

#include <File.h>
#include <SDHCI.h>

#include <cstddef>
#include <cstdint>

#include "datalog.h"

// sd_log_sink.h - datalog::LogSink の Spresense SD(SDHCI/FAT32) 実機実装
//
// 制御履歴（core datalog が整形した CSV 行）を microSD へ追記する薄いアダプタ。SDHCI.h に
// 依存するため実機ビルド（arduino-cli）でのみ使用する。ホストテストでは MockSink を用いる。
//
// 設計（software.md §5.7 / DoD #14）:
//   - begin(): SD マウント → ログ用ディレクトリ作成 → 空き連番の新規ファイルを開く。
//     既存ファイルは上書きしない（毎回 flightNNN.csv の未使用番号を採番）ので追記モードの
//     挙動に依存しない。**同時に開くファイルは1個**（本 sink は1ファイルのみ保持）。
//   - append(): 1行を write。書けたバイト数が len 未満なら失敗（SD 満杯/抜け）を返す。
//   - flush(): File::flush でメディアへ確定（電源断でのデータ損失を抑える）。
//   - デストラクタで close（スコープ離脱で確実に閉じる）。
//
// 注意（gotchas A: ターゲット≠ホスト）: FILE_WRITE の追記位置・SD の実挙動・電源断耐性は
//   実機 bring-up（datalog_bringup.md）で確認する。ヘッダ名は <File.h>/<SDHCI.h> と大小無視
//   でも衝突しない sd_log_sink.h とする（gotchas C5）。

namespace hal {

class SdLogSink : public datalog::LogSink {
 public:
  SdLogSink() { path_[0] = '\0'; }

  ~SdLogSink() override {
    if (file_) {
      file_.close();
    }
  }

  // SD をマウントし、log/ 配下の未使用な連番ファイルを新規に開く。失敗時 false。
  bool begin() override {
    if (file_) {
      file_.close();  // 再入時に旧ハンドルを確実に閉じる（リーク防止）
    }
    if (!SD_.begin()) {
      return false;
    }
    SD_.mkdir(datalog::kLogDir);  // 既存なら何もしない

    // 既存を上書きしないよう、空いている連番を採番する。
    bool found = false;
    for (uint16_t index = 0; index < kMaxLogFiles; ++index) {
      datalog::formatLogFilePath(path_, sizeof(path_), index);
      if (!SD_.exists(path_)) {
        found = true;
        break;
      }
    }
    // 全番号が埋まっているときは失敗を返す。FILE_WRITE は追記モードのため、既存ファイルを
    // 開くと二重ヘッダ＋別フライトのレコードが混在する（open は成功し気づけない）。
    // 運用上は SD を空にして回避する（gotchas C7）。
    if (!found) {
      path_[0] = '\0';
      return false;
    }
    file_ = SD_.open(path_, FILE_WRITE);
    return static_cast<bool>(file_);
  }

  bool append(const char* data, size_t len) override {
    if (!file_) {
      return false;
    }
    const size_t written = file_.write(reinterpret_cast<const uint8_t*>(data), len);
    return written == len;
  }

  void flush() override {
    if (file_) {
      file_.flush();
    }
  }

  // 実際に開いたログファイルのパス（begin 後に有効）。
  const char* path() const { return path_; }

 private:
  static constexpr uint16_t kMaxLogFiles = 1000;  // flight000..flight999 を走査

  SDClass SD_;
  File file_;
  char path_[datalog::kPathBufSize];
};

}  // namespace hal

#endif  // HAL_SD_LOG_SINK_H
