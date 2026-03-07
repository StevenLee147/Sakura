cmake_minimum_required(VERSION 3.25)

# ─── Catch2 ──────────────────────────────────────────────────────────────────
add_executable(SakuraTests
    test_main.cpp
    test_chart_loader_builtin.cpp
    test_score.cpp
    test_judge.cpp
    test_tutorial_data.cpp
)

target_compile_definitions(SakuraTests PRIVATE
    SAKURA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(SakuraTests PRIVATE
    sakura-game-logic
)

target_compile_features(SakuraTests PRIVATE cxx_std_20)

add_test(NAME SakuraTests COMMAND SakuraTests)
