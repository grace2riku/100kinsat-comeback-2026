# オンターゲット試験シェル（NT-Shell）

HW依存機能を、Spresense 実機にシリアル接続して**対話的なコマンドで確認する**ための試験環境（Issue #6）。

- スケッチ: [`src/shell/shell.ino`](../../src/shell/shell.ino)
- 行編集・履歴のフロントエンド: **NT-Shell**（[`libraries/ntshell`](../../libraries/ntshell)、MIT License / Shinichiro Nakamura。Spresense 移植は grace2riku/spresense_arduino_ntshell より同梱）
- コマンドのトークン化・探索: [`src/lib/core/cli`](../../src/lib/core/cli.h)（HW非依存・ホストテスト済 → [test/core/test_cli.cpp](../../test/core/test_cli.cpp)）

## 仕組み（層分け）

```
Serial(115200) ──▶ NT-Shell（行編集・履歴）
                      │ 確定した1行
                      ▼
              core/cli: tokenize → findCommand   （純粋・ホストテスト対象）
                      │ argc/argv
                      ▼
              shell.ino のコマンドハンドラ（HW依存: LED/センサ/…）
```

## ビルドと書き込み

```bash
tools/build.sh  src/shell          # ビルド（compile）
tools/upload.sh src/shell          # ビルド＋書き込み（要ブートローダ導入）
tools/monitor.sh                   # シリアルモニタ 115200bps
```

> `tools/build.sh` は `--libraries libraries`（NT-Shell）と `--libraries src/lib`（core）を探索パスに追加してビルドする。

## 使い方

1. `src/shell` を書き込む
2. シリアルモニタ（115200bps）を開く。`改行(LF/CR)` を送れる設定にする
3. プロンプト `100kinsat> ` に対しコマンドを入力。`help` で一覧表示

## コマンド一覧

| コマンド | 書式 | 説明 | 状態 |
|---|---|---|---|
| `help` | `help` | コマンド一覧を表示 | 実動作 |
| `led` | `led <0-3> <on\|off>` | 内蔵LED(LED0-3)を点灯/消灯 | 実動作 |
| `cds` | `cds` | 照度センサ(A0)の生値(0-1023)を表示 | 実動作 |
| `beep` | `beep <freq_hz> <ms>` | スピーカ(D09)を指定周波数・時間で鳴らす | 実動作 |
| `motor` | `motor …` | モータ駆動テスト | スタブ（Issue #8 で実装） |
| `imu` | `imu` | 9軸センサ読み取り | スタブ（Issue #9 で実装） |
| `gnss` | `gnss` | GNSS測位 | スタブ（Issue #10 で実装） |
| `log` | `log` | 制御履歴ログ | スタブ（Issue #14 で実装） |

### 実行例

```
100kinsat> help
  help    コマンド一覧を表示
  led     led <0-3> <on|off> : 内蔵LEDを点灯/消灯
  ...
100kinsat> led 0 on
LED0 = ON
100kinsat> cds
cds(A0) = 512
100kinsat> beep 440 200
beep done
```

## コマンドを追加する

1. ハンドラ `static int cmd_xxx(int argc, char** argv)` を `src/shell/shell.ino` に実装（HW依存処理はここ）。
2. `kCommands[]` に `{"xxx", "説明", cmd_xxx}` を追加。
3. 引数のトークン化・コマンド探索は `core/cli`（`cli::tokenize` / `cli::findCommand`）が担う。純粋ロジックを足す場合は `src/lib/core` 側に置き、`test/core/` でテストする。

> Phase2 では `motor`/`imu`/`gnss`/`log` のスタブを、各 hal モジュール（Issue #8/#9/#10/#14）の呼び出しに置き換える。
