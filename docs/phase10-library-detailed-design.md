# Phase 10 库化详细设计（v1.0）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-05
> 状态：待评审（评审）
> 前置：phase10-library-requirements.md v1.1 / phase10-library-preliminary-design.md **v1.2 定稿**（评审「可进详设」）
> 一句话：把初设的「三边界」落成**精确的移动清单、改写规则、CMake 全文与验收命令**——实施零决策

---

## 1. 分类修正（详设首要产出——初设判断被 Window.h 实证推翻）

**§2.3 勘察发现（详设阶段 grep）**：`include/ECDI/Window/Window.h`（**Public**）第 8-9 行：

```cpp
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Render/BackendFactory.h"
```

根因：Window 公共构造签名含默认参数 `RenderServices services = CreateDefaultRenderServices()`——**按值参数需完整类型 → RenderServices/BackendFactory 事实上已是 Public API**（默认参数暴露在公共签名 = 用户可传自定义装配——恰是「合理扩展」层的合法用例）。

**分类修正（覆盖初设 §2.3 的 Internal 判定）**：

| 头 | 初设判定 | 详设修正 | 依据 |
|---|---|---|---|
| Render/RenderServices.h | Internal（灰区） | **Public** | Window.h 公共签名按值依赖；自定义装配 = ②扩展位 |
| Render/BackendFactory.h | Internal（灰区） | **Public** | CreateDefaultRenderServices 声明在公共签名；被 RenderServices.h include |

**最终分类**：下沉 **9 头**（Win32 6 + GDIBackend/GDITextMeasurer/RecordingBackend），Public = 89 − 9 = **80 头**。

**依赖方向验证（下沉后 include/ 残留引用全部合法）**：

| include/ 现命中 | 处置 |
|---|---|
| Window.h → RenderServices/BackendFactory | ✅ 合法（两者 Public） |
| Win32 三头互引（PlatformWindow/WindowClass/MessageHandler） | 同树下沉自解（Win32 族内部引用） |
| BackendFactory.h → RenderServices.h | ✅ 两者同 Public |
| GDIBackend.h → Windows.h | 下沉自解 |

→ **include/ 内零 Internal 引用**成立，方向单向律（初设 §3.4）可验收。

## 2. 下沉清单（9 头——精确 from → to）

| # | from（include/ECDI/） | to（src/，与同名 cpp 同目录） |
|---|---|---|
| 1 | Platform/Win32/Win32PlatformWindow.h | Platform/Win32/Win32PlatformWindow.h |
| 2 | Platform/Win32/Win32WindowClass.h | Platform/Win32/Win32WindowClass.h |
| 3 | Platform/Win32/Win32PlatformApplication.h | Platform/Win32/Win32PlatformApplication.h |
| 4 | Platform/Win32/Win32RenderContext.h | Platform/Win32/Win32RenderContext.h |
| 5 | Platform/Win32/Win32ChildProcess.h | Platform/Win32/Win32ChildProcess.h |
| 6 | Platform/Win32/WindowMessageHandler.h | Platform/Win32/WindowMessageHandler.h |
| 7 | Render/GDIBackend.h | Render/GDIBackend.h |
| 8 | Render/GDITextMeasurer.h | Render/GDITextMeasurer.h |
| 9 | Render/RecordingBackend.h | Render/RecordingBackend.h |

（src/Platform/Win32/、src/Render/ 已有同名 cpp——头落同目录。）

## 3. 引用改写规则（grep 实证：src/Tests 内 18 文件）

**改写规则（两条全局替换）**：

```text
"ECDI/Platform/Win32/X.h"  →  "Platform/Win32/X.h"    （X ∈ 6 Win32 头）
"ECDI/Render/GDIBackend.h" / "GDITextMeasurer.h" / "RecordingBackend.h"  →  "Render/X.h"
```

**受影响文件清单（grep 实证 18 + 隐含自我引用）**：

| 组 | 文件 |
|---|---|
| Windows cpp（5） | Win32ChildProcess.cpp / Win32PlatformApplication.cpp / Win32PlatformWindow.cpp / Win32WindowClass.cpp / WindowMessageHandler.cpp |
| Render cpp（4） | BackendFactory.cpp / GDIBackend.cpp / GDITextMeasurer.cpp / RecordingBackend.cpp |
| 装配（2） | Application/Application.cpp / Window/Window.cpp |
| Tests（7） | CheckBoxTests / ClipTests / EventTests / ProgressBarTests / RendererTests / TextBoxTests / WidgetTests .cpp |

**CMake 使能**：`target_include_directories(ECDI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src")` → src 内 `#include "Platform/Win32/..."` 可达（评审倾向冻结）。

**RecordingBackend 特例**：其消费者（Tests）经 `PRIVATE src` 可达；但 RecordingBackend.cpp 依赖 TestFramework 头（src/Tests/）——include dir 需含 `ECDI/src/Tests`（或 RecordingBackend.h 不依赖——实施时验证，必要时 `PRIVATE "${src}/Tests"`）。

## 4. install/export 全文（R4——含 version.h 特例处理）

```cmake
# ── install（顶层 CMakeLists.txt 追加）──
install(TARGETS ECDI EXPORT ECDITargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ECDI DESTINATION include)          # 目录即 API 边界（评审 冻结——新增 Public 头零维护）
install(EXPORT ECDITargets NAMESPACE ECDI:: DESTINATION lib/cmake/ECDI)

# version.h 生成物单独装（DIRECTORY 不覆盖生成物——评审 🔴 评审项）
install(FILES "${CMAKE_BINARY_DIR}/generated/ECDI/Core/version.h"
        DESTINATION include/ECDI/Core)

install(FILES "${CMAKE_BINARY_DIR}/cmake/ECDIConfig.cmake"
              "${CMAKE_BINARY_DIR}/ECDIConfigVersion.cmake"
        DESTINATION lib/cmake/ECDI)
```

**cmake/ECDIConfig.cmake.in**（新建）：

```cmake
@PACKAGE_INIT@
include("${CMAKE_CURRENT_LIST_DIR}/ECDITargets.cmake")
check_required_components(ECDI)
```

**ConfigVersion（ExactVersion——需求 v1.1 冻结）**：

```cmake
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/ECDIConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion)          # 0.x：0.2.0 可破坏 API——不容忍自动升级
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ECDIConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/cmake/ECDIConfig.cmake"
    INSTALL_DESTINATION lib/cmake/ECDI)
```

**安装树（验收对照）**：

```text
<prefix>/
├── include/ECDI/…（80 Public 头）+ include/ECDI/Core/version.h（生成）
├── lib/libECDI.a（或 ECDI.lib）
└── lib/cmake/ECDI/{ECDITargets.cmake, ECDIConfig.cmake, ECDIConfigVersion.cmake}
```

## 5. version.h（R5——模板全文）

`ECDI/include/ECDI/Core/version.h.in`（新建——放 include 源树便于随模块认知，.in 后缀不进 GLOB 头集）：

```c
﻿#pragma once
// ECDI 版本宏（configure 生成——勿手改；SemVer 0.y.z：MINOR/PATCH 递增不承诺兼容）
#define ECDI_VERSION_MAJOR @ECDI_VERSION_MAJOR@
#define ECDI_VERSION_MINOR @ECDI_VERSION_MINOR@
#define ECDI_VERSION_PATCH @ECDI_VERSION_PATCH@
#define ECDI_VERSION "@ECDI_VERSION@"
```

顶层 CMakeLists：

```cmake
project(ECDI VERSION 0.1.0 LANGUAGES CXX)     # 1.0 → 0.1.0
set(ECDI_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
... # 拆分注入
set(GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated")
configure_file(ECDI/include/ECDI/Core/version.h.in
               "${GENERATED_INCLUDE_DIR}/ECDI/Core/version.h" @ONLY)
target_include_directories(ECDI PUBLIC "${GENERATED_INCLUDE_DIR}")   # 编译+消费者可达
```

## 6. selfcontain（R7——target 定稿 `ecdi_public_header_test`）

初设 §6 机制冻结不变；两处详设细化：

- **GLOB 排除**：`version.h.in` 非 .h 不入 GLOB ✓；生成物在 binary dir 不在源 include ✓（无自测污染）
- **粒度冻结**：每头一 TU（89→80 Public 头 = 80 TU；`int avoid_empty_tu = 0;` 防 ISO 对空 TU 报警）

## 7. MinimalApp（R8——文件全文）

**examples/MinimalApp/CMakeLists.txt**（独立 project——不进主树）：

```cmake
cmake_minimum_required(VERSION 3.20)
project(MinimalApp LANGUAGES CXX)
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
```

**examples/MinimalApp/main.cpp**（零 ModelProbe 依赖——库化烟雾测试）：

```cpp
#include <ECDI/Application/Application.h>
#include <ECDI/Window/Window.h>
#include <ECDI/Widget/Panel.h>

#include <memory>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    ECDI::Application application;
    ECDI::Window& window = application.Create("Minimal ECDI Consumer", 480, 320);
    auto panel = std::make_unique<ECDI::Panel>();
    panel->SetStretch(1);
    window.GetRootWidget().AddChild(std::move(panel));
    window.GetRootWidget().Arrange();
    window.Show();
    return application.Run();
}
```

**验收流程（三步）**：

```bash
cmake -S . -B build && cmake --build build
cmake --install build --prefix "%TEMP%\ecdi-install"
cmake -S examples/MinimalApp -B build-minimal -DCMAKE_PREFIX_PATH="%TEMP%\ecdi-install"
cmake --build build-minimal && "%TEMP%\ecdi-install\..\build-minimal\MinimalApp.exe"
```

（Windows 下 prefix 用实际目录；四工具链各走一遍 = R8 验收。）

## 8. 验收清单（实施完成判据）

| # | 验收 | 方式 |
|---|---|---|
| 1 | 9 头下沉完成、include/ 只剩 80 头 | git mv 记录 + `find include -name "*.h" \| wc -l` = 80（+version.h.in） |
| 2 | **依赖方向单向律** | `grep -rl "Windows.h" ECDI/include/` = 空；`grep -rl` 9 下沉头名 in `ECDI/include/` = 空 |
| 3 | 四工具链 | CLion：ECDI 库 + modelprobe 编译通过 ×4 |
| 4 | VS 不回归 | ECDI.vcxproj Debug/Release 出 ECDI.exe（图标/probe 嵌入不变） |
| 5 | 151 测试全绿 | src/Tests 照常（vcxproj 编译） |
| 6 | selfcontain | `--target ecdi_public_header_test` ×4 工具链 0 错误 |
| 7 | **install 树结构** | 对照 §4 安装树（80 头 + version.h + lib + cmake×3） |
| 8 | **MinimalApp 全链** | §7 三步 ×4 工具链（MingGW 的 find_package 亦通） |
| 9 | 版本宏 | MinimalApp 可 `#include <ECDI/Core/version.h>` 且三宏 == 0.1.0 |
| 10 | README | install/消费命令段已补（R6 收口） |

## 9. 实施顺序（步骤化）

1. **git mv 9 头**（include → src 同目录）+ gitignore/model-probe-gui 已处置
2. **改写引用**（§3 两规则——18 文件 sed 化逐条验证）
3. **CMake 更新**：PRIVATE src include dir + project VERSION 0.1.0 + version.h configure + install/export 全段 + selfcontain target
4. **cmake/ 模板**（ECDIConfig.cmake.in）+ version.h.in 新建
5. **vcxproj**：ClInclude 9 行路径同步（最小维护）
6. **自验**：验收 1-5（本仓库侧）
7. **install + MinimalApp**：验收 7-9（外部消费侧）
8. **README 增补**（验收 10）
9. **用户四工具链/VS 终验 → tag v0.1.0**（SemVer 打标——R 收口）

## 10. 修订记录

- v1.0（2026-09-05）详细设计初稿：**分类修正**（Window.h 公共签名按值依赖 RenderServices/BackendFactory 实证 → 两头升 Public，下沉 11→9、Public 78→80——依赖方向单向律可验收）+ 9 头精确移动清单 + 18 文件引用改写规则（两条全局替换）+ install/export 全文（version.h 特例 FILES + ExactVersion）+ version.h.in 全文 + selfcontain 定稿（ecdi_public_header_test）+ MinimalApp 双文件全文 + 验收清单 10 项 + 实施顺序 9 步。待评审。
