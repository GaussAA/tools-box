# 又一个工具插件。注意这个插件没有任何资源文件，
# 图标交给外壳用工具名首字符兜底生成。
#
# 解析与序列化的纯逻辑编成静态库，好让测试目标直接链接它
# （分层判定标准见 docs/architecture.md §3）。
add_library(jsonfmt_core STATIC
    core/JsonFormat.cpp
    core/JsonFormat.h)

target_link_libraries(jsonfmt_core PUBLIC Qt6::Core)

# PRIVATE, not PUBLIC (plan B): see plugins/base64/CMakeLists.txt for the
# rationale -- no include path leaks to whoever links this library.
target_include_directories(jsonfmt_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# 同 base64：abi 字段由构建注入，不手写（见 app/core/PluginScanPolicy.h）。
configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

add_library(jsonfmt_tool MODULE
    JsonFormatPlugin.cpp
    JsonFormatPlugin.h)

target_link_libraries(jsonfmt_tool PRIVATE ToolBox::Sdk jsonfmt_core)

# Q_PLUGIN_METADATA 的 FILE 是按 include 路径找的，生成物在构建目录里。
target_include_directories(jsonfmt_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(jsonfmt_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")

# ── 本模块的测试 ──────────────────────────────────────────────────────────────
# D3 收官：用例在 jsonfmt/tests/ 下，与 videodl 同一形态。
toolbox_add_test(tst_jsonformat jsonfmt_core)
