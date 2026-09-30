cmake_minimum_required(VERSION 3.21)

project(ToolBox
    VERSION 0.1.0
    DESCRIPTION "插件式桌面工具箱"
    LANGUAGES CXX)

# 主程序与所有插件都必须是同一套 Qt、同一个编译器、同一个 C++ 标准。
# 插件 DLL 与主程序之间共享 QWidget 对象，二进制不兼容会直接崩溃。
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test)

qt_standard_project_setup()

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
