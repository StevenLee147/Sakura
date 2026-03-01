cmake_minimum_required(VERSION 3.25)

project(Sakura
    VERSION 0.3.1
    DESCRIPTION "Sakura-樱 混合模式音乐节奏游戏"
    LANGUAGES CXX
)

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
# vcpkg 依赖查找
# ============================================================================
find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
# miniaudio 是 header-only, 通过 vcpkg 安装后直接 include

# ============================================================================
# 源文件收集
# ============================================================================
file(GLOB_RECURSE SAKURA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SAKURA_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# ============================================================================
# 可执行文件
# ============================================================================
add_executable(${PROJECT_NAME} WIN32
    ${SAKURA_SOURCES}
    ${SAKURA_HEADERS}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ============================================================================
# 链接库
# ============================================================================
target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    nlohmann_json::nlohmann_json
    unofficial::sqlite3::sqlite3
    spdlog::spdlog
)

# ============================================================================
# 资源文件复制
# ============================================================================
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/resources"
    COMMENT "Copying resources to output directory"
)

# ============================================================================
# 安装规则
# ============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)
install(DIRECTORY resources/
    DESTINATION bin/resources
)

# ============================================================================
# 版本信息
# ============================================================================
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
)
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
