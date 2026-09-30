# 又一个工具插件。注意这个插件没有任何资源文件，
# 图标交给外壳用工具名首字符兜底生成。
add_library(jsonfmt_tool MODULE
    JsonFormatPlugin.cpp
    JsonFormatPlugin.h)

target_link_libraries(jsonfmt_tool PRIVATE ToolBox::Sdk)

set_target_properties(jsonfmt_tool PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
    PREFIX "")
