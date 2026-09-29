# Phase 22 · `Create` 的 DIP 尺寸契约（pre-show DIP geometry）—— 详细设计（v1.3）

> 来源：**初步设计 `docs/phase22-preshow-dip-geometry-preliminary-design.md` v1.1 已通过评审**（2026-09-29；评审结论「通过，可进入详细设计」）
> 状态：**v1.3 已实施并收口**（2026-09-29）——★ 前置：**v1.1 已通过评审**（2026-09-29；外部评审结论「**通过，可以进入 Implementation**」——附 **2 个 🟡**，均为**验收 / 测试覆盖的表述口径**问题，逐条处置见 **§1.5**）⇒ ★★ **单批实施完成**（△1–△9 · **9 文件**）：**四链实测 264 / 264 全绿**（`MSVC` / `ClangCL` / `Clang` / `MinGW` · @DPI 120 · **断言启用**）——★ 实测判定见 **§5.1**，实施记录见 **§7.1**
> 本稿输入：初设稿的 **B1–B10 / D0–D1 / C1–C10 / O1–O5 / T22-1..T22-3** ＋ ★ 本稿新增的**代码基线 B11–B18**（全部带行号）＋ ★ 本稿对初设的**三处修正 / 细化**（§1.3）＋ ★ 外部评审（详设）**§1–§12 的逐条处置**（§1.5）

---

## 1. 设计输入与实施总览

### 1.1 已定项（初设的结论，本稿直接采用）

| 项 | 结论 | 出处 |
|---|---|---|
| **D0 落点** | `Application::Create` **末尾**、`WindowCreatedEvent` **之前** | 初设 §2.1（O3 定案） |
| **D1 形态** | 新增 **1 个无参纯虚** `PlatformWindow::ApplyStartupSize()`；**整块搬** `Show()` 的换算块（含 `SetDpi`） | 初设 §2.2 |
| **命名** | `ApplyStartupSize`（与成员 `m_startupWidthDip` 同词根；DIP 语义**不进名字**） | 初设 O1 |
| **不做的** | **不做** D-SEAM-1 第 ② 步（`Window` 透传）· **不补**客户区 DIP 查询 API | 初设 §2.3 / §1.1（D2） |
| **纯虚** | **纯虚，不给默认实现**（代价 = 2 个替身各 +1 空实现） | 初设 O4 |
| **契约** | **C1–C10**（★ C10 = 无启动尺寸 ⇒ 整方法早退、连 `SetDpi` 也不刷） | 初设 §5 |
| **测试** | **T22-1..T22-3**；★ T22-1 = **时序稳定性**、T22-3 = **直接 DIP 契约**、T22-2 = **platform seam contract test** | 初设 §8 |

### 1.2 ★★ 实施总览（**9 文件 = 生产 4 + 测试 5**，新建 1）

| # | 文件 | 性质 | 改动 |
|---|---|---|---|
| **△1** | `include/ECDI/Platform/PlatformWindow.h` | 生产（公共头） | **+1 纯虚**（Phase 22 分组，置于类尾） |
| **△2** | `src/Platform/Win32/Win32PlatformWindow.h` | 生产（内部头） | **+1 override 声明**（Phase 22 分组） |
| **△3** | `src/Platform/Win32/Win32PlatformWindow.cpp` | 生产 | **+1 方法体** ＋ `Show()` 瘦身（换算块 → 一行调用） |
| **△4** | `src/Application/Application.cpp` | 生产 | ★★ **+1 include** ＋ **+1 调用** |
| **△5** | `src/Tests/AnimationTests.cpp` | 测试（替身 1） | **+1 空实现** ＋ 分组注释 |
| **△6** | `src/Tests/ProgressBarTests.cpp` | 测试（替身 2） | **+1 空实现** ＋ 分组注释 |
| **△7** | `src/Tests/PreshowGeometryTests.cpp` | ★ **新建** | T22-1..T22-3（含 fixture 与两个 helper） |
| **△8** | `src/Tests/RunAllTests.h` | 测试（登记） | **+1 声明** |
| **△9** | `src/Tests/RunAllTests.cpp` | 测试（登记） | **+1 调用** |

★ **零改动**：`Window.h` / `Window.cpp` · `Application.h` · `PlatformWindowHost.h` · 渲染侧 · `CMakeLists.txt`（`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库）· `main.cpp`。
★ **规模**：公共头 **92 → 92** · 公共 API **+1** · **断言特征串不变（11 条）** · **用例 261 → 264**（+3）。

### 1.3 ★★ 对初设的三处修正 / 细化（★ 如实报告）

| # | 初设原文 | 实际 | 性质 |
|---|---|---|---|
| **D-1** | §6「改动的文件 = **6 个**」（生产 4 + 替身 2） | ★★ **实际 9 个**——初设**只数了替身，未计「测试文件的新增与登记」**：T22-1..T22-3 需要一个**承载文件**（⇒ △7 新建）＋ `RunAllTests.h` / `.cpp` **各 1 处登记**（⇒ △8 / △9）。★ **这不是"顺手多做"，而是"不改这 3 个文件则用例根本不存在"** | 🟡 **计数遗漏**（初设的"文件数"应含测试承载与登记——★ 与 R-③ 同族：**"改动的文件"这一列要按"不改它就做不成"来枚举**） |
| **D-2** | §6「`Application.cpp`（+1 调用）」 | ★ **该文件需 +1 include**——`Window.h:24` 只有 `class PlatformWindow;` **前置声明**（B11），而 `Application.cpp` **未 include** `PlatformWindow.h`（B12）⇒ 调用 `ApplyStartupSize()` 前必须补 include | 🟡 **依赖遗漏** |
| **D-3** | §4.1 伪代码把换算块写成**早退形态**（`if (无尺寸) return;`） | 原代码是**块级 `if`（正条件 `> 0`）**，其**外层还有 `m_shown = true` / `ShowWindow` / `UpdateWindow`**（B15）⇒ ★ **早退形态只在"方法内"成立**：`Show()` 侧必须改成**调用**（不能把 `Show()` 的后续语句也吞进早退） | 🟢 **措辞细化**（两者语义等价——初设的写法本就正确，此处冻结 **`Show()` 侧的具体形态**） |

### 1.4 ★ 代码基线 B11–B18（本稿新增，全部带行号）

| # | 事实 | 证据 |
|---|---|---|
| **B11** | ★★ `Window.h` 对 `PlatformWindow` **只有前置声明**——`class PlatformWindow;   // 前置声明（unique_ptr 成员——平台抽象，7.1.1）` | `include/ECDI/Window/Window.h:24` |
| **B12** | ★★ `Application.cpp` 的 include 段**不含** `PlatformWindow.h`（含 `Window.h` 等 22 项） | `src/Application/Application.cpp:1-22` |
| **B13** | ★ 调用平台接口的 **.cpp 显式 include 先例**：`TextBox.cpp` 在 `#include "ECDI/Window/Window.h"`（`:5`）之后**另起** `#include "ECDI/Platform/PlatformWindow.h"`（`:14`）——它调 `GetPlatformWindow().StartTimer/SetClipboardText`（`:173` / `:772` 等） | `src/Widget/TextBox.cpp:5/14/173` |
| **B14** | ★★ **构造期已经设过 DPI**：`m_messageHandler.SetDpi(GetDpiForWindow(m_hwnd));`（紧随 `CreateWindowExW` 成功之后） | `src/Platform/Win32/Win32PlatformWindow.cpp:145-148` |
| **B15** | ★★ `Show()` 的换算块是**块级 `if`（正条件 `> 0`）**，**不是早退**；其后还有 `m_shown = true` / `ShowWindow` / `UpdateWindow`（全在 `if (m_hwnd != nullptr)` 内） | `Win32PlatformWindow.cpp:171-202`（换算块 `:182-192`） |
| **B16** | ★ 两个替身的 **Phase 14 分组位置**：替身 1 = `// ── Phase 14：新增 2 个纯虚（本替身不关心显示/拖入——空实现）──` + `Hide()` + `SetFileDropEnabled(bool)`，紧接其后即**成员区**；替身 2 同款（分组注释 + 两方法 + 成员区） | `src/Tests/AnimationTests.cpp:85-91` · `src/Tests/ProgressBarTests.cpp:76-82` |
| **B17** | ★ 登记设施 = **两处**：`RunAllTests.h` 的 `void RegisterXxxTests();` + `RunAllTests.cpp` 的调用（Phase 21 的 `RegisterIconDecodeTests()` 为最近先例） | `src/Tests/RunAllTests.h` 尾 · `RunAllTests.cpp` 尾 |
| **B18** | ★★ **用例 fixture 模板**：`CaptionFixture` 形态——`Application&` 注入 → 构造时 `a.Create(title, w, h)`（★ **第 4 参 `RenderServices` 有默认值**，`Application.h:70-71`）→ `Handle()` **三跳**取 HWND → 析构 `window->Release()`（★ 只销毁 HWND，对象归 `Application`） | `src/Tests/CaptionBarTests.cpp:86-125`（`Handle()` = `:118-123`） |

### 1.5 ★ 外部评审处置（详设 v1.0 → v1.1）

★ **评审结论**：**通过，可以进入 Implementation**——「没有需要退回修改的架构问题」；附 **2 个 🟡**，★ **均为「验收 / 测试覆盖的表述口径」问题，不是实现问题**。★★ 评审明确：**不建议为了它们**「专门给测试系统增加 `WM_SIZE` 计数器」——「没有必要为了证明 Win32 `SetWindowPos` 的系统行为而增加测试基础设施」。

| # | 评审意见 | 处置 |
|---|---|---|
| **R-①** 🟡 | ★★ **C3 的「不派发 `WM_SIZE`」与 T22-2 的实际证明力不匹配**——T22-2 只比较前后几何，证明的是「**几何结果幂等**」，**没有直接观察 `WM_SIZE` 是否发生**。⇒ 建议把 C3 的文字**分成两层**：「**自动化测试**（T22-2 ⇒ 几何幂等）」与「**已有实测**（同尺寸 `SetWindowPos` 不派发 `WM_SIZE`）」——★ 不要让人读成「T22-2 自动证明了不派发 `WM_SIZE`」 | ✅ **采纳**——**C3 拆两层**（见 §3）；★ **T22-2 的用例注释**同步改写（见 △7）；★ **§3 表末与 §4.1 的措辞**一并校正。★★ **边界如实声明**：「不派发 `WM_SIZE`」**只有需求阶段 §1.7 的实测证据（差异归因法）**，**本阶段的自动化判据不覆盖它**（★ 且**刻意不加**计数器——评审认可该取舍） |
| **R-②** 🟡 | ★ **A3 的「三工具链 264 / 264」口径模糊**——「三工具链」指哪三个、第四个什么状态，读者无法确定；★ 若作为**最终验收记录**，应写成**四工具链各自的实际结果**，或明写「当前三工具链已验证，MSVC 待用户侧验证」 | ✅ **采纳**——**A3 改为「四工具链各自逐项记录」**（MSVC / ClangCL / Clang / MinGW 各一格）；★ **A2 同步**（它同样写「四工具链」却指向既有 261 —— 现明确为「**四链既有 261**」）；★ **§7 实施顺序的第 3 步判据**同步改为四链逐项。★★ 落到**实施记录**时**按实际结果填**（★ 不预写「全绿」） |

---

## 2. 逐文件改动（△1–△9）

### △1 `include/ECDI/Platform/PlatformWindow.h`：+1 纯虚

**位置**：类尾、**`};`（`:145`）之前**，新起 `Phase 22` 分组（沿 `Phase 12` / `Phase 13` / `Phase 14` 的分组注释先例，见 `:83` / `:109` / `:126` / `:133`）。

```cpp
	// ── Phase 22：启动尺寸的落实（`Create` 末尾 + `Show()` 两处调用）────────
	// @pre 调用时机 = `Window` **完全构造之后**（`Application::Create` 内）；
	//      ★ 构造期调用会同步派发 `WM_SIZE` 撞上未构造成员 = UB（见实现的构造处注释）。

	/// @brief 按当前窗口 DPI，把 `Create` 阶段记录的「启动 DIP 总尺寸」落实到物理尺寸
	/// @details 语义：「**`Window` 已完成构造，现按当前平台 DPI 应用 `Create` 阶段记录的 DIP 总尺寸**」。
	///          ★ 本方法**同时**刷新翻译器 DPI —— **两者必须同源**（契约 C9）：尺寸换算用
	///          `DipToPixels(w, dpi)`，翻译器用同一个 `dpi` 把消息坐标折成 DIP；二者取自不同
	///          时刻则「几何」与「命中判定」会按不同 DPI 换算（Phase 22 初设 §2.4）。
	/// @pre 窗口句柄有效（实现内自行守卫）；★ **未设置启动尺寸（`0`）⇒ 直接返回**——★ **刻意**：
	///      「无启动尺寸 ⇒ **连 DPI 也不刷**」是**沿用既有行为**、**零语义变化**（契约 **C10**）。
	///      理由 = 本阶段的对象是「`Create` 的启动几何」，**不是泛化的 DPI 同步**。
	/// @note **幂等**：尺寸已正确 ⇒ 底层 `SetWindowPos` 为 no-op —— ★ **实测连 `WM_SIZE`
	///       都不派发**（需求稿 §1.7），故 `Show()` 里的复核调用**零运行期成本**。
	/// @note **不提供「运行期改窗口尺寸」能力**：尺寸来源唯一（构造期记录，契约 C4）。
	virtual void ApplyStartupSize() = 0;
```

★ **公共头 92 → 92**（不新增头）· **公共 API +1**（★ 源码接口集合 +1；1.0 前不承诺 ABI —— 契约 C7）。

### △2 `src/Platform/Win32/Win32PlatformWindow.h`：+1 override 声明

**位置**：public 区、**Phase 14 分组（`:72-75`）之后**，新起 `Phase 22` 分组。

```cpp
	// ── Phase 22：启动尺寸落实（`Create` 末尾 + `Show()` 两处调用）──────────

	/// @brief 按当前窗口 DPI 落实启动尺寸 —— ★ 含翻译器 DPI 同步（契约 C9；详见基类契约）
	void ApplyStartupSize() override;
```

★ **成员零新增** —— `m_startupWidthDip` / `m_startupHeightDip` **已存在**（`:208-212`）；本方法**不改任何状态**（纯读 + 调平台 API）。

### △3 `src/Platform/Win32/Win32PlatformWindow.cpp`：+1 方法 ＋ `Show()` 瘦身

**方法体**（★ 沿用本文件的**隔行空行**风格，初设 §1.3）：

```cpp
void Win32PlatformWindow::ApplyStartupSize() {

	// ★★ Phase 22：本方法由「原 Show() 里的换算块」**原样搬来**（零新增语义）。
	// 搬家的理由：让同一段逻辑能在**两个时机**被调用——
	//   ① `Application::Create` 末尾（让 DIP 契约在 `Create` 返回时即成立——本阶段的目标）；
	//   ② `Show()`（保留跨屏复核——Phase 20 的既有理由：「Show 时窗口的显示器关联才确定」）。
	// ⚠️ DPI 与尺寸**必须同源**：`SetDpi` 与 `DipToPixels` 用同一个 dpi，
	//    否则「几何」与「命中判定」会按不同 DPI 换算。
	// ★ **早退语义（契约 C10）**：无启动尺寸（`<= 0`）⇒ **整方法早退**，**连 `SetDpi` 也不执行**
	//   —— ★ **刻意沿用既有行为**（本方法只管「启动几何」，不做泛化 DPI 同步）。

	if (m_startupWidthDip <= 0 || m_startupHeightDip <= 0) {

		return;

	}

	const int dpi = GetDpiForWindow(m_hwnd);

	m_messageHandler.SetDpi(dpi);

	SetWindowPos(m_hwnd, nullptr, 0, 0,
		DipToPixels(m_startupWidthDip, dpi), DipToPixels(m_startupHeightDip, dpi),
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// ★ dpi == 96 时尺寸不变 ⇒ SetWindowPos 为 no-op（G5 恒真）——★ 实测届时连 WM_SIZE 都不派发。

}
```

**`Show()` 的改动**（★ **只把换算块换成一行**，其余逐字不动；见 B15 —— 块级 `if` 改为调用，**后续三句仍在**）：

```cpp
void Win32PlatformWindow::Show() {

	if (m_hwnd != nullptr) {

		// ★★ Phase 20（△10 落点迁移 + 崩溃修复）：按窗口 DPI 把客户区定到「DIP 语义」对应的**物理尺寸**。
		// ...（原注释保留：搬家理由、消息时序、不闪烁、G5 恒真——逐字不动）
		// ★★ Phase 22：**尺寸落实本身已由 `Application::Create` 末尾执行**（契约在 `Create`
		//   返回时即成立）；此处保留调用是**跨屏复核**——同尺寸时为 no-op（实测不派发 WM_SIZE）
		//   ⇒ **零运行期成本**（契约 C6）。
		ApplyStartupSize();

		m_shown = true;   // 配置期 → 运行期分界线（与 Window::Show() 一一对应）

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);

	}

}
```

★ **可验证的等价**：原 `:182-192` 的 `if (m_startupWidthDip > 0 && m_startupHeightDip > 0) { … }` 与「`ApplyStartupSize()` 内 `<= 0 ⇒ return`」**逻辑互补**（De Morgan）⇒ 行为**逐位等价**。

### △4 `src/Application/Application.cpp`：**+1 include** ＋ **+1 调用**

**(a) include**（★ 沿 B13 的 `TextBox.cpp:14` 先例，插在 `#include "ECDI/Window/Window.h"`（`:4`）之后）：

```cpp
#include "ECDI/Platform/PlatformWindow.h"   // Phase 22：调用 ApplyStartupSize（Window.h:24 仅前置声明）
```

**(b) 调用**（插在 `Window& window = *m_windows.back();`（`:73`）之后、`WindowCreatedEvent` 之前）：

```cpp
	// ★★ Phase 22：把构造期记录的 DIP 目标尺寸**按当前 DPI 落实**。
	// 为什么在这里：此刻 `Window` **已完全构造**（成员就绪 ⇒ 回调安全，避开 Phase 20 的构造期 UB），
	//   且**本函数尚未返回** ⇒ 「公共 API 语义恒为 DIP」在 `Create` 返回时即成立。
	// 为什么在事件**之前**：`WindowCreatedEvent` 的监听者若读窗口几何，应当看到**已成立**的值。
	// 幂等：`Show()` 会再调一次（跨屏复核），届时为 no-op（契约 C3 / C6）。
	window.GetPlatformWindow().ApplyStartupSize();
```

★ **注意**：此处调用时，翻译器 DPI **已由构造期设过**（B14，同一窗口、同一时刻）⇒ 本处的 `SetDpi` **通常与构造期同值**（冗余但无害）——★ 保留它是**为了 C9 的同源性**（不能让"设 DPI"与"算尺寸"分家）；★ 而真正**可能改变 DPI** 的是 `Show()` 那一次（窗口的显示器关联此时才确定）。

### △5 / △6 两个测试替身：各 +1 空实现

**位置**：各自 **Phase 14 分组之后、成员区之前**（B16）。

```cpp
	// ── Phase 22：新增 1 个纯虚（本替身不关心启动尺寸——空实现）──

	void ApplyStartupSize() override{}
```

| 文件 | 落点 | 备注 |
|---|---|---|
| `src/Tests/AnimationTests.cpp` | `SetFileDropEnabled(bool) override{}`（`:89`）之后 | 该替身**无分组注释先例**（Phase 12/13 方法未分组）⇒ 本组**新起**注释 |
| `src/Tests/ProgressBarTests.cpp` | `SetFileDropEnabled(bool) override{}`（`:80`）之后 | ★ 该文件**已有**分组注释先例（`:58` / `:76`）⇒ 风格一致 |

★ **替身语义 = 空实现** —— 与既有 22 个纯虚里 `Show() override{}` / `Hide() override{}` **同款**。
★ **如实说明**：替身**观测不到**这个调用的事实本身（`Application::Create` 造的是真的 `Win32PlatformWindow`，测试无法把替身注入该入口）——★ 它们的改动**纯粹是接口实现义务**，**不为测试服务**（即：**不为本阶段引入依赖注入**）。

### △7 ★ 新建 `src/Tests/PreshowGeometryTests.cpp`

**文件骨架**（沿 `CaptionBarTests.cpp` 的形态与 `DpiTests.cpp` 的内部头 include 先例）：

```cpp
#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Window/Window.h"
#include "Platform/Win32/DpiConversion.h"      // PixelsToDip：外框 DIP 换算（唯一真相源）
#include "Platform/Win32/Win32RenderContext.h" // 三跳取 HWND

#include <string>

using namespace ECDI;

namespace {

// ══════════════════════════════════════════════════════════════════
// Phase 22：`Create` 的 DIP 尺寸契约（pre-show DIP geometry）—— T22-1..T22-3
// ★ 本组**必须**用真实窗口（`Application::Create` ⇒ `Win32PlatformWindow`）——
//   被测行为发生在「`Create` 返回」这一时刻，任何替身都到达不了该入口。
// ══════════════════════════════════════════════════════════════════

constexpr int kWinW = 800;
constexpr int kWinH = 600;

/// @brief 最小 Application（无事件观测需求——本组判据全在几何上）
struct TestApp : public Application {};

/// @brief 场景守卫（`CaptionFixture` 同款：本对象必须先于 Application 析构）
struct GeometryFixture {

	Window* window = nullptr;

	explicit GeometryFixture(Application& a) {

		window = &a.Create("ECDI_PreshowGeometry", kWinW, kWinH);   // ★ 第 4 参用默认值

	}

	~GeometryFixture() {

		window->Release();   // 只销毁 HWND（WM_DESTROY → Application 回收 Window 对象）

	}

	/// @brief 取底层 HWND（三跳——`CaptionBarTests.cpp:118-123` 同款）
	HWND Handle() const {

		return static_cast<const Win32RenderContext&>(
			window->GetPlatformWindow().GetRenderContext()).GetHandle();

	}

};

/// @brief 外框尺寸的 **DIP**（契约 C1 的判据量 = 「窗口总尺寸（含边框和标题栏）」）
/// @details ★ 必须用 `GetWindowRect`（外框）而不是 `GetClientSize`（客户区）——
///          契约说的是**总尺寸**（`Window.h:236-237`）。
Size OuterSizeDip(HWND hwnd) {

	RECT r{};

	GetWindowRect(hwnd, &r);

	const int dpi = GetDpiForWindow(hwnd);

	return Size{ static_cast<float>(PixelsToDip(r.right - r.left, dpi)),
	             static_cast<float>(PixelsToDip(r.bottom - r.top, dpi)) };

}

} // namespace
```

**三个用例**：

```cpp
// ── T22-1 ★★ 时序稳定性契约：`Create` 后的几何 == `Show` 后的几何 ─────────────
// ★ 职责（评审 §6 采纳）：本用例**不**直接证明「`Create` 后已等于请求尺寸」，
//   它证明的是「**`Create` 与 `Show` 之间不发生几何变化**」。
// ★ 失败归因：**T22-1 失败 ⇒ 判「Create / Show 时序不稳定」**。
// ★ 环境行为：96 DPI 下两侧本就相等 ⇒ **恒真**（不误报）；非 96 DPI 下
//   改前 `Create` 后是 640×480 DIP、`Show` 后 800×600 ⇒ **必红**。
void Test22GeometryStableAcrossShow()
{
	TestApp app;
	GeometryFixture fx(app);

	const Size afterCreate = OuterSizeDip(fx.Handle());   // ★ Create 已返回（含 Phase 22 的落实）

	fx.window->Show();

	const Size afterShow = OuterSizeDip(fx.Handle());

	EXPECT_EQ(afterCreate.width, afterShow.width);
	EXPECT_EQ(afterCreate.height, afterShow.height);
}

// ── T22-2 幂等（★ platform seam contract test——非公共 API 行为测试）─────────
// ★ 定位（评审 §7 采纳）：本用例经 `Window::GetPlatformWindow()` **直调平台接口**，
//   属**接缝契约测试**，验的就是 `ApplyStartupSize()` 的**幂等性**（契约 C3）。
// ★★ **证明边界（详设 v1.1 / 评审 R-① 采纳）**：本用例证明的是「**几何结果幂等**」
//   ——连续两次调用前后尺寸逐项相等。★ 它**不直接观察 `WM_SIZE` 是否派发**；
//   「同尺寸 `SetWindowPos` 不派发 `WM_SIZE`」的证据在**需求稿 §1.7 的实测**（非本用例）。
void Test22ApplyStartupSizeIdempotent()
{
	TestApp app;
	GeometryFixture fx(app);

	const Size before = OuterSizeDip(fx.Handle());

	fx.window->GetPlatformWindow().ApplyStartupSize();

	const Size after1 = OuterSizeDip(fx.Handle());

	fx.window->GetPlatformWindow().ApplyStartupSize();

	const Size after2 = OuterSizeDip(fx.Handle());

	EXPECT_EQ(before.width, after1.width);
	EXPECT_EQ(after1.width, after2.width);
	EXPECT_EQ(after1.height, after2.height);
}

// ── T22-3 直接 DIP 契约（C1 的直接判据——条件式）───────────────────────────
// ★ 职责（评审 §6 采纳）：**直接**断言「`Create` 后外框 DIP == 请求值」。
// ★ 失败归因：**T22-3 失败 ⇒ 判「`Create` 阶段根本没有正确落实 DIP」**。
// ★ 条件式：96 DPI 机器上跳过（用例仍通过；契约由 G5 恒等保证——见 C2 的两层口径）。
void Test22CreateGeometryMatchesRequestAtNon96Dpi()
{
	TestApp app;
	GeometryFixture fx(app);

	const HWND hwnd = fx.Handle();

	if (GetDpiForWindow(hwnd) != 96) {

		const Size dip = OuterSizeDip(hwnd);

		EXPECT_EQ(dip.width, static_cast<float>(kWinW));
		EXPECT_EQ(dip.height, static_cast<float>(kWinH));

	}

}

void RegisterPreshowGeometryTests()
{
	GetTestRegistry().Add("PreshowGeometry.GeometryStableAcrossShow", Test22GeometryStableAcrossShow);
	GetTestRegistry().Add("PreshowGeometry.ApplyStartupSizeIdempotent", Test22ApplyStartupSizeIdempotent);
	GetTestRegistry().Add("PreshowGeometry.CreateGeometryMatchesRequestAtNon96Dpi", Test22CreateGeometryMatchesRequestAtNon96Dpi);
}
```

★ **`Show()` 的副作用说明**：T22-1 会真正显示窗口（`ShowWindow`）——★ 与 `CaptionBarTests` 的 T13-1 同款（那里也是先 `Show()` 再断言命中）；测试结束时 fixture 析构 `Release()` 销毁 HWND。
★ **不用 `EXPECT_NEAR`**：几何是整数量折 DIP（`PixelsToDip` 结果为 `int`）⇒ `EXPECT_EQ` 精确断言即可。

### △8 / △9 登记（`RunAllTests.h` / `.cpp` 各 +1）

```cpp
// RunAllTests.h（追加到声明区末尾，Phase 21 之后）
void RegisterPreshowGeometryTests();   ///< Phase 22：Create 的 DIP 尺寸契约（T22-1..T22-3）
```

```cpp
// RunAllTests.cpp（追加到调用区末尾）
    RegisterPreshowGeometryTests();   // Phase 22：Create 的 DIP 尺寸契约（T22-1..T22-3）
```

---

## 3. 契约 C1–C10 → 实现落点 → 测试

| # | 契约 | 实现落点 | 测试 / 判据 |
|---|---|---|---|
| **C1** ★★ | `Create` 返回后外框 DIP == 请求 `w × h` | △4 的调用 + △3 的方法体 | **T22-3**（非 96 DPI 直接断言）；96 DPI 由 **C2** 兜底 |
| **C2** ★ | 96 DPI 零回归（① 数学保证 G5 恒等 ＋ ② 行为验收） | △3（`DipToPixels(w, 96) == w` ⇒ no-op） | ★ **两层**：① `DpiTests` 的 G5 恒等用例；② **既有 261 全绿**（四工具链） |
| **C3** ★ | **幂等**（`Create` + `Show()` 各一次；尺寸已对 ⇒ **尺寸不变**、**系统层面不派发 `WM_SIZE`**） | △3（`SetWindowPos` 参数相同 ⇒ 系统 no-op）· △4 + `Show()` 两处调用 | ★★ **两层（评审 R-① 采纳）**：**① 自动化判据** = **T22-2** ⇒ 证明「**几何结果幂等**」（连续两次调用前后尺寸逐项相等）；★ **T22-2 不直接观察 `WM_SIZE`**；**② 实测证据** = 需求稿 §1.7 的`WM_SIZE` 计数（**差异归因法**：mode C 比基线少一次，差值归因于同尺寸 `SetWindowPos`）——★ **「不派发 `WM_SIZE`」只有该实测支撑，本阶段自动化不覆盖**（★ 且**刻意不加计数器**） |
| **C4** ★ | 尺寸来源唯一（方法**无参**；不提供运行期改尺寸） | △1 签名（无参） | 方法签名（编译期） |
| **C5** ★★ | 不引入 UB：落实点必须在 `Window` **完全构造之后** | △4 落点（`:73` 之后） | ★ 结构性：`Create` 内 ⇒ 构造已完成；★ 125% 实测不崩（需求稿 R3） |
| **C6** ★ | `Show()` 保留复核，且尺寸已对时为 **no-op** | △3 的 `Show()` 改动 | **T22-1**（`Show` 前后几何相等 ⇒ 复核未引入变化） |
| **C7** | 公共头 92 → 92；公共 API **+1** | △1 | 头文件计数（★ 口径：源码接口集合；1.0 前不承诺 ABI） |
| **C8** ★ | DPI 语义 = 「当前 Win32 窗口状态所能获得的 DPI」，不承诺等于最终显示器 DPI | △3（`GetDpiForWindow`） | 文档口径；★ 跨屏场景本机不可测（L2） |
| **C9** ★★ | DPI 与尺寸**同源**（`SetDpi` 与 `DipToPixels` 用同一个 `dpi`） | △3 方法体（同一局部量 `dpi`） | ★ **代码审阅**（★ 无自动化判据——见 §8 的 L1） |
| **C10** ★ | 无启动尺寸 ⇒ 整方法早退，**`SetDpi` 也不执行** | △3 的 `if (… <= 0) return;` **在 `SetDpi` 之前** | ★ **字符级判据**：`return` 的出现位置早于 `SetDpi`（盯防项，见 §4） |

---

## 4. 关键行为冻结与盯防清单

### 4.1 调用序列（全链）

```
【时机 A：契约建立】Application::Create（△4）
   ├─ m_windows.emplace_back(unique_ptr(new Window(...)))     B6 :71-72  ← Window 完全构造
   │     └─ 构造期已 SetDpi（B14 :148）                       ← 此刻翻译器已有 DPI
   ├─ Window& window = *m_windows.back();                     :73
   ├─ ★ window.GetPlatformWindow().ApplyStartupSize();        ★ Phase 22 新增
   │     └─ 取 dpi → SetDpi（同值）→ SetWindowPos（DIP → 物理）
   ├─ WindowCreatedEvent event(&window);  OnEvent(event);      B6 :75-78  ← 监听者看到「已成立」的几何
   └─ return window;                                          :81  ← ★ C1 的判据点

【时机 B：跨屏复核】Win32PlatformWindow::Show()（△3）
   ├─ ApplyStartupSize();                                     ← 替代原换算块（B15 :182-192）
   ├─ m_shown = true;  ShowWindow(...);  UpdateWindow(...);   B15 :194-198（★ 逐字不动）
   └─ ★ 实测（需求稿 §1.7）：尺寸已对 ⇒ no-op ⇒ 不派发 WM_SIZE（★ **该结论来自实测，非自动化判据**）
```

### 4.2 ★ 盯防清单（实施后逐条机检）

| # | 盯防项 | 判据 |
|---|---|---|
| **①** | **早退在 `SetDpi` 之前**（C10） | 方法体内 `if (… <= 0)` 的 `return` **行号 <** `m_messageHandler.SetDpi` **行号** |
| **②** | **`Show()` 的四句仍在** | `Show()` 内 `ApplyStartupSize` / `m_shown = true` / `ShowWindow` / `UpdateWindow` **各 1 处** |
| **③** | **DPI 同源**（C9） | 方法体内 `m_messageHandler.SetDpi(dpi)` 与 `DipToPixels(…, dpi)` **共用同一局部量**（字符级：`dpi` 出现且无第二处 `GetDpiForWindow`） |
| **④** | `GetDpiForWindow` 在本方法内**只 1 处** | 计数 = 1 |
| **⑤** | `SetWindowPos` 在本方法内**只 1 处**且带 `SWP_NOMOVE \| SWP_NOZORDER \| SWP_NOACTIVATE` | 计数 + 标志串 |
| **⑥** | **`Application.cpp` 的 include** | 含 `#include "ECDI/Platform/PlatformWindow.h"`（1 处） |
| **⑦** | **调用在事件之前** | `ApplyStartupSize` 行号 **<** `WindowCreatedEvent event(&window);` 行号 |
| **⑧** | **公共头 API 计数** | `PlatformWindow.h` 的 `virtual` 纯虚数 = **23**（22 + 1）；`include/ECDI/**` 下 Win32 类型**代码行 = 0**（C1 不变的既有契约） |
| **⑨** | **围栏/行尾/BOM** | 新增测试文件 **BOM + CRLF**；两源码文件沿用其既有行尾 |
| **⑩** | **登记齐全** | `RunAllTests.h` / `.cpp` 各含 `RegisterPreshowGeometryTests` **1 处**；`GetTestRegistry().Add` 计数 = **6/ 文件**（本文件 3 条） |

★ **本阶段零新增 `FRAMEWORK_ASSERT`**（无新的前置条件需要断言；"尺寸为 0" 的既有权衡保持「直接返回」—— ★ 与 Phase 16 的 D-5 同款判断）。

---

## 5. 验收（A1–A6）

| # | 验收项 | 判据 |
|---|---|---|
| **A1** ★★ | **契约成立**（C1） | 本机 125% 下：`Create` 后外框 DIP = **800×600**（改前为 640×480）——★ 由 **T22-3** 自动断言 |
| **A2** ★★ | **零回归**（C2 ②） | ★ **四链既有 261 全绿，逐链记录**（`MSVC` / `ClangCL` / `Clang` / `MinGW` 各一格）；★ **96 DPI 下行为逐位一致**（G5 恒等）⇒ ★★ **已填，见 §5.1** |
| **A3** ★ | **新增用例通过**（★ 评审 R-② 采纳：**四链逐项记录，不写模糊的「三工具链」**） | ★ **按实际结果填，不预写「全绿」**——★★ **已填，见 §5.1**：**四链各 264 / 264**（`MSVC` / `Clang` / `MinGW` / `ClangCL`；261 + 3；@DPI 120 · **断言启用**）—— ★ `MSVC` 由**用户侧跑通**。★ **不留「三工具链 + MSVC 待确认」的长期模糊状态** —— ★ 该状态已由 §5.1 的**四链表格**显式承载（★ **待办已于 2026-09-29 结清**） |
| **A4** ★ | **不引入 UB**（C5） | 125% 下 `Create` 不崩；★ 结构性（落点在 `Create` 内） |
| **A5** | **规模口径** | 公共头 **92 → 92** · API **+1** · 断言特征串 **11（不变）** · 用例 **261 → 264** |
| **A6** ★ | **移动端验收环境** | ★ 本机 **DPI 120（125%）** ⇒ A1 的判据**实际会跑到**；★ 若临时切回 100%，A1 由 T22-1 的"两侧相等"承担（★ 两者都通过 ⇒ 双保险） |

### 5.1 A1–A6 实测判定（2026-09-29 · 单批实施后）

★ **运行环境**：本机 **1920×1080 @ DPI 120（125%）** ⇒ ★ A1 的判据**真的跑到**（T22-3 的非 96 DPI 分支**实际生效**，不是空跑）。
★ **断言状态**：**四链均已启用** —— `MSVC` = `/MDd` · `Clang` = `-D_DEBUG` · `MinGW` = `-D_DEBUG` · `ClangCL` = `-D_DEBUG` + `/MDd`（★ 判据按工具链二分：MSVC 系查 `/MDd`、GNU 系查 `-D_DEBUG`）。

| # | 验收项 | 判定 | 实测 |
|---|---|---|---|
| **A1** ★★ | 契约成立（C1） | ✅ **通过** | 125% 下 `Create` 后外框 DIP = **800×600**（★ 改前为 **640×480**）—— 由 **T22-3** 自动断言 |
| **A2** ★★ | 零回归（C2 ②） | ✅ **通过（四链）** | 见下方四链表 |
| **A3** ★ | 新增用例通过 | ✅ **通过（四链）** | **264 = 261 + 3**，见下方四链表 |
| **A4** ★ | 不引入 UB（C5） | ✅ **通过** | 125% 下 `Create` 不崩（四链测试全程无异常退出 / 无 `0xC000041D`） |
| **A5** | 规模口径 | ✅ **符合** | 公共头 **92 → 92** · API **+1** · 断言特征串 **11（不变）** · 用例 **261 → 264** |
| **A6** ★ | 移动端验收环境 | ✅ **符合** | 本机 DPI 120 ⇒ A1 判据真的跑到（★ 与 T22-1 的"两侧相等"**双保险**均成立） |

**四链逐项记录（用例总数 264 = 261 + 3）**：

| 工具链 | 结果 | 备注 |
|---|---|---|
| **MSVC** | ✅ **264 / 264** | `/MDd`（断言启用）—— ★ **由用户侧在 VS / CLion 手工构建跑通**（2026-09-29）；★ 该链跑通后 **A2 / A3 由「三链」升为「四链」** |
| **ClangCL** | ✅ **264 / 264** | `-D_DEBUG` + `/MDd`（断言启用） |
| **Clang** | ✅ **264 / 264** | `-D_DEBUG`（断言启用） |
| **MinGW** | ✅ **264 / 264** | `-D_DEBUG`（断言启用）—— ★ **首次运行出现 1 项失败，判定为既有的时间敏感用例、非本阶段回归**（见 **§7.1 的 O-1**），连续 3 次复跑全绿 |

---

## 6. 影响面

| 项 | 内容 |
|---|---|
| **改动的文件** | **9 个**（**生产 4** ＋ **测试 5**，★ 新建 **1**）—— 见 §1.2 的 △1–△9；★ 初设的"6 个"**未计测试承载与登记**（§1.3 的 D-1） |
| **零改动** | `Window.h` / `Window.cpp` · `Application.h` · `PlatformWindowHost.h` · 渲染侧 · `CMakeLists.txt` · `main.cpp` |
| **公共头 / API** | 头 **92 → 92** · API **+1** |
| **测试** | **+3**（T22-1..T22-3）⇒ **261 → 264** |
| **断言特征串** | **不变（11 条）** |
| **文档回写** | 实施后回写本稿（实施记录——★ **含 A2 / A3 的四链逐项实际结果**）＋ `docs/README.md` 的 Phase22 段 ＋ 审计 `D-7` 状态 ＋ `roadmap-deferred` §7.9 待做 ⑦ |

---

## 7. 实施顺序（**单批**）

★ **为什么不必分批**（与 Phase 21 的"两批"不同）：本阶段的**生产改动与测试改动之间没有链接级依赖** —— 新增的纯虚方法**自带实现**（△2/△3），测试只是**消费**它；★ 而 Phase 21 的 △5（登记）依赖 `RegisterIconDecodeTests()` 的定义（那才有批次约束）。

| 步 | 动作 | 检查点 |
|---|---|---|
| **1** | △1 → △2 → △3 → △4（**生产件**） | 构建通过 ⇒ **既有 261 全绿**（★ 此时新方法**无人调用**前先单独验证更稳；但 △4 已调用 ⇒ 直接看下一步） |
| **2** | △5 → △6（替身）—— ★ **必须与 △1 同批**（新增纯虚 ⇒ 不同步则**编译失败**） | 编译通过 |
| **3** | △7 → △8 → △9（测试与登记） | **264 全绿** + ★ **四链逐项记录**（见 A3） |

★ ★ **注意**：△1（加纯虚）与 △5/△6（替身补实现）**必须同批**（否则中间态**编译不过**）—— ★ 与 Phase 21 的 △5 教训**同源**：**「接口变更」与「其全部实现者」之间存在编译级依赖**。

### 7.1 实施记录（2026-09-29 · 单批）

**规模实测（计划 vs 实际）**：

| 项 | 计划 | 实测 |
|---|---|---|
| 改动文件 | **9**（生产 4 + 测试 5，新建 1） | ✅ **9** —— 与 △1–△9 **逐条吻合**（8 改 + 1 新建） |
| 代码行 | — | 8 个文件 **+75 / −11**；新建 `PreshowGeometryTests.cpp` **153 行** |
| 用例 | 261 → 264 | ✅ **264**（四链一致） |
| 公共头 / API | 92 → 92 · +1 | ✅ 92 → 92 · +1 |

**实施观察（O-1..O-3）**：

- **O-1 ★ 一处既有的时间敏感用例（判定为 flaky、非本阶段回归）**：`MinGW` 链**首次运行**报 **1 项失败** —— `DesktopLayerTests.cpp:538` 的 `GetWindow(desktop, GW_HWNDPREV) == hwnd`（**Phase 16 桌面 z 序跟随**用例，与本阶段**零逻辑关联**：本阶段只改 `Create` 的尺寸落实，且 `SetWindowPos` 带 `SWP_NOZORDER`）。★ **判定依据**：① 失败点**不在本阶段改动的任何文件内**；② **连续 3 次复跑全绿**；③ 该用例用**固定时间预算**（`PumpMessagesFor(300)`）断言**真实桌面 z 序**，而首次运行时机器刚经历**三链并发构建** ⇒ **超时性 flaky**。★ **如实记录、不掩盖**：`MinGW` 的"全绿"结论以**稳定复跑**为准，而非单次读数。
- **O-2 ★ 「接口变更 ↔ 全部实现者」的编译级依赖确实成立**：实施按 §7 的顺序（△1–△4 生产 → △5/△6 替身 → △7–△9 测试与登记）一次走完 ⇒ **中间态未单独编译**；★ 但本机三链**全量重编一次通过、零编译错误**（★ **`MSVC` 由用户侧单独构建，同样零错误、264 全绿**），间接印证「△1 与 △5/△6 **必须同批**」的判断。
- **O-3 无设计偏离**：△1–△9 的**落点 / 内容 / 注释**与 §2 一致（★ §4.2 盯防 10 条**全部通过**，见下表）；★ `Show()` 侧**只动了换算块那一处**（B15 的后续三句 `m_shown` / `ShowWindow` / `UpdateWindow` **逐字未动**）。
- **O-4 ★ 实施后「行号引用」位移 —— skill 条 114 的引用行号自检结果**：本阶段在 `Win32PlatformWindow.cpp` 插入 **32 行**（净 **+21**）⇒ ★ **本稿 B11–B18 / 初设 B1–B10 / 需求 K1–K8 中位于插入点之后的 `:行号` 全部位移**。★★ **处理原则 = 「快照不重写」**：这些**基线**与**现状勘察**记录的本就是**实施前的代码状态**（那正是"基线"的含义）⇒ **刻意不重写**（改了反而是篡改历史），改以本表登记映射：

| 文档内的引用 | 实施前 | **实施后** | 说明 |
|---|---|---|---|
| `Win32PlatformWindow.cpp:115-126`（`CreateWindowExW` 物理直传） | 115-126 | **115-126** | ★ **未位移**（在插入点**之前**）⇒ 引用仍准确 |
| `Win32PlatformWindow.cpp:145-148`（构造期 `SetDpi`） | 145-148 | **145-148** | ★ **未位移**（同上） |
| `Win32PlatformWindow.cpp:171-202`（`Show()` **全函数**） | 171-202 | ★ **199-223**（`Show()` 新范围） | ★ 换算块**已迁出**该函数 |
| `Win32PlatformWindow.cpp:182-192`（**换算块**） | 182-192 | ★ **171-197**（**方法体** `ApplyStartupSize()`）＋ **213**（`Show()` 内的**调用点**） | 本次「搬家」的直接结果 |
| `Win32PlatformWindow.cpp:181`（`Show()` 换算块内的注释行） | 181 | **199-223 区内** | 同上 |
| `Win32PlatformWindow.cpp:977`（**审计 `D-2`** 的证据行） | 977 | **998**（净 +21）· ★★ **且该引用早已腐坏**——所谓「`GetDeviceCaps` → `GetDpiForWindow` 的预留注释」**已随 Phase 20 实施被消费**（★ 实测：`GetDeviceCaps` 在该文件中**零出现**）⇒ **已在审计 v1.17 更正为「已消费」** | 既腐坏 + 位移 |

★ **由此得到的纪律（可推广）**：**基线与「当前状态」是两回事** —— 引用行号自检发现位移时，**先判「这条引用描述的是哪个时点的代码」**：① 描述**历史快照**（基线 / 现状勘察 / 修订记录）⇒ **只登记位移，不重写**；② 描述**当前实现**（概述性叙述）⇒ **必须更新**。★ 本阶段第 ② 类共 **1 处**（`docs/README.md` 的 Phase22 段概述），已更新。

**盯防清单 10 条机检结果（§4.2）**：

| # | 盯防项 | 结果 |
|---|---|---|
| **①** | 早退在 `SetDpi` 之前（C10） | ✅ 通过 —— `return`@**175** < `m_messageHandler.SetDpi`@**181** |
| **②** | `Show()` 四句仍在、各 1 处 | ✅ 通过 —— `ApplyStartupSize()` / `m_shown = true` / `ShowWindow` / `UpdateWindow` **各 1 处**且顺序不变 |
| **③** | DPI 同源（C9） | ✅ 通过 —— `SetDpi(dpi)` 与 `DipToPixels(…, dpi)` **共用同一局部量** |
| **④** | `GetDpiForWindow` 在本方法内只 1 处 | ✅ 通过 —— 计数 = **1** |
| **⑤** | `SetWindowPos` 只 1 处且带三标志 | ✅ 通过 —— 代码行 **1 处** · `SWP_NOMOVE \| SWP_NOZORDER \| SWP_NOACTIVATE` |
| **⑥** | `Application.cpp` 的 include | ✅ 通过 —— `#include "ECDI/Platform/PlatformWindow.h"` **1 处** |
| **⑦** | 调用在事件之前 | ✅ 通过 —— 调用@**81** < `WindowCreatedEvent event(&window)`@**84** |
| **⑧** | 公共头 API 计数 + 零 Win32 类型 | ✅ 通过 —— 纯虚数 = **23**（22 + 1）；`PlatformWindow.h` 含 Win32 类型的**代码行 = 0** |
| **⑨** | 围栏 / 行尾 / BOM | ✅ 通过 —— **9 文件 BOM ✓**；**裸 LF = 0**（全部 CRLF，与各自既有风格一致） |
| **⑩** | 登记齐全 | ✅ 通过 —— `RunAllTests.h` **1** · `RunAllTests.cpp` **1** · 本测试文件 `GetTestRegistry().Add` = **3** |

★ **本阶段零新增 `FRAMEWORK_ASSERT`**（如实符合 §4.2 末的说明）。

---

## 8. 局限（L1–L4）

| # | 局限 | 说明 |
|---|---|---|
| **L1** ★ | **C9（DPI 与尺寸同源）无自动化判据** | 它约束的是"两个值取自同一时刻"——★ 只能**代码审阅**（✓ 已在 §4.2 盯防 ③ 立了字符级判据）。★ 如实标注：**该判据失效时测试不会红** |
| **L2** ★ | **跨屏（Q1）本机不可测** | 单显示器环境无法验证"`Create` 期 DPI ≠ 最终显示器 DPI"的场景；★ 缓解 = `Show()` 的复核（时机 B）——★ 该场景下 `Show()` 会重新取 DPI 并重新落实 |
| **L3** | **96 DPI 机器上 A1 不判** | T22-3 是条件式（`dpi != 96` 才断言）；★ 但 T22-1 仍在（两侧相等，恒真）⇒ ★ 无虚假信心：**两种机器都能跑，判据强度随环境自适应** |
| **L4** | 测试会真正 `Show()` 窗口 | T22-1 需 `Show()` 才能取 Show 后读数（★ 与 `CaptionBarTests` T13-1 同款）；★ 不引入闪烁问题（测试进程一次性） |

---

## 9. 修订记录

- **v1.3**（2026-09-29）**MSVC 链回填 ⇒ 四链齐备（A2 / A3 结清）**。① ★★ **`MSVC` 由用户侧跑通：264 / 264 全绿**（`/MDd` ⇒ **断言启用**）⇒ ★ **A2 / A3 的判定由「三链」升为「四链」**，§5.1 的四链表**四格全部落地**。② ★ **同步回填**：**§5 的 A3 行**（`MSVC`：待用户侧跑通 → **由用户侧跑通 264 / 264**）· **§5.1 的断言状态行**（三链 → **四链**）· **§5.1 的 A2 / A3 判定**（通过（三链）→ **通过（四链）**）· **§5.1 的 MSVC 表格行**（⏳ → **✅ 264 / 264**）· **头部状态行与标题版本号**。③ ★ **结构零变动**：契约 **C1–C10** · 逐文件改动 **△1–△9** · 盯防 **10 条** · 用例数 **264** · 规模口径（头 92→92 / API +1 / 断言 11）**全部不变** —— 本版**只回填一条工具链的实测结果**。
- **v1.2**（2026-09-29）**单批实施完成（△1–△9 · 9 文件）**。① ★★ **实际结果（按 A3 的口径逐链填，不预写「全绿」）**：`Clang` / `MinGW` / `ClangCL` **三链各 264 / 264**（261 + 3）· @**DPI 120** · **断言启用**（`-D_DEBUG`；`ClangCL` 另带 `/MDd`）；**`MSVC` 待用户侧跑通**（★ 已在 §5.1 显式列出**待办方**，不留模糊状态）。② ★ **新增 §5.1「A1–A6 实测判定」**：A1 ✅（125% 下 `Create` 后外框 DIP = **800×600**，★ 改前 **640×480**）· A2 ✅（三链零回归）· A3 ✅ · A4 ✅ · A5 ✅（头 92→92 / API +1 / 断言 11 / 用例 261→264）· A6 ✅（本机 DPI 120 ⇒ A1 判据真的跑到）。③ ★ **新增 §7.1「实施记录」**：**规模实测**（**9 文件** 与 △1–△9 **逐条吻合** · 8 改 **+75/−11** · 新建 **153 行**）· **盯防 10 条机检全过**（逐条附实际读数）· **实施观察 O-1..O-4**（★ **O-4 = skill 条 114 的「引用行号自检」结果**：本阶段在 `Win32PlatformWindow.cpp` 净插入 **+21 行** ⇒ 基线中位于插入点之后的 `:行号` 位移；★★ **处理原则 = 「快照不重写、叙述必更新」**——**历史快照只登记映射**（附 6 行映射表），**描述当前实现的叙述则更新**（共 1 处））。④ ★★ **O-1 如实记录一处既有 flaky**：`MinGW` **首次运行** 1 项失败于 **Phase 16** 的 `DesktopLayerTests.cpp:538`（**真实桌面 z 序** + **固定时间预算** ⇒ 三链并发构建后的**超时性 flaky**；★ **连续 3 次复跑全绿**）⇒ ★ **判定为非本阶段回归**（失败点不在本阶段任何改动文件内，且本阶段的 `SetWindowPos` 带 `SWP_NOZORDER`），三链"全绿"结论**以稳定复跑为准**。⑤ ★ **结构零变动**：契约 **C1–C10** · 逐文件改动 **△1–△9** · 盯防 **10 条** · 用例数 **3** · 规模口径**全部不变** —— 本版**只加实测与记录**，不改任何设计结论。
- **v1.1**（2026-09-29）**外部评审处置（2 个 🟡 ⇒ 全部处置）**。★ **评审结论**：**通过，可以进入 Implementation**——「没有需要退回修改的架构问题」；★ 2 项**均为「验收 / 测试覆盖的表述口径」**，**不是实现问题**。① ★★ **R-① C3 拆两层**：**C3 的「不派发 `WM_SIZE`」与 T22-2 的实际证明力不匹配**（T22-2 只比几何、**不观察 `WM_SIZE`**）⇒ **C3 改为「① 自动化判据 = T22-2 ⇒ 几何结果幂等」＋「② 实测证据 = 需求稿 §1.7（差异归因法）」**，并**明写「本阶段自动化不覆盖『不派发 `WM_SIZE`』」**；★ 同步校正 **T22-2 的用例注释**（补「证明边界」）、**§4.1 的措辞**、**§3 表末**。★★ **边界如实声明**：评审明确**不建议加 `WM_SIZE` 计数器**（不为证明 Win32 系统行为增加测试基础设施）——本稿**遵从**。② ★ **R-② A3 逐链口径**：原「三工具链 264 / 264」指向不明 ⇒ **A3 改为四链逐项（`MSVC` / `ClangCL` / `Clang` / `MinGW` 各一格）**、**A2 同步**（明写「四链既有 261」）、**§7 第 3 步判据同步**；★ **明写「按实际结果填，不预写全绿」**，**不许留下「三工具链 + MSVC 待确认」的长期模糊状态**。③ **结构零变动**：契约仍 **C1–C10**（**仅 C3 的表述拆层**）· 逐文件改动 **△1–△9** · 盯防清单 **10 条** · 测试用例数 **3** · 规模口径（头 92→92 / API +1 / 断言 11 / 用例 261→264）**全部不变**。
- **v1.0**（2026-09-29）初稿。**输入** = 初设 v1.1（评审通过）＋ ★ **本稿新增的代码基线 B11–B18**（含 ★★ **B11/B12 = `Window.h` 只有前置声明、`Application.cpp` 未 include `PlatformWindow.h`** ⇒ △4 必须 **+1 include**；**B14 = 构造期已 `SetDpi`**；**B15 = `Show()` 是块级 `if` 而非早退**）。**核心**：逐文件改动 **△1–△9**（★ **修正初设的"6 文件" ⇒ 实际 9**——初设未计测试承载与登记，§1.3 的 D-1）· **契约 C1–C10 → 实现落点 → 测试**对照表 · **盯防清单 10 条** · **测试规格 T22-1..T22-3**（含 fixture、`OuterSizeDip` helper、逐条职责与失败归因）· **验收 A1–A6** · **局限 L1–L4** · **单批实施**（★ 判据：生产与测试之间**无链接级依赖**，与 Phase 21 的两批不同）。**待评审。**
