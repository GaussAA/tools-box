cmake_minimum_required(VERSION 3.25)

project(ToolBox
    VERSION 0.2.0
    DESCRIPTION "插件式桌面工具箱"
    LANGUAGES CXX)

# 主程序与所有插件都必须是同一套 Qt、同一个编译器、同一个 C++ 标准。
# 插件 DLL 与主程序之间共享 QWidget 对象，二进制不兼容会直接崩溃。
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test LinguistTools)

qt_standard_project_setup(REQUIRES 6.12)

# 单元测试走 CTest，命令见 docs/workflow.md §5。
enable_testing()

if(MSVC)
    # 源码里有中文字符串字面量，不加 /utf-8 会被当成 GBK 读成乱码。
    add_compile_options(/utf-8)
    # 警告等级。MSVC 默认只有 /W1，基本等于不查；这里提到 /W4 并开启标准一致性模式，
    # 目的是让「不引入新告警」这条规则由编译器兜底，而不是靠评审自觉
    # （见 docs/coding-standards.md §12）。警告一律当错误处理，否则会被忽略。
    add_compile_options(/W4 /permissive- /WX)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

# 统一输出布局，主程序靠这个布局去 tools/ 里找插件：
#   bin/<Config>/ToolBox.exe
#   bin/<Config>/tools/*.dll
set(TOOLBOX_BIN_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}")

add_subdirectory(sdk)
add_subdirectory(app)
add_subdirectory(plugins/base64)
add_subdirectory(plugins/jsonfmt)
add_subdirectory(plugins/videodl)
add_subdirectory(tests)

# ── 界面翻译 ────────────────────────────────────────────────────────────────
# 源语言是中文（tr() 里写的就是中文），这里编的是英文译文。lupdate 从下面这些
# 目标的源码里抽 tr() 字符串，lrelease 编成 .qm，再由 RESOURCE_PREFIX 编进可执行
# 文件的资源 —— 不落成外部文件，就不存在「翻译文件忘了一起拷」这种失败。
# （对比偏差 9.6 修的 Qt 自带翻译：那份必须随程序分发，所以只能走 translations/。）
#
# 新增插件时**必须把它的目标加进 SOURCE_TARGETS**，否则新插件的界面文案不会被抽取
# （workflow §4 第 9 步）。改写已有字符串后跑：
#   cmake --build build --target update_translations   刷新 .ts（然后补译文）
qt_add_translations(ToolBox
    TS_FILES translations/toolbox_en.ts
    SOURCE_TARGETS
        ToolBox
        ToolBoxApp
        ToolBoxCore
        base64_tool
        jsonfmt_tool
        videodl_tool
        videodl_core
    RESOURCE_PREFIX "/i18n"
    NO_GENERATE_PLURALS_TS_FILE)

# ── 交付打包 ────────────────────────────────────────────────────────────────
# 交付形态固定为「解压即用」的 zip，交付流程见 docs/workflow.md §9。
#
# 这里不逐个 install 目标，而是整个 bin/<Config>/ 目录一起装。理由是这个目录本身就
# 是运行时形态：主程序、tools/*.dll（插件）、Qt 运行时、translations/、以及
# tools/bin/ 下已下载的外部内核（架构 §8）。副作用是新增插件**不需要动本文件**，
# 与「新增工具不改外壳」这条核心约束保持一致。
install(DIRECTORY "${TOOLBOX_BIN_DIR}/"
        DESTINATION "."
        USE_SOURCE_PERMISSIONS)

# 打错配置、或者忘了跑 deploy，都在这里拦住，别等用户那边才发现。
# 用方括号实参的原样字符串：里面的变量要在安装时求值，不能在配置时被展开。
install(CODE [[
    if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Release")
        message(FATAL_ERROR
            "打包只针对 Release 配置（当前是 ${CMAKE_INSTALL_CONFIG_NAME}）。"
            "Debug 产物还依赖 MSVC 调试运行时，不用于分发。")
    endif()
    if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}/Qt6Core.dll")
        message(FATAL_ERROR
            "输出目录里没有 Qt 运行时。Qt 的 DLL 是 windeployqt 产生的，"
            "先跑 cmake --build build --config Release --target deploy。")
    endif()
]])

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "ToolBox")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "插件式桌面工具箱")
# 版本号不在打包配置里重复写：CPACK_PACKAGE_VERSION 默认取 CMAKE_PROJECT_VERSION，
# 也就是顶层 project(... VERSION ...) 那一个来源（见偏差 9.5）。
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-win64")
# zip 里套一层同名目录，解压不会把文件散在用户选定的目录里。
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
