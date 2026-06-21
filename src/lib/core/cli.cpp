#include "cli.h"

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
  long val = 0;
  while (*p != '\0') {
    if (*p < '0' || *p > '9') {
      return false;  // 数字以外を含む
    }
    val = val * 10 + (*p - '0');
    p++;
  }
  out = static_cast<int>(neg ? -val : val);
  return true;
}

}  // namespace cli
