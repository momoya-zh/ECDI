cmake_minimum_required(VERSION 3.20)
project(ECDI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# 源文件（CONFIGURE_DEPENDS：新增 .cpp 时自动触发重新 configure，避免 GLOB 旧列表漏文件）
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/main.cpp"
)

add_executable(ECDI WIN32 ${SOURCES})

target_include_directories(ECDI
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/include"
)

target_compile_features(ECDI PRIVATE cxx_std_20)

# 宽字符（Unicode）构建：代码全用 *W API（RegisterClassW/LoadCursorW 等），
# 需显式定义 UNICODE/_UNICODE（CMake 无 VS 的 CharacterSet 机制；
# MinGW 的 -municode 虽会隐式定义，但 MSVC 分支不会，故统一在此声明）。
target_compile_definitions(ECDI PRIVATE UNICODE _UNICODE)

if(MINGW)
    target_compile_options(ECDI PRIVATE -municode)
    target_link_options(ECDI PRIVATE -municode -static)
endif()

# UTF-8 源码/字面量：公共 API 用 std::string（UTF-8），MSVC/clang-cl 需显式 /utf-8
# （GCC/MinGW 默认 UTF-8 源码，无需此项）
if(MSVC)
    target_compile_options(ECDI PRIVATE /utf-8)
endif()

if(WIN32)
    target_link_libraries(ECDI PRIVATE user32 imm32)
endif()
