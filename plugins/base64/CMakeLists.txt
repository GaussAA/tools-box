# 一个工具插件 = 一个 MODULE 库（Windows 上是 DLL）。
# 复制这个目录改名，就得到了一个新工具的起点。
# 静态元数据由模板生成：abi（构建环境指纹）由构建注入，不手写 —— 手写迟早与本机
# 的 Qt / 编译器版本漂移，而漂移的后果是每个插件都被自家门禁拒掉（详见
# app/core/PluginScanPolicy.h）。
configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

add_library(base64_tool MODULE
    Base64Plugin.cpp
    Base64Plugin.h)

target_link_libraries(base64_tool PRIVATE ToolBox::Sdk)

# moc 处理 Q_PLUGIN_METADATA(FILE "metadata.json") 时是按 include 路径找这个文件的，
# 生成物在构建目录里，所以要把构建目录加进搜索路径。
target_include_directories(base64_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

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
