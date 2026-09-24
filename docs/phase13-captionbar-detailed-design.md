# Phase 13 CaptionBar 自绘标题栏 详细设计（v1.5）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-14（v1.3 同日：实现落地 → 消费者集成 → A5 判据修正；**v1.4 2026-09-15：A 项收口 + flaky 根因修复**；**v1.5 2026-09-15：ModelProbe 默认形态翻转为自绘标题栏**）
> 状态：**✅ 已实现并验收（2026-09-14 ~ 09-15）**——2 新建 + 8 修改 + 3 处替身同步全部落地；**A1–A6 全部通过**：四构建 **188 passed / 0 failed**、ModelProbe 手测 6 项 + Normal 零回归通过、静态自查（A6）通过（`IsClientInteractiveAt` override = **2**（`Window` / `FakeHost`）、`GetWindowState` override = **3**（`Win32PlatformWindow` / 2×`TestPlatformWindow`）、触碰文件 BOM 全绿）；测试用例 **183 → 188**（17 → 18 测试文件）。**收口**：`AntiAliasing.GDIRadiusZeroBitwise` flaky 根因定性为 Window Ghosting 并修复（`DisableProcessWindowsGhosting()`），诊断脚手架收进失败分支。原状态「✅ v1.1 外部评审通过（可进入实现）」见 §12 修订记录。**v1.5（2026-09-15）**：ModelProbe 默认形态翻转为**自绘标题栏**（新增 `--native` 回退系统标题栏）——**A4 的 Normal 复跑命令随之变为 `modelprobe.exe --native`**（§8 A4 行已注明），详见 §12 v1.5
> 前置：`phase13-captionbar-requirements.md` **v1.1** ✅ / `phase13-captionbar-preliminary-design.md` **v1.1** ✅（外部评审通过，可进详设）
> 一句话：把初设的 6 处头草案与命中委托链，落成**可直接照做的逐文件改动规格**（每处标注「动哪几行 / 不动哪些行」）+ 精确到坐标的测试规格 + 工程注册与验收清单——**不换 Phase 12 路线**（不动 NCCALCSIZE / NCACTIVATE / WINDOWPOSCHANGING，不动 B 契约与渲染四层）。
> v1.1 修订：**P0-1 修正** §2.3 述语替身返回类型（`void` → `WindowState`，确凿编译错误）· **P0-2 改写** 原 L4（`minimized` 不是 CaptionBar 可达交互路径 → 移出「已知局限」，改述为实现语义说明）· **P1-3 收紧** §4.3「负坐标无害」的绝对化表述 · **P1-5 明确** A6 实现者枚举（1 生产 + N 替身）· **P1-2 精确化** L2 措辞（`m_title` private ⇒ 「无公开标题样式入口」）· **新增** `SetSize` 可重复调用契约与 T13-4 断言分层原则 · **P1-4 否决**（`m_closeButton` 被 `RelayoutChildren` 使用——附依据）
> v1.2 修订：**实现落地状态同步（补记）**——实施期 6 项偏差/发现已列于 §12 v1.2 条目；新增 **L6 坐标系局限**（`WM_NCHITTEST` 物理像素 vs widget 逻辑像素，非 100% DPI 时命中委托坐标偏移）；T13-1 探针 ⑦ 改为客户区中部（`kWinH/2`）以避开 DPI 相关的条带边界
> v1.3 修订：**A5 判据修正**（原「`grep -c _DEBUG`」对 MSVC / ClangCL **会误判**——二者靠 debug CRT 选项 `-MDd` 隐含定义 `_DEBUG`，ninja 文本中仅 1 处命中；修正为**按工具链二分**）· **§7 消费者集成已实施**（`examples/ModelProbe/main.cpp`，用户 2026-09-14 单独授权）· §8 A5 附四构建实测
> v1.4 修订：**A 项收口 + `AntiAliasing.GDIRadiusZeroBitwise` flaky 根因修复**（Window Ghosting ⇒ `DisableProcessWindowsGhosting()`）——详见 §12 v1.4 条目；本行系事后补记（头部当时未单列 v1.4 修订行）
> v1.5 修订：**ModelProbe 默认形态翻转为自绘标题栏**（`bool borderless = false → true`）+ 新增 `--native` 回退系统标题栏（保住 A4 零回归手测可复跑）；顺带收掉两处标题串硬编码（提为 `kWindowTitle` 常量）与 `captionHeight` 魔数（改用 `ECDI::CaptionBar::kDefaultHeight`）；**A4 复跑命令由 `modelprobe.exe` 变为 `modelprobe.exe --native`**（§8 A4 行已同步注明）；**未验证**：编译与手测待用户执行（AI 侧仅静态自查：BOM / diff 逐行 / 常量唯一性 / 括号配平全绿）

---

## 1. 范围与前置

### 1.1 需求 / 决策 → 本文落点

| 来源 | 内容 | 本文落点 |
|---|---|---|
| R1 | CaptionBar Widget（标题 + 三按钮，矢量自绘） | §2.5 / §2.6 / §3.3 / §3.4 |
| R2 + D2 + D9 | NCHITTEST ↔ Widget 树委托（「可交互」判据） | §2.1 / §2.2 / §2.4 / §3.1 |
| R3 | Window 状态查询 API | §2.3 / §2.4 / §3.6 |
| R4 + O1 + D8 | close 走关闭请求（新增框架侧入口） | §2.4 / §3.5 |
| R5 | 悬停 / 按下视觉态 | §3.4 |
| R6 | 布局集成（普通 Widget，零特判） | §2.5 / §3.3 / §7 |
| R7 | `captionHeight` 职责二分（不联动） | §4.2 |
| R8 | 回归底线 | §5 / §8 |
| D1 | 独立 Public Widget（用户组合挂载） | §2.5 |
| D3 | 头域 = Window 域（`include/ECDI/Window/`） | §2.5 |
| D4 | 矢量自绘（零资源） | §3.4 |
| D5 | 样式 = 构造参数 / StyleOverride（不做主题集成） | §3.3 / §4.4 |
| D6 | 状态 = Window 查询 API（非自缓存） | §3.6 |
| D7 | 高度解耦（行为区 / 实体区职责二分） | §4.2 |
| O2 | 公开面极小（不做按钮可见性 / 回调 / 样式参数） | §2.5 |
| O3 | `IsClientInteractiveAt` 纯虚 | §2.2 |
| O4 | `GetWindowState` 挂 `PlatformWindow`（事实源） | §2.3 |
| O5 | 不做 Debug 期高度一致性检查 | §4.3 |
| O6 | `ConsumesMouseInput` 坐标无关 | §2.1 |

### 1.2 初设 → 详设的 6 处精化（本文相对初设 v1.1 的实际增量）

| # | 精化 | 依据（均为本次读源码所得） |
|---|---|---|
| **P1** | CaptionBar **override `SetSize`** 在内部重排子控件（替代初设的「纯手工摆位」）——窗口 resize 零额外接线 | `VerticalLayout.cpp` 对 `stretch=0` 子控件执行 `child->SetSize(parent.GetWidth(), child->GetHeight())`；`Window::OnResized → m_rootWidget->Arrange()` ⇒ **宽度变化自动到达 SetSize**。先例：`CollapsiblePanel::SetSize` override（复合控件同步子几何） |
| **P2** | 标题越界**天然被裁切**，无需手工 `PushClip`（初设 §6 待定项 2 定案） | `Widget::Paint` 每个控件各自 `PushClip(自身矩形)`（`Widget.cpp:240`）⇒ Label 文本溢出被自身边界裁掉 |
| **P3** | v0.1 视觉常量落实现内；**标题前景色必须由 CaptionBar 注入**（不可依赖主题默认） | `DefaultTheme::GetTextStyle().foreground = Color::Black()` —— 黑字画在深色标题栏上不可读；而 O2 不提供标题样式 API ⇒ 若不注入则**消费者永远改不了标题色** |
| **P4** | 测试落点 `CaptionBarTests.cpp`；T13-4 判据 = **命令缓冲直接断言**，**零产品测试缝** | 既有先例 `CheckBoxTests.cpp:175-177`：`RecordingBackend backend; CommandBuffer commands; PaintContext ctx(commands, backend);` → `std::holds_alternative<...>(commands[i])`。`CaptionButton` 是内部头 ⇒ 测试可直接构造 |
| **P5** | 按钮命令路径测试用**合成 Event + `Application::OnEvent`**（公开入口），不伪造鼠标消息 | `Application` 继承 `EventRouter`，`OnEvent(const Event&)` 为 **public**；`MouseButtonDownEvent(Window*, x, y, MouseButton, bool)` 构造器 public ⇒ 可完整走 HitTest → Dispatch → Capture 链，无需 Show / 屏幕坐标换算 |
| **P6** | ModelProbe 集成必须把 CaptionBar **加在 page 之前** | `VerticalLayout` 按 `children` 顺序排布竖直次序（先前=上方）；demo 里 page 先 `AddChild` ⇒ 若 bar 后加，bar 会落到 page **下方** |

---

## 2. 文件级改动清单

> **规格纪律（skill 条 42 / 43）**：每个代码块都标注「**落点**」与「**其余行保持现状**」；未列出的文件一律不动。

| 类别 | 文件 | 改动性质 |
|---|---|---|
| Public 新增 | `ECDI/include/ECDI/Window/CaptionBar.h` | 全文（§2.5）——Public 头 **85 → 86** |
| Public 修改 | `ECDI/include/ECDI/Widget/Widget.h` | +1 条非纯虚能力声明（§2.1） |
| Public 修改 | `ECDI/include/ECDI/Widget/Button.h` · `StateWidget.h` · `TextBox.h` | 各 +1 行 `override { return true; }`（§2.1.1） |
| Public 修改 | `ECDI/include/ECDI/Platform/PlatformWindowHost.h` | +1 条纯虚（§2.2） |
| Public 修改 | `ECDI/include/ECDI/Platform/PlatformWindow.h` | +1 条纯虚 + 1 个 include（§2.3） |
| Public 修改 | `ECDI/include/ECDI/Window/Window.h` | +2 API + 1 个 include + 1 条 override 声明（§2.4） |
| Internal 新增 | `ECDI/src/Window/CaptionButton.h` / `.cpp` | 内部按钮（§2.6 / §3.4） |
| Internal 新增 | `ECDI/src/Window/CaptionBar.cpp` | CaptionBar 实现（§3.3 / §3.5 / §3.6） |
| Internal 修改 | `ECDI/src/Platform/Win32/Win32PlatformWindow.cpp` | NCHITTEST caption 分支（§3.1）+ `GetWindowState` 实现（§3.6） |
| Internal 修改 | `ECDI/src/Platform/Win32/Win32PlatformWindow.h` | +1 行 override 声明（§3.6） |
| Internal 修改 | `ECDI/src/Window/Window.cpp` | +3 个函数体 + 1 个 include（§2.4.2） |
| **测试替身同步（3 处——预检所得）** | `src/Tests/EventTests.cpp:44` `FakeHost` | +`IsClientInteractiveAt` stub |
| | `src/Tests/AnimationTests.cpp:23` `TestPlatformWindow` | +`GetWindowState` stub |
| | `src/Tests/ProgressBarTests.cpp:29` `TestPlatformWindow` | +`GetWindowState` stub |
| 测试新增 | `src/Tests/CaptionBarTests.cpp` + `RunAllTests.h` / `RunAllTests.cpp` 注册 | §5 |
| 工程 | `ECDI/ECDI.vcxproj` + `.filters` | +2 `ClCompile` + 2 `ClInclude`（§6） |
| 消费者 | `examples/ModelProbe/main.cpp` | 插入 CaptionBar（§7） |
| 文档 | `docs/README.md` + 本文件 | 索引与状态 |

**明确不动**：B 契约（`window-ownership.md`）、渲染四层、`WM_NCCALCSIZE` / `WM_NCACTIVATE` / `WM_WINDOWPOSCHANGING`、ChromeMode 一次确定语义、`DipToPixels` 换算点、`Renderer` / `RenderCommand` / `GDIBackend`、主题系统。

---

## 2.1 `ECDI/include/ECDI/Widget/Widget.h`

**落点**：public 区，`CanFocus()`（现 `Widget.h:175`）**之后**、`OnFocusGained()` 之前。

**新增**：

```cpp
	/// @brief 该控件是否消费鼠标输入（Phase 13 D9——NCHITTEST 委托判据）
	/// @details 语义：「被 HitTest 命中」**≠**「应阻止系统的标题栏拖拽」。默认 false（纯显示控件）——
	/// Label / Panel / ProgressBar / CollapsiblePanel 等命中后 caption 区仍返回 HTCAPTION
	/// （保证「拖标题文字可移动窗口」）；交互控件（Button / StateWidget / TextBox，及内部 CaptionButton）
	/// override 返回 true。
	/// ⚠️ **坐标无关**：HitTest 只返回控件指针、不返回局部坐标（若做成点查询需二次坐标换算）⇒
	/// 局部可交互需求由**复合控件**表达（如 CaptionBar 把按钮做成子控件——子命中拿到的就是按钮自身）。
	/// ⚠️ **与 CanFocus() 无关**：后者是键盘焦点语义，二者恰好近似但不可互推（复用会让两个概念互相绑架）。
	virtual bool ConsumesMouseInput() const noexcept { return false; }
```

**其余行保持现状**（本次不动 `CanFocus` / `ContainsPoint` / `HitTest` 的任何实现或签名）。

> **为什么放 public**：`Window::IsClientInteractiveAt` 不是 `Widget` 的友元，调用发生在 `Widget*` 静态类型上 ⇒ 必须是 public。与 `CanFocus()`（同族能力查询，亦 public）一致。
> **非纯虚 ⇒ 零破坏**：无派生类需要同步实现（无条 33 意义上的「实现者清单」问题）；`HitTest` / 既有控件行为完全不变。

### 2.1.1 override 清单（4 处，全部 `return true;`）

| 文件 | 落点 | 新增行 |
|---|---|---|
| `Widget/Button.h` | `bool CanFocus() const noexcept override { return true; }`（`:30`）之后 | `bool ConsumesMouseInput() const noexcept override { return true; }` |
| `Widget/StateWidget.h` | `bool CanFocus() const noexcept override { return true; }`（`:23`）之后 | 同上（覆盖 CheckBox / Radio） |
| `Widget/TextBox.h` | `bool CanFocus() const noexcept override { return true; }`（`:31`）之后 | 同上 |
| `src/Window/CaptionButton.h` | 见 §2.6 | 同上（内部类） |

**不 override（保持默认 false）**：`TextWidget` / `Label` / `Panel` / `CollapsiblePanel` / `ProgressBar` / `RootWidget`（= `Widget` 本身）。
> `CollapsiblePanel` 不 override 的依据：其 `header` **完全外部自组**（`CollapsiblePanel.h` 类注释硬契约「本控件不持有/管理/隐藏 header」）⇒ 面板自身不消费鼠标，语义与 `Panel` 一致。

---

## 2.2 `ECDI/include/ECDI/Platform/PlatformWindowHost.h`

**落点**：public 纯虚区末尾，`OnIMECompositionCommit`（现 `:55`）**之后**、类结束 `};` 之前。

**新增**：

```cpp
	/// @brief 客户区可交互命中查询（Phase 13 R2 / D2——NCHITTEST 委托）
	/// @param x,y 窗口局部坐标（Borderless 下客户区 = 整窗，两者坐标系一致——§4.1）
	/// @return true  = 该点落在**消费鼠标输入的控件**上 → 平台层让本次命中走客户区（HTCLIENT）
	///         false = 交 caption 区语义（HTCAPTION——拖拽 / 双击最大化·还原 / 系统菜单）
	/// @details 平台层**不认识 Widget**（分层律）——只问本契约；实现方（Window）转
	/// RootWidget::HitTest → Widget::ConsumesMouseInput（§2.4.2）。
	/// **纯虚**：遗漏实现 = 编译期暴露（与 Phase 12 的 7 个纯虚同规格）。
	virtual bool IsClientInteractiveAt(int x, int y) const noexcept = 0;
```

**其余行保持现状**。

> ⚠️ **实现者清单（全库 grep 已核，skill 条 33）**：
> ① `Window`（生产实现——§2.4.2）；② **`src/Tests/EventTests.cpp:44` 的 `FakeHost`**（测试替身）。
> 后者**必须同步补一行**，否则立刻变抽象类（`cannot declare variable to be of type abstract`）。补法：

```cpp
    // ── Phase 13：新增纯虚（本替身不关心客户区命中——恒定"不可交互"）──
    bool IsClientInteractiveAt(int, int) const noexcept override { return false; }
```

---

## 2.3 `ECDI/include/ECDI/Platform/PlatformWindow.h`

**落点 A（include）**：文件头 include 块末尾，`#include "ECDI/Window/WindowLayer.h"` 之后：

```cpp
#include "ECDI/Window/WindowState.h"   // Phase 13 新增（GetWindowState 返回类型需完整定义）
```

**落点 B（纯虚）**：public 区末尾，`Restore()`（现 `:112`）**之后**、类结束 `};` 之前：

```cpp
	// ── Phase 13：窗口状态查询（R3——**事实来源在本层**）────────────────

	/// @brief 查询当前窗口状态（Phase 13 R3）
	/// @details 与 `WindowStateChangedEvent` 互补：事件回答「变成了什么」，本查询回答「现在是什么」。
	/// 事实的唯一来源在平台层（`WM_SIZE` 时由 `IsIconic` / `IsZoomed` 判定并缓存）——**纯虚**。
	virtual WindowState GetWindowState() const noexcept = 0;
```

**其余行保持现状**。

> ⚠️ **实现者清单 = 3 个**：`Win32PlatformWindow`（§3.6，返回既有成员 `m_lastWindowState`）+ **`AnimationTests.cpp:23` / `ProgressBarTests.cpp:29` 的 `TestPlatformWindow`**（各补一行 stub）。两处已有「Phase 12：新增 7 个纯虚」分节注释 ⇒ 本次在**同一分节内**追加：

```cpp
	WindowState GetWindowState() const noexcept override { return WindowState::restored; }   // Phase 13：stub
```

> 两个测试文件均已 include `ECDI/Window/WindowState.h`（Phase 12 引入）——若实测缺失则补 include。

---

## 2.4 `ECDI/include/ECDI/Window/Window.h` + `src/Window/Window.cpp`

### 2.4.1 `Window.h`

**落点 A（include）**：`#include "ECDI/Window/WindowLayer.h"`（现 `:15`）之后：

```cpp
#include "ECDI/Window/WindowState.h"   // Phase 13 新增（GetWindowState 返回类型）
```

**落点 B（运行期 API——紧接 `Restore()` 之后，现 `:124`）**：

```cpp
		/// @brief 查询当前窗口状态（Phase 13 R3——转发平台层；**事实唯一来源在平台层**）
		/// @details 与 `Minimize/Maximize/Restore` 同组为「运行期状态面」；查询本身无 @pre
		/// （未 Show 时返回平台缓存的初始态 restored——即「现在是什么」的诚实回答）。
		/// @note 无状态缓存：本方法不做 Window 侧影子状态（避免双份维护——D6 / O4）。
		WindowState GetWindowState() const noexcept;

		/// @brief 请求关闭本窗口（Phase 13 R4——**框架侧关闭请求入口**）
		/// @details 语义 = 「用户请求关闭」：构造 `WindowCloseRequestedEvent` 派发给 Application，
		/// 与系统 X（`WM_CLOSE` → 平台翻译同一事件）**完全同路径** ⇒ 应用的 `OnWindowCloseRequested`
		/// 拦截逻辑对自绘关闭按钮同样生效（D8）。
		/// **同步派发**：调用即完成派发与全部 handler 执行，返回时拦截逻辑已跑完（无队列 / 无延迟）
		/// ——与 `Application::Create` 手动派发 `WindowCreatedEvent` 同款先例。
		/// ⚠️ 与 `Release()` 的区别：`Release()` 是**销毁句柄**（资源层，幂等），
		/// `RequestClose()` 是**请求**（语义层，可被拦截 / 可被拒绝——拦截方不调 `Release()` 即可）。
		/// @pre 无（Show 前后均可调用——语义层请求不依赖窗口可见性）
		void RequestClose();
```

**落点 C（Host 契约实现——private 区，与 `OnEvent` / `OnPaint` 等并列，现 `:215` 附近）**：

```cpp
		/// @brief 客户区可交互命中查询（Phase 13 R2——PlatformWindowHost 契约实现）
		/// @details 转 RootWidget::HitTest → Widget::ConsumesMouseInput（平台层不认识 Widget）
		bool IsClientInteractiveAt(int x, int y) const noexcept override;
```

**其余行保持现状**（不动构造器 / 成员顺序 / 既有 7 个 Host override）。

### 2.4.2 `Window.cpp`

**落点 A（include）**：include 块中 `#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"`（现 `:10`）之后：

```cpp
#include "ECDI/EventSystem/Window/WindowCloseRequsted.h"   // Phase 13 R4（文件名拼写沿用仓库既有：Requsted）
```

**落点 B（函数体）**：紧接 `Window::Restore()`（现 `:197-201`）之后、`// ── 动画系统（9.6）`分节之前，插入三个函数：

```cpp
// ── Phase 13：窗口状态查询（R3）──────────────────────────────────

WindowState Window::GetWindowState() const noexcept{

	return m_platformWindow->GetWindowState();

}

// ── Phase 13：关闭请求入口（R4——语义层/可拦截；与系统 X 同路径）────

void Window::RequestClose(){

	WindowCloseRequestedEvent event(this);

	// 同步派发（先例：Application::Create 手动派发 WindowCreatedEvent）
	m_application.OnEvent(event);

}

// ── Phase 13：客户区可交互命中（R2——Host 契约实现）──────────────

bool Window::IsClientInteractiveAt(int x, int y) const noexcept{

	// 防御：RootWidget 尚未就绪时早退（与 OnResized 的既有守卫同款）。
	// 可达性说明：WM_NCHITTEST 需要窗口可见才有鼠标命中，此时 RootWidget 必已构造
	// （构造期不产生鼠标命中）——本守卫是纵深防御，不是常规路径。
	if (!m_rootWidget){

		return false;

	}

	Widget* hit = m_rootWidget->HitTest(x, y);

	// D9 判据：命中「消费鼠标输入的控件」才算可交互——命中纯显示控件（如标题 Label）仍走 HTCAPTION
	return hit != nullptr && hit->ConsumesMouseInput();

}
```

**其余行保持现状**。

> `Widget.h` 已在该文件 include（现 `:6`）⇒ `HitTest` / `ConsumesMouseInput` 可见。
> `m_rootWidget` 是 `std::unique_ptr<Widget>`；在 `const` 成员函数里 `m_rootWidget.get()` 仍返回 `Widget*`（const 修饰指针本身而非 pointee）⇒ 调用非 const 的 `HitTest` 合法。

---

## 2.5 新建 `ECDI/include/ECDI/Window/CaptionBar.h`（Public 头 **85 → 86**）

```cpp
#pragma once

#include "ECDI/Widget/Panel.h"

#include <string>

namespace ECDI{

class Window;
class Label;
class CaptionButton;

/// @brief 自绘标题栏（Phase 13 R1）——Borderless 窗口的标题文本 + 最小化 / 最大化·还原 / 关闭
/// @details **组合式控件**（D1-A：CaptionBar 是 Widget，**不是** Window 的隐式组成部分）：
/// 基类 Panel（继承背景 / 圆角 / 边框能力，以及「自身永不参与命中」语义——`Panel::ContainsPoint` 恒 false
/// ⇒ 标题栏空白区天然是拖拽区），内部持 1 个标题 Label + 3 个按钮子控件（矢量自绘，零资源依赖）。
///
/// 使用（把「行为区」与「实体区」设为同值——D7 不联动，建议一致）：
/// ```cpp
/// ECDI::Window& w = app.Create("标题", 900, 600);
/// w.SetChromeMode(ECDI::ChromeMode::Borderless);
/// w.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);   // 行为区（系统标题栏命中区）
/// auto bar = std::make_unique<ECDI::CaptionBar>(w, "标题");
/// bar->SetSize(900, ECDI::CaptionBar::kDefaultHeight);    // 实体区（控件几何）
/// w.GetRootWidget().AddChild(std::move(bar));
/// w.Show();
/// ```
/// ⚠️ **宽度自适应**：本控件 override `SetSize` 在内部重排子控件；若父容器带 fillCrossAxis 布局
/// （如 RootWidget 的 VerticalLayout），窗口 resize → Arrange → SetSize → 子控件自动跟随（零额外接线）。
///
/// 生命周期（B 契约）：本控件**非拥有** `Window&`——只发起窗口命令
/// （Minimize / Maximize / Restore / RequestClose）与状态查询（GetWindowState）。
class CaptionBar : public Panel{
public:

	/// @brief 建议高度（DIP）——与 `Window::SetCaptionHeight` 建议同值（**框架不联动**，D7）
	static constexpr int kDefaultHeight = 32;

	/// @brief 构造（标题 + 三按钮子控件建好并入树）
	/// @param window 所属窗口（**非拥有**引用——B 契约）
	/// @param title  标题文本（可后改）
	explicit CaptionBar(Window& window, const std::string& title = std::string());

	/// @brief 设置标题文本（转发标题 Label）
	void SetTitle(const std::string& title);

	/// @brief 标题文本（只读）
	const std::string& GetTitle() const noexcept;

	/// @brief 设置几何（override：先记尺寸，再按新宽度重排子控件——标题占左、三按钮靠右）
	/// @details 与 `CollapsiblePanel::SetSize` 同款先例（复合控件在 SetSize 内同步子控件几何）。
	/// 宽度变化的每次调用（含父布局 fillCrossAxis 的每次 Arrange）都会重排 ⇒ 窗口 resize 零接线。
	/// ⚠️ **可重复调用契约**：本方法必须幂等——`RelayoutChildren` 只按**当前** w/h 重新计算，不读取上一次布局结果、无累积偏移（父布局每次 Arrange 都会调用它，故这是硬性前提）。
	void SetSize(int w, int h) override;

protected:

	/// @brief 绘制前刷新 max 按钮二态 glyph，再走 Panel 背景绘制
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:

	/// @brief 按当前宽高重排子控件
	void RelayoutChildren();

	/// @brief 最大化 ↔ 还原切换（读当前事实决定动作——状态不由本控件缓存，D6）
	void ToggleMaximizeRestore();

	Window& m_window;	///< 非拥有（D1-A / B 契约）

	Label* m_title = nullptr;			///< 标题（树内地址稳定——AddChild 前抓裸指针先例）
	CaptionButton* m_minButton = nullptr;	///< 最小化
	CaptionButton* m_maxButton = nullptr;	///< 最大化·还原（二态——§3.3）
	CaptionButton* m_closeButton = nullptr;	///< 关闭（→ m_window.RequestClose()）

};

}
```

> **公开面刻意极小**（O2）：只有「构造 + 标题读写 + 高度常量 + SetSize override」。按钮可见性、自定义回调、样式参数一律**不做**——无消费者（YAGNI）。
> **继承而来的能力**（零新增 API）：底色 / 边框 / 圆角经 `Panel::SetStyle(PanelStyleOverride)`（public 继承）。

---

## 2.6 新建 `ECDI/src/Window/CaptionButton.h`（**内部头**——不进 Public 计数）

```cpp
#pragma once

#include "ECDI/Widget/Widget.h"

#include <functional>

namespace ECDI{

/// @brief CaptionBar 内部按钮（矢量自绘——min / max·restore / close 四种 glyph）
/// @details **内部头**（`src/` 下沉先例）：不进 Public 头计数、不对外承诺。
/// **不复用 Button**（详设 §3.4 决策）：Button 带文本 / 圆角 / 主题 / 点击语义，
/// 与「无文本 glyph + 窗口命令」不是同一抽象——为复用而复用会把两套语义绑在一起。
/// 二态 glyph 由 owner（CaptionBar）在绘制前设置，**无状态订阅、无 Invalidate**。
class CaptionButton final : public Widget{
public:

	enum class Glyph{ Minimize, Maximize, Restore, Close };

	/// @param glyph   初始 glyph
	/// @param onClick 点击回调（Up 且鼠标仍在按钮内时触发；可为空）
	CaptionButton(Glyph glyph, std::function<void()> onClick);

	/// @brief 切换 glyph（二态：Maximize ⇄ Restore——owner 于绘制前设置）
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

	void RaiseClick();

	Glyph m_glyph;

	std::function<void()> m_onClick;

	bool m_hovered = false;
	bool m_pressed = false;

};

}
```

---

## 3. 关键实现规格

### 3.1 命中委托链（R2 / D2 / D9——本阶段核心，改动风险最高点）

**唯一改动点**：`ECDI/src/Platform/Win32/Win32PlatformWindow.cpp` 的 `WM_NCHITTEST` caption 分支。

**改动前**（现 `:273-279`，逐字）：

```cpp
		// 标题栏区 → HTCAPTION（拖动移动 / 双击最大化 / Aero Snap 系统免费获得）
		// ⚠️ 判定顺序：先 resize 边（上一条），后 caption——保证窗口最上缘是缩放手感
		if (y < caption && caption > 0){

			return HTCAPTION;

		}
```

**改动后**：

```cpp
		// 标题栏区 → HTCAPTION（拖动移动 / 双击最大化 / Aero Snap 系统免费获得）
		// ⚠️ 判定顺序：先 resize 边（上一条），后 caption——保证窗口最上缘是缩放手感
		if (y < caption && caption > 0){

			// ★ Phase 13 R2：caption 区内**先问 Host**——命中「消费鼠标输入的控件」（如 CaptionBar 按钮）
			//   则让本次命中走客户区（break → DefWindowProc → HTCLIENT，与「非 caption 区」同一出口），
			//   鼠标事件正常派发给控件；否则维持 HTCAPTION（拖拽 / 双击最大化 / 系统菜单）。
			//   委托范围**仅限本分支**（非全窗口每点）——命中测试高频调用，成本与既有判定同级（§4.3）。
			if (m_host.IsClientInteractiveAt(x, y)){

				break;

			}

			return HTCAPTION;

		}
```

**该函数内其余行保持现状**（resize 九宫格 / `IsZoomed` 门控 / `DipToPixels` 换算点 / 末尾 `break` 注释全部不动）。

> ⚠️ **消息结构固定（评审 §6 追问）**：`break` 之后**不是** `return HTCLIENT`，而是跳出 `switch` 由 `DefWindowProc` 返回 `HTCLIENT`——`Win32PlatformWindow.cpp:281` 的既有注释已写明此意图。Phase 13 **只加前置分支**，命中可交互控件时**复用同一个 `break`**：该出口已被 Phase 12 的 T2/T3/T5/T6 验证，另开一条 = 凭空引入新的命中变体。

**后端到前端的调用链**（平台层全程不认识 Widget）：

```
WM_NCHITTEST (screen pt)
   ↓ 转窗口局部 (x, y)                          ← 既有（Phase 12）
   ↓ ① resize 九宫格（最高优先——T2 原断言不变）  ← 既有
   ↓ ② y < caption（物理像素 = DipToPixels）     ← 既有
        ↓ m_host.IsClientInteractiveAt(x, y)   ← 新增契约调用（§2.2）
             ↓ Window::IsClientInteractiveAt   ← 新增实现（§2.4.2）
                 ↓ m_rootWidget->HitTest(x, y) ← 既有机制（逆序递归 / Visible && Enabled）
                     ├─ 命中 Widget* w → w->ConsumesMouseInput()   ← 新增能力（D9）
                     │     ├─ true  → true  → break → HTCLIENT（事件派发给控件）
                     │     └─ false → false → HTCAPTION（拖标题文字可移窗）
                     └─ 未命中 → false → HTCAPTION（空白区即拖拽区）
   ↓ ③ 超出 caption → break（既有出口）→ DefWindowProc → HTCLIENT
```

**三层判定顺序（锁死）**：`resize 区` > `caption 区内可交互控件` > `caption 内未命中 → HTCAPTION` > `超界 → HTCLIENT`（= 需求 §4.1 / 初设 §3.1）。

**前提（源码核实）**：`Widget::HitTest`（`src/Widget/Widget.cpp:105-145`）先过滤 `!IsVisible()` / `!IsEnabled()`（返回 `nullptr`）→ **逆序**遍历 children → 局部坐标换算后**递归** → 子命中即 `return target` → 全部落空才 `if (ContainsPoint) return this`。⇒ 按钮中心返回的是 **`CaptionButton`**（`ConsumesMouseInput()==true`），**不会**返回 `CaptionBar` 容器。
**附带推论（白得）**：禁用的按钮**天然落回 `HTCAPTION`**（`IsEnabled()` 过滤在 HitTest 内）→「按钮不可用 → 该区域变回拖拽区」。

### 3.2 坐标契约（`IsClientInteractiveAt` 入参）

`x, y` = **窗口局部坐标**（= `pt - rcWin.left/top`，见既有 `:230-232`）。
Borderless 下 `WM_NCCALCSIZE` 使**客户区 = 整窗** ⇒ 窗口局部坐标与客户区坐标一致，可直接喂 `RootWidget::HitTest`。
Normal 模式下该路径**永不执行**（`m_chromeMode != Borderless` 早退，现 `:217-221`）⇒ 坐标歧义不存在。

### 3.3 CaptionBar 实现（构造 / 布局 / 重排 / 二态）

**布局常量**（`CaptionBar.cpp` 匿名 namespace）：

```cpp
namespace{

	// 布局常量（逻辑像素 DIP——初设 §6 待定项 1 的定案：常量集中在实现内）
	constexpr int kButtonWidth   = 46;   ///< 按钮宽（Windows 11 caption 按钮量级；高 = 标题栏高）
	constexpr int kTitleLeftPad  = 12;   ///< 标题左边距
	constexpr int kTitleRightGap = 8;    ///< 标题右留白（标题与最小化按钮之间的空白 = 拖拽区）

}
```

**构造**：

```cpp
CaptionBar::CaptionBar(Window& window, const std::string& title)
	: m_window(window){

	// v0.1 默认视觉（D5：样式 API 留待二次用例；主题集成为本阶段非目标）——
	// 底色经**继承的** Panel::SetStyle 注入（Panel 能力直接消费，零新增 API）。
	SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(38, 40, 45, 255) });

	auto label = std::make_unique<Label>(title);
	// ⚠️ 标题前景色必须在此注入：DefaultTheme 的文本色是黑色（不可读于深色标题栏），
	//    而 O2 不提供标题样式 API ⇒ 不注入则消费者永远无法改变标题颜色（§4.4 局限记录）。
	label->SetStyle(TextStyleOverride{ .foreground = Color::FromRGBA8(235, 236, 240, 255) });
	m_title = label.get();

	auto minButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Minimize, [this]{ m_window.Minimize(); });
	m_minButton = minButton.get();

	auto maxButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Maximize, [this]{ ToggleMaximizeRestore(); });
	m_maxButton = maxButton.get();

	auto closeButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Close, [this]{ m_window.RequestClose(); });
	m_closeButton = closeButton.get();

	// 顺序 = 树内顺序（互不重叠 ⇒ 与 Z 序无关）；先标题后按钮
	AddChild(std::move(label));
	AddChild(std::move(minButton));
	AddChild(std::move(maxButton));
	AddChild(std::move(closeButton));

	RelayoutChildren();   // 初始几何（父布局 Arrange 会以新宽度再次 SetSize → 重排）
}
```

> 构造期 `SetStyle` → `Invalidate()` 是**既有安全形态**：`Panel::Panel()` 自身就走 `ApplyTheme → Invalidate`（`Panel.cpp:11`），`Invalidate` 已处理「未挂树 = 无 Window」的情形。

**重排**（`RelayoutChildren`）：

```cpp
void CaptionBar::RelayoutChildren(){

	const int w = GetWidth();
	const int h = GetHeight();

	// 三按钮靠右依次排布（左→右：min | max | close——系统惯例 close 在最右）
	const int closeX = w - kButtonWidth;
	const int maxX   = w - 2 * kButtonWidth;
	const int minX   = w - 3 * kButtonWidth;

	m_minButton->SetPosition(minX, 0);
	m_minButton->SetSize(kButtonWidth, h);

	m_maxButton->SetPosition(maxX, 0);
	m_maxButton->SetSize(kButtonWidth, h);

	m_closeButton->SetPosition(closeX, 0);
	m_closeButton->SetSize(kButtonWidth, h);

	// 标题占左侧剩余（宽度钳 0——窗口极窄时不出现负宽；越界文本由控件自身 PushClip 裁切，P2）
	const int titleW = (std::max)(0, minX - kTitleLeftPad - kTitleRightGap);
	m_title->SetPosition(kTitleLeftPad, 0);
	m_title->SetSize(titleW, h);

}
```

**SetSize override**：

```cpp
void CaptionBar::SetSize(int w, int h){

	Widget::SetSize(w, h);   // 基类几何（Panel 未 override SetSize——直连 Widget）

	RelayoutChildren();      // 复合控件同步子控件几何（CollapsiblePanel::SetSize 同款先例）

}
```

**OnPaint（二态刷新——零新接缝）**：

```cpp
void CaptionBar::OnPaint(PaintContext& ctx, int x, int y){

	// 二态刷新：绘制前把 max 按钮 glyph 设为当前窗口状态对应的形态。
	// 为什么无需订阅 WindowStateChangedEvent：最大化 / 还原**必产生 WM_SIZE** →
	// Window::OnResized → Invalidate → 重绘，故每次绘制时读「现在是什么」即可
	// （与 D6「状态是事实、由 Window 提供」一致；也避免引入新的订阅接缝）。
	m_maxButton->SetGlyph(
		m_window.GetWindowState() == WindowState::maximized
			? CaptionButton::Glyph::Restore
			: CaptionButton::Glyph::Maximize);

	Panel::OnPaint(ctx, x, y);   // 背景 / 边框（继承能力）

}
```

> `Widget::Paint` 的调用序是 `OnPaint(自身) → 子控件`（`Widget.cpp:243-249`）⇒ 本 override 在按钮绘制**之前**执行，glyph 已就位。`SetGlyph` 是纯赋值（不 `Invalidate`）⇒ 无重入 / 无死循环。

**其余文件内容**（构造 / `RelayoutChildren` / `SetSize` / `OnPaint` / 下述 §3.5 §3.6）：`GetTitle` / `SetTitle` 转发 `m_title`（`Label` 继承 `TextWidget::SetText/GetText`）。

### 3.4 CaptionButton 绘制与交互

**视觉常量**（`CaptionButton.cpp` 匿名 namespace）：

```cpp
namespace{

	// 视觉常量（v0.1 实现内常量——D5：样式 API 留待二次用例；主题集成为本阶段非目标）
	constexpr float kGlyphSize = 10.0f;   ///< glyph 图标框边长（10×10 逻辑像素）
	constexpr float kLineWidth = 1.0f;    ///< 线宽（DrawLine 默认）

	const Color kGlyph        = Color::FromRGBA8(220, 222, 226, 255);   ///< 常态笔色
	const Color kGlyphOnRed   = Color::FromRGBA8(255, 255, 255, 255);   ///< close 悬停时的笔色（红底白图）
	const Color kHoverBg      = Color::FromRGBA8(255, 255, 255, 26);    ///< 悬停底（半透明白 ≈10%）
	const Color kPressedBg    = Color::FromRGBA8(255, 255, 255, 45);    ///< 按下底（≈18%）
	const Color kCloseHover   = Color::FromRGBA8(196, 43, 28, 255);     ///< close 悬停底（系统惯例红）
	const Color kClosePressed = Color::FromRGBA8(160, 32, 20, 255);     ///< close 按下底

}
```

**glyph 坐标表**（`gx/gy` = 居中 10×10 图标框左上角；`cx/cy` = 按钮中心）：

```
	const int cx = x + GetWidth() / 2;
	const int cy = y + GetHeight() / 2;
	const float gx  = static_cast<float>(cx) - kGlyphSize * 0.5f;
	const float gy  = static_cast<float>(cy) - kGlyphSize * 0.5f;
	const float gx2 = gx + kGlyphSize;      // gx + 10
	const float gy2 = gy + kGlyphSize;      // gy + 10
```

| glyph | 线段 | 条数 | 说明 |
|---|---|---|---|
| `Minimize` | `(gx, cy) → (gx2, cy)` | **1** | 一条水平线（垂直居中） |
| `Maximize` | `(gx,gy)→(gx2,gy)` · `(gx,gy2)→(gx2,gy2)` · `(gx,gy)→(gx,gy2)` · `(gx2,gy)→(gx2,gy2)` | **4** | 10×10 描边方框（`PaintContext` 无描边矩形能力 ⇒ 四条边线；先例：CheckBox 用 DrawLine 画勾） |
| `Restore` | 后框：`(gx+3,gy)→(gx2,gy)` · `(gx2,gy)→(gx2,gy+7)`；前框：`(gx,gy+3)→(gx+7,gy+3)` · `(gx,gy+3)→(gx,gy2)` · `(gx,gy2)→(gx+7,gy2)` · `(gx+7,gy+3)→(gx+7,gy2)` | **6** | 两个错位方框（后框只画上边 + 右边） |
| `Close` | `(gx,gy)→(gx2,gy2)` · `(gx2,gy)→(gx,gy2)` | **2** | 两条对角 |

**OnPaint 结构**：

```cpp
void CaptionButton::OnPaint(PaintContext& ctx, int x, int y){

	const Rect self{ static_cast<float>(x), static_cast<float>(y),
	                 static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

	// ① 背景（仅悬停 / 按下——常态不画，露出标题栏底色）
	if (m_pressed){

		ctx.DrawRect(self, m_glyph == Glyph::Close ? kClosePressed : kPressedBg);

	}
	else if (m_hovered){

		ctx.DrawRect(self, m_glyph == Glyph::Close ? kCloseHover : kHoverBg);

	}

	// ② glyph（居中图标框——坐标见上表）
	const Color pen = (m_hovered && m_glyph == Glyph::Close) ? kGlyphOnRed : kGlyph;

	// ... 按 m_glyph 分派，见上表 ...

}
```

**交互（严格对齐 `Button` 的既有先例——`Button.cpp:101-135` / `:167-179`）**：

| 虚方法 | 行为 |
|---|---|
| `OnMouseButtonDown` | `m_pressed = true; Invalidate();`（不区分按键——与 `Button` 一致；隐式 Capture 保证 Up 必达） |
| `OnMouseButtonUp` | `m_pressed = false; Invalidate();` → **若鼠标仍在自身矩形内**（`GetAbsolutePosition()` + `GetWidth/GetHeight` 判定，同 `Button` 的 I6 修正）→ `RaiseClick()` |
| `OnMouseEnter` / `OnMouseLeave` | 置/清 `m_hovered` + `Invalidate()` |
| `RaiseClick` | 仅调 `m_onClick()`（无 `OnClick` 虚方法——内部类不对外承诺扩展点） |

> **按下的「拖出取消」语义**与 `Button` 一致：Down 后拖出再 Up 不触发命令。
> **不做**：键盘激活（按钮 `CanFocus()==false`）、双击语义、禁用态视觉（禁用时 `HitTest` 已过滤 ⇒ 该区域落回拖拽）。

### 3.5 close 路径（R4 / D8）

```
CaptionButton(Close) 点击（Up 且仍在按钮内）
   ↓ RaiseClick → m_onClick
   ↓ CaptionBar 构造时绑定：m_window.RequestClose()          ← §2.4 新增入口
   ↓ WindowCloseRequestedEvent event(this)          （同步）
   ↓ m_application.OnEvent(event)
   ↓ Application::OnWindowCloseRequested（**可被应用 override 拦截**）
   ↓ 基类默认实现：event.GetWindow()->Release() → DestroyWindow（既有路径，窗口销毁）
```

⇒ 自绘 X 与系统 X **同一语义通道**：ModelProbe 的后端清理（`DemoApplication::OnWindowCloseRequested` → `ShutdownBackend()`）对两者一视同仁。

**最大化·还原切换**：

```cpp
void CaptionBar::ToggleMaximizeRestore(){

	// 读当前事实决定动作（不缓存状态——D6/O4）
	if (m_window.GetWindowState() == WindowState::maximized){

		m_window.Restore();

	}
	else{

		m_window.Maximize();   // 非 maximized 一律调 Maximize（含 minimized——由平台/系统决定恢复行为，不加特判）

	}

}
```

### 3.6 状态查询链（R3）

**`Win32PlatformWindow.h`** —— public override 区末尾（`Restore()` 声明之后）新增一行：

```cpp
	WindowState GetWindowState() const noexcept override;   ///< Phase 13 R3：返回 WM_SIZE 缓存的状态
```

**`Win32PlatformWindow.cpp`** —— `Restore()` 实现（现 `:826-839`）之后：

```cpp
WindowState Win32PlatformWindow::GetWindowState() const noexcept{

	// 事实来源：WM_SIZE 时由 IsIconic / IsZoomed 判定并写入（Phase 12 既有去重锚）——
	// 本方法只是读取，不重新查询系统（避免与事件流的判定不一致）。
	return m_lastWindowState;

}
```

**完整链**：

```
CaptionBar::OnPaint  → Window::GetWindowState()  → m_platformWindow->GetWindowState()
                                                   → return m_lastWindowState;   （WM_SIZE 时更新）
测试替身 TestPlatformWindow::GetWindowState()      → return WindowState::restored;  （stub）
```

---

## 4. 契约

### 4.1 坐标契约
`IsClientInteractiveAt(x, y)` 入参 = **窗口局部坐标**；Borderless 下与客户区坐标一致（§3.2）。Normal 模式该路径不执行。

### 4.2 D7 职责二分（契约条文——不联动）

| 量 | 职责 |
|---|---|
| `captionHeight`（`Window::SetCaptionHeight`） | **系统标题栏行为区**——范围内未落在消费鼠标的控件上 → `HTCAPTION` |
| CaptionBar 高度（Widget 实体） | **可交互实体区**——命中消费鼠标的控件 → `HTCLIENT` |

框架**不**把 Bar 高度隐式写回命中区；建议使用方设同值（`CaptionBar::kDefaultHeight` 为建议常量，ModelProbe 取 `captionHeight` 变量）。

### 4.3 失败模式与安全

| 场景 | 行为 |
|---|---|
| `m_rootWidget == nullptr`（构造早期） | `IsClientInteractiveAt` 返回 `false`（早退）→ `HTCAPTION`，无 UB |
| 控件被禁用 / 隐藏 | `HitTest` 天然过滤（`Visible && Enabled`）⇒ 落回 `HTCAPTION`（拖拽仍可用——**良性**） |
| `m_platformWindow` 为空 | 不可能（构造体即建）；`GetWindowState` 无额外守卫 |
| CaptionBar 宽度 < 三按钮总宽（3×46 = 138） | 标题 / 按钮可能落到负或零几何；这些区域位于 CaptionBar 可视矩形**之外**，正常窗口布局下**不构成有效命中区**——命中与否由既有几何 + Paint/HitTest 共同决定，**非本合同保证**。本阶段**不引入窄窗专用布局策略** |
| 性能 | 委托**仅在 caption 条带内**（32 DIP 高）发生，非全窗口每点；`HitTest` 为纯树遍历（不派发事件、不绘制、不分配） |
| 重入 | `WM_NCHITTEST` 期间调用框架层 `HitTest`：同线程、无重入（`HitTest` 不触发事件/绘制/`SetSize`） |
| Debug 期高度一致性检查 | **不做**（O5——不联动、不检查；误配后果轻微） |

### 4.4 已知局限（记账，非缺陷）

| # | 局限 | 记录 |
|---|---|---|
| L1 | **自绘按钮拿不到 Win11 的 Snap Layouts**（悬停 max 键的贴靠面板）——自绘路线的固有代价 | 需求 §5 非目标已圈 |
| L2 | **CaptionBar 没有公开的标题样式入口**（`m_title` 为 private，消费者拿不到该 Label）：O2 不提供样式 API + D5 不做主题集成 ⇒ 标题前景色由构造注入常量（P3） | 二次用例出现时优先开放（`SetTitleStyle` 或 `CaptionBarStyle` 入 Theme） |
| L3 | 点击标题栏按钮（`CanFocus()==false`）会**清空焦点控件**（`Application::OnMouseButtonDown` 既有语义） | 可接受；拖拽路径不受影响（`HTCAPTION` 不产生客户区鼠标消息） |
| L5 | 标题栏底色 / glyph 色为 v0.1 实现内常量（不支持主题切换） | 同 L2 |
| L6 | **坐标系局限（实施期发现）**：`WM_NCHITTEST` 坐标为**物理像素**，而 widget 几何为**逻辑像素（DIP）**——`DipToPixels` 目前只作用于 `captionHeight` / `resizeInset`，故 **DPI ≠ 100% 时命中委托的入参会与 widget 几何错位**。根因是框架当前**无 DPI 缩放**（既有记账项，非本阶段引入） | 与「DPI 感知」技术债同源；做 DPI 缩放时一并闭合 ⇒ ★ **✅ 已闭合（2026-09-24）**：由 **Phase 20**（感知声明 + 平台边界 DIP 贯通；`WM_NCHITTEST` 现按 **Q5 定案 A** 把入参折成 DIP 再比对）+ **Phase 20.1**（渲染层折算）关闭，详见 Phase 20 详设 **§14.5** |

> **关于 `minimized` 态（评审 §三 追问——原 L4 已移除）**：`ToggleMaximizeRestore()` 只在当前状态为 `maximized` 时调 `Restore()`，否则**一律**调 `Maximize()`。⚠️ `minimized` **不是 CaptionBar 的可达交互路径**——窗口最小化后本控件随之不可见、不可点，故该分支只体现**内部状态转换函数的完备性**，不是 UI 语义；本阶段**不为 `minimized` 加特判**，具体恢复行为由平台 / 系统决定。

---

## 5. 测试规格

**文件**：`ECDI/src/Tests/CaptionBarTests.cpp`（新建）+ `RunAllTests.h` 增 `void RegisterCaptionBarTests();` + `RunAllTests.cpp` 增调用（`RegisterWindowChromeTests();` 之后）。

**文件级辅助**（本文件匿名 namespace——沿用 `WindowChromeTests.cpp` 的 `HitTestLocal` / `Handle()` 形态；测试辅助**按文件局部复制**是既有惯例）：

```cpp
    /// @brief 取底层 HWND（同 WindowChromeTests::TestWindow::Handle——三跳）
    HWND HandleOf(Window& w) {
        return static_cast<const Win32RenderContext&>(
            w.GetPlatformWindow().GetRenderContext()).GetHandle();
    }

    /// @brief 窗口局部坐标 → WM_NCHITTEST 查询（同步直达 WndProc——未显示窗口同样有效）
    LRESULT HitTestLocal(HWND hwnd, int x, int y) {
        RECT wr{}; GetWindowRect(hwnd, &wr);
        const POINT pt{ wr.left + x, wr.top + y };
        return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
    }

    /// @brief 造一个 Borderless 窗口 + 同高 CaptionBar 的场景（不 Show——T13-1 零闪窗）
    struct CaptionFixture {
        Window* window = nullptr;
        CaptionBar* bar = nullptr;
        explicit CaptionFixture(Application& app, int w = 800, int h = 600, int caption = 32) {
            window = &app.Create("ECDI_CaptionTest", w, h);
            window->SetChromeMode(ChromeMode::Borderless);
            window->SetCaptionHeight(caption);
            window->SetResizeInset(8);
            auto barPtr = std::make_unique<CaptionBar>(*window, "ECDI");
            bar = barPtr.get();
            bar->SetPosition(0, 0);
            bar->SetSize(w, caption);
            window->GetRootWidget().SetSize(w, h);
            window->GetRootWidget().AddChild(std::move(barPtr));
        }
        ~CaptionFixture() { window->Release(); }   // 只销毁 HWND（Application 回收对象）
    };
```

**坐标依据**（w = 800、caption = 32、`kButtonWidth = 46`、`kTitleLeftPad = 12`、`kTitleRightGap = 8`）：
`closeX = 754` · `maxX = 708` · `minX = 662` · 标题 Rect = `[12, 654) × [0, 32)` · 按钮纵向中心 `y = 16`。

### T13-1 命中委托八态（**D9 回归锚**——本阶段最重要的用例）

| # | 探针（窗口局部坐标） | 期望 | 语义 |
|---|---|---|---|
| 1 | `(300, 16)` 标题 Label 覆盖区 | `HTCAPTION` | **命中 Label 但不消费鼠标**（D9 反例防回退） |
| 2 | `(708 + 23, 16) = (731, 16)` max 按钮中心 | `HTCLIENT` | 按钮消费鼠标 |
| 3 | `(662 + 23, 16) = (685, 16)` min 按钮中心 | `HTCLIENT` | 同上 |
| 4 | `(754 + 23, 16) = (777, 16)` close 按钮中心 | `HTCLIENT` | 同上 |
| 5 | `(658, 16)` 标题右留白条带（`[654, 662)` 无子控件） | `HTCAPTION` | 空白 = 拖拽区 |
| 6 | `(2, 2)` 左上角 | `HTTOPLEFT` | **resize 优先于 caption**（T2 原断言不受影响） |
| 7 | `(300, 40)` caption 之外 | `HTCLIENT` | 超界 = 普通客户区（原语义不变） |
| 8 | 禁用 max 按钮后 `(731, 16)` | `HTCAPTION` | `HitTest` 过滤禁用控件 ⇒ 落回拖拽（§4.3 推论） |

> #7 的安全性已由 T2 既有断言背书（`TestNCHitTestNineGrid` 已断言 `(w/2, h/2) == HTCLIENT`）。
> **向后兼容论证**（T2 不回归的根因）：无 CaptionBar 时 `(w/2, 20)` 命中 `RootWidget`，而 `ConsumesMouseInput()` **默认 false** ⇒ 仍返回 `HTCAPTION`——正是默认值的意义所在。

### T13-2 按钮命令四路径（D8 语义）

前置：`app.Show(); PumpMessages(64);`（`Minimize/Maximize` 有 `@pre Show()` 契约）。

**事件注入方式（P5）**：直接构造事件经公开入口派发，走完整 HitTest → Dispatch → Capture 链：

```cpp
    void ClickAt(Application& app, Window& w, int x, int y) {
        MouseButtonDownEvent down(&w, x, y, MouseButton::Left);
        app.OnEvent(down);
        MouseButtonUpEvent up(&w, x, y, MouseButton::Left);
        app.OnEvent(up);
    }
```

| 路径 | 操作 | 断言 |
|---|---|---|
| min | `ClickAt(685, 16)` + `PumpMessages` | `TestApp::seen` 含 `WindowState::minimized` |
| max | `ClickAt(731, 16)` + pump | `seen` 含 `WindowState::maximized`；**再点一次** → `seen` 含 `restored`（二态切换） |
| close | `ClickAt(777, 16)` | `TestApp` 的 `closeRequestedCount == 1`；因 override **不调基类** ⇒ 窗口仍在（`IsWindow(hwnd) != FALSE`）——**证明可拦截（D8）** |

`TestApp` = `struct TestApp : Application { int closeRequestedCount = 0; std::vector<WindowState> seen; protected: void OnWindowCloseRequested(const WindowCloseRequestedEvent&) override { ++closeRequestedCount; } void OnWindowStateChanged(const WindowStateChangedEvent&) override { seen.push_back(e.GetState()); } };`

### T13-3 状态查询一致性（R3）

`Show + pump` 后：`w.GetWindowState() == WindowState::restored` → `w.Maximize()` + pump → `== maximized` 且与 `seen.back()` 一致 → `w.Restore()` + pump → `== restored`。

### T13-4 CaptionBar 组合与二态刷新（P4——**命令缓冲直接断言，零产品测试缝**）

**(a) CaptionButton 四态命令数**（内部头直接构造，无需窗口）：

```cpp
    RecordingBackend backend;         // 仅作 TextMeasurer 占位（构造需要）
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    CaptionButton btn(CaptionButton::Glyph::Maximize, {});
    btn.SetSize(46, 32);
    btn.Paint(ctx, 0, 0);
```

| glyph | `commands.size()` | 结构 |
|---|---|---|
| `Minimize` | **3** | PushClip → 1×DrawLine → PopClip |
| `Close` | **4** | PushClip → 2×DrawLine → PopClip |
| `Maximize` | **6** | PushClip → 4×DrawLine → PopClip |
| `Restore` | **8** | PushClip → 6×DrawLine → PopClip |

⇒ **四态靠命令数即完全可分辨**（另断言 4 条线段的端点构成 10×10 方框：`(gx,gy)/(gx2,gy2)` 等，`EXPECT_NEAR` eps 0.01）。

**(b) CaptionBar 组合**（`CaptionFixture`，不 Show）：

```cpp
    bar->SetTitle("Hello");
    EXPECT_TRUE(bar->GetTitle() == "Hello");

    RecordingBackend backend; CommandBuffer commands; PaintContext ctx(commands, backend);
    bar->Paint(ctx, 0, 0);

    // 结构断言（不绑定总条数——未来加绘制不误伤；ClipTests::TraceClip 同款过滤式遍历）
    // ① 存在 DrawTextCommand 且 text == "Hello"，其 pos.x ∈ [12, 654)
    // ② DrawLineCommand 计数 == 7（min 1 + max 4 + close 2——此时窗口 restored ⇒ max 画 Maximize）
```

> **断言分层原则（评审 §十一 定案）**：`CaptionButton`（封闭的内部小对象）→ 可**严格**断言 `commands.size()`（3/4/6/8）；`CaptionBar`（组合体）→ **禁止**绑定总条数，只断言语义要素（存在标题 `DrawText` + 7 条 `DrawLine` + 几何落区）。理由：组合体的命令流会随背景 / 悬停 / Label 内部实现变化而变化，绑定总数 = 把实现细节写进契约。

### T13-5 回归与手测

| 项 | 内容 |
|---|---|
| 自动化回归 | **T0–T7a 全量零回归**（重点 T2 / T3 / T5 / T6——全部依赖 NCHITTEST）；`EventTests` / `AnimationTests` / `ProgressBarTests` 因替身补虚而重新编译通过 |
| 手测 | ModelProbe `--borderless`：拖标题文字移窗 / 三按钮各按一次 / 按钮 hover 高亮 / close 走后端清理 → 日志可见 / 最大化后 max 图标变「还原」 |

---

## 6. 构建与工程注册

| 构建系统 | 动作 |
|---|---|
| **CMake** | **零维护**——`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 自动纳入 `ECDI/src/**/*.cpp`；Public Header 自包含测试自动覆盖 `CaptionBar.h`（重新 configure 即可） |
| **MSVC vcxproj** | 手动追加 4 条（`ECDI/ECDI.vcxproj`，追加在 Phase 12 末尾条目之后——本次 `:320` 之后）：`<ClCompile Include="src\Window\CaptionBar.cpp" />` · `<ClCompile Include="src\Window\CaptionButton.cpp" />` · `<ClInclude Include="include\ECDI\Window\CaptionBar.h" />` · `<ClInclude Include="src\Window\CaptionButton.h" />` |
| **vcxproj.filters** | 上述 4 条各配一个 `<ClCompile>` / `<ClInclude>` 过滤项（归 `src\Window` / `include\ECDI\Window` 既有节点） |

---

## 7. 消费者集成：`examples/ModelProbe/main.cpp`

**落点**：CLI 解析段之后、`root.SetLayout(...)`（现 `:127`）之后、`page` 创建（现 `:129`）**之前**插入（**必须在 page 之前**——P6）；现 `:139-145` 的 chrome 配置块保持原位不动。

```cpp
	// ★ Phase 13：自绘标题栏（仅 Borderless——实体区高度 = 行为区 captionHeight，D7 建议同值）。
	// ⚠️ 顺序约束：必须在 page 之前 AddChild —— RootWidget 的 VerticalLayout 按 children 顺序
	//    排布竖直次序（先前 = 上方）；bar 的 SetStretch 保持 0（主轴固定高度），page 保持 stretch=1。
	if (borderless) {

		auto bar = std::make_unique<ECDI::CaptionBar>(win, "ECDI 模型探测工具");

		bar->SetSize(680, captionHeight);   // 初始宽度（Arrange 会以真实宽度再次 SetSize）
		root.AddChild(std::move(bar));

		ECDI::Logger::Log(ECDI::LogLevel::Info, L"CaptionBar: attached（自绘标题栏）");

	}
```

**行为**：窗口 resize → `OnResized → root.Arrange()` → `VerticalLayout` 对 `stretch=0` 的 bar 执行 `SetSize(rootW, captionHeight)` → `CaptionBar::SetSize` → `RelayoutChildren` ⇒ 标题与三按钮自动跟随（**零额外接线**）。page 得到剩余高度。

> ⚠️ **手测关注点**：page 高度减少 `captionHeight`（默认 32）——若其内部为固定像素布局，底部可能被裁（→ §9 风险 R3；必要时把窗口高度 +32 或调整 page 内部布局）。本项列入 A3 手测。

> ✅ **已实施（2026-09-14，用户单独授权）**：`main.cpp` 已按本节插入（含 `#include "ECDI/Window/CaptionBar.h"`）。**实施期风格对齐**：用 `if (borderless){`（沿用该文件既有的紧贴大括号风格，非本节示意里的空行版）。
>
> ✅ **默认形态变更（2026-09-15，v1.5，用户单独授权）**：ModelProbe **默认改为自绘标题栏**——`bool borderless = false → true`，新增 `--native` **回退系统标题栏**（保住 §8 A4 的 Normal 零回归手测可复跑）；`--borderless [caption inset]` 保留，语义变为「显式重复确认默认态 + 调参」（幂等，仍可自定义标题栏高度与缩放热区；解析为「最后一个赢」）。**同批收掉两处「默认化后必踩」的重复**：窗口标题串原先在 `application.Create(...)`（决定任务栏 / Alt-Tab 显示）与本节 `CaptionBar(...)` 构造（决定自绘标题栏显示）**各自硬编码**——原先仅在 `--borderless` 时可能不一致，默认自绘后成为**每台机器的必经路径** ⇒ 提为文件级 `namespace { constexpr const char* kWindowTitle = "ECDI 模型探测工具"; }`；`captionHeight` 初值由 `32` 改为 `ECDI::CaptionBar::kDefaultHeight`（把 D7「行为区 / 实体区建议同值」从注释约定升为代码事实）。**本节规格代码块保持原样**——本次属实施后形态调整，非设计变更（`git diff --stat` = 20 insertions / 10 deletions，单文件 `examples/ModelProbe/main.cpp`）。⚠️ 页面高度影响见上文「手测关注点」——**A3 手测 ⑥ 已实测页面底部无裁切**。

---

## 8. 验收清单（A 项——由用户在 VS / CLion 执行）

| # | 项 | 判据 | 实测结果 |
|---|---|---|---|
| **A1** | MSVC Debug 构建 + `ecdi_tests` 全量 | 失败数 **0**；报告须写明**断言是否启用**（MSVC + CMake Debug 带 `_DEBUG`）。用例总数 = 183 + 新增（T13-1 八态按 1 用例计 ⇒ 预计 **+5**） | ✅ **188 passed / 0 failed** |
| **A2** | 四工具链构建（MSVC / ClangCL / Clang / MinGW） | 全部编译通过；**MinGW 欲验断言须加 `-DCMAKE_CXX_FLAGS=-D_DEBUG`**（skill 条 35——默认 MinGW Debug 下断言是死代码）。⚠️ **2026-09-17 更新**：`CMakeLists.txt` 已为非 MSVC 模拟工具链按 Debug 配置自动补 `_DEBUG`，**无需再加 flag**（Phase 14 详设 §8 A2） | ✅ **四构建全绿 188/188** |
| **A3** | 手测 `modelprobe.exe --borderless` | ① 拖标题文字能移窗；② 三按钮各生效；③ hover 高亮（close 变红）；④ close 触发后端清理日志；⑤ 最大化后 max 图标变「还原」；⑥ 页面底部无裁切异常 | ✅ **6 项通过**（含 ⑥ 底部无被裁 32px） |
| **A4** | 手测 `modelprobe.exe`（Normal） | 系统标题栏行为**完全不变**（无 CaptionBar、无命中变化）——零回归 | ✅ **通过**（含 ④ 关窗后 `probe.exe` 随之退出）。⚠️ **复跑命令已变（v1.5）**：2026-09-15 起 ModelProbe 默认自绘标题栏 ⇒ Normal 复跑请用 **`modelprobe.exe --native`** |
| **A5** | 断言启用核验 | ⚠️ **判据按工具链二分**（v1.3 修正——原判据「`grep -c _DEBUG`」对 MSVC / ClangCL **会误判**）：**MSVC 系查 `-MDd` / `/MDd`**（debug CRT 隐含定义 `_DEBUG`，ninja 文本中仅 1 处命中）；**GNU 系查 `-D_DEBUG`** | ✅ **通过（2026-09-14）**：`debug-visual-studio` = `-MDd` ✅ · `debug-clangcl` = `-MDd` ✅ · `debug-clang` = `-D_DEBUG` ✅ · **`debug-mingw` = 0 命中 ⇒ 断言是死代码**（欲验须加 `-DCMAKE_CXX_FLAGS=-D_DEBUG`，skill 条 35）。**2026-09-17 更新**：`CMakeLists.txt` 已按 Debug 配置为非 MSVC 模拟工具链自动补 `_DEBUG`，MinGW 复测 **7/7** ✅（Phase 14 详设 §8 A2） |
| **A6** | 静态自查（AI 侧） | 4 处述语替身全部补齐（`FakeHost` + 2×`TestPlatformWindow`）；`grep -rn "IsClientInteractiveAt"` → **override 实现 = 2（1 生产 + 1 替身）**：`Window` / `EventTests::FakeHost`；`grep -rn "GetWindowState"` → **override 实现 = 3（1 生产 + 2 替身）**：`Win32PlatformWindow` / `AnimationTests::TestPlatformWindow` / `ProgressBarTests::TestPlatformWindow`（**勿读成「3 个生产实现」**） | ✅ 通过 |

**A 项执行摘要（2026-09-14 收口）**：

1. **A1/A2 阻塞项已排除——`AntiAliasing.GDIRadiusZeroBitwise` flaky 根因定性**：四构建执行中该用例**间歇性失败**（每次读数不同，`GetPixel` 返回 `CLR_INVALID`）。经 **5 条假设逐个否证**（任务栏遮挡 / 窗口位置 / AA 渲染差异 / 工具链差异 / 「中心窗口非自身即遮挡」——后者被 `SunAwtFrame` 覆盖下仍全绿的实测反例推翻），第 6 条命中：**Windows Window Ghosting**。机制 = 测试窗口全程不泵消息（读 4 万像素/帧 ≈ 1.5 s）**超过 ~5 s 系统阈值** ⇒ DWM 用类名 `Ghost` 的替身窗口替换原窗口 ⇒ 其 DC 可见区为 `NULLREGION`（`clipType=1`）⇒ `GetPixel` 恒 `CLR_INVALID`。**修法**：`src/Tests/test_main.cpp` 启动即调 **`DisableProcessWindowsGhosting()`**（`main()` 首行，附定性依据注释）。修复后 `clipType` 1→2、`clip=(0,0,200,200)`、AA-off 复查 `INVALID=0`、**四构建 188/188 全绿**。
2. **诊断脚手架收口**：早期为避免 flaky 引入的「读帧重试」已移除（实测其把阻塞推过 5 s 阈值，**偶发变必然**——反而是放大器）；诊断输出统一收进**仅失败时调用**的 `DumpWindowState` / `DumpFrameMismatch`（正常路径零输出，`AntiAliasingTests.cpp` 709 → 655 行，用例体 190 → 32 行）。
3. **`test_main.cpp` 变更纪律**：**12 insertions / 0 deletions**——仅追加 `DisableProcessWindowsGhosting()` 与既有 `SetConsoleOutputCP(CP_UTF8)`（后者解决中文日志乱码：UTF-8 写 + CP936 读）。

---

## 9. 风险与回滚

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| **R1** | **NCHITTEST 新增分支**——T2/T3/T5/T6 全依赖该消息 | ★★★ | 只加**前置分支**、命中复用同一 `break` 出口；新增 T13-1 八态（含「Label 命中仍 HTCAPTION」「resize 优先」「超界 HTCLIENT」）+ 全量回归 |
| **R2** | `ConsumesMouseInput` 默认值若写成 `true` | ★★★ | **默认必须是 `false`**——这是 T2 `(w/2,20)==HTCAPTION` 不回归的**唯一**原因（§5 T13-1 注）；T13-1 #1 是该默认值的回归锚 |
| **R3** | ModelProbe 页面高度 -32px 导致底部裁切 | ★★ → ✅ **已缓解** | A3 手测（**⑥ 实测通过：页面底部无被裁 32px**）；必要时窗口高度 +`captionHeight`。⚠️ v1.5 默认自绘后本风险从「仅 `--borderless` 时」变为**默认路径**——A3 ⑥ 的结论直接适用 |
| **R4** | 委托引入每点递归成本 | ★ | 仅限 caption 条带（32 DIP 高）；`HitTest` 为纯遍历（无分配 / 无事件 / 无绘制） |
| **R5** | 替换 `PlatformWindow` / `PlatformWindowHost` 纯虚遗漏替身 ⇒ 编译失败 | ★ | 已全库 grep 预检（§2.2 / §2.3）——**3 处替身**列入改动清单 |
| **R6** | 标题色在黑字默认下不可读 | ★★ | 构造注入浅色（P3）；局限 L2 已记账 |

**回滚**：改动集中在 2 新建 + 8 修改文件；最小回滚 = 删除 §3.1 的 `if (m_host.IsClientInteractiveAt(x, y)) { break; }` 前置分支（即恢复 Phase 12 行为），CaptionBar 变为「单纯显示 + 按钮不可点」——无残留副作用。

---

## 11. 评审响应（v1.0 → v1.1）

外部评审结论：**架构通过——未发现需要推翻 Phase 12/13 初设路线的问题，可进入实现**；并确认本阶段此后「不再重新设计，只按详设施工」。

**评审确认、本文不改（锁定设计）**：`ConsumesMouseInput()` 默认 `false` 的能力接口形态（D9 核心）· **P1** `SetSize` 内重排的复合控件模式 · `RequestClose()` 语义统一（系统 X / 自绘 X 同经 `WindowCloseRequestedEvent`）· `GetWindowState()` 挂在 `PlatformWindow`（与事件同事实源）· T13-1 八态（D9 回归锚）· T13-4 零测试缝的命令断言 · 工程注册（CMake glob 零维护 / VS 手动注册）· `HitTest` 为「最终命中者」的既有前提（**实现时不得顺手修改 `HitTest`**）· 按钮不区分鼠标键（YAGNI，与 `Button` 一致）。

**7 条处置**：

| # | 评审提出 | 判定 | 处置 / 依据 |
|---|---|---|---|
| **P0-1** | `TestPlatformWindow` 述语替身写成 `void GetWindowState()`，是编译错误 | **✅ 采纳（一处，非两处）** | 复现确认 `void` 函数带 `return <值>` ⇒ 确凿编译错误。**修正 §2.3**（`void` → `WindowState`）。⚠️ 评审称「两处」——**实际文档仅一处**（§3.6 链条文字无签名，非第二处） |
| **P0-2** | 原 L4 把「minimized 后点击 max」描述成正常 UI 路径，易误导 | **✅ 采纳** | **删除原 L4 行**，改在局限表后新增「关于 `minimized` 态」说明段：该状态下 CaptionBar 已不可见 / 不可点 ⇒ **不是可达交互路径**，只体现内部转换函数的完备性，**不加特判**；§3.5 inline 注释同步收紧 |
| **P1-3** | §4.3「负坐标无害」表述过于绝对 | **✅ 采纳** | 改写为几何行为描述：区域落于可视矩形**之外**，正常布局下**不构成有效命中区**，并声明「非本合同保证」+ 不引入窄窗专用布局策略。符合「不把当前实现表现写成框架契约」纪律 |
| **P1-4** | 建议删除「无实际用途」的 `m_closeButton` | **❌ 否决（评审事实有误）** | 复现确认：`m_closeButton` 在 `RelayoutChildren()` 中被使用（`SetPosition(closeX, 0)` / `SetSize(kButtonWidth, h)`）——**删掉则关闭按钮无法定位**。已权衡的替代（具名数组 / `GetChildAt(3)` 索引定位）以可读性换「少存一个指针」，不符合最小改动面，不采纳 |
| **P1-5** | A6「实现者 = 3」易误读为「3 个生产实现」 | **✅ 采纳** | A6 改为显式枚举：`IsClientInteractiveAt` **override = 2（1 生产 + 1 替身）**（`Window` / `FakeHost`）；`GetWindowState` **override = 3（1 生产 + 2 替身）**（`Win32PlatformWindow` / 两个 `TestPlatformWindow`），并加「勿读成 3 个生产实现」 |
| **P1-2** | L2「标题颜色不可由消费者修改」不够精确（真因是无公开入口） | **✅ 采纳** | 改为「**CaptionBar 没有公开的标题样式入口**（`m_title` 为 private）」，并保留二次用例开放方向 |
| **§六 / §十一** | 建议明确 `SetSize` 可重复调用；T13-4 断言分层 | **✅ 采纳** | ① §2.5 `SetSize` 补「**可重复调用契约**」（只按当前 w/h 计算、不读上次结果、无累积偏移）；② §5 补「**断言分层原则**」（内部小对象可严格断总数 / 组合体只断语义要素） |

## 12. 修订记录

- **v1.5（2026-09-15）ModelProbe 默认形态翻转（实施后调整，非设计变更）**：
  - **变更**：`examples/ModelProbe/main.cpp` 默认改为**自绘标题栏**（`bool borderless = false` → `true`），新增 **`--native` 回退系统标题栏**；`--borderless [caption inset]` 保留为显式重复确认 + 调参入口（解析语义 = 「最后一个赢」）。**单文件 20 insertions / 10 deletions**。
  - **收掉的重复（默认化让暴露面从「偶尔」变「每台机器」）**：窗口标题串原先在 `application.Create(...)`（任务栏 / Alt-Tab 显示）与 `CaptionBar` 构造（自绘标题栏显示）**两处硬编码**——原先仅在 `--borderless` 时可能不一致，默认自绘后成为必经路径 ⇒ 提为文件级 `namespace { constexpr const char* kWindowTitle }`。另：`captionHeight` 初值 `32` → `ECDI::CaptionBar::kDefaultHeight`（D7「行为区 / 实体区建议同值」由注释约定升为代码事实）。
  - **A4 复跑命令已变**（§8 A4 行同步注明）：Normal 复跑由 `modelprobe.exe` 改为 **`modelprobe.exe --native`**。**A3 的 `--borderless` 手测 6 项属历史记录（记当时如何测、结果如何），不改写**——仅操作指引随形态变更更新。
  - **动机**：Phase 13 的客户价值是「Borderless 窗口有看得见摸得着的标题栏」，而 ModelProbe 是首个（当前唯一）消费者；默认态停在 Normal 会使其长期停留在「需显式开关才能见到」的状态。**回退开关必须保留**的硬性理由：否则 A4「Normal 零回归」验收项将**永久失去复跑能力**。
  - **未验证**：编译与手测待用户执行；AI 侧仅静态自查（BOM `efbbbf` / `git diff` 逐行核对 / `kWindowTitle` 计数 3（1 声明 + 2 使用）· 中文字面量仅剩常量定义 1 处 / 大括号 22-22 配平 / 无 `bool borderless = false` 残留）。
- **v1.4（2026-09-14 ~ 09-15）A 项收口 + flaky 根因修复**：
  - **A1–A6 全部通过**（§8 表新增「实测结果」列）：**四构建 188 passed / 0 failed**；ModelProbe `--borderless` 手测 6 项 + Normal 零回归手测通过。
  - **`AntiAliasing.GDIRadiusZeroBitwise` flaky 修复（判定性）**：根因 = **Windows Window Ghosting**（线程 >5 s 不取消息 ⇒ DWM 以 `Ghost` 替身窗口替换 ⇒ 原窗口 DC 可见区 `NULLREGION` ⇒ `GetPixel` 恒 `CLR_INVALID`）。**修法** = `test_main.cpp` 调 `DisableProcessWindowsGhosting()`（12 insertions / 0 deletions）。已否证：任务栏遮挡 / 窗口位置 / AA 渲染差异 / 工具链差异（详见 §8 A 项执行摘要）。
  - **诊断脚手架收口**：移除「读帧重试」（经实测为阻塞放大器）；`DumpWindowState` / `DumpFrameMismatch` 改为**仅失败时调用**（正常路径零输出）。`AntiAliasingTests.cpp` 709 → 655 行。
  - **`CreateAAWindow` 位置** 定为屏幕右上角（`screenW-210, kTopMargin`）——与上述收口一并保留。
- v1.3（2026-09-14）**A5 判据修正 + 消费者集成落地**：
  - **A5 修正（判定性）**：原判据「`grep -c _DEBUG <build-dir>/build.ninja`」**对 MSVC / ClangCL 会给出误导结论**——二者由 CMake 的 debug CRT 选项 **`-MDd`** 隐含定义 `_DEBUG`，ninja 文本里仅 **1 处**命中（`clang` 用 GNU 前端才是显式 `-D_DEBUG` ×157）。**新判据按工具链二分**：MSVC 系查 `-MDd`，GNU 系查 `-D_DEBUG`。**四构建实测**：`visual-studio` / `clangcl` = `-MDd` ✅、`clang` = `-D_DEBUG` ✅、**`mingw` = 0 命中 ⇒ 断言是死代码**（与 skill 条 35 一致）⇒ **A1 的 MSVC 全量是「带断言」的结论**，MinGW 那次不带。
  - **消费者集成落地**：§7 的 `examples/ModelProbe/main.cpp` 代码已插入（用户单独授权）——插入点 = `root.SetLayout(...)` 之后、`page` 创建之前（**必须在 page 之前**，P6）；含 `#include "ECDI/Window/CaptionBar.h"`。**实施期风格对齐**：采用该文件既有的 `if (borderless){` 紧贴风格。
  - **实施前核实（两处，均为好消息）**：① `examples/ModelProbe/CMakeLists.txt` 的 MSVC 分支**已有 `/utf-8`** ⇒ §7 的中文标题字面量正确编码为 UTF-8，符合 `CaptionBar(std::string)` 的 UTF-8 契约（否则会变 GBK 乱码）；② `--borderless` CLI 开关已存在（Phase 12 接入，含可选 `captionHeight` / `resizeInset`），无需额外接线。
  - **A3/A4 解锁**：集成后 `modelprobe.exe --borderless` 才真正有 CaptionBar 可测（此前 `CaptionBar` 在 `examples/` 下零引用）。
- v1.2（2026-09-14）**实现落地状态同步（补记）**：原头部记「✅ v1.1 外部评审通过（可进入实现）」。**全部按 v1.1 规格施工**（用户授权原话「我确认用这个方案改」），实施期 6 项偏差/发现记录如下：
  - **D1（偏差）**：vcxproj 实际 **+3 ClCompile**（`CaptionBar.cpp` / `CaptionButton.cpp` / **`CaptionBarTests.cpp`**）+ 2 ClInclude；详设 §6 只写了 +2 ClCompile + 2 ClInclude——**原因：vcxproj 也编译框架测试源码**，新增测试文件必须登记，否则 `RegisterCaptionBarTests()` 链接失败。
  - **D2（澄清）**：`CaptionBarTests.cpp` **无需 filters 条目**——既有 19 个测试源在 `.filters` 中均无条目（既有实情，非遗漏）。
  - **D3（计数）**：测试用例 **183 → 188**、测试文件 17 → 18（实测 `GetTestRegistry().Add(` 求和 = 188，与预期完全一致）。
  - **D4（补 include）**：`AnimationTests.cpp` / `ProgressBarTests.cpp` 各补 `#include "ECDI/Window/WindowState.h"`（显式依赖；虽可经 `PlatformWindow.h` 传递获得，但显式声明更稳——与 `WindowChromeTests.cpp` 同款写法）。
  - **D5（探针调整）**：T13-1 探针 ⑦ 由详设的 `(300, 40)` 改为 `(300, kWinH/2)`。**原因**：`y = 40` 与 caption 的物理像素边界（`DipToPixels(32)`）耦合，DPI > 125% 时会落回 caption 分支使断言失效；取客户区中部与 Phase 12 T2 的 `(w/2, h/2)` 同款，DPI 稳健。
  - **D6（L6 新局限）**：实施期发现 **`WM_NCHITTEST` 物理像素 vs widget 逻辑像素**的坐标系错位——`IsClientInteractiveAt(x, y)` 拿到的 `(x, y)` 是物理像素（与 `caption`/`inset` 同源），而 `HitTest` 用的是 widget 的 DIP 几何；两者仅在 **100% DPI** 下重合。**根因是框架当前无 DPI 缩放**（既有记账项），故**本阶段不引入换算**，列为局限 **L6**，与「DPI 感知」技术债一并闭合。★ **✅ 已闭合（2026-09-24）**：由 **Phase 20**（`WM_NCHITTEST` 按 Q5 定案 A 折成 DIP 再比对）+ **Phase 20.1**（渲染层折算）关闭；详见 Phase 20 详设 **§14.5**。
  - **验证状态**：AI 侧完成**静态自查**（A6：实现者枚举 / BOM / 见上）；**A1–A5 待用户执行**（MSVC+`_DEBUG` 全量测试、四工具链、ModelProbe 手测、Normal 模式零回归、`grep -c _DEBUG` 断言启用核验）。
- v1.1（2026-09-14）**外部评审「通过——可进入实现」+ 2 条 P0 / 5 条 P1 已逐条处置**（新增 §11 评审响应）：
  - **P0-1（必须修）**：`TestPlatformWindow` 替身返回类型 `void` → **`WindowState`**（§2.3）——原规格的确凿编译错误；评审称「两处」，实际仅一处。
  - **P0-2（必须修）**：原 **L4 移除**——`minimized` 态不是 CaptionBar 的可达交互路径（窗口最小化 ⇒ 本控件不可见不可点），改述为实现语义说明（§4.4 表后新增说明段 + §3.5 注释同步）。
  - **P1-3**：§4.3 负坐标行改写为几何行为描述 + 明确「非本合同保证」。
  - **P1-5**：A6 实现者枚举化（2 = 1 生产 + 1 替身 / 3 = 1 生产 + 2 替身）。
  - **P1-2**：L2 改为「无公开标题样式入口（`m_title` private）」。
  - **P1-4 否决**：`m_closeButton` 被 `RelayoutChildren()` 使用（`SetPosition` / `SetSize`），删除将导致关闭按钮无法定位；替代方案（数组 / 索引）以可读性换指针数量，不采纳。
  - **补充**：§2.5 `SetSize`「可重复调用契约」；§5「断言分层原则」（内部小对象严格断总数 / 组合体只断语义要素）。
  - **评审锁定项（实现期不得擅改）**：`ConsumesMouseInput()` 默认 `false` · `HitTest` 的「最终命中者」语义 · `RequestClose()` 单一路径 · `GetWindowState()` 事实源在平台层。
- v1.0（2026-09-14）**详细设计初稿**：
  - **逐文件规格**（§2）：2 新建（`CaptionBar.h` 85→86 / 内部 `CaptionButton.h`+.cpp）+ 8 修改（含 **3 处测试替身同步**——`FakeHost` / 2×`TestPlatformWindow`，全库 grep 预检所得）+ 工程注册（CMake glob 零维护 / vcxproj 4 条）。
  - **初设 → 详设 6 处精化**（§1.2）：**P1** CaptionBar override `SetSize` 内重排（依据 `VerticalLayout.cpp` 对 `stretch=0` 子控件调 `SetSize` + `CollapsiblePanel::SetSize` 先例 ⇒ 窗口 resize 零接线）；**P2** 标题越界天然被 `Widget::Paint` 自身 PushClip 裁切（初设 §6 待定项 2 定案）；**P3** 标题前景色必须由构造注入（`DefaultTheme` 文本为黑 + O2 无样式 API ⇒ 否则消费者永远改不了）；**P4** T13-4 判据 = 命令缓冲直接断言（`CheckBoxTests` 先例）零产品测试缝；**P5** 命令路径测试走合成 Event + `Application::OnEvent`（public 入口，免 Show / 屏幕坐标）；**P6** ModelProbe 必须把 bar 加在 page 之前（VerticalLayout 顺序 = 竖直次序）。
  - **关键实现规格**（§3）：NCHITTEST **最小 diff**（改动前后逐字对照 + 固定 `break → DefWindowProc → HTCLIENT` 结构）；CaptionBar 布局常量（`kButtonWidth 46` / 左距 12 / 右留白 8）与 `RelayoutChildren` 全文；CaptionButton **glyph 坐标表**（min 1 / max 4 / restore 6 / close 2 条线）；close 路径与 `ToggleMaximizeRestore`。
  - **测试规格**（§5）：T13-1 **八态**（含禁用按钮落回拖拽；坐标全部按常量算出）；T13-2 四命令路径（含 **close 可拦截**断言）；T13-3 查询一致性；T13-4 命令数四态（3/4/6/8）+ 组合结构断言（**不绑定总条数**）。
  - **契约与局限**（§4）：失败模式表 + **5 条已知局限**（Snap Layouts 缺失 / 标题色不可改 / 点按钮清焦点 / minimized→max / 视觉常量）。
  - **验收与回滚**（§8 / §9）：A1–A6（含 `_DEBUG` 启用核验）+ R1–R6 风险 + 最小回滚路径。
  - **未做**：未改动任何代码；未提交 git。
