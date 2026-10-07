# 单元测试。用 Qt Test + CTest 跑，命令与约定见 docs/workflow.md §5。
#
# 本目录**不再是所有用例的家**。目标形态是「测试跟着被测模块走」：模块的用例放在
# <模块目录>/tests/ 下，由该模块自己的 CMakeLists.txt 用 toolbox_add_test() 注册
# （见 docs/architecture.md 迁移计划 D3）。videodl 是第一个搬完的模块。
#
# 留在这里的是两类：
#   * 不属于任何单个模块的用例 —— 外壳装配（tst_mainwindow）、跨 DLL 集成
#     （tst_integration）、契约层（tst_pluginmeta）；
#   * 其余模块**尚未搬迁**的用例（base64 / jsonfmt / imgwatermark / app）。
#     它们按 architecture.md §10 的规矩「下次改动该模块时顺带搬」；搬迁是纯移动，
#     因为脚本已按全仓扫描，不需要再动（过渡期的双路径查找见偏差 9.18）。
#
# 被测对象一律不依赖真实网络与真实子进程。个别确实要装配真实控件的用例
# （tst_mainwindow / tst_integration）使用 offscreen 平台的 QApplication，
# 它们只验证「构造与跨 DLL 装载不崩」，不模拟用户操作 —— 界面交互仍归
# scripts/verify/ 下的手工回归脚本（workflow §5）。
#
# toolbox_add_test() 由顶层 cmake/ToolBoxTest.cmake 全局提供：它把可执行文件统一落到
# build/tests/<Config>/，并把 Qt 的 bin 前置进 PATH，免得依赖用户机器上的环境变量。

toolbox_add_test(tst_toolcatalog ToolBoxCore)
toolbox_add_test(tst_languagechoice ToolBoxCore)
toolbox_add_test(tst_jsonformat jsonfmt_core)
toolbox_add_test(tst_base64 base64_core)
toolbox_add_test(tst_stego imgwatermark_core)

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
