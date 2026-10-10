# DesktopNest 应用（examples/——2026-10-09 M1 观感里程碑；链接框架静态库 ECDI）
# 目标名 desktopnest（需求 K7）；框架侧零改动——本目录纯消费 ECDI 公共能力

add_executable(desktopnest WIN32
    main.cpp
    BoxModel.cpp
    BoxView.cpp
    BoxWindow.cpp
    MemberListPopup.cpp
    DesktopNest.cpp
)

target_include_directories(desktopnest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"            # 同目录头（BoxModel.h / BoxView.h / …）
)
# ECDI 静态库：PUBLIC include（ECDI/include）+ PUBLIC 链接库自动传递
target_link_libraries(desktopnest PRIVATE ECDI)

# 入口 wWinMain（WINAPI）——需 UNICODE（同 ModelProbe 配置）
target_compile_definitions(desktopnest PRIVATE UNICODE _UNICODE)
if(MSVC)
    target_compile_options(desktopnest PRIVATE /utf-8)   # 源码含中文注释/字面量
endif()
if(MINGW)
    target_compile_options(desktopnest PRIVATE -municode)
    target_link_options(desktopnest PRIVATE -municode -static)   # 单 exe 分发
endif()
