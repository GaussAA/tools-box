# 一个工具插件 = 一个 MODULE 库（Windows 上是 DLL）。
# 复制这个目录改名，就得到了一个新工具的起点。
add_library(base64_tool MODULE
    Base64Plugin.cpp
    Base64Plugin.h)

target_link_libraries(base64_tool PRIVATE ToolBox::Sdk)

# 图标编进插件自己的资源里，插件拷到哪都带着图标，不需要额外文件。
qt_add_resources(base64_tool "base64_icons"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "icons/base64.svg")

set_target_properties(base64_tool PROPERTIES
    # 必须输出到主程序约定的插件目录，文件名不限。
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")
