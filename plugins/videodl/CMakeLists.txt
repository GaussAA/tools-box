# 视频下载插件。相比其它工具它多一层：orch（编排层）。
#
# 分层（判定标准见 docs/architecture.md §3）：
#   * videodl_core —— 纯逻辑（命令行构造、输出解析、策略），只链 Qt6::Core；
#   * videodl_orch —— 「把外部世界的事件翻译成界面信号」的那一层：DownloadRunner
#     （yt-dlp 输出 → 进度）、DouyinResolver（渲染 → 直链）、EngineFetcher
#     （传输 → 续传/重试/取消）。
#
# 它们**不是** core/，因为持有用户可见的 tr() 文案 —— 按架构 §3 的判定表，把 Model
# 的变化刷成界面文案属于 View 这一侧。但它们的价值恰恰全在决策上：外部通道
# （子进程 / 网络）经 IChildProcess / IEngineTransport 注入后，测试可以换成假的，
# 于是「哪一行输出对应哪个阶段」「取消与重试撞车时谁赢」这些分支第一次可测 ——
# 而且既不碰网络也不碰子进程。
#
# 第三方依赖 miniz（偏差 9.13：ffmpeg 解压不再走 powershell.exe 子进程）：
#   * 来源   https://github.com/richgel999/miniz（已核实：MIT，仓库活跃）
#   * 版本   release 3.1.2（2026-07-01）官方 zip 附件里的 amalgamation 三件
#     （miniz.c / miniz.h / LICENSE，原样入库，未做任何修改）
#   * 隔离   单独编静态库并 /W0 —— 第三方代码不按本项目的 /W4 /WX 管，
#     升级换版本时也不受全局警告等级变化的影响。
#   * 消费者 只有 videodl_core 的 ZipExtract.cpp 与测试 tst_zipextract；
#     别的模块要用解压能力时走 core 的封装，不直接 include miniz。
# 路径锚在仓库根的 third_party/：第三方代码是仓库级资产，不随插件目录走。
add_library(miniz STATIC "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${PROJECT_SOURCE_DIR}/third_party/miniz")
if(MSVC)
    target_compile_options(miniz PRIVATE /W0)
endif()

toolbox_add_module(videodl
    CORE_SOURCES
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
        core/OutputParsing.h
        core/ZipExtract.cpp
        core/ZipExtract.h
    CORE_LIBS
        Qt6::Core
        miniz
    ORCH_SOURCES
        IChildProcess.cpp
        IChildProcess.h
        IEngineTransport.cpp
        IEngineTransport.h
        DownloadRunner.cpp
        DownloadRunner.h
        DouyinResolver.cpp
        DouyinResolver.h
        EngineFetcher.cpp
        EngineFetcher.h
    # EngineFetcher 的解压收尾用 QtConcurrent（内嵌 miniz 后不再走子进程，
    # 但百 MB 级提取不能占主线程 —— 见 EngineFetcher::extractFfmpeg）。
    ORCH_LIBS
        videodl_core
        Qt6::Core
        Qt6::Network
        Qt6::Concurrent
    PLUGIN_SOURCES
        VideoDlPlugin.cpp
        VideoDlPlugin.h
    ICON icons/videodl.svg
    TESTS
        tst_downloadargs
        tst_outputparsing
        tst_douyinsupport
        tst_cookiefile
        tst_enginelocator
        tst_enginecheck
        tst_enginefetchpolicy
        tst_linesplitter
        tst_engineinstall
        tst_zipextract
    ORCH_TESTS
        tst_downloadrunner
        tst_douyinresolver
        tst_enginefetcher)
