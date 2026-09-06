cmake_minimum_required(VERSION 3.20)
project(MinimalApp LANGUAGES CXX)

# Phase 10 库化烟雾测试（外部消费者验收）：
# 仅通过 find_package(ECDI) + ECDI::ECDI 完成配置/编译/链接/运行——
# 不手工追加 ECDI 的 include 路径 / lib 路径 / platform libraries（user32/imm32/msimg32
# 经 ECDITargets.cmake 的 PUBLIC INTERFACE 依赖传播）。
# 注意：本程序是 Windows 平台验收程序（Windows-first 框架），不承担跨平台消费者验证。
find_package(ECDI 0.1.0 CONFIG REQUIRED)

add_executable(MinimalApp WIN32 main.cpp)
target_link_libraries(MinimalApp PRIVATE ECDI::ECDI)

target_compile_definitions(MinimalApp PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(MinimalApp PRIVATE /utf-8)
endif()
if(MINGW)
    target_compile_options(MinimalApp PRIVATE -municode)
    target_link_options(MinimalApp PRIVATE -municode -static)
endif()
