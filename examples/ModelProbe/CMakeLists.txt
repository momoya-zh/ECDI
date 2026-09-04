# ModelProbe 应用（examples/——2026-09-03 demo 独立文件夹；链接框架静态库 ECDI）
# 框架 → ECDI/（include + src）；ModelProbe = 第一个 example：probe 后端嵌入 + API 图标随本应用

add_executable(modelprobe WIN32
    main.cpp
    ModelProbe.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/ModelProbe_resources.rc"   # configure_file 生成（见下）
)

target_include_directories(modelprobe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"            # 同目录头（ModelProbe.h）
)
# ECDI 静态库：PUBLIC include（ECDI/include）+ PUBLIC 链接库（user32/imm32/msimg32）自动传递
target_link_libraries(modelprobe PRIVATE ECDI)

# P2 资源（probe.exe → RCDATA 101 / app.ico → ICON 102）：rc 从构建目录跑——configure 生成绝对路径版
set(PROBE_EXE_ABSOLUTE "${CMAKE_CURRENT_SOURCE_DIR}/../../probe-go/probe.exe")   # 仓库根 probe-go
set(APP_ICO_ABSOLUTE   "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ModelProbe.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ModelProbe_resources.rc"
    @ONLY
)

# 入口 wWinMain（WINAPI）——需 UNICODE（原顶层 exe 配置随 demo 迁移）
target_compile_definitions(modelprobe PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(modelprobe PRIVATE /utf-8)   # demo 源码含中文注释/字面量
endif()
if(MINGW)
    target_compile_options(modelprobe PRIVATE -municode)
    target_link_options(modelprobe PRIVATE -municode -static)   # 单 exe 分发（同原顶层配置）
endif()
