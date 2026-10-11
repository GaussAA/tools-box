# 一个工具插件 = 一段固定骨架：静态元数据生成 + core 静态库 + 插件 MODULE 库
# + 图标资源 + 输出目录 + 测试注册。五个插件各抄一份，就会出现五份只有名字不同
# 的 CMakeLists —— 改一处（比如给 core 库换链接方式）要改五遍，而且迟早有一处
# 漏掉。这就是偏差 9.7 记的那笔账，现在由这个函数统一收口。
#
# 触发条件（当时写下的判据）：**出现第 5 个插件**。dev.hash-sum 落地后达成。
#
# 用法：
#   toolbox_add_module(<名字>
#       CORE_SOURCES   <core 的头与源>          # 编成 <名字>_core（静态库，可单测）
#       [CORE_LIBS     <Qt 模块>…]              # 默认 Qt6::Core
#       [ORCH_SOURCES  <编排层头与源>…]          # 编成 <名字>_orch（qt_add_library，跑 moc）
#       [ORCH_LIBS     <库>…]                   # 默认 <名字>_core Qt6::Core Qt6::Network
#       PLUGIN_SOURCES <插件头与源>…             # 编成 <名字>_tool（MODULE，即 DLL）
#       [PLUGIN_LIBS   <库>…]                   # 默认 ToolBox::Sdk <名字>_core（有 orch 时含 orch）
#       [ICON          <图标文件>]
#       [TESTS         <用例名>…]                # 链 <名字>_core
#       [ORCH_TESTS    <用例名>…])               # 链 <名字>_orch
#
# 刻意**没有**把 metadata.json.in 的路径、输出目录这类东西做成参数：它们在全项目
# 里只有一个值，做成参数只会让人以为可以不一样。真出现需要不一样的插件，说明约定
# 本身要改，那时候该动的是这个函数而不是某个插件的调用参数。

include_guard(GLOBAL)

function(toolbox_add_module name)
    set(options "")
    set(oneValueArgs ICON)
    set(multiValueArgs CORE_SOURCES CORE_LIBS ORCH_SOURCES ORCH_LIBS PLUGIN_SOURCES
                       PLUGIN_LIBS TESTS ORCH_TESTS)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "toolbox_add_module(${name}): 无法识别的参数：${ARG_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT ARG_PLUGIN_SOURCES)
        message(FATAL_ERROR "toolbox_add_module(${name}): 缺少 PLUGIN_SOURCES")
    endif()

    # 静态元数据：abi 由构建注入，不手写（见 app/core/PluginScanPolicy.h）。
    # 生成物落在构建目录，所以下面把构建目录加进插件库的搜索路径。
    configure_file(metadata.json.in "${CMAKE_CURRENT_BINARY_DIR}/metadata.json" @ONLY)

    if(ARG_CORE_SOURCES)
        if(NOT ARG_CORE_LIBS)
            set(ARG_CORE_LIBS Qt6::Core)
        endif()
        add_library(${name}_core STATIC ${ARG_CORE_SOURCES})
        target_link_libraries(${name}_core PUBLIC ${ARG_CORE_LIBS})
        # PRIVATE, not PUBLIC (plan B): 链接 core 库不得顺带把它的头文件路径送出去。
        # 自家测试由 toolbox_add_test() 领本模块路径；外模块显式加路径会被
        # verify_moduleboundaries 的 R4 拦下。
        target_include_directories(${name}_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    # 编排层只在需要时出现（目前只有 videodl 有）。用 qt_add_library 而非
    # add_library：这一层的类带 Q_OBJECT，需要 Qt 的自动 moc，普通 add_library
    # 不跑 moc 会缺虚表符号。
    if(ARG_ORCH_SOURCES)
        if(NOT ARG_ORCH_LIBS)
            set(ARG_ORCH_LIBS ${name}_core Qt6::Core Qt6::Network)
        endif()
        qt_add_library(${name}_orch STATIC ${ARG_ORCH_SOURCES})
        target_link_libraries(${name}_orch PUBLIC ${ARG_ORCH_LIBS})
        target_include_directories(${name}_orch PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()

    if(NOT ARG_PLUGIN_LIBS)
        set(ARG_PLUGIN_LIBS ToolBox::Sdk ${name}_core)
        if(ARG_ORCH_SOURCES)
            list(INSERT ARG_PLUGIN_LIBS 1 ${name}_orch)
        endif()
    endif()

    add_library(${name}_tool MODULE ${ARG_PLUGIN_SOURCES})
    target_link_libraries(${name}_tool PRIVATE ${ARG_PLUGIN_LIBS})
    # Q_PLUGIN_METADATA 的 FILE 是按 include 路径找 metadata.json 的。
    target_include_directories(${name}_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

    # 图标编进插件自己的资源里，插件拷到哪都带着，不需要额外文件。
    if(ARG_ICON)
        qt_add_resources(${name}_tool "${name}_icons"
            PREFIX "/"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}"
            FILES "${ARG_ICON}")
    endif()

    set_target_properties(${name}_tool PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
        RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}/tools"
        PREFIX "")

    # 用例就在被测代码旁边（<模块>/tests/），见 D3。
    foreach(test IN LISTS ARG_TESTS)
        toolbox_add_test(${test} ${name}_core)
    endforeach()
    foreach(test IN LISTS ARG_ORCH_TESTS)
        toolbox_add_test(${test} ${name}_orch)
    endforeach()
endfunction()
