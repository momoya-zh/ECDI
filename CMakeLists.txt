cmake_minimum_required(VERSION 3.20)
project(ECDI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# ═══════════════════════════════════════════════════════════════════════════
# 2026-09-03 拆库（demo 独立 examples/ModelProbe/——Phase 10 前奏）：
#   ECDI        = 框架静态库（ECDI/src 排除 Demo//Tests/——Demo 残留框架演示后续归档、
#                 Tests 回框架侧跑法 Phase 10 定；include PUBLIC 透传给消费者）
#   modelprobe  = 应用（add_subdirectory——main/ModelProbe/资源全在 examples/ModelProbe/）
# 静态库依赖（user32/imm32/msimg32）PUBLIC——静态库消费者（modelprobe）链接时自动带上
# ═══════════════════════════════════════════════════════════════════════════

# 框架库源（CONFIGURE_DEPENDS：新增 .cpp 自动重新 configure）
file(GLOB_RECURSE FRAMEWORK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src/*.cpp"
)
list(FILTER FRAMEWORK_SOURCES EXCLUDE REGEX "/Demo/|/Tests/")   # demo/测试不在库内

add_library(ECDI STATIC ${FRAMEWORK_SOURCES})

target_include_directories(ECDI PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include"   # PUBLIC——消费者 include <ECDI/...> 自动可达
)

target_compile_features(ECDI PUBLIC cxx_std_20)

# 宽字符构建：库内代码用 *W API 需 UNICODE/_UNICODE（编译期宏——仅库自身，头无 Win32 依赖故 PRIVATE）
target_compile_definitions(ECDI PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ECDI PRIVATE /utf-8)   # UTF-8 源码/字面量（GCC/MinGW 默认 UTF-8 无需）
endif()

if(WIN32)
    # 链接库 PUBLIC：静态库自身不链接——依赖传递给最终 exe（modelprobe）
    # msimg32：Phase 8 AlphaBlend（DrawImage 的 AC_SRC_ALPHA 混合）
    target_link_libraries(ECDI PUBLIC user32 imm32 msimg32)
endif()

# 应用（ModelProbe——第一个 example：链接 ECDI + 自有资源）
add_subdirectory(examples/ModelProbe)
