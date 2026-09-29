# Phase 22 · `Create` 的 DIP 尺寸契约（pre-show DIP geometry）—— 初步设计（v1.1）

> 来源：**需求确认 `docs/phase22-preshow-dip-geometry-requirements.md` v1.1 已通过评审**（2026-09-28；评审结论「通过，可进入初步设计」）
> 状态：**v1.1 已通过评审**（2026-09-28；外部评审结论「**通过，可进入详细设计**」——★ 附 1 🟡 + 5 🟢，逐条处置见 **§1.4**）
> 本稿输入：需求稿的 **K1–K8 / R1–R5 / D0–D3 / N1–N4 / A1–A5** ＋ 外部评审 **§3「初设必须把 D1 那条接缝讲透」** ＋ ★ 本稿新增的**代码基线 B1–B10**（全部带行号）＋ ★ 外部评审（初设）**§1–§11 的逐条处置**（§1.4）

---

## 1. 设计输入与基线

### 1.1 已定项（需求稿的结论，本稿直接采用）

| 项 | 结论 | 出处 |
|---|---|---|
| **D0** | 换算落点 = **(a) `Application::Create` 末尾**（`Window` 完全构造之后；不依赖成员声明顺序） | 需求稿 §3 · R3 实测 |
| **D1** | **新增平台接缝方法（方案 ①）**——★ **本稿的核心任务 = 把它的形态 / 命名 / 调用链讲透** | 需求稿 §3 · 评审 §3 |
| **D2** | **不补**「客户区 DIP 尺寸」查询 API（K8 是判据 ③ 的证据，不是本阶段的对象） | 需求稿 §3 · 评审 §4 |
| **D3** | **保留** `Show()` 的尺寸 / DPI 复核（跨屏兜底；实测为 no-op） | 需求稿 §3 · 评审 §5 |
| **N2** | **不处理运行期尺寸变化**——本阶段只管「`Create` → `Show()`」这一段 | 需求稿 §4 |
| **Q1 口径** | `Create` 期应用的 DPI = 「**当前 Win32 窗口状态所能获得的 DPI**」，**不承诺**等于最终显示器 DPI | 需求稿 v1.1 §5（评审 §6 采纳） |
| **Q2 结论** | 新增换算是**净零成本**：「挂表后」`WM_SIZE` 总次数修前修后均为 **2**；且 `SetWindowPos` 在尺寸未变时**连消息都不发**（实测） | 需求稿 v1.1 §1.7 |

### 1.2 ★ 代码基线（B1–B10，全部带行号）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | `PlatformWindow` 是**公共头**里的**纯虚接口**——**22 个纯虚**（`Show` / `Release` / `Invalidate` / `GetClientSize` / `GetDpiScale` / `GetRenderContext` / `UpdateTextInputCaret` / `DestroyTextInputCaret` / `GetClipboardText` / `SetClipboardText` / `StartTimer` / `StopTimer` / `SetChromeMode` / `SetCaptionHeight` / `SetResizeInset` / `SetWindowLayer` / `Minimize` / `Maximize` / `Restore` / `GetWindowState` / `Hide` / `SetFileDropEnabled`），**无一个有默认实现** | `include/ECDI/Platform/PlatformWindow.h:29-145` |
| **B2** | ★ **`PlatformWindow` 实际有 3 个实现者**——生产 1（`Win32PlatformWindow`）＋ **测试替身 2** | `PlatformWindow.h:19` 自述「唯一实现：Win32PlatformWindow」（★ 指**生产**）· `src/Platform/Win32/Win32PlatformWindow.h:25` · `src/Tests/AnimationTests.cpp:24` · `src/Tests/ProgressBarTests.cpp:30` |
| **B3** | ★★ **DIP 目标尺寸在构造期已被记下**：`m_startupWidthDip` / `m_startupHeightDip`（注释自述「**延迟到 `Show()` 再换算成物理**」；`0` = 未设置 ⇒ 跳过） | `Win32PlatformWindow.h:208-212` |
| **B4** | ★★ **换算落在 `Show()` 的一个 `if` 块里（4 个语句）**：取 DPI → `m_messageHandler.SetDpi(dpi)` → `SetWindowPos(DipToPixels(...))` | `Win32PlatformWindow.cpp:182-192` |
| **B5** | `Show()` 的完整动作序列：**换算块 → `m_shown = true` → `ShowWindow` → `UpdateWindow`**（★ 全部在 `if (m_hwnd != nullptr)` 内） | 同上 `:171-200` |
| **B6** | ★ `Application::Create` 的动作序列：`unique_ptr(new Window(...))` → 取 `Window&` → **派发 `WindowCreatedEvent`** → `return window` | `src/Application/Application.cpp:65-82`（构造 `:71-72` · 事件 `:76-78` · 返回 `:81`） |
| **B7** | `Window::GetPlatformWindow()` 是 **public**（返回 `PlatformWindow&`——框架层零 Win32 类型） | `include/ECDI/Window/Window.h:96` |
| **B8** | `Window` 构造器 **private**，`friend class Application` ⇒ **`Application` 是唯一构造入口** | `Window.h:244-247` |
| **B9** | `PlatformWindow.h:22-28` 记着 **D-SEAM-1 平台能力扩展惯例（三步）**：① 接口加能力 virtual ② **`Window` 加公共方法透传（应用层唯一入口）** ③ 平台消息在实现内消化 | `PlatformWindow.h:22-28` |
| **B10** | 两个测试替身均为**逐方法 `override`** 风格，且**已有分组注释先例**（`// ── Phase 12：新增 7 个纯虚（…空实现）──`） | `ProgressBarTests.cpp:58` 等 |

### 1.3 ★ `Win32PlatformWindow.cpp` 的书写风格（实现时的硬约束）

该文件采用**隔行空行**风格（每个语句或语句组之间留一个空行）——**见 B5 的 `Show()` 全文**。★ 新增方法**必须沿用**该风格，否则同一文件内会出现两种密度。

### 1.4 ★ 外部评审处置（初设 v1.0 → v1.1）

★ **评审结论**：**通过，可进入详细设计**——「没有需要推翻当前方案的架构性问题」；附 **1 项 🟡 + 5 项 🟢**，★ 本稿即为**逐条处置结果**。

| # | 评审意见 | 处置 |
|---|---|---|
| **R-①** 🟡 | **`ApplyStartupSize()` 无启动尺寸时，是否连 `SetDpi` 都跳过？**——评审指出伪代码里 `return` 在 `SetDpi` **之前** ⇒ 无启动尺寸时**翻译器 DPI 也不刷新**；★ 要求**冻结为「刻意保持旧行为」**，而不是留到实现期临时决定；★ 评审**倾向保持直接 return**（否则该方法会滑向「顺便 `SyncDpi()`」的**职责膨胀**） | ✅ **采纳并冻结**——★ **新增契约 C10**（见 §5）；★ 在 §2.2 的 `@pre` 与 §4.1 的实现注释**两处**都写明「**刻意**」，并给出理由：**本阶段的对象是「`Create` 的启动几何」，不是泛化的 DPI 同步** ⇒ **无启动尺寸 ⇒ 整方法早退（含不刷 DPI）**，**沿用既有行为、零语义变化** |
| **R-②** 🟢 | **T22-1 / T22-3 的职责要在详设写明**——评审指出 T22-1 实际验证的是「`Create` 与 `Show` 之间没有几何变化」（**时序稳定性**），而非直接证明「`Create` 后已等于请求尺寸」 | ✅ **采纳**——§8 补**两条用例的职责分工**与**失败归因判据** |
| **R-③** 🟢 | ★★ **§6 写「5 个文件」但实际列了 6 个** | ✅ **采纳（文档修正）**——§6 与 §9 均改为 **6 个**，并把两个替身**分列成两个文件条目**（原写法把它们合称「两个测试替身」⇒ **计数与枚举不一致**） |
| **R-④** 🟢 | **「公共 API +1」应注明 source / ABI 口径** | ✅ **采纳**——**C7** 补口径：这是**源码接口集合 +1**（新增 1 纯虚 ⇒ **第三方既有 `PlatformWindow` 派生类必须重新编译**）；★ **本框架在 1.0 前不承诺 ABI 兼容**（沿 Phase 20.1 的既有口径）⇒ **不因此改变方案** |
| **R-⑤** 🟢 | **是否引入 `Window` 透传**（D-SEAM-1 第 ② 步） | ✅ **维持「不引入」**——与 **O2** 一致；评审明确支持该判断（本项是**框架内部接缝**，应用层无消费者） |
| **R-⑥** 🟢 | **是否给纯虚默认实现** | ✅ **维持「纯虚」**——与 **O4** 一致；评审指出让它成为 22 个纯虚里**唯一的例外**反而破坏接口自洽 |
| **R-⑦** 🟢 | **T22-2 的定位**——它经 `GetPlatformWindow()` **直调平台接口** ⇒ 严格说不是「公共 API 行为测试」 | ✅ **采纳**——§8 标注 T22-2 = **platform seam contract test**（验证 `ApplyStartupSize()` 的**幂等接缝**），**不是**用户公共 API 行为测试。★ 这个定位是**刻意的**：本阶段要验的正是那条接缝 |
| **R-⑧** 🟢 | 评审 §8 / §11：认可「**不为测试给 `PlatformWindow` 注入替身**」与「**是迁移而非新增**」 | **维持**（无改动）——★ 评审对 §4.3 的诚实说明（替身观测不到该调用）与 §4.1 的「一份实现、两个时机」均表赞同 |

---

## 2. ★★ 核心决策：D0 落点 ＋ D1 接缝的形态

### 2.1 D0 落点：`Application::Create` 末尾

**定案**：在 `Application::Create` 里、**`WindowCreatedEvent` 派发之前**、`Window` 已完全构造之后插入一步。

**为什么"在事件之前"**（本稿新增的判据）：

`WindowCreatedEvent` 的监听者若读窗口几何（`GetRootWidget()` / 平台层查询），**应当看到契约已成立的值**——否则「Create 返回时契约成立」只在 `return` 那一刻成立，而**事件通知期间仍是错的**。★ 把落实放在事件派发**之前**，两个时机同时成立，且**不增加任何成本**（同一次调用）。

**为什么不是构造期**：B8 的 `friend` 授权说明 `Window` 在构造时**尚处于成员初始化列表**——B3 的注释与需求稿 K6 已实测：构造期动手会同步派发 `WM_SIZE`，回调读到未构造成员 = **UB（125% 实测崩溃 `0xC000041D`）**。

### 2.2 ★★ D1 定案：方法形态 / 命名 / 签名 / 调用链

**方法（新增 1 个纯虚，落在 `PlatformWindow.h`）**：

```cpp
	// ── Phase 22：启动尺寸的落实（`Create` 末尾 / `Show()` 两处调用）──────

	/// @brief 按当前窗口 DPI，把 `Create` 阶段记录的「启动 DIP 总尺寸」落实到物理尺寸（Phase 22）
	/// @details 语义：「**`Window` 已完成构造，现按当前平台 DPI 应用 `Create` 阶段记录的 DIP 总尺寸**」。
	///          ★ 本方法**同时**刷新翻译器 DPI（`m_messageHandler.SetDpi`）——**两者必须同源**：
	///          尺寸换算用 `DipToPixels(w, dpi)`，而翻译器用同一个 `dpi` 把消息坐标折成 DIP，
	///          若二者取自不同时刻，几何与命中判定会不一致（**契约 C9**）。
	/// @pre 窗口句柄有效（实现内自行守卫）；★ **未设置启动尺寸（`0`）⇒ 直接返回**——★ **刻意**：
///      「无启动尺寸 ⇒ **连 DPI 也不刷**」是**沿用既有行为**、**零语义变化**（契约 **C10**，评审 R-① 冻结）。
///      理由 = 本阶段的对象是「`Create` 的启动几何」，**不是泛化的 DPI 同步** ⇒ 不让本方法滑向
///      「顺便 `SyncDpi()`」的职责膨胀。★ 本方法**只做一件事**：把启动尺寸落实（含它所需的 DPI 同源）。
	/// @note **幂等**：尺寸已正确 ⇒ 底层 `SetWindowPos` 为 no-op —— ★ **实测连 `WM_SIZE` 都不派发**
	///       （需求稿 §1.7），故 `Show()` 里的复核调用**零运行期成本**。
	/// @note **不提供「运行期改窗口尺寸」能力**——尺寸来源唯一（构造期记录），本阶段只管
	///       「`Create` → `Show()`」（需求稿 N2）。
	virtual void ApplyStartupSize() = 0;
```

**调用链（两处，缺一不可）**：

```
【时机 A：契约建立】Application::Create                       ← 需求稿 D0(a)
   ├─ m_windows.emplace_back(unique_ptr(new Window(...)))      B6 :71-72  ← Window 完全构造
   ├─ Window& window = *m_windows.back();                      B6 :73
   ├─ ★ window.GetPlatformWindow().ApplyStartupSize();         B7 :96（public）
   ├─ WindowCreatedEvent event(&window);  OnEvent(event);      B6 :76-78  ← ★ 监听者看到的是「已成立」的几何
   └─ return window;                                           B6 :81     ← ★ R1 的判据点

【时机 B：跨屏复核】Win32PlatformWindow::Show()                ← 需求稿 D3（保留）
   ├─ ApplyStartupSize();                                      ★ 替代原 B4 的 SetWindowPos（DPI 刷新随之内聚）
   ├─ m_shown = true;  ShowWindow(...);  UpdateWindow(...);    B5 :194-198
   └─ ★ 实测：尺寸已对 ⇒ no-op ⇒ 不派发 WM_SIZE
```

**`ApplyStartupSize()` 的体内动作**（= B4 的 4 个语句搬家，**零新增语义**）：

```
取 dpi = GetDpiForWindow(m_hwnd)      ← 语义 = 「当前窗口状态所能获得的 DPI」（需求稿 Q1 口径）
m_messageHandler.SetDpi(dpi)          ← 翻译器 DPI 与尺寸换算同源（契约 C9）
SetWindowPos(..., DipToPixels(m_startupWidthDip, dpi), DipToPixels(m_startupHeightDip, dpi),
             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)
```

### 2.3 ★ 为什么不走 D-SEAM-1 的第 ② 步（`Window` 透传）

B9 的三步惯例中，第 ② 步的**原文理由是「`Window` 加公共方法透传——**应用层唯一入口**」**——即：**当应用层需要这项能力时**，才在 `Window` 上开一个公共方法。

| 判断 | 结论 |
|---|---|
| 应用层是否需要「重新应用启动尺寸」？ | **不需要**——应用表达尺寸意图的入口**就是** `Create(title, w, h)` 的形参（B6）；再开一个公共方法**没有消费者**（YAGNI） |
| 那 `PlatformWindow` 上的 virtual 算不算"给应用的 API"？ | ★ **不算**——它的语义是「平台接口能力」，**调用者是框架自己**（`Application::Create`）。★ 载体必须是公共接口（因为 `Application` 只认识抽象 `PlatformWindow`，**不能**认识 `Win32PlatformWindow`——那会破坏 7.1 平台解耦） |
| 故 | ★ **只做惯例的第 ① 步（接口加 virtual）＋ 第 ③ 步（实现在平台层消化）；第 ② 步本次不适用**，并**如实记录这一偏离** |

★ 这是**唯一需要在初设里讲清的"惯例偏离"**：三步惯例是为「**应用层需要的新能力**」设的模板；本项是「**框架内部的契约修正**」，第 ② 步的**前提不成立**。

### 2.4 ★ 为什么是"搬家"而不是"只搬 `SetWindowPos`"

B4 那个块里有 **`m_messageHandler.SetDpi(dpi)`**——它不只是尺寸问题，而是**翻译器的 DPI 状态**。

若只把 `SetWindowPos` 搬走、把 `SetDpi` 留在 `Show()`：
- `Create` 末尾会按新 DPI 改尺寸，**但翻译器仍在用构造期的 DPI**（B3 注释自述「构造期已 SetDpi」）；
- 于是 `Create` → `Show()` 之间若来一条需要坐标换算的消息（如 `WM_NCHITTEST` ⇒ `PixelsToDip`），**换算用的 DPI 与实际几何不匹配**。

⇒ ★ **整块搬走**，并由 `ApplyStartupSize` 统一承担——**尺寸与 DPI 必须同源**（契约 C9）。

---

## 3. 头文件改动（草案）

### 3.1 `include/ECDI/Platform/PlatformWindow.h`（**既有头内追加 1 个纯虚**）

- **位置**：新起一个 `Phase 22` 分组（沿 `Phase 12` / `Phase 13` / `Phase 14` 的分组注释先例），**置于文件末尾**（不动既有方法的相对顺序）。
- **草案全文**：见 §2.2 的代码块。
- ★ **公共头计数 92 → 92**（不新增头文件）· **公共 API 计数 +1**。

### 3.2 其余头文件：**零改动**

| 头 | 判断 |
|---|---|
| `include/ECDI/Window/Window.h` | ★ **不改**（§2.3：不做第 ② 步透传） |
| `include/ECDI/Application/Application.h` | **不改**（`Create` 签名不变——这正是不需要新增公共 API 的原因） |
| `src/Platform/Win32/Win32PlatformWindow.h` | **不改**（新增的是 override，声明即可；★ 成员 `m_startup*Dip` 已存在，B3） |

---

## 4. 实现分解

### 4.1 `Win32PlatformWindow`：新增 `ApplyStartupSize()` ＋ `Show()` 瘦身

```cpp
// ── Win32PlatformWindow.h（public 区，Phase 22 分组）──
	// ── Phase 22：启动尺寸落实（Create 末尾 + Show 两处调用）────────

	/// @brief 按当前 DPI 落实启动尺寸（含翻译器 DPI 同步——详见 PlatformWindow.h 的契约）
	void ApplyStartupSize() override;
```

```cpp
// ── Win32PlatformWindow.cpp（★ 沿用本文件的隔行空行风格，见 §1.3）──

void Win32PlatformWindow::ApplyStartupSize() {

	// ★★ Phase 22：本方法由「原 Show() 里的换算块」原样搬来（B4）——**零新增语义**。
	// 搬家的理由：让同一段逻辑能在**两个时机**被调用——
	//   ① `Application::Create` 末尾（让 DIP 契约在 Create 返回时即成立——本阶段的目标）；
	//   ② `Show()`（保留跨屏复核——Phase 20 的既有理由：「Show 时窗口的显示器关联才确定」）。
	// ⚠️ DPI 与尺寸**必须同源**：`SetDpi` 与 `DipToPixels` 用同一个 dpi，
	//    否则「几何」与「命中判定」会按不同 DPI 换算。
	// ★ **早退语义（契约 C10，评审 R-① 冻结）**：无启动尺寸（`<= 0`）⇒ **整方法早退**，
	//   **连 `SetDpi` 也不执行** —— ★ **刻意沿用既有行为**（本方法只管「启动几何」，不做泛化 DPI 同步）。

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

`Show()` 的对应改动（**只把 B4 的块换成一行**，其余逐字不动）：

```cpp
void Win32PlatformWindow::Show() {

	if (m_hwnd != nullptr) {

		// ★★ Phase 20（△10 落点迁移 + 崩溃修复）：按窗口 DPI 把客户区定到「DIP 语义」对应的**物理尺寸**。
		//   （原注释保留——理由仍成立，只是执行体改为调用 ApplyStartupSize）
		// ★ 顺序：「构造期已 SetDpi」→ 此处再取一次并更新（Show 时窗口的显示器关联已确定）。
		// ★ 此刻窗口**尚未显示**（ShowWindow 在后）⇒ 改尺寸**不产生闪烁**。
		// ★★ Phase 22：**尺寸落实本身已由 `Application::Create` 末尾执行**（契约在 Create 返回时即成立）；
		//   此处保留调用是**跨屏复核**——同尺寸时为 no-op（实测不派发 WM_SIZE）⇒ 零运行期成本。
		ApplyStartupSize();

		m_shown = true;

		ShowWindow(m_hwnd, SW_SHOW);

		UpdateWindow(m_hwnd);

	}
```

### 4.2 `Application::Create`：新增一步

```cpp
	Window& window = *m_windows.back();

	// ★★ Phase 22：把构造期记录的 DIP 目标尺寸**按当前 DPI 落实**。
	// 为什么在这里：此刻 `Window` **已完全构造**（成员就绪 ⇒ 回调安全，避开 Phase 20 的构造期 UB），
	//   且**本函数尚未返回** ⇒ 「公共 API 语义恒为 DIP」在 Create 返回时即成立（需求稿 R1）。
	// 为什么在事件**之前**：`WindowCreatedEvent` 的监听者若读几何，应当看到已成立的值。
	// 幂等：`Show()` 会再调一次（跨屏复核），届时为 no-op。
	window.GetPlatformWindow().ApplyStartupSize();

	// 手动派发 WindowCreatedEvent（不是 Win32 消息翻译的产物，是框架层语义事件）
	WindowCreatedEvent event(&window);

	OnEvent(event);


	return window;
```

### 4.3 两个测试替身：各 +1 空实现

| 文件 | 落点 | 改动 |
|---|---|---|
| `src/Tests/AnimationTests.cpp` | `TestPlatformWindow`（`:24`） | `void ApplyStartupSize() override{}` ＋ 分组注释 |
| `src/Tests/ProgressBarTests.cpp` | `TestPlatformWindow`（`:30`） | 同上（★ 该文件已有分组注释先例，见 B10） |

★ **替身语义 = 空实现**（它们不实现真实尺寸逻辑）——与既有 22 个纯虚里 `Show() override{}` / `Hide() override{}` 等**同款处理**。★ 如实说明：替身**观测不到**这个调用（`Application::Create` 造的是真的 `Win32PlatformWindow`，测试无法注入替身到该入口）。

---

## 5. 契约（C1–C10）

| # | 契约 | 判据 |
|---|---|---|
| **C1** ★★ | **`Application::Create` 返回后**（★ **不待 `Show()`**），窗口的**外框 DIP 尺寸 == 请求的 `w × h`** | 非 96 DPI 下可测（本机 125%）；96 DPI 下恒等 ⇒ 无差异 |
| **C2** ★ | **96 DPI 零回归**：`dpi == 96` ⇒ `DipToPixels(w, 96) == w` ⇒ 尺寸不变 ⇒ **`SetWindowPos` 为 no-op** | ★ **分两层**（需求稿 A2 的措辞纪律）：**① 数学保证**（G5 恒等，可证）＋ **② 行为验收**（四工具链既有 **261** 全绿）——★ ① 不替代 ② |
| **C3** | **幂等**：`ApplyStartupSize()` 可重复调用（`Create` ＋ `Show()` 各一次）；尺寸已正确 ⇒ 不改尺寸、**不派发 `WM_SIZE`** | ★ 实测依据：需求稿 §1.7（mode C 的 `Show()` 只剩 1 次 `WM_SIZE`，且来自 `ShowWindow`） |
| **C4** ★ | **尺寸来源唯一**：`ApplyStartupSize()` **无参**——尺寸只能来自构造期记录的 DIP（B3）；★ **不提供「运行期改尺寸」能力** | 方法签名无参数；需求稿 N2 |
| **C5** ★★ | **不引入 UB**：落实点必须在 `Window` **完全构造之后**（结构性判据，非"恰好没崩"） | 落点 = `Application::Create` 内（B6 `:73` 之后）· 125% 实测不崩（需求稿 R3） |
| **C6** ★ | **`Show()` 保留复核**，且它在尺寸已对时为 **no-op** | `Show()` 仍调用 `ApplyStartupSize()`（§4.1）；需求稿 §1.7 实测 |
| **C7** | **公共头 92 → 92**；**公共 API +1**（`PlatformWindow` 新增 1 纯虚）——★ **口径（评审 §10 采纳）**：这是**源码接口集合 +1**（新增纯虚 ⇒ **第三方既有 `PlatformWindow` 派生类必须重新编译**）；★ **本框架在 1.0 前不承诺 ABI 兼容**（沿 Phase 20.1 的既有口径）⇒ **不因此改变方案** | 头文件计数；不新增公共头 |
| **C8** ★ | **DPI 语义**：`ApplyStartupSize` 用的是「**当前 Win32 窗口状态所能获得的 DPI**」，**不承诺**等于最终显示器 DPI | 需求稿 v1.1 Q1 的口径声明（评审 §6 采纳） |
| **C9** ★★ | **DPI 与尺寸同源**：翻译器 `SetDpi` 与 `DipToPixels` 用**同一个** `dpi` 值 | §2.4 的论证；实现上二者在同一函数内、共用局部量 `dpi` |
| **C10** ★ 新增（v1.1） | ★★ **无启动尺寸 ⇒ 整方法早退（含不刷 DPI）**：`m_startupWidthDip <= 0 \|\| m_startupHeightDip <= 0` ⇒ **直接返回**，**`SetDpi` 也不执行** | ★ **刻意沿用既有行为**（评审 R-① 采纳并冻结）：本阶段对象 = 「`Create` 的启动几何」，**不是泛化的 DPI 同步** ⇒ 避免职责膨胀。★ 判据 = 实现里 `return` **在 `SetDpi` 之前**（字符级） |

---

## 6. 影响面

| 项 | 内容 |
|---|---|
| **改动的文件** | **6 个**：`include/ECDI/Platform/PlatformWindow.h`（+1 纯虚）· `src/Platform/Win32/Win32PlatformWindow.h`（+1 override 声明）· `src/Platform/Win32/Win32PlatformWindow.cpp`（+1 方法 + `Show()` 瘦身）· `src/Application/Application.cpp`（+1 调用）· `src/Tests/AnimationTests.cpp`（`TestPlatformWindow` +1 空实现）· `src/Tests/ProgressBarTests.cpp`（同） |
| **零改动** | `Window.h` / `Window.cpp` · `Application.h` · `PlatformWindowHost.h` · 渲染侧 · `CMakeLists.txt`（GLOB 自动入库）· `main.cpp` |
| **公共头 / 公共 API** | 头 **92 → 92** · API **+1** |
| **测试** | 新增用例见 §8（**+3**：T22-1..T22-3）；★ 既有 **261** 必须全绿 |
| **断言特征串** | **不变**（11 条）——本阶段**不新增 `FRAMEWORK_ASSERT`**（无新的前置条件需要断言；尺寸为 0 的既有权衡保持"直接返回"） |
| **文档回写** | 需求稿无需改（结论未变）；执行后回写本稿（实施记录）＋ `docs/README.md` 进度 + 审计 `D-7` 状态 |

---

## 7. 开放决策点（O1–O5，评审已背书）

| # | 问题 | 候选 | ★ 本稿倾向 |
|---|---|---|---|
| **O1** ★★ | **方法命名** | ① `ApplyStartupSize` ② `ApplyStartupSizeDip` ③ `SyncStartupGeometry` ④ 其他 | **①** ——★ 理由：**与成员名同词根**（`m_startupWidthDip` 的 `Startup`）⇒ 符合「一个概念一个词」；★ 且既有惯例是「DIP 语义写在注释、**不写进名字**」（如 `SetCaptionHeight` / `SetResizeInset`），故**不加 `Dip` 后缀** |
| **O2** ★★ | **是否走 D-SEAM-1 第 ② 步（`Window` 透传）** | ① **不做**（本项是框架内部修正，应用层无消费者）② 做（补 `Window::ApplyStartupSize()` 公共方法） | **①** ——见 §2.3 的完整论证；★ **这是本稿唯一声明偏离惯例之处**，请评审重点核 |
| **O3** | **`Create` 内的调用位置**（事件前 / 后） | ① **`WindowCreatedEvent` 之前** ② 之后 | **①** ——★ 让事件监听者看到的几何也已成立（§2.1）；成本为零 |
| **O4** | **纯虚 vs 有默认实现** | ① **纯虚**（沿用 B1 的惯例 ⇒ 须改 2 个替身）② 给空默认实现（替身零改动，但**破坏接口自洽**：22 个纯虚里唯一的例外） | **①** ——一致性优先；2 处替身改动是**一次性的、机械的** |
| **O5** ★ | **测试如何覆盖**（96 DPI 下改动是 no-op ⇒ 断言什么？） | ① **「Create 后 == Show 后」当主判据**（★ 96 DPI 恒真、非 96 DPI 下是真的判据——改前必红）② 用 `GetDpiForWindow() != 96` 条件断言 ③ 两条都要 | **③ 两条都要** ——见 §8 |

★ **评审已背书**：**O2**（维持不引入 `Window` 透传）与 **O4**（维持纯虚）——见 §1.4 的 **R-⑤ / R-⑥**；**O1**（命名）/ **O3**（调用位置）/ **O5**（测试覆盖）**无异议**。

---

## 8. 测试方向（T22-1..T22-3）

★ **设计约束**：本阶段在 **96 DPI 下是恒等 no-op** ⇒ 「改动前后无差异」的断言**测不出东西**。故**主判据选「与环境无关但能区分改前/改后」的量**。

| # | 用例 | 输入 / 期望 | ★ 为什么它能区分改前/改后 |
|---|---|---|---|
| **T22-1** ★★ | **`Create` 后的几何 == `Show` 后的几何** | 先不要在 `Create` 后立刻读（窗口未显示时 `GetWindowRect` 在 96 与非 96 下都可读）；取 `GetClientSize()` 与根 widget 尺寸，在 `Create` 后与 `Show()` 后各读一次，断言**逐项相等** | ★★ **改前**：非 96 DPI 下 `Create` 后是 **640×480 DIP** 一侧、`Show` 后 **800×600** ⇒ **必红**；**改后**：两侧都在 `Create` 末尾已落实 ⇒ **相等**。★ 96 DPI 下**恒真**（两侧本就相等）⇒ **不依赖机器 DPI 就能跑**，而在有差异的机器上**真的判** |
| **T22-2** | **`ApplyStartupSize()` 幂等** | 通过 `window.GetPlatformWindow()`（B7 public）连续调用两次，读 `GetClientSize()` / 根 widget 尺寸，断言**不变** | 覆盖 C3；不依赖 DPI |
| **T22-3** | ★ **条件式契约断言**（C1 的直接判据） | `if (GetDpiForWindow(hwnd) != 96) { 断言 Create 后外框 DIP == 请求值 }` | ★ 在 **96 DPI 机器上跳过**（用例仍通过），在**非 96 DPI 上真正验证 C1**——★ 本机 125%，**实际会跑到** |
| **既有 261** | 全绿 | 四工具链 | C2 的**行为验收**层（§5） |

★ **实现提示**：测试取 `HWND` 用既有三跳先例（`WindowChromeTests` / `CaptionBarTests` 的 `Handle()`）；★ **若用例不 `Show()`**，注意 Phase 21 末期记录的「未 Show 窗口的 DIP 尺寸」陷阱——**本阶段的 T22-1 正是利用这个陷阱做判据**。

★★ **两条 DPI 相关用例的职责分工（评审 §6 采纳）**：

- **T22-1 = 时序稳定性契约**——验证「**`Create` 与 `Show` 之间不发生几何变化**」。★ **它失败 ⇒ 判「Create / Show 时序不稳定」**。
- **T22-3 = 直接 DIP 契约**——在非 96 DPI 下**直接**断言「`Create` 后外框 DIP == 请求值」。★ **它失败 ⇒ 判「`Create` 阶段根本没有正确落实 DIP」**。
⇒ ★ 拆成两条的价值 = **失败归因明确**（不必从一条复合断言里反推是「时序」还是「落实」的问题）。

★ **T22-2 的定位（评审 §7 采纳）**：它经 `Window::GetPlatformWindow()` **直调平台接口** ⇒ 属 **platform seam contract test**（验证 `ApplyStartupSize()` 的**幂等接缝**），**不是**用户公共 API 行为测试。★ 这个定位是**刻意的**：本阶段要验的正是**那条接缝**。

---

## 9. 修订记录

- **v1.1**（2026-09-28）**外部评审处置（1 🟡 + 5 🟢 ⇒ 全部处置）**。★ **评审结论**：**通过，可进入详细设计**——「没有需要推翻当前方案的架构性问题」。① ★ **R-① 🟡 冻结「无启动尺寸 ⇒ 整方法早退（含不刷 DPI）」**——**新增契约 C10**；★ 在 §2.2 `@pre` 与 §4.1 注释**两处**写明「**刻意**」，理由 = 本阶段对象是「`Create` 的启动几何」**不是泛化 DPI 同步** ⇒ 避免职责膨胀。② ★ **R-② 补 T22-1 / T22-3 的职责分工**（**时序稳定性** vs **直接 DIP 契约**）＋**失败归因判据**。③ ★★ **R-③ 修正计数**：§6 与 §9 的「**5 文件**」→ **6 文件**——★ **根因** = 原写法把**两个测试替身合称**为一项（`AnimationTests.cpp` / `ProgressBarTests.cpp`），**计数与枚举不一致**；本版把二者**分列为独立条目**。④ ★ **R-④ C7 补 source / ABI 口径**（源码接口集合 +1；1.0 前不承诺 ABI）。⑤ ★ **R-⑦ T22-2 定位**为 **platform seam contract test**。⑥ **维持**（评审背书）：**不引入 `Window` 透传** · **纯虚不给默认实现**。★ **结构零变动**：契约 **C1–C9 → C1–C10**（**新增 1 条**）· 内核逻辑 / 调用链 / 开放点 / 测试用例数**均不变**。
- **v1.0**（2026-09-28）初稿。**输入** = 需求稿 v1.1（评审通过）＋ 评审 §3「把 D1 讲透」的要求。**新增代码基线 B1–B10**（含 ★ **B2 的实现者实为 3 个**——生产 1 + 测试替身 2，与 `PlatformWindow.h:19` 的「唯一实现」表述**不矛盾但需分辨**）。**核心决策**：D0 落点（**事件派发之前**——本稿新增的判据）· ★★ **D1 形态定案**（新增 1 个**无参**纯虚 `ApplyStartupSize()`；**整块**搬 `Show()` 的换算块——含 `SetDpi`，理由 = **DPI 与尺寸必须同源**）· ★ **声明一处惯例偏离**（不做 D-SEAM-1 第 ② 步 `Window` 透传，理由 = 该步前提「应用层需要」不成立）。**契约 C1–C9** · **影响面 6 文件**（公共头 92→92 / API +1 / 零新增断言）· **开放点 O1–O5** · **测试 T22-1..T22-3**（★ 主判据 = 「Create 后 == Show 后」，**96 DPI 恒真、非 96 DPI 真的判**）。**待评审。**
