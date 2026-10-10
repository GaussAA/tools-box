# 外壳里不依赖界面的逻辑单独编成一个静态库，好让测试目标直接链接它。
# 分层判定标准见 docs/architecture.md §3。
add_library(ToolBoxCore STATIC
    core/ToolCatalog.cpp
    core/ToolCatalog.h
    core/LanguageChoice.cpp
    core/LanguageChoice.h
    core/Logger.cpp
    core/Logger.h
    core/PluginScanPolicy.cpp
    core/PluginScanPolicy.h)

target_link_libraries(ToolBoxCore PUBLIC Qt6::Core)

# 宿主的构建环境指纹。与写进各插件 metadata.json 的 abi 字段同源（顶层
# CMakeLists.txt 的 TOOLBOX_PLUGIN_ABI）， PluginScanPolicy 用它做装载前的门禁。
target_compile_definitions(ToolBoxCore PRIVATE TOOLBOX_HOST_ABI="${TOOLBOX_PLUGIN_ABI}")

# PRIVATE, not PUBLIC (plan B): the shell is never included by anyone (plugins
# may not reach into app), so no include path needs to leave this module. Own
# tests get the module root from toolbox_add_test().
target_include_directories(ToolBoxCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# 把「外壳界面 + 插件装载」抽成一个静态库，让烟雾测试能直接链接它，
# 而不必把 MainWindow.cpp / ToolRegistry.cpp 重复编译两份（见 docs/best-practices-assessment.md §3.4）。
# 用 qt_add_library 而非 add_library：库里 MainWindow/ToolRegistry 都带 Q_OBJECT，
# 需要 Qt 的自动 moc，普通 add_library 不会跑 moc 会缺虚表符号。
qt_add_library(ToolBoxApp STATIC
    MainWindow.cpp
    MainWindow.h
    ToolIcons.cpp
    ToolIcons.h
    ToolRegistry.cpp
    ToolRegistry.h)
target_link_libraries(ToolBoxApp PUBLIC ToolBox::Sdk ToolBoxCore Qt6::Widgets)
# PRIVATE, not PUBLIC (plan B): see ToolBoxCore above.
target_include_directories(ToolBoxApp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_executable(ToolBox
    main.cpp)

# 不带控制台窗口，双击 exe 不会弹黑框。
set_target_properties(ToolBox PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(ToolBox PRIVATE ToolBoxApp)

# 版本号只有一个来源：顶层 CMakeLists.txt 的 project(... VERSION ...)。
# 这里把它传成编译定义，main.cpp 直接引用，避免源码里再硬编码一份（见偏差 9.5）。
target_compile_definitions(ToolBox PRIVATE TOOLBOX_VERSION="${PROJECT_VERSION}")

# 可选目标：cmake --build <dir> --target deploy 把 Qt 运行时 DLL 收进输出目录，
# 之后整个 bin/<Config>/ 目录可以直接拷给别人运行。
find_program(WINDEPLOYQT_EXE
    NAMES windeployqt
    HINTS "${Qt6_DIR}/../../../bin" "${QT6_INSTALL_PREFIX}/bin")

# ── app-local VC 运行时 ─────────────────────────────────────────────────────
# windeployqt 默认往输出目录放 vc_redist.x64.exe —— 那是 17.9 MB 的**安装器**，
# 接收方还得先跑它一次。改成把 CRT 的 DLL 直接摆在程序旁边（微软支持的 app-local
# 部署方式），包更小、而且真的「解压即用」。
#
# 目录名里的版本号随工具集走（VS 18 / MSVC 14.51 是 Microsoft.VC145.CRT，
# VS 2022 是 VC143），所以用 glob，不要写死。
file(GLOB TOOLBOX_CRT_DLLS
     "$ENV{VCToolsRedistDir}x64/Microsoft.VC*.CRT/*.dll")

if(WINDEPLOYQT_EXE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXE}"
                --translations zh_CN
                # 纯 Widgets 走光栅引擎，不需要软件 OpenGL 兜底（省 19.7 MB）。
                --no-opengl-sw
                # 不拷编译器运行时安装器，改用上面的 app-local DLL。
                --no-compiler-runtime
                --no-system-d3d-compiler
                --dir "$<TARGET_FILE_DIR:ToolBox>"
                "$<TARGET_FILE:ToolBox>"
        DEPENDS ToolBox
        COMMENT "部署 Qt 运行时到输出目录")
elseif(TOOLBOX_CRT_DLLS)
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${TOOLBOX_CRT_DLLS} "$<TARGET_FILE_DIR:ToolBox>"
        DEPENDS ToolBox
        COMMENT "未找到 windeployqt，只部署 VC 运行时")
else()
    # 找不到 CRT 就明确失败。VCToolsRedistDir 只在 VS 开发环境下存在，静默跳过会
    # 产出一个「能构建、接收方一运行就缺 DLL」的包 —— 那种问题到用户那边才发现。
    add_custom_target(deploy
        COMMAND "${CMAKE_COMMAND}" -E echo
                "ERROR: 既没有 windeployqt，也没有找到 VC 运行时目录。"
        COMMAND "${CMAKE_COMMAND}" -E echo
                "       请在 Visual Studio 开发环境下重新配置（vcvars64 / DevShell）。"
        COMMAND "${CMAKE_COMMAND}" -E false
        COMMENT "部署失败：缺少 Qt 与 VC 运行时")
endif()

if(WINDEPLOYQT_EXE AND TOOLBOX_CRT_DLLS)
    add_custom_command(TARGET deploy POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${TOOLBOX_CRT_DLLS} "$<TARGET_FILE_DIR:ToolBox>"
        COMMENT "复制 VC 运行时（app-local）")
endif()

# ── 外壳自己的测试 ──────────────────────────────────────────────────────────
# D3 收官：测试跟着被测代码走，用例在 app/tests/ 下。core/ 的四个用例链
# ToolBoxCore；窗口装配与跨 DLL 装载用例链 ToolBoxApp，使用 offscreen 平台的
# QApplication（在其 main() 里、构造 QApplication 之前设置，见 workflow.md §5）。
toolbox_add_test(tst_toolcatalog ToolBoxCore)
toolbox_add_test(tst_languagechoice ToolBoxCore)
toolbox_add_test(tst_pluginscanpolicy ToolBoxCore)
toolbox_add_test(tst_logger ToolBoxCore)
toolbox_add_test(tst_mainwindow ToolBoxApp)

# tst_integration 也归外壳：它测的是 ToolRegistry 的跨 DLL 装载能力，
# base64/jsonfmt 两个插件 DLL 只是它自己部署的测试夹具。
toolbox_add_test(tst_integration ToolBoxApp)

# 先确保插件被构建，再拷贝 DLL 到 <测试exe目录>/tools/。
add_dependencies(tst_integration base64_tool jsonfmt_tool)

# 部署到专属目录 integration_tools，而非主程序约定的 tools/ —— 所有测试 exe 都
# 落在 build/tests/<Config>/ 同一层，若用共享的 tools/，本测试部署的插件会被
# 同目录的 tst_mainwindow 经 defaultPluginDir() 扫到，破坏其「无插件」前提。
# 独立目录让两测试互不干扰。
add_custom_command(TARGET tst_integration POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:tst_integration>/integration_tools"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:base64_tool>" "$<TARGET_FILE_DIR:tst_integration>/integration_tools/"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:jsonfmt_tool>" "$<TARGET_FILE_DIR:tst_integration>/integration_tools/"
    COMMENT "部署 base64/jsonfmt 插件到 tst_integration 专属目录（端到端集成测试）"
    VERBATIM)
