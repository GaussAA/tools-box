# 视频下载插件。相比其它工具它多依赖一个 Qt 模块：
# Qt6::Network —— 用来在首次使用时把 yt-dlp / ffmpeg 下载到 tools/bin。
add_library(videodl_tool MODULE
    VideoDlPlugin.cpp
    VideoDlPlugin.h)

target_link_libraries(videodl_tool PRIVATE ToolBox::Sdk Qt6::Network)

# 图标编进插件自己的资源里，插件拷到哪都带着图标，不需要额外文件。
qt_add_resources(videodl_tool "videodl_icons"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "icons/videodl.svg")

set_target_properties(videodl_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")