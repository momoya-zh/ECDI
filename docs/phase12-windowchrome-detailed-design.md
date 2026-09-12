# Phase 12 WindowChrome 详细设计（v1.5）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-12（v1.2 内部复核、v1.3 AI 核验补正、v1.4 实施期回写 同日）
> 状态：**✅ 已实施（2026-09-12）——MinGW g++ 构建通过 + `ecdi_tests` 183/183 通过**（174 既有零回归 + 9 新增）；**其中一次为带 `-D_DEBUG` 的构建**（`FRAMEWORK_ASSERT` 真正生效，见 v1.5）——不带 `_DEBUG` 的构建下该断言层被编译为空操作，不能作为断言层证据；MSVC / Clang / ClangCL 待用户在 VS / CLion 确认；手测矩阵与 Desktop spike 待用户执行
> 前置：`phase12-windowchrome-preliminary-design.md` **v1.3**（三轮外部评审 **PASS**——可进详设）/ `phase12-windowchrome-requirements.md` **v1.2**
> 一句话：把初设 v1.3 的「架构方向 + 代码骨架」落成**可直接实施的规格**——初设 §8 的 **9** 个开放决策点全部形成实施决策 / `Win32PlatformWindow` 全部改动的可编译全文（含三处初设勘误 + 一处本文档自查勘误）/ 4 新头定稿 / dwmapi 双构建系统落地 / 测试逐用例断言 / 实施顺序与验收
> 定位：**本文档不含架构讨论**（初设 v1.1–v1.3 三轮已定案）；只做「编译级契约 + 平台实现细节写死」
> **v1.5 修订（实施后缺陷修复——测试替身所有权越界，由 MSVC 构建暴露）**：用户运行 MSVC 构建的 `ecdi_tests` 触发 `Application.cpp:92` 断言 （`Expression: it != m_windows.end()`，`OnWindowDestroyed`）。**根因**：§5.2 的 `TestWindow` 用 `std::make_unique<Window>(&a, ...)` **直接构造**窗口，绕过了 `Application::Create()` —— 而登记 `Application::m_windows` 只发生在此。窗口销毁链为 `~Window → Release() → DestroyWindow()`（**同步** `WM_DESTROY`）`→ WindowDestroyedEvent → Application::OnWindowDestroyed`，在册中找不到该窗口 ⇒ 断言。**为何此前未暴露**：`FRAMEWORK_ASSERT` 只在 `#ifdef _DEBUG` 下存在（`Core/ECDIAssert.h:22`），而 CMake 的 MinGW 构建不含 `_DEBUG` ⇒ 断言层被编译为 `((void)0)`，此前「183/183 全绿」**未执行过任何框架断言**（MSVC 构建 `build.ninja` 含 `-D_DEBUG`，故只有它暴露）。**修订**：`TestWindow` 改持**非拥有** `Window*`（`window = &a.Create(...)`），析构只做 `window->Release()`（销毁 HWND，对象由 Application 回收）。⚠️ **禁止**用 `unique_ptr` 持有：`m_deferredDestroy` 会对同一对象二次析构（double delete）。**验证**：带 `-DCMAKE_CXX_FLAGS=-D_DEBUG` 的 MinGW 构建 → `ecdi_tests` **183 passed, 0 failed，退出码 0**（断言层真正生效，且无其它被掩盖的断言缺陷）。**遗留（另案）**：`Application` 与 `Window` 的**所有权契约本身仍不对称**（public 构造器 + 「必在册」断言）——已作为独立方案上报评审，本版不动框架代码。
> **v1.4 修订（实施期回写——4 处详设缺口，全部由构建/测试实测暴露）**：① **D-DWM-1 前提被推翻**——`DWMWA_WINDOW_CORNER_PREFERENCE` / `DWMWCP_*` 是**枚举成员而非宏**（MinGW-w64 `dwmapi.h` 实据），`#ifndef <宏>` 守卫恒真 ⇒ 原兜底块必与既有定义冲突（实测 `error: using typedef-name ... after 'enum'` / `conflicts with a previous declaration`）⇒ **定稿 = 零兜底**；② **`NCCALCSIZEPARAMS` → `NCCALCSIZE_PARAMS`**（§3.4 类型名笔误，实测 `does not name a type`）；③ **影响面遗漏：2 个测试替身**——`AnimationTests.cpp` / `ProgressBarTests.cpp` 的 `TestPlatformWindow` 假实现因新增纯虚变抽象类，**不补 override 即编译失败**（§6 清单已补 2 行）；④ **§5.1 include 缺口**——`TestWindow::Handle()` 需 `PlatformWindow` **完整类型**（`Window.h` 仅前置声明）+ 文件需 `using namespace ECDI;`（测试文件约定）。**另记**：`Show()` 实际写法为 `if (m_hwnd != nullptr)`（详设片段作 `if (m_hwnd)`）——实现按源码原样、仅插入 `m_shown = true;` 一行。**实测结果**：`ecdi_tests` **183 passed, 0 failed, 183 total**。
> **v1.3 修订（AI 核验补正——1 处计数不一致 + 3 处清理，均不影响设计）**：① **§6 `.cpp` 行计数不一致**——v1.2 只同步了 `.h` 行（「2→3 辅助声明」），漏改 `.cpp` 行；该行仍写「8 方法实现」，而 §3 实为 **10**（7 override + 3 私有辅助）⇒ 已改为「**10 方法实现（7 override + 3 私有辅助）**」；② §5.2 `TestWindow::app` 为**死成员**（从未被读——`Window` 自身持有 `Application*`）⇒ 删除；③ `~TestWindow(){ window->Release(); }` **冗余**（`Window::~Window()` 已调 `Release()`，`Window.cpp:120-124` 实核；且 `unique_ptr` 随后析构 `Window`）⇒ 改为说明性注释；④ §5.1 include 清单补 2 个**直连** include（`ECDI/Window/WindowState.h`、`ECDI/EventSystem/EventRouter.h`）——原靠传递可用，按本文档「include 依赖核对」纪律改为直连。**另记（未改，留待实施期确认）**：§5.2 注释「800×600 客户区基准」——Normal 模式下客户区小于整窗（仅 Borderless 相等），措辞宜为「800×600 窗口基准」；因不属本次授权清理项，暂不动。
> **v1.2 修订（内部复核——2 P1 + 4 P2 全采纳，修订完成）**：① **P1 勘误④**——§3.12 `DipToPixels` 函数闭合后**残留 5 行旧版尾巴**（v1.0→v1.1 升 `long long` 时清理遗漏，照抄即编译不过，且内容与 v1.1 P2 修复自相矛盾）⇒ 删除；② **P1 规格缺口**——§5.2 `TestWindow` **无句柄访问器**，而 §5.3 全部窗口类用例都需要 HWND（`GetWindowRect`/`SendMessageW`/`IsWindowVisible`）⇒ 补 `HWND Handle()` 全文（`GetPlatformWindow()` → `GetRenderContext()` → `static_cast<Win32RenderContext>`）+ §5.1 include；③ P2 内部不一致 ×4：§1.1 表 D-DPI-1 来源回写为 `GetDeviceCaps`（原仍写 `GetDpiForWindow`）/ §1.1 表 D-TST-1 窗口策略改为「每用例独立」（原文「T2/T5/T6 共享」与 §5.3 配置冲突）/ §3.2 补 `ApplyDwmEnhancements` 声明（「2 辅助」→「3 辅助」）/ §6 清单同步（「8 自动用例」→**9**、「+2 辅助声明」→**+3**）；④ 排版与表述修正：§3.6 `WM_NCACTIVATE` 括号重排（逻辑未变，防误改）/ §5.3 标题「10 条」→「11 行 = 9 自动 + 2 非自动」/ T7 行与节末注的捕获方式表述统一 / §8 A7「六场景」→「七场景」；⑤ **新增 A10 验收项**——§3 代码块「include 依赖 + API 签名 + **函数体完整性**」三级静态核对（勘误④制度化）。
> **v1.1 修订（外部评审——2 P1 + 4 P2 全采纳，Conditional PASS → PASS）**：① **P1 D-CHROME-1**——`SetChromeMode` 改「**一次确定**」语义（v1.0 的幂等分支存在状态失同步漏洞：Borderless→Normal 不派发 `SWP_FRAMECHANGED`，且 `m_borderlessApplied` 残留会把第三次调用挡在幂等分支外；采纳评审方案 A 而非配置期动态重配置——YAGNI）；② **P1** `WM_NCACTIVATE` 最小化态放行 `DefWindowProc`（MSDN 注明的特殊路径）；③ P2 `DipToPixels` 中间量升 `long long`（int 溢出防护）+ 「零改动」措辞收紧为「仅替换 DPI 来源」；④ P2 T3 选点动态化（`GetSystemMetrics` 合成，去掉 20px 硬编码）+ 只断言 `resultA != resultB`；⑤ P2 手测矩阵 +Bottom×Maximize/Restore；⑥ **新增 T0**（ChromeMode 一次确定的回归锚）⇒ 自动用例 8→9，预期 **183/183**。
> ⚠️ **已核实源码事实**（本详设全部实现规格均以真实源码为锚）：`Win32PlatformWindow.cpp` 现无 `Logger.h`/`windowsx.h` include；`Logger::Log` 第二参为 `std::wstring_view`（宽字面量）；`HandleMessage` 结构 = 状态同步 switch → IME 吞字符检查 → 翻译器 → `DefWindowProcW`；事件经 `m_host.OnEvent(const Event&)` 派发；`Application` 默认构造轻量（不调 `Run()` 无消息循环，测试可直接构造）

---

## 1. 范围映射与待定项收口

### 1.1 初设 §8 的 9 个开放决策点（全部形成实施决策）

| # | 初设待定项 | **详设定稿** | 决策编号 | 本文档节 |
|---|---|---|---|---|
| 1 | 最大化是否需要补偿、补偿量 | **不引入任何补偿**——`rcClient = mi.rcWork` 即满足 `ClientRectScreen ⊆ rcWork` 不变量。**标定流程**：T4 自动断言（`⊆`）+ 真机人工观察；若 T4 在某 Windows 版本失败 → **另起 R 立项**（回初设修订补偿细则），本阶段不静默加补偿 | D-COMP-1 | §3.4 |
| 2 | 最大化态 NCHITTEST 是否返回 resize 命中 | **已在初设落码**（`resizable = !IsZoomed(hwnd)` 门控四边四角、caption 保留）——详设全文复核采纳，零改动 | D-HIT-1 | §3.5 |
| 3 | `dwmapi` / Win11 常量在 MinGW 的可用性 | ⚠️ **实现期修正（v1.4）**：原「`#ifndef` 常量兜底」的前提**不成立**——`DWMWA_WINDOW_CORNER_PREFERENCE` 与 `DWMWCP_*` 在 MinGW-w64 与 MSVC SDK 中都是 **枚举成员/枚举定义，不是宏**，故 `#ifndef <宏>` 守卫**恒真**、兜底块必然与既有定义冲突（MinGW 实测编译错误）。**定稿 = 零兜底**，直接用头里的定义；`dwmapi.h` 头本体（`DwmExtendFrameIntoClientArea`）mingw-w64 自带；链接失败 = 构建期错误（不做条件编译） | D-DWM-1 | §3.1 / §3.11 |
| 4 | `Desktop` spike 结果 | **本详设不含 spike 结果**（需真机）——§5.5 给出 spike 程序**全文**；spike 与实现**并行**（不阻塞其它 R）；结果落 `desktopnest-roadmap.md` | D-SPK-1 | §5.5 |
| 5 | 测试窗口创建/销毁合并策略 | **每用例独立窗口**（v1.2 修订——见下）——T1/T3 为对照类（状态污染禁止）；T0 需干净的一次确定起点；T2/T5/T6 的 caption/inset 配置**互不相同**（T2=32/8、T5=32/0、T6=0/0 与 -5/-8），共享窗口会引入**用例顺序依赖**（后跑的用例改配置即让先跑的期望失效），故各自独立；T4/T7/T7a 涉及 Show/状态变迁，独立。**T0–T3/T5/T6 不 Show**（创建即有效——`WM_NCCALCSIZE` 在 `CreateWindowExW` 期已走完，`SendMessageW(WM_NCHITTEST)` 不要求可见）→ **零闪窗**（优于初设"右下角显示"方案——那只为渲染测试的 DC 而设） | D-TST-1 | §5.2 |
| 6 | DWM margin 值 | **1px 定稿起步**（四边 1,1,1,1）；真机视觉标定方法见 §3.11——观察项：阴影是否保留 / 客户区顶部是否出现玻璃条 / Win10 与 Win11 各一遍 | D-DWM-2 | §3.11 |
| 7 | `PlatformWindow.h` 类注释「能力扩展惯例」措辞 | **定稿全文见 §2.5**——三步模板：「加能力 = ① PlatformWindow 加 virtual + ② Window 加公共方法透传 + ③ 平台消息在 `HandleMessage` 内消化（不进翻译器）」 | D-SEAM-1 | §2.5 |
| 8 | DIP → 物理像素换算点与 DPI 来源（v1.3 新增） | **D-DPI-1**：来源 = **`GetDeviceCaps(hdc, LOGPIXELSX)`（窗口 DC）**——⚠️ **相对初设倾向的偏离**（初设倾向 `GetDpiForWindow`，因 `_WIN32_WINNT` 宏门槛在四工具链默认值不一而改用 `GetDeviceCaps`；**偏离理由与未来替换路径见 §3.12**）；语义 = **窗口所在显示器**（非鼠标所在显示器）；换算点 = `WM_NCHITTEST` 内 inset/caption 消费处（私有静态辅助 `DipToPixels`）；公式 = 整数 half-up `(dip * dpi + 48) / 96`（中间量 `long long` 防溢出）；当前项目无 PerMonitorV2 声明 → DPI 恒 96 → 现状 1:1，但契约已就位 | D-DPI-1 | §3.5 / §3.12 |
| 9 | `Desktop` 语义状态 ≠ 实现路径（v1.3 新增） | **D-DESK-1**：契约写入 `WindowLayer.h` 头注释（§2.2）+ `SetWindowLayer` 降级分支注释（§3.9）；**本阶段不新增 `GetWindowLayer()`**（YAGNI——无消费者；未来新增时契约已就位：返回**请求语义** `Desktop`，而非实现路径 `Bottom`） | D-DESK-1 | §2.2 / §3.9 |

### 1.2 初设代码的三处勘误（⚠️「照抄初设即编译不过」——本详设已全部修正）

Phase 8.6 详设曾发生同类问题（v1.2 修订记录勘误①）；本轮在撰写详设时以**源码事实**复核初设全部代码片段，发现三处：

| # | 初设写法 | 源码事实 | 详设修正 |
|---|---|---|---|
| ① | 所有日志为窄字面量 `"WindowChrome: ..."` | `Core/Logger.h:28`——`Logger::Log(LogLevel, std::wstring_view)`，**窄字面量无法构造 `wstring_view`**（编译错误） | 全部改为宽字面量 `L"..."`（§3 各实现全文） |
| ② | `WM_NCHITTEST` 代码用 `GET_X_LPARAM(lParam)` | `Win32PlatformWindow.cpp` 现 include 清单无 `<windowsx.h>`（`GET_X_LPARAM` 定义于此）——直接用会编译错误 | §3.1 include 策略补 `<windowsx.h>` |
| ③ | 各实现直接调 `Logger::Log` | `Win32PlatformWindow.cpp` 现无 `ECDI/Core/Logger.h` include（构造失败走异常路径，从未用过 Logger） | §3.1 include 策略补 `ECDI/Core/Logger.h` |

> **纪律重申**：这是「详设/初设示例代码未经编译验证」缺陷的第二次出现。本轮起执行——**凡详设代码片段，至少完成「include 依赖 + API 签名」两级静态核对**（本详设 §3 全部代码已按 `Win32PlatformWindow.cpp/.h`、`Core/Logger.h`、`PlatformWindowHost.h` 实文核对）。

### 1.3 与初设的继承关系声明

- **§2 的 4 个新头 + 3 处头修改**：初设 v1.3 §2 已给全文且经三轮评审——详设**零改动采纳**（逐头声明，不重复贴全文，避免双源漂移）；仅 `WindowLayer.h` 补 D-DESK-1 契约注释一行（§2.2 列出差异）。
- **§3 的平台实现**：初设已有骨架（§3.2–§3.9），详设给出**编译级全文**（补齐勘误、include、成员定义、事件派发路径等初设未落的内容）。
- **初设 v1.3 §2.4 的重复 `void Restore();` 行**：撰写本详设时发现并已修复（初设文档同步修正，不占版本号——机械重复非设计内容）。

---

## 2. Public 头定稿（4 新头 + 5 处修改）

> Public 头 81 → **85**。每头一 TU 的自包含测试（`ecdi_public_header_test`）经 CMake glob **自动覆盖新头**（§4），零维护。

### 2.1 新增：`include/ECDI/Window/ChromeMode.h`（82 头）

**定稿 = 初设 v1.3 §2.1 全文，零改动。** `ChromeMode{ Normal=0, Borderless }`——纯词汇枚举，零 include。

### 2.2 新增：`include/ECDI/Window/WindowLayer.h`（83 头）

**定稿 = 初设 v1.3 §2.2 全文，加一行契约注释（D-DESK-1 落地）**——在枚举 `Desktop` 值的 doc 注释末尾追加：

```cpp
	/// @brief 桌面驻留层：被应用窗口覆盖，且 **Win+D 后仍保持可见**
	/// @details ⚠️ 实现路线待 spike 验证；spike 未通过前此档位不承诺可用——
	/// 调用后退化为 Bottom 语义并记 Warning 日志。
	/// **API 承诺与当前平台能力刻意解耦**：枚举值保留，未来 Windows 版本可行时
	/// 只需替换实现，公共 API 零变更。
	/// ★ 语义状态 ≠ 实现路径（详设 D-DESK-1）：降级的是「实现如何执行」（当前按
	/// Bottom 路径执行），**不是「状态是什么」**——本档位的请求语义恒为 Desktop，
	/// 未来若新增查询 API（如 GetWindowLayer），spike 未通过时必须仍返回 Desktop。
	Desktop
```

### 2.3 新增：`include/ECDI/Window/WindowState.h`（84 头）

**定稿 = 初设 v1.3 §2.5 全文，零改动。** `WindowState{ restored=0, minimized, maximized }`——零值 = 窗口初始态，与 `m_lastWindowState` 的默认成员初始化语义对齐（§3.2）。

### 2.4 新增：`include/ECDI/EventSystem/Window/WindowStateChangedEvent.h`（85 头）

**定稿 = 初设 v1.3 §2.6 全文，零改动。** 依赖：`WindowEvent.h` + `ECDI/Window/WindowState.h`（事件 → 状态，方向正确）。

### 2.5 修改：`include/ECDI/Platform/PlatformWindow.h`

**API 追加部分定稿 = 初设 v1.3 §2.3 全文，零改动**（配置期 4 virtual + 运行期 3 virtual，含 `@pre` 注释与 include 追加）。另按 D-SEAM-1 在类 doc 注释追加「能力扩展惯例」段（详设定稿全文）：

```cpp
/// @brief 平台窗口抽象（7.1）：平台负责"窗口存在"，框架负责"窗口里面发生什么"
/// @details Window 组合此接口——Window 不接触 HWND/创建细节；
/// 生命周期 + 平台能力（重绘请求/客户区查询/文本输入插入点）下沉。
/// 唯一实现：Win32PlatformWindow（X11/Wayland 只留接口，YAGNI）。
/// 零 Win32 类型——接口全部用框架层类型（Size/CaretGeometry），平台细节封装在实现内。
///
/// ★ 平台能力扩展惯例（Phase 12 R9 定稿——后续阶段加能力的模板，D-SEAM-1）：
///   ① PlatformWindow 加一个能力 virtual（本文件——接口即契约，零消息号）；
///   ② Window 加公共方法透传（Window.h——应用层唯一入口）；
///   ③ 平台消息在具体实现的消息循环内消化（如 Win32PlatformWindow::HandleMessage
///      状态同步区——不进 WindowMessageHandler 翻译器，除非它产生 Framework Event）。
///   三步都不新建接缝类/消息注册机制——「惯例而非抽象」（R9 裁决）。
///   先例：Phase 12 的 SetChromeMode/SetWindowLayer/Minimize/Maximize/Restore。
class PlatformWindow{
```

### 2.6 修改：`include/ECDI/Window/Window.h`

**定稿 = 初设 v1.3 §2.4 全文，零改动**（配置期 4 方法 + 运行期 3 方法 + 两个 include 追加）。`Window.cpp` 对应实现为**纯转发**（§3.9/§3.10 已含平台侧全文，Window 侧每方法一行 `m_platformWindow->Xxx(...)`，不做防御——`m_platformWindow` 构造后恒非空，与既有 `Invalidate()` 转发同风格）。

### 2.7 修改：`include/ECDI/EventSystem/EventType.h` / `EventRouter.h` / `EventRouter.cpp`

**头文件定稿 = 初设 v1.3 §2.7/§2.8 全文，零改动**（`WindowStateChanged` 枚举 + `OnWindowStateChanged` 虚方法 + 前置声明）。`EventRouter.cpp` 的 Dispatch 追加（初设只说"追加一条"，此处给出与既有条目同构的全文）：

```cpp
	// include 追加（事件头按字母序插入既有分组）：
	#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"

	// OnEvent() 内、WindowResizedEvent 的 Dispatch 之后追加：
	dispatcher.Dispatch<WindowStateChangedEvent>([this](const WindowStateChangedEvent& e){

		OnWindowStateChanged(e);

	});
```

### 2.8 `Application` 不改（确认）

`EventRouter::OnWindowStateChanged` 默认空实现 = 「不关心」。**框架零透传代码**（初设 §2.9 裁决维持）；demo/VisualTest 需要时由应用侧自行 override。

---

## 3. `Win32PlatformWindow` 实施规格

> 改动落点：`src/Platform/Win32/Win32PlatformWindow.h`（状态成员 + 7 方法 override）与 `.cpp`（include + 实现 + `HandleMessage` 6 处）。以下全文均已按源码实文核对（§1.2 纪律）。

### 3.1 include 策略与常量兜底（勘误②③落地 + D-DWM-1）

`Win32PlatformWindow.cpp` **现行 include 块**（`Windows.h`/`imm.h`/`cstring`/`string`/`system_error`）追加与修改后：

```cpp
﻿#include "Platform/Win32/Win32PlatformWindow.h"

#include "Platform/Win32/Win32WindowClass.h"
#include "ECDI/Core/Logger.h"          // ★ 勘误③：Phase 12 各契约警告需 Logger（原文件无此 include）
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"   // §3.8 事件构造

#include <Windows.h>
#include <dwmapi.h>                    // Phase 12 R6：DwmExtendFrameIntoClientArea / DwmSetWindowAttribute
#include <imm.h>
#include <windowsx.h>                  // ★ 勘误②：GET_X_LPARAM / GET_Y_LPARAM（NCHITTEST 屏幕坐标提取）

#ifdef DrawText
#undef DrawText   // Win32 宏防护（与既有同款——dwmapi.h/ windowsx.h 展开链也可能带入 Windows.h）
#endif

#include <cstring>
#include <string>
#include <system_error>

// ── Phase 12 D-DWM-1（实现期修正 v1.4）：Win11 圆角常量**无需兜底** ──────────
// 实测取证：MinGW-w64 的 <dwmapi.h> 把 DWMWA_WINDOW_CORNER_PREFERENCE 定义为
//   DWMWINDOWATTRIBUTE 枚举**成员**（= 33），DWM_WINDOW_CORNER_PREFERENCE 与
//   DWMWCP_* 亦为枚举定义——**它们都不是宏**，故 `#ifndef <宏>` 守卫恒真、必与既有
//   定义冲突（原 D-DWM-1 的「#ifndef 兜底」前提不成立）。MSVC SDK 同样为枚举。
// ⇒ 直接用头里的定义，零兜底代码。
```

> **说明**：① `#ifdef DrawText #undef DrawText` 原本在 `Win32PlatformWindow.h`（include 链首）——`.cpp` 侧追加 `dwmapi.h`/`windowsx.h` 后按 skill 条 10 惯例在 `.cpp` 的 include 块尾再放一次（防御传递展开）；② ⚠️ **原「`#ifndef` 兜住两者即可双头兼容」判断错误**（v1.4 实现期推翻）：这两组名字都是**枚举成员**而非宏，`#ifndef` 永远为真 ⇒ 强制零兜底（MinGW 实测 error: using typedef-name after 'enum' / conflicts with a previous declaration）；③ **不做链接条件编译**（D-DWM-1：链接失败是构建期错误，四工具链验证暴露，YAGNI）。

### 3.2 新增状态成员（`Win32PlatformWindow.h` private 区追加）

```cpp
	// ── Phase 12：chrome / 层级 / 生命周期状态 ─────────────────────

	ChromeMode m_chromeMode = ChromeMode::Normal;	///< chrome 形态（R1——配置期生效，Show 后拒绝切换）
	int m_captionHeight = 32;	///< 标题栏命中高度（R3；逻辑坐标 DIP——默认 32）
	int m_resizeInset = 8;	///< 缩放热区宽度（R3；逻辑坐标 DIP——默认 8）
	WindowLayer m_windowLayer = WindowLayer::Normal;	///< 层级档位（R10——语义状态，不随实现路径降级）

	bool m_shown = false;	///< 是否已调用过 Show()（配置期/运行期判据——「API 调用事实」，非「系统当前可见」）
	bool m_chromeConfigured = false;	///< ChromeMode 是否已配置（D-CHROME-1：一次确定——重复调用一律 Warning + 忽略）
	WindowState m_lastWindowState = WindowState::restored;	///< 状态事件去重锚（零值 = 窗口初始态，§2.3 对齐）
```

`Win32PlatformWindow.h` 头部追加 include：

```cpp
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/WindowLayer.h"
#include "ECDI/Window/WindowState.h"
```

public 区追加 7 个 override（签名与初设 §2.3 一致）：

```cpp
	// ── Phase 12：WindowChrome / 窗口层级（配置期——Window 构造后 / Show() 前）──
	void SetChromeMode(ChromeMode mode) override;
	void SetCaptionHeight(int height) override;
	void SetResizeInset(int inset) override;
	void SetWindowLayer(WindowLayer layer) override;

	// ── Phase 12：窗口状态（运行期——Show() 之后才有效）──────────
	void Minimize() override;
	void Maximize() override;
	void Restore() override;
```

private 区追加**三个**内部辅助：

```cpp
	/// @brief 最大化客户区校正（R4——rcWork 唯一基准，D-COMP-1：不引入补偿）
	void AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient);

	/// @brief DIP → 物理像素（D-DPI-1：窗口 DPI——非鼠标所在显示器）
	static int DipToPixels(int dip, HWND hwnd);

	/// @brief DWM 增强（R6——系统阴影 + Win11 圆角；失败容忍，仅日志不中断）
	void ApplyDwmEnhancements(HWND hwnd);
```

> **v1.2 补正（内部复核发现）**：v1.0/v1.1 此处只列 2 个辅助，但 §3.11 的注又要求追加 `ApplyDwmEnhancements`（§6 清单亦误记为「+2 辅助声明」）——**实为 3 个**。v1.2 已把声明补进本节代码块，§6 清单同步改为「+3 辅助声明」。

### 3.3 `Show()` 置位（既有方法修改——一处插入）

```cpp
void Win32PlatformWindow::Show() {

	if (m_hwnd) {

		m_shown = true;   // 配置期 → 运行期分界线（与 Window::Show() 一一对应）

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);

	}

}
```

### 3.4 `WM_NCCALCSIZE` + `AdjustMaximizedClientRect`（R2/R4 定稿全文）

`HandleMessage` 状态同步 switch 内、`case WM_PAINT:` **之前**插入（消息到达早期返回）：

```cpp
	case WM_NCCALCSIZE:

		// 仅 Borderless 且 wParam == TRUE（客户区矩形需重算）时拦截。
		// wParam == FALSE 时 lParam 是 RECT 而非 NCCALCSIZE_PARAMS——不能解释（必须走 DefWindowProc）。
		if (m_chromeMode == ChromeMode::Borderless && wParam == TRUE){

			// 非最大化：客户区 = 整窗 → 返回 0 且不改 rect（系统按 rect 直接采用）
			if (!IsZoomed(hwnd)){

				return 0;

			}

			// 最大化：系统会把窗口外扩一圈（边框 + 阴影），客户区若原样采用会盖住任务栏。
			// 按 rcWork 校正（R4——细节见 AdjustMaximizedClientRect）
			AdjustMaximizedClientRect(hwnd,
				reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]);

			return 0;

		}

		break;   // Normal 或 wParam == FALSE → DefWindowProc
```

私有辅助（类外实现）：

```cpp
void Win32PlatformWindow::AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient){

	// R4 验收基准：客户区不覆盖任务栏、不残留系统边框空白。
	// ⚠️ 不能用 rcMonitor（含任务栏区，客户区会盖住任务栏）；必须用 rcWork。
	MONITORINFO mi{};

	mi.cbSize = sizeof(MONITORINFO);

	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)){

		return;   // 查询失败：保持系统原值（fail-safe——宁可留系统空白也不盖任务栏）

	}

	// 客户区 = 显示器工作区（屏幕坐标）。
	// 最大化时系统已把 rcWindow 撑到 rcWork 之外（按边框量外扩）；此处直接把客户区取为
	// rcWork，即得「可见范围 == 工作区」。无需补偿，也就不会出现方向性错误（D-COMP-1）。
	rcClient = mi.rcWork;

	// ⚠️ D-COMP-1：刻意不引入任何基于 (rcWindow - rcMonitor) 差值的补偿——最大化时该
	// 差值为负，「正负代入」的对称补偿会把客户区推出 rcWork。若 T4 在某 Windows 版本
	// 失败，另起 R 立项修订（补偿只能「正内缩」：left+=ix / right-=ix，ix>=0）。

}
```

### 3.5 `WM_NCHITTEST`（R3 定稿全文——含 D-HIT-1 门控与 D-DPI-1 换算）

```cpp
	case WM_NCHITTEST: {

		// Normal 模式不干预（系统标题栏/边框行为完全不变——零回归底线）
		if (m_chromeMode != ChromeMode::Borderless){

			break;

		}

		// 屏幕坐标 → 窗口坐标（NCHITTEST 的 lParam 是屏幕坐标——GET_X_LPARAM 带符号提取，
		// 多显示器负坐标安全；windowsx.h 由 §3.1 引入）
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		RECT rcWin{};

		GetWindowRect(hwnd, &rcWin);

		const int x = pt.x - rcWin.left;

		const int y = pt.y - rcWin.top;

		// DIP → 物理像素（D-DPI-1：换算点唯一在此——公共 API 语义恒为 DIP；
		// 当前无 PerMonitorV2 声明 → dpi 恒 96 → 1:1，但契约已就位）
		const int inset = DipToPixels(m_resizeInset, hwnd);

		const int caption = DipToPixels(m_captionHeight, hwnd);

		const int w = rcWin.right - rcWin.left;

		const int h = rcWin.bottom - rcWin.top;

		// 最大化态：系统不会进入 resize 循环，返回 HTLEFT/HTTOP 等会让人误以为可拖宽——
		// 故四边四角 resize 判据整体跳过（D-HIT-1）。
		// ⚠️ caption 判据不走这条门——最大化时仍允许从顶部往下拖还原（系统行为）。
		const bool resizable = !IsZoomed(hwnd);

		// 四角优先（角命中优先级高于边——否则角落会被边的判定吃掉）
		const bool left = resizable && x < inset;

		const bool right = resizable && x >= w - inset;

		const bool top = resizable && y < inset;

		const bool bottom = resizable && y >= h - inset;

		if (top && left)     return HTTOPLEFT;

		if (top && right)    return HTTOPRIGHT;

		if (bottom && left)  return HTBOTTOMLEFT;

		if (bottom && right) return HTBOTTOMRIGHT;

		if (left)   return HTLEFT;

		if (right)  return HTRIGHT;

		if (top)    return HTTOP;

		if (bottom) return HTBOTTOM;

		// 标题栏区 → HTCAPTION（拖动移动 / 双击最大化 / Aero Snap 系统免费获得）
		// ⚠️ 判定顺序：先 resize 边（上一条），后 caption——保证窗口最上缘是缩放手感
		if (y < caption && caption > 0){

			return HTCAPTION;

		}

		break;   // 客户区 → DefWindowProc（返回 HTCLIENT，交框架派发鼠标事件）

	}
```

### 3.6 `WM_NCACTIVATE`（R8——继承初设 v1.3 §3.6，仅勘误不涉及）

```cpp
	case WM_NCACTIVATE: {

		// Normal 模式不干预（系统标题栏行为完全不变——零回归底线）
		if (m_chromeMode != ChromeMode::Borderless){

			break;

		}

		// ★ v1.1（评审 §三 P1）：最小化态按 Win32 语义放行 DefWindowProc——MSDN 对
		// WM_NCACTIVATE 明确注明 minimized 窗口有特殊处理路径，lParam = -1 抑制重绘的模式
		// 不覆盖该状态（强行套用可能与最小化态的激活链维护冲突）。
		if (IsIconic(hwnd)){

			break;   // 最小化态：走 DefWindowProc 正常处理

		}

		// 防闪烁标准做法：以 lParam = -1 调 DefWindowProcW 表示「不重绘非客户区」，
		// 但保留其返回值的语义（激活状态变更的内部处理照常执行）——MSDN 记录的模式。
		// ⚠️ lParam = -1 只抑制重绘，不改变激活语义（wParam 仍被系统内部使用）；
		// 返回值必须原样透传——不能返回固定 TRUE（会破坏系统对激活链的维护）。
		const LRESULT result = DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

		return result;

	}
```

> **v1.2 排版修正（内部复核发现）**：v1.0/v1.1 本节的两个 `if` 闭合大括号位置与缩进错位（`if (IsIconic(hwnd)){ ... }` 的 `}` 与外层 `if` 的 `}` 视觉上粘连）。**逻辑本身正确**（非 Borderless → break；Borderless 且最小化 → break），但形似缺陷、实施者极易「顺手改对」时改坏，故重排为两个平铺 `if` 块。

### 3.7 `WM_WINDOWPOSCHANGING`（R10——继承初设 v1.3 §3.7）

```cpp
	case WM_WINDOWPOSCHANGING: {

		// Bottom / Desktop 档：持续维护普通窗口层中的底部位置
		// ⚠️ 必须是「持续维护」而非一次性 SetWindowPos（其他程序会把我们顶下来）；
		// ⚠️ 只改 hwndInsertAfter，不碰 x/y/cx/cy/flags（否则会干扰最大化/还原几何）；
		// ⚠️ 契约边界：不承诺阻止第三方 SetWindowPos 造成的瞬时 z 序变化（Windows z 序是动态的）
		if (m_windowLayer != WindowLayer::Normal){

			auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);

			if ((wp->flags & SWP_NOZORDER) == 0){

				wp->hwndInsertAfter = HWND_BOTTOM;

			}

		}

		break;   // 走 DefWindowProc（几何变更仍由系统处理）

	}
```

### 3.8 `WM_SIZE` → `WindowStateChangedEvent`（R7——既有 case 修改全文）

**事件派发路径（初设未明，此处定稿）**：状态同步区构造事件 → `m_host.OnEvent(const Event&)`（与翻译器同一派发通道）→ `Window::OnEvent` → `Application::OnEvent` → `EventRouter::OnEvent` → `OnWindowStateChanged`。**平台层不直接认识 EventRouter**（Host 契约边界不破）。

```cpp
	case WM_SIZE: {
		// 窗口大小变化 → Host 回调同步 RootWidget 尺寸；随后 fall-through 翻译器（WindowResizedEvent）
		m_host.OnResized(LOWORD(lParam), HIWORD(lParam));

		// Phase 12 R7：窗口状态变化 → 事件（尺寸同步已在 OnResized 完成——顺序契约：
		// 消费者收到事件时 RootWidget 已是新尺寸）。
		// ⚠️ 判定来源是 IsIconic/IsZoomed（系统真实状态），不是我们调了哪个 API——
		// 鼠标拖拽最大化 / Win+↑ / Aero Snap / 双击标题栏等非 API 路径同样产生事件（T7b）。
		WindowState state = WindowState::restored;

		if (IsIconic(hwnd)){

			state = WindowState::minimized;

		}
		else if (IsZoomed(hwnd)){

			state = WindowState::maximized;

		}

		// 去重：仅状态真正变化时派发（WM_SIZE 在拖拽缩放时高频到达——
		// 不去重会以同一状态淹没消费者；连续两次 Maximize 只产生 1 个事件）
		if (state != m_lastWindowState){

			m_lastWindowState = state;

			m_host.OnEvent(WindowStateChangedEvent(m_host.GetWindow(), state));

		}

		break;   // 既有行为：继续走翻译器（WindowResizedEvent——零回归）
	}
```

> **边界记录（不扩大范围）**：`IsIconic` 时 `WM_SIZE` 的 `lParam` 为 0 → `OnResized(0,0)`。这是**既有行为**（Phase 12 之前即如此），RootWidget 被同步为 0×0 后还原时恢复——本阶段**不修**（无消费者报告问题；如需修，属独立 R）。

### 3.9 配置期四方法（R1/R3/R10 定稿全文）

```cpp
void Win32PlatformWindow::SetChromeMode(ChromeMode mode){

	// 配置期契约（同其它 chrome 配置判据）。
	// 判据 m_shown：在 Show() 内置位——表达「框架 API 是否调用过 Show()」，
	// 而非「系统当前是否可见」（Show()+Hide() 后 IsWindowVisible 为假，但契约上已进运行期）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetChromeMode ignored after Show() - mode is config-time only");

		return;

	}

	// ★ D-CHROME-1（v1.1——评审 §二 P1）：ChromeMode **一次确定**——配置期内重复调用
	//（无论同值异值）一律 Warning + 忽略。
	// 根因（v1.0 缺陷）：Borderless→Normal 不派发 SWP_FRAMECHANGED ⇒ frame 与状态失同步，
	// 且 m_borderlessApplied 残留 true 会把第三次 Borderless 调用挡在幂等分支外。
	// 采用「一次确定」而非「配置期动态重配置」：后者需要双向 frame 重算状态机——
	// 无消费者（YAGNI），且与「窗口形态由创建期一次决定」的配置期语义更一致。
	if (m_chromeConfigured){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetChromeMode ignored - chrome mode is decided once (config-time)");

		return;

	}

	m_chromeConfigured = true;

	m_chromeMode = mode;

	if (mode != ChromeMode::Borderless){

		return;   // Normal：无需任何处理（默认样式即 Normal——不走 SWP_FRAMECHANGED）

	}

	// HWND 生命周期前提（初设 v1.2 §3.9）：本框架窗口构造即建 HWND
	//（Win32PlatformWindow 构造体 CreateWindowExW，失败抛 std::system_error），
	// 故 Window 构造完成后 m_hwnd 恒非空。此处仍做防御，与 SetWindowLayer 对齐。
	if (m_hwnd == nullptr){

		return;

	}

	// Borderless 应用流程：
	// ① 样式本身不变（保留 WS_OVERLAPPEDWINDOW——技术路线核心）
	// ② 通知系统重算非客户区：SWP_FRAMECHANGED（必须在窗口显示前派发——否则闪一次边框）
	SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	// ③ DWM 增强（阴影/圆角——失败容忍）
	ApplyDwmEnhancements(m_hwnd);

}

void Win32PlatformWindow::SetCaptionHeight(int height){

	// 配置期契约（同 SetChromeMode 判据）
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetCaptionHeight ignored after Show() - chrome config is config-time only");

		return;

	}

	// 边界契约：「<= 0 视为 0」——允许应用完全放弃 HTCAPTION 拖动区（初设 §3.1）
	m_captionHeight = height < 0 ? 0 : height;

}

void Win32PlatformWindow::SetResizeInset(int inset){

	// 配置期契约（同 SetChromeMode 判据）
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetResizeInset ignored after Show() - chrome config is config-time only");

		return;

	}

	m_resizeInset = inset < 0 ? 0 : inset;

}

void Win32PlatformWindow::SetWindowLayer(WindowLayer layer){

	// 配置期契约（v1.1——初设评审 §3）：与 chrome 三件套同一生命周期。
	// API 签名不因此锁死——未来若出现运行期切层需求，只需松开此判据（零签名变更）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetWindowLayer ignored after Show() - layer is config-time only");

		return;

	}

	if (m_windowLayer == layer){

		return;   // 幂等

	}

	m_windowLayer = layer;   // 语义状态恒记录用户请求（D-DESK-1：不随实现路径降级）

	if (layer == WindowLayer::Desktop){

		// ⚠️ spike 未通过前：Desktop 不承诺可用——状态不降级，仅当前 Win32 实现
		// 按 Bottom 语义执行（D-DESK-1：降级的是「实现如何执行」，不是「状态是什么」；
		// 未来 GetWindowLayer 必须仍返回 Desktop）。
		Logger::Log(LogLevel::Warning,
			L"WindowChrome: WindowLayer::Desktop not yet validated (spike pending) - executed as Bottom");

	}

	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由 WM_WINDOWPOSCHANGING 自然生效）

	}

	// 切到 Bottom/Desktop：立即派发一次（后续由 WM_WINDOWPOSCHANGING 持续维护）
	// 切回 Normal：不主动改变当前 z 序（交系统自然演化——避免「突然跳到最前」的反直觉效果）
	if (m_windowLayer != WindowLayer::Normal){

		SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}
```

### 3.10 运行期三方法（R7 定稿全文）

```cpp
void Win32PlatformWindow::Minimize(){

	// 运行期契约（初设 v1.3——评审 §三）：与配置期 API 对称，Show() 前拒绝。
	// ⚠️ 不能靠「系统会忽略」成立契约——ShowWindow 本身就是显示状态操作。
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Minimize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);

}

void Win32PlatformWindow::Maximize(){

	// 运行期契约（同 Minimize）
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Maximize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MAXIMIZE);

}

void Win32PlatformWindow::Restore(){

	// 运行期契约（同 Minimize）
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: Restore ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE);

}
```

### 3.11 `ApplyDwmEnhancements`（R6 定稿全文——勘误①修正 + D-DWM-2 标定项）

```cpp
void Win32PlatformWindow::ApplyDwmEnhancements(HWND hwnd){

	// R6 失败容忍契约：DWM 不可用/属性不被支持 → 仅日志 Warning，绝不中断。
	// 本函数整体属「Win32 实现细节」——数值参数不是公共 API 语义。
	// ⚠️ 日志为宽字面量（Logger::Log 第二参 std::wstring_view——初设勘误①）。

	// ① 保留系统阴影/层次：minimal frame extension（MARGINS{1,1,1,1}——D-DWM-2 起步值；
	//    全 0 失去系统阴影，过大则玻璃延伸进客户区。真机标定观察项：
	//    [a] 阴影是否保留 [b] 客户区顶部是否出现玻璃条 [c] Win10/Win11 各一遍）
	MARGINS margins{ 1, 1, 1, 1 };

	const HRESULT hrExtend = DwmExtendFrameIntoClientArea(hwnd, &margins);

	if (FAILED(hrExtend)){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: DwmExtendFrameIntoClientArea failed - DWM enhancements skipped");

	}

	// ② Win11 圆角：DWMWA_WINDOW_CORNER_PREFERENCE（build 22000+）
	//    ⚠️ Win10 及更早返回 E_INVALIDARG——被失败容忍吸收（预期路径，非异常）
	DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;

	const HRESULT hrCorner = DwmSetWindowAttribute(hwnd,
		DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

	if (FAILED(hrCorner)){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: corner preference unsupported - rounded corners skipped");

	}

}
```

> **私有方法声明**：`void ApplyDwmEnhancements(HWND hwnd);` 加入 `Win32PlatformWindow.h` private 区（§3.2 清单一并列出）。

### 3.12 D-DPI-1 定稿：`DipToPixels`（换算函数全文 + 编译期风险与备案）

```cpp
int Win32PlatformWindow::DipToPixels(int dip, HWND hwnd){

	// D-DPI-1：DPI 来源 = GetDeviceCaps(LOGPIXELSX)（**窗口 DC**——非鼠标所在显示器）。
	// 理由：caption/inset 描述的是窗口自身 UI 的几何语义（标题栏是窗口的一部分），
	// 跟随窗口比跟随鼠标更一致；且命中几何只服务本窗口的九宫格判定。
	// 公式：整数 half-up —— px = (dip * dpi + 48) / 96（dpi=96 时恒等于 dip，无误差）。
	// 当前项目未声明 PerMonitorV2 → 系统 DPI 虚拟化下 dpi 恒 96 → 现状 1:1。
	// ⚠️ 措辞（v1.1——评审 §七 P2）：本实现**保持换算点唯一**；未来启用 Per-Monitor V2 时
	// **仅替换 DPI 来源**（GetDeviceCaps → GetDpiForWindow），**不改变**调用位置、参数语义
	// 与换算公式——**不是**「零改动」（替换即改动，只是改动被圈定在本函数体内）。
	// ★ 溢出防护（v1.1——评审 §八 P2）：dip 是公共 API 直收的 int，理论可传 INT_MAX——
	// dip * dpi 会溢出 int。中间量升 long long（不做人为的「合理最大值」截断）。
	HDC hdc = GetDC(hwnd);

	if (hdc == nullptr){

		return dip;   // DC 获取失败：1:1 降级（与「DPI 未落地」现状一致——fail-safe）

	}

	const int dpi = GetDeviceCaps(hdc, LOGPIXELSX);

	ReleaseDC(hwnd, hdc);

	const long long scaled = static_cast<long long>(dip) * dpi;

	return static_cast<int>((scaled + 48) / 96);

}
```

> ⚠️ **v1.2 勘误④（内部复核发现）**：v1.0/v1.1 的本节代码块在函数闭合 `}` 之后**残留了 5 行旧版尾巴**（`const int dpi = GetDeviceCaps(...); ReleaseDC(...); return (dip * dpi + 48) / 96; }`）——这是 v1.0 → v1.1 修订（升 `long long`）时的清理遗漏。该残留**照抄即编译不过**（函数外游离语句 + 多余 `}`），且其内容恰是 v1.1 P2 要修掉的**未做溢出防护的旧写法**，与同节修订自相矛盾。v1.2 已删除。

> ⚠️ **实现期决策修正（相对 §1.1 表格的倾向）**：原倾向 `GetDpiForWindow`（Win10 1607+），但该 API 受 `_WIN32_WINNT` 宏门槛约束，四工具链的宏默认值不一，会引入编译期变量。**定稿改用 `GetDeviceCaps(hdc, LOGPIXELSX)`**——XP 起恒可用、零宏依赖、无 PerMonitor 时与 `GetDpiForWindow` 返回同值（系统 DPI）。**未来 Per-Monitor V2 落地时再替换为 `GetDpiForWindow`**（单一函数体内替换——换算点唯一，正是 D-DPI-1 的设计目的）。此处为详设相对初设倾向的一处**偏离**，理由 = 消除四工具链编译期风险（若评审认为应直接用 `GetDpiForWindow`，改动仅此一函数）。

---

## 4. 链接库与构建系统定稿（dwmapi）

### 4.1 CMake（`CMakeLists.txt` WIN32 块追加一行）

```cmake
if(WIN32)
    # Existing Win32 / rendering dependencies
    # msimg32：Phase 8 AlphaBlend（DrawImage 的 AC_SRC_ALPHA 混合）
    target_link_libraries(ECDI PUBLIC user32 imm32 msimg32)
    # Phase 11 image decoding dependencies（WIC/COM——组内以实际符号收敛，linker 验证收尾）
    target_link_libraries(ECDI PUBLIC windowscodecs ole32 shlwapi)
    # Phase 12 WindowChrome dependencies（R6 DWM——静态库模型下 PUBLIC 传播；
    # 未来切换 SHARED 模型时重新评估传播属性——初设 §4.1 裁决）
    target_link_libraries(ECDI PUBLIC dwmapi)
endif()
```

> **MinGW**：mingw-w64 自带 `libdwmapi.a` import library——`-ldwmapi` 可解析。若某环境缺失 → 构建期错误（D-DWM-1：不做条件编译）。

### 4.2 vcxproj（`ECDI.vcxproj`——三类条目）

**① 4 处 `AdditionalDependencies`**（Debug/Release × Win32/x64 四个配置——现有行追加 `dwmapi.lib;`）：

```xml
<AdditionalDependencies>imm32.lib;msimg32.lib;windowscodecs.lib;ole32.lib;shlwapi.lib;dwmapi.lib;%(AdditionalDependencies)</AdditionalDependencies>
```

**② 4 个 `ClInclude`**（追加到既有 include 头分组）：

```xml
    <ClInclude Include="include\ECDI\Window\ChromeMode.h" />
    <ClInclude Include="include\ECDI\Window\WindowLayer.h" />
    <ClInclude Include="include\ECDI\Window\WindowState.h" />
    <ClInclude Include="include\ECDI\EventSystem\Window\WindowStateChangedEvent.h" />
```

**③ 1 个 `ClCompile`**（测试文件，与既有 Tests 组并列）：

```xml
    <ClCompile Include="src\Tests\WindowChromeTests.cpp" />
```

> **CMake 零维护项确认**：框架 `.cpp` glob（`CONFIGURE_DEPENDS`）自动吸纳测试文件；4 新 Public 头经 `PUBLIC_HEADERS` glob 自动进自包含测试；install 走目录复制（初设 §4.2 已核实）——**CMake 侧唯一改动 = §4.1 一行**。

---

## 5. 测试规格定稿

### 5.1 文件与注册

- 新建 `src/Tests/WindowChromeTests.cpp`（include `<Windows.h>` + `#undef DrawText` 防护——skill 条 10；include `RunAllTests.h`/`TestFramework.h`/`ECDI/Window/Window.h`/`ECDI/Application/Application.h`/`ECDI/Window/ChromeMode.h`/`ECDI/Window/WindowLayer.h`/`ECDI/Window/WindowState.h`（`TestApp::seen` 的 `std::vector<WindowState>`）/`ECDI/EventSystem/EventRouter.h`（§5.2 `TestApp` 的 override 目标）/`ECDI/EventSystem/Window/WindowStateChangedEvent.h`（T7 事件类型）；**`Platform/Win32/Win32RenderContext.h` + `ECDI/Platform/PlatformWindow.h`（v1.2/v1.4——`TestWindow::Handle()` 取 HWND 必需：前者提供 `Win32RenderContext::GetHandle`，后者提供 `PlatformWindow` **完整类型**——`Window.h` 对 `PlatformWindow` 仅前置声明）**；`<memory>`/`<vector>`）。 **另：文件内需 `using namespace ECDI;`**（与 `AntiAliasingTests.cpp:21` 同一测试文件约定——否则 `Window`/`Application`/`ChromeMode` 全不可见）。
- `src/Tests/RunAllTests.h`：声明 `void RegisterWindowChromeTests();`；`RunAllTests.cpp`：注册调用（与 `RegisterAntiAliasingTests` 同款）。

### 5.2 测试窗口辅助（D-TST-1 落地全文）

```cpp
namespace {

/// @brief 测试窗口守卫（D-TST-1：T0–T3/T5/T6 不 Show——创建即有效，零闪窗；
/// T4/T7/T7a 自行 Show 并负责泵消息）
/// @details 每用例独立构造（v1.2——D-TST-1 修订：caption/inset 配置各异，共享窗口会造成
/// 用例顺序依赖）。
struct TestWindow{

	/// ⚠️ **非拥有**指针——窗口对象由 Application 持有（唯一登记路径 `Create()`）
	Window* window = nullptr;

	TestWindow(Application& a, ChromeMode mode, int caption, int inset){

		// 窗口**必须**经 Application::Create() 创建：登记 `Application::m_windows` 只发生在此。
		// 直接构造 Window（构造器是 public）会绕过登记 —— 销毁时
		// `~Window → Release() → DestroyWindow() →（同步 WM_DESTROY）→ WindowDestroyedEvent
		// → Application::OnWindowDestroyed` 在册中找不到该窗口 ⇒ `Application.cpp:92` 断言。
		// ⚠️ v1.5：v1.0–v1.4 的 `std::make_unique<Window>(&a, ...)` 正是这样崩的；
		//    `FRAMEWORK_ASSERT` 只在 `_DEBUG` 下存在（`Core/ECDIAssert.h:22`）⇒ 只在 MSVC 构建暴露。
		window = &a.Create("ECDI_ChromeTest", 800, 600);

		window->SetChromeMode(mode);
		window->SetCaptionHeight(caption);
		window->SetResizeInset(inset);
		// 800×600 窗口基准（配合 T2 九宫格坐标表）；不调用 Show()（T4/T7/T7a 自行 Show + 泵消息）。
		// ⚠️ 绝不可用 `unique_ptr<Window>` 持有本窗口：Application 的延迟销毁表
		//    （`m_deferredDestroy`）会对同一对象二次析构 = double delete。

	}

	~TestWindow(){

		// 只销毁 HWND（WM_DESTROY → Application 回收窗口对象）；本对象不得后于 Application 析构
		window->Release();

	}

	/// @brief 取底层 HWND（★ v1.2 新增——§5.3 全部窗口类用例的唯一几何/命中查询入口）
	/// @details Window 刻意不暴露句柄（7.1.1 边界：框架层零 Win32 类型），测试经
	/// 「抽象接口 → 平台上下文」两跳取回：
	///   ① Window::GetPlatformWindow()        （include/ECDI/Window/Window.h:86，返回 PlatformWindow&）
	///   ② PlatformWindow::GetRenderContext() （返回 const PlatformRenderContext&——空基类）
	///   ③ static_cast 到 Win32RenderContext 后 GetHandle()（src/Platform/Win32/Win32RenderContext.h）
	/// ⚠️ 这是本项目**首次**由 `Window` 对象取 HWND——既有窗口测试（RendererTests /
	/// AntiAliasingTests）全部裸 `CreateWindowExW` 自建句柄，无先例可抄；故此路径写死在此，
	/// 不得在用例内另行 find-by-title 之类的旁路（不可靠）。
	/// ⚠️ 依赖 §5.1 的 `Platform/Win32/Win32RenderContext.h` include。
	HWND Handle() const{

		return static_cast<const Win32RenderContext&>(
			window->GetPlatformWindow().GetRenderContext()).GetHandle();

	}
};

/// @brief 手动消息泵（T4/T7 用——处理至多 n 条消息后返回）
void PumpMessages(int maxCount){

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i){

		TranslateMessage(&msg);
		DispatchMessageW(&msg);

	}

}

}
```

> **v1.2 补正（内部复核发现——P1 规格缺口）**：v1.0/v1.1 的辅助结构体**没有句柄访问器**，而 §5.3 的 T1/T2/T3/T5/T6/T7a 全部需要 `GetWindowRect` / `SendMessageW(WM_NCHITTEST)` / `IsWindowVisible`——**照原文实施会当场卡壳**。v1.2 补 `Handle()` 全文（上）。用例内一律写 `win.Handle()`，不再出现裸 HWND 变量。

> ⚠️ **T2/T3 的 `WM_NCHITTEST` 查询用 `SendMessageW`（同步直达 WndProc）**，不经泵——命中测试是纯几何函数，未显示窗口同样有效。T3 的两窗口同几何位置：用 `SetWindowPos` 把 B 挪到与 A 相同的屏幕矩形（或直接以各自窗口局部坐标查询——**推荐后者**，与屏幕位置无关，零竞态）。

### 5.3 用例清单（定稿 **11 行** = 9 自动用例（T0–T7a）+ 2 非自动项（T7b/T8））

| # | 用例名 | 手法 | 断言（逐条） |
|---|---|---|---|
| T0 | `WindowChrome.ChromeModeDecidedOnce` | 构造 Borderless 测试窗口 → `SetChromeMode(ChromeMode::Normal)`（第二次，异值）→ 再 `SetChromeMode(ChromeMode::Borderless)`（第三次，同值） | **三次调用的最终语义 = 第一次的 Borderless**（`GetClientRect`+`ClientToScreen` 与窗口矩形四边相等——T1 同款断言）。⚠️ 一次确定语义（D-CHROME-1）：第二/三次调用 Warning + 忽略——防止未来把状态机改坏（v1.0 缺陷的回归锚） |
| T1 | `WindowChrome.BorderlessClientEqualsWindow` | 窗口 A（Normal）、B（Borderless, 4/4），均不 Show；各自 `GetWindowRect` + `GetClientRect`+`ClientToScreen` | **A**：客户区**严格小于**窗口（任一边差 ≥ 1）；**B**：`ClientRectScreen` 与窗口矩形**四边全部相等**。坐标系规则见初设 T1（禁止直接比较两个 RECT 结构） |
| T2 | `WindowChrome.NCHitTestNineGrid` | Borderless 窗口（32/8）；局部坐标逐点 `SendMessageW(WM_NCHITTEST)`（屏幕坐标 = 窗口原点 + 局部） | `(4,4)→HTTOPLEFT`、`(w-4,4)→HTTOPRIGHT`、`(4,h-4)→HTBOTTOMLEFT`、`(w-4,h-4)→HTBOTTOMRIGHT`、`(4,h/2)→HTLEFT`、`(w-4,h/2)→HTRIGHT`、`(w/2,4)→HTTOP`、`(w/2,h-4)→HTBOTTOM`、`(w/2,20)→HTCAPTION`、`(w/2,h/2)→HTCLIENT`。**测试目的**：`(w/2,4)` 同时落 inset 与 caption 区 → 必须返回 `HTTOP`（resize 优先级覆盖 caption） |
| T3 | `WindowChrome.NCHitTestModeIsolation` | 窗口 A（Normal, 0/0）+ B（Borderless, **4/4**——刻意取小，留余量）。**选点动态生成（v1.1——评审 §十 P2）**：`sysNC = GetSystemMetrics(SM_CYCAPTION) + SM_CYSIZEFRAME + SM_CXPADDEDBORDER`（Normal 窗口顶部非客户区总高——不硬编码 20px）；`yProbe = sysNC - 2`（确定落在 A 的 caption 区，且 > 4 远离 B 的 inset/caption） | **断言只一对**：`SendMessageW(WM_NCHITTEST, 0, MAKELPARAM(...))` 在 `(w/2, yProbe)` 处——**断言 `resultA != resultB`**（A 应为 `HTCAPTION`、B 应为 `HTCLIENT`，但**不断言具体值**——测试目的 = 「Normal 与 Borderless 对同一位置 NC 语义不同」，而非「Windows 当前标题栏恰好大于 20px」）。⚠️ 禁止「同坐标一律不同」断言——`(2,2)` 类角点两边本来就都返回 `HTTOPLEFT`（初设 v1.3 评审 §四） |
| T4 | `WindowChrome.MaximizedClientWithinWorkArea` | Borderless 窗口 `Show()` → `Maximize()` → `PumpMessages(64)` → `GetClientRect`+`ClientToScreen` + `GetMonitorInfoW(MonitorFromWindow)` | **不变量 `ClientRectScreen ⊆ rcWork`**：`left>=work.left && top>=work.top && right<=work.right && bottom<=work.bottom`（子集非相等——D-COMP-1）。⚠️ 断言前必须泵到 `WM_NCCALCSIZE`/`WM_SIZE` 已处理 |
| T5 | `WindowChrome.CaptionHeightUnit` | Borderless 窗口（`SetCaptionHeight(32)`，inset 0 以隔离变量）；`(w/2,31)` 与 `(w/2,33)` | 31 → `HTCAPTION`；33 → `HTCLIENT`（当前 DPI 1:1，边界精确可断言；D-DPI-1 契约锚点） |
| T6 | `WindowChrome.ZeroBoundaryClamp` | B1：`SetCaptionHeight(0)`/`SetResizeInset(0)`；B2：`SetCaptionHeight(-5)`/`SetResizeInset(-8)` | B1：`(w/2,4)` → `HTCLIENT`（无 caption/inset 区）；B2：与 B1 行为一致（负值 clamp 到 0——契约「<= 0 视为 0」） |
| T7 | `WindowChrome.StateEventFromApi` | 独立窗口 + **`TestApp`（`Application` 子类化）**捕获事件（override `OnWindowStateChanged` 记录——见本节末修正说明） | `Show()` → `Minimize()` 泵 → 收到 `minimized`；`Restore()` 泵 → `restored`；`Maximize()` 泵 → `maximized`；**去重**：连续两次 `Maximize()` 仅 1 个 `maximized` |
| T7a | `WindowChrome.RuntimeApiRejectedBeforeShow` | 构造后**不 Show**，直接 `Minimize()`/`Maximize()`/`Restore()` | 三者均**不产生**任何 `WindowStateChangedEvent`、`IsWindowVisible` 恒 false（框架侧拒绝，非系统忽略）；随后 `Show()`+`Maximize()` 泵 → 正常产生 `maximized` |
| T7b | （手测——不入自动用例） | 拖标题栏到屏幕顶 / Win+↑ / 双击标题栏 | 仍收到 `maximized` 事件——「事件来自系统真实状态」的验收落点 |
| T8 | （回归——无新用例） | 既有全部测试通过 | 截至 2026-09-12 为 **174** 条；Phase 12 落地后总数 = 174 + 9（T0–T7a）= **183** 预期 |

> ⚠️ **T7 的事件捕获方式（v1.0 实现期修正，v1.2 清理表述）**：事件链路 = 平台 `m_host.OnEvent` → `Window::OnEvent` → `Application::OnEvent`（`src/Window/Window.cpp:363` 实核），而 **`Application` 继承 `EventRouter` 且无法注入自定义 router**——故不能用「独立 CapturingRouter 实例」的方案（v1.0 初稿写法，已否决）。**定稿**：T7/T7a 用 `Application` 真实例 + **子类化**：
>
> ```cpp
> struct TestApp : public Application{
> 	using Application::Application;   // 默认构造轻量（源码实核：不调 Run() 无消息循环）
> 	std::vector<WindowState> seen;
> protected:
> 	void OnWindowStateChanged(const WindowStateChangedEvent& e) override{
> 		seen.push_back(e.GetState());
> 	}
> };
> ```
>
> ⚠️ **继承层级**：`OnWindowStateChanged` 在 `EventRouter` 中是 `protected` 虚方法（§2.7），故 `TestApp` 的 override 也必须写在 `protected` 区——写成 public 会编译不过（访问级别不得放宽）。泵消息后检查 `seen`。

### 5.4 手测矩阵（R8——实现期人工项，不自动化）

| 场景 | 期望 |
|---|---|
| Alt+Space 系统菜单（Borderless） | 弹出（保留 `WS_OVERLAPPEDWINDOW` 的红利） |
| Aero Snap 拖到左/右边缘 | 半屏贴靠正常 |
| Win+↑ / Win+↓ | 最大化 / 还原正常，且事件到达（T7b 同路径） |
| 最小化动画 | 正常（`WS_OVERLAPPEDWINDOW` 保留） |
| **Bottom + Maximize/Restore**（v1.1——评审 §九 P2：Bottom 持续维护与最大化 z 序的交互） | `SetWindowLayer(Bottom)` 后 `Maximize()` / `Restore()` / Win+↑ / Alt+Tab：最大化态下 Bottom 维护不产生异常 z 序跳动；还原后仍维持底部位置 |
| 双显示器跨屏拖动 | 命中热区手感无异常（DPI 相同时） |
| IME 输入（Borderless 下 TextBox） | 候选窗定位不回归（`WM_NCACTIVATE` 拦截不影响 IME 通道） |

### 5.5 Desktop spike 程序（全文——D-SPK-1；独立于实现，可并行）

```cpp
// desktop_spike.cpp —— 独立 50 行程序（不进 ECDI 仓库构建；VS 空工程或临时 cmake target）
// 判据：① Win+D 后仍可见 ② 切换应用/开始菜单/双屏 10 次层级稳定 ③ 桌面图标可点击（一票否决）
// 路线 A = reparent；路线 B = 注释切换（对照编译）
#define UNICODE
#define _UNICODE
#include <Windows.h>

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
	if (m == WM_DESTROY) PostQuitMessage(0);
	return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show){
	WNDCLASSW wc{ CS_HREDRAW|CS_VREDRAW, WndProc, 0,0, inst, nullptr,
		LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"DesktopSpike" };
	RegisterClassW(&wc);
	HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE, wc.lpszClassName,
		L"DesktopSpike", WS_POPUP, 100, 100, 400, 300, nullptr, nullptr, inst, nullptr);

	// ── 路线 A：reparent 到 WorkerW（路线 B：整段注释掉）──
	HWND progman = FindWindowW(L"Progman", nullptr);
	HWND defview = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
	HWND workerW = FindWindowExW(progman, defview, L"WorkerW", nullptr);
	if (workerW) SetParent(hwnd, workerW);   // A 路线核心

	ShowWindow(hwnd, show);
	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0)){ TranslateMessage(&msg); DispatchMessageW(&msg); }
	return 0;
}
```

> **判据 3 一票否决**（reparent 后图标不可点）→ 路线 A 否决 → 路线 B 或降级 `Bottom`（降级三步见初设 §6.4——公共 API 零变更）。**结果落 `desktopnest-roadmap.md`**。

---

## 6. 文件改动清单

| 类型 | 文件 | 改动 |
|---|---|---|
| 🆕 | `include/ECDI/Window/ChromeMode.h` | 82 头（初设 §2.1 全文） |
| 🆕 | `include/ECDI/Window/WindowLayer.h` | 83 头（初设 §2.2 全文 + D-DESK-1 注释） |
| 🆕 | `include/ECDI/Window/WindowState.h` | 84 头（初设 §2.5 全文） |
| 🆕 | `include/ECDI/EventSystem/Window/WindowStateChangedEvent.h` | 85 头（初设 §2.6 全文） |
| 🆕 | `src/Tests/WindowChromeTests.cpp` | **9 自动用例（T0–T7a）** + `TestWindow`（含 `Handle()`）/`PumpMessages`/`TestApp` 辅助（§5） |
| ✏️ | `include/ECDI/Platform/PlatformWindow.h` | +7 virtual + 类注释惯例段 + 2 include |
| ✏️ | `include/ECDI/Window/Window.h` | +7 公共方法 + 2 include |
| ✏️ | `include/ECDI/EventSystem/EventType.h` | +1 枚举 |
| ✏️ | `include/ECDI/EventSystem/EventRouter.h` | +1 虚方法 + 1 前置声明 |
| ✏️ | `src/EventSystem/EventRouter.cpp` | +1 include + 1 Dispatch |
| ✏️ | `src/Platform/Win32/Win32PlatformWindow.h` | +7 override + 状态成员 + **3 辅助声明** + 3 include |
| ✏️ | `src/Platform/Win32/Win32PlatformWindow.cpp` | +6 case + **10 方法实现**（7 override + 3 私有辅助） + Show 置位 + include/常量兜底 |
| ✏️ | `src/Tests/AnimationTests.cpp` | ⚠️ **实现期新增（v1.4 影响面补正）**：`TestPlatformWindow` 假实现补 7 个 no-op override——`PlatformWindow` 新增纯虚后该替身变抽象类，**不补即编译失败**（构建实测） |
| ✏️ | `src/Tests/ProgressBarTests.cpp` | ⚠️ **同上**（第二处 `TestPlatformWindow` 假实现）——全库共 2 处，已用 `grep 'public \w*PlatformWindow'` 确认无第三处 |
| ✏️ | `src/Tests/RunAllTests.h` / `.cpp` | 注册 `RegisterWindowChromeTests` |
| ✏️ | `CMakeLists.txt` | +1 行 `target_link_libraries(ECDI PUBLIC dwmapi)` |
| ✏️ | `ECDI/ECDI.vcxproj` | 4×`AdditionalDependencies` + 4×`ClInclude` + 1×`ClCompile` |

**零改动区（确认）**：`WindowMessageHandler.h/.cpp`（翻译器——4 个 NC 消息不进）、`Widget` 全部、`Layout` 全部、`Renderer`/`Theme` 全部、`PlatformApplication`、`examples/`。

---

## 7. 实施顺序（9 步）

1. 4 新头落盘（§2.1–§2.4）——含 BOM 校验
2. `EventType.h` / `EventRouter.h` / `EventRouter.cpp`（§2.7）
3. `PlatformWindow.h` / `Window.h` 头与类注释（§2.5/§2.6）+ `Window.cpp` 转发
4. `Win32PlatformWindow.h`（成员/override/辅助声明/include）→ `.cpp`（include/常量兜底/全部实现/6 case）
5. `CMakeLists.txt` + `ECDI.vcxproj`（§4）
6. `WindowChromeTests.cpp` + RunAllTests 注册（§5）
7. **编译**（MSVC 先行——用户 VS；四工具链按 A8 惯例跟进）
8. **跑测试**：183 预期全绿（174 既有零回归 + 9 新增）
9. 手测矩阵（§5.4）+ spike 并行（§5.5，结果回写 roadmap）

---

## 8. 验收清单

| # | 项 | 判据 |
|---|---|---|
| A1 | Public 头零平台泄漏 | `git status ECDI/include` 仅 4 新头；新头零 `Windows.h`/`HWND`/消息号 |
| A2 | 自包含测试 | `ecdi_public_header_test` 覆盖 85 头全绿 |
| A3 | 测试全绿 | `ecdi_tests` **183/183**（174 既有零回归 + 9 新增）；四工具链 |
| A4 | Normal 零回归 | Normal 模式四消息全部 `break` 走 DefWindowProc——T8 既有用例即证 |
| A5 | T4 不变量 | `ClientRectScreen ⊆ rcWork` 通过（D-COMP-1 无补偿验证） |
| A6 | 生命周期契约对称 | T7a（运行期 Show 前拒绝）+ 配置期 Show 后拒绝（既有 T 系列覆盖） |
| A7 | 手测矩阵 | §5.4 **七场景**通过（用户人工） |
| A8 | spike 产出 | 路线 A/B 判据表 + 结论落 `desktopnest-roadmap.md` |
| A9 | 文档回写 | 初设/需求状态行、`docs/README.md` 索引、`roadmap-deferred.md`（如产生新延期项） |
| A10 | **§3 代码块可编译性**（v1.2 新增——勘误④的制度化） | §3 全部代码片段通过「include 依赖 + API 签名 + **函数体完整性**」三级静态核对：无游离语句、无重复/残留片段、无未声明标识符 |

---

## 9. 修订记录

- v1.5（2026-09-12）**实施后缺陷修复：测试替身所有权越界（MSVC 构建暴露）**——✅ 已修并验证：
  - **现象**：用户运行 MSVC 构建的 `ecdi_tests` → `Application.cpp:92` 断言 `it != m_windows.end()`（`OnWindowDestroyed`）。
  - **根因**：§5.2 `TestWindow` 用 `std::make_unique<Window>(&a, ...)` 直接构造窗口 —— 全库仅两处构造 `Window`（`Application.cpp:45` = `Create()` 已登记；本测试未登记），其余窗口（ModelProbe / VisualTest / MinimalApp / 既有测试）全走 `Create()` 或裸 `CreateWindowExW`。⇒ **该断言在此前从未被触发**（一直是未执行过的防御）。
  - **为何 MinGW 绿而 MSVC 崩**：`FRAMEWORK_ASSERT` 仅 `#ifdef _DEBUG` 存在（`Core/ECDIAssert.h:22/38`）。`cmake-build-debug-mingw/build.ninja` 中 `_DEBUG` 出现 **0** 次；`cmake-build-debug-visual-studio/build.ninja` 中 `FLAGS = -DWIN32 -D_DEBUG`。**⇒ 「183/183 全绿（MinGW）」不等于断言通过——报绿必须写明该构建是否启用断言。**
  - **修订**：`TestWindow` 改持**非拥有** `Window*` = `&a.Create("ECDI_ChromeTest", 800, 600)`；析构改回显式 `~TestWindow(){ window->Release(); }`（只销毁 HWND）。⚠️ 与 v1.3 删除「冗余析构」的结论**不矛盾**——v1.3 的前提是「`unique_ptr` 拥有、`Window::~Window()` 会调 `Release()`」；改非拥有后该前提消失，显式 `Release()` 成为必需。**同时禁止**改回 `unique_ptr` 持有（`m_deferredDestroy` 二次析构）。
  - **验证**：MinGW + `-DCMAKE_CXX_FLAGS=-D_DEBUG`（`build.ninja` 中 `_DEBUG` 153 处，断言真正生效）→ `ecdi_tests` **183 passed, 0 failed, 183 total**，退出码 0 ⇒ **无第二个被断言层掩盖的缺陷**。
  - **附带修正**：§5.2 注释「800×600 **客户区**基准」→「800×600 **窗口**基准」（v1.3 已标注宜改但留待实施期；本次随该块回写一并修正——Normal 模式下客户区小于整窗）。
  - **遗留（另案上报，本版未动框架代码）**：`Application` / `Window` 所有权契约不对称 —— ① `Window` 构造器 public 且收 `Application*`（可为 `nullptr`）却只在 `Create()` 登记；② `OnWindowDestroyed` 断言「必在册」（`:92`）与其容错分支（`:94` 直接 `return`）自相矛盾；③ 销毁路径的 `m_deferredDestroy` 依赖消息循环（`Win32PlatformApplication.cpp:19`）而公开契约未声明；④ `PlatformWindowHost.h:15`「WM_DESTROY 框架层无需动作」已过期（与 `WindowMessageHandler.cpp:65` 冲突）。
- v1.4（2026-09-12）**实施期回写（4 处详设缺口，均由构建/测试实测暴露）——✅ 已实施，183/183 全绿**：
  - **① D-DWM-1 的「`#ifndef` 常量兜底」前提不成立**：`DWMWA_WINDOW_CORNER_PREFERENCE` 与 `DWMWCP_*` 在 MinGW-w64 `<dwmapi.h>` 中是 `DWMWINDOWATTRIBUTE` 枚举**成员**（= 33）与 `DWM_WINDOW_CORNER_PREFERENCE` 枚举定义——**不是宏**。`#ifndef <宏>` 永远为真 ⇒ 兜底块必与既有定义冲突。实测错误：`using typedef-name 'DWM_WINDOW_CORNER_PREFERENCE' after 'enum'` + 4 条 `DWMWCP_* conflicts with a previous declaration`。**定稿：删除兜底块，零兼容代码**（MSVC SDK 同为枚举）。
  - **② §3.4 类型名笔误**：`NCCALCSIZEPARAMS` → **`NCCALCSIZE_PARAMS`**（实测 `error: 'NCCALCSIZEPARAMS' does not name a type; did you mean 'NCCALCSIZE_PARAMS'?`）——两处（正文 + 代码）。
  - **③ 影响面遗漏：`PlatformWindow` 新增纯虚 → 测试替身必须同步**。全库有 **2 个** `TestPlatformWindow final : public PlatformWindow` 假实现（`AnimationTests.cpp:23`、`ProgressBarTests.cpp:29`）——不补 7 个 override 即 `cannot declare variable to be of abstract type`。§6 清单补 2 行；已用 `grep 'public \w*PlatformWindow'` 全库确认无第三处（另两处命中为 `PlatformWindowHost` 与 `Win32PlatformWindow` 本体）。**教训（已入 skill）：给接口加纯虚函数时，「实现者清单」必须全库 grep——包括测试替身，不只生产代码。**
  - **④ §5.1 include 缺口**：`TestWindow::Handle()` 三跳中的 ② 需要 `PlatformWindow` **完整类型**（`Window.h` 仅前置声明 → `invalid use of incomplete type`）⇒ 补 `#include "ECDI/Platform/PlatformWindow.h"`；另该文件需 `using namespace ECDI;`（`AntiAliasingTests.cpp:21` 同约定）。
  - **⑤ 实现细节校正**：`Show()` 实际为 `if (m_hwnd != nullptr) {`（详设片段写作 `if (m_hwnd)`）——实现按源码原样、仅插入 `m_shown = true;` 一行（最小 diff）。
  - **实测结果（MinGW g++ 16.x + CMake/ninja，`cmake-build-debug-mingw`）**：构建通过 → `ecdi_tests` **183 passed, 0 failed, 183 total**（174 既有零回归 + 9 新增 T0–T7a）。**跨工具链待办**：~~MSVC / Clang / ClangCL（A8）~~ ✅ 2026-09-12 用户实测确认；手测矩阵（A7）、Desktop spike（A8）。
- v1.3（2026-09-12）**AI 核验补正（1 处计数不一致 + 3 处清理）——可进入实施**：
  - **① §6 `.cpp` 行计数（实质不一致）**：v1.2 的「P2④ §6 清单同步」只改了 `.h` 行（2→3 辅助声明），`.cpp` 行仍写「+6 case + **8 方法实现** + Show 置位」——与 §3 实际给出的方法体数不符（§3.9 四个配置期 + §3.10 三个运行期 + §3.11 `ApplyDwmEnhancements` + §3.12 `DipToPixels` + §3.4 `AdjustMaximizedClientRect` = **10**）。已改为「+6 case + **10 方法实现（7 override + 3 私有辅助）** + Show 置位 + include/常量兜底」。
  - **②③ §5.2 清理**：`TestWindow::app` 死成员删除（从未被读——`Window` 构造时已接收 `Application*`）；`~TestWindow(){ window->Release(); }` 冗余删除（`Window::~Window()` 已调 `Release()`——`Window.cpp:120-124` 实核，且 `std::unique_ptr` 随后析构 `Window`；原写法幂等无害但属死代码），改为说明性注释。
  - **④ §5.1 include 补直连**：新增 `ECDI/Window/WindowState.h`（`TestApp::seen` 的 `std::vector<WindowState>`）与 `ECDI/EventSystem/EventRouter.h`（`TestApp` 的 override 目标）——二者原经 `WindowStateChangedEvent.h` / `Application.h` 传递可用，非硬错误；按本文档自身的「include 依赖核对」纪律改为直连，避免依赖链变动时静默失效。
  - **核验范围声明**：v1.3 仅做上述 4 项补正，**未改动任何设计内容**；v1.2 的 2 P1 + 4 P2 与 v1.1 的 2 P1 + 4 P2 均经源码级复核确认属实且修复正确（含 §5.2 `Handle()` 三跳路径、`Window.cpp:363` / `EventRouter.h:34` 行号、174 测试基线）。
- v1.2（2026-09-12）**内部复核修订（2 P1 + 4 P2）——修订完成，可进入实施**：
  - **P1 勘误④：§3.12 `DipToPixels` 残留代码删除**。v1.0 → v1.1 把中间量升 `long long` 时，**旧版实现尾巴未清理**——函数闭合 `}` 之后仍留着 `const int dpi = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(hwnd, hdc); return (dip * dpi + 48) / 96; }` 五行。后果有两重：**照抄即编译不过**（函数外游离语句 + 多余 `}`），且该残留正是 v1.1 P2 判定为「理论溢出」的旧写法，**与同节修订自相矛盾**。已删除，并在 §3.12 原位留勘误注。**制度化**：§8 新增 **A10**——§3 全部代码片段过「include 依赖 + API 签名 + **函数体完整性**」三级静态核对。
  - **P1 规格缺口：§5.2 补 `TestWindow::Handle()` 全文**。§5.3 的 T1/T2/T3/T5/T6/T7a **全部**需要 HWND（`GetWindowRect` / `SendMessageW(WM_NCHITTEST)` / `IsWindowVisible`），而 `Window` 刻意不暴露句柄（7.1.1 边界），v1.0/v1.1 的辅助结构体也没有访问器 ⇒ **照原文实施会当场卡壳**。已写死取回路径：`Window::GetPlatformWindow()`（`Window.h:86`）→ `PlatformWindow::GetRenderContext()` → `static_cast<const Win32RenderContext&>(...).GetHandle()`（`src/Platform/Win32/Win32RenderContext.h`，源码实核）。⚠️ 另注：本项目**既有窗口测试全部裸 `CreateWindowExW` 自建句柄**（RendererTests / AntiAliasingTests），从无「由 `Window` 对象取 HWND」的先例——故该路径必须写死，禁用 find-by-title 之类旁路。§5.1 include 清单同步加 `Platform/Win32/Win32RenderContext.h`。
  - **P2 内部不一致 ×4**：① §1.1 表 D-DPI-1 的「来源」仍写 `GetDpiForWindow(hwnd)`，与 §3.12 定稿 `GetDeviceCaps(hdc, LOGPIXELSX)` 冲突 ⇒ 表格回写并标注「相对初设倾向的偏离，理由见 §3.12」；② §1.1 表 D-TST-1 写「T2/T5/T6 共享一个 Borderless 窗口」，与 §5.3 三者配置（T2=32/8、T5=32/0、T6=0/0 与 -5/-8）冲突 ⇒ 改为「**每用例独立窗口**」并写明理由（共享会造成用例顺序依赖）；③ §3.2 只列 2 个私有辅助，而 §3.11 注又追加 `ApplyDwmEnhancements` ⇒ §3.2 补该声明、措辞改「三个内部辅助」；④ §6 清单未随 v1.1 同步 ⇒ 「8 自动用例」→「**9 自动用例（T0–T7a）**」、「+2 辅助声明」→「**+3 辅助声明**」。
  - **排版与表述修正（不改变语义）**：§3.6 `WM_NCACTIVATE` 两个 `if` 的闭合括号与缩进重排（原排版形似缺陷、极易被「顺手改坏」）；§5.3 标题「定稿 10 条」→「**11 行 = 9 自动用例（T0–T7a）+ 2 非自动项（T7b/T8）**」；T7 行的捕获方式与节末注统一为 `TestApp` 子类化（并补注：`OnWindowStateChanged` 在 `EventRouter` 中为 `protected`，override 必须留在 `protected` 区——`EventRouter.h:34` 源码实核）；§8 A7「六场景」→「**七场景**」（§5.4 实为 7 行）。
  - **核对未变的结论**：测试基线 **174** 条经**实测**确认（`cmake-build-debug-mingw/ecdi_tests.exe` → `174 passed, 0 failed, 174 total`）⇒ 落地后 **183/183** 的预期自洽。（旁注：按 `grep 'Add("'` 会数到 184——`EventTests.cpp` 内有 10 处是**局部** `TestRegistry` 的自测，非全局注册；基线数字无问题。）
- v1.1（2026-09-12）**外部评审全采纳（2 P1 + 4 P2）——PASS，可进入实施**：
  - **P1 D-CHROME-1：`SetChromeMode` 一次确定语义**（评审 §二）：v1.0 实现存在状态失同步漏洞——`SetChromeMode(Borderless)` 后 `SetChromeMode(Normal)` **不派发 `SWP_FRAMECHANGED`**（frame 未恢复），且 `m_borderlessApplied` 残留 `true`，第三次 `Borderless` 调用被幂等分支**错误拒绝** ⇒ 逻辑状态与实际 frame 脱钩。**采纳评审方案 A**（ChromeMode 一次确定：重复调用一律 Warning + 忽略；新增 `m_chromeConfigured` 取代 `m_borderlessApplied`），否决方案 B（配置期动态重配置需双向 frame 重算状态机——无消费者，YAGNI）。同步 §3.2 成员替换；**新增 T0 用例**（一次确定的回归锚），自动用例 8→9。
  - **P1 `WM_NCACTIVATE` 最小化态放行**（评审 §三）：MSDN 注明 minimized 窗口对该消息有特殊处理路径，`lParam = -1` 抑制重绘模式**不覆盖该状态**。**修订**：Borderless 分支内加 `if (IsIconic(hwnd)) break;`——最小化态走 `DefWindowProc` 正常处理。
  - **P2 ×4**：① `DipToPixels` 中间量升 `long long`（公共 API 直收 int，`dip * dpi` 理论溢出；不做人为截断）；② 「未来零改动」措辞收紧为「换算点唯一 + 仅替换 DPI 来源」（§3.12）；③ T3 选点动态化——`GetSystemMetrics(SM_CYCAPTION)+SM_CYSIZEFRAME+SM_CXPADDEDBORDER` 合成探测点，去掉 20px 硬编码，且**只断言 `resultA != resultB`**（不断言具体 HT 值——解除对系统主题/DPI 的隐含绑定）；④ 手测矩阵新增 **Bottom + Maximize/Restore**（Bottom 持续维护与最大化 z 序的交互）。
  - **用例数变化**：自动用例 8→**9**（+T0）；落地后预期 `ecdi_tests` **183/183**（174 既有零回归 + 9 新增）。
- v1.0（2026-09-12）详细设计初稿：① **§1.1 初设 §8 的 9 个开放决策点全部形成实施决策**（D-COMP-1 不补偿 / D-HIT-1 已落码 / D-DWM-1 常量兜底 / D-SPK-1 spike 并行 / D-TST-1 测试窗口策略——T1–T3/T5/T6 不 Show 零闪窗 / D-DWM-2 margin 1px 起步+标定观察项 / D-SEAM-1 能力扩展三步惯例 / D-DPI-1 GetDeviceCaps 方案 / D-DESK-1 语义状态≠实现路径）；② **§1.2 三处初设勘误**（Logger 宽字面量 / `windowsx.h` / `Logger.h` include——源码逐文件核对所得，「照抄初设即编译不过」缺陷第二次出现，立编译级核对纪律）；③ §2 头定稿（4 新头零改动采纳 + WindowLayer 加 D-DESK-1 注释 + PlatformWindow 类注释惯例段全文 + EventRouter.cpp Dispatch 全文）；④ §3 平台实现全文（6 case + 8 方法 + Show 置位 + DipToPixels——**D-DPI-1 实现期偏离初设倾向**：`GetDpiForWindow` → `GetDeviceCaps(LOGPIXELSX)`，理由消除 `_WIN32_WINNT` 四工具链宏风险，单一函数体替换点保留）；⑤ §4 双构建系统（CMake 1 行 + vcxproj 4×依赖 + 4×头 + 1×测试）；⑥ §5 测试定稿（10 条含 T7a/手测矩阵/spike 程序全文；**T7 实现期修正**——Application 无法注入 router，改子类化 TestApp）；⑦ §6 文件清单（5 新建 + 10 修改 + 零改动区）；⑧ §7 实施顺序 9 步；⑨ §8 验收 A1–A9。待评审。
