cmake_minimum_required(VERSION 3.20)
project(VisualTest LANGUAGES CXX)

# Phase 11 视觉验收 GUI（人工核）：
# 解码结果 vs 代码构造的期望 Image 并排对照 + 四象限色彩 + JPEG 管线——
# 两图视觉一致 = 解码链（WIC→PBGRA→Image）正确。
find_package(ECDI 0.1.0 CONFIG QUIET)

if(NOT ECDI_FOUND)
    # 仓库内构建：直接链 ECDI target（同主树）
    add_executable(visualtest WIN32 main.cpp)
    target_link_libraries(visualtest PRIVATE ECDI)
else()
    add_executable(visualtest WIN32 main.cpp)
    target_link_libraries(visualtest PRIVATE ECDI::ECDI)
endif()

target_compile_definitions(visualtest PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(visualtest PRIVATE /utf-8)
endif()
if(MINGW)
    target_compile_options(visualtest PRIVATE -municode)
    target_link_options(visualtest PRIVATE -municode -static)
endif()
