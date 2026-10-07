# 图片数字水印插件。
#
# 分层与其它插件一致（判定标准见 docs/architecture.md §3）：
#   * imgwatermark_core —— 纯逻辑（载荷编解码 + DCT 嵌入/提取），可单测；
#   * imgwatermark_tool —— 界面，只有它链 Qt6::Widgets。
#
# 与 base64/jsonfmt 的一处**有意的不同**：core 链 Qt6::Gui 而非 Qt6::Core，
# 因为算法要直接操作 QImage 的像素。这仍然满足「core 不含界面代码」这条约束
# ——QtGui 里没有任何 QWidget，误 include 界面头文件照样编译不过。
# WatermarkText.* 也是纯逻辑（输出格式表 + 自动水印文本生成），放在这里才能被
# tst_stego 链接 —— MODULE 库无法被测试 exe 链接，那样这两段逻辑将永远无法单测
# （见 docs/architecture.md 偏差 9.17）。它不碰任何 QWidget，只用 QtCore。
add_library(imgwatermark_core STATIC
    WatermarkText.cpp
    WatermarkText.h
    core/Stego.cpp
    core/Stego.h
    core/StegoPayload.cpp
    core/StegoPayload.h)

target_link_libraries(imgwatermark_core PUBLIC Qt6::Gui)
target_include_directories(imgwatermark_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

add_library(imgwatermark_tool MODULE
    ImgWatermarkPlugin.cpp
    ImgWatermarkPlugin.h)

target_link_libraries(imgwatermark_tool PRIVATE ToolBox::Sdk Qt6::Concurrent imgwatermark_core)

# Q_PLUGIN_METADATA 的 FILE 是按 include 路径找的，生成物在构建目录里。
target_include_directories(imgwatermark_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# 导航图标随 DLL 编进资源，路径是 :/icons/imgwatermark.svg。
qt_add_resources(imgwatermark_tool "imgwatermark_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "icons/imgwatermark.svg")

set_target_properties(imgwatermark_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")
