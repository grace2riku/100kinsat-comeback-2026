// cli（コマンドライン処理）の単体テスト
//
// トークン分割（連続空白・先頭/末尾空白・最大数の切り捨て・破壊的書き換え）と、
// コマンド表の線形探索（先頭/末尾/不一致/空表）を検証する。

#include <cstring>

#include "cli.h"
#include "doctest.h"

namespace {
// findCommand 用のダミーハンドラ（呼ばれないが表に必要）
int noop(int, char**) { return 0; }
}  // namespace

TEST_CASE("tokenize: 通常のコマンドと引数を分割する") {
  char line[] = "motor 100 200";
  char* argv[8];
  int argc = cli::tokenize(line, argv, 8);
  CHECK(argc == 3);
  CHECK(std::strcmp(argv[0], "motor") == 0);
  CHECK(std::strcmp(argv[1], "100") == 0);
  CHECK(std::strcmp(argv[2], "200") == 0);
}

TEST_CASE("tokenize: 空文字列は argc=0") {
  char line[] = "";
  char* argv[4];
  CHECK(cli::tokenize(line, argv, 4) == 0);
}

TEST_CASE("tokenize: 空白のみは argc=0") {
  char line[] = "    \t  ";
  char* argv[4];
  CHECK(cli::tokenize(line, argv, 4) == 0);
}

TEST_CASE("tokenize: 連続空白・先頭/末尾空白を無視する") {
  char line[] = "   led   on   ";
  char* argv[4];
  int argc = cli::tokenize(line, argv, 4);
  CHECK(argc == 2);
  CHECK(std::strcmp(argv[0], "led") == 0);
  CHECK(std::strcmp(argv[1], "on") == 0);
}

TEST_CASE("tokenize: タブ区切りも分割できる") {
  char line[] = "a\tb\tc";
  char* argv[4];
  int argc = cli::tokenize(line, argv, 4);
  CHECK(argc == 3);
  CHECK(std::strcmp(argv[2], "c") == 0);
}

TEST_CASE("tokenize: 単一トークン") {
  char line[] = "help";
  char* argv[2];
  int argc = cli::tokenize(line, argv, 2);
  CHECK(argc == 1);
  CHECK(std::strcmp(argv[0], "help") == 0);
}

TEST_CASE("tokenize: maxArgs を超える分は切り捨てる（オーバーフローしない）") {
  char line[] = "a b c d e";
  char* argv[3];
  int argc = cli::tokenize(line, argv, 3);
  CHECK(argc == 3);  // 最大3個まで
  CHECK(std::strcmp(argv[0], "a") == 0);
  CHECK(std::strcmp(argv[2], "c") == 0);
}

TEST_CASE("findCommand: 先頭・末尾・不一致を正しく扱う") {
  const cli::Command table[] = {
      {"help", "ヘルプ", noop},
      {"led", "LED", noop},
      {"cds", "照度", noop},
  };
  const int n = 3;
  CHECK(cli::findCommand(table, n, "help") == 0);    // 先頭
  CHECK(cli::findCommand(table, n, "cds") == 2);     // 末尾
  CHECK(cli::findCommand(table, n, "led") == 1);     // 中間
  CHECK(cli::findCommand(table, n, "motor") == -1);  // 不一致
}

TEST_CASE("findCommand: 空の表では常に -1") {
  const cli::Command* table = nullptr;
  CHECK(cli::findCommand(table, 0, "help") == -1);
}

TEST_CASE("findCommand: 大文字小文字は区別する") {
  const cli::Command table[] = {{"help", "ヘルプ", noop}};
  CHECK(cli::findCommand(table, 1, "HELP") == -1);
  CHECK(cli::findCommand(table, 1, "help") == 0);
}
