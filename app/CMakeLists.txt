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

if(WINDEPLOYQT_EXE)
    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXE}"
                --translations zh_CN
                --no-system-d3d-compiler
                --dir "$<TARGET_FILE_DIR:ToolBox>"
                "$<TARGET_FILE:ToolBox>"
        DEPENDS ToolBox
        COMMENT "部署 Qt 运行时到输出目录")
else()
    message(STATUS "未找到 windeployqt，跳过 deploy 目标")
endif()
