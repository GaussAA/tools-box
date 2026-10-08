# 注册一个 Qt Test 用例的公共入口。
#
# 放在 cmake/ 而不是 tests/ 里，是因为**测试要跟着被测模块走**
# （docs/architecture.md 的迁移计划 D3）：每个模块在自己的 CMakeLists.txt 里注册
# 自己的用例。函数定义在 tests/ 下的话它是目录局部的，plugins/* 与 app/ 看不见 ——
# 提升成全局函数之后，模块才能自己管自己的测试。
#
#   toolbox_add_test(<name> <被测库>...)
#
# 测试源码固定在 <当前目录>/tests/<name>.cpp —— 也就是注册它的那个模块的 tests/
# 子目录。找不到就配置期失败：静默跳过会让用例悄悄消失，那比报错糟得多。
# （D3 收官后删掉了过渡期的第二条查找路径 <当前目录>/<name>.cpp，
# 见 docs/architecture.md 偏差 9.18 的注销记录。）
#
# 可执行文件一律落在 build/tests/<Config>/，与源码在哪个目录无关：
#   - 不混进要分发的 bin/<Config>/（那是交付目录）；
#   - workflow.md §5 与脚本里的路径约定都指向这里，动它会牵一串。
# 注意 obj 落在源码那一侧（build/plugins/videodl/...），verify_coretest.ps1
# 因此按 build 全树找 tst_*.cpp.obj，而不是只扫 build/tests/。

# Qt 的 bin 目录只解析一次，存进全局属性供后续每次调用复用。测试 exe 与主程序不在
# 同一层，按默认搜索路径找不到 Qt 的 DLL，必须前置进 PATH —— 否则干净构建（没跑过
# deploy）下测试启动即缺 DLL。
function(_toolbox_qt_bin_dir out_var)
  get_property(_cached GLOBAL PROPERTY TOOLBOX_QT_BIN_DIR)
  if(_cached)
    set(${out_var} "${_cached}" PARENT_SCOPE)
    return()
  endif()

  set(_dir "")
  get_target_property(_core_loc Qt6::Core IMPORTED_LOCATION_DEBUG)
  if(NOT _core_loc)
    get_target_property(_core_loc Qt6::Core IMPORTED_LOCATION_RELEASE)
  endif()

  if(_core_loc)
    get_filename_component(_dir "${_core_loc}" DIRECTORY)
  else()
    message(WARNING "定位不到 Qt6 Core 的运行时目录，测试可能因缺少 Qt DLL 而无法启动")
  endif()

  set_property(GLOBAL PROPERTY TOOLBOX_QT_BIN_DIR "${_dir}")
  set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

function(toolbox_add_test name)
  set(_src "${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.cpp")
  if(NOT EXISTS "${_src}")
    message(FATAL_ERROR
        "toolbox_add_test(${name}): 找不到 ${_src}。"
        "用例必须放在注册它的那个模块的 tests/ 子目录下（D3 已收官，无过渡路径）。")
  endif()

  add_executable(${name} "${_src}")
  target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})

  # The module root goes on the test's own include path (PRIVATE): tests may
  # reach their module's internals by design, but since plan B the module
  # libraries advertise no include directories at all -- linking a foreign
  # module's library does NOT bring its headers with it, so a cross-module
  # include cannot compile by accident.
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  set_target_properties(${name} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/$<CONFIG>")

  add_test(NAME ${name} COMMAND ${name})

  _toolbox_qt_bin_dir(_qt_bin)
  if(_qt_bin)
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qt_bin}")
  endif()
endfunction()
