# Phase 12 WindowChrome 初步设计（v1.3）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-11（v1.1 同日外部评审修订）／2026-09-12（v1.2 第二轮、v1.3 第三轮外部评审修订）
> 状态：**v1.3 外部评审 PASS（可进入详细设计）**——"前置小修 2 P1" 已完成
> 前置：`phase12-windowchrome-requirements.md` **v1.2**（外部评审通过——可进初设）
> 一句话：把「保留 WS_OVERLAPPEDWINDOW + 拦截四个 NC 消息」落成「Public 头全文草案 + 消息拦截分层归属 + 最大化客户区处理 + R9 能力式接缝骨架 + R10 双档语义」——工程细节归详设
> v1.1 修订：8 项评审意见全采纳（2 必改 + 4 建议拍板 + 2 措辞收紧）——**API 统计口径 / 配置期与运行期分组 / `WindowState` 独立成头（84→85）/ Bottom 语义收紧 / Desktop 纯语义不绑实现 / T2·T3 测试目的修正 / DWM 参数归实现细节 / dwmapi PUBLIC 限定静态库模型**
> **v1.2 修订（第二轮评审——1 P0 + 3 P1 + 4 P2 全采纳）**：① **P0 §3.4**——删去方向错误的 `dx/dy` 对称补偿，改为「`rcWork` 唯一基准 + 补偿只能正内缩」双不变量（附数值实证）；② P1 配置期判据由 `IsWindowVisible` 改为 `m_shown`（`Show()+Hide()` 后二者分歧）；③ P1 `SetChromeMode` 补 `m_hwnd` 防御并明确「构造即建 HWND」前提；④ P1 最大化态 NCHITTEST 的 `IsZoomed` 门控落入代码（原仅见于文字）；⑤ P2 T1 明确坐标系不可直接比较 / T7b 新增非 API 路径手测 / §3.5 收紧「1px = 阴影」表述 / §2.4 清理重复残块（代码围栏失衡）
> **v1.3 修订（第三轮评审——2 P1 + 2 P2 全采纳）**：① **P1 运行期 API 生命周期闭合**——`Minimize`/`Maximize`/`Restore` 补 `if (!m_shown)` → Warning + 忽略，与配置期四件套**对称**（原「Show 前调用无意义、系统会忽略」的说法既不准确也未实现）；② **P1 T3 断言逻辑修正**——「同坐标一律不同」在角点必然失败，改为「预期不同 / 预期相同」两类点；③ P2 DIP→像素的**换算点与 DPI 来源**列为详设必锁项（§8 决策点 8）；④ P2 `Desktop` **语义状态 ≠ 实现路径**正式写成契约（§8 决策点 9）。另：修正 §9.2 生命周期表**表头与分隔线挤在同一行**的 markdown 缺陷。

---

## 1. 范围映射（R → 设计域）

| 需求 | 设计域 | 本文档节 | 初设是否给全文 |
|---|---|---|---|
| R1 Chrome 模式 API | `ChromeMode` 枚举 + Window API（配置期三件套） | §2.2 / §2.4 | ✅ |
| R1 切换时机（决策 7） | **运行时切换 → 降级为配置期**（初设拍板） | §9.1 | ✅ 拍板 |
| R2 无边框客户区 | `WM_NCCALCSIZE` 拦截 | §3.2 | ✅ |
| R3 命中测试 | `WM_NCHITTEST` 九宫格 + 单位换算 | §3.3 | ✅ |
| R4 最大化修正 | 客户区处理（`MONITORINFO.rcWork` 为唯一基准 + 补偿不变量） | §3.4 | ✅ 实现全文（补偿量**归详设标定**——v1.2 修订） |
| R5 CaptionBar 范围 | 不做 Widget——仅验证「应用自绘标题栏可行」 | §7.4 | ✅ |
| R6 DWM 增强 | `DwmExtendFrameIntoClientArea` + 圆角属性 + 失败容忍 | §3.5 | ⚠️ 参数待实测（§9.3） |
| R7 窗口状态 API 与事件 | `Minimize/Maximize/Restore` + `WindowStateChangedEvent` | §2.5 / §2.6 | ✅ |
| R8 兼容性底线 | `WM_NCACTIVATE` 防闪烁 + IME/DPI 不回归 + 手测矩阵 | §3.6 / §7.5 | ⚠️ 手测项归实现期 |
| R9 平台消息扩展接缝 | **能力式接缝骨架**（本阶段不实现托盘/拖放本体） | §4 | ✅ 骨架全文 |
| R10 窗口层级能力 | `WindowLayer` + `Bottom` 实现 + `Desktop` spike 前置 | §2.3 / §3.7 / §6 | ⚠️ Desktop 只定语义（§9.4） |
| — 链接库传播 | `dwmapi` PUBLIC 分析 | §5 | ✅ |
| — 测试 | 对照断言 / 九宫格 / rcWork / 手测矩阵 | §7 | ✅ 方向 |

**一句话判据**：本阶段「框架负责窗口**行为**，应用决定 UI」——框架交付 **7 个 Window 公共 API + 1 个事件**，**零新 Widget**。

### 1.1 API 统计口径（v1.1 新增——评审 §1）

为避免后续 changelog / 文档回溯时出现「7 个 API 怎么算」的歧义，此处**明确定义计数边界**：

```text
「7 个 API」= Window 公共 API 的**方法**数量，且带分组语义：

  配置期（创建后 / Show() 前调用）──────────── 4 个
    SetChromeMode()      SetCaptionHeight()
    SetResizeInset()     SetWindowLayer()

  运行期（Show() **之后**有效）──────────────── 3 个
    Minimize()           Maximize()           Restore()

总计 4 + 3 = 7
```

**不计入 7 个的**：

| 项 | 说明 |
|---|---|
| `ChromeMode` / `WindowLayer` / `WindowState` 枚举 | 是类型定义，不是 API |
| `WindowStateChangedEvent` | 是事件，不是 API（计数口径里单列为「+1 个事件」） |
| `EventType::WindowStateChanged` / `EventRouter::OnWindowStateChanged` | 事件系统的配套扩展点，随事件走 |
| `PlatformWindow` 的 7 个 virtual | **平台抽象层接口**——与 Window 公共 API 一一对应但**不单独计数**（同一能力的两个层次，重复计数会虚增） |

**依赖方向澄清（v1.1 补强）**：`PlatformWindow.h` 引用 `Window/ChromeMode.h` 与 `Window/WindowLayer.h` 属**公共头之间的引用**，不构成库边界破坏——这两个枚举头是**纯词汇枚举**（无 class 依赖、无前置声明、不 include 任何东西），任何层都可引用（与 `EventSystem/Input/KeyBoard/KeyCode.h` 被 `Platform/` 引用同例）。

---

## 2. Public 头全文草案

### 2.1 新增：`include/ECDI/Window/ChromeMode.h`（82 头）

```cpp
﻿#pragma once

namespace ECDI{

/// @brief 窗口 chrome 形态（Phase 12 R1）
/// @details 只描述「系统边框/标题栏是否保留」这一件事——
/// 标题栏高度、缩放热区宽度是**独立可调参数**（见 Window::SetCaptionHeight / SetResizeInset），
/// 不塞进枚举（否则组合爆炸：Borderless 小标题栏 / Borderless 大标题栏…）。
enum class ChromeMode{

	/// @brief 系统标题栏 + 边框（默认——与 Phase 12 之前行为完全一致）
	Normal = 0,

	/// @brief 无边框：客户区扩满整窗，标题栏/边框由应用自绘
	/// @details 保留底层 WS_OVERLAPPEDWINDOW 样式——系统动画 / Aero Snap /
	/// 最小化动画 / Alt+Space 系统菜单全部保留（技术路线见需求 §2）。
	Borderless
};

}
```

### 2.2 新增：`include/ECDI/Window/WindowLayer.h`（83 头）

```cpp
﻿#pragma once

namespace ECDI{

/// @brief 窗口层级档位（Phase 12 R10——桌面常驻应用前置）
/// @details **本枚举只描述「窗口所处层级」这一语义事实**，
/// 与任何平台实现手段（WorkerW / Progman / SetParent / SetWinEventHook / SetWindowPos）
/// **完全解耦**——平台层如何达成该层级是实现的自由，公共 API 不做任何绑定。
/// 档位按 **Win+D 行为** 区分：Normal / Bottom 会被 Win+D 一起隐藏，Desktop 不会
/// （决策依据见 docs/desktopnest-roadmap.md §1.1）。
enum class WindowLayer{

	/// @brief 默认：普通窗口层，无特殊 z 序处理
	Normal = 0,

	/// @brief 普通窗口层的**底部位置**
	/// @details 语义为「框架**持续维护**该窗口处于非置顶普通窗口中的底部位置」——
	/// ⚠️ **不承诺**「绝对位于最底层」：Windows z 序是动态的，受激活、拥有关系、
	/// 系统策略以及第三方程序主动 SetWindowPos 影响；外部行为造成的**瞬时** z 序
	/// 变化不属于本契约的违反（框架会在下一次 z 序变更机会中重新维护底部位置）。
	/// Win+D 时本档窗口**与普通窗口一起被隐藏**。
	Bottom,

	/// @brief 桌面驻留层：被应用窗口覆盖，且 **Win+D 后仍保持可见**
	/// @details ⚠️ 实现路线待 spike 验证；spike 未通过前此档位不承诺可用——
	/// 调用后退化为 Bottom 语义并记 Warning 日志。
	/// **API 承诺与当前平台能力刻意解耦**：枚举值保留，未来 Windows 版本可行时
	/// 只需替换实现，公共 API 零变更。
	Desktop
};

}
```

> **设计澄清**：`WindowLayer` 与 `ChromeMode` 同为**正交**枚举，落在 `Window/` 而非 `Platform/`——它们是框架层的「窗口形态词汇」，平台层负责翻译成 HWND 行为。两个枚举各据一头文件（与 `KeyCode.h` / `MouseButton.h` 同风格：一个枚举一个文件，避免「什么都能往里塞」的杂物头）。

> **三层分工对照（v1.1 新增——评审 §5，Desktop 不绑实现的正式声明）**：
>
> | 层 | 内容 | 变更自由度 |
> |---|---|---|
> | **语义层**（公共 API） | `Normal` = 普通窗口层 / `Bottom` = 普通窗口层底部 / `Desktop` = 桌面驻留层（Win+D 后仍可见） | **稳定契约**——Windows 版本变化不改 |
> | **平台抽象层** | `PlatformWindow::SetWindowLayer(WindowLayer)` | 稳定（虚接口） |
> | **Win32 实现层** | `WM_WINDOWPOSCHANGING` + `HWND_BOTTOM` / `SetParent(WorkerW)` / `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` | **自由变更**——某版 Windows 改了 WorkerW 结构，只改这里 |
>
> 这条边界是本阶段 R10 最重要的设计成果：**`WindowLayer::Desktop` 的语义不依赖任何具体的 Win32 桌面层机制。**

### 2.3 修改：`include/ECDI/Platform/PlatformWindow.h`（虚接口扩展）

在既有 11 个虚方法后**追加**（既有方法零改动——纯扩展，无破坏）。

**分组与 Window 公共 API 一一对应**（配置期 4 + 运行期 3——见 §1.1 统计口径）：

```cpp
	// ── Phase 12：WindowChrome / 窗口层级（配置期——Window 构造后 / Show() 前）──

	/// @brief 设置窗口 chrome 形态（R1）
	/// @param mode 目标形态
	/// @details **仅配置期生效**（初设决策 D1，见 §9.1）——
	/// 派发到已显示窗口时记 Warning 日志并忽略（不抛异常）。
	virtual void SetChromeMode(ChromeMode mode) = 0;

	/// @brief 设置自定义标题栏高度（R3；逻辑坐标 DIP）
	/// @param height 标题栏高度（逻辑坐标；<= 0 视为 0——允许应用完全放弃 HTCAPTION 拖动区）
	virtual void SetCaptionHeight(int height) = 0;

	/// @brief 设置缩放热区宽度（R3；逻辑坐标 DIP）
	/// @param inset 四边/四角的命中测试宽度（逻辑坐标；<= 0 视为 0——完全禁用边缘缩放）
	virtual void SetResizeInset(int inset) = 0;

	/// @brief 设置窗口层级档位（R10）
	/// @param layer 目标档位
	/// @details Bottom/Desktop 档持续维护普通窗口层底部位置（Win32 实现经
	/// WM_WINDOWPOSCHANGING）；切回 Normal 时停止维护（不主动改变当前 z 序——
	/// 交系统自然演化）。
	virtual void SetWindowLayer(WindowLayer layer) = 0;

	// ── Phase 12：窗口状态（运行期——**Show() 之后**才有效）──────────
	// @pre 运行期契约（v1.3——评审 §三）：Show() 之前调用记 Warning 并忽略；
	//      与配置期四件套**对称**（配置期 = Show 前有效 / 运行期 = Show 后有效）。

	/// @brief 最小化窗口（R7）——薄封装 ShowWindow(SW_MINIMIZE)
	/// @pre Show() 之前调用记 Warning 并忽略（运行期 API——见初设 §9.2）
	virtual void Minimize() = 0;

	/// @brief 最大化窗口（R7）——薄封装 ShowWindow(SW_MAXIMIZE)
	/// @pre 同 Minimize
	virtual void Maximize() = 0;

	/// @brief 还原窗口（R7）——薄封装 ShowWindow(SW_RESTORE)
	/// @details 最小化态 → 还原到原尺寸；最大化态 → 还原到最大化前尺寸（系统语义）。
	/// @pre 同 Minimize
	virtual void Restore() = 0;
```

**include 追加**：`#include "ECDI/Window/ChromeMode.h"` + `"ECDI/Window/WindowLayer.h"`。

> ⚠️ **依赖方向检查**：`Platform/` → `Window/` 是新增的反向引用吗？**不是。** 依赖方向单向律约束的是 `src → include` 与 `include → src / Windows.h`；`include/ECDI/Platform/PlatformWindow.h` → `include/ECDI/Window/ChromeMode.h` 是两个公共头之间的引用，**不构成库边界破坏**。但为免除「Platform 认识 Window 层」的语义嫌疑，两个枚举头本身**不含任何 `Window` 类型**——它们是纯词汇枚举，无前置声明、无 class 依赖，任何层都可引用（与 `EventSystem/Input/KeyBoard/KeyCode.h` 被 `Platform/` 引用同例）。

### 2.4 修改：`include/ECDI/Window/Window.h`（公共 API 透传）

在 `GetPlatformWindow()` 之后追加。

**⚠️ v1.1 关键修订（评审 §3）**：明确划分为**配置期 / 运行期**两组 API，并**统一生命周期契约**——配置期的 4 个方法（含 `SetWindowLayer`）**全部必须在 `Show()` 之前调用**：

```cpp
		// ── Phase 12：WindowChrome（配置期——构造后 / Show() 前调用）──────

		/// @brief 设置窗口 chrome 形态（R1）
		/// @param mode Normal（系统标题栏）/ Borderless（客户区扩满整窗）
		/// @pre 必须在 Show() 之前调用（配置期——见初设 §9.1 决策 D1）；
		/// Show() 之后调用记 Warning 并忽略（不抛异常）
		void SetChromeMode(ChromeMode mode);

		/// @brief 设置自定义标题栏高度（R3；逻辑坐标 DIP）
		/// @pre 必须在 Show() 之前调用（同 SetChromeMode 生命周期契约）
		/// @details 仅 Borderless 模式有意义（Normal 模式系统标题栏不由本参数控制）；
		/// 决定 WM_NCHITTEST 中 HTCAPTION 命中区的高度（顶部起算）。
		void SetCaptionHeight(int height);

		/// @brief 设置缩放热区宽度（R3；逻辑坐标 DIP）
		/// @pre 必须在 Show() 之前调用（同 SetChromeMode 生命周期契约）
		/// @details 八向 resize 的命中宽度（四边 + 四角各向内 inset）。
		void SetResizeInset(int inset);

		/// @brief 设置窗口层级档位（R10）
		/// @pre 必须在 Show() 之前调用（与 chrome 三件套同组——配置期 API）
		/// @details 档位语义见 WindowLayer 定义；实现手段（WM_WINDOWPOSCHANGING /
		/// reparent / WinEventHook）为平台细节，公共 API 不承诺任何具体机制。
		void SetWindowLayer(WindowLayer layer);

		// ── Phase 12：窗口状态（运行期——**Show() 之后**才有效）────────

		/// @brief 最小化窗口——状态变化经 WindowStateChanged 事件回流
		/// @pre Show() 之前调用记 Warning 并忽略（运行期 API——见初设 §9.2）
		void Minimize();

		/// @brief 最大化窗口
		/// @pre 同 Minimize
		void Maximize();

		/// @brief 还原窗口（最小化态/最大化态通用）
		/// @pre 同 Minimize
		void Restore();
```

**为什么 `SetWindowLayer` 也归配置期（v1.1 说明）**：它虽然技术上可在运行期安全调用（改强制标志 + 一次 `SetWindowPos`），但**与其他三个配置项保持同一生命周期**能让 API 语义整齐——「窗口生命周期内的形态由创建期一次决定」是一条比「哪些能改哪些不能改」更容易记忆、更少误用的契约。若未来真出现运行期切层需求（如 DesktopNest 从托盘切换常驻），再单独立项放宽（API 签名零变更）。

**include 追加**：`#include "ECDI/Window/ChromeMode.h"` + `"ECDI/Window/WindowLayer.h"`。

### 2.5 新增：`include/ECDI/Window/WindowState.h`（84 头）

**v1.1 修订（评审 §2——采纳）**：`WindowState` **从事件头中拆出，独立成头**。

**理由（架构层级）**：`WindowState` 是 **Window 领域状态**，不是 Event System 的概念。若放在 `EventSystem/Window/WindowStateChangedEvent.h` 里，依赖关系会被写成：

```text
Window  →  EventSystem  →  WindowState          ❌ 状态被降格为事件的附属物
```

而实际正确的依赖方向是：

```text
Window        →  WindowState                     ✅ 状态是 Window 领域的基础概念
WindowStateChangedEvent  →  WindowState          ✅ 事件只是状态变化的通知机制
```

**修正后的依赖关系**：

```text
Window/WindowState.h                 （零依赖——纯词汇枚举）
      ↑                    ↑
      │                    │
Window/Window.h    EventSystem/Window/WindowStateChangedEvent.h
```

事件头 `#include "ECDI/Window/WindowState.h"` 即可，与 `WindowChrome` 的两个枚举头落在同一目录，**`Window/` 目录从 1 头变成 4 头**（`Window.h` + 三个词汇枚举）。

```cpp
﻿#pragma once

namespace ECDI{

/// @brief 窗口状态（Phase 12 R7）
/// @details 归 Window 领域而非 Event 领域——本枚举描述「窗口**处于什么状态**」
/// 这一基础事实，`WindowStateChangedEvent` 只是它的变化通知机制。
/// 依赖方向：`WindowStateChangedEvent` → `WindowState`（反向不成立）。
enum class WindowState{

	restored = 0,   ///< 还原态（普通态——最小化/最大化均未生效；也是窗口创建后的初始态）
	minimized,      ///< 已最小化
	maximized       ///< 已最大化
};

}
```

> ⚠️ **枚举值顺序说明**：`restored = 0` 而非 `minimized = 0`——因为**窗口创建后的初始状态就是 restored**，让默认值（零值）直接对应真实初始态，可避免 `Win32PlatformWindow::m_lastWindowState` 的默认成员初始化出现「默认值是 minimized 但实际窗口是 restored」的语义错位。三个值互斥（不会同时 minimized + maximized）。

### 2.6 新增：`include/ECDI/EventSystem/Window/WindowStateChangedEvent.h`（85 头）

```cpp
﻿#pragma once

#include "ECDI/EventSystem/Window/WindowEvent.h"
#include "ECDI/Window/WindowState.h"

namespace ECDI
{

/// @brief 窗口状态变化事件（R7）
/// @details 由 Window::Minimize/Maximize/Restore 引发的**系统实际状态**变化回流，
/// 消费者（应用自绘标题栏按钮 / 未来的 CaptionBar 控件）据此切换按钮形态。
///
/// ⚠️ 语义边界：本事件表示「**窗口状态已经是** X」这一既成事实——
/// 不是「请把窗口变成 X」的请求。触发源固定为系统状态变化，
/// 因此鼠标拖拽标题栏到屏幕顶部触发的最大化、Win+↑、Aero Snap 等
/// **非 API 路径**同样产生本事件（这是刻意的——状态同步必须覆盖全部来源）。
class WindowStateChangedEvent : public WindowEvent{

public:

	WindowStateChangedEvent(
		Window* window,
		WindowState state):
		WindowEvent(window),
		m_state(state){

	}

	static EventType StaticType(){

		return EventType::WindowStateChanged;

	}

	EventType GetType() const override{

		return StaticType();

	}

	/// @brief 获取变化后的窗口状态
	WindowState GetState() const noexcept{

		return m_state;

	}

private:

	WindowState m_state;
};

}
```

### 2.7 修改：`include/ECDI/EventSystem/EventType.h`

窗口事件组内、`WindowResized` 之后插入一行：

```cpp
	WindowResized,			///< 窗口大小变化
	WindowStateChanged,		///< 窗口状态变化（Phase 12 R7——minimized/maximized/restored）
```

### 2.8 修改：`include/ECDI/EventSystem/EventRouter.h`

窗口事件组内追加（`OnWindowResized` 之后），并新增前置声明：

```cpp
class WindowStateChangedEvent;
```

```cpp
	virtual void OnWindowStateChanged(
		const WindowStateChangedEvent& event
	){}
```
**对应的 `src/EventSystem/EventRouter.cpp`**：追加 include + Dispatch 一条（与既有五条同构）。

### 2.9 修改：`include/ECDI/Application/Application.h`（可选接缝）

`EventRouter` 新增虚方法后，`Application` **无需** override（默认空实现即「不关心」）。若 Phase 12 的 demo/VisualTest 需要，由应用侧自行 override——**框架不代为透传**（YAGNI：无框架内部消费者）。

---

## 3. 实现分解（`src/Platform/Win32/`）

### 3.0 分层归属裁决（关键——需求 §7 影响面表述需修正）

需求 §7 写作「Win32PlatformWindow + **WindowMessageHandler**（NCCALCSIZE/NCHITTEST/NCACTIVATE 拦截…）」。**初设裁决：这四个消息不进 `WindowMessageHandler`。**

| 消息 | 归属 | 理由 |
|---|---|---|
| `WM_NCCALCSIZE` / `WM_NCHITTEST` / `WM_NCACTIVATE` / `WM_WINDOWPOSCHANGING` | **`Win32PlatformWindow::HandleMessage` 状态同步区** | 它们是**窗口自身行为**，不产生任何 Framework Event——翻译器职责是「Win32 消息 → Event」（7.1.2 职责纯粹化），把非事件消息塞进去会破坏该边界 |
| 现有 `WM_PAINT` / `WM_SIZE` / `WM_EXITSIZEMOVE` / IME | 同上（既有） | 同层同类——「平台层状态同步区」已有先例 |
| `WM_CLOSE` / 鼠标 / 键盘 / `WM_TIMER` | `WindowMessageHandler`（不变） | 产生 Event |

**新增文件**：无。全部落在既有 `Win32PlatformWindow::HandleMessage` 的 switch 内，新增 4 个 case（`WM_NCCALCSIZE` 因需在**消息到达早期**返回，位置在 `WM_PAINT` 之前）+ `WM_WINDOWPOSCHANGING`。

> 需求 §7 影响面表的「WindowMessageHandler」措辞应在详设/实现阶段回写修正。**本初设已裁决，不需上升为开放决策点。**

### 3.1 新增状态成员（`Win32PlatformWindow.h`）

```cpp
	// ── Phase 12：chrome / 层级状态 ──────────────────────────────

	ChromeMode m_chromeMode = ChromeMode::Normal;	///< chrome 形态（R1——仅配置期生效，Show 后拒绝切换）
	int m_captionHeight = 32;	///< 标题栏命中高度（R3；逻辑坐标 DIP——默认 32）
	int m_resizeInset = 8;	///< 缩放热区宽度（R3；逻辑坐标 DIP——默认 8）
	WindowLayer m_windowLayer = WindowLayer::Normal;	///< 层级档位（R10）

	bool m_shown = false;	///< 是否已调用过 Show()（配置期判据——v1.2：区别于「当前是否可见」）

	bool m_borderlessApplied = false;	///< Borderless 是否已 application（防重复派发 SWP_FRAMECHANGED）
	bool m_inSizeMove = false;	///< 是否处于拖动/缩放循环（WM_ENTERSIZEMOVE~WM_EXITSIZEMOVE）
```

**边界检查（构造期）**：`m_captionHeight` / `m_resizeInset` 为负时 clamp 到 0（契约：「<= 0 视为 0」）。

**配置期判据 `m_shown`（v1.2 新增——评审 P1）**：4 个配置期 API 的「是否仍在配置期」判据统一为 `m_shown`，**在 `Win32PlatformWindow::Show()` 内置位**：

```cpp
void Win32PlatformWindow::Show() {

	if (m_hwnd) {

		m_shown = true;   // 配置期 → 运行期 的分界线（与 Window::Show() 一一对应）

		ShowWindow(m_hwnd, SW_SHOW);

	}

}
```

> **为什么不复用 `IsWindowVisible`（v1.1 的做法）**：二者**不等价**。`Show()` → `Hide()` 之后 `IsWindowVisible()` 为假，但契约上「已调用过 `Show()`」已成立——v1.1 用可见性做判据，会出现「契约说 Show 后拒绝、实现却仍允许」的不一致。
>
> **它不是「第二真相源」**：`m_shown` 表达的是「**框架 API 是否调用过 `Show()`**」，而 `IsWindowVisible` 表达的是「**系统当前是否可见**」——**这是两个不同的事实**，不是同一事实的两份副本。第二真相源禁令针对的是后者被复制（如自行维护「现在可见吗」），本项不属该禁令范围。

### 3.2 R2：`WM_NCCALCSIZE`

```cpp
	case WM_NCCALCSIZE:

		// 仅 Borderless 且 wParam == TRUE（客户区矩形需重算）时拦截。
		// wParam == FALSE 时 lParam 是 RECT 而非 NCCALCSIZEPARAMS——不能解释（必须走 DefWindowProc）。
		if (m_chromeMode == ChromeMode::Borderless && wParam == TRUE){

			// 非最大化：客户区 = 整窗 → 返回 0 且不改 rect（系统按 rect 直接采用）
			if (!IsZoomed(hwnd)){

				return 0;

			}

			// 最大化：系统会把窗口外扩一圈（边框 + 阴影），客户区若原样采用会盖住任务栏。
			// 按 rcWork 收缩（R4——细节见 §3.4）
			AdjustMaximizedClientRect(hwnd,
				reinterpret_cast<NCCALCSIZEPARAMS*>(lParam)->rgrc[0]);

			return 0;

		}

		break;   // Normal 或 wParam == FALSE → DefWindowProc
```

> ⚠️ **`wParam == FALSE` 必须放行**：该分支下 `lParam` 指向的只是一个 `RECT`，按 `NCCALCSIZEPARAMS` 解释会读越界。

### 3.3 R3：`WM_NCHITTEST` 九宫格

```cpp
	case WM_NCHITTEST: {

		// Normal 模式不干预（系统标题栏/边框行为完全不变——零回归底线）
		if (m_chromeMode != ChromeMode::Borderless){

			break;

		}

		// 屏幕坐标 → 客户区坐标（NCHITTEST 的 lParam 是屏幕坐标）
		POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };

		RECT rcWin{};

		GetWindowRect(hwnd, &rcWin);

		const int x = pt.x - rcWin.left;

		const int y = pt.y - rcWin.top;

		// 逻辑坐标 → 设备像素（**当前 DPI 缩放未落地 = 1:1**；公共 API 语义恒为 DIP，不受未来 DPI 落地影响）
		// ⚠️ v1.3（评审 §八）：换算点在此，但 **DPI 来源尚未锁死**——详设必须拍板：
		//   ① 用 `GetDpiForWindow(hwnd)`（窗口所在显示器）？还是鼠标所在显示器？还是未来统一 DPI 服务？
		//   ② 窗口跨 DPI 显示器、且鼠标位于**另一台**显示器时的命中规则。
		//   见 §8 开放决策点 8（v1.3 新增）。
		// 公共 API 语义不变——R3 §单位契约）
		const int inset = m_resizeInset;

		const int caption = m_captionHeight;

		const int w = rcWin.right - rcWin.left;

		const int h = rcWin.bottom - rcWin.top;

		// 四角优先（角命中优先级高于边——否则角落会被边的判定吃掉）
		// 最大化态：系统不会进入 resize 循环，返回 HTLEFT/HTTOP 等会让人误以为可拖宽——
		// 故四边四角 resize 判据整体跳过（v1.2：把 §契约要点 第 2 条落实进代码，避免「文字说了代码没体现」）。
		// ⚠️ caption 判据**不走这条门**——最大化时仍允许从顶部往下拖还原（系统行为）。
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

**契约要点**：

- 命中顺序 `四角 → 四边 → 标题栏 → 客户区` 是**不可调换**的——否则 8px 角落在 `caption` 区内会被判成 `HTCAPTION`（顶部两角将无法缩放）
- 最大化态**不应返回 caption 区以外的 resize 命中**：系统在最大化时不会进入 resize 循环，返回 HTLEFT 等值会让人误以为能拖宽。**处理（v1.2 已落入代码）**：`resizable = !IsZoomed(hwnd)` 统一门控四边四角判据；`caption` 判据**不走该门**（最大化时仍允许从顶部往下拖还原——系统行为）。
- `WM_NCHITTEST` 对触摸/笔输入同样生效——无需额外处理

### 3.4 R4：最大化客户区处理（`rcWork` 为唯一基准）

**v1.2 关键修订（外部评审 P0——必须修）**：v1.1 曾以「`rcWork` 为基准 + 用 `rcWindow` 与 `rcMonitor` 的偏移做对称补偿」给出算法全文。**该补偿的方向是反的。**

最大化窗口的 `rcWindow` 被系统撑到 `rcMonitor` 之外（`rcWindow.left < rcMonitor.left`），故 `dx = rcWindow.left - rcMonitor.left` **恒为负**；代入 `rcClient.left = rcWork.left + dx` 会使客户区**向左越出工作区**，`rcClient.right = rcWork.right - dx` 同理向右越出。**它实际是「扩大」而非「收缩」，直接违反 `ClientRectScreen ⊆ rcWork` 不变量。**

**数值实证**（1920×1080 显示器 / 任务栏 40px / 边框 8px）：

| 量 | 值 |
|---|---|
| `rcMonitor` | (0, 0, 1920, 1080) |
| `rcWork` | (0, 0, 1920, 1040) |
| 最大化 `rcWindow` | (−8, −8, 1928, 1048) |
| `dx` / `dy` | **−8 / −8** |
| v1.1 公式产出 `rcClient` | **(−8, −8, 1928, 1048)**——1936×1056，**比整块显示器还大** |
| 不变量 `⊆ rcWork` | **★违反★**（四边全部越出） |
| 仅取 `rcClient = rcWork` | (0, 0, 1920, 1040)——**满足** |

**v1.2 定案**：初设**只锁两件事**，不再给「补偿算法全文」。

- **① 唯一基准（锁死）**——客户区以 `MONITORINFO.rcWork` 为基准。`rcClient = mi.rcWork` 本身即满足 `ClientRectScreen == rcWork`，**无需任何补偿**。
- **② 补偿不变量（锁死）**——若详设真机实测发现仍需补偿，**补偿只能让客户区从 `rcWork` 进一步收缩，绝不允许扩大到 `rcWork` 之外**。形式只能是「加正内缩量」（`ix` / `iy` 必须 `>= 0`）：

  ```cpp
  rcClient.left   += ix;   // 只能让窗口更小
  rcClient.top    += iy;
  rcClient.right  -= ix;
  rcClient.bottom -= iy;
  ```

- **③ 归详设**——**是否需要补偿 / 补偿多少 / X-Y 是否对称 / 混合 DPI 下是否对称**，一律交详设真机标定（§8 开放决策点 1）。

```cpp
void Win32PlatformWindow::AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient){

	// R4 验收基准：客户区不覆盖任务栏、不残留系统边框空白。
	// ⚠️ 不能简单用 rcMonitor（V1.2 评审 §10 明令禁止——rcMonitor 含任务栏区，
	// 客户区会盖住任务栏）；必须用 rcWork。
	MONITORINFO mi{};

	mi.cbSize = sizeof(MONITORINFO);

	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)){

		return;   // 查询失败：保持系统原值（fail-safe——宁可留系统空白也不盖任务栏）

	}

	// 客户区 = 显示器工作区（屏幕坐标）。
	// 最大化时系统已把 rcWindow 撑到 rcWork 之外（按边框量外扩）；此处直接把客户区取为
	// rcWork，即得「可见范围 == 工作区」。**无需补偿，也就不会出现方向性错误。**
	rcClient = mi.rcWork;

	// ⚠️ v1.2：此处刻意**不引入**任何基于 (rcWindow - rcMonitor) 差值的补偿。
	//    最大化时该差值为负，任何「正负代入」的对称补偿都会把客户区推出 rcWork。
	//    若详设真机标定确认需要补偿，必须用「正内缩量」且只能收缩（见本节 ②）。

}
```

> **纪律条款（v1.2 新增）**：`ClientRectScreen ⊆ rcWork` 是**不变量**，不是「验收倾向」。详设若引入补偿量，必须给出真机实测数据（含混合 DPI 场景）并证明补偿后仍满足该不变量。**不得为「让初设看起来完整」而提前锁死未经验证的补偿公式。**

### 3.5 R6：DWM 增强

**v1.1 定位修订（评审 §11）**：本节内容**整体属于「Win32 MVP 实现细节」**，不是 API 层语义。契约层只承诺两件事：

```text
契约层（公共 API 承诺——稳定）
  WindowChrome 启用时：保留系统级视觉层次/阴影；Win11 上叠加系统圆角
  DWM 不可用 / 属性不被支持 → 仅记日志，功能完整（优雅降级）

实现层（Win32 MVP——可变）
  DwmExtendFrameIntoClientArea + MARGINS{1,1,1,1}   ← 具体数值是参数，不是契约
  DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)
  ↓ 若某版 Windows 表现不同 → 只改这里，公共契约零变更
```

**「minimal frame extension」**是本项的语义描述；`MARGINS{1,1,1,1}` 只是当前 Win32 实现选取的具体参数（详设按真机视觉实测标定，可能微调）。

```cpp
void Win32PlatformWindow::ApplyDwmEnhancements(HWND hwnd){

	// R6 失败容忍契约：DWM 不可用/属性不被支持 → 仅日志 Warning，绝不中断。
	// （老系统优雅降级：视觉层次缺失，功能完整）
	// ⚠️ 本函数整体为「Win32 Desktop Layer implementation strategy」——
	// 下方全部数值参数（1px / DWMWCP_ROUND）属实现细节，不是公共 API 语义。

	// ① 保留系统阴影/层次：minimal frame extension
	//    （全 0 会失去系统阴影；过大则玻璃延伸进客户区——当前 Win32 MVP 倾向取 1px。
	//     ⚠️ v1.2 措辞收紧（评审 §18）：不宣称「1px = 阴影」是普适数学关系——DWM frame
	//        extension 在不同 Windows 版本/窗口状态/视觉效果下并非固定映射。1px 只是本实现
	//        **当前选取的参数**，最终值由详设真机视觉实测确定，可能微调。）
	MARGINS margins{ 1, 1, 1, 1 };

	const HRESULT hrExtend = DwmExtendFrameIntoClientArea(hwnd, &margins);

	if (FAILED(hrExtend)){

		Logger::Log(LogLevel::Warning, "WindowChrome: DwmExtendFrameIntoClientArea failed (hr=0x...)");

	}

	// ② Win11 圆角：DWMWA_WINDOW_CORNER_PREFERENCE（build 22000+）
	//    ⚠️ Win10 及更早返回 E_INVALIDARG——被失败容忍吸收
	DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_ROUND;

	const HRESULT hrCorner = DwmSetWindowAttribute(hwnd,
		DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));

	if (FAILED(hrCorner)){

		Logger::Log(LogLevel::Warning, "WindowChrome: corner preference unsupported (hr=0x...)");

	}

}
```

**调用时机**：`SetChromeMode(Borderless)` 应用样式变更之后调用一次。

**⚠️ 常量可用性问题（详设必须验证）**：

- `DWMWA_WINDOW_CORNER_PREFERENCE` / `DWM_WINDOW_CORNER_PREFERENCE` / `DWMWCP_ROUND` 定义在 **Windows 11 SDK 的 `dwmapi.h`**——MinGW 头可能缺失
- **应对（预案）**：若 MinGW 缺失，本文件内自定义常量兜底（`DWM_WINDOW_CORNER_PREFERENCE` 是属性枚举，值固定；不改 `dwmapi.lib` 的链接方式）——归详设实测（§9.3）

### 3.6 R8：`WM_NCACTIVATE` 防闪烁

```cpp
	case WM_NCACTIVATE: {

		if (m_chromeMode != ChromeMode::Borderless){

			break;

		}

		// 防闪烁标准做法：以 lParam = -1 调 DefWindowProcW 表示「不重绘非客户区」，
		// 但保留其返回值的语义（激活状态变更的内部处理照常执行）。
		// 这是 MSDN 记录的模式——用于无边框窗口在激活切换时避免标题栏残影。
		const LRESULT result = DefWindowProcW(hwnd, WM_NCACTIVATE, wParam, -1);

		return result;

	}
```

> ⚠️ `lParam = -1` **只抑制重绘**，不改变激活语义（`wParam` 仍被系统内部使用）。返回值必须原样透传——不能返回固定 `TRUE`（会破坏系统对激活链的维护）。

### 3.7 R10：`WM_WINDOWPOSCHANGING` 持续维护普通窗口层底部位置

**v1.1 措辞修订（评审 §4）**：不再叫「持续强制置底」——`HWND_BOTTOM` 的语义是「置于**非 topmost 窗口**的底部」，**不是**「永远位于整个 Windows 桌面的最底层」。措辞与契约都按此收紧。

```cpp
	case WM_WINDOWPOSCHANGING: {

		// Bottom / Desktop 档：持续维护普通窗口层中的底部位置
		// ⚠️ 必须是「持续维护」而非一次性 SetWindowPos（R10 决策依据——
		// 其他程序会把我们顶下来；且系统自身在激活/拥有关系变化时也会改 z 序）
		// ⚠️ 只改 hwndInsertAfter，不碰 x/y/cx/cy/flags（否则会干扰最大化/还原几何）
		// ⚠️ 契约边界：本处理只保证「每次 z 序变更机会都把窗口放到底部」——
		// 不承诺阻止第三方程序主动 SetWindowPos 造成的瞬时 z 序变化
		// （Windows z 序是动态的；那种瞬时变化不构成本契约的违反）
		if (m_windowLayer != WindowLayer::Normal){

			auto* wp = reinterpret_cast<WINDOWPOS*>(lParam);

			if ((wp->flags & SWP_NOZORDER) == 0){

				wp->hwndInsertAfter = HWND_BOTTOM;

			}

		}

		break;   // 走 DefWindowProc（几何变更仍由系统处理）

	}
```

> **Desktop 档在 spike 通过前走同一分支**（降级为 Bottom 语义），并在 `SetWindowLayer(Desktop)` 时记一次 Warning。spike 通过后，Desktop 档在该分支基础上叠加实现层路径（reparent 或 WinEventHook）——**该路径属「Win32 Desktop Layer implementation strategy」，写进实现文档而非公共契约**（§9.4）。
>
> **实现层与语义层的措辞对照（v1.1 新增）**：
>
> | | 措辞 |
> |---|---|
> | ❌ 不用 | 「持续强制置底」「永远位于最底层」 |
> | ✅ 使用 | 「持续维护该窗口处于非置顶普通窗口中的底部位置」 |
>
> 这条措辞纪律的意义：未来若某第三方程序 `SetWindowPos(HWND_TOP)` 让窗口短暂浮起，**按契约不算 ECDI 违约**——避免契约被实现细节绑架。

### 3.8 R10：`SetWindowLayer` 实现

```cpp
void Win32PlatformWindow::SetWindowLayer(WindowLayer layer){

	// 配置期契约（v1.1——评审 §3）：与 chrome 三件套同一生命周期，必须在 Show() 前调用。
	// 理由：让「窗口形态由创建期一次决定」成为一条统一契约（比「哪些能改哪些不能改」
	// 更易记忆、更少误用）。API 签名不因此锁死——未来若出现运行期切层需求，
	// 只需松开此判据（零签名变更）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			"WindowChrome: SetWindowLayer ignored after Show() - layer is config-time only");

		return;

	}

	if (m_windowLayer == layer){

		return;   // 幂等

	}

	m_windowLayer = layer;

	if (layer == WindowLayer::Desktop){

		// ⚠️ spike 未通过前：Desktop 不承诺可用——**状态不降级**，仅当前 Win32 实现
		// 按 Bottom 语义执行（v1.2——评审 §7：保留用户请求的 Desktop，降级的是「实现如何执行」，
		// 不是「状态是什么」；spike 通过后 m_windowLayer 无需改动）。

		// ★ 原则（v1.3 正式化——评审 §十二）：**语义状态 ≠ 当前实现路径**。
		//   请求语义（m_windowLayer == Desktop）与实现手段（走 Bottom 路径）是两个层次；
		//   降级的是「实现如何执行」，不是「状态是什么」。故未来若新增 GetWindowLayer()，
		//   在 spike 未通过时它仍须返回 Desktop，**不得**返回 Bottom。
		Logger::Log(LogLevel::Warning,
			"WindowChrome: WindowLayer::Desktop not yet validated (spike pending) - executed as Bottom");

	}

	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由 WM_WINDOWPOSCHANGING 自然生效）

	}

	// 切到 Bottom/Desktop：立即派发一次（后续由 WM_WINDOWPOSCHANGING 持续维护）
	// 切回 Normal：不主动改变当前 z 序（交系统自然演化——避免"突然跳到最前"的反直觉效果）
	if (m_windowLayer != WindowLayer::Normal){

		SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}
```

### 3.9 R1：`SetChromeMode` 实现（配置期契约）

**v1.1 修订**：标题与日志措辞由「初始化期」统一为「**配置期**」——与 §2.4 的 API 分组术语一致（评审 §3 建议的统一生命周期术语）。

```cpp
void Win32PlatformWindow::SetChromeMode(ChromeMode mode){

	// 决策 D1（§9.1）：仅配置期生效——已显示窗口拒绝切换。
	// 判据 m_shown：在 Show() 内置位——它表达的是「框架 API 是否调用过 Show()」，
	// 而不是「系统当前是否可见」。二者**本来就不同**：Show()+Hide() 后 IsWindowVisible 为假，
	// 但契约上已进入运行期（v1.2——评审 P1：修掉 v1.1「契约说 Show 后拒绝、实现按可见性判」的不一致）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			"WindowChrome: SetChromeMode ignored after Show() - mode is config-time only");

		return;

	}

	if (m_chromeMode == mode && m_borderlessApplied){

		return;

	}

	m_chromeMode = mode;

	if (mode != ChromeMode::Borderless){

		return;   // Normal：无需任何处理（默认样式即 Normal）

	}

	// HWND 生命周期前提（v1.2——评审 P1 明确）：本框架**窗口构造即建 HWND**
	//（Win32PlatformWindow 构造体 CreateWindowExW，失败抛 std::system_error），
	// 故 Window 构造完成后 m_hwnd 恒非空。此处仍做防御，与 SetWindowLayer 对齐，
	// 避免前提失效时把 nullptr 交给 Win32 API。
	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由消息流自然生效）

	}

	// Borderless 应用流程：
	// ① 样式本身**不变**（保留 WS_OVERLAPPEDWINDOW——技术路线核心，R2）
	// ② 通知系统重新计算非客户区：SWP_FRAMECHANGED
	//    （必须在窗口显示前派发——否则会看到一次"边框→无边框"的闪烁）
	SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

	// ③ DWM 增强（阴影/圆角——失败容忍）
	ApplyDwmEnhancements(m_hwnd);

	m_borderlessApplied = true;

}
```

### 3.10 R7：窗口状态 API 实现

```cpp
void Win32PlatformWindow::Minimize(){

	// 运行期契约（v1.3——评审 §三）：与配置期 API 对称，Show() 前拒绝。
	// 理由：ShowWindow(SW_MINIMIZE) 本身就是显示状态操作，**不能靠「系统会忽略」**来成立
	//       「Show 前调用无意义」这一说法——必须在框架侧显式闭合契约。
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			"WindowChrome: Minimize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MINIMIZE);

}

void Win32PlatformWindow::Maximize(){

	// 运行期契约（v1.3——评审 §三）：同 Minimize
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			"WindowChrome: Maximize ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_MAXIMIZE);

}

void Win32PlatformWindow::Restore(){

	// 运行期契约（v1.3——评审 §三）：同 Minimize
	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			"WindowChrome: Restore ignored before Show() - state API is runtime-only");

		return;

	}

	if (m_hwnd) ShowWindow(m_hwnd, SW_RESTORE);

}
}
```

**事件回流在 `HandleMessage` 的 `WM_SIZE` 分支**（既有位置，追加状态判定）：

```cpp
	case WM_SIZE: {

		m_host.OnResized(LOWORD(lParam), HIWORD(lParam));

		// Phase 12 R7：窗口状态变化 → 事件（尺寸同步已在 OnResized 完成——顺序契约）
		// ⚠️ 判定来源是 IsIconic/IsZoomed（系统真实状态），不是我们调了哪个 API——
		// 这样鼠标拖拽最大化 / Win+↑ / Aero Snap 等非 API 路径同样产生事件
		WindowState state = WindowState::restored;

		if (IsIconic(hwnd)){

			state = WindowState::minimized;

		}

		else if (IsZoomed(hwnd)){

			state = WindowState::maximized;

		}

		// 去重：仅状态真正变化时派发（WM_SIZE 在拖拽缩放时会高频到达——
		// 不去重会让应用按钮形态切换逻辑被无意义事件淹没）
		if (state != m_lastWindowState){

			m_lastWindowState = state;

			WindowStateChangedEvent event(m_host.GetWindow(), state);

			m_host.OnEvent(event);

		}

		break;   // 继续翻译器（WindowResizedEvent）→ DefWindowProc

	}
```

> **新增成员**：`WindowState m_lastWindowState = WindowState::restored;`（去重用——初值 restored 对应创建后的普通态）
>
> ⚠️ **顺序契约**：`OnResized` 先于 `WindowStateChangedEvent` 派发——应用在状态事件里查询客户区尺寸时，RootWidget 已是最新尺寸（与 `WindowResizedEvent` 的既有注释「Window 内部会先同步 RootWidget 尺寸，再派发此事件」保持同一契约）。

### 3.11 R9：能力式接缝骨架（本阶段只立骨架）

**根因回顾**：`PlatformWindowHost` 现仅 8 个回调，应用层**够不着窗口消息**。托盘（`Shell_NotifyIcon` 回调消息）与拖入（`WM_DROPFILES`）必须落窗口过程 → 无接缝则 Phase 13 无法实现。

**接缝形态（初设定案）**：**不新增公共消息注册 API**。理由是 R9 决策已否决裸消息注册；本阶段只**确立能力式扩展点的落位**——即「每个能力 = 一个 `PlatformWindow` 虚接口 + 一个 `Window` 公共方法 + 平台实现在 `HandleMessage` 内消化消息」。

```text
已定形态（骨架——本阶段不实现托盘/拖放本体）：

  应用
   └─ Window::SetXxxCapability(...)          ← 公共面：零 HWND / 零消息号
        └─ PlatformWindow::SetXxxCapability(...)   ← 平台抽象面
             └─ Win32PlatformWindow 内部：
                  ├─ 调 Win32 API 注册（如 Shell_NotifyIcon / DragAcceptFiles）
                  └─ HandleMessage 内消化回调消息（如 WM_APP+1 / WM_DROPFILES）
                       └─ 翻译为框架层 Event（如 DropFilesEvent 的路径列表，UTF-8）
```

**本阶段必须交付的骨架（3 项）**：

| # | 交付物 | 内容 |
|---|---|---|
| 1 | `PlatformWindow` 的**能力接口扩展惯例** | 本阶段新增的 `SetChromeMode` / `SetWindowLayer` 本身就是第一个样例——它们内部消化了 `WM_NCCALCSIZE`/`WM_WINDOWPOSCHANGING`，公共面零消息号。**这就是 R9 要的形态**，无需额外抽象 |
| 2 | `HandleMessage` 的**未消费消息底线** | 现有结构已满足：switch 未命中的消息 → `m_messageHandler.Handle` → 返回 `nullopt` → `DefWindowProcW`。Phase 12 新增的 5 个 case 全部为「条件命中」（`m_chromeMode != Borderless` 时 `break`）——**Normal 模式零行为变化** |
| 3 | **Phase 13 落位说明** | 在 `PlatformWindow.h` 类注释中追加一段「平台能力扩展惯例」说明（不加任何虚函数——避免 YAGNI 违规）。**详设定稿措辞** |

**明确不做**（本阶段）：

- ❌ `RegisterRawMessageHandler(UINT, std::function)` —— 消息号泄漏进公共 API
- ❌ `OnRawMessage(UINT, WPARAM, LPARAM)` Host 回调 —— 同上（Win32 类型进框架契约）
- ❌ 泛型 `IMessageSink` / `IWindowCapability` 基类 —— 只有一个消费者（本阶段自己的 chrome）时抽接口是投机（二次用例出现再抽象）
- ❌ 托盘 / 拖放本体 —— Phase 13

> **R9 的正面结论**：Phase 12 本身**不需要新增任何 R9 专用代码**——它通过「把 chrome 做对」来证明能力式扩展点可用。Phase 13 的新增才需要按此惯例加 `SetTrayIcon` / `SetDropFilesEnabled`。这是 YAGNI 的正确形态：接缝是**惯例**而非**抽象**。

---

## 4. 链接库传播分析（库化边界关键项）

### 4.1 `dwmapi` 的链接属性（v1.1 限定——评审 §12）

| 库 | 引用位置 | 链接属性 | 理由 |
|---|---|---|---|
| `dwmapi.lib` | `Win32PlatformWindow.cpp`（`DwmExtendFrameIntoClientArea` / `DwmSetWindowAttribute`） | **PUBLIC（当前静态库模型下）** | 静态库不链接——消费者链接器需解析 ECDI.obj 里的 DWM 符号（同 user32 先例；Phase 11 已确立此判据） |

> ⚠️ **传播策略的前置条件（v1.1 新增）**：上述 `PUBLIC` 判定**成立的前提是 ECDI 当前为 STATIC 库**（`add_library(ECDI STATIC ...)`——Phase 10 库化定案，且**明确不做 DLL**）。
>
> **若未来切换为动态库模型**（`ECDI_SHARED` / Windows 平台实现拆成独立 DLL），链接传播需**重新评估**：
>
> - 动态库自身会在链接期解析其依赖 → 依赖可降为 `PRIVATE`（消费者不再需要直接链接 `dwmapi`）
> - 但需确认符号是否从 DLL 导出面泄漏（若公共头暴露了 DWM 类型则仍需 PUBLIC）
>
> **措辞纪律**：文档中不写「`dwmapi` 永久 PUBLIC」，而写「**当前静态库模型下使用 PUBLIC 传播；若未来切换动态库模型，再重新评估**」。这与 Phase 10 已确立的「转库决策：静态优先、不做 DLL」一致——本项只是把该前提显式写出，避免未来模型变更时遗漏。

### 4.2 落地修改点

- **CMake**：追加到既有 PUBLIC 组

```cmake
    target_link_libraries(ECDI PUBLIC user32 imm32 msimg32)
    target_link_libraries(ECDI PUBLIC windowscodecs ole32 shlwapi)
    target_link_libraries(ECDI PUBLIC dwmapi)        # Phase 12：DWM 阴影/Win11 圆角（R6）
    # 注：PUBLIC 以「ECDI 为 STATIC 库」为前提；若未来转 SHARED 需重新评估（§4.1）
```

- **vcxproj**：`AdditionalDependencies` 追加 `dwmapi.lib`（4 个配置组：Debug/Release × Win32/x64——现有文件已有 4 处）
- **MinGW 差异风险**：`libdwmapi.a` 在 MinGW-w64 中是否存在、`DWMWA_WINDOW_CORNER_PREFERENCE` 是否在头中——**详设四工具链实测**（§9.3）。若 `dwmapi` 缺失，R6 整体降级为「不调用 DWM，仅日志」——不影响 R2/R3/R4/R7 的完整性（R6 本就是失败容忍设计）

---

## 5. 影响面

| 区 | 范围 | 类型 |
|---|---|---|
| `include/ECDI/Window/ChromeMode.h` | 新建（82 头） | 🆕 |
| `include/ECDI/Window/WindowLayer.h` | 新建（83 头） | 🆕 |
| `include/ECDI/Window/WindowState.h` | 新建（84 头——v1.1 从事件头拆出，评审 §2） | 🆕 |
| `include/ECDI/EventSystem/Window/WindowStateChangedEvent.h` | 新建（85 头） | 🆕 |
| `include/ECDI/Platform/PlatformWindow.h` | 追加 7 个纯虚方法（配置期 4 + 运行期 3）+ 2 个 include | ✏️ |
| `include/ECDI/Window/Window.h` | 追加 7 个公共方法（同分组）+ 2 个 include | ✏️ |
| `include/ECDI/EventSystem/EventType.h` | 追加 1 个枚举值 | ✏️ |
| `include/ECDI/EventSystem/EventRouter.h` | 追加 1 个前置声明 + 1 个虚方法 | ✏️ |
| `src/EventSystem/EventRouter.cpp` | 追加 1 个 include + 1 个 Dispatch | ✏️ |
| `src/Platform/Win32/Win32PlatformWindow.h` | 追加 7 个 override + 2 个私有辅助（`AdjustMaximizedClientRect` / `ApplyDwmEnhancements`）+ 6 个状态成员 | ✏️ |
| `src/Platform/Win32/Win32PlatformWindow.cpp` | 追加 5 个 case（NCCALCSIZE/NCHITTEST/NCACTIVATE/WINDOWPOSCHANGING + WM_SIZE 状态分支）+ 7 个方法实现 + 2 个辅助实现 | ✏️ |
| `src/Window/Window.cpp` | 追加 7 个薄转发实现 | ✏️ |
| `CMakeLists.txt` | PUBLIC 链接追加 `dwmapi` | ✏️ |
| `ECDI/ECDI.vcxproj` | `ClInclude` 追加 4 头 + `AdditionalDependencies` 追加 `dwmapi.lib`（4 处） | ✏️ |
| `src/Tests/` | 新建 `WindowChromeTests.cpp` + `RunAllTests.h/.cpp` 登记 | 🆕 |
| `docs/` | 本文档 + README 索引 + 需求 §7 影响面回写修正 | ✏️ |

**零改动区**（重要）：`WindowMessageHandler.h/.cpp`（§3.0 裁决）、`Widget` 全部、`Layout` 全部、`Renderer` 全部、`Theme` 全部。

**Public 头计数**：81（Phase 11 后）→ **85**（+4：`ChromeMode` / `WindowLayer` / `WindowState` / `WindowStateChangedEvent`）。

> ⚠️ **v1.1 计数变更说明**：v1.0 为 84（`WindowState` 内嵌在事件头里）；采纳评审 §2 建议拆出独立头后为 **85**。本文档 §1 / §2 / §5 / §10 的头计数已统一为 85。

---

## 6. R10 `Desktop` 档：spike 前置设计

### 6.1 spike 程序规格（独立小程序，不进 ECDI 测试）

| 项 | 内容 |
|---|---|
| 位置 | 独立 `.cpp`（`docs/` 外，用户手工建 VS 工程或临时 CMake target——**不污染 ECDI 仓库结构**，倾向放 `x64/` 或仓库外的临时目录） |
| 规模 | 50 行以内（`CreateWindowExW` + 一个 static 框 + 路径 A/B 二选一） |
| 目标 | 判定在**用户当前 Win11 版本**上哪条路有效、是否稳定 |
| 判据 1（有效性） | Win+D 之后窗口**仍可见** |
| 判据 2（稳定性） | 切换应用 10 次 / 打开关闭开始菜单 / 双屏切换后仍保持正确层级 |
| 判据 3（副作用） | 是否盖住桌面图标；是否导致桌面图标无法选中（reparent 路线的高风险副作用） |

### 6.2 两条候选路线（spike 对比）

| 路线 | 做法 | 已知风险 |
|---|---|---|
| **A · reparent 到桌面层** | `FindWindowW(L"Progman", ...)` → `FindWindowExW(..., L"SHELLDLL_DefView", ...)` → `FindWindowExW(..., L"WorkerW", ...)` → `SetParent(hwnd, workerW)`（Wallpaper Engine / Fences 路线） | Win11 新版本对 `WorkerW` 层级处理有变化；reparent 后**失去独立窗口消息循环的部分语义**（本进程仍收消息，但 z 序/激活/DPI 上下文跟随宿主）；可能盖住桌面图标且**导致图标无法点击**（严重副作用） |
| **B · WinEventHook 临时置顶** | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND, ...)` 监测前台变化，发现 `WorkerW`/`Progman` 变前台时 `SetWindowPos(HWND_TOP)` | 需要全局钩子（性能/权限）；时序竞争（钩子回调与 z 序变更之间有窗口期，可能闪烁）；「Win+D 后仍可见」的达成路径更绕 |

### 6.3 初设倾向

**倾向路线 A（reparent）**——语义更干净（就是「成为桌面层的子窗口」，与桌面图标同层，Win+D 天然不受影响），且是业界主流做法（Wallpaper Engine / Fences 都是这条）。

**但倾向的前提是 spike 判据 3 通过**：若 reparent 后桌面图标不可点击/不可选中，则路线 A **必须否决**（可点击性 > 视觉效果）——此时退路线 B 或直接降级 `Bottom`。

### 6.4 失败降级路径（需求 R10 已定，此处明确工程动作）

```text
spike 失败（任一路线不可行 或 副作用不可接受）
  ├─ ① SetWindowLayer(Desktop) 保持"降级为 Bottom + Warning 日志"（§3.8 已实现）
  ├─ ② 回写 docs/desktopnest-roadmap.md §1.1 与 §7 R-1：标记 Desktop 档不可用
  └─ ③ DesktopNest 应用侧改选 Bottom 档 → 「Win+D 会一起被藏」纳入产品设计
        （DesktopNest roadmap §1.1 已预写此降级路径）
```

**关键**：降级路径**不需要改 Phase 12 的公共 API**——`WindowLayer::Desktop` 枚举值保留（未来 Windows 版本可能可行），只是当前实现退化为 Bottom 语义 + 日志。这是把「API 承诺」与「当前平台能力」解耦的刻意设计。

---

## 7. 测试方向

### 7.1 自动测试（`src/Tests/WindowChromeTests.cpp`——新建）

**核心洞察**：Phase 12 的大部分行为可通过「**同一坐标在不同 chrome 模式下的返回差异**」来断言，无需截图/像素读回。

| 用例 | 手法 | 断言 |
|---|---|---|
| **T1 Normal vs Borderless 客户区对照**（需求 §6 核心） | 创建两个真窗口（`WS_OVERLAPPEDWINDOW`），一个默认、一个 `SetChromeMode(Borderless)`，各自 `GetClientRect` / `GetWindowRect` | Normal：`clientRect != windowRect`（差 = 标题栏+边框）；Borderless：客户区与窗口矩形**四边重合**。**对照才有意义**——单独断言 Borderless 无法证明是 chrome 拦截在起作用（可能窗口本来就无边框）。<br>⚠️ **坐标系不可直接比较（v1.2——评审 §12）**：`GetClientRect` 返回 `(0,0,w,h)`（**客户区局部坐标**），`GetWindowRect` 返回 `(screenL,screenT,screenR,screenB)`（**屏幕坐标**）——两者坐标系不同，**禁止** `GetClientRect() == GetWindowRect()`。正确做法二选一：① `GetClientRect` → `ClientToScreen` 左上角 → 得 `ClientRectScreen` 再与 `GetWindowRect` 逐边比较；② 只比较 `client 宽/高 == window 宽/高` |
| **T2 NCHITTEST 九宫格 + resize 优先级**（v1.1 明确测试目的——评审 §8） | 真窗口 + `SetChromeMode(Borderless)` + `SetCaptionHeight(32)` + `SetResizeInset(8)`；`SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(screenX, screenY))` 逐点查询 | **测试目的（必须写进用例名/注释）：「resize 命中优先级覆盖 caption 命中」**——`(w/2, 4)` 同时落在 `inset`（4 < 8）与 `caption`（4 < 32）两个区内，期望 `HTTOP` 而非 `HTCAPTION`。其余九宫格：`(4,4)→HTTOPLEFT`、`(w-4,4)→HTTOPRIGHT`、`(4,h-4)→HTBOTTOMLEFT`、`(w-4,h-4)→HTBOTTOMRIGHT`、`(4,h/2)→HTLEFT`、`(w-4,h/2)→HTRIGHT`、`(w/2,4)→HTTOP`、`(w/2,h-4)→HTBOTTOM`、`(w/2,20)→HTCAPTION`、`(w/2,h/2)→HTCLIENT` |
| **T3 NCHITTEST 模式隔离（双窗口对照）**（v1.1 改双窗口；**v1.3 修断言逻辑**——评审 §四） | **窗口 A = Normal、窗口 B = Borderless，同几何位置**（两个独立 HWND——不依赖运行时切换，符合配置期契约）。B 的配置须**刻意取小**：`SetCaptionHeight(4)` + `SetResizeInset(4)`——以便与 Normal 的系统非客户区（顶部约 31px @100% DPI）拉开**可判定**的差异区间 | **断言分两类（v1.3 关键修订）**：<br>**A. 预期「不同」的点**——`(w/2, 20)`：Normal → `HTCAPTION`（20 < 系统非客户区高度），Borderless → `HTCLIENT`（20 ≥ `captionHeight` 4）。**这才是「Borderless 拦截生效」的证据**。<br>**B. 预期「相同」的点**——`(2,2)` → 两边**均** `HTTOPLEFT`；`(w-2,2)` → 均 `HTTOPRIGHT`（证明 Borderless **没有破坏** resize 九宫格）。<br>⚠️ **不得**断言「同一组坐标一律不同」——`(2,2)` 这类角点在两种模式下**本来就都返回** `HTTOPLEFT`，等值→不等断言**必然失败**；也不得用 `result != HTCAPTION` 单值断言（Normal 的系统结果本来就可能含 `HTCAPTION`）。<br>⚠️ **两类点必须留余量**：Normal 的非客户区高度是**系统度量**（随 DPI / 主题变化），**不可假设为定值**——故 B 的 caption/inset 取小值，使 A/B 两类点在各自主张的区间内都有 ≥8px 余量 |
| **T4 最大化客户区在 rcWork 内**（R4 验收基准） | 真窗口 Borderless + `Maximize()` + 消息泵处理一轮 → `GetClientRect` + `ClientToScreen` | **不变量：`ClientRectScreen ⊆ rcWork`**（子集，非相等——边框/DWM/Windows 版本差异可能让边界处理方式不同；用相等断言会引入脆弱性）。即：不覆盖任务栏、不超出工作区。**注意**：断言前必须让消息循环处理 `WM_NCCALCSIZE`/`WM_SIZE` |
| **T5 单位契约（`captionHeight` = 逻辑坐标）** | `SetCaptionHeight(32)` → 查询 `(w/2, 31)` 与 `(w/2, 33)` | 31 → HTCAPTION；33 → HTCLIENT（当前 DPI 1:1，边界精确可断言） |
| **T6 边界值 clamp** | `SetCaptionHeight(0)` / `SetResizeInset(0)` | `caption 0`：无 HTCAPTION 区（`(w/2, 4)` → HTCLIENT，但 `(2,2)` 仍 HTTOPLEFT 如果 inset>0）；`inset 0`：无 resize 区（`(2,2)` → HTCAPTION 或 HTCLIENT） |
| **T7a 运行期契约边界（v1.3 新增——评审 §三）** | 构造后**不调用 `Show()`**，直接调 `Minimize()` / `Maximize()` / `Restore()` | 三者均**不产生** `WindowStateChangedEvent`、窗口不可见状态**不变**（被 Warning 拒绝）；随后 `Show()` + `Maximize()` → 正常产生 `maximized`。**证明运行期 API 的 Show 前拒绝是框架行为、不依赖系统** |
| **T7 状态事件（API 路径）** | 注册一个捕获 `WindowStateChangedEvent` 的测试 `Application`（或直接测 `Win32PlatformWindow` 经 Host 回调） | `Minimize()` → 收到 `minimized`；`Restore()` → 收到 `restored`；`Maximize()` → 收到 `maximized`。**去重验证**：连续两次 `Maximize()` 只产生 1 个事件 |
| **T7b 状态事件（非 API 路径）**（v1.2 新增——评审 §19） | **手测/视觉**（不适合自动化——需真实输入） | 通过系统行为触发最大化：**拖标题栏到屏幕顶部**（Aero Snap）/ **Win+↑** / 双击标题栏，确认仍收到 `WindowStateChangedEvent(maximized)`。**这是本设计最核心命题的验收落点**——事件由**系统真实状态**产生，而非由 ECDI 某个 API 被调用产生（若只测 T7，无法区分「事件真的来自系统」还是「`Maximize()` 自己发了个事件」） |
| **T8 R9 未消费消息零回归** | 既有全部测试通过（截至 2026-09-12 为 **174** 条） | 无需新用例——**回归即证明** |

**窗口创建规格**（复用 `RendererTests.cpp` 先例，但**必须区分样式**）：

```cpp
// ⚠️ 与 RendererTests 的差异：那里用 WS_POPUP（纯像素测试，无需系统 chrome），
// 本处必须用 WS_OVERLAPPEDWINDOW——否则 Normal/Borderless 对照失去意义
// （WS_POPUP 本来就无边框，Borderless 拦截不会产生可观察差异）。
// 屏幕右下角短暂显示 + SW_SHOWNOACTIVATE 的原因同 RendererTests
// （屏幕外窗口 DC 裁剪区为空）。
```

> **测试窗口计数**：T1–T7 合计约 4-6 个真窗口，每个创建后立即销毁。**详设需评估**是否合并为「一次创建、多断言共享」以降低窗口创建开销（§9.6）。
>
> ⚠️ **但 T1 / T3 这类对照测试不可合并**——它们的核心是「两个窗口在同一条件下返回不同结果」，共享窗口会引入状态污染（v1.1 明确——评审 §9 的延伸）。

### 7.2 视觉验收（补充，非阻塞）

- **`examples/VisualTest/` 式人工核窗口**（或复用既有 VisualTest 加一页）：
  - Borderless 窗口 + 应用自绘标题栏（一个 `Panel` 圆角条 + `Label` 标题 + 两个 `Button`——关闭/最小化）
  - **手测矩阵**（需求 R8 §系统交互手测矩阵）：Alt+F4 / **Alt+Space** / Win+↑ / Win+↓ / Win+← / Win+→ / 双击标题区 / 拖动标题区 / 边缘八向 resize / Aero Snap（拖到屏幕顶/左/右）
  - **判定标准**：全部行为与有边框窗口一致（除外观）；Alt+Space 系统菜单正常弹出（这是 NCCALCSIZE 改动后最易坏的一项）
- **像素读回**（可选补充）：`RendererTests` 的 `GetPixel` 手法验证 Borderless 窗口的**左上角**（0,0）是应用绘制内容而非系统标题栏灰色——`CLR_INVALID` 陷阱已在那份测试里注释（屏幕外窗口 DC 裁剪区为空）

### 7.3 R10 spike（独立，不进自动测试）

见 §6.1——spike 是**人工执行的独立程序**，产出是「路线 A / B / 两者皆不可行」的结论 + 一份简短的实测记录（回写 `desktopnest-roadmap.md`）。**spike 通过才补 Desktop 档的验收项。**

### 7.4 R5 验证（「应用自绘标题栏可行」）

不新增 API 测试——**用 §7.2 的视觉验收窗口承担**：如果应用能用 `Panel` + `Label` + `Button` 摆出一个可拖动（HTCAPTION 免费获得）的标题栏，R5 的「框架负责行为、应用负责 UI」定位就得到验证。**这是 R5 的唯一验收形式**。

### 7.5 四工具链 + VS

按既有惯例：用户在其 VS 2026 / CLion 2026.2（MSVC / Clang / ClangCL / MinGW）中编译与运行测试。**MinGW 的 `dwmapi` 可用性是本阶段跨工具链风险点**（§4）——若失败，验证 R6 的失败容忍是否真的「优雅降级」。

---

## 8. 开放决策点（归详设——已拍板项不重复）

| # | 项 | 初设倾向 | 归属 |
|---|---|---|---|
| 1 | 最大化**是否需要补偿**、补偿量多少 | **基准已锁死**：`rcClient = rcWork` 即满足 `⊆` 不变量，**无需补偿**；是否需要额外补偿 / 量值 / X-Y 与混合 DPI 对称性**全部归真机标定**。**纪律**：补偿只能「正内缩」（只收缩、不扩大），且须附实测数据（§3.4） | 详设 §3.4 |
| 2 | 最大化态 NCHITTEST 是否返回 resize 命中 | **已在初设落到代码**：`resizable = !IsZoomed(hwnd)` 门控四边四角，caption 保留（允许顶部下拖还原）——详设复核即可 | 详设 §3.3 |
| 3 | `dwmapi` / Win11 圆角常量在 MinGW 的可用性 | 头缺失则文件内自定义常量兜底；库缺失则 R6 整体降级为「仅日志」 | 详设 §4 |
| 4 | `WindowLayer::Desktop` 的 spike 结果与后续实现 | 倾向路线 A（reparent）；判据 3（桌面图标可点性）一票否决 | spike 产出 |
| 5 | 测试窗口创建/销毁的合并策略 | 倾向「一次创建多断言共享」降低开销——但**对照类测试（T1/T3）必须独立窗口**（否则状态污染） | 详设 §7.1 |
| 6 | DWM margin 值（1px vs 其他）的视觉效果 | 倾向 1px；不同 Windows 版本视觉效果不保证一致，须实测（需求 R6 已声明） | 详设 §3.5 |
| 7 | `PlatformWindow.h` 类注释中「平台能力扩展惯例」的措辞 | 需写清「加能力 = 加虚接口 + 公共方法 + 消息在平台内消化」三步，作为 Phase 13 的模板 | 详设 §3.11 |
| 8 | **DIP → 物理像素的换算点与 DPI 来源**（v1.3 新增——评审 §八） | 公共 API 语义为 **DIP**（`captionHeight`/`resizeInset`）；当前 DPI 未落地 = 1:1。**详设必须锁死**：① 换算点（`WM_NCHITTEST` 内 DIP→px）② **DPI 来源**（`GetDpiForWindow` / 鼠标所在 monitor / 未来统一 DPI 服务）③ **窗口跨 DPI 显示器且鼠标在另一台**时的规则。**不做 = 留一个不可判定边界** | 详设 §3.3 |
| 9 | **`Desktop` 语义状态 ≠ 当前实现路径**（v1.3 新增——评审 §十二） | 已倾向「保留 `m_windowLayer == Desktop`，仅 Win32 实现按 Bottom 执行」；详设须把它**正式写成契约**：若未来出现 `GetWindowLayer()`，spike 未通过时它仍应返回 `Desktop`（请求语义），而非 `Bottom`（实现路径） | 详设 §3.8 |

---

## 9. 初设拍板项（需求挂账兑现）

### 9.1 决策 7：ChromeMode 运行时切换 → **降级为配置期**

**需求 §4 决策 7 原文**：「🔸 倾向允许运行时切换（含 SWP_FRAMECHANGED/DWM/命中基线处理）；初设若判定成本过高可降级为『仅初始化期』——**初设拍板**」

**初设裁决：降级为「**配置期**（构造后 / `Show()` 前）设置」，运行时切换不承诺。**

> **术语修订（v1.1——评审 §3）**：v1.0 用「初始化期」，v1.1 统一为「**配置期**」——与 §2.4 的 API 分组术语一致，且与「运行期」形成清晰对照。且**该契约覆盖全部 4 个配置期 API**（`SetChromeMode` / `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer`），不只 `SetChromeMode`。

**成本判定明细**（运行时切换 Normal ↔ Borderless 需要同时成立的事情）：

| # | 需要处理 | 成本/风险 |
|---|---|---|
| 1 | `SetWindowPos(SWP_FRAMECHANGED)` 触发非客户区重算 | 可行，但**必须伴随一次显式重绘**——否则会残留旧标题栏像素 |
| 2 | **窗口几何迁移** | 切到 Borderless 时客户区突然变大（多了标题栏高度）；切回 Normal 时客户区变小。**是保持「窗口外框不变」还是「客户区不变」？** 两个选择都会让用户觉得窗口"跳"了一下。且**窗口尺寸变化会连锁触发 `WM_SIZE` → RootWidget 重排 → 布局重算**（Phase 9.7 的 Arrange 链）——应用侧所有手动布局的位置都要跟着动 |
| 3 | DWM 状态 | `DwmExtendFrameIntoClientArea` 的 margin 需要切回 Normal 时**反向撤销**（设 `{0,0,0,0}`）——撤销后系统阴影消失，视觉上又是一次跳变 |
| 4 | 命中测试基线 | 命中值从「系统」变「自定义」——切换瞬间若鼠标正悬停在边框上，系统内的 resize 循环状态与新命中值不一致（可能卡住一次拖动） |
| 5 | 最大化态迁移 | 若切换时窗口处于最大化态，`WM_NCCALCSIZE` 的收缩逻辑与系统几何要重新对齐一次——**这是最容易出 bug 的路径** |
| 6 | 状态双份维护 | 需要在 Win32PlatformWindow 里额外维护「已应用/未应用」+「目标模式」+「是否显示中」三态，与系统真实状态形成潜在第二真相源 |

**裁决理由**：

1. **收益极低**：真实用例（ModelProbe / DesktopNest / Demo）都是**创建时决定形态**——没有一个需要在窗口生命周期中途改形态。这是典型的「没有消费者的灵活性」。
2. **成本集中在几何迁移**（第 2 项）——而这一项**无法用工程手段消除**，只能选择一种「跳变」方式。强行承诺会给 API 留下一个「行为不确定」的语义。
3. **可逆性**：API 形态（`SetChromeMode`）**不因降级而改变**——未来若真出现运行期切换用例，只需松开 `m_shown` 判据并补齐上述 6 项，**API 零变更**。降级只影响**当前契约的承诺强度**，不锁死未来。（v1.2：判据由 `IsWindowVisible` 改为 `m_shown`——见 §3.1；后者才精确表达「`Show()` 是否调用过」。）

**契约措辞（写入头注释）**：

> `SetChromeMode` / `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer` **必须在本窗口 `Show()` 之前调用**（配置期 API）。`Show()` 之后调用记 Warning 日志并忽略（不抛异常，不做运行时切换）。理由见初设 §9.1
> 
> 对称地，`Minimize` / `Maximize` / `Restore` **必须在本窗口 `Show()` 之后调用**（运行期 API）。`Show()` 之前调用记 Warning 日志并忽略（不抛异常）。**两侧互为镜像**——见 §9.2 生命周期表。

**替代方案（若应用确实需要切换）**：销毁旧窗口 + 创建新窗口（Phase 12 不提供便利 API，但应用层完全可自行这样做——`Window::Release()` + `Application::Create()` 已有）。

### 9.2 决策 7 的推广：配置期 / 运行期 API 分组（v1.1 新增——评审 §3）

**这是 v1.1 最有价值的一条契约整理**——把 7 个 API 按「窗口生命周期阶段」分成两组，形成整齐的心智模型：

```text
Window window;

// ── 配置期（4 个——窗口形态，创建后一次性决定）──
window.SetChromeMode(ChromeMode::Borderless);
window.SetCaptionHeight(32);
window.SetResizeInset(8);
window.SetWindowLayer(WindowLayer::Bottom);

window.Show();   // ← 分界线

// ── 运行期（3 个——窗口状态，可反复调用）──
window.Maximize();
window.Restore();
window.Minimize();
```

| 组 | API | 生命周期窗口 | `Show()` **之前** | `Show()` **之后** |
|---|---|---|---|---|
| **配置期**（窗口**形态**） | `SetChromeMode` / `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer` | 构造后 ~ `Show()` 前 | ✅ 正常生效 | ⚠️ Warning + 忽略 |
| **运行期**（窗口**状态**） | `Minimize` / `Maximize` / `Restore` | `Show()` 后全程 | ⚠️ Warning + 忽略 | ✅ 正常生效 |

> **对称性（v1.3 新增——评审 §三）**：两组 API **互为镜像**——各自只在自己的生命周期区间内有效，越过边界一律 `Warning + 忽略`。
>
> ⚠️ v1.2 及以前**只约束了配置期一侧**；运行期一侧仅以「`Show()` 之前调用无意义（且系统会忽略）」作说明——该说法**既不准确也未实现**：`ShowWindow(SW_MAXIMIZE)` 本身就是显示状态操作，**并非「会被忽略」**，契约必须在框架侧显式闭合（`if (!m_shown)` → Warning + return，见 §3.10）。

**为什么这个分组重要**：

1. **语义整齐**——「窗口形态由创建期决定，窗口状态可随时变化」是一句能记住的话；比「哪些能改哪些不能改」的清单式契约更少误用
2. **判据统一**——全部用 `m_shown` 一个判据实现（**v1.2 修订**：v1.1 用 `IsWindowVisible`，但它表达的是「系统当前可见性」而非「`Show()` 是否调用过」，两者在 `Show()+Hide()` 后分歧——见 §3.1 说明。`m_shown` 不是第二真相源：它记录的是框架 API 调用事实，与系统可见性是两个不同事实）
3. **`Minimize/Maximize/Restore` 天然属运行期**——它们改变的是**状态**而非**形态**；`Show()` 之前调用**由框架显式拒绝**（Warning + 忽略），**不依赖系统行为**（v1.3——评审 §三：原「系统会忽略」的说法不成立）
4. **API 不锁死**——未来若出现「运行期切层级」的真实需求，只需把 `SetWindowLayer` 从配置期组移到运行期组（签名零变更）

### 9.3 ~ 9.8

见 §8 开放决策点表——这些是**详设要拍板**的项，初设只给倾向。

---

## 10. 修订记录

- v1.3（2026-09-12）**第三轮外部评审全采纳（2 P1 + 2 P2）——评审结论 PASS，可进入详细设计**：
  - **P1 运行期 API 的 Show 前行为未闭合**（评审 §三）：§9.2 只说「运行期 API，`Show()` 后可调用」，并以「`Show()` 之前调用无意义（且系统会忽略）」作说明；但 §3.10 实现只有 `if (m_hwnd) ShowWindow(...)`，**无 `m_shown` 检查**。该说法**既不准确也未实现**——`ShowWindow(SW_MAXIMIZE)` 本身就是显示状态操作，并非「会被忽略」。**修订**：三个运行期方法统一补 `if (!m_shown)` → `Warning + return`；§2.3/§2.4 头草案加 `@pre`；§9.2 生命周期表改为**对称**两列（`Show()` 之前 / 之后）；新增 **T7a** 用例（构造后不 `Show()` 直接调三者 → 无事件、状态不变）。
  - **P1 T3 断言逻辑错误**（评审 §四）：v1.1–v1.2 写「同一组坐标分别向 A/B 发 `WM_NCHITTEST`，断言两者返回**不同的**命中值」——但 `(2,2)` 这类角点在 Normal（系统边框）与 Borderless（自定义九宫格）下**本来就都返回** `HTTOPLEFT`，该断言**必然失败**。**修订**：断言分两类——**A. 预期「不同」的点**（`(w/2,20)`：Normal `HTCAPTION` vs Borderless `HTCLIENT`，这才是「拦截生效」的证据）；**B. 预期「相同」的点**（`(2,2)`/`(w-2,2)` 均 `HTTOPLEFT`/`HTTOPRIGHT`，证明九宫格未被破坏）。并标注**必须留余量**：Normal 非客户区高度是系统度量（随 DPI/主题变化），故 B 的 `captionHeight`/`resizeInset` 取小值（4/4）以拉开可判定区间。
  - **P2 DIP → 物理像素的换算点与 DPI 来源**（评审 §八）：初设「当前 1:1，未来只需改这里」作为初设可以，但**详设必须锁死**换算点与 DPI 来源。**修订**：§3.3 换算点注释扩展为三问（换算点 / DPI 来源 / 跨 DPI 显示器且鼠标在另一台时的规则），并在 §8 新增**开放决策点 8**。
  - **P2 `Desktop` 语义状态 ≠ 实现路径**（评审 §十二）：**修订**：§3.8 在 `Desktop` 降级处新增 ★ 原则注释，并在 §8 新增**开放决策点 9**——若未来出现 `GetWindowLayer()`，spike 未通过时仍返回 `Desktop`（请求语义），不得返回 `Bottom`（实现路径）。
  - **自行发现并修正（评审未提）**：§9.2 生命周期表的**表头行与分隔线 `|---|---|---|---|` 挤在同一行**（markdown 表格缺陷，渲染时整表失效）——v1.3 一并拆为两行。
  - **评审确认保留不动**：§五 `WM_NCCALCSIZE`（含 `wParam==FALSE` 放行）、§六 最大化 `rcWork`「不要再过度设计」、§七 九宫格优先级、§九 事件源自 `WM_SIZE`（并特别肯定 **T7b** 的价值）、§十 `WindowState` 独立成头、§十一 R9 能力式接缝、§十三 `WM_WINDOWPOSCHANGING`、§十四 DWM 范围控制、§十五 `dwmapi PUBLIC`、§十六 测试矩阵整体、§十七 影响面（81→85）。
- v1.2（2026-09-12）**第二轮外部评审全采纳（1 P0 必改 + 3 P1 + 4 P2）——修正 P0 后可进详设**：
  - **P0（必改）§3.4 最大化补偿方向错误**（评审 §二）：v1.1 的 `dx = rcWindow.left - rcMonitor.left` 在最大化时**恒为负**（窗口被系统撑到 `rcMonitor` 之外），代入 `rcClient.left = rcWork.left + dx` 会使客户区**越出工作区**——实际是「扩大」而非「收缩」。**数值实证**（1920×1080 / 任务栏 40 / 边框 8）：公式产出 `rcClient = (−8,−8,1928,1048)` = 1936×1056，**比整块显示器还大**，违反 `ClientRectScreen ⊆ rcWork`。**修订**：初设**只锁两条**——① 唯一基准 `rcClient = mi.rcWork`（本身即满足不变量，**无需补偿**）；② **补偿不变量**（若详设实测仍需补偿，只能「正内缩」、只收缩不扩大）；是否需要补偿/量值/对称性全部**降为详设真机标定**（§8 决策点 1）。同时新增**纪律条款**：不得为「让初设看起来完整」而提前锁死未经验证的补偿公式。
  - **P1 配置期判据 `IsWindowVisible` → `m_shown`**（评审 §四）：二者不等价——`Show()`→`Hide()` 后可见性为假但契约上已进运行期，造成「契约说拒绝、实现仍允许」的不一致。**修订**：§3.1 新增 `bool m_shown`（在 `Show()` 内置位），§3.8/§3.9 判据统一改用它，§9.1 可逆性说明与 §9.2「判据统一」同步。并说明它**不是第二真相源**（记录的是「API 是否调用过 Show」而非「系统是否可见」，两者是不同的两个事实）。
  - **P1 `SetChromeMode` 缺 `m_hwnd` 防御 + HWND 生命周期前提未明**（评审 §五）：§3.8 有 `m_hwnd == nullptr` 早退而 §3.9 没有。**修订**：§3.9 补同款防御；并**明确前提**——源码核实 `Win32PlatformWindow` 构造体即 `CreateWindowExW`（失败抛 `std::system_error`）、`Window` 构造体持有该对象（`Window.cpp:47`），故 **Window 构造完成后 `m_hwnd` 恒非空**（评审倾向的选项 A 成立）。
  - **P1 最大化态 NCHITTEST「文字说了代码没体现」**（评审 §九）：§3.3 契约要点写了 `IsZoomed` 时跳过四边四角，但代码无此判定。**修订**：代码引入 `const bool resizable = !IsZoomed(hwnd)` 统一门控四边四角，**caption 判据刻意不走该门**（保留顶部下拖还原）；契约要点同步改为「已落入代码」。
  - **P2（4 项）**：⑩ **T1 坐标系说明**（评审 §十二）——`GetClientRect`（客户区局部坐标）与 `GetWindowRect`（屏幕坐标）**不可直接比较**，须 `ClientToScreen` 后比较或只比宽高；⑪ **T7b 新增非 API 路径**（评审 §十九）——拖标题栏到顶 / Win+↑ / 双击标题栏触发最大化，手测确认事件仍到（这是「事件来自系统真实状态」的验收落点，原 T7 只覆盖 API 路径）；⑫ **§3.5「1px」表述收紧**（评审 §十八）——不宣称「1px = 阴影」是普适数学关系，仅是本实现当前选取的参数；⑬ **§2.4 清理重复残块**（评审 §二十）——删除重复的 `SetCaptionHeight`/`SetResizeInset`/`SetWindowLayer`/`Minimize`/`Maximize`/`Restore` 片段与其失衡代码围栏。
  - **评审确认保留不动**：§六/§七 `Desktop` 语义抽象与分层（并采纳措辞收紧——**状态不降级，仅实现按 Bottom 语义执行**，日志文案随之改为 `executed as Bottom`）、§八 `WM_WINDOWPOSCHANGING` 设计、§十 T2 设计、§十一 T3 双窗口对照、§十三 T4 `⊆` 不变量、§十四 `WindowState` 独立成头、§十五 事件源自 `WM_SIZE`、§十六 R9 不造抽象、§十七 `dwmapi PUBLIC` 措辞、§二十一 影响面统计（81 + 4 = 85）。
  - **另：T8 的既有用例数由「158 条」更新为「174 条」**（截至 2026-09-12 实测）。
- v1.1（2026-09-11）**外部评审通过（修改后通过——可进详设），8 项全采纳**：
  - **必改 4 项**：① **API 统计口径明确化**（评审 §1）——新增 §1.1，定义「7 个 API = Window 公共 API 方法数（配置期 4 + 运行期 3）」，明确枚举/事件/`PlatformWindow` virtual 不计入，并写清计数边界规则；② **配置期 / 运行期 API 分组**（评审 §3）——新增 §9.2，4 个配置期 API（`SetChromeMode`/`SetCaptionHeight`/`SetResizeInset`/`SetWindowLayer`）统一「必须 Show 前调用」契约（含 `SetWindowLayer` 归入配置期），3 个运行期 API 可随时调用，§2.3/§2.4 头草案同步加分组注释与 `@pre`；③ **`Bottom` 语义收紧**（评审 §4）——「持续强制置底」→「**持续维护普通窗口层底部位置**」，明写「不承诺阻止第三方 `SetWindowPos` 造成的瞬时 z 序变化，那种瞬时变化不构成契约违反」（§2.2 头注释 + §3.7 + §3.7 措辞纪律对照表）；④ **`Desktop` 纯语义不绑实现**（评审 §5）——§2.2 新增三层分工对照表（语义层/平台抽象层/Win32 实现层），显式声明 `WorkerW`/`Progman`/`SetParent`/`SetWinEventHook` 全属实现层，Windows 版本变更只改实现不改 API。
  - **建议拍板 4 项**：⑤ **`WindowState` 独立成头**（评审 §2）——从 `WindowStateChangedEvent.h` 拆出为 `Window/WindowState.h`（84 头），修正依赖方向为「事件 → 状态」而非「状态 → 事件」；Public 头计数 **84 → 85**（§1/§2/§5/§10 全量同步）；⑥ **T3 改双窗口对照**（评审 §9）——不再用「Normal 结果 != HTCAPTION」这类不可靠断言，改为 **窗口 A（Normal）+ 窗口 B（Borderless）同坐标结果对比**，且明确不用运行时切换（符合配置期契约）；⑦ **T2 明确测试目的**（评审 §8）——用例名/注释写明「验证 resize 优先级覆盖 caption」，并标注 `(w/2,4)` 的重叠语义；⑧ **DWM `1px` 归实现细节**（评审 §11）——§3.5 拆为「契约层承诺（保留阴影/圆角 + 失败容忍）」与「实现层参数（MARGINS/DWMWCP_ROUND）」，指明 `1px` 是 Win32 MVP 的具体选取值而非 API 语义。
  - **额外采纳 1 项（评审 §12 顺带）**：⑨ **`dwmapi` PUBLIC 限定静态库模型**——§4 拆为 §4.1（传播属性 + 前置条件 + 未来 SHARED 模型的重新评估说明）与 §4.2（落地修改点），措辞改为「当前静态库模型下使用 PUBLIC；若未来切换动态库模型，再重新评估」，不写「永久 PUBLIC」。
  - **采纳 1 项措辞统一**：⑩ 「初始化期」全文统一为「**配置期**」（与 §9.2 分组术语对齐）；`SetChromeMode` 日志文案同步改为 `config-time only`。
  - **评审明确保留不动的项（确认）**：R5 不做 CaptionBar Widget（评审 §14 特别赞成）、R9 不造任何专用抽象（评审 §6 称「这份初设里比较喜欢的一部分」，建议保留「接缝不一定意味着新的抽象类」这句）、R4 `rcWork` 基准（评审 §10 认可并强化为 `⊆` 子集不变量）、T4 子集断言、`WM_NCACTIVATE` 方向正确（评审 §7 细节归详设）。
- v1.0（2026-09-11）初步设计初稿：① **范围映射**（R1–R10 → 设计域，标注哪些给全文、哪些归详设）；② **Public 头全文草案**——新建 3 头（`ChromeMode.h` 82 / `WindowLayer.h` 83 / `WindowStateChangedEvent.h` 84）+ 修改 5 处（`PlatformWindow.h` 7 虚方法 / `Window.h` 7 公共方法 / `EventType.h` +1 枚举 / `EventRouter.h` +1 虚方法 / `EventRouter.cpp` +1 Dispatch）；③ **分层归属裁决**——`WM_NCCALCSIZE`/`NCHITTEST`/`NCACTIVATE`/`WINDOWPOSCHANGING` **不进 `WindowMessageHandler`**（非 Event，进 `Win32PlatformWindow::HandleMessage` 状态同步区），需求 §7 影响面表述需回写修正；④ **实现分解**（5 个 case + 3 个辅助函数全文：NCCALCSIZE 拦截 / NCHITTEST 九宫格 / 最大化 rcWork 收缩 / DWM 失败容忍 / WINDOWPOSCHANGING 持续置底 + 状态去重）；⑤ **链接库传播**（`dwmapi` PUBLIC + MinGW 风险）；⑥ **R9 能力式接缝骨架**（结论：本阶段**不需新增接缝代码**——通过把 chrome 做对来证明惯例可用；明确 4 项不做）；⑦ **R10 Desktop 档 spike 前置设计**（50 行程序规格 + 路线 A/B 对比 + 倾向 reparent + 判据 3 一票否决 + 降级路径三步）；⑧ **测试方向**（T1–T8：Normal/Borderless 对照为核 / NCHITTEST 九宫格 / rcWork 校验 / 状态事件去重；窗口样式必须 `WS_OVERLAPPEDWINDOW` 而非 RendererTests 的 `WS_POPUP`）；⑨ **决策 7 拍板**——降级为仅初始化期（6 项成本明细 + 3 条裁决理由 + API 不锁死未来的可逆性论证）；⑩ 7 项开放决策点归详设。待评审。
