# Phase 19 · 鼠标事件维度补全（Mouse event dimensions）—— 详细设计

> 状态：**v1.1**（2026-09-23）——✅ **评审通过**（**无阻塞项**）· **可进入实现**——严格按 **§7 的 ①→⑥** 执行，**不扩大范围**（见 §1.5）
> 输入：需求 **v1.1**（✅ 评审通过）· 初步设计 **v1.1**（✅ 评审通过，**无阻塞项**）
> 本稿任务：把初设落成**行级实现规格**，并解决初设 **§9 的四件事**（① 构造接口语义 ② 5 个翻译点逐点 diff ③ `FakeHost` 扩展 ④ P1/P2 实测记录）
> 定性：**契约错误修复**——"平台给了、被中途丢掉"

---

## 1. 设计输入与基线

### 1.1 已冻结（需求 + 初设，本稿不再讨论）

| 项 | 结论 |
|---|---|
| **D2** | 私有位掩码 + `IsButtonDown(MouseButton)` 谓词；**不新增命名类型**（`MouseButtonMask` 暂不给）；**不得**把 `MouseButton` 改成位标志枚举 |
| **D3 / Q6** | 修饰键源 = **`wParam`**（事件时刻权威 + 可无头断言）；**Alt 不置位** = 当前 Win32 映射的边界（**非**模型缺失） |
| **D5** | 只给 `HasModifier(KeyModifier)`；**不给** `GetModifiers()`、**不给** `IsShiftDown`/`IsCtrlDown`/`IsAltDown`（成套或不给） |
| **Q3** | 零破坏传参 = **带默认值的尾形参**（沿用 `isDoubleClick` 先例） |
| **Q4** | 测试走 **`FakeHost` + `Handle()` 真翻译路径**；手工构造**不得**充当 D-1 的验收证据 |
| **D4 / N1–N6** | **不简化** `ScrollBar::m_dragging` / `TextBox::m_mouseDown`；不做控件级拖拽 / OLE 拖出 / 全局热键 / hover / Alt |
| **D6** | #36 横向滚轮**不并入**（本项不为其预留任何形参） |

### 1.2 详设新增基线（B13–B18，2026-09-23 实测）

| # | 事实 | 证据 |
|---|---|---|
| **B13** ★★ | **既有测试已经按「LOWORD = 状态 / HIWORD = 键」的约定写**：`WM_LBUTTONDOWN` 的 wParam 传的是 **`MK_LBUTTON`**；X 键用例用 **`MAKEWPARAM(MK_XBUTTON1, XBUTTON1)`** | `EventTests.cpp:148` · `:159-161` |
| **B14** ★★ | `FakeHost::OnEvent` 的 switch **只有** KeyDown / CharInput / **MouseButtonDown** / **MouseMove** / WindowCloseRequested / WindowResized ⇒ **没有 `MouseButtonUp` 与 `MouseWheel` 分支**；且 `ReceivedEvent` **没有 `delta` 字段** | `EventTests.cpp:57-97` · `:32-42` |
| **B15** ★ | **本项不需要登记两处**：`RegisterEventTests()` 是**既有**注册函数，`RunAllTests.cpp:13` 已调用它 ⇒ 只需在该函数内**追加 5 条 `Add()`**；`RunAllTests.h` / `RunAllTests.cpp` **零改动**（**初设 §9-③ 的措辞有误**，见 §1.3） | `EventTests.cpp:337-344` · `RunAllTests.cpp:13` |
| **B16** ★★ | **位序与 `MK_*` 的数值 5 个里 3 个不同**——`Left` 0x01=0x01 ✓ · `Right` 0x02=0x02 ✓ · **`Middle` 0x04 vs `MK_MBUTTON` 0x10 ✗** · **`X1` 0x08 vs `MK_XBUTTON1` 0x20 ✗** · **`X2` 0x10 vs `MK_XBUTTON2` 0x40 ✗** | B12 的常数值 vs `MouseButton` 枚举序 |
| **B17** | `MK_*` 各位**互不重叠**（0x0001/0x0002/0x0004/0x0008/0x0010/0x0020/0x0040）⇒ 按键位与修饰键位**天然不串**（这是断言可取的前提） | B12 |
| **B18** | 既有翻译器 5 个调用点的**精确形态**（逐个见 §2.2）：`:97` 移动 · `:114` 按下组 · `:130` 双击（**多带 `true`**）· `:149` 抬起组 · `:173` 滚轮（**坐标为屏幕转客户区**） | `WindowMessageHandler.cpp` 鼠标段 |

### 1.3 ★ 对初设的三处修正（实现前必须接受）

| # | 初设原文 | 实测 | 修正 |
|---|---|---|---|
| **△C1** | §7「`ReceivedEvent` **+2 字段**」 | **B14** | **需 +3 字段**——除 `pressedButtons` / `modifiers` 外还需 **`int delta`**：T19-8 要断言 `GetDelta()==120`，而 delta 走的正是 `HIWORD`（与新增的 `LOWORD` 路径**并存**），不断言它就少了一层"两条路径互不干扰"的证据 |
| **△C2** | §7「`FakeHost` 的 `MouseMove` / `MouseButtonDown` 分支**各补 2 行**」 | **B14** | **除补两处，还需新增两个 `case`**：`MouseButtonUp`（T19-9）与 `MouseWheel`（T19-8）——否则这两类事件的字段**根本没被拷贝**，用例无法断言 |
| **△C3** | §9-③「用例登记（`RunAllTests.h` / `RunAllTests.cpp` **两处**）」 | **B15** | **不涉及那两个文件**——本项**不新建测试文件**，只在既有 `RegisterEventTests()` 内追加 `Add()`。⇒ 影响面比初设估计**更小** |

★ 三处均为**收窄方向的修正**（初设**低估**了测试装置改动、**高估**了登记改动），**不触及任何已冻结决策**（D2/D3/D5/D6 与契约 C1–C6 全部不变）。

### 1.4 初设 §9 四件事 → 本稿答案索引

| 初设 §9 | 事项 | 本稿答案 |
|---|---|---|
| **①** | `unsigned int pressedButtons` 的**构造接口语义** | **§2.1**（逐类给出 `@note` 正文）+ **§3**（位序与映射的精确形态） |
| **②** | **5 个翻译点的精确修改** | **§2.2**（两个 helper 全文 + 5 处逐点 diff） |
| **③** | **`FakeHost` 测试装置的扩展** | **§5.1**（`ReceivedEvent` +3 字段 · `OnEvent` +2 case + 2 处补行 · 完整片段） |
| **④** | **P1 / P2 的实测记录** | **§6**（两种方式 + 记录位置 + **一条从根上回避的写法建议**） |

---

### 1.5 ★ 第二轮评审（详设）的处置

外部评审（2026-09-23，针对本稿 v1.0）**结论：通过，无阻塞项**。评审确认本稿已达 implementation specification 级别（6 头 / 2 helper / 5 翻译点 / `FakeHost` 片段 / 9 场景输入期望 / P1-P2 方法 / 实现顺序与检查点齐全），并逐项认可 §2.1 掩码通道语义 · §3 的位序量化表 · **§5.2 用例口径 240 → 236** · Alt 的两层区分 · P1/P2 的"只写规则不写未验证行为" · **§7 步① 把"零破坏"拆成独立可验证检查点**。

| # | 评审提出 | 处置 |
|---|---|---|
| **1** | ★ 指出 **`HasModifier(KeyModifier::None)` 恒返回 `true`**（`x & 0 == 0`），本稿未讨论该边界；**建议"注意即可、不改"** | ✅ **已实测确认**（见下"实测"），**采纳"不改"**，并**三处记录**：§2.1 头注释加 `@note` · **§2.3 盯防清单第 7 条**（防"顺手改"）· 审计 **§7 A-10**（含重启条件） |
| **2** | 实现时严格按 §7 ①→⑥，**不扩大范围**；特别点名 `ScrollBar::m_dragging` / `TextBox::m_mouseDown` / Alt / 横向滚轮 **都不要顺手改** | ✅ **本稿重申**（§2.3 第 5、6 条 + §7），与 **D4 / N5 / N6 / D6** 一致 |

**★ 实测（不凭推理）**：用 g++ 逐字复刻 `KeyEvent::HasModifier` 的表达式（`KeyEvent.h:23-27`）跑数值实例——

```
  m=None(空)    Has(None)=1  Has(Shift)=0  Has(Ctrl)=0  Has(Alt)=0
  m=Shift|Ctrl  Has(None)=1  Has(Shift)=1  Has(Ctrl)=1  Has(Alt)=0
  m=Alt         Has(None)=1  Has(Shift)=0  Has(Ctrl)=0  Has(Alt)=1
```

⇒ **"全含"语义**（`(m & q) == q`）对 `q = None` **空洞成立**，与 `m` 无关。这不是 bug，是**数学上一致、直觉上意外**的边界。

**辅助取证**：① 全库**无测试**覆盖 `HasModifier(None)`；② 全库**无消费者**传 `None`（只有 `IsShiftDown` / `IsCtrlDown` / `IsAltDown` 三个内部短路传具体修饰键）⇒ **该分支当前不可达** ⇒ 不修也不构成现实风险。

---

## 2. ★ 逐文件行级改动（△1–△12）

### 2.1 六份公共头（△1–△10）

**公共头数 92 → 92**（只改既有头，**无新头**）。`MouseEvent.h` 需**新增两个 include**：`MouseButton.h`、`KeyBoard/KeyModifier.h`。

#### △1–△2 · `MouseEvent.h`（基类——本项核心）

```cpp
#include "ECDI/EventSystem/Input/InputEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"            // ★ 新增（IsButtonDown 形参）
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"         // ★ 新增（HasModifier 形参）

class MouseEvent : public InputEvent {

protected:

	/// @param window 事件来源窗口
	/// @param mouseX 鼠标 X 坐标（客户区坐标）
	/// @param mouseY 鼠标 Y 坐标（客户区坐标）
	/// @param pressedButtons 此刻按下的鼠标键位掩码（**位序 = MouseButton 枚举序**；0 = 无键按下）
	/// @note ★ `pressedButtons` 是**低层构造通道参数**，**不是**推荐消费者使用的语义接口——
	///       消费侧请用 `IsButtonDown()`。掩码只为两件事存在：① 平台翻译器（WindowMessageHandler）填入；
	///       ② 既有测试构造点保持兼容（默认值 0 = 空状态）。请勿把 `1u << 序号` 写进业务逻辑。
	/// @param modifiers 此刻的修饰键状态（与 `KeyEvent` 同类型同语义）
	/// @note **Alt 恒不置位**——**当前 Win32 鼠标映射**不从 `wParam` 产生 Alt（`WM_*MOUSE*` 无 `MK_ALT`）。
	///       这是**映射边界**，**不是**事件模型不支持 Alt（`KeyModifier` 本就含 `Alt`）。
	///       理由、措辞纪律与重启条件见 docs/phase19-mouse-event-dimensions-preliminary-design.md §2.3。
	MouseEvent(
		Window* window,
		int mouseX,
		int mouseY,
		unsigned int pressedButtons = 0,
		KeyModifier modifiers = KeyModifier::None)
		: InputEvent(window), m_mouseX(mouseX), m_mouseY(mouseY),
		  m_pressedButtons(pressedButtons), m_modifiers(modifiers) {

	}

public:

	int GetMouseX() const noexcept { return m_mouseX; }   // 不动
	int GetMouseY() const noexcept { return m_mouseY; }   // 不动

	/// @brief 此刻指定的鼠标键是否处于按下状态（**原始事实**——不做"拖动"之类语义结论）
	/// @note 位序 = `MouseButton` 枚举序（见构造注释）；"未按下"与"无信息"不作区分（默认值即空）
	bool IsButtonDown(MouseButton button) const noexcept {

		return (m_pressedButtons & (1u << static_cast<unsigned int>(button))) != 0u;

	}

	/// @brief 是否按下指定修饰键组合（位与 == 全含——`HasModifier(Ctrl | Shift)` 可组合查询）
	/// @note 与 `KeyEvent::HasModifier` **同名同义**（「一个概念只用一个词」）
	/// @note `KeyModifier::Alt` 当前**恒返回 false**（当前 Win32 鼠标映射不产生 Alt，见构造注释）
	/// @note ★ **`KeyModifier::None` 恒返回 true**——"**全含**"语义对**空集合空洞成立**（`x & 0 == 0`）。
	///       这是**继承 `KeyEvent` 的既有语义**，本项**有意保持一致、不改**（两处必须同名同义）。
	///       改它属**独立的 API 语义问题** ⇒ 记账于 `framework-defect-audit.md` **§7 A-10**（含重启条件）。
	bool HasModifier(KeyModifier modifier) const noexcept {

		return (static_cast<int>(m_modifiers) & static_cast<int>(modifier)) == static_cast<int>(modifier);

	}

private:

	int m_mouseX;
	int m_mouseY;
	unsigned int m_pressedButtons = 0;             ///< ★ 新增：位序 = MouseButton 枚举序
	KeyModifier m_modifiers = KeyModifier::None;   ///< ★ 新增：与 KeyEvent 同源同类型

};
```

#### △3–△10 · 五个具体类（**只追加带默认值的尾形参**）

★ **要点：默认实参不通过继承传递** ⇒ 六个类（含 `MouseButtonEvent`）**必须各自带**这两个默认形参，**不能只改基类**。

```cpp
// △3 MouseMoveEvent.h —— 追加在最后
	MouseMoveEvent(
		Window* window, int mouseX, int mouseY,
		unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None
	): MouseEvent(window, mouseX, mouseY, pressedButtons, modifiers) { }

// △4–△5 MouseButtonEvent.h（protected 中间类）—— 追加在 button 之后
	MouseButtonEvent(
		Window* window, int mouseX, int mouseY, MouseButton button,
		unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None
	): MouseEvent(window, mouseX, mouseY, pressedButtons, modifiers), m_button(button) { }

// △6–△7 MouseButtonDownEvent.h —— ★ 追加在【既有的 isDoubleClick 之后】
	MouseButtonDownEvent(
		Window* window, int mouseX, int mouseY, MouseButton button,
		bool isDoubleClick = false,                                   // 既有，不动
		unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None
	): MouseButtonEvent(window, mouseX, mouseY, button, pressedButtons, modifiers),
	   m_isDoubleClick(isDoubleClick) { }

// △8–△9 MouseButtonUpEvent.h —— 追加在 button 之后
	MouseButtonUpEvent(
		Window* window, int mouseX, int mouseY, MouseButton button,
		unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None
	): MouseButtonEvent(window, mouseX, mouseY, button, pressedButtons, modifiers) { }

// △10 MouseWheelEvent.h —— 追加在既有的 delta 之后
	MouseWheelEvent(
		Window* window, int mouseX, int mouseY, int delta,
		unsigned int pressedButtons = 0, KeyModifier modifiers = KeyModifier::None
	): MouseEvent(window, mouseX, mouseY, pressedButtons, modifiers), m_delta(delta) { }
```

★ **零破坏的自证方式**：这六份头改完后，**不改平台层、不改测试**，直接构建 ⇒ **既有 231 用例应全绿**（22 处构造点全部靠默认值）。这是"零破坏"的**第一个可验证信号**，也是实现顺序的检查点 ①（§7）。

### 2.2 平台层（△11–△12）

#### △11 · 两个文件局部 helper（`WindowMessageHandler.cpp`，anonymous namespace）

```cpp
/// @brief 取「此刻按下的鼠标键」掩码（位序 = MouseButton 枚举序——本框架自有约定，与 MK_* 数值无关）
/// @note ★ 只读 LOWORD(wParam)：WM_MOUSEWHEEL 的 HIWORD 是 wheel delta、
///       WM_XBUTTON* 的 HIWORD 是 XBUTTON1/2（"本次是哪个键"）——读整个 wParam 会把它们误当按下位。
///       ★ 必须显式映射：5 个 MK_* 里有 3 个与框架位序数值不同（Middle / X1 / X2），移位捷径会静默写错。
unsigned int TranslateMouseButtons(WPARAM wParam) {
	const WORD bits = LOWORD(wParam);
	unsigned int mask = 0;
	const auto AddIf = [&mask, bits](WORD mk, MouseButton b) {
		if (bits & mk) mask |= 1u << static_cast<unsigned int>(b);
	};
	AddIf(MK_LBUTTON,  MouseButton::Left);
	AddIf(MK_RBUTTON,  MouseButton::Right);
	AddIf(MK_MBUTTON,  MouseButton::Middle);
	AddIf(MK_XBUTTON1, MouseButton::X1);
	AddIf(MK_XBUTTON2, MouseButton::X2);
	return mask;
}

/// @brief 取「此刻的修饰键」——★ **与键盘侧不同源**：此处走 wParam（事件时刻的权威值），故不含 Alt
/// @note 键盘侧的 TranslateModifier() 走 GetKeyState（平台所迫：WM_KEYDOWN 的 wParam 是虚拟键码，
///       不携带修饰位）。两者**都**不在消费侧查询，分层不变。
KeyModifier TranslateMouseModifiers(WPARAM wParam) {
	KeyModifier m = KeyModifier::None;
	const WORD bits = LOWORD(wParam);
	if (bits & MK_SHIFT)   m = m | KeyModifier::Shift;
	if (bits & MK_CONTROL) m = m | KeyModifier::Ctrl;
	return m;   // 无 Alt——平台不提供 MK_ALT（见初步设计 §2.3）
}
```

★ 放在**文件局部**（与既有 `TranslateModifier` / `TranslateMouseButton` 同风格）：**不进头**、不新增公共类型（契约 **C6**）。

#### △12 · 五个调用点（逐点 diff）

| # | 位置 | 改前（末尾形参） | 改后 |
|---|---|---|---|
| **1** | `:97` `WM_MOUSEMOVE` | `window, x, y` | `window, x, y, TranslateMouseButtons(wParam), TranslateMouseModifiers(wParam)` |
| **2** | `:114` 按下组（`WM_LBUTTONDOWN`/`R`/`M`/`XBUTTON`） | `window, GET_X, GET_Y, TranslateMouseButton(msg, wParam)` | 同上追加两个实参 |
| **3** | `:130` **双击** `WM_LBUTTONDBLCLK` | `window, GET_X, GET_Y, MouseButton::Left, true` | `... , true, TranslateMouseButtons(wParam), TranslateMouseModifiers(wParam)` ★ **追加在 `true` 之后** |
| **4** | `:149` 抬起组 | `window, GET_X, GET_Y, TranslateMouseButton(msg, wParam)` | 同上追加两个实参 |
| **5** | `:173` 滚轮 | `window, point.x, point.y, delta` | `... , delta, TranslateMouseButtons(wParam), TranslateMouseModifiers(wParam)` ★ 该分支**已用 `wParam` 取 `HIWORD`（delta）**，与 helper 内的 `LOWORD` **并存**——这是最容易写错的一处 |

★ **可读性对策（针对"位置传参易错位"）**：5 处调用点**一律换行 + 逐参行尾注释**，例如

```cpp
		MouseButtonDownEvent event(
			window,
			GET_X_LPARAM(lParam),
			GET_Y_LPARAM(lParam),
			TranslateMouseButton(msg, wParam),                 // 本次是哪个键（HIWORD——仅 X 键用）
			false,                                             // isDoubleClick（双击分支传 true）
			TranslateMouseButtons(wParam),                     // 此刻按下的键（LOWORD 的 MK_*）
			TranslateMouseModifiers(wParam)                    // 此刻的修饰键（LOWORD 的 MK_SHIFT/CONTROL）
		);
```

理由：`MouseButtonDownEvent` 的第 4 个实参是 `button`、第 6 个才是 `pressedButtons`（中间隔着 `isDoubleClick`）⇒ **两个"键"类实参不相邻**，是最易错位的地方。

### 2.3 ★ 实现盯防清单（本相位最易写错的 6 条）

| # | 红线 | 后果 |
|---|---|---|
| **1** | ★ **默认实参不继承** ⇒ **6 个类都要带**，不能只改 `MouseEvent` | 否则 `MouseMoveEvent(nullptr,x,y)` 编译失败 ⇒ 22 处测试构造点全崩（**违 R5**） |
| **2** | ★★ **只读 `LOWORD(wParam)`** 取状态位 | 读到 `HIWORD` ⇒ 滚轮把 delta 当按下位、X 键把 `XBUTTON1/2` 当按下位 |
| **3** | ★★ **显式映射表，禁止移位**（B16：3/5 数值不同） | `Middle`/`X1`/`X2` 静默错位（`MK_MBUTTON` 0x10 落在框架的 bit4=X2 上） |
| **4** | ★ **`GET_XBUTTON_WPARAM` 只喂 `TranslateMouseButton`** | 其返回值 `0x0001`/`0x0002` **恰等于 `MK_LBUTTON`/`MK_RBUTTON`** ⇒ 会点亮**左键或右键**（T19-4 断言 `IsButtonDown(Right)==false` 专门钉这一条） |
| **5** | ★ **不要动既有 22 处测试构造点与全部消费点** | 违 C5；`ScrollBar::m_dragging` / `TextBox::m_mouseDown` **不简化**（D4） |
| **6** | ★ **不在 Event 层引入任何 Win32 查询**（`GetKeyState` 等） | 违 C3/C6；且会让 A1/A2 失去无头可断言的资格 |
| **7** | ★ **不要顺手改 `HasModifier(KeyModifier::None)` 的语义**（当前恒 true = "全含"的**空洞成立**，与 `KeyEvent` 一致） | 改它会**静默改变键盘侧语义**（两处实现必须同名同义）；属**独立 API 语义问题** ⇒ 记账 **A-10**（§1.5） |

---

## 3. 位序与映射（契约 C1 的精确形态）

**框架位序（↔ `MouseButton` 枚举序）**

| `MouseButton` | 枚举值 | 框架位 | 掩码 |
|---|---|---|---|
| `Left` | 0 | bit0 | `0x01` |
| `Right` | 1 | bit1 | `0x02` |
| `Middle` | 2 | bit2 | `0x04` |
| `X1` | 3 | bit3 | `0x08` |
| `X2` | 4 | bit4 | `0x10` |

**平台映射（`MK_*` → 该位）——★ 必须显式，禁止移位**

| `MK_*` | 值 | → 框架位 | 数值是否相同 |
|---|---|---|---|
| `MK_LBUTTON` | 0x0001 | `Left` `0x01` | ✅ 相同（巧合） |
| `MK_RBUTTON` | 0x0002 | `Right` `0x02` | ✅ 相同（巧合） |
| `MK_MBUTTON` | **0x0010** | `Middle` `0x04` | ❌ **不同** |
| `MK_XBUTTON1` | **0x0020** | `X1` `0x08` | ❌ **不同** |
| `MK_XBUTTON2` | **0x0040** | `X2` `0x10` | ❌ **不同** |

★ 前两个"巧合相同"恰是最危险的地方：**照抄前两个的写法（移位）会在后三个上静默出错**（`MK_MBUTTON` 0x0010 会落到框架 `X2` 的位上）。

**修饰键映射（`MK_*` → `KeyModifier`）**

| `MK_*` | 值 | → `KeyModifier` |
|---|---|---|
| `MK_SHIFT` | 0x0004 | `Shift` (1) |
| `MK_CONTROL` | 0x0008 | `Ctrl` (2) |
| （无 `MK_ALT`） | — | **不置位** |

★ 按键位与修饰键位**互不重叠**（B17）⇒ 两维天然不串（用例 T19-3 / T19-6 双向验证）。

---

## 4. 契约 C1–C6 的验证映射

| 契约 | 由什么保证 |
|---|---|
| **C1 位序 = 枚举序** | §3 表 + 头注释 + **端到端用例 T19-1..T19-4**（同时钉住"两处公式必须一致"） |
| **C2 单一读取点 / 只读 `LOWORD`** | 代码结构（两个 helper 是唯一读 `wParam` 的新增点）+ `GET_XBUTTON_WPARAM` 仅出现在 `TranslateMouseButton` + **T19-4 / T19-8** |
| **C3 不在鼠标路径用 `GetKeyState`** | grep（`WindowMessageHandler.cpp` 里 `GetKeyState` 只应在既有的 `TranslateModifier` 内）+ **T19-7** |
| **C4 默认值 = 空状态** | 六份头的默认形参 + **既有 231 用例全绿**（不改测试即通过 ⇒ 行为逐位不变）+ T19-5 |
| **C5 零破坏（精确口径）** | `git diff` 结构性核查：**测试 22 处构造点 + 全部消费点无改动**；**翻译器 5 处必然有改动**（本项实现） |
| **C6 平台层零新增公共类型** | `MK_*` / `XBUTTON*` 只在 `src/Platform/Win32/` 出现；两个 helper 为**文件局部**、不进头 |

---

## 5. 测试实现规格

### 5.1 测试装置改动（`EventTests.cpp`——**B14 的落地**）

**① `ReceivedEvent` 追加 3 个字段**（△C1）：

```cpp
struct ReceivedEvent
{
    EventType type = EventType::None;
    KeyCode keyCode = KeyCode::Unknown;
    MouseButton button = MouseButton::Left;
    int x = 0;
    int y = 0;
    char32_t codepoint = 0;
    int width = 0;
    int height = 0;
    unsigned int pressedButtons = 0;            // ★ 新增
    KeyModifier modifiers = KeyModifier::None;  // ★ 新增
    int delta = 0;                              // ★ 新增（T19-8：证明 LOWORD 与 HIWORD 两条路径并存）
};
```

**② 两个"谓词 → 记录值"小工具**（★ 因为 `MouseEvent` **只提供谓词**，没有 `GetModifiers()` / 取整个掩码的接口——§1.1 D5）：

```cpp
// 小工具：把谓词折叠成"可断言的记录值"（仅测试用——消费侧正式用法就是直接调 IsButtonDown）
unsigned int SnapshotButtons(const MouseEvent& e) {
    unsigned int m = 0;
    const MouseButton all[] = { MouseButton::Left, MouseButton::Right,
                                MouseButton::Middle, MouseButton::X1, MouseButton::X2 };
    for (MouseButton b : all) if (e.IsButtonDown(b)) m |= 1u << static_cast<unsigned int>(b);
    return m;
}
KeyModifier SnapshotModifiers(const MouseEvent& e) {
    KeyModifier m = KeyModifier::None;
    if (e.HasModifier(KeyModifier::Shift)) m = m | KeyModifier::Shift;
    if (e.HasModifier(KeyModifier::Ctrl))  m = m | KeyModifier::Ctrl;
    if (e.HasModifier(KeyModifier::Alt))   m = m | KeyModifier::Alt;   // 恒不成立——但"照抄谓词"
    return m;
}
```

★ 这两个小工具**只存在于测试**，且**故意用公共谓词**（而非去读私有掩码）——这样测试断言的是**公共 API 的可见行为**，而非内部表示。⇒ 若将来掩码换成命名类型，测试**不必改**。

**③ `OnEvent` 的 switch：既有 2 个分支补字段 + 新增 2 个 case**（△C2）：

```cpp
        case EventType::MouseButtonDown:
        {
            const auto& e = static_cast<const MouseButtonDownEvent&>(event);
            r.button = e.GetButton();
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);      // ★ 补
            r.modifiers = SnapshotModifiers(e);         // ★ 补
            break;
        }
        case EventType::MouseButtonUp:                  // ★ 新增 case
        {
            const auto& e = static_cast<const MouseButtonUpEvent&>(event);
            r.button = e.GetButton();
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);
            r.modifiers = SnapshotModifiers(e);
            break;
        }
        case EventType::MouseMove:
        {
            const auto& e = static_cast<const MouseMoveEvent&>(event);
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);      // ★ 补
            r.modifiers = SnapshotModifiers(e);         // ★ 补
            break;
        }
        case EventType::MouseWheel:                     // ★ 新增 case
        {
            const auto& e = static_cast<const MouseWheelEvent&>(event);
            r.x = e.GetMouseX();
            r.y = e.GetMouseY();
            r.pressedButtons = SnapshotButtons(e);
            r.modifiers = SnapshotModifiers(e);
            r.delta = e.GetDelta();                     // ★ 唯一读 delta 的地方
            break;
        }
```

★ **四个 case 各自 `static_cast`**——**沿用该文件既有风格**（现有代码就是逐类型 `static_cast`）。**不用** `dynamic_cast`、也不合并成基类共用分支：`button` 只存在于 Down/Up，硬合并要靠 RTTI 判型，反不如四个直白的 case 好读（教学型取舍）。

**④ 该文件还需补两个 include**（现行 include 列表见 `EventTests.cpp:12-18`）：

```cpp
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"   // ★ 新增（MouseButtonUp case 的 static_cast）
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"      // ★ 新增（MouseWheel case 的 static_cast）
```

（`KeyModifier.h` **已在** `:18` 引入 ⇒ `ReceivedEvent` 的新字段类型直接可用；`MouseEvent.h` 经 `MouseMoveEvent.h` 间接可见 ⇒ 无需显式 include）

### 5.2 九个用例（T19-1..T19-9，完整输入/期望）

全部经 `handler.Handle(nullptr, nullptr, msg, wParam, lParam)` → `FakeHost` 断言（**真翻译路径**，Q4）。

| # | 输入（msg / wParam / lParam） | 期望 | 钉住 |
|---|---|---|---|
| **T19-1** | `WM_MOUSEMOVE` / `MK_LBUTTON` / `MAKELPARAM(10,20)` | `type==MouseMove` · `x==10` · `y==20` · `pressedButtons == 0x01` · `IsButtonDown(Right)==false` | **A1** |
| **T19-2** | `WM_MOUSEMOVE` / `MK_LBUTTON \| MK_RBUTTON` / `0` | `pressedButtons == 0x03`（左**与**右同时在位 ⇒ 是"集合"不是"某一个键"） | **A1 / R1** |
| **T19-3** | `WM_LBUTTONDOWN` / `MK_CONTROL \| MK_SHIFT` / `MAKELPARAM(1,2)` | `button==Left` · `HasModifier(Ctrl)` · `HasModifier(Shift)` · `HasModifier(Ctrl\|Shift)` 全 true · ★ **`pressedButtons == 0`**（该 wParam **不含**任何 `MK_*` 按键位 ⇒ 修饰键位不串进按键位） | **A2 / R2** |
| **T19-4** ★ | `WM_XBUTTONDOWN` / `MAKEWPARAM(MK_XBUTTON1, XBUTTON2)` / `0` | `pressedButtons == 0x08`（**X1**——来自 `LOWORD`）· `IsButtonDown(X2)==false` · `button == X2`（来自 `HIWORD`）· ★★ **`IsButtonDown(Right)==false`**（若误用 `GET_XBUTTON_WPARAM` 当掩码，`0x0002` 会**点亮右键**——本断言专钉该后果） | **Q2 / C2** |
| **T19-5** | `WM_MOUSEMOVE` / `0` / `0` | `pressedButtons == 0` · `modifiers == None`（空 = 空，非"未知"） | **R6** |
| **T19-6** | `WM_MOUSEMOVE` / `MK_SHIFT` / `0` | `modifiers == Shift` · `HasModifier(Ctrl)==false` · `pressedButtons == 0`（修饰键位不串进按键位——与 T19-3 反向） | **R2** |
| **T19-7** | `WM_MOUSEMOVE` / `MK_LBUTTON` / `0` | ★ **`HasModifier(KeyModifier::Alt)==false`** —— 把"Alt 恒不置位"钉成**回归契约**（将来被误判为 bug 时测试会说明这是有意为之） | **D3 / C3** |
| **T19-8** ★ | `WM_MOUSEWHEEL` / `MAKEWPARAM(MK_CONTROL, WHEEL_DELTA)` / `0` | `type==MouseWheel` · `modifiers == Ctrl`（`LOWORD`）· **`delta == 120`**（`HIWORD`）⇒ **两条 `LOWORD`/`HIWORD` 路径并存且互不干扰**。⚠️ **不断言坐标**（`hwnd==nullptr` ⇒ `ScreenToClient` 不生效，B10） | **R3 / C2** |
| **T19-9** | `WM_LBUTTONUP` / `0` / `MAKELPARAM(1,2)` | `type==MouseButtonUp` · `button==Left` · `pressedButtons == 0`（抬起事件的按下集**不含**被抬起键） | **R3** |

**登记（B15）**：在既有 `RegisterEventTests()` 内追加 5 条：

```cpp
    GetTestRegistry().Add("Event.MouseStatePressedButtons",  &TestMouseStatePressedButtons);   // T19-1/2/5
    GetTestRegistry().Add("Event.MouseStateModifiers",       &TestMouseStateModifiers);        // T19-3/6
    GetTestRegistry().Add("Event.MouseStateXButtonTrap",     &TestMouseStateXButtonTrap);      // T19-4
    GetTestRegistry().Add("Event.MouseStateAltBoundary",     &TestMouseStateAltBoundary);      // T19-7
    GetTestRegistry().Add("Event.MouseStateWheelAndUp",      &TestMouseStateWheelAndUp);       // T19-8/9
```

★ 命名与既有 `Event.Translator*` 并列（`Event.MouseState*`）；**用例数 5**（用例内多断言）⇒ `ecdi_tests` **231 → 236**。

⚠️ **数值勘误声明**：初设 §5 写"用例 **231 → 240**（+9）"，那是把 **T 编号数（9）当作用例数**。本稿按**注册条目数**计 ⇒ **+5 = 236**。★ 若评审希望"一个 T 编号 = 一条注册用例"，则改为 **9 条**（`231 → 240`）——**两种都可行，请在评审时指定**（本稿倾向 **5 条**：同一装置、同一主题的连续断言放一个用例内，便于失败定位）。

---

## 6. P1 / P2 的实测方案与记录位置（初设 §9-④）

| 项 | 内容 |
|---|---|
| **P1** | `WM_LBUTTONDOWN` 的 `wParam` 是否**置** `MK_LBUTTON` |
| **P2** | `WM_LBUTTONUP` 的 `wParam` 是否**不含** `MK_LBUTTON` |
| **方式 A（结构性）** | 既有测试 `EventTests.cpp:148` 已按"**含**"的约定写（B13）⇒ 新用例**沿用同一约定**即可，**不新增平台假设**。★ 但这只证明"与既有约定一致"，**不证明真实平台行为** |
| **方式 B（真验证，推荐）** | 在 `WindowMessageHandler.cpp` 的 `WM_LBUTTONDOWN` 分支**临时**加一行 `Logger::Log(LogLevel::Debug, L"...LOWORD(wParam)=...")`，用户在 ModelProbe 里点一下 ⇒ 看日志。**验证完即删**（不得留在提交里） |
| **记录位置** | **本稿 §6 的"实测回填"表**（实现后追加，模式同 Phase 18 详设 §7.1） |
| ★ **从根上回避（推荐同时采用）** | **头注释只描述映射规则**（"`pressedButtons` = `LOWORD(wParam)` 的 `MK_*` 映射"），**不举例**"按下左键时掩码必含 `Left`" ⇒ 于是 **P1/P2 无论实测结果如何，注释都不会说谎**，也就不必为了注释去实测 |
| **纪律（重申）** | 若实测与预期不符 ⇒ **只改注释**，**代码与用例不变**（因为断言的是"掩码 == `LOWORD` 的映射"，不是"某键是否在内"） |

---

## 7. 实现顺序与检查点

| 步 | 动作 | 检查点（做错时的最早信号） |
|---|---|---|
| **①** | 六份公共头（△1–△10） | ★ **不动平台层、不动测试**直接构建 ⇒ **既有 231 全绿**（证明"默认值 ⇒ 零破坏"，违 C5 的最早信号） |
| **②** | 平台层两个 helper + 5 个调用点（△11–△12） | 构建通过；行为无变化（本项不改变任何交互行为） |
| **③** | 测试装置扩展（`ReceivedEvent` +3 · `OnEvent` +2 case + 补行） | 既有 5 个 Event 用例仍全绿 |
| **④** | 新增 5 条用例（T19-1..T19-9） | **四工具链** `ecdi_tests` **236 全绿**；断言特征串 **11 → 11** |
| **⑤** | P1/P2 实测（方式 B，临时日志，验完即删） | 回填 §6 |
| **⑥** | 文档收口（§10） | 两份 README / `roadmap #41` / 详设回填 |

★ **步 ① 是本相位最有价值的一个检查点**：它把"零破坏"从**声明**变成**可观测事实**（不改调用点即通过）。

---

## 8. 影响面（汇总）

| 面 | 规模 |
|---|---|
| **公共头** | **92 → 92**（改 6 个既有头；**无新头**）· `MouseEvent.h` **+2 include** |
| **公共 API 净增** | **+2**（`MouseEvent::IsButtonDown` · `MouseEvent::HasModifier`） |
| **构造形参** | **+12**（6 类 × 2，**均带默认值**）——不计入 API 净增，但**六个类都不能漏** |
| **实现** | `WindowMessageHandler.cpp`：**+2 文件局部 helper** · **5 个调用点**改形参 |
| **测试** | `EventTests.cpp`：`ReceivedEvent` **+3 字段** · `OnEvent` **+2 case + 2 处补字段** · **+5 条注册用例**（T19-1..T19-9） |
| **用例数** | **231 → 236**（★ 初设写 240 为"T 编号数"口径，见 §5.2 勘误声明） |
| **断言特征串** | **11 → 11**（本项**不新增断言**） |
| **新测试文件** | **0** · `RunAllTests.h` / `RunAllTests.cpp` **0 改动**（**修正初设 §9-③**） |
| **`examples/`** | **零改动** |
| **构建** | 零改动（`GLOB_RECURSE … CONFIGURE_DEPENDS`；无新文件） |

---

## 9. 验收（A1–A5）

| # | 判据 | 落地 |
|---|---|---|
| **A1** | 按住左键移动 ⇒ 订阅方从**移动事件本身**得知"左键按着" | **T19-1 / T19-2**（真翻译路径；**不得**用手工构造充当） |
| **A2** | 按住 Ctrl / Shift 点击 ⇒ 按键事件能读到修饰键 | **T19-3 / T19-6** |
| **A3** | 零回归 | **四工具链 `ecdi_tests` 236 全绿**（**须写明断言是否启用**） |
| **A4** | 零破坏（结构性） | `git diff`：**测试 22 处构造点 + 全部消费点无改动**；翻译器 5 处**必有改动** |
| **A5** | ModelProbe 目视行为不变 | 拖动窗口 / 拖选文本 / 滚动条拖动**行为与改前一致**（本项只补信息） |

---

## 10. 文档收口清单（实现后执行）

| # | 动作 |
|---|---|
| 1 | 本稿 §6 **P1/P2 实测回填** |
| 2 | `roadmap-deferred.md` **§7.9 #41** → 标 **✅ 已实现**（附实测用例数与特征串） |
| 3 | `docs/README.md` + 根 `README.md`：Phase 19 状态 → ✅ 已实现；**用例锚点 231 → 236**；**文档数 132 → 133** |
| 4 | 初设 §5 / §9 的**用例数口径**与 **登记两处**两处按 §1.3 的修正回写（或在新稿注明"以详设为准"） |
| 5 | `MEMORY.md`：Phase 19 收口 + 规模锚点刷新 |

---

## 11. 修订记录

- **v1.1**（2026-09-23）**评审通过 + 一处边界记账**。① **评审结论**：外部评审 ✅ **通过、无阻塞项**；确认本稿达 implementation specification 级，逐项认可掩码通道语义 / 位序量化表 / **用例口径 240 → 236** / Alt 两层区分 / P1-P2 纪律 / 步① 检查点。② ★ **采纳评审提出的 `HasModifier(KeyModifier::None)` 边界问题**——**实测确认**其恒为 `true`（"全含"对空集合**空洞成立**），按评审建议**不改**，但**三处记录**：§2.1 `@note` · §2.3 盯防第 **7** 条 · 审计 **§7 A-10**（含重启条件与辅助取证：无测试覆盖、无消费者、分支不可达）。③ **新增 §1.5**（本轮评审逐条处置 + 实测算例输出）。④ **内容零删改**——六头草案 / 两 helper / 5 翻译点 / 盯防 1–6 条 / 位序表 / 契约映射 / 测试规格 / P1-P2 / 实现顺序 **全部原样保留**。

- **v1.0**（2026-09-23）初稿。**输入**：需求 v1.1（✅ 通过）· 初设 v1.1（✅ 通过，无阻塞项；§9 交办四件事）。**内容**：§1.2 **B13–B18 详设新增基线**（含 ★★ **B13 既有测试已按 LOWORD/HIWORD 约定写** · ★★ **B14 `FakeHost` 缺 `MouseButtonUp`/`MouseWheel` 分支且无 delta 字段** · ★ **B15 无需登记两处** · ★★ **B16 位序与 `MK_*` 5 个里 3 个不同**）；§1.3 **对初设的三处修正**（△C1 字段 +3 · △C2 补 2 个 case · △C3 登记不涉及 `RunAllTests.*`）；§1.4 四件事索引；**§2 逐文件行级改动 △1–△12**（六份头全文草案 + 两个 helper 全文 + 5 个调用点逐点 diff + ★ **实现盯防清单 6 条**）；**§3 位序与映射表**（含"禁止移位"的数值证据）；**§4 契约 C1–C6 验证映射**；**§5 测试实现规格**（装置改动完整片段 + **9 个 T 用例的输入/期望表** + 注册草案 + ★ **用例数口径勘误声明**）；**§6 P1/P2 实测方案**（两种方式 + 记录位置 + ★ **从根上回避的写法建议**）；**§7 实现顺序与 6 个检查点**（★ 步①"不改调用点即全绿"= 零破坏的最早信号）；§8 影响面（用例 **231→236**）；§9 验收 A1–A5；§10 文档收口清单。**待评审**。
