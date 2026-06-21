#ifndef CORE_CLI_H
#define CORE_CLI_H

// cli - コマンドライン処理（ハードウェア非依存）
//
// オンターゲット試験シェル（NT-Shell）から渡された「確定した1行」を、
// トークン分割してコマンド表から探す純粋ロジック。Serial 等の HW には触れないため
// ホストPCでユニットテストできる（test/core/test_cli.cpp）。
//
// シェルのフロントエンド（行編集・履歴）は NT-Shell が担当し、
// 振り分け対象の HW コマンド実体はスケッチ側（src/shell）に置く。

namespace cli {

// コマンドハンドラ。argc/argv を受け取り、0 を成功とする。
using Handler = int (*)(int argc, char** argv);

// コマンド表の1要素。
struct Command {
  const char* name;  // コマンド名（例 "motor"）
  const char* desc;  // help 表示用の説明
  Handler handler;   // 実行関数
};

// 空白（スペース/タブ）区切りで line をトークンへ分割する。
// argv に最大 maxArgs 個のトークン先頭ポインタを格納し、トークン数(argc)を返す。
// 注意: line は破壊的に書き換える（トークン末尾に '\0' を差し込む）。
// 連続空白・先頭/末尾空白は無視する。maxArgs を超える分は切り捨てる。
int tokenize(char* line, char** argv, int maxArgs);

// table（count 件）から name に一致するコマンドを線形探索する。
// 見つかればその添字、見つからなければ -1 を返す。
int findCommand(const Command* table, int count, const char* name);

}  // namespace cli

#endif  // CORE_CLI_H
