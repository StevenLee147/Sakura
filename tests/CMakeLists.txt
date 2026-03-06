cmake_minimum_required(VERSION 3.25)

# ─── Catch2 ──────────────────────────────────────────────────────────────────
find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)   # 提供 catch_discover_tests()

# ─── 测试可执行文件 ────────────────────────────────────────────────────────────
add_executable(SakuraTests
    test_score.cpp
    test_judge.cpp
)

target_link_libraries(SakuraTests PRIVATE
    sakura-game-logic
    Catch2::Catch2WithMain
)

target_compile_features(SakuraTests PRIVATE cxx_std_20)

# ─── 注册到 CTest ─────────────────────────────────────────────────────────────
catch_discover_tests(SakuraTests
    REPORTER console
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}"
)
