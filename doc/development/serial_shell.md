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
2. シリアルモニタ（115200bps）を開く。改行コードは **CR / LF / CRLF いずれでも可**（`func_read` で正規化している）
3. プロンプト `100kinsat> ` に対しコマンドを入力。`help` で一覧表示

## 端末について（行編集・履歴・カーソル移動）

NT-Shell の **カーソル移動（←→）・コマンド履歴（↑↓）は、矢印キーの VT100 エスケープ列
（`ESC [ A` 等）を送出する端末**でのみ動作する。BackSpace（単一バイト）はどの端末でも動く。

| 端末 | 矢印キーによる編集/履歴 |
|---|---|
| **minicom** / **screen**（macOS/Linux） | ✅ 動作（確認済み: minicom） |
| **TeraTerm** / **PuTTY**（Windows） | ✅ 動作 |
| VSCode 統合ターミナル | ⚠️ 矢印キーが転送されず動かないことがある |
| Arduino IDE シリアルモニタ | ⚠️ VT100 端末ではないため行編集系は不可 |

> 例: macOS で minicom を使う場合
> ```bash
> minicom -D /dev/cu.SLAB_USBtoUART -b 115200
> ```

### 矢印キーが効かない端末での代替（Ctrl キー）

矢印が送られない端末でも、以下の **Ctrl キー（単一バイト）** は常に動作する。

| 操作 | キー |
|---|---|
| 履歴 前 / 次 | `Ctrl-P` / `Ctrl-N` |
| カーソル 左 / 右 | `Ctrl-B` / `Ctrl-F` |
| 行頭 / 行末 | `Ctrl-A` / `Ctrl-E` |
| 1文字削除 | `BackSpace` / `Ctrl-D` |
| 入力取り消し | `Ctrl-C` |

## コマンド一覧

| コマンド | 書式 | 説明 | 状態 |
|---|---|---|---|
| `help` | `help` | コマンド一覧を表示 | 実動作 |
| `led` | `led <0-3> <on\|off>` | 内蔵LED(LED0-3)を点灯/消灯 | 実動作 |
| `cds` | `cds` | 照度センサ(A0)の生値(0-1023)を表示 | 実動作 |
| `beep` | `beep <freq_hz> <ms>` | スピーカ(D09)を指定周波数・時間で鳴らす | 実動作 |
| `motor` | `motor <forward\|back\|left\|right\|stop> [duty 0-255] [ms]` | モータ駆動(TB6612FNG)。指定方向へ駆動し ms 後に自動停止 | 実動作（Issue #8） |
| `imu` | `imu [init\|stat\|cal\|mon [n]]` | 9軸センサ(BNO055)の方位・校正・状態を読む（init=再初期化 / stat=健全性 / cal=校正のみ / mon=連続表示） | 実動作（Issue #9） |
| `gnss` | `gnss [init\|mon [n]]` | GNSS測位の状態/位置/品質を読む（init=初期化 / mon=連続表示） | 実動作（Issue #10） |
| `land` | `land [mon [n]]` | 加速度から着地(静止)を検知（要 `imu init`。無印=\|a\|確認の単発 / mon=連続観測で静止→着地確定） | 実動作（Issue #12） |
| `separate` | `separate [ms\|stop]` | パラシュート切り離し電熱線(D06)を加熱（安全上限あり・⚠高温注意） | 実動作（Issue #13） |
| `log` | `log [n]` | 制御履歴(制御量+操作量)をSDへCSV記録(既定5件・ダミー) | 実動作（Issue #14） |

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

> `motor`（#8）・`imu`（#9）・`gnss`（#10）・`land`（#12）・`separate`（#13）・`log`（#14）は実装済み。Phase2 の単体機能コマンドは出揃った。
