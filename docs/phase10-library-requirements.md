# Phase 10 库化需求确认（v1.1）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-04（v1.1 修订 2026-09-04——GPT 评审 12 节，7 项修改全采纳）
> 状态：**v1.1 GPT 评审修订完成**（可进初步设计）
> 前置：Phase 9.8 AutoSize ✅（2026-09-02 全链路闭环）；**demo 独立已提前完成**（2026-09-03——examples/ModelProbe/ + CMake ECDI 静态库拆分，用户三项决策：examples 位置 / CMake 先拆库 / 测试回框架侧）
> 目标：**ECDI 0.1.0**——「外部程序可以依赖 ECDI」跑通；**不做 1.0 稳定承诺**（SemVer 0.y.z 可疯狂改）
> 边界一句话（v1.1 升级——GPT 评审 §11「库化完成的定义」）：**Public API 边界 + Build 边界 → install/export → ECDIConfig.cmake → 外部消费者 find_package(ECDI) → ECDI::ECDI → 编译 → 运行——这条链跑通 = 库化完成**。仓库内 examples 链接只是内部消费，不算数。
> 本阶段定位（GPT §12）：**纯粹的工程边界整理 + 构建体系升级 + 外部消费闭环——不加任何 GUI 功能**

---

## 1. 背景与现状事实

**已完成**（本阶段的前置成果，不重做）：

| 项 | 状态 |
|---|---|
| Demo 分离 | ✅ examples/ModelProbe/（main/ModelProbe/资源/CMakeLists 全自含） |
| CMake 静态库拆分 | ✅ `ECDI STATIC`（include PUBLIC + user32/imm32/msimg32 PUBLIC 传递）+ `modelprobe` exe |
| 静态优先不做 DLL | ✅ 既有决策（导出符号/ABI/STL 跨界全不碰） |
| 四工具链 | ✅ MSVC/Clang/ClangCL/MinGW（CLion 驱动，2026-09-03 拆库后 libECDI.a 已验证） |

**现状事实**（源码勘察，2026-09-04）：

| # | 事实 | 位置/数据 |
|---|---|---|
| 1 | 公共头 89 个、10 模块 | `include/ECDI/{Animation,Application,Core,EventSystem,Layout,Platform,Render,Theme,Widget,Window}` |
| 2 | **Win32 平台头 6 个在公共 include 里** | `Platform/Win32/{Win32ChildProcess,Win32PlatformApplication,Win32PlatformWindow,Win32RenderContext,Win32WindowClass,WindowMessageHandler}.h`——含 Windows.h 依赖，库用户 include 即泄漏平台 |
| 3 | Platform 抽象头 6 个（平台无关） | `Platform/{ChildProcess,ExecutablePath,PlatformApplication,PlatformRenderContext,PlatformWindow,PlatformWindowHost}.h` |
| 4 | **测试接缝 4 头在公共 include** | Button（Displayed/OnMouseEnter…）/ProgressBar（ResolveAnimationManager）/TextBox（多 using 暴露）/TextWidget（ResolveMeasurer）——protected 暴露实现细节 |
| 5 | src 无私有实现头（干净） | src 仅 Demo/Tests 侧有 .h；框架实现全在 .cpp——**边界整理成本低** |
| 6 | 版本写死 | `project(ECDI VERSION 1.0)`——应 0.1.0 |
| 7 | VS vcxproj 合一 | ECDI.vcxproj 仍编「框架+Tests+examples 跨目录源」（拆库仅 CMake 侧） |

**动机**：ModelProbe 成为第一个真正的 ECDI 用户后，暴露的核心问题 = 「**用户该 include 什么**」没有边界——89 个头全平铺、平台实现头与抽象头混居、测试接缝混在公共 API 里。库化的本质是**划清这条线**。

---

## 2. 需求条目

### R1：Public API 边界判定与落地（核心——v1.1 判定标准升级）

**三层判定标准（v1.1——GPT 评审：examples include 清单只是初筛依据，不能作最终标准）**：

| 层 | 判定 | 例 |
|---|---|---|
| ① 用户直接接触 | 创建/使用的类型 | Widget/Window/Application/Label/Button/TextBox/Layout/Event |
| ② 用户合理扩展 | 继承/重写虚方法所必需（**即使 examples 没用到**） | 自定义 Widget 继承 Widget → `OnPaint(PaintContext&)` → **PaintContext 及 Render 必要类型必须 Public** |
| ③ 框架实现细节 | 装配层/平台实现——用户经高层 API 间接使用 | Win32PlatformWindow/Win32WindowClass/WindowMessageHandler |

- **冻结措辞**：examples 实际 include 清单 = Public API **初筛的硬依据**；最终以「**用户创建、使用、继承、扩展 ECDI 所必需的类型**」为准
- 落地形态：**真私有头下沉 `src/`（选 A——v1.1 冻结，见 §3.2）**——include 目录只留 Public，物理边界即发布边界

### R2：测试接缝的 API 稳定性边界（v1.1 改名重写——GPT：原「头不入公共 API」概念混淆）

**概念修正**：Button/ProgressBar/TextBox/TextWidget 本身是 Public class（头不能下沉）；问题不是「头是否 Public」，而是 **protected virtual 接缝本身是否稳定 API**：

```text
TextWidget.h      → Public（类是公共 API）
ResolveMeasurer() → protected implementation seam → 0.1.0 暂不承诺稳定
```

- **冻结措辞**：测试接缝可存在于 Public class 的 protected 区域，但**不承诺其为稳定扩展 API**（文档标注；Testable* 派生类全在库外测试侧）
- 1.0 冻结 API 时再重新审视接缝去留

### R3：CMake 主、vcxproj 辅（v1.1 冻结——GPT：库化的 install/export/find_package 全是 CMake 天然领域）

- **CMake = 唯一需要完整维护的构建定义**（ECDI target / ECDI::ECDI / install / export）
- **vcxproj 保留不删**：VS 调试 / Windows 开发体验 / 历史工程兼容——但不再承诺与 CMake 同步完整（新增源文件以 CMake 为准）
- 消除双系统漂移：「CMake 加了源文件、忘 vcxproj、两编译内容不一样」从根上断掉

### R4：静态库构建完善 + install/export（v1.1 升级——0.1.0 必做）

- **ECDIConfig.cmake + `find_package(ECDI CONFIG REQUIRED)` + `ECDI::ECDI`**——外部消费的唯一正规入口
- 闭环：`cmake build → cmake install → ECDIConfig.cmake → find_package → ECDI::ECDI → 编译 → 运行`
- 区分（GPT §6）：CMake install/export = **库本身的消费边界**（本阶段做）；vcpkg/Conan = **生态分发**（不做）

### R5：版本注入（v1.1 冻结——B 但最小化）

- `project(ECDI VERSION 0.1.0)` + configure 生成 `ECDI/version.h`——**只做三宏**：

```cpp
#define ECDI_VERSION_MAJOR 0
#define ECDI_VERSION_MINOR 1
#define ECDI_VERSION_PATCH 0
```

- 不搞 VERSION_CHECK()/API_VERSION()/ABI_VERSION()（0.1.0 无 ABI 概念）

### R6：README + 最小使用示例

README.md（仓库根）：项目定位/构建方式/最小示例（引用 examples/ModelProbe 为完整用例）——首个面向外部的门面

### R7：Public Header 自包含测试（v1.1 新增——GPT 评审 §9）

**每个 Public header 建最小 TU 单独编译**（`#include <ECDI/Widget/Label.h>` + `int main(){}`）——杜绝「单独 include 编不过、恰好先 include 了别的才过」的自包含问题：

- Public header 真正成为 Public header 的机械化验证
- CMake 侧生成（每个头一个 TU 或聚合分批）

### R8：外部最小消费者验收（v1.1 新增——GPT 评审 §10，本阶段最高价值验收）

**examples/MinimalApp/**（或独立验收目录）：比 ModelProbe 更重要的验收——证明**外部项目**能依赖：

```cmake
find_package(ECDI CONFIG REQUIRED)
target_link_libraries(MinimalApp PRIVATE ECDI::ECDI)
```

- 最小 main（include ECDI 头 + 创建窗口 + 退出）
- 不需要做成正式 example——验收载体
- 这条链跑通 = 「外部程序可以依赖 ECDI」成立

---

## 3. 决策点（v1.1 全部收敛——GPT 评审后无开放项，仅列冻结结论）

| # | 决策 | 冻结结论 | 依据 |
|---|---|---|---|
| 3.1 | Public API 判定标准 | **三层判定**（R1——直接接触/合理扩展/实现细节）；examples 清单仅初筛 | GPT：用到了≠Public、没用≠Internal |
| 3.2 | 下沉形态 | **选 A：真私有头移 `src/`**——不搞 `include/ECDI/detail/`（detail 仍是「请勿触碰」，src 是「根本不发布」——物理边界才是真边界）；**Win32 6 头 = 候选下沉，逐头审查**（不写死全下沉——Win32RenderContext 类头存在高级扩展位可能），Platform 抽象 6 头留 Public | src 无私有实现头 = 整理成本低（一次性窗口） |
| 3.3 | 测试接缝 | **保留 protected + 文档标注「不承诺稳定」**（R2——接缝是测试必需，动它伤 151 测试；1.0 再审） | 概念修正后接缝与头下沉正交 |
| 3.4 | install/export | **0.1.0 必做**（R4——ECDIConfig + find_package 是「可依赖」的定义；仓库内消费只是内部闭环） | GPT：顺手做正当时 |
| 3.5 | 版本头 | **做——只三宏**（R5） | 成本极低、日志/问题反馈受益 |
| 3.6 | CMake 主次 | **CMake 主、vcxproj 辅、不删 vcxproj**（R3） | install/export/find_package 全是 CMake 领域；vcxproj 留调试体验 |

---

## 4. 非目标（YAGNI 明确不做——v1.1 强化：本阶段不加任何 GUI 功能）

- **不加任何 GUI 功能**（本阶段 = 纯工程边界整理 + 构建体系升级 + 外部消费闭环——GPT 定调）
- **DLL / 导出符号 / __declspec**（既有决策——静态优先）
- **1.0 稳定承诺 / API 向后兼容约束**（SemVer 0.y.z 语义）
- WindowChrome / GPU Backend / 新控件（能力阶段后置——Phase 10 后路线）
- 预编译产物分发（GitHub Release 等——0.1.0 源码形态）
- **vcpkg / Conan / system package**（生态分发 ≠ CMake 消费边界——前者不做，后者 R4 做）
- CollapsiblePanelDemo/Showcase 的 examples 化（框架演示暂留 src/Demo——有二次用例再动）
- 版本宏复杂化（VERSION_CHECK/ABI_VERSION——只三宏）

---

## 5. 测试/验证方向（需求层——v1.1 加两项核心验收）

- **外部消费者验收（R8——最高价值）**：MinimalApp `find_package(ECDI)` → `ECDI::ECDI` → 编译 → 运行 全链通
- **Public Header 自包含测试（R7）**：每个 Public header 最小 TU 单独编译通过
- 四工具链全绿：ECDI 库 + modelprobe 各工具链编译通过
- **VS 合一工程不回归**：ECDI.vcxproj Debug/Release 照常出 ECDI.exe（含图标/probe 嵌入）
- **151 测试全绿**：Tests 留框架侧（vcxproj 编译；CMake 侧 tests target 挂账）
- **消费验证**：examples/ModelProbe 全流程（查询/测试/图标/probe 释放）
- **边界对照表**：R1 落地后「用户视角 include 清单」与 Public 集合一致（文档化）

---

## 6. 影响面（预判——v1.1 更新）

| 区 | 范围 |
|---|---|
| include/ECDI | **Win32 6 头等下沉候选逐头审查 → 移 src/**（物理边界；include 只留 Public） |
| src/ | 承接下沉头（含 include 路径调整）；新增 install/export 相关 |
| CMakeLists | ECDI target 改写（PUBLIC 头过滤）+ install/ECDIConfig + version.h configure |
| **examples/MinimalApp/** | 新建（R8 外部消费者验收载体） |
| README.md | 新建（仓库根首个） |
| ECDI.vcxproj | 辅助定位（不删不主推；源引用已 examples 化） |
| examples/ModelProbe | 消费验证载体（本身零改动预期） |
| docs/ | 本文档 + 后续初设/详设 |

---

## 7. 与既有决策的关系

1. **demo 独立（2026-09-03）** = 本阶段 R 清单「Demo 分离」项提前完成——examples/ModelProbe 为 R1 判定标准提供硬依据（用户 include 清单）
2. **静态优先不做 DLL**（既有）= §4 非目标第一条的依据
3. **测试接缝不入公共 API**（2026-09-02 冻结）= R2 的依据——本阶段只做「文档标注」，不动接缝
4. **CMake 主次**（2026-09-02 挂起）= R3——本阶段必须冻结
5. **1.0 推后**（2026-09-02 GPT 规划采纳）= §4 非目标——WindowChrome 等能力后置

---

## 8. 修订记录

- v1.1（2026-09-04）**GPT 评审 12 节——7 项修改全采纳**：① R1 判定标准升级**三层**（直接接触/合理扩展/实现细节——examples 清单降为初筛依据；「用户创建、使用、继承、扩展所必需」为最终标准）；② R2 改名「**测试接缝的 API 稳定性边界**」（Public class 的 protected 接缝存在但不承诺稳定——「头不入公共 API」概念混淆修正）；③ 下沉形态**冻结选 A 移 src/**（detail/ 只是「请勿触碰」不是「不发布」——物理边界才是真边界；src 无私有头 = 一次性低成本窗口；Win32 6 头**候选下沉逐头审查**不写死）；④ R4 升级 **0.1.0 必做 install/export**（ECDIConfig + find_package + ECDI::ECDI——仓库内消费 ≠ 外部可依赖）；⑤ R3 **冻结 CMake 主 vcxproj 辅不删**；⑥ **新增 R7 Public Header 自包含测试**（每头最小 TU）；⑦ **新增 R8 外部最小消费者验收**（MinimalApp find_package 全链——本阶段最高价值验收）；⑧ R5 冻结版本头三宏；⑨ 非目标强化「不加任何 GUI 功能」+ vcpkg/Conan 维持 + 版本宏复杂化不做；⑩ 边界一句话升级「库化完成的定义」（find_package 全链）。§3 六决策点全部收敛（无开放项）。
- v1.0（2026-09-04）需求确认初稿：现状勘察（89 头/Win32 6 头/接缝 4 头/src 无私有实现头/版本 1.0 写死）→ R1-R6（API 边界判定/接缝约束/CMake 主次/构建完善/版本注入/README）+ §3 六决策点（判定标准精确化/下沉形态 A-C/接缝 A-B/install 范围/版本头/主次 A-C）+ §4 非目标（DLL/1.0/能力后置）+ §5 验证方向（四工具链/VS 不回归/151/消费验证/边界对照）+ §7 既有决策对齐。待评审。
