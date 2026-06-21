# arduino-cli によるコマンドラインビルド環境

Arduino IDE を使わずにコマンドラインで Spresense 向けスケッチをビルドするための手順。
CI（Issue #7）やエディタ補完（clangd）の土台になる。

- 前提: Spresense コア／ライブラリの仕様は [spresense_setup.md](./spresense_setup.md) を参照
- 確認環境: **arduino-cli 1.5.1（Homebrew）/ SPRESENSE:spresense 3.4.7**
  - 仕様書(software.md)記載は 3.0.0 だが、それより新しい 3.4.7 でビルド確認済み（後方互換）。

> 既に Arduino IDE を導入済みの場合、Spresense コアは `~/Library/Arduino15` に入っており、Homebrew 版 arduino-cli も同じデータディレクトリを共有する。その場合は手順2のコア導入は不要（`arduino-cli core list` で確認できる）。

---

## 1. arduino-cli の導入（macOS / Homebrew）

```bash
brew install arduino-cli
arduino-cli version
```

> Linux / CI では公式インストールスクリプトでも可:
> `curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh`

---

## 2. Spresense コアの導入（初回のみ）

```bash
# ボードマネージャに Spresense のパッケージ index URL を追加
arduino-cli config init   # 既に設定済みならスキップ可
arduino-cli config add board_manager.additional_urls \
  https://github.com/sonydevworld/spresense-arduino-compatible/releases/download/generic/package_spresense_index.json

# index 更新 → コア導入
arduino-cli core update-index
arduino-cli core install SPRESENSE:spresense

# 導入確認
arduino-cli core list
```

- FQBN（Fully Qualified Board Name）: **`SPRESENSE:spresense:spresense`**

> 注意: 書き込み（upload）には別途 Spresense ブートローダの導入が必要（[spresense_setup.md §4](./spresense_setup.md)）。本書で扱うのは**ビルド（compile）まで**。

---

## 3. ビルド

リポジトリ同梱のスクリプトを使う:

```bash
tools/build.sh src/blink_led    # 動作確認スケッチ
tools/build.sh src/flight       # 本番ソフトの雛形
```

直接叩く場合:

```bash
arduino-cli compile --fqbn SPRESENSE:spresense:spresense --warnings all src/blink_led
```

> 将来 `src/lib/` のモジュールをスケッチから使う場合は、`--libraries src/lib` を付けてライブラリ探索パスを追加する（各モジュールを Arduino ライブラリ形式にしてから）。

---

## 4. 書き込み(upload)とシリアルモニタ

ブートローダ導入後（初回のみ IDE で実施。[spresense_setup.md §4](./spresense_setup.md)）は、**書き込みもシリアルモニタも CLI で完結**する。Arduino IDE は不要。

### 書き込み

```bash
tools/upload.sh src/blink_led                          # ポート自動検出
tools/upload.sh src/blink_led /dev/cu.SLAB_USBtoUART   # ポート明示
```

- ポート省略時は Spresense らしきポート(CP210x: `cu.SLAB_USBtoUART` / `cu.usbserial*`)を自動検出する。1つに定まらない場合は一覧を表示して終了するので、第2引数で明示する。
- 直接叩く場合（コンパイル＋書き込みを一発）:
  ```bash
  arduino-cli compile --fqbn SPRESENSE:spresense:spresense -u -p /dev/cu.SLAB_USBtoUART src/blink_led
  ```

### シリアルモニタ（115200bps）

```bash
tools/monitor.sh                        # ポート自動検出
tools/monitor.sh /dev/cu.SLAB_USBtoUART # ポート明示
```

- 直接叩く場合: `arduino-cli monitor -p /dev/cu.SLAB_USBtoUART -c baudrate=115200`
- 起動バナーを先頭から見たい場合は、モニタを開いてからボードを**リセット**する（Serial は CP210x UART でモニタ接続を検知できないため）。終了は Ctrl-C。

> 補足: ブートローダの初回書き込み（Burn Bootloader / Spresense Firmware Updater）は EULA 同意つきファームの取得が絡むため IDE で行うのが簡単。それさえ済めば以降は IDE 不要。

---

## 5. clangd 用の補完設定（Arduino.h 等の誤検知解消）

エディタの C/C++ 言語サーバ（clangd）は、何もしないと Spresense コアのインクルードパスを知らないため
`Arduino.h が開けません` 等の**誤検知**を出す。`compile_commands.json` を生成して clangd に渡すことで解消する。

リポジトリ同梱のスクリプトで生成する:

```bash
tools/gen_compile_commands.sh            # 既定 src/blink_led から生成
tools/gen_compile_commands.sh src/flight # 別スケッチを基に生成する場合
```

これでリポジトリ直下に `compile_commands.json` が作られ、clangd 拡張（VS Code 等）が自動検出する。
生成物には Spresense コアのインクルードパス（例 `.../3.4.7/cores/spresense`）が含まれ、`Arduino.h` 等が解決される。

- `compile_commands.json` は**マシン依存の絶対パス**を含むため Git 管理しない（`.gitignore` 済み）。各自ローカルで生成する。
- コアを更新（バージョンが変わる）したら再生成する。

> スクリプトは `arduino-cli compile --build-path .arduino-build/<sketch>` を実行し、生成された `compile_commands.json` をリポジトリ直下へコピーする。
> DB の各エントリはこのビルドパス内のファイル（`sketch/*.ino.cpp` 等）を参照するため、`.arduino-build/` は**削除せず永続化**する（`.gitignore` 済み）。消すと DB が存在しないファイルを指し、補完が機能しなくなる。

---

## 6. トラブルシュート

| 症状 | 対処 |
|---|---|
| `platform not installed` | 手順2のコア導入を実施 |
| `board not found (SPRESENSE:spresense:spresense)` | `arduino-cli core list` でコア導入を確認、index URL 追加を確認 |
| `ポートを自動検出できません` | `tools/upload.sh`/`tools/monitor.sh` の第2引数/引数でポートを明示。`arduino-cli board list` で確認 |
| 書き込みが失敗する | ブートローダ未導入の可能性（[spresense_setup.md §4](./spresense_setup.md)）。Upload speed を下げる（`--board-options UploadSpeed=115200`） |
| clangd が `Arduino.h` を見つけられない | 手順5で `compile_commands.json` を生成・配置 |
