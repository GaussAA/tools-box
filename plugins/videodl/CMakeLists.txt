# 插件里不依赖界面的逻辑单独编成一个静态库，好让测试目标直接链接它。
# 分层判定标准见 docs/architecture.md §3。
add_library(videodl_core STATIC
    core/CookieFile.cpp
    core/CookieFile.h
    core/DouyinSupport.cpp
    core/DouyinSupport.h
    core/DownloadArgs.cpp
    core/DownloadArgs.h
    core/EngineCheck.cpp
    core/EngineCheck.h
    core/EngineFetchPolicy.cpp
    core/EngineFetchPolicy.h
    core/EngineLocator.cpp
    core/EngineLocator.h
    core/OutputParsing.cpp
    core/OutputParsing.h)

target_link_libraries(videodl_core PUBLIC Qt6::Core)

# 把头文件目录暴露出去，测试目标才能 #include "core/OutputParsing.h"
target_include_directories(videodl_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# 视频下载插件。相比其它工具它多依赖一个 Qt 模块：
# Qt6::Network —— 用来在首次使用时把 yt-dlp / ffmpeg 下载到 tools/bin。
add_library(videodl_tool MODULE
    VideoDlPlugin.cpp
    VideoDlPlugin.h
    DownloadRunner.cpp
    DownloadRunner.h
    DouyinResolver.cpp
    DouyinResolver.h
    EngineFetcher.cpp
    EngineFetcher.h)

target_link_libraries(videodl_tool PRIVATE ToolBox::Sdk videodl_core Qt6::Network)

# 同 base64：静态元数据由模板生成，abi 字段由构建注入。
configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

# Q_PLUGIN_METADATA 的 FILE 是按 include 路径找的，生成物在构建目录里。
target_include_directories(videodl_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# 图标编进插件自己的资源里，插件拷到哪都带着图标，不需要额外文件。
qt_add_resources(videodl_tool "videodl_icons"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "icons/videodl.svg")

set_target_properties(videodl_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")
