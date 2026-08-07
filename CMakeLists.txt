cmake_minimum_required(VERSION 3.20)
project(ECDI VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

# 源文件
file(GLOB_RECURSE SOURCES
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

if(WIN32)
    target_link_libraries(ECDI PRIVATE user32)
endif()
