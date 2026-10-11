# 又一个工具插件。图标跟 base64 一样编进插件自己的资源里，插件拷到哪都带着。
#
# 解析与序列化的纯逻辑编成静态库，好让测试目标直接链接它
# （分层判定标准见 docs/architecture.md §3）。
toolbox_add_module(jsonfmt
    CORE_SOURCES
        core/JsonFormat.cpp
        core/JsonFormat.h
    PLUGIN_SOURCES
        JsonFormatPlugin.cpp
        JsonFormatPlugin.h
    ICON icons/jsonfmt.svg
    TESTS tst_jsonformat)
