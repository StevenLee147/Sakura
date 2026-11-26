cmake_minimum_required(VERSION 3.25)

# ─── SakuraTests ─────────────────────────────────────────────────────────────
add_executable(SakuraTests
    test_main.cpp
    test_play_session.cpp
    test_persistence.cpp
    test_achievement_manager.cpp
    test_approach_visuals.cpp
    test_chart_loader_builtin.cpp
    test_frame_input_buffer.cpp
    test_pp_calculator.cpp
    test_score.cpp
    test_judge.cpp
    test_tutorial_data.cpp
    test_chart_loader_legacy.cpp
    test_record_script.cpp
    test_synergy_v3.cpp
    test_time_stretch.cpp
    test_sakura_archive.cpp
    test_video_and_clock.cpp
    test_v1_stress.cpp
    test_online_v110.cpp
    test_market_sync_v120.cpp
    test_v130_feel_and_charts.cpp
    test_v140_course_daily_collections.cpp
    test_v150_skins_and_stage_v4.cpp
    test_v160_ghost_and_arena.cpp
    test_v170_converter_and_vol4.cpp
    test_v200_updater_and_career.cpp
)

target_compile_definitions(SakuraTests PRIVATE
    SAKURA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(SakuraTests PRIVATE
    sakura-game-logic
)

target_compile_features(SakuraTests PRIVATE cxx_std_20)

add_test(NAME SakuraTests COMMAND SakuraTests)
