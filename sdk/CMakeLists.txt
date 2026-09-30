# 插件 SDK 是纯头文件的接口库，不产生任何二进制，只负责把
# 头文件路径和 Qt 依赖传递给主程序与所有插件。
add_library(ToolBoxSdk INTERFACE)
add_library(ToolBox::Sdk ALIAS ToolBoxSdk)

target_include_directories(ToolBoxSdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ToolBoxSdk INTERFACE Qt6::Widgets)
