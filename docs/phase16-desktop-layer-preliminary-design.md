# Phase 16 桌面驻留层（`WindowLayer::Desktop`）初步设计（v1.3）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-19
> 状态：**评审通过 · O1 已实测关闭 ⇒ 可进详细设计**——v1.1 按评审处置（**O3 定为 C** · **O2 反转为「不加」** · 新增契约 **C11 / C12**）；**v1.2 实测定案：`ApplyDesktopStyle` 不需要 `SWP_FRAMECHANGED`**（§6.1：三组 × 3 轮 Win+D + 框架既有先例），并如实记录探针 v1–v5 的一处**保真度缺陷**（漏了框架 `SetChromeMode` 的那次 `SWP_FRAMECHANGED`，**已修于 v5a**）。★ **v1.3 补记**：探针 **v5a**（**已与框架四点对齐**，含配置期 `SWP_FRAMECHANGED`）复测组 1 ⇒ **A / B / C 三点全部 `non-client = 0 × 0`**、Win+D **3/3 PASS** ⇒ O1 结论**不变**，且决定性证据现出自**已对齐**的探针
> 实测来源：`.workbuddy/spike/desktop_layer_probe.cpp` **v5a**（**四点**对齐框架：窗口样式 · 三处 NC 拦截 · **配置期 `SWP_FRAMECHANGED`** · DPI 感知）
> 前置：`phase16-desktop-layer-requirements.md` **v1.3**（D0–D11 已定 · §8.1 两条新约束 · §8.2 留给初设的 8 项）· `.workbuddy/spike/desktop_layer_probe.cpp`（4 形态 × 3 轮实测源码）· `desktop_spike.cpp`（路线 E 原始提交，`--auto` 可复现）
> 一句话：把「**紧贴桌面窗口正上方 + 前台钩子维持**」这条已取证路线实现进 `Win32PlatformWindow`——**公共 API 净增 0**，全部改动收敛在**平台实现层内部**（本项目首个纯实现层 Phase）。

---

## 1. 设计输入与基线

### 1.1 需求阶段已定（本稿不再讨论）

| 决策 | 结论 | 来源 |
|---|---|---|
| **D1** ★ | **A′：`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`**（真因 = `WS_MINIMIZEBOX`；「显示桌面」只最小化可最小化窗口） | 需求稿 §8 P0（4 形态 × 3 轮 + 目视） |
| **D2** | **不加 `WS_EX_NOACTIVATE`**（`ex=0` 下交互完整）；`WS_EX_TOOLWINDOW` **留本稿 O2（v1.1 倾向：不加）** | 需求稿 §8 P1 |
| **D3** | **配置期内 `SetWindowLongPtrW` 改样式位**（改动安全性的细节归本稿 §3.2 / O1） | 需求稿 §4 |
| **D4** | **只装前台钩子，不加 Timer**（实测 tick 三倍开销零收益） | 需求稿 §8 P2 |
| **D5** | **每窗口一个钩子**；生命周期必须绑 `Win32PlatformWindow` | 需求稿 §4 |
| **D6** | **不缓存 `Progman` 句柄**（每次即时重查 ⇒ 结构上免疫 explorer 重建） | 需求稿 §8 P3-4 |
| **D7** | `GetShellWindow()` ≡ `FindWindowW("Progman")`（实测恒等）⇒ **倾向官方 API** | 需求稿 §8 P3-5 |
| **D8** | **`TargetInsertAfter` 抽象** + 句柄无效时**跳过修改**（绝不降级 `HWND_BOTTOM`） | 需求稿 §4 |
| **D11** | 钩子生命周期四态：Set ⇒ 装 · Hide ⇒ **不卸** · Show ⇒ **不重装** · Release ⇒ **必卸** | 需求稿 §3.3 R10 |
| **§8.1②** | **重插前先判在位**（已由「备注」升格为**正式设计输入**） | 需求稿 §8.1 |

### 1.2 代码基线勘察（B1–B11，2026-09-19 复核）

| # | 事实 | 出处 |
|---|---|---|
| **B1** | `SetWindowLayer` 现状：`m_shown` 判据（配置期）→ 幂等 → `m_windowLayer = layer` → Desktop 只记 Warning → `if (m_hwnd == nullptr) return;` → `if (m_windowLayer != Normal) SetWindowPos(…, HWND_BOTTOM, …)` | `Win32PlatformWindow.cpp:842-887` |
| **B2** | `WM_WINDOWPOSCHANGING` 现状：`if (m_windowLayer != WindowLayer::Normal)` ⇒ 无条件写 `HWND_BOTTOM` | `:405-424`（判据 `:410`，赋值 `:416`） |
| **B3** | 窗口创建样式：`CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW, …)` | `:63-77` |
| **B4** | `Release()` 现状：`if (m_hwnd==nullptr) return true;` **先判空再** `DestroyWindow` ⇒ **若 hwnd 已空则不会执行任何清理** | `:187-197` |
| **B5** | 析构：`~Win32PlatformWindow(){ Release(); }` | `:92-96` |
| **B6** | 测试缝先例：`using DragFinishFn = void (*)(HDROP);` + `SetDragFinishForTests(fn)`——**函数指针形态，不出实现层、保 `final`** | `Win32PlatformWindow.h:70-94` |
| **B7** | 平台层**零**「hook → 实例」反查设施；全库 `SetWinEventHook` 只在 spike 中出现（用全局变量——单窗口探针的便利） | 全库 grep |
| **B8** | 测试基线：`WindowChromeTests.cpp` 9 用例（`ChromeModeDecidedOnce` / `BorderlessClientEqualsWindow` / `NCHitTestNineGrid` / `NCHitTestModeIsolation` / `MaximizedClientWithinWorkArea` / `CaptionHeightUnit` / `ZeroBoundaryClamp` / `StateEventFromApi` / `RuntimeApiRejectedBeforeShow`）；测试设施：`TestWindow`（非拥有 `Window*` + `Handle()` 三跳取 HWND）+ `PumpMessages(n)` | `src/Tests/WindowChromeTests.cpp:1-135` |
| **B9** | 全局测试基线 **210 用例**（Phase 15 收口）；`PlatformWindow` 有 **3 个实现者**（`Win32PlatformWindow` + `AnimationTests` / `ProgressBarTests` 的 `TestPlatformWindow`） | MEMORY / skill 条 33 先例 |
| **B10** | **测试可直接 include 内部头**：`DropFilesTests.cpp:13` 即有 `#include "Platform/Win32/Win32PlatformWindow.h"` ⇒ 拆出的**纯函数无需新文件 / 无需测试缝**即可被自动化覆盖（O3 取 C 的前提） | `src/Tests/DropFilesTests.cpp:13` |
| **B11** | **框架已有「配置期通知系统重算非客户区」的先例**：`SetChromeMode(Borderless)` 自己派发 `SetWindowPos(…, SWP_FRAMECHANGED)`，注释原文「通知系统重算非客户区……**必须在窗口显示前派发——否则闪一次边框**」 | `Win32PlatformWindow.cpp:800-804` |

### 1.3 ★ 需求稿勘误（K13 与实况不符——本稿发现）

需求稿 §1.2 的 **K13** 写：

> **`Bottom` 档已有跨工具链验证的 z 序用例**（Phase 12）——`WindowChromeTests.cpp`（R10 相关用例）· `phase12-windowchrome-detailed-design.md` §5.3 T 系列

**实核不成立**：`WindowChromeTests.cpp` 中 `SetWindowLayer` 出现 **0 次**，9 个用例**无一条**涉及 `WindowLayer` / z 序；`WindowLayer.h` 仅被 `#include`（`:16`）而未被使用。全库唯一的 `SetWindowLayer` 调用点，是 2 个测试替身里的 `override{}` 空实现。Phase 12 详设 §5.3 的 T0–T7a 九条同样**无 z 序 / 层级用例**。

⇒ **结论：Phase 12 的 R10 是「有实现、零自动测试覆盖」**。影响两条：

1. **R2「`Bottom` 语义零回归」没有现成测试保护**——本稿改动 `WM_WINDOWPOSCHANGING` 时若无新用例，`Bottom` 是否被破坏**无法自动发现** ⇒ 本稿 **T16-1 专门补 `Bottom` 回归**（§7）；
2. K13 的表述必须回写修正（本稿记录，需求稿同步改）——这是「**文档声称的测试覆盖 ≠ 实际覆盖**」的又一实例，与 skill 条 44（grep 判据须区分代码 / 注释）同族。

### 1.4 §8.2 八问 → 本稿的答案索引

| §8.2 # | 问题 | 本稿回答处 |
|---|---|---|
| 1 | `A′` 改法 / 是否需 `SWP_FRAMECHANGED` / 幂等与可逆 | §3.2（**幂等与可逆**）· **O1**（`SWP_FRAMECHANGED` 待实测） |
| 2 | `WS_EX_TOOLWINDOW` 取舍 | **O2**（给倾向与补验方案） |
| 3 | 钩子成员 / 回调 / 生命周期 + 四工具链一致性 | §2.2 + §3.3（**hook → 实例反查**是本稿核心设计）· **O5** |
| 4 | `TargetInsertAfter` / `IsDirectlyAboveDesktop` 内部结构 | §3.1 + §3.3 |
| 5 | `GetShellWindow()` vs `FindWindowW` | §3.4（**取前者**） |
| 6 | 句柄无效绝不误降级 `HWND_BOTTOM` 的代码级保证 | §3.1（`nullptr` = 跳过哨兵）· 契约 **C2** |
| 7 | `Hide → Show → Release` 状态机落法 | §3.5 + 契约 **C6** |
| 8 | 测试缝形态与四工具链测试策略 | §3.7（**钩子计数缝**，复刻 `DragFinishFn` 先例）· §7 |

---

## 2. 头文件改动

### 2.1 公共头：**净增 0**（逐头确认）

| 头 | 改动 | 说明 |
|---|---|---|
| `ECDI/Window/WindowLayer.h` | **仅注释** | 枚举语义已定（Phase 12 `D-DESK-1`）；本阶段**不新增 `GetWindowLayer()`**（需求稿 §5 非目标——仍无消费者）。注释中「spike 未验证 / 退化为 Bottom」的表述须改为已落地事实（§3.6） |
| `ECDI/Platform/PlatformWindow.h` | **零** | `SetWindowLayer` 契约已足；**不新增任何 pure virtual** ⇒ **3 个实现者零同步**（B9） |
| `ECDI/Window/Window.h` | **零** | `SetWindowLayer` 透传已存在 |
| 其余公共头 | **零** | — |

⇒ **公共头 92 → 92**（Phase 15 收口值）。**本阶段全部结构性改动在 `src/` 内部头与实现内。**

### 2.2 `src/Platform/Win32/Win32PlatformWindow.h` 增量（内部头）

**成员（private 区，`m_windowLayer` 之后）**：

```cpp
	/// @brief 桌面驻留维护钩子（Phase 16 D11——**仅 Desktop 档持有**，其余档恒 nullptr）
	HWINEVENTHOOK m_desktopHook = nullptr;

	/// @brief 钩子观测缝（测试用——生产恒 nullptr；见 public 区 setter）
	HookObserverFn m_hookObserver = nullptr;
```

**方法（private 区）**：

```cpp
	// ── Phase 16：桌面驻留层（R1–R4）──────────────────────────────

	/// @brief 目标 z 序位置（D8——按档位分流的**唯一**判据）
	/// @return `HWND_BOTTOM` = Bottom 档 ·「桌面窗口的上一位」= Desktop 档 ·
	///         **`nullptr` = 本次不修改**（Normal 档 / 桌面句柄无效 / 桌面已处最顶）
	/// @details ⚠️ **`nullptr` 是「跳过」哨兵，不是「插到最底」**——调用方必须显式判空，
	/// 绝不可把它直接交给 `SetWindowPos`（那会落到 `HWND_BOTTOM`，把 Desktop 档
	/// **意外降级**成 Bottom —— K8 / 契约 C2 的核心）。
	HWND TargetInsertAfter() const;

	/// @brief 定位桌面窗口（D6/D7——**不缓存**，每次即时重查）
	/// @return 桌面窗口句柄；`nullptr` = 不可用（explorer 重建窗口期 / 非交互式会话）
	static HWND FindDesktopWindow();

	/// @brief 本窗口是否已「紧贴桌面窗口正上方」（D8 判据——§8.1② 的前置检查）
	bool IsDirectlyAboveDesktop() const;

	/// @brief 把窗口重新插到桌面窗口正上方（内部**先判在位**，已在位则什么都不做）
	void ReinsertAboveDesktop();

	/// @brief 应用 / 撤销 Desktop 档所需的样式位（D1 A′ / D3）
	/// @param desktop true = 移除 `WS_MINIMIZEBOX`；false = 补回
	/// @details **幂等**（目标值与现值相同则不动手）且**可逆**（离开 Desktop 档时补回——
	/// 配置期内允许 `Desktop → Normal` 转移，见 B1 的配置期契约）。
	void ApplyDesktopStyle(bool desktop);

	/// @brief 同步钩子与当前档位（D11 四态表的**唯一**执行点）
	/// @details 幂等：Desktop 档且未装 ⇒ 装；非 Desktop 档且已装 ⇒ 卸；其余不动。
	void SyncDesktopHook();

	/// @brief 强制卸除钩子（`Release()` / 析构路径专用——只减不增，不读档位）
	void SyncDesktopHookOff();

	/// @brief 前台事件回调的转发落点（在调用线程上执行——见契约 C8）
	/// @param foreground 成为前台的窗口（可能不是本窗口）
	void OnForegroundChanged(HWND foreground);

	/// @brief 静态窗口过程式回调（`SetWinEventHook` 无 user-data 参数 ⇒ 经 hook 反查实例）
	static void CALLBACK DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
		LONG idObject, LONG idChild, DWORD thread, DWORD time);

	/// @brief 当前是否处于 Desktop 档（判据集中——避免各处重复比较枚举）
	bool IsDesktopLayer() const noexcept{ return m_windowLayer == WindowLayer::Desktop; }
```

**纯函数（public 区——O3 定为 C 的落点、零新文件）**：

```cpp
	/// @brief 把「档位 + 桌面句柄」映射为目标 z 序位置（**纯函数**——不读实例状态，O3 定为 C）
	/// @return `HWND_BOTTOM` = Bottom 档 ·「桌面窗口的上一位」= Desktop 档 ·
	///         **`nullptr` = 本次不修改**（Normal 档 / 桌面句柄无效 / 桌面已处最顶）
	/// @details ⚠️ `nullptr` 是「跳过」哨兵，**不是**「插到最底」——调用方必须显式判空。
	/// @note **public static 的目的只是可被自动化测试直接覆盖**（C2 是本阶段最危险的路径）。
	///       测试已 include 本内部头（B10）⇒ 零成本；不读实例状态 ⇒ 无副作用、可纯逻辑断言。
	static HWND ResolveTarget(WindowLayer layer, HWND desktop);
```

> ⚠️ **为什么放 public（本类其余新增方法一律 private）**：C2「桌面不可用 ⇒ 不修改（**绝不**降级 `HWND_BOTTOM`）」是本阶段**最危险的路径**（写错即静默降级）；而它**不需要任何窗口、不需要 explorer** 就能验证——这正是 v1.1 把 O3 由「倾向 C」**定为 C** 的理由（A 的注入式测试缝要「假装系统 API」，C 只验证自己的判据）。

**测试缝（public 区，紧随 `DragFinishFn` 先例 B6）**：

```cpp
	/// @brief 桌面钩子安装/卸除的观测缝（Phase 16 D9——**函数指针**形态，不出实现层、保 `final`）
	/// @details 复刻 `DragFinishFn` 先例（B6）：`SetWinEventHook` 依赖真实桌面环境，
	/// 自动化只能断言「**装/卸被调用了**」，故以观测缝计数而不 mock 系统 API。
	/// @param fn 观测回调（`installed = true` 装、`false` 卸）；nullptr = 取消观测
	using HookObserverFn = void (*)(bool installed);
	void SetDesktopHookObserverForTests(HookObserverFn fn){ m_hookObserver = fn; }
```

> ⚠️ **`HookObserverFn` 的声明顺序**：必须位于首个使用点（`m_hookObserver` 成员）之前——与 `DragFinishFn` 同一教训（GCC 对成员函数形参不做延迟名字查找；放在 private 区会让 MinGW 报「has not been declared」）。

### 2.3 零改动区（显式声明）

| 区 | 为何不动 |
|---|---|
| 渲染四层（`Widget → PaintContext → CommandBuffer → Renderer → RenderingBackend`） | 与窗口层级正交 |
| `PlatformWindow` / `PlatformApplication` 的接口面 | 无新增能力（**纯实现层**） |
| Phase 12 的其余三消息拦截（`WM_NCCALCSIZE` / `WM_NCHITTEST` / `WM_NCACTIVATE`） | 与层级正交；仅 `WM_WINDOWPOSCHANGING` **分支重构**（§3.1） |
| Phase 13 命中委托 / Phase 14 托盘与拖入 | 无关 |
| `Window` / `Application` 层 | 零连接（层级能力完全在平台实现内闭环） |
| 构建系统 | `GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库；**无新文件**（改动全在既有文件） |

---

## 3. 实现分解

### 3.1 `TargetInsertAfter`：按档位分流（D8——本阶段的核心 diff）

**① 纯函数 `ResolveTarget`（v1.1：O3 **定为 C**——把最危险的 C2 路径变成可直接断言的真值表）**：

```cpp
HWND Win32PlatformWindow::ResolveTarget(WindowLayer layer, HWND desktop){

	// ── Normal 档：不参与 z 序维护 ⇒ 返回「跳过」哨兵 ──
	if (layer == WindowLayer::Normal){

		return nullptr;

	}

	// ── Bottom 档：既有语义（Phase 12 已实现）——逐位不变 ──
	if (layer == WindowLayer::Bottom){

		return HWND_BOTTOM;

	}

	// ── Desktop 档：紧贴桌面窗口正上方 ──
	if (desktop == nullptr || !IsWindow(desktop)){

		// ★ K8 / C2：桌面窗口不可用（explorer 重建窗口期）⇒ **跳过本次修改**。
		//   绝不可 fallback 到 HWND_BOTTOM —— 那会把 Desktop 档意外降级成 Bottom，
		//   是「比不重插更糟」的位置（desktop_spike.cpp:592-594 已显式防御同一陷阱）。
		return nullptr;

	}

	// 桌面窗口的上一位 = 「紧贴着它的那个位置」。
	// ⚠️ 返回 nullptr（桌面已在 z 序最顶）同样是**跳过** —— 此时「紧贴其上」不可能，
	//    而任何替代位置（HWND_BOTTOM / HWND_TOP）都违反契约。
	return GetWindow(desktop, GW_HWNDPREV);

}
```

> 📌 **可测化兑现**：五条判据（`Normal ⇒ nullptr` · `Bottom ⇒ HWND_BOTTOM` · `Desktop + nullptr ⇒ nullptr` · `Desktop + 非窗口 ⇒ nullptr` · `Desktop + 有效 ⇒ GetWindow(desktop, GW_HWNDPREV)`）**不需要窗口、不需要 explorer** ⇒ **T16-7** 按此行全覆盖（§7）。**这是 O3 取 C 而非 A 的全部理由**：A 的注入式测试缝要「假装系统 API」，C 只验证自己的判据。

**② 薄包装 `TargetInsertAfter`（实例方法——只做「取句柄」与「交给纯函数」两件事）**：

```cpp
HWND Win32PlatformWindow::TargetInsertAfter() const{

	// ⚠️ **只让 Desktop 档去查桌面句柄**——Normal / Bottom 不看它。
	//    这让 `WM_WINDOWPOSCHANGING` 在 **Bottom 档下不产生任何 `GetShellWindow()` 调用**
	//    （零新增成本：Bottom 是既有档位，不应因本阶段变慢）。
	const HWND desktop = IsDesktopLayer() ? FindDesktopWindow() : nullptr;

	return ResolveTarget(m_windowLayer, desktop);

}
```

**`WM_WINDOWPOSCHANGING` 分支重构**（`HandleMessage` 内，替换 B2 的 `:405-424`）：

```cpp
	case WM_WINDOWPOSCHANGING: {

		// ⚠️ 必须是「持续维护」而非一次性 SetWindowPos（其他程序会把我们顶下来）；
		// ⚠️ 只改 hwndInsertAfter，不碰 x/y/cx/cy/flags（否则会干扰最大化/还原几何）；
		// ⚠️ 契约边界：不承诺阻止第三方 SetWindowPos 造成的瞬时 z 序变化。
		// ★ Phase 16：判据由「!= Normal ⇒ 一律 HWND_BOTTOM」改为**按档位分流**（D8）。
		if ((wp->flags & SWP_NOZORDER) == 0){

			const HWND target = TargetInsertAfter();

			if (target != nullptr){

				wp->hwndInsertAfter = target;

			}

			// target == nullptr ⇒ **保持原值**（Normal 档 / 桌面句柄无效 / 桌面已最顶）——
			// 这是 K8 的代码级保证：Desktop 档在任何异常路径下都不会退化成 Bottom。

		}

		break;   // 走 DefWindowProc（几何变更仍由系统处理）

	}
```

**等价性核对**（零回归的结构保证）：

| 档位 | 旧行为 | 新行为 | 等价？ |
|---|---|---|---|
| `Normal` | 判据 `!= Normal` 不成立 ⇒ 不改 | `target == nullptr` ⇒ 不改 | ✅ **逐位等价** |
| `Bottom` | `hwndInsertAfter = HWND_BOTTOM` | `target == HWND_BOTTOM` ⇒ 同 | ✅ **逐位等价** |
| `Desktop` | ⚠️ 同 Bottom（**错误**——R1 从未实现） | 桌面窗口的上一位（或跳过） | **本阶段修正对象** |

### 3.2 `SetWindowLayer`：样式位 + 钩子（D3 / D11）

```cpp
void Win32PlatformWindow::SetWindowLayer(WindowLayer layer){

	// 配置期契约：与 chrome 三件套同一生命周期（不变）。
	if (m_shown){

		Logger::Log(LogLevel::Warning,
			L"WindowChrome: SetWindowLayer ignored after Show() - layer is config-time only");

		return;

	}

	if (m_windowLayer == layer){

		return;   // 幂等

	}

	m_windowLayer = layer;   // 语义状态恒记录用户请求（D-DESK-1：不随实现路径降级）

	// ── Phase 16 D1(A′) / D3：Desktop 档移除 WS_MINIMIZEBOX——「显示桌面」才不会最小化它 ──
	// 实测真因：Show Desktop 只最小化**可最小化**窗口 ⇒ 差异是**一位**，不是整个窗口形态。
	ApplyDesktopStyle(IsDesktopLayer());

	// ── Phase 16 D11：钩子生命周期（Set ⇒ 装 · 离档 ⇒ 卸；Hide/Show 不参与，见 §3.5）──
	SyncDesktopHook();

	// ── K2：原文案写 "spike pending"（spike 已于 2026-09-15 结项）——见 §3.6 ──
	if (IsDesktopLayer()){

		Logger::Log(LogLevel::Info,
			L"WindowChrome: WindowLayer::Desktop enabled - window is wedged above the desktop window");

	}

	if (m_hwnd == nullptr){

		return;   // 无窗口：仅记录状态（Show 后由 WM_WINDOWPOSCHANGING 自然生效）

	}

	// 切到 Bottom/Desktop：立即派发一次（后续由 WM_WINDOWPOSCHANGING 持续维护）；
	// 切回 Normal：不主动改变当前 z 序（交系统自然演化——避免「突然跳到最前」的反直觉效果）。
	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}
```

> ⚠️ **与旧实现的两处差异**：(a) Desktop 档不再走 `!= Normal ⇒ HWND_BOTTOM` 的通用分支，而是经 `TargetInsertAfter` 分流；(b) 新增样式位与钩子的同步。**`Normal` 档「不主动改变 z 序」的语义保持不变**（`TargetInsertAfter` 返回 `nullptr`）。

**`ApplyDesktopStyle`（幂等 + 可逆）**：

```cpp
void Win32PlatformWindow::ApplyDesktopStyle(bool desktop){

	if (m_hwnd == nullptr){

		return;   // 无窗口无从改样式（B3：构造期已创建，此处仅防御）

	}

	const LONG_PTR current = GetWindowLongPtrW(m_hwnd, GWL_STYLE);

	const LONG_PTR wanted = desktop
		? (current & ~static_cast<LONG_PTR>(WS_MINIMIZEBOX))
		: (current |  static_cast<LONG_PTR>(WS_MINIMIZEBOX));

	if (wanted == current){

		return;   // 幂等：已是目标值则不动手（避免多余的 WM_STYLECHANGING/CHANGED 往返）

	}

	SetWindowLongPtrW(m_hwnd, GWL_STYLE, wanted);

}
```

✅ **不需要 `SWP_FRAMECHANGED`（O1 已于 2026-09-19 实测关闭——§6.1）**：`WS_MINIMIZEBOX` **不参与非客户区几何计算**（只控制系统菜单项与按钮可用性）⇒ 改这一位**没有**任何需要系统重算的东西——实测改前 / 改后非客户区尺寸**逐位不变**（均为 `0 × 0`——v5a 与框架对齐后的复测；见 §6.1）。另：Borderless 档的 `SetChromeMode` 本就会在配置期派发一次 `SWP_FRAMECHANGED`（`Win32PlatformWindow.cpp:800-804`），那次重算在 `SetWindowLayer` **之前**发生，且不会因为改这一位而失效 ⇒ 此处**既不必要、也不应**重复派发。

### 3.3 维护钩子：`hook → 实例` 反查（本稿的核心设计点）

**问题**：`SetWinEventHook` 的回调签名**固定且无 user-data 参数**（B7：spike 用全局变量绕过——那是单窗口探针的便利）。框架须支持**多窗口**且**每窗口一个钩子**（D5）⇒ 回调执行时必须能找回「这是谁的钩子」。

**解法**：回调的**首参就是 hook 自身**（`HWINEVENTHOOK`）⇒ 以 hook 为键反查实例。这是**唯一不引入进程级「桌面窗口列表」**的做法（后者会让「谁该响应」变成广播语义，与 D5 冲突）。

```cpp
namespace {   // 匿名 namespace（.cpp 内——不进头、不出实现层）

/// @brief hook → 实例 映射（Phase 16 D5：每窗口一个钩子 ⇒ 回调必须能反查 owner）
/// @details 回调首参即 hook，故以 hook 为键。**替代方案（进程级窗口列表 + 广播）被否**：
/// 那要求回调遍历所有窗口并各自判断「是否该动」，把「谁的维护」变成全局语义。
/// ⚠️ 线程安全：`WINEVENT_OUTOFCONTEXT` 的回调在**注册钩子的那个线程**的消息循环中执行
/// ⇒ 与 Install/Uninstall 天然同线程 ⇒ **无需加锁**（写入契约 C8）。
std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*>& HookOwners(){
	static std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*> owners;
	return owners;
}

/// @brief 桌面类窗口判定（钩子过滤与 z 序判据共用同一份类名集合）
bool IsDesktopClassWindow(HWND hwnd){
	wchar_t buf[32]{};
	if (GetClassNameW(hwnd, buf, 32) == 0){ return false; }
	return wcscmp(buf, L"Progman") == 0 || wcscmp(buf, L"WorkerW") == 0;
}

}
```

**回调（静态——只做「反查 + 转发」）**：

```cpp
void CALLBACK Win32PlatformWindow::DesktopForegroundProc(HWINEVENTHOOK hook, DWORD event,
	HWND hwnd, LONG idObject, LONG /*idChild*/, DWORD /*thread*/, DWORD /*time*/){

	if (event != EVENT_SYSTEM_FOREGROUND || hwnd == nullptr || idObject != OBJID_WINDOW){

		return;

	}

	const auto it = HookOwners().find(hook);

	if (it == HookOwners().end()){

		return;   // 已卸除（回调与 UnhookWinEvent 之间的窗口期——见 §3.5 的注销顺序）

	}

	it->second->OnForegroundChanged(hwnd);

}
```

**成员侧（判类名 + 先判在位——§8.1②）**：

```cpp
void Win32PlatformWindow::OnForegroundChanged(HWND foreground){

	// 只有「桌面层被抬升」才需要跟随（Win+D / Show Desktop 的机制即此——spike §7.1）。
	if (!IsDesktopClassWindow(foreground)){

		return;

	}

	ReinsertAboveDesktop();

}

void Win32PlatformWindow::ReinsertAboveDesktop(){

	if (!IsDesktopLayer() || m_hwnd == nullptr || !IsWindow(m_hwnd)){

		return;

	}

	// ★ §8.1②（**正式设计输入**）：先判在位，已在位则什么都不做。
	// spike 的路线 E 是**无条件重插**，实测发现那正是 Win+D 瞬间「闪烁一下」的来源之一
	// （用户目视确认）。在位检查把这个抖动降到最小，且零额外成本。
	if (IsDirectlyAboveDesktop()){

		return;

	}

	const HWND target = TargetInsertAfter();

	if (target != nullptr){

		SetWindowPos(m_hwnd, target, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	}

}

bool Win32PlatformWindow::IsDirectlyAboveDesktop() const{

	// ★ C11（v1.1 语义澄清）：本函数**有三态** —— true = 已在位 / false = 未在位 / true = **不可判定**。
	//   不可判时返回 true 的含义是「**禁止无依据的 z-order 操作**」，**不是**「z 序已满足 C1」。
	//   名称保留（改成 IsDirectlyAboveDesktopOrCannotDetermine 只会让调用点更难读），语义由 C11 承担。
	if (m_hwnd == nullptr || !IsWindow(m_hwnd)){ return true; }   // 不可判 ⇒ 抑制动作（C11）

	const HWND desktop = FindDesktopWindow();

	if (desktop == nullptr || !IsWindow(desktop)){ return false; }   // 桌面不可用 ⇒ 未到位

	return GetWindow(desktop, GW_HWNDPREV) == m_hwnd;

}
```

**`SyncDesktopHook`（D11 四态表的唯一执行点）**：

```cpp
void Win32PlatformWindow::SyncDesktopHook(){

	const bool want = (IsDesktopLayer() && m_hwnd != nullptr);

	if (want && m_desktopHook == nullptr){

		m_desktopHook = SetWinEventHook(
			EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
			nullptr, &Win32PlatformWindow::DesktopForegroundProc,
			0, 0, WINEVENT_OUTOFCONTEXT);   // hInstance 可传 nullptr（D5 / K9）

		if (m_desktopHook != nullptr){

			HookOwners()[m_desktopHook] = this;

			if (m_hookObserver != nullptr){ m_hookObserver(true); }

		}

	}
	else if (!want && m_desktopHook != nullptr){

		SyncDesktopHookOff();

	}

}

void Win32PlatformWindow::SyncDesktopHookOff(){

	if (m_desktopHook == nullptr){ return; }

	// ★ 注销顺序：**先从映射表移除，再 UnhookWinEvent** —— 反序会让
	//   「回调已取出 owner 但对象正在析构」成为可能（UAF，契约 C6 的隐藏条件）。
	HookOwners().erase(m_desktopHook);

	UnhookWinEvent(m_desktopHook);

	m_desktopHook = nullptr;

	if (m_hookObserver != nullptr){ m_hookObserver(false); }

}
```

### 3.4 桌面窗口定位：不缓存（D6 / D7）

```cpp
HWND Win32PlatformWindow::FindDesktopWindow(){

	// D7：实测 `GetShellWindow()` ≡ `FindWindowW(L"Progman")`（四组独立运行一致）⇒ 取
	// **官方 API**：语义直述（「shell 的桌面窗口」）、无按类名枚举的成本。
	// ⚠️ 返回值仍须 `IsWindow` 校验——explorer 重建窗口期可能短暂失效（K8）；
	//    调用方（TargetInsertAfter / IsDirectlyAboveDesktop）一律按「不可用 ⇒ 跳过」处理。
	return GetShellWindow();

}
```

**D6 的收益**（写进注释）：不缓存 ⇒ 不需要 `RefreshDesktopHwnd` 那套「缓存 + 有效性检测 + 重定位」状态机——explorer 重建 `Progman` 之后，下一次查询**天然**拿到新句柄。**把一个待处理的状态问题变成了结构上不存在的问题**（需求稿 §4 D6 的理由）。

**成本**：实测 tick=0 时 **0.8 次/秒**（需求稿 §8 P3-4）⇒ 可忽略。

### 3.5 生命周期：`Release` / 析构 / Hide / Show（R3 · K11 · D11）

**`Release()` 修正（修 B4 的坑）**：

```cpp
bool Win32PlatformWindow::Release() noexcept{

	// ★ Phase 16：**脱钩必须在 hwnd 判空之前** —— 旧实现
	//   `if (m_hwnd==nullptr) return true;` 会让「hwnd 已空但钩子仍在」的路径
	//   跳过全部清理 ⇒ 回调打到已析构对象（UB）。钩子与 hwnd 是**独立资源**，不可同判。
	SyncDesktopHookOff();

	if (m_hwnd==nullptr){

		return true;

	}

	return DestroyWindow(m_hwnd) != FALSE;

}
```

**四态语义落地对照（D11）**：

| 触发 | 动作 | 实现处 |
|---|---|---|
| `SetWindowLayer(Desktop)` | **装** | `SetWindowLayer` → `SyncDesktopHook()` |
| `SetWindowLayer(Normal / Bottom)` | **卸** | 同上（`want == false` 分支） |
| **`Hide()`** | **不卸** | ✅ **无需代码**——`Hide()` 不触碰 `m_windowLayer` 与钩子（只是 `ShowWindow(SW_HIDE)`） |
| **`Show()`**（Hide 之后） | **不重装** | ✅ **无需代码**——同上（钩子从未卸除，`SyncDesktopHook` 也不会被调用） |
| **`Release()` / 析构** | **必卸** | `Release()` 顶部（本节修正） |

> ★ **「Hide 保留、Release 清理」的价值正是「零代码」**——需求稿 §4 D11 选择 A 而非 B，就是因为 A 不需要「重装」状态机：钩子在窗口不可见期间**照常存在且无害**（对隐藏窗口的 `SetWindowPos` 等价于空操作），而窗口重新可见时它**已经在工作**。

### 3.6 K2 过期文案修正

| 处 | 现状（过期） | 改为 |
|---|---|---|
| `Win32PlatformWindow.cpp:867-868` | `"WindowChrome: WindowLayer::Desktop not yet validated (spike pending) - executed as Bottom"` | **删除该 Warning**（Desktop 已实现，不再降级）；改为 `LogLevel::Info` 的启用提示（§3.2 已含） |
| `WindowLayer.h:24-31` | 「⚠️ 实现路线待 spike 验证；spike 未通过前此档位不承诺可用——调用后退化为 Bottom 语义并记 Warning 日志。」 | 改为「实现已落地」的事实陈述（**注释级改动**——§2.1「净增 0」仍成立） |
| `examples/ModelProbe/main.cpp:247` | `// spike 未通过 → 降级 Bottom + Warning` | ⚠️ **AI 不得改 `main.cpp`**——须用户显式授权（需求稿 R4 已登记） |

### 3.7 测试缝与纯函数（D9 / §8.2-8——v1.1：C2 由纯函数覆盖，缝只用于钩子计数）

**`HookObserverFn`**（§2.2 已给）：`SetWinEventHook` 依赖真实桌面环境，**自动化只能断言「装/卸被调用了」**，不能 mock 系统 API ⇒ 用观测缝计数。复刻 `DragFinishFn` 先例（B6）的**函数指针**形态——**不出实现层、保 `final`**。

**`TargetInsertAfter` 的纯逻辑测试**：返回值依档位而变，但 **Desktop 档依赖真实桌面窗口**（`GetShellWindow()`）⇒ 自动化可断言的是：

- `Normal` ⇒ `nullptr`（不修改）✅ 可自动
- `Bottom` ⇒ `HWND_BOTTOM` ✅ 可自动
- `Desktop` + **无效句柄** ⇒ `nullptr`（**C2 的关键分支**）——**v1.1 已解**：O3 定为 C ⇒ 拆出纯函数 `ResolveTarget(layer, desktop)`，把「桌面句柄」当**入参**直接注入 `nullptr` / 无效值，**无需任何测试缝**（§3.1 ①、T16-7）

---

## 4. 契约（C1–C12）

| # | 契约 | 验证方式 |
|---|---|---|
| **C1** | `Desktop` 档的目标 z 序位置 = `GetWindow(桌面窗口, GW_HWNDPREV)`（「紧贴桌面窗口正上方」的精确表述） | T16-1 / 手测 |
| **C2** | ★ 桌面窗口**不可用**（`nullptr` / `!IsWindow`）或**已处 z 序最顶** ⇒ **不修改** `hwndInsertAfter`；**任何路径下都不得退化为 `HWND_BOTTOM`** | **T16-7**（`ResolveTarget` 纯逻辑断言）/ 代码审查 |
| **C3** | `Normal` 档**不修改** z 序（`TargetInsertAfter` 返回 `nullptr`） | T16-5 |
| **C4** | `Bottom` 档语义**逐位不变**（`HWND_BOTTOM`）——`Bottom` 与 `Normal` 的行为在本阶段零回归 | **T16-1（★ 补 Phase 12 的空缺）** |
| **C5** | `Desktop` 档移除 `WS_MINIMIZEBOX`；离开 Desktop 档**补回**（幂等 + 可逆） | T16-2 / T16-3 |
| **C6** | 钩子生命周期：Set ⇒ 装 · 离档 ⇒ 卸 · `Hide` ⇒ **不卸** · `Show` ⇒ **不重装** · `Release`/析构 ⇒ **必卸**；且**注销顺序 = 先摘映射、后 `UnhookWinEvent`** | T16-4（经 `HookObserverFn` 计数） |
| **C7** | 桌面窗口句柄**不缓存**——每次查询即时重定位（⇒ explorer 重建**无需**状态机） | 代码审查 + 代码审查（`FindDesktopWindow` 无成员缓存字段） |
| **C8** | 钩子回调在**注册线程**执行（`WINEVENT_OUTOFCONTEXT`）⇒ 与安装/卸除同线程 ⇒ **进程内无锁** | 代码审查（MSDN 语义） |
| **C9** | `m_windowLayer` 语义状态**不随实现路径降级**（Phase 12 `D-DESK-1` 延续） | 现有 + 代码审查 |
| **C10** | `SetWindowLayer` 仍为**配置期 API**（`Show()` 后 Warning + 忽略）——本阶段**不松开** | 补配置期反例用例（T16-6） |
| **C11** | ★ `IsDirectlyAboveDesktop()` 的**不可判定**返回值（`true`）语义 = **「禁止无依据的 z-order 操作」**，**不是**「z 序已满足 C1」（三态：已在位 / 未在位 / 不可判 ⇒ 抑制） | 代码审查 |
| **C12** | ★ **钩子是与 `HWND` 相互独立的资源**——`Release()` 的判空**不得**覆盖钩子脱除；任何「一个判空条件同时代表两种资源」的写法一律禁止（**B4 的坑即此**，与 Phase B 的 Window 所有权教训同族） | T16-4 / 代码审查 |

---

## 5. 影响面

| 类别 | 项 |
|---|---|
| **新增 Public 头** | **0** |
| 修改 Public 头 | **1**（**仅注释**）：`WindowLayer.h:24-31` 的「spike 未验证 / 退化为 Bottom」表述 → 实现已落地的事实陈述 |
| 修改 Internal 头 | `src/Platform/Win32/Win32PlatformWindow.h`：+2 成员（`m_desktopHook` / `m_hookObserver`）· +12 方法（6 功能 + **2** 静态 + **1 纯函数 `ResolveTarget`（public static）** + 1 强制卸 + 1 内联判据 + 1 测试缝 setter）——v1.1 更正：原「+10」把两个静态方法（`FindDesktopWindow` / `DesktopForegroundProc`）记成了一个 |
| 修改实现 | `src/Platform/Win32/Win32PlatformWindow.cpp`：`WM_WINDOWPOSCHANGING` 分支重构 · `SetWindowLayer` 重构 · `Release` 顶部插入脱钩 · +新方法体 · 匿名 namespace +2（`HookOwners()` / `IsDesktopClassWindow`）· +`<unordered_map>` |
| 测试 | `src/Tests/WindowChromeTests.cpp` 追加 **T16-1..T16-6**（+ **T16-7** 覆盖 `ResolveTarget` 真值表——**不需要窗口**，见 §7；是否新建文件**归详设**）；若新建文件则 `RunAllTests.h` / `RunAllTests.cpp` 手工接线 |
| ⚠️ **测试替身** | **零同步**——本阶段**不新增任何 pure virtual**（B9 的 3 个实现者不受影响） |
| 构建 | **零改动**（无新文件；`GLOB_RECURSE … CONFIGURE_DEPENDS` 自动入库） |
| **工具（不进仓库）** | `.workbuddy/spike/desktop_layer_probe.cpp` **v5a**——O1 实测与后续**验收手测**复用；**四点**对齐框架（窗口样式 · 三处 NC 拦截 · **配置期 `SWP_FRAMECHANGED`** · DPI 感知）；`--auto` 自动注入 Win+D 多轮并自裁定（`VERDICT:` 行） |
| Demo | `examples/ModelProbe/main.cpp:247` 的过期注释——**须用户授权**（AI 不得改 `main.cpp`） |
| 文档 | 本稿 + 详设；**两处 README 索引都要改**（skill 条 77）· **需求稿 K13 勘误回写**（§1.3）· `desktopnest-roadmap.md` §5 G-1 可标 ✅（实现并验收后） |
| 明确不动 | §2.3 已列 |

---

## 6. 开放决策点（O1–O5——**O1 已实测关闭**，其余待详设 / 验收收敛）

| # | 决策 | 选项 / 倾向 | 依据与理由 |
|---|---|---|---|
| **O1** | `ApplyDesktopStyle` 是否需要 `SWP_FRAMECHANGED` | ✅ **已关闭：不需要**（2026-09-19 实测 + 框架先例） | **证据链三条**：① `WS_MINIMIZEBOX` **不参与非客户区几何**——实测改这一位前后 `non-client` **逐位不变**（均为 `0 × 0`——v5a 与框架对齐后的复测；见 §6.1）⇒ 没有需要系统重算的对象；② **加与不加行为完全一致**——三组各 3 轮 Win+D **全 PASS**（`iconic` 恒 0 · `rect` 不变）；③ **框架已有先例**：`SetChromeMode(Borderless)` 在配置期就靠 `SWP_FRAMECHANGED` 通知系统重算 NC（`Win32PlatformWindow.cpp:800-804`）——那次重算发生在改样式**之前**，且不因改这一位失效。详见 §6.1（含一处**探针保真度缺陷**的如实记录） |
| **O2** | `WS_EX_TOOLWINDOW` 是否加 | **倾向不加**（v1.1 **由「倾向加」反转**） | `TOOLWINDOW` 改变的是**任务栏 / Alt+Tab / 激活 / 系统菜单**这一组窗口语义，**不属本阶段需求**（需求稿里 Desktop 档的需求只有四条：z 序 / Win+D 可见 / explorer 重建 / 交互不降级）。「桌面常驻物不该占任务栏位」这个判断**本身成立**，但它是一条**尚未立项的需求** ⇒ 按 YAGNI 等**真实消费者**驱动（DesktopNest 明确要求「不出现在任务栏」时再开）。**本阶段的原则是「已经实测的最小改动」，不是「顺手把桌面常驻行为做完整」** |
| **O3** | `TargetInsertAfter` 的 C2 分支（句柄无效 ⇒ 跳过）如何自动化 | **定为 C**（v1.1 收敛——A / B 不再保留）：拆出**纯函数** `static HWND ResolveTarget(WindowLayer layer, HWND desktop)` | C2 是**本阶段最危险的路径**（写错即静默降级成 Bottom）⇒ 必须自动化。**落点零新文件**：作为 `Win32PlatformWindow` 的 **public static**（声明在既有内部头，定义在既有 `.cpp`）。**测试可达性已有先例**：`DropFilesTests.cpp:13` 即 include 内部头（B10）⇒ 无需新头、无需测试缝、无需 mock 系统 API。详见 §3.1 ① 与 T16-7 |
| **O4** | `HookOwners()` 的容器形态 | **A** `std::unordered_map<HWINEVENTHOOK, Win32PlatformWindow*>`（本稿）· **B** 静态单链表 / 小数组 | 倾向 **A**（O(1)、语义直白）；窗口数少（YAGNI：无大规模多窗口消费者），B 的省内存无意义。**v1.1 采评审意见：保持 A**；须详设确认：`<unordered_map>` 只在 `.cpp`（**不入公共头** ⇒ 零传播） |
| **O5** | `GetShellWindow()` 的四工具链一致性 | 待验证 | 需求稿 §8-8（K9）的顺延项。`GetShellWindow` 是 Win2000+ 的 winuser API（四工具链均应有声明）；**须四工具链各构建一次确认**（归验收） |

### 6.1 O1 实测结果（2026-09-19 已跑——**已关闭**）

**结论**：`ApplyDesktopStyle` **不需要** `SWP_FRAMECHANGED`——只改样式位即可。详设据此定稿。

**三组实测**（`desktop_layer_probe.exe --auto --secondary`，各 3 轮 Win+D，判据 =「Win+D 生效中仍可见」）：

| 组 | 窗口形态 | Win+D 判决 | `iconic` | `rect` | 样式回读 |
|---|---|---|---|---|---|
| 1 | `--strip-after`（**创建后**改样式，逐位复刻框架次序） | **3/3 PASS** | 恒 0 | 不变 | `0x04CF0000 → 0x04CD0000`（`WS_MINIMIZEBOX` 1 → 0） |
| 2 | `--strip-after --framechanged`（对照：补 `SWP_FRAMECHANGED`） | **3/3 PASS** | 恒 0 | 不变 | 同上 |
| 3 | `--style overlapped-nominbox`（**创建期**烘焙，既有参考组） | **3/3 PASS** | 恒 0 | 不变 | `0x14CD0000`（创建即无该位） |

⇒ **加与不加 `SWP_FRAMECHANGED` 行为完全一致**，且组 1 与组 3（创建期烘焙）也一致。

**决定性读数**：组 1 里「改样式前后**非客户区尺寸逐位不变**」⇒ 改这一位**没有引起任何几何变化**，
因此不存在「需要通知系统重算」的对象。这与 `WS_MINIMIZEBOX` 的语义一致（它只控制系统菜单项与按钮可用性，
不参与 NC 计算）。

**★ v5a 复测（探针与框架四点对齐后，2026-09-19）**——上面那条决定性读数出自**未对齐**的探针（A / B 点是 `16 × 39`，那个值在框架里不会出现，见下节），故补测一遍，让证据出自**已对齐**的量具：

| 测量点 | 时机 | `window` | `client` | **`non-client`** |
|---|---|---|---|---|
| **A** | 创建后 + 配置期 `SWP_FRAMECHANGED`（= 框架 `SetChromeMode` 之后） | `420 × 280` | `420 × 280` | **`0 × 0`** |
| **B** | 改样式后（`WS_MINIMIZEBOX` 1 → 0）、**Show 前** | `420 × 280` | `420 × 280` | **`0 × 0`** |
| **C** | `Show()` 后 | `420 × 280` | `420 × 280` | **`0 × 0`** |

⇒ **A = B = C，且非客户区恒为 0** ⇒ **改样式位对几何零影响**（这正是「不需要 `SWP_FRAMECHANGED`」的判据，
而且是在**与框架等价的**形态下取得的）；同一轮 Win+D 仍 **3/3 PASS**。

> 📌 **两组早期数据仍有效**：组 2 / 组 3 是在 v5a 之前跑的，但它们的判决依据是 `iconic` / `rect` / 像素
（**与 NC 读数无关**）——保真度缺口只影响「非客户区这个读数」，不影响窗口形态与 z 序，故其 PASS 结论无需重跑。

**框架先例（补强）**：`SetChromeMode(Borderless)` 已在配置期派发 `SWP_FRAMECHANGED`（`Win32PlatformWindow.cpp:800-804`）。
⇒ Borderless + Desktop 的真实调用序列是：

```
CreateWindowExW(WS_OVERLAPPEDWINDOW)
    → SetChromeMode(Borderless)      【派发 SWP_FRAMECHANGED ⇒ NC 归零】
    → SetWindowLayer(Desktop)        【改一位（NC 中性）+ 装钩子】
    → Show()
```

⇒ `Desktop` 档**既不需要、也不应**重复派发。

**⚠️ 一处必须如实记录的探针保真度缺陷（自审发现）**

探针 v1–v5 声称「三处 NC 拦截**逐字照抄** `Win32PlatformWindow.cpp:274-402`」，但**漏了框架的第四步**——
`SetChromeMode` 的那一次 `SWP_FRAMECHANGED`。后果：**探针的 Borderless ≠ 框架的 Borderless**（框架在配置期
就把 NC 归零了，探针没有）⇒ v4/v5 打印的 `non-client = 16 × 39`（A / B 点）**在框架里根本不会出现**。

| 项 | 说明 |
|---|---|
| **对结论的影响** | **无**。P0（Win+D 可见性）看 `iconic` / `rect` / 像素；O1 看「改样式前后是否变化」这个**相对量**——两者都不依赖该绝对值 |
| **已修** | 探针 **v5a** 补上该派发（位置与框架一致：创建后、Show 前），新增 `--no-chrome-framechanged` 可复现旧行为；并新增三点量具 **A（创建后）/ B（改样式后）/ C（Show 后）** |
| **教训** | 宣称「逐字照抄」时必须比对**完整调用序列**，而不是只比对那几个消息处理函数——本轮漏的是**框架自己的一步**（skill 条 76 的又一实例） |

**判据修正（v1.1 的写法有误）**：v1.1 把「`non-client` delta ≠ 0」读作「系统自己重算了 ⇒ 仍不需要」——**机制上说反了**：
delta ≠ 0 只出现在**我们显式派发 `SWP_FRAMECHANGED`** 的那一组（组 2，`16×39 → 0×0`），它是**派发的效果**，
不是系统自发行为。正确判据是：**「改样式本身是否引起几何变化」（组 1 的 delta = 0 ⇒ 否）** + **「加与不加是否行为一致」（是）**。

---

## 7. 测试方向（T16-1..T16-7——用例数 210 → 210+N，N 归详设）

| # | 用例 | 断言 | 层级 |
|---|---|---|---|
| **T16-1** ★ | **`Bottom` 档回归**（**补 Phase 12 的空缺**——§1.3） | `SetWindowLayer(Bottom)` + Show + 泵消息 ⇒ 窗口位于普通窗口层底部（与同进程参照窗口比 z 序：本窗口在参照之下） | 自动 |
| **T16-2** | `Desktop` 档移除 `WS_MINIMIZEBOX` | `SetWindowLayer(Desktop)` ⇒ `GetWindowLongPtrW(GWL_STYLE) & WS_MINIMIZEBOX == 0`，且其余位（`WS_CAPTION` / `WS_THICKFRAME` / `WS_SYSMENU` / `WS_MAXIMIZEBOX`）**逐位保持** | 自动 |
| **T16-3** | 样式可逆 + 幂等 | `Desktop → Normal` ⇒ `WS_MINIMIZEBOX` **补回**；重复设同档位被幂等分支拦下 | 自动 |
| **T16-4** | 钩子生命周期（C6） | 经 `HookObserverFn` 计数：`Set(Desktop)` ⇒ `true`×1；`Set(Normal)` ⇒ `false`×1；`Hide()` → `Show()` ⇒ **计数不变**；`Release()` ⇒ `false`×1（且不重复） | 自动 |
| **T16-5** | `Normal` 档不参与维护（C3） | `Normal` 档下触发 `WM_WINDOWPOSCHANGING` ⇒ `hwndInsertAfter` 保持调用方给的值 | 自动（`SendMessageW` 直发 + 观察 z 序邻居） |
| **T16-6** | 配置期契约（C10） | `Show()` 后调 `SetWindowLayer` ⇒ 档位**不变** + Warning（沿用 `RuntimeApiRejectedBeforeShow` 同款手法） | 自动 |
| **T16-7** ★ | **`ResolveTarget` 真值表**（O3 = C 的兑现——**不需要窗口 / explorer**） | 五条逐条断言：`Normal ⇒ nullptr` · `Bottom ⇒ HWND_BOTTOM` · `Desktop + nullptr ⇒ nullptr` · `Desktop + 无效句柄 ⇒ nullptr` · `Desktop + 有效句柄 ⇒ GetWindow(desktop, GW_HWNDPREV)`（用本进程真实窗口作「有效句柄」样本） | 自动 |
| **手测** | 真机六判据 + 闪烁 | 复用 `.workbuddy/spike/desktop_layer_probe.cpp` 的 `--auto` 三态采样；ModelProbe `--layer desktop` | 手测（需求稿 §6 L3） |

> 📌 **测试边界的自洽（v1.1 修正——原表述超前于本稿状态）**：v1.0 时只有 `Normal` / `Bottom` 两支可自动断言，**C2（`Desktop` + 无效句柄）当时依赖未定的 O3**（§3.7 也照实写了），故原句「唯一无法自动化的是 C1」**与 §3.7 自相矛盾**（评审第 13 点）。**v1.1 把 O3 定为 C 后矛盾消除**，目标态为：

| 契约 | 自动化 |
|---|---|
| **C1**（真实 z 序位置 = 紧贴桌面之上） | **真机手测**——依赖 explorer 的 `Progman`，本阶段**唯一**不可自动项 |
| **C2**（不可用 ⇒ 不修改） | **自动**（经 `ResolveTarget` 纯函数——O3 = C，T16-7） |
| **C3–C12** | **自动** |

> 这是需求稿 §6 三层分层的直接落实。

---

## 8. 修订记录

- v1.0（2026-09-19）初步设计初稿：① **§1.1** 需求阶段已定决策汇总（D1/D2/D3/D4/D5/D6/D7/D8/D11 + §8.1②）；② **§1.2 代码基线 B1–B9**（全部带行号）；③ **§1.3 ★ 需求稿勘误**——K13 声称「`Bottom` 档已有跨工具链验证的 z 序用例」**实核不成立**（`WindowChromeTests.cpp` 9 用例零 `WindowLayer` 覆盖）⇒ R2 零回归缺测试保护，本稿 **T16-1 补**；④ **§1.4 §8.2 八问的答案索引**；⑤ **§2 头文件改动**——**公共头净增 0**（逐头确认 + **不新增 pure virtual ⇒ 3 个替身零同步**）+ `Win32PlatformWindow.h` 内部头增量；⑥ **§3 实现分解**——`TargetInsertAfter` 按档位分流（含 **`nullptr` 前置检查哨兵的语义澄清**：**不是**「插到最底」）+ 等价性核对表（`Normal` / `Bottom` **逐位等价**）+ `ApplyDesktopStyle` 幂等可逆 + **`hook → 实例` 反查**（本稿核心设计，**否**进程级广播方案）+ 重插前判在位（§8.1② 落实）+ `Release` 顶部脱钩（**修 B4 的 `hwnd` 空判陷阱**）+ 四态语义**「零代码」**对照 + K2 文案修正；⑦ **§4 契约 C1–C10**；⑧ **§5 影响面**（含「修改 Public 头 1 处但仅注释」与两处索引同改的提醒）；⑨ **§6 开放决策点 O1–O5**；⑩ **§7 测试 T16-1..6**（含 ★ T16-1 补 Phase 12 的空缺）。待评审。

- v1.1（2026-09-19）**外部评审处置（「基本通过，带 3 项收敛进入详细设计」——14 条中 12 条采纳 / 1 条已完成 / 1 条转为实验）**：① **状态行**改为「评审通过（带 3 项收敛）」+ 标注 **O1 为进详设的闸门**（skill 条 74：状态由评审结论决定）；② **★ O3 定为 C**——拆出**纯函数** `ResolveTarget(layer, desktop)`（§2.2 public static 声明 + §3.1 ① 定义），**C2 由「需 O3 / 代码审查」升级为 T16-7 全自动**；§3.7 的「需注入状态 ⇒ O3」同步作废；③ **★ O2 倾向由「加」反转为「不加」**——`TOOLWINDOW` 改的是任务栏 / Alt+Tab / 激活 / 系统菜单语义，**不属本阶段需求**；「桌面常驻物不该占任务栏」成立但属**未立项需求**，等真实消费者驱动；④ **新增契约 C11 / C12**——C11 钉死 `IsDirectlyAboveDesktop()` **不可判定**返回值（`true`）的语义是「禁止无依据的 z-order 操作」而非「已满足 C1」（三态，§3.3 已内联注释）；C12 钉死「**钩子独立于 `HWND`**」（`Release()` 判空不得覆盖脱钩——B4 的根因，与 Phase B 所有权教训同族）；⑤ **修 §5 方法计数**（原「+10」把 `FindDesktopWindow` / `DesktopForegroundProc` 两个静态记成了一个 ⇒ **+12**）；⑥ **修 §7 自洽**（原「唯一不可自动 = C1」与 §3.7 矛盾 ⇒ 改为契约级自动化对照表，并新增 **T16-7**）；⑦ **补 §1.2 B10**——`DropFilesTests.cpp:13` 已 include 内部头，作为 O3 取 C 的前提证据；⑧ **★ O1 由「倾向不需要」升级为「详设前置实验」**——并指出本稿此前推理的**缺口**：既有 4 组是**创建期**设样式，框架是**创建后** `SetWindowLongPtrW`，**两者不等价** ⇒ 新增 **§6.1 实验方案**（探针 v4 `--strip-after` / `--framechanged`，打印样式回读 + 非客户区尺寸前后对比）。

- v1.2（2026-09-19）**O1 实测关闭（进详设的闸门已解除）**：① **§6.1 整节改写**为「实测结果」——三组 × 3 轮 Win+D 全 PASS、加与不加 `SWP_FRAMECHANGED` 行为一致、组 1 的「改样式前后 NC 逐位不变」为决定性读数 ⇒ **`ApplyDesktopStyle` 不需要 `SWP_FRAMECHANGED`**；② **§3.2 的 ⚠️ 待验段改为定案**（含「Borderless 的 `SetChromeMode` 已在配置期派发过一次，且不因改这一位失效」的理由）；③ **§6 O1 行改为「已关闭」**并附证据链；④ **新增 B11**（框架既有先例 `Win32PlatformWindow.cpp:800-804`）；⑤ **§5 新增「工具」行**（探针 v5a 四点对齐框架，后续验收复用）；⑥ **如实记录一处探针保真度缺陷**——v1–v5 漏了框架 `SetChromeMode` 的 `SWP_FRAMECHANGED`，故其 `non-client = 16×39` 读数在框架里不会出现（**对 P0/O1 结论均无影响**，已修于 v5a）；⑦ **修正 v1.1 的判据表述**——把「delta ≠ 0 = 系统自发重算」更正为「delta ≠ 0 是**我们派发**的效果，正确判据是『改样式本身有无几何变化』+『加与不加行为是否一致』」。

- v1.3（2026-09-19）**v5a 复测补记（结论不变，证据换源）**：探针修好保真度缺口后（补上框架 `SetChromeMode` 的 配置期 `SWP_FRAMECHANGED`），复测组 1 ⇒ 新增三点量具 **A（创建后）/ B（改样式后、Show 前）/ C（Show 后）** 结果**全部 `non-client = 0 × 0`**、Win+D **3/3 PASS** ⇒ O1 结论不变。**为何要补**：v1.2 的「决定性读数」取自**未对齐**的探针（其 `16 × 39` 在框架里不会出现）——让证据出自**已对齐的量具**，文档才自洽。另注明组 2 / 组 3 的早期 PASS **无需重跑**（其判决依据 `iconic` / `rect` / 像素与 NC 读数无关）。