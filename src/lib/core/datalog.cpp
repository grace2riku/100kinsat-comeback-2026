#include "datalog.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace datalog {

namespace {

// 数値フィールドを書式化する。valid かつ有限(finite)なら fmt で整形、そうでなければ空文字列。
// 「非有限(NaN/Inf)は空」「hasFix=false の位置由来は空」を一箇所に集約する（欠損表現の統一）。
// float も可変長引数で double に昇格するため fmt は "%.7f" 等の double 用でよい。
template <typename T>
void writeNumOrEmpty(char* out, size_t n, T v, bool valid, const char* fmt) {
  if (n == 0) {
    return;
  }
  if (valid && std::isfinite(v)) {
    std::snprintf(out, n, fmt, static_cast<double>(v));
  } else {
    out[0] = '\0';
  }
}

}  // namespace

int formatCsvHeader(char* buf, size_t bufSize) {
  return std::snprintf(buf, bufSize,
                       "t_ms,state,fix,lat_deg,lon_deg,vel_mps,course_deg,"
                       "heading_deg,dist_m,bearing_deg,motor_l,motor_r\n");
}

int formatCsvRecord(const LogRecord& rec, char* buf, size_t bufSize) {
  // 位置由来フィールドは hasFix=false なら（値の内容に関わらず）空にする。
  const bool pos = rec.hasFix;

  char lat[24];
  char lon[24];
  char vel[16];
  char course[12];
  char heading[12];
  char dist[16];
  char bearing[12];

  writeNumOrEmpty(lat, sizeof(lat), rec.latitudeDeg, pos, "%.7f");
  writeNumOrEmpty(lon, sizeof(lon), rec.longitudeDeg, pos, "%.7f");
  writeNumOrEmpty(vel, sizeof(vel), rec.velocityMps, pos, "%.2f");
  writeNumOrEmpty(course, sizeof(course), rec.courseDeg, pos, "%.1f");
  // heading は 9軸姿勢由来。GNSS FIX に依存せず、値が有限なら出す。
  writeNumOrEmpty(heading, sizeof(heading), rec.headingDeg, true, "%.1f");
  writeNumOrEmpty(dist, sizeof(dist), rec.distanceM, pos, "%.2f");
  writeNumOrEmpty(bearing, sizeof(bearing), rec.bearingDeg, pos, "%.1f");

  return std::snprintf(buf, bufSize, "%lu,%u,%d,%s,%s,%s,%s,%s,%s,%s,%d,%d\n",
                       static_cast<unsigned long>(rec.timestampMs),
                       static_cast<unsigned>(rec.state), rec.hasFix ? 1 : 0, lat, lon, vel, course,
                       heading, dist, bearing, static_cast<int>(rec.motorLeft),
                       static_cast<int>(rec.motorRight));
}

int formatLogFilePath(char* buf, size_t bufSize, uint16_t index) {
  return std::snprintf(buf, bufSize, "%sflight%03u.csv", kLogDir, static_cast<unsigned>(index));
}

FlightLogger::FlightLogger(LogSink& sink, uint16_t flushEveryN)
    : sink_(sink),
      flushEveryN_(flushEveryN == 0 ? 1 : flushEveryN),
      recordsWritten_(0),
      sinceFlush_(0),
      begun_(false),
      buf_{} {}

bool FlightLogger::begin() {
  if (begun_) {
    return false;  // 二重 begin 拒否（ヘッダ二重書き込みを防ぐ。#17 の区間再開は別経路で）
  }
  if (!sink_.begin()) {
    return false;
  }
  const int n = formatCsvHeader(buf_, sizeof(buf_));
  // n<0（整形失敗）/ n>=bufSize（切り詰め）は書かない。切り詰め行は末尾 '\n' が落ちて
  // 次行と結合し CSV を壊すため、壊れた行を出力しない（append 長に strlen を使う都合）。
  if (n < 0 || n >= static_cast<int>(sizeof(buf_))) {
    return false;
  }
  if (!sink_.append(buf_, std::strlen(buf_))) {
    return false;
  }
  begun_ = true;
  return true;
}

bool FlightLogger::log(const LogRecord& rec) {
  if (!begun_) {
    return false;
  }
  const int n = formatCsvRecord(rec, buf_, sizeof(buf_));
  // 切り詰め（n>=bufSize）は末尾 '\n' 落ち→行結合になるため書かない（begin と同じ理由）。
  if (n < 0 || n >= static_cast<int>(sizeof(buf_))) {
    return false;
  }
  if (!sink_.append(buf_, std::strlen(buf_))) {
    return false;
  }
  ++recordsWritten_;
  if (++sinceFlush_ >= flushEveryN_) {
    sink_.flush();
    sinceFlush_ = 0;
  }
  return true;
}

void FlightLogger::flush() {
  sink_.flush();
  sinceFlush_ = 0;
}

}  // namespace datalog
