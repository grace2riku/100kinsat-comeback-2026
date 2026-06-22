# =============================================================================
# ホストPC ユニットテスト専用ビルド
#
# これは「HW非依存ロジック（src/lib/core）をホストPCで検証する」ための CMake。
# 実機ファームウェアのビルドではない（ファームは arduino-cli / tools/build.sh）。
#
# 使い方:
#   tools/test.sh            # 設定→ビルド→ctest を一括
#   もしくは:
#   cmake -S . -B build/host-test && cmake --build build/host-test \
#     && ctest --test-dir build/host-test --output-on-failure
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(kinsat_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# clangd 用に compile_commands.json を build ディレクトリへ出力する
# （テストコード編集時の補完用。build/ は .gitignore 済み）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# 警告を厳しめに（テスト対象は純粋ロジックなので警告ゼロを保つ）
add_compile_options(-Wall -Wextra)

# HW非依存ロジック層をライブラリ化（Arduino 非依存のもののみ）
add_library(core
  src/lib/core/geo.cpp
  src/lib/core/cli.cpp
  src/lib/core/motor.cpp
)
target_include_directories(core PUBLIC src/lib/core)

enable_testing()
add_subdirectory(test)
