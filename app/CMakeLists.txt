# 外壳里不依赖界面的逻辑单独编成一个静态库，好让测试目标直接链接它。
# 分层判定标准见 docs/architecture.md §3。
add_library(ToolBoxCore STATIC
    core/ToolCatalog.cpp
    core/ToolCatalog.h)

target_link_libraries(ToolBoxCore PUBLIC Qt6::Core)

# 把头文件目录暴露出去，测试目标才能 #include "core/ToolCatalog.h"
target_include_directories(ToolBoxCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

qt_add_executable(ToolBox
    main.cpp
    MainWindow.cpp
    MainWindow.h
    ToolRegistry.cpp
    ToolRegistry.h)

# 不带控制台窗口，双击 exe 不会弹黑框。
set_target_properties(ToolBox PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(ToolBox PRIVATE ToolBox::Sdk ToolBoxCore)

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
