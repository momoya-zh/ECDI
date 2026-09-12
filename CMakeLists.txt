cmake_minimum_required(VERSION 3.20)
project(ECDI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# 2026-09-03 拆库（demo 独立 examples/ModelProbe/）/ 2026-09-05 库化实施（Phase 10 详设 v1.1）：
#   ECDI        = 框架静态库（include/ECDI = 80 Public 头；9 个 Internal 实现头已下沉
#                 ECDI/src 与同名 cpp 同目录——include/ 内零 Windows.h / 零下沉头引用）
#   modelprobe  = 应用（add_subdirectory(examples/ModelProbe)——链接 ECDI）
#   ecdi_public_header_test = Public Header 自包含测试（每头一 TU，EXCLUDE_FROM_ALL——
#                 --target ecdi_public_header_test 按需验证 80 头独立可编译）
# 依赖方向单向律：src → include/ECDI 允许；include/ECDI → src / Windows.h 禁止
# ═══════════════════════════════════════════════════════════════════════════

# 框架库源（CONFIGURE_DEPENDS：新增 .cpp 自动重新 configure）
file(GLOB_RECURSE FRAMEWORK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src/*.cpp"
)
list(FILTER FRAMEWORK_SOURCES EXCLUDE REGEX "/Demo/|/Tests/")   # demo/测试不在库内

add_library(ECDI STATIC ${FRAMEWORK_SOURCES})

target_include_directories(ECDI
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include>"   # Public 头（构建树内绝对路径）
        "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>"              # 生成头（version.h）
        "$<INSTALL_INTERFACE:include>"                                  # 安装后相对 <prefix>/include
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src"                          # Internal 头（下沉后 src 内模块相对 include）
)

target_compile_features(ECDI PUBLIC cxx_std_20)

# 宽字符构建：库内代码用 *W API 需 UNICODE/_UNICODE（编译期宏——仅库自身，头无 Win32 依赖故 PRIVATE）
target_compile_definitions(ECDI PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ECDI PRIVATE /utf-8)   # UTF-8 源码/字面量（GCC/MinGW 默认 UTF-8 无需）
endif()

if(WIN32)
    # Existing Win32 / rendering dependencies
    # msimg32：Phase 8 AlphaBlend（DrawImage 的 AC_SRC_ALPHA 混合）
    target_link_libraries(ECDI PUBLIC user32 imm32 msimg32)
    # Phase 11 image decoding dependencies（WIC/COM——组内以实际符号收敛，linker 验证收尾）
    target_link_libraries(ECDI PUBLIC windowscodecs ole32 shlwapi)
    # Phase 12 WindowChrome dependencies（R6 DWM——静态库模型下 PUBLIC 传播；
    # 未来切换 SHARED 模型时重新评估传播属性）
    target_link_libraries(ECDI PUBLIC dwmapi)
endif()

# ── R5：version.h 生成（三宏——configure 注入 SemVer）──
set(ECDI_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(ECDI_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(ECDI_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include/ECDI/Core/version.h.in"
    "${GENERATED_INCLUDE_DIR}/ECDI/Core/version.h"
    @ONLY
)

# ── R7：Public Header 自包含测试（每头一 TU——独立编译验证）──
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include/ECDI/*.h"
)
set(PUBLIC_HEADER_TEST_TUS "")
foreach(h ${PUBLIC_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include" "${h}")
    string(REPLACE "/" "_" tu "${rel}")
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/selfcontain/${tu}.cpp")
    file(GENERATE OUTPUT "${tu}" CONTENT "#include \"${rel}\"\r\nint avoid_empty_tu = 0;\r\n")
    list(APPEND PUBLIC_HEADER_TEST_TUS "${tu}")
endforeach()
add_library(ecdi_public_header_test STATIC EXCLUDE_FROM_ALL ${PUBLIC_HEADER_TEST_TUS})
target_link_libraries(ecdi_public_header_test PRIVATE ECDI)

# ── 测试运行入口（Phase 10 挂账兑现——console exe，CLion 直接跑；退出码 = 失败数）──
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src/Tests/*.cpp"
)
# ModelProbeTests 断言 ModelProbePage 行为 → 需链接 demo 实现（examples 内，非框架）
list(APPEND TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/examples/ModelProbe/ModelProbe.cpp")
add_executable(ecdi_tests ${TEST_SOURCES})
target_include_directories(ecdi_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include"            # ECDI 公共头
    "${CMAKE_BINARY_DIR}/generated"                       # version.h
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src"                # 下沉头（RecordingBackend 等）
    "${CMAKE_CURRENT_SOURCE_DIR}/examples/ModelProbe"     # ModelProbe.h
)
target_link_libraries(ecdi_tests PRIVATE ECDI)
target_compile_definitions(ecdi_tests PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(ecdi_tests PRIVATE /utf-8)
endif()

# ── R4：install/export（外部消费唯一正规入口）──
install(TARGETS ECDI EXPORT ECDITargets ARCHIVE DESTINATION lib)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include/ECDI"
        DESTINATION include)                                  # 目录即 API 边界（新增 Public 头零维护）
# ⚠️ 生成头 version.h 不在源 include 树——单独 FILES 安装（DIRECTORY 不覆盖生成物）
install(FILES "${GENERATED_INCLUDE_DIR}/ECDI/Core/version.h"
        DESTINATION include/ECDI/Core)
install(EXPORT ECDITargets NAMESPACE ECDI:: DESTINATION lib/cmake/ECDI)
install(FILES "${CMAKE_BINARY_DIR}/cmake/ECDIConfig.cmake"
              "${CMAKE_BINARY_DIR}/ECDIConfigVersion.cmake"
        DESTINATION lib/cmake/ECDI)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/ECDIConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion)                               # 0.x：0.2.0 可破坏 API——不容忍自动升级
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ECDIConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake/ECDIConfig.cmake"
    INSTALL_DESTINATION lib/cmake/ECDI)

# ── 应用（ModelProbe——第一个 example：链接 ECDI + 自有资源）──
add_subdirectory(examples/ModelProbe)

# ── 视觉验收 GUI（Phase 11——解码图 vs 期望图并排人工核）──
add_subdirectory(examples/VisualTest)
