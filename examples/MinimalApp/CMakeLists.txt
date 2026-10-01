cmake_minimum_required(VERSION 3.20)
project(MinimalApp LANGUAGES CXX)

# Phase 10 库化烟雾测试（外部消费者验收）——★ 2026-09-30 起亦随主树构建（双路径）：
#   · 有可用的 ECDI 包（find_package 命中）⇒ 仅通过 find_package(ECDI) + ECDI::ECDI 完成
#     配置/编译/链接/运行——**不手工追加** ECDI 的 include 路径 / lib 路径 /
#     platform libraries（user32/imm32/msimg32 经 ECDITargets.cmake 的
#     PUBLIC INTERFACE 依赖传播）。这是 Phase 10 的原意，保留不变；
#   · 仓库内构建（无包，例如直接 configure 本仓库）⇒ 直接链主树的 ECDI target。
#     形态照 examples/VisualTest/CMakeLists.txt 的既有惯用法（同一份源两路可用）。
# 注意：本程序是 Windows 平台验收程序（Windows-first 框架），不承担跨平台消费者验证。
find_package(ECDI 0.1.0 CONFIG QUIET)

if(NOT ECDI_FOUND)
    # 仓库内构建：直接链 ECDI target（同主树）
    add_executable(MinimalApp WIN32 main.cpp)
    target_link_libraries(MinimalApp PRIVATE ECDI)
else()
    # 外部消费者验收：仅经 find_package(ECDI) + ECDI::ECDI
    add_executable(MinimalApp WIN32 main.cpp)
    target_link_libraries(MinimalApp PRIVATE ECDI::ECDI)
endif()

target_compile_definitions(MinimalApp PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(MinimalApp PRIVATE /utf-8)
endif()
if(MINGW)
    target_compile_options(MinimalApp PRIVATE -municode)
    target_link_options(MinimalApp PRIVATE -municode -static)
endif()
