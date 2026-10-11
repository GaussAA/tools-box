# 图片数字水印插件。
#
# 分层与其它插件一致（判定标准见 docs/architecture.md §3）：
#   * imgwatermark_core —— 纯逻辑（载荷编解码 + DCT 嵌入/提取），可单测；
#   * imgwatermark_tool —— 界面，只有它链 Qt6::Widgets。
#
# 与其余插件的两处**有意的不同**，都通过 toolbox_add_module() 的参数表达：
#   1. core 链 Qt6::Gui 而非 Qt6::Core —— 算法要直接操作 QImage 的像素。这仍然
#      满足「core 不含界面代码」：QtGui 里没有任何 QWidget。
#   2. 插件库额外链 Qt6::Concurrent —— 嵌入/提取在后台线程跑，界面不卡住。
# WatermarkText.* 也是纯逻辑（输出格式表 + 自动水印文本生成），放进 core 才能被
# tst_stego 链接 —— MODULE 库无法被测试 exe 链接（见偏差 9.17）。
toolbox_add_module(imgwatermark
    CORE_SOURCES
        WatermarkText.cpp
        WatermarkText.h
        core/Stego.cpp
        core/Stego.h
        core/StegoPayload.cpp
        core/StegoPayload.h
    CORE_LIBS Qt6::Gui
    PLUGIN_SOURCES
        ImgWatermarkPlugin.cpp
        ImgWatermarkPlugin.h
    PLUGIN_LIBS ToolBox::Sdk Qt6::Concurrent imgwatermark_core
    ICON icons/imgwatermark.svg
    TESTS tst_stego)
