# =============================================================================
# foobar2000 AI Metadata Plugin - CMake Build Configuration
# =============================================================================
# 插件名称: foo_ai_metadata
# 功能: AI驱动的音乐元数据分析插件
#   - 流派分类 (Genre Classification)
#   - 版本识别 (Edition Identification)
#   - 元数据翻译 (Metadata Translation)
# =============================================================================

cmake_minimum_required(VERSION 3.25)

# =============================================================================
# 项目配置
# =============================================================================
# 可配置路径 (通过 cmake -D 覆盖)
#   VCPKG_PATH         : vcpkg 安装路径
#   FOOBAR2000_SDK_PATH: foobar2000 SDK 路径
#   FOOBAR_DEV_DIR     : 本地 foobar2000 开发目录 (可选, 留空则不自动部署)
set(VCPKG_PATH "D:/Programs/vcpkg" CACHE PATH "vcpkg installation path")
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_PATH}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project(foo_ai_metadata VERSION 1.0.0 LANGUAGES C CXX)

# C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /std:c++20)
endif()

# =============================================================================
# 依赖配置
# =============================================================================
set(FOOBAR2000_SDK_PATH "D:/Programs/foobar2000_sdk" CACHE PATH "foobar2000 SDK path")

# nlohmann_json - JSON解析库
find_package(nlohmann_json CONFIG REQUIRED)

# =============================================================================
# 核心库 (ai_core) - 静态库
# =============================================================================
# 包含:
#   - ai_core.cpp      : AI核心处理逻辑
#   - cache_layer.cpp  : SQLite缓存层
#   - worker_manager.cpp: IPC Worker管理
#   - task_queue.cpp   : 任务队列
#   - sqlite3.c        : SQLite数据库
# =============================================================================

set(SQLITE_SOURCES
    ${PROJECT_SOURCE_DIR}/include/third_party/sqlite/sqlite3.c
)

set(CORE_SOURCES
    core/ai_core.cpp
    core/cache_layer.cpp
    core/worker_manager.cpp
    core/task_queue.cpp
    core/logger.cpp
    ${SQLITE_SOURCES}
)

add_library(ai_core STATIC ${CORE_SOURCES})

target_include_directories(ai_core PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/third_party
)

target_compile_definitions(ai_core PRIVATE
    _UNICODE
    UNICODE
    WIN32
    _WINDOWS
    STRICT
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
)

target_link_libraries(ai_core
    PRIVATE
    nlohmann_json::nlohmann_json
)

# =============================================================================
# foobar2000 SDK 源文件
# =============================================================================
# PFC - foobar2000基础类库
file(GLOB PFC_SOURCES "${FOOBAR2000_SDK_PATH}/pfc/*.cpp")
list(REMOVE_ITEM PFC_SOURCES "${FOOBAR2000_SDK_PATH}/pfc/pfc-fb2k-hooks.cpp")

# SDK - foobar2000核心SDK
file(GLOB SDK_SOURCES "${FOOBAR2000_SDK_PATH}/foobar2000/SDK/*.cpp")

# 组件客户端
set(CLIENT_SOURCES "${FOOBAR2000_SDK_PATH}/foobar2000/foobar2000_component_client/component_client.cpp")

# 辅助工具
file(GLOB HELPER_SOURCES "${FOOBAR2000_SDK_PATH}/foobar2000/helpers/*.cpp")

# PPUI - UI组件库
file(GLOB LIBPPUI_SOURCES "${FOOBAR2000_SDK_PATH}/libPPUI/*.cpp")

# =============================================================================
# 插件主库 (foo_ai_metadata) - 动态库
# =============================================================================
# 包含:
#   - main.cpp        : 插件入口点
#   - menu_handler.cpp: 右键菜单处理
#   - tag_writer.cpp  : 元数据写入
#   - foo_ai_metadata.rc: 版本资源
# =============================================================================

set(PLUGIN_SOURCES
    plugin/main.cpp
    plugin/menu_handler.cpp
    plugin/preferences_page.cpp
    plugin/dialogs.cpp
    plugin/confirm_dialog.cpp
    plugin/backup_manager.cpp
    ${PROJECT_SOURCE_DIR}/foo_ai_metadata.rc
    ${PFC_SOURCES}
    ${SDK_SOURCES}
    ${CLIENT_SOURCES}
    ${HELPER_SOURCES}
    ${LIBPPUI_SOURCES}
)

add_library(foo_ai_metadata SHARED ${PLUGIN_SOURCES})

target_include_directories(foo_ai_metadata PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/third_party
    ${PROJECT_SOURCE_DIR}/include/third_party/sqlite
    ${FOOBAR2000_SDK_PATH}
    ${FOOBAR2000_SDK_PATH}/pfc
    ${FOOBAR2000_SDK_PATH}/foobar2000
    ${FOOBAR2000_SDK_PATH}/foobar2000/helpers
    ${FOOBAR2000_SDK_PATH}/foobar2000/shared
    ${FOOBAR2000_SDK_PATH}/libPPUI
)

target_compile_definitions(foo_ai_metadata PRIVATE
    _UNICODE
    UNICODE
    WIN32
    _WINDOWS
    STRICT
    FOOBAR2000_SDK_HELPERS
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
)

# =============================================================================
# 链接库
# =============================================================================
target_link_libraries(foo_ai_metadata
    PRIVATE
    ai_core
    "${FOOBAR2000_SDK_PATH}/foobar2000/shared/shared-x64.lib"
    nlohmann_json::nlohmann_json
    # Windows系统库
    shlwapi.lib
    msimg32.lib
    ws2_32.lib
    uxtheme.lib
    comctl32.lib
    gdi32.lib
    ole32.lib
    oleaut32.lib
    comdlg32.lib
    gdiplus.lib
    uuid.lib
)

# =============================================================================
# 部署配置 (可选)
# =============================================================================
# 设置 FOOBAR_DEV_DIR 后, 编译完成自动部署到本地 foobar2000 开发环境
# 留空则跳过部署 (适用于 CI/CD 或发布打包)
set(FOOBAR_DEV_DIR "" CACHE PATH "Local foobar2000 dev directory (empty to skip auto-deploy)")

if(FOOBAR_DEV_DIR)
    set(PLUGIN_DIR "${FOOBAR_DEV_DIR}/components/foo_ai_metadata")

    # 部署DLL到foobar2000 components目录
    add_custom_command(TARGET foo_ai_metadata POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FOOBAR_DEV_DIR}/components"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:foo_ai_metadata>
            "${FOOBAR_DEV_DIR}/components/$<TARGET_FILE_NAME:foo_ai_metadata>"
        COMMENT "Deploying plugin DLL to foobar2000 dev environment"
    )

    # 创建插件目录结构
    file(MAKE_DIRECTORY "${PLUGIN_DIR}")
    file(MAKE_DIRECTORY "${PLUGIN_DIR}/cache")
    file(MAKE_DIRECTORY "${PLUGIN_DIR}/logs")
    file(MAKE_DIRECTORY "${PLUGIN_DIR}/worker")

    # 部署Python Worker脚本
    add_custom_command(TARGET foo_ai_metadata POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/worker"
            "${PLUGIN_DIR}/worker"
        COMMENT "Deploying Python worker scripts to foobar2000 dev environment"
    )
endif()
