cmake_minimum_required(VERSION 3.25)

project(Sakura
    VERSION 0.5.0
    DESCRIPTION "Sakura-樱 混合模式音乐节奏游戏"
    LANGUAGES CXX
)

# 预发布标签（留空则为正式版）
set(SAKURA_VERSION_PRERELEASE "alpha")
if(SAKURA_VERSION_PRERELEASE STREQUAL "")
    set(SAKURA_VERSION_FULL "${PROJECT_VERSION}")
else()
    set(SAKURA_VERSION_FULL "${PROJECT_VERSION}-${SAKURA_VERSION_PRERELEASE}")
endif()

# ============================================================================
# C++ 标准
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# 编译选项
# ============================================================================
if(MSVC)
    add_compile_options(
        /W4             # 高警告级别
        /utf-8          # 源文件 UTF-8 编码
        /permissive-    # 严格标准一致性
        /Zc:__cplusplus # 正确报告 __cplusplus 值
    )
    # Debug 模式
    add_compile_options($<$<CONFIG:Debug>:/Zi>)
    add_compile_options($<$<CONFIG:Debug>:/Od>)
    add_compile_options($<$<CONFIG:Debug>:/RTC1>)
    # Release 模式
    add_compile_options($<$<CONFIG:Release>:/O2>)
    add_compile_options($<$<CONFIG:Release>:/DNDEBUG>)
endif()

# ============================================================================
# 测试选项
# ============================================================================
if(WIN32)
    set(SAKURA_BUILD_APP_DEFAULT ON)
else()
    set(SAKURA_BUILD_APP_DEFAULT OFF)
endif()

option(SAKURA_BUILD_APP "构建 Sakura 主程序" ${SAKURA_BUILD_APP_DEFAULT})
option(SAKURA_BUILD_TESTS "构建单元测试" OFF)

# enable_testing() 必须在根 CMakeLists 中调用，才能让 ctest 发现子目录测试
enable_testing()

# ============================================================================
# vcpkg 依赖查找
# ============================================================================
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(SAKURA_BUILD_APP)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(SDL3_image CONFIG REQUIRED)
    find_package(SDL3_ttf CONFIG REQUIRED)
    # miniaudio 是 header-only, 通过 vcpkg 安装后直接 include
endif()

# ============================================================================
# 源文件收集
# ============================================================================
if(SAKURA_BUILD_APP)
    file(GLOB_RECURSE SAKURA_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    )

    file(GLOB_RECURSE SAKURA_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    )

    file(GLOB_RECURSE SAKURA_RESOURCE_DIRECTORIES
        LIST_DIRECTORIES true
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/*"
    )

    file(GLOB_RECURSE SAKURA_RESOURCE_FILES
        LIST_DIRECTORIES false
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/*"
    )

    # ========================================================================
    # 可执行文件
    # ========================================================================
    add_executable(${PROJECT_NAME} WIN32
        ${SAKURA_SOURCES}
        ${SAKURA_HEADERS}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )

    # ========================================================================
    # 链接库
    # ========================================================================
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3
        spdlog::spdlog
    )

    # ========================================================================
    # 资源文件复制
    # ========================================================================
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
        COMMENT "Syncing resources to output directory"
    )

    foreach(resourceDir IN LISTS SAKURA_RESOURCE_DIRECTORIES)
        if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources/${resourceDir}")
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/${resourceDir}"
            )
        endif()
    endforeach()

    foreach(resourceFile IN LISTS SAKURA_RESOURCE_FILES)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/${resourceFile}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources/${resourceFile}"
        )
    endforeach()

    # ========================================================================
    # 安装规则
    # ========================================================================
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY resources/
        DESTINATION bin/resources
    )
endif()

# ============================================================================
# 版本信息
# ============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
)
if(SAKURA_BUILD_APP)
    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
endif()

# ============================================================================
# 单元测试（仅 SAKURA_BUILD_TESTS=ON 时启用）
# ============================================================================
if(SAKURA_BUILD_TESTS)
    # 提取可独立测试的游戏逻辑（无 SDL3 运行时依赖）
    add_library(sakura-game-logic STATIC
        src/core/config.cpp
        src/data/database.cpp
        src/game/achievement_manager.cpp
        src/game/chart_loader.cpp
        src/game/pp_calculator.cpp
        src/game/score.cpp
        src/game/judge.cpp
        src/game/tutorial_data.cpp
        src/utils/logger.cpp
    )
    target_include_directories(sakura-game-logic PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(sakura-game-logic PUBLIC
        nlohmann_json::nlohmann_json
        unofficial::sqlite3::sqlite3
        spdlog::spdlog
    )
    target_compile_features(sakura-game-logic PUBLIC cxx_std_20)

    add_subdirectory(tests)
endif()
