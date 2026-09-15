# Phase 13 CaptionBar 自绘标题栏 初步设计（v1.1）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-13（v1.1 同日外部评审修订）
> 状态：**✅ 已实现并验收（2026-09-14 ~ 09-15）**——证据：详设 `phase13-captionbar-detailed-design.md` **v1.4**（四构建 188/188 + 手测通过）；原状态「✅ v1.1 外部评审通过（可进入详细设计）」的头草案 6 处 + 命中委托链 + 三处初设新发现均已落地
> 前置：`phase13-captionbar-requirements.md` **v1.1**（外部评审通过——可进初设）
> 一句话：把 R1–R8 与 D0–D9 落成「Public 头全文草案 + 一条命中委托接缝 + 组合式 CaptionBar」——**不换 Phase 12 路线**（不动 NCCALCSIZE / NCACTIVATE / WINDOWPOSCHANGING，不动 B 契约与渲染四层）。
> v1.1 修订：① §3.1 补 **`HitTest` 深度优先前提**（源码核实 `Widget.cpp:105`）；② §2.4 定案 `RequestClose` **同步派发**；③ §7 T13-4 改 **`RecordingBackend` 判据**（**不为测试加 Public API**）；④ §3.1 固定 **`break → DefWindowProc → HTCLIENT`** 结构（`Win32PlatformWindow.cpp:281` 引证）；⑤ §6 五项确认留详设

---

## 1. 范围映射（需求 → 设计域）

| 需求 | 设计域 | 本文落点 |
|---|---|---|
| R1 CaptionBar Widget（标题 + 三按钮，矢量自绘） | 新建 Public 头 + 内部按钮实现 | §2.5 / §2.6 / §3.2 / §3.3 |
| R2 NCHITTEST ↔ Widget 树委托（**含 D9 可交互判据**） | Host 契约 + 平台命中分支 + Widget 能力声明 | §2.1 / §2.2 / §3.1 |
| R3 Window 状态查询 API | PlatformWindow 查询面 + Window 转发 | §2.3 / §2.4 / §3.5 |
| R4 close 走关闭请求 | **框架侧关闭请求入口（初设新发现——见 §8 O1）** | §2.4 / §3.4 |
| R5 悬停 / 按下视觉态 | 内部按钮自绘 + Hover 机制 | §3.3 |
| R6 布局集成（普通 Widget） | 零特判（9.7/9.8 现成） | §3.2 |
| R7 `captionHeight` 职责二分 | 契约条文（不联动） | §4.2 |
| R8 回归底线 | 测试方向 | §7 |

**范围外**（需求 §5 已圈）：Snap Layouts、标签页标题栏、Aero 动画、Theme 深度集成、caption 拖拽行为变更、每显示器 DPI。

---

## 2. 头全文草案

### 2.1 `ECDI/include/ECDI/Widget/Widget.h`（增量：D9 能力声明）

在 `CanFocus()` 附近（同一区域——都是「控件能力」查询）：

```cpp
	/// @brief 该控件是否消费鼠标输入（Phase 13 D9——NCHITTEST 委托的判据）
	/// @details 语义：**「被 HitTest 命中」≠「应阻止系统的标题栏拖拽」**。
	/// 默认 false（纯显示控件）——Label / Panel / ProgressBar 等命中后仍让 caption 区返回 HTCAPTION，
	/// 保证「拖标题文字可移动窗口」；交互控件（Button / StateWidget / TextBox）override 返回 true。
	/// ⚠️ **坐标无关**：HitTest 已保证询问点落在本控件内，且 HitTest 只返回控件指针、不返回局部坐标
	/// （若做成点查询需二次坐标换算）⇒ 局部可交互需求由**复合控件**表达（如 CaptionBar 把按钮做子控件）。
	/// ⚠️ **与 `CanFocus()` 无关**：后者是键盘焦点语义，两者恰好近似但不可互推（复用会让概念互相绑架）。
	virtual bool ConsumesMouseInput() const noexcept { return false; }
```

**override 清单（3 处，均 `return true;`）**：`Button.h` · `StateWidget.h`（覆盖 CheckBox / Radio）· `TextBox.h`。
（`Label` / `Panel` / `ProgressBar` / `CollapsiblePanel` 不 override——默认 false。全库其余控件见 §5 影响面复核表。）

> ⚠️ **非纯虚 ⇒ 零破坏**：无派生类需要同步（**无**条 33 意义上的「实现者清单」问题）。

### 2.2 `ECDI/include/ECDI/Platform/PlatformWindowHost.h`（增量：D2 命中委托）

```cpp
	/// @brief 客户区可交互命中查询（Phase 13 R2 / D2——NCHITTEST 委托）
	/// @param x,y 窗口局部坐标（Borderless 下客户区 = 整窗，两者坐标系一致——见 §4.1）
	/// @return true  = 该点落在**消费鼠标输入的控件**上 → 平台层应让本次命中走客户区（HTCLIENT）
	///         false = 交 caption 区语义（HTCAPTION——拖拽 / 双击最大化·还原 / 系统菜单）
	/// @details 平台层**不认识 Widget**（分层律）——只问本契约；实现方（Window）转
	/// `RootWidget::HitTest` → `Widget::ConsumesMouseInput()`（§3.1）。
	/// **纯虚**：遗漏实现 = 编译期暴露（契约硬性——与 Phase 12 的 7 个纯虚同规格）。
	virtual bool IsClientInteractiveAt(int x, int y) const noexcept = 0;
```

> ⚠️ **实现者 = 2 个**（全库 grep 已核）：`Window`（框架）+ **`EventTests.cpp:44` 的 `FakeHost`（测试替身——必须同步补一行 no-op override）**。Phase 12 的教训（skill 条 33）在此**再次兑现**：若不预检，实施期会撞编译错误。

### 2.3 `ECDI/include/ECDI/Platform/PlatformWindow.h`（增量：R3 状态查询）

```cpp
#include "ECDI/Window/WindowState.h"   // 新增 include（返回类型需完整定义）

	/// @brief 查询当前窗口状态（Phase 13 R3；事实来源 = 平台层）
	/// @details 与 `WindowStateChangedEvent` 互补：事件回答「变成了什么」，本查询回答「现在是什么」。
	/// 事实的唯一来源在平台层（`WM_SIZE` 时由 `IsIconic` / `IsZoomed` 判定并缓存）——**纯虚**。
	virtual WindowState GetWindowState() const noexcept = 0;
```

> ⚠️ **实现者 = 3 个**：`Win32PlatformWindow`（返回既有成员 `m_lastWindowState`）+ **`AnimationTests.cpp:23` / `ProgressBarTests.cpp:29` 两个 `TestPlatformWindow`**（各补一行 stub）。**成本已预先核实**（Phase 12 已为 7 个纯虚补过同款）。

### 2.4 `ECDI/include/ECDI/Window/Window.h`（增量：R3 转发 + R4 关闭入口）

```cpp
#include "ECDI/Window/WindowState.h"   // 新增 include（GetWindowState 返回类型）

	public:
		/// @brief 查询当前窗口状态（Phase 13 R3——转发平台层，事实唯一来源在平台层）
		WindowState GetWindowState() const noexcept;

		/// @brief 请求关闭本窗口（Phase 13 R4——**框架侧关闭请求入口**，初设新发现）
		/// @details 语义 = 「用户请求关闭」：构造 `WindowCloseRequestedEvent` 派发给 Application，
		/// 与系统 X（`WM_CLOSE` → 平台翻译同一事件）**完全同路径** ⇒ 应用的 `OnWindowCloseRequested`
		/// **同步派发**（v1.1 定案——评审 §2 确认）：调用即完成事件派发与**全部 handler 执行**，返回时应用拦截逻辑已跑完；
		/// 无队列、无延迟——与 `Application::Create` 手动派发 `WindowCreatedEvent` 同款先例。
		/// 拦截逻辑对自绘关闭按钮同样生效（D8 的落地形态）。
		/// ⚠️ 与 `Release()` 的区别：`Release()` 是**销毁句柄**（资源层），`RequestClose()` 是**请求**
		/// （语义层，可被拦截 / 可被拒绝）。⚠️ 派生自 `Application::Create` 手动派发 `WindowCreatedEvent` 的既有先例。
		void RequestClose();
```

> **为什么必须有这个入口**：需求 R4 说「close 按钮发关闭请求」，但框架侧**当前不存在**发起关闭请求的 API（只有平台层 `WM_CLOSE` 翻译这一条来源）。若不补，CaptionBar 只能直调 `Release()`——那正是 D8 明确否决的语义分裂。**这是初设在实现形态上补的第一处需求空隙。**

### 2.5 新建 `ECDI/include/ECDI/Window/CaptionBar.h`（Public 头 **85 → 86**）

```cpp
#pragma once

#include "ECDI/Widget/Panel.h"

#include <string>

namespace ECDI{

class Window;
class Label;
class Widget;

/// @brief 自绘标题栏（Phase 13 R1）——Borderless 窗口的标题文本 + 最小化/最大化·还原/关闭
/// @details **组合式控件**（D1-A：CaptionBar 是 Widget，不是 Window 的隐式组成部分）：
/// 内部持 1 个标题 Label + 3 个按钮子控件；按钮为矢量自绘（D4），零资源依赖。
///
/// 使用（把「行为区」与「实体区」设为同值——D7 不联动，建议一致）：
/// ```cpp
/// ECDI::Window& w = app.Create("标题", 900, 600);
/// w.SetChromeMode(ECDI::ChromeMode::Borderless);
/// w.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);   // 行为区
/// auto bar = std::make_unique<ECDI::CaptionBar>(w, "标题");
/// bar->SetSize(w.GetRootWidget().GetWidth(), ECDI::CaptionBar::kDefaultHeight);   // 实体区
/// w.GetRootWidget().AddChild(std::move(bar));
/// w.Show();
/// ```
///
/// 生命周期（B 契约）：本控件**非拥有** `Window&`——Window 对象归 Application；
/// 本控件只发起窗口命令（Minimize / Maximize / Restore / RequestClose）与状态查询。
class CaptionBar : public Panel{
public:

	/// @brief 建议高度（DIP）——与 `Window::SetCaptionHeight` 建议同值（**框架不联动**，D7）
	static constexpr int kDefaultHeight = 32;

	/// @brief 构造
	/// @param window 所属窗口（**非拥有**引用——B 契约）
	/// @param title  标题文本（可后改）
	explicit CaptionBar(Window& window, const std::string& title = std::string());

	/// @brief 设置标题文本
	void SetTitle(const std::string& title);

	/// @brief 标题文本（只读）
	const std::string& GetTitle() const noexcept;

private:

	Window& m_window;	///< 非拥有（D1-A / B 契约）

	Label* m_title = nullptr;	///< 标题（树内稳定——AddChild 前抓取先例）
	Widget* m_minButton = nullptr;	///< 最小化（内部实现类——见 src/Window/CaptionButton.h）
	Widget* m_maxButton = nullptr;	///< 最大化·还原（二态——§3.3）
	Widget* m_closeButton = nullptr;	///< 关闭

};

}
```

> **公开面刻意极小**：只有「构造 + 标题读写 + 一个高度常量」。按钮可见性、自定义回调、样式参数一律**不做**（无消费者——YAGNI；见 §8 O2）。

### 2.6 新建 `ECDI/src/Window/CaptionButton.h`（**内部头**——不进 Public 头计数）

```cpp
#pragma once

#include "ECDI/Widget/Widget.h"

#include <functional>

namespace ECDI{

/// @brief CaptionBar 内部按钮（矢量自绘——min / max·restore / close 三种 glyph）
/// @details **内部头**（`src/` 下沉先例）：不进 Public 头计数，不对外承诺。
/// 二态由 owner 每帧设置（§3.3），无状态订阅。
class CaptionButton final : public Widget{
public:

	enum class Glyph{ Minimize, Maximize, Restore, Close };

	CaptionButton(Glyph glyph, std::function<void()> onClick);

	/// @brief 切换 glyph（二态：Maximize ⇄ Restore——owner 于 OnPaint 前设置）
	void SetGlyph(Glyph glyph) noexcept{ m_glyph = glyph; }

	/// @brief D9 能力声明：按钮消费鼠标输入 → 命中该按钮的 caption 区返回 HTCLIENT
	bool ConsumesMouseInput() const noexcept override{ return true; }

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;
	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;
	void OnMouseButtonUp(const MouseButtonUpEvent& event) override;
	void OnMouseEnter() override;
	void OnMouseLeave() override;

private:

	Glyph m_glyph;
	std::function<void()> m_onClick;

	bool m_hovered = false;
	bool m_pressed = false;

};

}
```

---

## 3. 实现分解

### 3.1 命中委托链（R2 / D2 / D9——本阶段核心）

**唯一改动点**：`Win32PlatformWindow.cpp` 的 `WM_NCHITTEST` caption 分支（现为 `if (y < caption && caption > 0) return HTCAPTION;`）：

```cpp
	// 标题栏区 → HTCAPTION（拖动移动 / 双击最大化 / Aero Snap 系统免费获得）
	// ⚠️ 判定顺序：先 resize 边（上一条），后 caption——保证窗口最上缘是缩放手感
	if (y < caption && caption > 0){

		// ★ Phase 13 R2：caption 区内**先问 Host**——命中消费鼠标的控件（如 CaptionBar 按钮）
		//   则让本次命中走客户区（break → DefWindowProc → HTCLIENT），鼠标事件正常派发给控件。
		//   ⚠️ 委托范围仅限本分支（不是全窗口每点）——命中测试高频调用，成本可控（§4.3）。
		if (m_host.IsClientInteractiveAt(x, y)){

			break;

		}

		return HTCAPTION;

	}
```

> ⚠️ **消息结构固定（v1.1——评审 §6 追问，实施期不得变更）**：`break` 之后**不是**直接 `return HTCLIENT`，而是跳出 `switch`，由 `DefWindowProc` 返回 `HTCLIENT`——源码 `Win32PlatformWindow.cpp:281` 的既有注释已写明此意图（「客户区 → DefWindowProc（返回 HTCLIENT，交框架派发鼠标事件）」）。Phase 13 **只新增一个前置分支**（Host 委托），**不改动**这条既有结构 ⇒ 命中可交互控件时**复用同一个 `break`**（与「非 caption 区」走完全相同的出口），而不是自己 `return HTCLIENT`。这条纪律的理由：`break → DefWindowProc` 是 Phase 12 已验证（T2/T3/T5/T6）的路径，另开一条 = 凭空引入新的命中变体。

**调用链**（平台层 → 框架层，平台不认识 Widget）：

```
WM_NCHITTEST (screen pt)
   ↓ 转窗口局部 (x, y)
   ↓ ① resize 九宫格（最高优先——T2 原断言不变）
   ↓ ② y < caption（物理像素，DipToPixels）
        ↓ Host::IsClientInteractiveAt(x, y)          ← 新增契约调用
             ↓ Window::IsClientInteractiveAt
                 ↓ m_rootWidget->HitTest(x, y)        ← 既有机制（Visible && Enabled && ContainsPoint）
                     ├─ 命中 Widget* w
                     │     ↓ w->ConsumesMouseInput()  ← 新增能力声明（D9）
                     │        ├─ true  → 返回 true  → break → HTCLIENT（事件派发给控件）
                     │        └─ false → 返回 false → HTCAPTION（拖标题文字可移窗）
                     └─ 未命中 → false → HTCAPTION（空白区即拖拽区）
        ↓
   ↓ ③ 超出 caption → 正常 client（HTCLIENT）
```

**三层判定顺序**（与需求 §4.1 一致）：**resize > caption 区内可交互控件 > caption 内未命中（`HTCAPTION`）> 超界（`HTCLIENT`）**。

**前提（v1.1 补——评审 §1 追问，已源码核实）**：整条委托链的正确性依赖一条**既有事实**——`Widget::HitTest` **返回最深层命中控件**，而非中间容器。源码 `src/Widget/Widget.cpp:105–145`：先过滤 `!IsVisible()` / `!IsEnabled()`（直接 `nullptr`）→ **逆序**遍历 children（Z-Order 后添加者在上）→ 每个子控件做局部坐标换算后**递归** → 子命中即 `return target` → 全部落空才 `if (ContainsPoint(x, y)) return this`。

⇒ CaptionBar 的复合结构下，按钮中心返回的是 **`CaptionButton`**（`ConsumesMouseInput() == true`），**不会**返回 `CaptionBar` 容器（默认 `false`）。**架构无需改动**——本条只是把这个既有前提**写死为契约**，防止实施期误改 `HitTest` 语义或误加容器级拦截。

⇒ **附带推论（既有机制白得）**：禁用 / 隐藏的按钮**天然落回 `HTCAPTION`**——`IsEnabled()` / `IsVisible()` 过滤发生在 `HitTest` 内部。即「按钮不可用 → 该区域变回拖拽区」，与 §4.3 失败模式表同源。

### 3.2 CaptionBar 内部结构（R1 / R6）

- 基类 `Panel`（有背景 —— 标题栏底色；默认透明，可 `SetStyle(PanelStyleOverride{...})`）。
- 子控件：`Label`（标题）+ 3 个 `CaptionButton`；构造时按顺序 `AddChild` 并抓取裸指针（树内地址稳定先例：ModelProbe 各控件）。
- 布局：**不设内部 Layout**（与 ModelProbe 的 `btnRow` 同款——手工 `SetPosition/SetSize`）。按钮靠右依次排布，宽度 = 高度（正方形热区），标题占左侧剩余，`PushClip` 防溢出（§6 待定项）。
- 尺寸：由使用方 `SetSize` 决定（`kDefaultHeight` 为建议值）；随窗口 resize 跟随由外层布局负责（9.7）。

### 3.3 按钮绘制与视觉态（R1 / R5）

`CaptionButton::OnPaint`（矢量——先例：`CheckBox` 用 `DrawLine` 画勾）：

| glyph | 画法（`PaintContext` 现成 API） |
|---|---|
| Minimize | 一条水平 `DrawLine`（宽 ~10px，居中偏下） |
| Maximize | `DrawRect` 描边方框（~10×10） |
| Restore | 两个错位方框（前框 + 右上角第二框的两条边） |
| Close | 两条对角 `DrawLine`（~10×10） |

**视觉态**：`m_hovered` → 背景 `DrawRect`（半透明提亮）；`m_pressed` → 更深一档；`Close` 悬停用红色系（系统惯例）。悬停态由**既有 Hover 机制**驱动（`OnMouseEnter/Leave`——`Button` 同款先例）。

**二态刷新（关键——零新接缝）**：`CaptionBar` 在**绘制前**把 `m_maxButton->SetGlyph(...)` 设为当前状态对应的 glyph；状态读取 = `m_window.GetWindowState()`（R3）。**无需订阅事件**：最大化/还原**必产生 `WM_SIZE`** → 重绘 → glyph 自然刷新（最小化到任务栏无需重绘）。⇒ 不引入「CaptionBar 监听 Window 状态」的新接缝（与 D6「状态是事实、由 Window 提供」一致）。

### 3.4 close 路径（R4 / D8）

```
CaptionButton(Close) 点击
   ↓ CaptionBar 绑定：m_window.RequestClose()          ← §2.4 新增入口
   ↓ 构造 WindowCloseRequestedEvent(this) + OnEvent 派发（手动派发——WindowCreatedEvent 先例）
   ↓ Application::OnWindowCloseRequested（**可被应用 override 拦截**）
   ↓ 基类默认实现：event.GetWindow()->Release() → 销毁 HWND（既有路径）
```

⇒ 自绘 X 与系统 X **同一语义通道**：ModelProbe 的后端清理（挂 `OnWindowCloseRequested`）对两者一视同仁。

### 3.5 状态查询链（R3）

```
Window::GetWindowState()  → m_platformWindow->GetWindowState()
Win32PlatformWindow::GetWindowState()  → return m_lastWindowState;   // WM_SIZE 时更新（Phase 12 既有）
测试替身 GetWindowState()  → return WindowState::restored;            // stub
```

---

## 4. 契约

### 4.1 坐标契约（`IsClientInteractiveAt` 的入参）
`x, y` = **窗口局部坐标**。前提：Borderless 下 `WM_NCCALCSIZE` 使**客户区 = 整窗** ⇒ 窗口局部坐标与客户区坐标一致，可直接喂 `RootWidget::HitTest`。Normal 模式下该路径**永不执行**（`m_chromeMode != Borderless` 早退）——坐标歧义不存在。

### 4.2 D7 职责二分（契约条文——不联动）
| 量 | 职责 |
|---|---|
| `captionHeight` | **系统标题栏行为区**——范围内未落在消费鼠标的控件上 → `HTCAPTION` |
| CaptionBar 高度 | **可交互实体区**——命中消费鼠标的控件 → `HTCLIENT` |

框架**不**把 Bar 高度隐式写回命中区；建议使用方设同值（`kDefaultHeight` 为建议常量）。

### 4.3 失败模式与安全（新增——初设补强）
| 场景 | 行为 |
|---|---|
| `m_rootWidget == nullptr`（构造早期） | `IsClientInteractiveAt` 返回 `false`（早退）→ `HTCAPTION`，无 UB |
| 控件被禁用 / 隐藏 | `HitTest` 天然过滤（`Visible && Enabled`）⇒ 落回 `HTCAPTION`（拖拽仍可用——**良性**） |
| 命中面积极小（1px 按钮） | 不特判；命中即为命中（使用方负责给足热区） |
| 性能 | 委托**仅在 caption 条带内**发生（非全窗口每点）；`HitTest` 与 `OnMouseMove` 同级开销，属既有量级 |

---

## 5. 影响面

| 类别 | 文件 | 改动 |
|---|---|---|
| **新增 Public** | `include/ECDI/Window/CaptionBar.h` | 全文（§2.5）——**85 → 86** |
| Public 修改 | `Widget/Widget.h` | +`ConsumesMouseInput()`（非纯虚，默认 false） |
| Public 修改（能力 override） | `Widget/Button.h` · `Widget/StateWidget.h` · `Widget/TextBox.h` | 各 +1 行 `override { return true; }` |
| Public 修改 | `Platform/PlatformWindowHost.h` | +`IsClientInteractiveAt`（**纯虚**） |
| Public 修改 | `Platform/PlatformWindow.h` | +`GetWindowState()`（**纯虚**）+ include `WindowState.h` |
| Public 修改 | `Window/Window.h` | +`GetWindowState()` / +`RequestClose()` + include `WindowState.h` |
| **新增 Internal** | `src/Window/CaptionButton.h` / `.cpp` | 内部按钮（矢量 glyph） |
| **新增 Internal** | `src/Window/CaptionBar.cpp` | CaptionBar 实现（组合 + 命令绑定 + 二态刷新） |
| Internal 修改 | `src/Platform/Win32/Win32PlatformWindow.cpp` | NCHITTEST caption 分支（§3.1）+ `GetWindowState` 实现 |
| Internal 修改 | `src/Window/Window.cpp` | Host 实现 `IsClientInteractiveAt` + 两个转发（`GetWindowState` / `RequestClose`） |
| **测试替身同步（3 处——预检所得）** | `src/Tests/EventTests.cpp:44` `FakeHost` | +`IsClientInteractiveAt` stub（纯虚） |
| | `src/Tests/AnimationTests.cpp:23` `TestPlatformWindow` | +`GetWindowState` stub（纯虚） |
| | `src/Tests/ProgressBarTests.cpp:29` `TestPlatformWindow` | +`GetWindowState` stub（纯虚） |
| 测试新增 | `src/Tests/CaptionBarTests.cpp` + `RunAllTests.h/.cpp` 注册 | §7 |
| 构建 | `ECDI/ECDI.vcxproj` | +2 `ClCompile`（CaptionBar / CaptionButton）+ 1 `ClInclude`（CaptionBar.h）；CMake glob 零维护 |
| 消费者 | `examples/ModelProbe` | 挂 CaptionBar（评估替换临时「窗口控制」按钮行） |
| 文档 | `docs/README.md` + 本文件 | 索引与状态 |

**明确不动**：B 契约（`window-ownership.md`）、渲染四层、`WM_NCCALCSIZE` / `WM_NCACTIVATE` / `WM_WINDOWPOSCHANGING`、ChromeMode 一次确定语义、`DipToPixels` 换算点。

---

## 6. 待定项（详设解决）

| # | 待定 | 倾向 |
|---|---|---|
| 1 | 按钮几何（宽高/间距/内边距/标题左边距） | 常量集中在 `CaptionBar.cpp` 匿名 namespace |
| 2 | 标题过长 | `PushClip` 裁切（**不做省略号**——O(n²) 截断教训见 `roadmap-deferred` #10） |
| 3 | 悬停/按下色值 | `CaptionBar.cpp` 内常量；`SetStyle` 覆盖留待二次用例 |
| 4 | glyph 线宽与像素对齐 | `DrawLine` 默认宽；圆角无关（直线/描边） |
| 5 | 是否复用 `Button` 基类做按钮 | **否**——`Button` 带文本/圆角/主题语义，glyph 按钮用轻量 `Widget` 派生（§2.6） |

---

## 7. 测试方向

| 组 | 内容 | 判据 |
|---|---|---|
| **T13-1 命中委托三态**（D9 回归锚） | 构造 Borderless 窗口 + CaptionBar（同值高度）；`SendMessageW(WM_NCHITTEST)`（**不 Show**——Phase 12 D-TST-1 先例）：① 标题 Label 覆盖区 → **`HTCAPTION`**；② 按钮中心 → **`HTCLIENT`**；③ Bar 空白 → **`HTCAPTION`**；④ `x < inset` → `HTLEFT`（**resize 优先**） | 四态区分；防「HitTest 非空即 HTCLIENT」回退 |
| **T13-2 按钮命令四路径** | 探针驱动按钮点击（或直接调 CaptionBar 命令路径）→ min / max + restore / close：状态事件回流断言（`minimized` / `maximized` …）+ close 走 `OnWindowCloseRequested`（**可拦截**——用 `TestApp` 计数） | 四路径生效 + D8 语义 |
| **T13-3 状态查询一致性** | `GetWindowState()` 与最后一次 `WindowStateChangedEvent` 一致；`Maximize()` → `maximized`，`Restore()` → `restored` | 事实/通知不矛盾 |
| **T13-4 CaptionBar 组合** | 标题 `SetTitle` 生效；最大化后 max 按钮 glyph 切到 Restore | **`RecordingBackend` 命令断言**（v1.1 定案——评审 §8；**不得为测试加 Public API**） |
| **回归** | **T0–T7a 全量零回归**（重点 T2 / T3 / T5 / T6——全部依赖 NCHITTEST） | 零失败 |
| **手测** | ModelProbe：拖标题文字移窗 / 按钮点击 / hover / close 拦截 / 最大化后 glyph 变化 | 视觉 + 日志 |

---

> **T13-4 判据定案（v1.1——评审 §8 追问）**：**优先 `RecordingBackend` 命令断言**，**绝不为测试新增 Public API**（不提供 `GetGlyph()` / `GetMaxButtonForTesting()` 一类测试缝）。理由：① `CaptionButton` 本就**刻意不对外承诺**（`src/` 内部头）；② 「为测试暴露生产 API」直接破坏 O2 的「公开面极小」。**可行性**：Maximize（`DrawRect` 描边方框）与 Restore（两个错位方框）在命令流上**可区分**（命令条数 / 参数不同）。若详设实测发现命令流**不足以稳定区分**，**再**讨论内部测试 seam——**不在初设提前决定**。

## 8. 开放决策点（含初设新发现）

| # | 决策 | 选项 | 倾向 | 说明 |
|---|---|---|---|---|
| **O1** | **关闭请求入口形态（初设新发现——需求 R4 的空隙）** | 新增 `Window::RequestClose()` / 让 CaptionBar 直调 `Release()` / 用回调注入 | **新增 `RequestClose()`** | R4 只说「走关闭请求」，但框架侧**当前无该入口**；直调 `Release()` 正是 D8 否决的语义分裂。与 `Application::Create` 手动派发 `WindowCreatedEvent` 同构（既有先例） |
| **O2** | CaptionBar 是否提供按钮可见性 / 自定义回调 / 样式参数 | 提供 / 不提供 | **不提供**（YAGNI——无消费者；应用要拦截已在 `OnWindowCloseRequested`） | 后续有真实用例再开 |
| **O3** | `IsClientInteractiveAt` 纯虚 vs 默认 `false` | 纯虚（编译期强制）/ 非纯虚 | **纯虚** | 与 Phase 12 同规格；遗漏实现即编译错（成本已核：**2 个实现者**，其中 1 个测试替身） |
| **O4** | `GetWindowState` 挂在 `PlatformWindow`（事实源）vs Window 侧缓存 | 平台查询 / Window 缓存事件 | **平台查询** | 「状态是事实」——缓存 = 双份维护（D6 同款论证）；成本已核：3 个实现者 |
| **O5** | 是否做 Debug 期一致性检查（Bar 高度 vs `captionHeight`） | 做 / 不做 | **不做**（不联动、不检查——文档说明 + `kDefaultHeight` 建议值即可） | YAGNI；误配后果轻微（拖拽区大小不符预期） |
| **O6** | `ConsumesMouseInput` 坐标无关（本文）vs 点查询（需求稿） | 坐标无关 / 点查询 | **坐标无关**（本文修正） | `HitTest` 只返回控件指针、不返回局部坐标 ⇒ 点查询需二次坐标换算；局部可交互由**复合控件**表达（正是 D1-A 的选择） |

---

## 9. 评审响应（v1.0 → v1.1）

外部评审结论：**通过——无架构级别问题，未发现需推翻路线的设计，可进入详细设计**。

**评审确认、本文不改**：Phase 12→13 切分 · D9 委托链方向 · `RequestClose` 属「需求实现所必需的最小 API 补充」（非功能扩张） · `CaptionButton` 不复用 `Button`（避免为复用而复用） · CaptionBar 公开面极小（未出现一堆 `SetXxxButtonVisible` 式投机配置） · D7 高度解耦语义自洽 · 状态查询挂平台（继续贯彻「状态是事实」） · 影响面预检（`FakeHost` 等替身提前查出） · **非虚 / 纯虚的层次划分**（`ConsumesMouseInput` 非虚 = Widget 能力扩展，默认即可；`IsClientInteractiveAt` / `GetWindowState` 纯虚 = 契约必须明确实现）。

**5 条进详设前确认的落点**：

| # | 评审建议 | 处理 | 落点 |
|---|---|---|---|
| 1 | 明确 `HitTest()` 返回**最深层**命中 Widget（否则按钮命中得到 `CaptionBar` ⇒ D9 判据失效） | **采纳 · 源码核实为既有事实**（`Widget.cpp:105–145` 逆序递归、子命中即返回）——**写死为契约前提**，架构不动 | §3.1 前提段 |
| 2 | 明确 `RequestClose()` **同步派发** `WindowCloseRequestedEvent` | **采纳 · 定案同步**——调用返回时拦截逻辑已执行完，与 `WindowCreatedEvent` 手动派发同款 | §2.4 注释 |
| 3 | T13-4 **优先 `RecordingBackend`**，不为测试增加 Public API | **采纳 · 定案**——禁 `GetGlyph()` 类测试缝；命令流不足以区分时**再议**内部 seam（不提前决定） | §7 T13-4 注 |
| 4 | 详设固定 `break → DefWindowProc → HTCLIENT`，不得改走另一条命中路径 | **采纳 · 结构固定**——引 `Win32PlatformWindow.cpp:281` 既有注释为证；只**加前置分支**，命中也复用同一 `break` 出口 | §3.1 结构固定段 |
| 5 | §6 五个待定项全部留详设，无需回退需求阶段 | **确认保留**——五项均属几何 / 色值 / 线宽 / 复用判定的实现细节 | §6（表不动） |

**评审指出的本阶段核心成果（记录）**：`WM_NCHITTEST → Host → Widget::HitTest → Widget::ConsumesMouseInput` 这条链让 **Win32 平台层完全不知道 CaptionBar / Button 甚至 Widget 是什么**，却能把标题栏按钮准确交给既有 Widget 输入系统——§3.1 的调用链即此成果的落地形态。

## 10. 修订记录

- v1.2（2026-09-14）实现落地状态同步（补记）：原头部记「✅ v1.1 外部评审通过（可进入详细设计）」。**头草案 6 处全部落地**：`Widget::ConsumesMouseInput()`（非纯虚·默认 false）/ `PlatformWindowHost::IsClientInteractiveAt()`（纯虚）/ `PlatformWindow::GetWindowState()`（纯虚，读 `m_lastWindowState`）/ `Window::GetWindowState()` + `RequestClose()`/ 新建 `CaptionBar.h`（Public 头 85 → **86**）/ 内部 `CaptionButton.h`。**三处初设新发现全部兑现**：`RequestClose()` 入口补齐、`FakeHost` + 2×`TestPlatformWindow` 替身同步、`ConsumesMouseInput` 坐标无关形态。**实施期与初设的 6 处精化见详设 v1.2**（`SetSize` 内重排 / 标题色注入 / 命令缓冲断言 / 合成事件 / 集成顺序）。
- v1.1（2026-09-13）**外部评审「通过——可进入详细设计」+ 5 条进详设前确认已全部落文**（新增 §9 评审响应）：
  - **① §3.1 补 `HitTest` 前提段（评审 §1）**：源码核实 `Widget::HitTest` 为**深度优先逆序递归**、**返回最深层命中控件**（`src/Widget/Widget.cpp:105–145`）⇒ 按钮命中得到 `CaptionButton` 而非 `CaptionBar` 容器，D9 判据成立；**写死为契约前提**（架构不动）。附带推论：`IsEnabled/IsVisible` 过滤在 `HitTest` 内 ⇒ **禁用按钮天然落回 `HTCAPTION`**（与 §4.3 同源）。
  - **② §2.4 定案 `RequestClose()` 同步派发（评审 §2）**：调用即完成派发与全部 handler 执行，返回时拦截逻辑已跑完（无队列 / 无延迟）——与 `Application::Create` 手动派发 `WindowCreatedEvent` 同款。
  - **③ §7 T13-4 改为 `RecordingBackend` 命令断言（评审 §8）**：**不得为测试新增 Public API**（禁 `GetGlyph()` 类测试缝）——`CaptionButton` 刻意不对外承诺；Maximize / Restore 两态命令流可区分；不足以区分时**再议**内部 seam（**不提前决定**）。
  - **④ §3.1 固定消息结构（评审 §6）**：`break → DefWindowProc → HTCLIENT` 引 `Win32PlatformWindow.cpp:281` 既有注释为证；Phase 13 只加前置分支、命中也复用同一 `break` 出口——因该路径已被 Phase 12 的 T2/T3/T5/T6 验证，另开一条 = 凭空引入命中变体。
  - **⑤ §9 新增评审响应块**（评审确认不变项 + 5 条确认落点表 + 核心成果记录）；§6 五项确认留详设、**不回退需求阶段**。
- v1.0（2026-09-13）**初步设计初稿**：
  - **范围映射**（R1–R8 → 设计域）；**头草案 6 处**（`Widget.h` / `PlatformWindowHost.h` / `PlatformWindow.h` / `Window.h` / 新建 `CaptionBar.h`（85→86）/ 内部 `CaptionButton.h`）。
  - **命中委托链**（§3.1）：NCHITTEST caption 分支加 Host 委托 + 三层判定顺序 + 完整调用链；委托**仅限 caption 条带**（性能说明）。
  - **三处初设新发现**：① **`Window::RequestClose()` 入口缺失**（需求 R4 的空隙——O1）；② **第三个 Host 实现者 `FakeHost`**（`EventTests.cpp:44`，条 33 预检所得——§5 已列入必须同步）；③ **`ConsumesMouseInput` 改为坐标无关形态**（`HitTest` 不返回局部坐标 —— O6 修正需求稿的点查询草案）。
  - **零新接缝的二态刷新**（§3.3）：Maximize/Restore 必产生 `WM_SIZE` → 重绘 ⇒ glyph 自然刷新，无需订阅状态事件。
  - **契约**（§4）：坐标契约 / D7 职责二分 / 失败模式与性能（新增）。
  - **测试**（§7）：T13-1 命中三态（D9 回归锚）/ T13-2 ~ T13-4 + T0–T7a 零回归；**开放决策点 O1–O6 含倾向**。
  - **未做**：未改动任何代码；详设需落「逐文件精确改动 + 按钮几何常量 + glyph 坐标表 + 测试探针细节」。
