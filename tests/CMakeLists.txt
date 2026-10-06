# 单元测试。用 Qt Test + CTest 跑，命令与约定见 docs/workflow.md §5。
#
# 被测对象是抽出来的纯逻辑（app/core、plugins/videodl/core），一律不依赖真实网络与
# 真实子进程。个别确实要装配真实控件的用例（tst_mainwindow / tst_integration）使用
# offscreen 平台的 QApplication，它们只验证「构造与跨 DLL 装载不崩」，不模拟用户操作
# ——界面交互仍归 scripts/verify/ 下的手工回归脚本（workflow §5）。

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
toolbox_add_test(tst_languagechoice ToolBoxCore)
toolbox_add_test(tst_jsonformat jsonfmt_core)
toolbox_add_test(tst_base64 base64_core)
toolbox_add_test(tst_downloadargs videodl_core)
toolbox_add_test(tst_outputparsing videodl_core)
toolbox_add_test(tst_douyinsupport videodl_core)
toolbox_add_test(tst_cookiefile videodl_core)
toolbox_add_test(tst_enginelocator videodl_core)
toolbox_add_test(tst_enginecheck videodl_core)
toolbox_add_test(tst_enginefetchpolicy videodl_core)
toolbox_add_test(tst_linesplitter videodl_core)
toolbox_add_test(tst_pluginscanpolicy ToolBoxCore)
toolbox_add_test(tst_pluginmeta ToolBox::Sdk)
toolbox_add_test(tst_logger ToolBoxCore)
toolbox_add_test(tst_mainwindow ToolBoxApp)

# ── 端到端集成测试 ────────────────────────────────────────────────────────────
# 部署 base64/jsonfmt 两个纯 GUI 安全插件到测试运行目录的 tools/，验证
# ToolRegistry 跨 DLL 扫描 + qobject_cast + createPage 真实路径（tst_mainwindow 刻意回避）。
toolbox_add_test(tst_integration ToolBoxApp)

# 先确保插件被构建，再拷贝 DLL 到 <测试exe目录>/tools/。
add_dependencies(tst_integration base64_tool jsonfmt_tool)

# 部署到专属目录 integration_tools，而非主程序约定的 tools/ —— 因为所有测试 exe
# 都落在 tests/<Config>/，若用共享的 tools/，本测试部署的插件会被同目录的 tst_mainwindow
# 经 defaultPluginDir() 扫到，破坏其「无插件」前提。独立目录让两测试互不干扰。
add_custom_command(TARGET tst_integration POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:tst_integration>/integration_tools"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:base64_tool>" "$<TARGET_FILE_DIR:tst_integration>/integration_tools/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:jsonfmt_tool>" "$<TARGET_FILE_DIR:tst_integration>/integration_tools/"
    COMMENT "部署 base64/jsonfmt 插件到 tst_integration 专属目录（端到端集成测试）"
    VERBATIM)
