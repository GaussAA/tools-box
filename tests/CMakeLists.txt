# 单元测试。用 Qt Test + CTest 跑，命令与约定见 docs/workflow.md §5。
#
# 这些测试不允许依赖 QApplication、真实网络或真实子进程 —— 被测的都是从界面
# 和进程里抽出来的纯逻辑（app/core、plugins/videodl/core）。

# 测试 exe 落在 build/tests/<Config>/，和主程序不在同一层，按默认搜索路径找不到
# Qt 的 DLL。这里把 Qt 的 bin 目录前置进 PATH，免得依赖用户机器上的环境变量。
get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_DEBUG)
if(NOT _qt_core_location)
    get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
endif()

if(_qt_core_location)
    get_filename_component(QT_BIN_DIR "${_qt_core_location}" DIRECTORY)
else()
    set(QT_BIN_DIR "")
    message(WARNING "定位不到 Qt6 Core 的运行时目录，测试可能因缺少 Qt DLL 而无法启动")
endif()

# 一个测试一个可执行文件。额外参数是需要链接的被测静态库。
function(toolbox_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})

    # 根 CMakeLists 把运行时输出目录统一指到了 bin/<Config>，那是要拷给别人
    # 的目录，测试程序不该混在里面，这里单独挪到 build/tests/<Config>。
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")

    add_test(NAME ${name} COMMAND ${name})

    if(QT_BIN_DIR)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}")
    endif()
endfunction()

toolbox_add_test(tst_toolcatalog ToolBoxCore)
toolbox_add_test(tst_outputparsing videodl_core)
toolbox_add_test(tst_douyinsupport videodl_core)
toolbox_add_test(tst_cookiefile videodl_core)
toolbox_add_test(tst_enginelocator videodl_core)
