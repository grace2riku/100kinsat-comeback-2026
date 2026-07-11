// datalog（制御履歴ログ: 制御量＋操作量の時系列 CSV 整形／SD 追記器）の単体テスト
//
// ルール要件（細則§5「制御履歴とは」/ 大会要領 種目5）: 制御量（観測値=位置・速度・方位）と
// 操作量（モータ指令）の両方を時系列で残す。ここでは HW非依存な部分——
//   - LogRecord → CSV 行への整形（12列・桁数・欠損/NaN サニタイズ・バッファ境界）
//   - FlightLogger（LogSink 注入・ヘッダ書き込み・周期 flush・begin前拒否・失敗伝播）
//   - ログファイル名規約（ゼロ埋め連番のフルパス）
// を doctest で検証する。実際の SD 書き込みは hal（SDHCI）が担い実機 bring-up で確認する。

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "datalog.h"
#include "doctest.h"

namespace {

// LogSink のモック。append された各行を保持し、begin/flush の回数と結果を制御できる。
class MockSink : public datalog::LogSink {
 public:
  std::vector<std::string> lines;  // append された生バイト列（1呼び出し=1要素）
  bool beginResult = true;         // begin() の戻り値（SD マウント失敗の模擬）
  bool appendResult = true;        // append() の戻り値（書き込み失敗の模擬）
  int beginCount = 0;
  int flushCount = 0;

  bool begin() override {
    ++beginCount;
    return beginResult;
  }
  bool append(const char* data, size_t len) override {
    if (!appendResult) {
      return false;
    }
    lines.emplace_back(data, len);
    return true;
  }
  void flush() override { ++flushCount; }
};

// 代表的な「FIX あり・全フィールド有効」レコード。値は 2進で正確 or double7桁で安定な
// ものを選び、浮動小数の丸めでテストが脆くならないようにしている。
datalog::LogRecord makeFullRecord() {
  datalog::LogRecord r;
  r.timestampMs = 12000;
  r.state = 3;
  r.hasFix = true;
  r.latitudeDeg = 35.6812345;
  r.longitudeDeg = 139.7671234;
  r.velocityMps = 1.25f;  // 2進正確
  r.courseDeg = 90.5f;    // 2進正確
  r.headingDeg = 88.0f;   // 2進正確
  r.distanceM = 12.75f;   // 2進正確
  r.bearingDeg = 95.5f;   // 2進正確
  r.motorLeft = 200;
  r.motorRight = -150;
  return r;
}

}  // namespace

TEST_CASE("formatCsvHeader は 12 列の固定ヘッダ行を返す") {
  char buf[datalog::kLineBufSize];
  int n = datalog::formatCsvHeader(buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(std::string(buf) ==
        "t_ms,state,fix,lat_deg,lon_deg,vel_mps,course_deg,heading_deg,dist_m,"
        "bearing_deg,motor_l,motor_r\n");
}

TEST_CASE("formatCsvRecord は FIX ありレコードを桁数どおりに整形する") {
  char buf[datalog::kLineBufSize];
  int n = datalog::formatCsvRecord(makeFullRecord(), buf, sizeof(buf));
  CHECK(n > 0);
  // lat/lon=小数7桁, vel/dist=2桁, course/heading/bearing=1桁, motor=符号付き整数。
  CHECK(std::string(buf) ==
        "12000,3,1,35.6812345,139.7671234,1.25,90.5,88.0,12.75,95.5,200,-150\n");
}

TEST_CASE("formatCsvRecord は hasFix=false のとき位置由来フィールドを空にする") {
  // fix=0 の行は lat/lon/vel/course/dist/bearing が空、heading（9軸姿勢）と
  // motor（操作量）・state・時刻は出る。偶然ゴールでないことを示す操作量は必ず残す。
  datalog::LogRecord r;
  r.timestampMs = 12500;
  r.state = 2;
  r.hasFix = false;
  r.latitudeDeg = 35.0;  // 値が入っていても hasFix=false なら出さない
  r.longitudeDeg = 139.0;
  r.velocityMps = 1.0f;
  r.courseDeg = 10.0f;
  r.headingDeg = 269.5f;  // IMU 由来は FIX に依存せず出す
  r.distanceM = 5.0f;
  r.bearingDeg = 20.0f;
  r.motorLeft = 120;
  r.motorRight = -120;

  char buf[datalog::kLineBufSize];
  int n = datalog::formatCsvRecord(r, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(std::string(buf) == "12500,2,0,,,,,269.5,,,120,-120\n");
}

TEST_CASE("formatCsvRecord は非有限(NaN/Inf)の数値を空フィールドにする") {
  // gotchas B12/B13 と #13 の NaN 凍結の教訓: 値を信用せず、非有限は欠損として空に倒す。
  datalog::LogRecord r = makeFullRecord();
  r.latitudeDeg = std::nan("");                           // FIX でも NaN なら空
  r.headingDeg = std::numeric_limits<float>::infinity();  // +Inf → 空
  r.distanceM = -std::numeric_limits<float>::infinity();  // -Inf → 空

  char buf[datalog::kLineBufSize];
  int n = datalog::formatCsvRecord(r, buf, sizeof(buf));
  CHECK(n > 0);
  // lat=空, heading=空, dist=空。lon/vel/course/bearing/motor は残る。
  CHECK(std::string(buf) == "12000,3,1,,139.7671234,1.25,90.5,,,95.5,200,-150\n");
}

TEST_CASE("formatCsvRecord は後進(負)のモータ操作量を符号付きで出す") {
  datalog::LogRecord r = makeFullRecord();
  r.motorLeft = -255;
  r.motorRight = 255;
  char buf[datalog::kLineBufSize];
  datalog::formatCsvRecord(r, buf, sizeof(buf));
  std::string s(buf);
  CHECK(s.find(",-255,255\n") != std::string::npos);
}

TEST_CASE("formatCsvRecord はバッファが小さくてもオーバーランせず切り詰める") {
  // snprintf 準拠: 戻り値は「切り詰めなければ必要だった長さ」、buf は必ず '\0' 終端。
  char buf[10];
  std::memset(buf, 'X', sizeof(buf));
  int n = datalog::formatCsvRecord(makeFullRecord(), buf, sizeof(buf));
  CHECK(n > static_cast<int>(sizeof(buf)) - 1);  // 本来の長さは 10 を超える
  CHECK(buf[sizeof(buf) - 1] == '\0');           // 終端されている（オーバーランなし）
}

TEST_CASE("formatCsvRecord は bufSize=0 でも書き込まず安全") {
  char guard = 'Z';
  int n = datalog::formatCsvRecord(makeFullRecord(), &guard, 0);
  CHECK(n > 0);         // 必要長は返る
  CHECK(guard == 'Z');  // 1バイトも書かれない
}

TEST_CASE("formatLogFilePath はゼロ埋め連番のフルパスを返す") {
  char buf[datalog::kPathBufSize];
  datalog::formatLogFilePath(buf, sizeof(buf), 0);
  CHECK(std::string(buf) == "log/flight000.csv");
  datalog::formatLogFilePath(buf, sizeof(buf), 5);
  CHECK(std::string(buf) == "log/flight005.csv");
  datalog::formatLogFilePath(buf, sizeof(buf), 42);
  CHECK(std::string(buf) == "log/flight042.csv");
  datalog::formatLogFilePath(buf, sizeof(buf), 123);
  CHECK(std::string(buf) == "log/flight123.csv");
  // 連番が 3 桁を超えても衝突しない（クランプで上書きしない）。
  datalog::formatLogFilePath(buf, sizeof(buf), 1000);
  CHECK(std::string(buf) == "log/flight1000.csv");
}

TEST_CASE("FlightLogger.begin は sink を開きヘッダ行を書き込む") {
  MockSink sink;
  datalog::FlightLogger logger(sink);
  CHECK(logger.begin() == true);
  CHECK(sink.beginCount == 1);
  REQUIRE(sink.lines.size() == 1);
  CHECK(sink.lines[0] ==
        "t_ms,state,fix,lat_deg,lon_deg,vel_mps,course_deg,heading_deg,dist_m,"
        "bearing_deg,motor_l,motor_r\n");
}

TEST_CASE("FlightLogger は二重 begin を拒否する（ヘッダ二重書き込み防止）") {
  MockSink sink;
  datalog::FlightLogger logger(sink);
  CHECK(logger.begin() == true);
  CHECK(logger.begin() == false);  // 2回目は拒否
  // ヘッダは1行のまま（sink.begin は1回目で成功済み。二重ヘッダにならない）。
  CHECK(sink.lines.size() == 1);
}

TEST_CASE("FlightLogger は flushEveryN=0 を 1 に補正し毎回 flush する（境界）") {
  MockSink sink;
  datalog::FlightLogger logger(sink, /*flushEveryN=*/0);  // 0 は 1 に補正
  REQUIRE(logger.begin());
  logger.log(makeFullRecord());
  CHECK(sink.flushCount == 1);  // 毎レコードで flush
  logger.log(makeFullRecord());
  CHECK(sink.flushCount == 2);
}

TEST_CASE("FlightLogger は begin 前の log を拒否する（安全側）") {
  MockSink sink;
  datalog::FlightLogger logger(sink);
  CHECK(logger.log(makeFullRecord()) == false);
  CHECK(sink.lines.empty());
  CHECK(logger.recordsWritten() == 0);
}

TEST_CASE("FlightLogger は sink.begin 失敗時に begin=false でヘッダを書かない") {
  MockSink sink;
  sink.beginResult = false;
  datalog::FlightLogger logger(sink);
  CHECK(logger.begin() == false);
  CHECK(sink.lines.empty());
  // begin 失敗後は log も拒否（begun_ にならない）。
  CHECK(logger.log(makeFullRecord()) == false);
}

TEST_CASE("FlightLogger.log はレコードを整形追記し recordsWritten を増やす") {
  MockSink sink;
  datalog::FlightLogger logger(sink);
  REQUIRE(logger.begin());
  CHECK(logger.log(makeFullRecord()) == true);
  CHECK(logger.log(makeFullRecord()) == true);
  CHECK(logger.recordsWritten() == 2);
  // lines[0]=ヘッダ, [1],[2]=レコード。
  REQUIRE(sink.lines.size() == 3);
  CHECK(sink.lines[1] == "12000,3,1,35.6812345,139.7671234,1.25,90.5,88.0,12.75,95.5,200,-150\n");
}

TEST_CASE("FlightLogger は flushEveryN 毎に flush する（電源断でのデータ損失を抑える）") {
  MockSink sink;
  datalog::FlightLogger logger(sink, /*flushEveryN=*/2);
  REQUIRE(logger.begin());
  logger.log(makeFullRecord());  // 1件目: flush しない
  CHECK(sink.flushCount == 0);
  logger.log(makeFullRecord());  // 2件目: flush する
  CHECK(sink.flushCount == 1);
  logger.log(makeFullRecord());  // 3件目
  CHECK(sink.flushCount == 1);
  logger.log(makeFullRecord());  // 4件目: flush する
  CHECK(sink.flushCount == 2);
}

TEST_CASE("FlightLogger.log は append 失敗を伝播し recordsWritten を増やさない") {
  MockSink sink;
  datalog::FlightLogger logger(sink);
  REQUIRE(logger.begin());
  sink.appendResult = false;  // 以後 append 失敗（SD 満杯/抜け等）
  CHECK(logger.log(makeFullRecord()) == false);
  CHECK(logger.recordsWritten() == 0);
}

TEST_CASE("FlightLogger.flush は明示的に sink.flush し周期カウンタをリセットする") {
  MockSink sink;
  datalog::FlightLogger logger(sink, /*flushEveryN=*/3);
  REQUIRE(logger.begin());
  logger.log(makeFullRecord());  // カウンタ 1
  logger.flush();                // 明示 flush → flushCount=1, カウンタ 0 へ
  CHECK(sink.flushCount == 1);
  logger.log(makeFullRecord());  // カウンタ 1（リセット後）
  logger.log(makeFullRecord());  // カウンタ 2
  CHECK(sink.flushCount == 1);   // まだ flushEveryN(3) に未達
  logger.log(makeFullRecord());  // カウンタ 3 → 周期 flush
  CHECK(sink.flushCount == 2);
}
