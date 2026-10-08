# 插件 SDK 是纯头文件的接口库，不产生任何二进制，只负责把
# 头文件路径和 Qt 依赖传递给主程序与所有插件。
add_library(ToolBoxSdk INTERFACE)
add_library(ToolBox::Sdk ALIAS ToolBoxSdk)

target_include_directories(ToolBoxSdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ToolBoxSdk INTERFACE Qt6::Widgets)

# ── 契约层自己的测试 ────────────────────────────────────────────────────────
# D3 收官：用例在 sdk/tests/ 下，验证静态元数据的 IID 门禁与
# toolMetaFromMetaData() 的取值约定（MetaData → toolbox 两层取值）。
# 契约是纯头文件 INTERFACE 库，测试直接链接这个别名即获得头文件搜索路径。
toolbox_add_test(tst_pluginmeta ToolBox::Sdk)
