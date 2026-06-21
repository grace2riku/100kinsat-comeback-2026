# test/ — ホストPC ユニットテスト

`src/lib/core`（**ハードウェア非依存ロジック**）を、Spresense 実機なしで **ホストPC** で検証するためのテスト。
CI（Issue #7）でも同じ仕組みで自動実行する。

- フレームワーク: **doctest**（単一ヘッダ・同梱）— `test/vendor/doctest.h`（v2.4.11）
- ビルド: CMake + ctest（外部ネット接続不要）

## 実行

```bash
tools/test.sh         # 設定→ビルド→ctest を一括
tools/test.sh -V      # 詳細表示（追加引数は ctest に渡る）
```

直接叩く場合:

```bash
cmake -S . -B build/host-test
cmake --build build/host-test
ctest --test-dir build/host-test --output-on-failure
```

## ディレクトリ

```
test/
  vendor/doctest.h     # doctest 単一ヘッダ（同梱・改変しない）
  doctest_main.cpp     # doctest 本体+main を1か所でコンパイル
  CMakeLists.txt       # add_core_test(<name> <src...>) でテストを追加
  core/
    test_geo.cpp       # geo（距離・方位計算）のテスト
```

## テストを追加する手順

1. テスト対象の純粋ロジックを `src/lib/core/` に置く（Arduino 非依存）。
2. `CMakeLists.txt`（リポジトリ直下）の `add_library(core ...)` に `.cpp` を追加。
3. `test/core/test_xxx.cpp` を作成（`#include "doctest.h"` と対象ヘッダを include）。
4. `test/CMakeLists.txt` に `add_core_test(test_xxx core/test_xxx.cpp)` を追加。
5. `tools/test.sh` で実行。

## テストの品質方針（厳守）

本リポジトリの [CLAUDE.md](../CLAUDE.md)「テスト戦略」および、プロジェクト共通のテスト規約に従う。

- **無意味なテストを書かない**。`CHECK(true)` のような常に真のアサーションは禁止。
  各テストは具体的な入力と期待出力を検証すること。
- **境界値・異常系・退化ケースを必ず含める**。正常系だけにしない
  （例: 同一点の距離=0、方位の 0–360 正規化、往復の対称性 など）。
- **期待値は根拠を持って決める**。解析解（子午線/赤道の1度＝約111.2km）や、
  独立に計算した参照値を使い、マジックナンバーを避ける。
- **テスト名は何を検証するか明確に**（例:「真西は270度（負値ではなく0-360に正規化）」）。
- **TDD（Red→Green→Refactor）** で進める。先に失敗するテストを書き、最小実装で通し、
  テストを保ったままリファクタする。

## TDD の実例（geo モジュール）

`geo`（距離・方位）は次の順で実装した。

1. **Red**: `test/core/test_geo.cpp` を先に書き、`geo.cpp` をスタブ（常に0）にして失敗を確認
   （12ケース中、0を期待するもの以外が失敗）。
2. **Green**: haversine 距離・初期方位を実装し、全ケース通過。
3. **Refactor**: `toRad/toDeg` ヘルパー抽出・const 整理を行い、テストが緑のままを確認。
