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
    core/EngineInstall.cpp
    core/EngineInstall.h
    core/EngineLocator.cpp
    core/EngineLocator.h
    core/LineSplitter.cpp
    core/LineSplitter.h
    core/OutputParsing.cpp
    core/OutputParsing.h)

target_link_libraries(videodl_core PUBLIC Qt6::Core)

# PRIVATE, not PUBLIC (plan B): see plugins/base64/CMakeLists.txt. Own tests
# get the module root from toolbox_add_test().
target_include_directories(videodl_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# ── 编排层 ────────────────────────────────────────────────────────────────────
# 「把外部世界的事件翻译成界面信号」的那一层：DownloadRunner（yt-dlp 输出 → 进度）、
# DouyinResolver（渲染 → 直链）、EngineFetcher（传输 → 续传/重试/取消）。
#
# 它们**不是** core/，因为持有用户可见的 tr() 文案 —— 按架构 §3 的判定表，把 Model
# 的变化刷成界面文案属于 View 这一侧。但它们的价值恰恰全在决策上，而此前这层一个
# 用例都没有：外部通道（子进程 / 网络）被直接 new 在构造函数里，想测就必须真的把
# yt-dlp 与浏览器跑起来。
#
# 现在两条外部通道都经 IChildProcess / IEngineTransport 注入，测试可以换成假的，
# 于是「哪一行输出对应哪个阶段」「取消与重试撞车时谁赢」这些分支第一次可测 —— 而且
# 既不碰网络也不碰子进程，workflow §5 那条约定没有被绕过，只是不再是不写测试的理由。
#
# 单独成库（而不是并进 videodl_tool）是为了让测试目标能链接它：MODULE 库不可被链接。
# 用 qt_add_library 而非 add_library：这几个类都带 Q_OBJECT，需要 Qt 的自动 moc。
qt_add_library(videodl_orch STATIC
    IChildProcess.cpp
    IChildProcess.h
    IEngineTransport.cpp
    IEngineTransport.h
    DownloadRunner.cpp
    DownloadRunner.h
    DouyinResolver.cpp
    DouyinResolver.h
    EngineFetcher.cpp
    EngineFetcher.h)

target_link_libraries(videodl_orch PUBLIC videodl_core Qt6::Core Qt6::Network)

# PRIVATE, not PUBLIC (plan B): see plugins/base64/CMakeLists.txt. Own tests
# get the module root from toolbox_add_test().
target_include_directories(videodl_orch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# 视频下载插件本体。相比其它工具它多依赖一个 Qt 模块：
# Qt6::Network —— 用来在首次使用时把 yt-dlp / ffmpeg 下载到 tools/bin
# （由 videodl_orch 传递过来）。
add_library(videodl_tool MODULE
    VideoDlPlugin.cpp
    VideoDlPlugin.h)

target_link_libraries(videodl_tool PRIVATE ToolBox::Sdk videodl_orch videodl_core)

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

# ── 本模块的测试 ──────────────────────────────────────────────────────────────
# 用例就在被测代码旁边（plugins/videodl/tests/），改这个工具时不必跑到仓根另一头。
# 这是 D3「模块内聚」的第一个落地模块；D3 收官后其余模块也已同形态归位。
toolbox_add_test(tst_downloadargs videodl_core)
toolbox_add_test(tst_outputparsing videodl_core)
toolbox_add_test(tst_douyinsupport videodl_core)
toolbox_add_test(tst_cookiefile videodl_core)
toolbox_add_test(tst_enginelocator videodl_core)
toolbox_add_test(tst_enginecheck videodl_core)
toolbox_add_test(tst_enginefetchpolicy videodl_core)
toolbox_add_test(tst_linesplitter videodl_core)
toolbox_add_test(tst_engineinstall videodl_core)

# 编排层三个：外部通道（子进程 / 网络）经 IChildProcess / IEngineTransport 注入，
# 测试换成假实现驱动 —— 既不启动子进程也不发网络请求。
toolbox_add_test(tst_downloadrunner videodl_orch)
toolbox_add_test(tst_douyinresolver videodl_orch)
toolbox_add_test(tst_enginefetcher videodl_orch)
