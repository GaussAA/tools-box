# 视频下载插件。相比其它工具它多一层：orch（编排层）。
#
# 分层（判定标准见 docs/architecture.md §3）：
#   * videodl_core —— 纯逻辑（命令行构造、输出解析、策略），只链 Qt6::Core；
#   * videodl_orch —— 「把外部世界的事件翻译成界面信号」的那一层：DownloadRunner
#     （yt-dlp 输出 → 进度）、DouyinResolver（渲染 → 直链）、EngineFetcher
#     （传输 → 续传/重试/取消）。
#
# 它们**不是** core/，因为持有用户可见的 tr() 文案 —— 按架构 §3 的判定表，把 Model
# 的变化刷成界面文案属于 View 这一侧。但它们的价值恰恰全在决策上：两条外部通道
# （子进程 / 网络）经 IChildProcess / IEngineTransport 注入后，测试可以换成假的，
# 于是「哪一行输出对应哪个阶段」「取消与重试撞车时谁赢」这些分支第一次可测 ——
# 而且既不碰网络也不碰子进程。
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
    ORCH_TESTS
        tst_downloadrunner
        tst_douyinresolver
        tst_enginefetcher)
