# 又一个工具插件（第 5 个）。图标跟 base64 一样编进插件自己的资源里，
# 插件拷到哪都带着。
#
# 摘要计算的纯逻辑编成静态库，好让测试目标直接链接它
# （分层判定标准见 docs/architecture.md §3）。
toolbox_add_module(hashsum
    CORE_SOURCES
        core/HashSum.cpp
        core/HashSum.h
    PLUGIN_SOURCES
        HashSumPlugin.cpp
        HashSumPlugin.h
    ICON icons/hashsum.svg
    TESTS tst_hashsum)
