#ifndef CORE_DATALOG_H
#define CORE_DATALOG_H

#include <cstddef>
#include <cstdint>
#include <limits>

// datalog - 制御履歴ログ（制御量＋操作量の時系列 CSV 保存）。ハードウェア非依存。
//
// 本種目で順位がつくための必須要件（細則§5「制御履歴とは」/ 大会要領 種目5）。制御量
// （観測値=位置・速度・方位）だけでなく、操作量（モータ指令）も時系列で残すことで、
// 「偶然ゴールに向かった」のではなく合理的に制御した証拠とする。車輪破損・スタックで
// 指令通り動かなくても、操作量の記録があれば制御指令までは正常として部分点評価に使える。
//
// ここに置くのは HW非依存な整形・管理ロジックのみ:
//   - LogRecord → CSV 行への整形（12列・桁数固定・欠損/非有限のサニタイズ）
//   - FlightLogger: LogSink（出力先の抽象）へヘッダ＋各行を書き、一定件数ごとに flush
//   - ログファイル名規約（ゼロ埋め連番のフルパス）
// 実際の SD(SDHCI/FAT32) 書き込みは hal（SDHCI ラッパ）が LogSink を実装して担い、
// フォーマット/欠損/flush 間隔はホストPCでユニットテストできる（test/core/test_datalog.cpp）。
//
// 注意（gotchas A: ターゲット≠ホスト / C5 ヘッダ名衝突）:
//   ヘッダ名は datalog.h とする（Spresense の <File.h>/<SDHCI.h> と大小無視でも衝突しない。
//   file.h / log.h は避ける）。ここで保証できるのは「与えた値を桁数・欠損規約どおり CSV
//   にすること」まで。実際の SD への追記・電源断耐性・容量は実機 bring-up で確認する。

namespace datalog {

// CSV 1行の整形バッファ長 [byte]（'\0' 含む）。12列＋最大桁で 100 前後に対し余裕を持たせる。
constexpr size_t kLineBufSize = 128;

// ログファイルのパス用バッファ長 [byte]（"log/flightNNN.csv" ＋余裕）。
constexpr size_t kPathBufSize = 32;

// ログ保存ディレクトリ（末尾スラッシュ込み）。hal はこの規約で mkdir する。
constexpr char kLogDir[] = "log/";

// 既定の flush 間隔 [レコード数]。この件数ごとに LogSink.flush() し、電源断での損失を抑える。
constexpr uint16_t kDefaultFlushEveryN = 10;

// 1 サンプル分の制御履歴（制御量＋操作量＋状態＋時刻）。HW非依存表現。
// 数値フィールドは「非有限(NaN/Inf)なら CSV で空フィールド」に倒れる（欠損表現）。
// さらに位置由来フィールド（lat/lon/vel/course/dist/bearing）は hasFix=false のとき
// 値の内容に関わらず空にする（未測位の残骸を履歴へ流さない多層防御, gotchas B12）。
// なお欠損化するのは非有限のみで、値域チェックはしない（有限値はそのまま出す契約）。
// 桁数・単位の妥当な有界値を渡すのは呼び出し側（#17/#18）の責務。異常に巨大な有限値を
// 渡すと中間バッファで切り詰められ、行全体が kLineBufSize 超なら log() がそのレコードを
// 落とす（オーバーランはしないが 1 サンプル全損）。
struct LogRecord {
  uint32_t timestampMs = 0;  // 起動からの経過時刻 [ms]（millis 相当）
  uint8_t state = 0;         // ミッション状態コード（#17 MissionState と同値。対応表は doc）
  bool hasFix = false;       // GNSS 位置が有効か（gnss::hasPositionFix 相当）

  // --- 制御量（観測値／推定値） ---
  double latitudeDeg = std::numeric_limits<double>::quiet_NaN();   // 緯度 [deg]
  double longitudeDeg = std::numeric_limits<double>::quiet_NaN();  // 経度 [deg]
  float velocityMps = std::numeric_limits<float>::quiet_NaN();     // 対地速度 [m/s]（GNSS）
  float courseDeg = std::numeric_limits<float>::quiet_NaN();       // 進行方位 [deg]（GNSS/観測）
  float headingDeg = std::numeric_limits<float>::quiet_NaN();      // 姿勢方位 [deg]（9軸/制御用）
  float distanceM = std::numeric_limits<float>::quiet_NaN();       // 目標までの距離 [m]（geo）
  float bearingDeg = std::numeric_limits<float>::quiet_NaN();      // 目標への方位 [deg]（geo）

  // --- 操作量 ---
  int16_t motorLeft = 0;   // 左モータ指令（-255..255, 負=後進）
  int16_t motorRight = 0;  // 右モータ指令（-255..255, 負=後進）
};

// CSV ヘッダ行（末尾 '\n'）を buf に書く。戻り値は snprintf 準拠（'\0' 除く必要長。
// 切り詰め時は bufSize-1 より大きい値）。列順は formatCsvRecord と厳密に一致する。
int formatCsvHeader(char* buf, size_t bufSize);

// 1レコードを CSV 行（末尾 '\n'）に整形する。戻り値は snprintf 準拠。
// hasFix=false の位置由来フィールドと、非有限な数値は空フィールドになる。
int formatCsvRecord(const LogRecord& rec, char* buf, size_t bufSize);

// "log/flightNNN.csv"（NNN=ゼロ埋め3桁, 1000以上は桁が増える）を buf に書く。
// 連番の上限クランプはしない（既存ファイルの上書き衝突を避けるため）。戻り値は snprintf 準拠。
int formatLogFilePath(char* buf, size_t bufSize, uint16_t index);

// ログ出力先の抽象（実機=SD の SDHCI 実装 / テスト=モック）。gpio.h と同じ注入方針。
class LogSink {
 public:
  virtual ~LogSink() = default;
  virtual bool begin() = 0;  // 出力先を開く（SD: マウント＋ファイル open）
  virtual bool append(const char* data, size_t len) = 0;  // 1行を追記（len は '\0' を含まない）
  virtual void flush() = 0;                               // バッファをメディアへ確定（電源断対策）
};

// 制御履歴の記録器。begin() でヘッダを書き、log() で各レコードを整形追記し、
// flushEveryN 件ごとに flush する。begin 前の log は拒否（安全側）。
class FlightLogger {
 public:
  // sink: 出力先（本オブジェクトより長命であること）。flushEveryN: flush 間隔（0 は 1 に補正）。
  explicit FlightLogger(LogSink& sink, uint16_t flushEveryN = kDefaultFlushEveryN);

  // 出力先を開き、CSV ヘッダ行を書く。sink.begin() 失敗時は false（log も以後拒否）。
  bool begin();

  // 1レコードを整形して追記する。未 begin / append 失敗時は false（recordsWritten 据え置き）。
  // 追記成功が flushEveryN の倍数に達したら flush する。
  bool log(const LogRecord& rec);

  // 明示的に flush し、周期 flush のカウンタをリセットする。
  void flush();

  uint32_t recordsWritten() const { return recordsWritten_; }

 private:
  LogSink& sink_;
  uint16_t flushEveryN_;
  uint32_t recordsWritten_;
  uint16_t sinceFlush_;
  bool begun_;
  char buf_[kLineBufSize];  // CSV 行の整形バッファ
};

}  // namespace datalog

#endif  // CORE_DATALOG_H
