# ホストユニットテスト（doctest 同梱・単一ヘッダ）

# doctest 本体（main 付き）を一度だけコンパイルする TU。
# 各テストファイルは doctest.h を include するだけでよい（コンパイル高速化）。
add_library(doctest_main STATIC doctest_main.cpp)
target_include_directories(doctest_main PUBLIC vendor)

# テスト実行ファイルを定義するヘルパー。
#   add_core_test(<name> <src...>)
function(add_core_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE core doctest_main)
  target_include_directories(${name} PRIVATE vendor)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_geo core/test_geo.cpp)
add_core_test(test_cli core/test_cli.cpp)
