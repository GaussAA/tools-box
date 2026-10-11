# 一个工具插件 = 一个 MODULE 库（Windows 上是 DLL）。复制这个目录改名，就得到了
# 一个新工具的起点 —— 骨架（元数据生成 / core 静态库 / 插件库 / 图标 / 输出目录 /
# 测试注册）由 toolbox_add_module() 统一提供，见 cmake/ToolBoxModule.cmake。
#
# 编解码的纯逻辑编成静态库，好让测试目标直接链接它
# （分层判定标准见 docs/architecture.md §3）。
toolbox_add_module(base64
    CORE_SOURCES
        core/Base64Codec.cpp
        core/Base64Codec.h
    PLUGIN_SOURCES
        Base64Plugin.cpp
        Base64Plugin.h
    ICON icons/base64.svg
    TESTS tst_base64)
