cmake_minimum_required(VERSION 3.25)

project(ToolBox
    VERSION 0.2.0
    DESCRIPTION "插件式桌面工具箱"
    LANGUAGES CXX)

# 主程序与所有插件都必须是同一套 Qt、同一个编译器、同一个 C++ 标准。
# 插件 DLL 与主程序之间共享 QWidget 对象，二进制不兼容会直接崩溃。
# Concurrent：图片数字水印插件用它把嵌入/提取放到后台线程（大图是CPU 密集型，
# 放GUI 线程会卡住界面，见 docs/architecture.md §5）。原先只需 Core/Widgets/
# Network/Test/LinguistTools，是那个插件带进来的。
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Test LinguistTools)

# ── C++ 标准：显式钉住 17，不靠 Qt 的默认值 ────────────────────────────────────
# qt_standard_project_setup() 会在变量未设置时替我们填一个默认值，但那个默认值
# 随 Qt 版本走 —— 升级 Qt 时标准可能被静默抬高，而 docs/coding-standards.md §9
# 写的是「标准固定 17，不允许使用更高标准的特性」。规则要成立就得自己写出来，
# 不能靠上游的默认值兜着。
#
#   * CMAKE_CXX_STANDARD_REQUIRED ON —— 编译器不支持 17 时直接失败，而不是悄悄
#     降级到一个能编的标准（降级的后果是「本机编得过、CI 编不过」）。
#   * CMAKE_CXX_EXTENSIONS OFF       —— 禁用 -std=gnu++17 这类带编译器扩展的档位，
#     避免写出只有 MSVC 认的写法。MSVC 本来也没有这档，主要是防将来接其它编译器。
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

qt_standard_project_setup(REQUIRES 6.12)

# 单元测试走 CTest，命令见 docs/workflow.md §5。
enable_testing()

if(MSVC)
    # 源码里有中文字符串字面量，不加 /utf-8 会被当成 GBK 读成乱码。
    add_compile_options(/utf-8)
    # 警告等级。MSVC 默认只有 /W1，基本等于不查；这里提到 /W4 并开启标准一致性模式，
    # 目的是让「不引入新告警」这条规则由编译器兜底，而不是靠评审自觉
    # （见 docs/coding-standards.md §12）。警告一律当错误处理，否则会被忽略。
    add_compile_options(/W4 /permissive- /WX)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型" FORCE)
endif()

# ── 编译加速：装了 ccache 就自动接上 ─────────────────────────────────────────
# 没装则静默跳过，构建行为完全不变 —— 所以 CI（不装 ccache）与本机（装了）
# 都能直接跑，不需要两套配置。
#
# 关键点：**必须同时把 Debug 的调试信息格式切成 Embedded（/Z7）**。
# MSVC 默认 /Zi 走共享 PDB，命令行里带着 `/Fd<构建目录>\`，而 ccache 一见调试信息
# 相关 flag 就把当前工作目录算进哈希 —— 换个构建目录（「清空 build 重配」是最常见的
# 姿势）就全部 miss。实测本机：不切 /Z7 时跨目录命中率直接归零。
#
# 实测收益（本机 i5-12500H，41 个 TU，Debug，touch 全部源文件后重编）：
#   不接 ccache  55.3 s
#   接了 ccache  25.1 s（命中 36/39 = 92%）
# 首次（缓存为空）反而略慢，因为要写缓存；收益从第二次全量重建开始显现。
# 关掉它：-DTOOLBOX_USE_CCACHE=OFF。
option(TOOLBOX_USE_CCACHE "用 ccache 加速编译（未安装则自动忽略）" ON)
if(TOOLBOX_USE_CCACHE AND MSVC)
    find_program(TOOLBOX_CCACHE_EXECUTABLE NAMES ccache)
    if(TOOLBOX_CCACHE_EXECUTABLE)
        set(CMAKE_C_COMPILER_LAUNCHER "${TOOLBOX_CCACHE_EXECUTABLE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${TOOLBOX_CCACHE_EXECUTABLE}")
        # /Z7：调试信息进各自的 .obj，不引用构建目录专属的 PDB 路径。
        # 调试体验不受影响（VS 照样能断点），代价是 .obj 变大、链接稍慢。
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)
        message(STATUS "ccache: 已接入 ${TOOLBOX_CCACHE_EXECUTABLE}（Debug 调试信息改用 /Z7）")
    else()
        message(STATUS "ccache: 未找到，跳过（编译不受影响）")
    endif()
endif()

# 统一输出布局，主程序靠这个布局去 tools/ 里找插件：
#   bin/<Config>/ToolBox.exe
#   bin/<Config>/tools/*.dll
set(TOOLBOX_BIN_DIR "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${TOOLBOX_BIN_DIR}")

# ── 构建环境指纹（插件 ABI 门禁）─────────────────────────────────────────────
# 每个插件的 metadata.json 里带一个 abi 字段，宿主在 load() 之前拿它跟自己比。
#
# 光比对 IID 只能挡「接口版本不对」，挡不住「接口版本对、但用的是另一套 Qt 或
# 编译器」—— 那种 DLL 照样能被 QPluginLoader 加载进来，然后因为二进制不兼容
# 在宿主进程里崩掉。指纹写在静态元数据里，不 load() 就能读到，所以挡得住。
#
# 宿主侧拿同一个值，由 TOOLBOX_HOST_ABI 编进 ToolBoxCore（app/CMakeLists.txt）。
# 两边**必须同源**，否则每个插件都会被自己拒掉 —— 这里只 set 一次就是这个原因。
if(MSVC)
    set(TOOLBOX_PLUGIN_ABI "${Qt6_VERSION}-${CMAKE_CXX_COMPILER_ID}-${MSVC_TOOLSET_VERSION}")
else()
    set(TOOLBOX_PLUGIN_ABI
        "${Qt6_VERSION}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
endif()

add_subdirectory(sdk)
add_subdirectory(app)
add_subdirectory(plugins/base64)
add_subdirectory(plugins/jsonfmt)
add_subdirectory(plugins/imgwatermark)
add_subdirectory(plugins/videodl)
add_subdirectory(tests)

# ── 界面翻译 ────────────────────────────────────────────────────────────────
# 源语言是中文（tr() 里写的就是中文），这里编的是英文译文。lupdate 从下面这些
# 目标的源码里抽 tr() 字符串，lrelease 编成 .qm，再由 RESOURCE_PREFIX 编进可执行
# 文件的资源 —— 不落成外部文件，就不存在「翻译文件忘了一起拷」这种失败。
# （对比偏差 9.6 修的 Qt 自带翻译：那份必须随程序分发，所以只能走 translations/。）
#
# 新增插件时**必须把它的目标加进 SOURCE_TARGETS**，否则新插件的界面文案不会被抽取
# （workflow §4 第 9 步）。改写已有字符串后跑：
#   cmake --build build --target update_translations   刷新 .ts（然后补译文）
qt_add_translations(ToolBox
    TS_FILES translations/toolbox_en.ts
    SOURCE_TARGETS
        ToolBox
        ToolBoxApp
        ToolBoxCore
        base64_tool
        jsonfmt_tool
        jsonfmt_core
        imgwatermark_tool
        imgwatermark_core
        videodl_tool
        videodl_orch
        videodl_core
    RESOURCE_PREFIX "/i18n"
    NO_GENERATE_PLURALS_TS_FILE)

# ── 交付打包 ────────────────────────────────────────────────────────────────
# 交付形态固定为「解压即用」的 zip，交付流程见 docs/workflow.md §9。
#
# 这里不逐个 install 目标，而是整个 bin/<Config>/ 目录一起装。理由是这个目录本身就
# 是运行时形态：主程序、tools/*.dll（插件）、Qt 运行时、translations/、以及
# tools/bin/ 下已下载的外部内核（架构 §8）。副作用是新增插件**不需要动本文件**，
# 与「新增工具不改外壳」这条核心约束保持一致。
install(DIRECTORY "${TOOLBOX_BIN_DIR}/"
        DESTINATION "."
        USE_SOURCE_PERMISSIONS)

# 打错配置、或者忘了跑 deploy，都在这里拦住，别等用户那边才发现。
# 用方括号实参的原样字符串：里面的变量要在安装时求值，不能在配置时被展开。
install(CODE [[
    if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Release")
        message(FATAL_ERROR
            "打包只针对 Release 配置（当前是 ${CMAKE_INSTALL_CONFIG_NAME}）。"
            "Debug 产物还依赖 MSVC 调试运行时，不用于分发。")
    endif()
    if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}/Qt6Core.dll")
        message(FATAL_ERROR
            "输出目录里没有 Qt 运行时。Qt 的 DLL 是 windeployqt 产生的，"
            "先跑 cmake --build build --config Release --target deploy。")
    endif()
]])

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "ToolBox")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "插件式桌面工具箱")
# 版本号不在打包配置里重复写：CPACK_PACKAGE_VERSION 默认取 CMAKE_PROJECT_VERSION，
# 也就是顶层 project(... VERSION ...) 那一个来源（见偏差 9.5）。
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-win64")
# zip 里套一层同名目录，解压不会把文件散在用户选定的目录里。
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)
