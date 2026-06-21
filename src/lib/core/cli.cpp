#include "cli.h"

#include <climits>
#include <cstring>

namespace {
inline bool isSpace(char c) { return c == ' ' || c == '\t'; }
}  // namespace

namespace cli {

int tokenize(char* line, char** argv, int maxArgs) {
  int argc = 0;
  char* p = line;
  while (*p != '\0' && argc < maxArgs) {
    // 先頭の空白を読み飛ばす
    while (isSpace(*p)) {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    argv[argc++] = p;  // トークン先頭
    // 次の空白まで進める
    while (*p != '\0' && !isSpace(*p)) {
      p++;
    }
    if (*p != '\0') {
      *p = '\0';  // トークンを終端
      p++;
    }
  }
  return argc;
}

int findCommand(const Command* table, int count, const char* name) {
  for (int i = 0; i < count; i++) {
    if (std::strcmp(table[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

bool parseInt(const char* s, int& out) {
  if (s == nullptr || *s == '\0') {
    return false;
  }
  const char* p = s;
  bool neg = false;
  if (*p == '+' || *p == '-') {
    neg = (*p == '-');
    p++;
  }
  if (*p == '\0') {
    return false;  // 符号のみは不可
  }
  // int の範囲で直接累積し、各桁でオーバーフローを検査する。
  // （Spresense では long も 32bit のため long 経由では防げない。負値は INT_MIN を
  //   正しく扱うため下向きに累積する。）
  int result = 0;
  while (*p != '\0') {
    if (*p < '0' || *p > '9') {
      return false;  // 数字以外を含む
    }
    int digit = *p - '0';
    if (neg) {
      if (result < (INT_MIN + digit) / 10) {
        return false;  // 下限を下回る
      }
      result = result * 10 - digit;
    } else {
      if (result > (INT_MAX - digit) / 10) {
        return false;  // 上限を超える
      }
      result = result * 10 + digit;
    }
    p++;
  }
  out = result;
  return true;
}

}  // namespace cli
