# Phase 10 库化初步设计（v1.2 定稿）

> 阶段：初步设计（五阶段法 ②）——**完成**
> 日期：2026-09-04（v1.1 修订 2026-09-05；v1.2 定稿 2026-09-05——评审定性「初步设计完成，可以进入详细设计」）
> 状态：**v1.2 定稿**（评审 无必改项；§10 三开放点按 评审倾向预冻结，详设收口）
> 一句话：把 R1-R8 落成「物理边界 + 构建边界 + 外部消费闭环」的具体方案——**不加任何 GUI 功能**
> 评审定性补充：**Phase 10 = API 边界 + 构建边界 + 消费边界三线闭环**（include/ECDI 78 Public / CMake STATIC+install / MinimalApp find_package）——「从项目到 C++ Library 的边界闭环」
> 前置：phase10-library-requirements.md **v1.1**（外部评审 7 项采纳，决策点全收敛）

---

## 1. 范围映射（R → 设计域）

| 需求 | 设计域 | 本文档节 |
|---|---|---|
| R1 Public API 边界（三层判定 + 下沉 src/） | 头分类草案 + Win32 6 头逐头审查 + 下沉机制 | §2/§3 |
| R2 测试接缝稳定性边界 | 分类时的接缝标注（不动接缝） | §2.4 |
| R3 CMake 主 vcxproj 辅 | 下沉后的 vcxproj 最小同步 | §3.3 |
| R4 install/export + ECDIConfig | install 布局 + config 模板 | §4 |
| R5 版本头三宏 | version.h.in configure | §5 |
| R6 README | 已落地（47abac0）——补 install 段 | §8 |
| R7 Public Header 自包含测试 | CMake 生成 TU 机制 | §6 |
| R8 外部最小消费者 | MinimalApp 独立 project + 验收流程 | §7 |

## 2. R1：Public API 分类草案（三层判定应用）

### 2.1 现状勘察（2026-09-04 源码核实）

- include/ECDI = **89 头**：Animation 4 / Application 1 / Core 10 / EventSystem 24 / Layout 3 / Platform 12 / Render 10 / Theme 10 / Widget 14 / Window 1
- **泄漏实证**：`Render/GDIBackend.h`（公共头）第 7 行直接 `#include <Windows.h>`；Platform/Win32 6 头在公共 include——用户 include 这条链即引入平台实现
- examples/ModelProbe 实际 include **20 个头**（初筛硬依据）：Application/Window/Core×4/Layout×2/Platform 抽象×3/Widget×7/EventSystem×2

### 2.2 三层判定应用（逐模块草案）

| 模块 | 头数 | 判定 | 说明 |
|---|---|---|---|
| Core | 10 | **全 Public** | 基础类型（Rect/Color/Point/Size/Font/Logger/String/UTF8/Image/Assert）——一切消费的底座 |
| EventSystem | 24 | **全 Public** | 用户消费事件（override OnXxx）必需；三层①② |
| Layout | 3 | **全 Public** | 用户组合布局；① |
| Animation | 4 | **全 Public** | 用户自驱动动画的扩展面；② |
| Theme | 10 | **全 Public** | 主题/样式消费与覆盖；①② |
| Widget | 14 | **全 Public** | 直接接触 + 继承扩展；①② |
| Window / Application | 1+1 | **全 Public** | 入口；① |
| Platform（抽象） | 6 | **Public** | ChildProcess/ExecutablePath/PlatformWindow/PlatformApplication/PlatformRenderContext/PlatformWindowHost——跨平台契约（②扩展位） |
| Platform/Win32 | 6 | **Internal 候选** | 逐头审查 §2.3 |
| Render | 10 | **混合** | 见 §2.3 逐头 |

### 2.3 逐头审查（需求 v1.1 §3.2 要求——Win32 6 头 + Render 灰区）

| 头 | 判定 | 依据 |
|---|---|---|
| Platform/Win32/Win32PlatformWindow.h | **Internal** | 装配实现；引用者全在 src（Application/Window 装配） |
| Platform/Win32/Win32WindowClass.h | **Internal** | 窗口类注册实现（含图标加载） |
| Platform/Win32/Win32PlatformApplication.h | **Internal** | 消息泵实现 |
| Platform/Win32/Win32RenderContext.h | **Internal** | GDI 句柄装配细节 |
| Platform/Win32/Win32ChildProcess.h | **Internal** | ChildProcess 工厂的 Win32 实现——用户只见抽象 |
| Platform/Win32/WindowMessageHandler.h | **Internal** | 翻译器契约——src/Window.cpp + src/Tests/EventTests 引用（src 树内可达 ✓） |
| Render/GDIBackend.h | **Internal** | **Windows.h 泄漏实证**；具体后端——用户经 RenderingBackend 抽象消费 |
| Render/GDITextMeasurer.h | **Internal** | 具体测量实现——经 RenderServices 注入 |
| Render/RecordingBackend.h | **Internal** | **测试设施**（R2 同类）——断言后端，仅 src/Tests 消费 |
| Render/RenderServices.h | **Internal（灰区标注）** | 装配 bundle——建 Window 不需要；自定义装配需求出现（二次用例）再升 Public |
| Render/BackendFactory.h | **Internal（灰区标注）** | 同上——工厂属装配层 |

**结论草案**：下沉 **11 头**（Win32 6 + Render 5），Public = 89 − 11 = **78 头**；include 内下沉后应**零 Windows.h、零平台实现头**（§3.4 验收 grep）。

### 2.4 R2 接缝标注（不动接缝）

4 头的 protected 接缝（Button::Displayed、ProgressBar::ResolveAnimationManager、TextBox 多 using、TextWidget::ResolveMeasurer）**随 Public 头保留**——文档标注「implementation seam，不承诺稳定」；Testable* 派生类在 src/Tests（库外）不受影响。

## 3. R1 落地机制：下沉 src/（选 A——需求冻结）

### 3.1 头的位置与引用

- 11 头移入 `src/` **与同名 cpp 同目录**（如 `src/Platform/Win32/Win32PlatformWindow.h`、`src/Render/GDIBackend.h`）——物理内聚
- `target_include_directories(ECDI PRIVATE src)` → src 内引用改相对 include（`#include "Platform/Win32/Win32PlatformWindow.h"`）
- include/（Public）内**必须零引用**下沉头（现状唯一交叉：GDIBackend.h/Win32PlatformWindow.h 自身——下沉后自解；§3.4 grep 验收兜底）

### 3.2 src/Tests 的可达性

- Tests 留 `src/Tests/`（含 TestFramework/RunAllTests/RecordingBackend 消费）——下沉头在同一 src 树，`PRIVATE src` include dir 覆盖 ✓
- **vcxproj 合一工程照常编译 Tests**（引用路径 = src 内，无需改动）

### 3.3 vcxproj 最小同步（R3：CMake 主）

- 11 头移动 → vcxproj 的 ClInclude 路径同步更新（保 VS 编译不断）；此外 vcxproj 不追新功能

### 3.4 依赖方向规则（v1.1 新增——评审：比「有没有 Windows.h」更重要）

**依赖方向单向律**（Public 被 Internal 依赖，反向即边界破洞）：

```text
        ┌──────────────────┐
        │   Public API     │  include/ECDI（78 头）
        └────────┬─────────┘
                 │ 被实现依赖（允许：src include ECDI 抽象）
                 ▼
        ┌──────────────────┐
        │  Internal Impl   │  src/（11 下沉头 + 实现）
        └────────┬─────────┘
                 ▼
             Win32 / GDI
```

| 方向 | 规则 | 验收 |
|---|---|---|
| src → include/ECDI | ✅ 允许（实现依赖抽象） | — |
| **include/ECDI → 下沉头** | ❌ **禁止**（Public 引 Internal = 用户被迫引入实现） | grep `include/` 无 11 下沉头名（§2.3 清单） |
| **include/ECDI → Windows.h** | ❌ 禁止 | grep `include/` 无 `<Windows.h>` |

- 现状唯一交叉（GDIBackend.h/Win32PlatformWindow.h 自身引用）下沉后自解；**两条 grep 列入实施验收清单**（详设/实施时双向检查）
- 违例本质：Public 头 include Internal 头 → 用户 include 该 Public 头即被拖入实现——**方向检查与 Windows.h 检查并列为一等验收项**

## 4. R4：install/export 设计（0.1.0 必做）

```cmake
# CMakeLists.txt（顶层）追加：
install(TARGETS ECDI EXPORT ECDITargets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ECDI DESTINATION include)          # 下沉后 = 纯 Public 头集
install(EXPORT ECDITargets NAMESPACE ECDI:: DESTINATION lib/cmake/ECDI)

# ⚠️ v1.1（外部评审——实现必踩坑预警）：install(DIRECTORY include/ECDI) 只装源码头——
# 生成物 version.h 不在其中，必须单独 install(FILES)，否则消费者 #include <ECDI/Core/version.h> 直接找不到：
install(
    FILES "${CMAKE_BINARY_DIR}/generated/ECDI/Core/version.h"
    DESTINATION include/ECDI/Core
)

# 手写两个 config（模板放 cmake/ 目录）：
#   cmake/ECDIConfig.cmake.in      → include("${CMAKE_CURRENT_LIST_DIR}/ECDITargets.cmake")
#   ECDIConfigVersion.cmake        → write_basic_package_version_file
install(FILES "${PROJECT_BINARY_DIR}/cmake/ECDIConfig.cmake" "${PROJECT_BINARY_DIR}/ECDIConfigVersion.cmake"
        DESTINATION lib/cmake/ECDI)
```

- 消费端：`find_package(ECDI 0.1.0 CONFIG REQUIRED)` → `target_link_libraries(app PRIVATE ECDI::ECDI)`（头/库/传递链接库全带）

## 5. R5：version.h 注入

- 模板：`ECDI/include/ECDI/Core/version.h.in` → configure 生成到 `${CMAKE_BINARY_DIR}/generated/ECDI/Core/version.h`
- 库 target include binary dir（编译消费）；**安装时单独 `install(FILES)` 导出**（§4——DIRECTORY 不覆盖生成物）
- 内容：三宏（MAJOR/MINOR/PATCH——project VERSION 拆分注入）+ `ECDI_VERSION` 组合串宏

### 5.1 ECDIConfigVersion 兼容语义（v1.1 冻结——评审：**ExactVersion**）

- **0.x 阶段 `find_package(ECDI 0.1.0)` 只接受 0.1.0**（Compatibility ExactVersion）
- 依据：0.y.z 语义 = 0.2.0 可能破坏 API——AnyNewerVersion 会放行不兼容替换（编译过的消费者被换掉）
- 未来若明确「0.1.x 兼容」再放宽（详设冻结 ExactVersion）

## 6. R7：Public Header 自包含测试

**机制（CMake 自动生成，无手写维护）**——**每头一 TU（v1.1 评审 确认保留，不改模块聚合）**：

```cmake
file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/ECDI/*.h")
set(SELF_CONTAIN_TUS "")
foreach(h ${PUBLIC_HEADERS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}/include" "${h}")
    string(REPLACE "/" "_" tu "${rel}")
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/selfcontain/${tu}.cpp")
    file(GENERATE OUTPUT "${tu}" CONTENT "#include \"${rel}\"\nint avoid_empty_tu = 0;\n")
    list(APPEND SELF_CONTAIN_TUS "${tu}")
endforeach()
add_library(ecdi_public_header_test STATIC EXCLUDE_FROM_ALL ${SELF_CONTAIN_TUS})
target_link_libraries(ecdi_public_header_test PRIVATE ECDI)
```

- **静态库承载（不链接）**——每头一个 TU 编译 obj：无 main 冲突、独立编译单元验证自包含
- **每头一 TU 的价值（评审）**：某天 Widget.h 巧合依赖了「先 include 别的才编译得过」——独立 TU 立即暴露；模块聚合 TU 会掩盖
- `EXCLUDE_FROM_ALL`——不进默认构建，`--target ecdi_public_header_test` 按需跑（四工具链各验一次）
- target 命名 `ecdi_public_header_test`（v1.1 统一——见名知义）
- 每次头增删自动覆盖（GLOB CONFIGURE_DEPENDS）

## 7. R8：外部最小消费者验收

- **位置**：`examples/MinimalApp/`——**独立 CMake project**（不进主树 add_subdirectory——它验证的是 install 后的 find_package 消费，与主构建模型正交）
- **零依赖 ModelProbe（v1.1 明确）**——只干一件事（库化烟雾测试）：`find_package(ECDI)` → 创建 Application → 创建 Window → 最小 Widget → 运行退出；不 include 任何 ModelProbe 产物
- 结构：

```text
examples/
├── ModelProbe/     完整工具（真实消费参照）
└── MinimalApp/     库化烟雾测试（main.cpp + CMakeLists.txt）
```

- **验收流程（v1.1 修正 exe 名笔误）**：

```bash
cmake -S . -B build && cmake --build build
cmake --install build --prefix /tmp/ecdi-install          # ① 安装
cmake -S examples/MinimalApp -B build-minimal \
       -DCMAKE_PREFIX_PATH=/tmp/ecdi-install              # ② 外部消费 configure
cmake --build build-minimal && ./build-minimal/MinimalApp # ③ 运行（Windows: MinimalApp.exe）
```

- 这条链跑通 = 「外部程序可以依赖 ECDI」成立（需求 v1.1 的库化完成定义）

## 8. R6：README 增补

README.md Build 节补 install + MinimalApp 消费命令（§7 序列）——与 47abac0 已有内容合并

## 9. 影响面

| 区 | 范围 |
|---|---|
| include/ECDI | −11 头（6 Win32 + 5 Render）→ 78 纯 Public |
| src/ | +11 头（与 cpp 同目录）；src 内引用路径改相对；`PRIVATE src` include dir |
| CMakeLists | install/export/config 模板 + version.h configure + selfcontain target |
| cmake/ | 新目录（ECDIConfig.cmake.in 模板） |
| examples/MinimalApp/ | 新建（独立 project） |
| ECDI.vcxproj | ClInclude 11 头路径同步（最小维护） |
| README.md | Build 节补 install/消费命令 |

## 10. 开放决策点（v1.2 全部预冻结——外部评审给出倾向，详设收口）

1. **RenderServices/BackendFactory → 维持 Internal**（评审：按当前模型，用户经 Window，ECDI 内部装配到 GDI——用户不应触碰；自定义装配需求出现再升 Public）
2. **src 引用风格 → `PRIVATE src` + 模块相对路径**（评审：`#include "Platform/Win32/..."` 远优于 `../../` 相对跳）
3. **install 头集过滤 → `install(DIRECTORY include/ECDI)` 全拷保留**（评审：目录结构即 API 边界——新增 Public 头零维护；**不要**为绝对严格改逐头 install(FILES)；唯一特例 = version.h 生成物单独 FILES）
4. ~~selfcontain TU 粒度~~——每头一 TU（v1.1 冻结）
5. ~~ECDIConfigVersion 兼容语义~~——ExactVersion（v1.1 冻结）
6. **新增详设定义（评审）**：Public Header 精确定义 = **「安装包的一部分，且其声明属于 ECDI 对外 API」**——「用户日常是否直接 include」不作判定条件（防误判公共扩展接口为 Internal）

## 11. 修订记录

- v1.2（2026-09-05）**评审定性「初步设计完成，可进入详细设计」——定稿**：无必改项；§10 三开放点按 评审倾向预冻结（RenderServices/BackendFactory 维持 Internal / src 引用风格保留 / install DIRECTORY 全拷保留——目录即边界，与物理边界设计统一）；新增 Public Header 精确定义（安装包一部分 + 声明属对外 API；日常 include 与否不作判定）归详设落笔；安装树结构确认（源码头 + 生成头 + lib + cmake 三来源）。详设阶段工作 = 工程细节收口（11 头移动清单/引用修改列表/Config 模板全文/MinimalApp 文件/验收命令化）。
- v1.1（2026-09-05）**外部评审 5 处全采纳（可进详设）**：① §4 **version.h 单独 install(FILES)**（DIRECTORY 不装生成物——MinimalApp 消费 `#include <ECDI/Core/version.h>` 会找不到——实现必踩坑提前修）；② §5.1 **ECDIConfigVersion 冻结 ExactVersion**（0.x 语义：0.2.0 可破坏 API——AnyNewerVersion 放行不兼容替换）；③ §3.4 **新增依赖方向单向律**（Public → Internal 引用禁止——比 Windows.h 检查更重要；方向 grep 与 Windows.h grep 并列一等验收）；④ §7 MinimalApp **零依赖 ModelProbe 明确**（库化烟雾测试定位）+ 验收命令 exe 名笔误修正（modelprobe → MinimalApp）；⑤ §6 selfcontain **每头一 TU 确认保留** + target 更名 `ecdi_public_header_test`。§10 开放点收敛至 3（TU 粒度/ConfigVersion 语义冻结移除）。
- v1.0（2026-09-04）初步设计初稿：R1 三层判定落成**逐模块分类草案**（89 头 → 78 Public / 11 Internal）+ **Win32 6 头与 Render 灰区逐头审查表**（GDIBackend.h Windows.h 泄漏实证）+ 下沉机制（与 cpp 同目录 + PRIVATE src）+ install/export 布局（ECDIConfig 手写模板）+ version.h 三宏注入 + **selfcontain 静态库承载机制**（file(GENERATE) 生成 TU，EXCLUDE_FROM_ALL）+ **MinimalApp 独立 project 与三步验收流程** + 影响面 + 5 开放决策点。待评审。
