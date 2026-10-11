# 又一个工具插件（第 5 个）。图标跟 base64 一样编进插件自己的资源里，
# 插件拷到哪都带着。
#
# 摘要计算的纯逻辑编成静态库，好让测试目标直接链接它
# （分层判定标准见 docs/architecture.md §3）。
add_library(hashsum_core STATIC
    core/HashSum.cpp
    core/HashSum.h)

target_link_libraries(hashsum_core PUBLIC Qt6::Core)

# PRIVATE, not PUBLIC (plan B): see plugins/base64/CMakeLists.txt for the
# rationale -- no include path leaks to whoever links this library.
target_include_directories(hashsum_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# 同 base64：abi 字段由构建注入，不手写（见 app/core/PluginScanPolicy.h）。
configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

add_library(hashsum_tool MODULE
    HashSumPlugin.cpp
    HashSumPlugin.h)

target_link_libraries(hashsum_tool PRIVATE ToolBox::Sdk hashsum_core)

# Q_PLUGIN_METADATA 的 FILE 是按 include 路径找的，生成物在构建目录里。
target_include_directories(hashsum_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# 图标编进插件自己的资源里，不需要额外文件。
qt_add_resources(hashsum_tool "hashsum_icons"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "icons/hashsum.svg")

set_target_properties(hashsum_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")

# ── 本模块的测试 ──────────────────────────────────────────────────────────────
# D3：用例在 hashsum/tests/ 下，与其余模块同一形态。
toolbox_add_test(tst_hashsum hashsum_core)
