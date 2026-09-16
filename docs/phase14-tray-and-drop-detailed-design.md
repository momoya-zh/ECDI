# Phase 14 托盘与拖入接缝 详细设计（v1.0）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-16
> 状态：待评审
> 前置：`phase14-tray-and-drop-preliminary-design.md` **v1.1**（外部评审「通过，可进详设」· 2026-09-16）
> 一句话：把初设的 3 新头 + 7 头修改落成**可直接施工的逐文件最小 diff 规格**（每处标注动哪几行 / 不动哪些行）+ 平台实现全文规格 + 状态机完整转移表（含失败分支）+ 测试规格与工程注册——**不换初设路线**（不动渲染四层 / Window 所有权契约 / Phase 12 拦截骨架 / Phase 13 命中委托）。
> v1.0：初稿（§1 实施前核实与精化 · §2 逐文件改动清单 · §3 关键实现规格 · §4 工程注册 · §5 测试规格 · §6 契约汇总 · §7 已知局限 · §8 验收清单 · §9 修订记录）

---

## 1. 实施前核实与精化（初设 v1.1 → 本文的增量）

### 1.1 ★ 重大更正：O-6 的载体**已存在**——初设 §2.10 作废

实施前核实（本轮勘察）发现，初设 v1.0 的 O-6 前提「`Window*` 无法在平台层获得」是**错误断言**（勘察遗漏），v1.1 据此新增的 §2.10（`PlatformWindowHost` 加 `virtual Window& GetWindow() noexcept = 0`）**为多余设计，本文作废之**：

| 证据 | 内容 |
|---|---|
| `PlatformWindowHost.h:32-34` | **既有纯虚** `virtual Window* GetWindow() const noexcept = 0;`——注释原文：*"事件来源窗口（翻译器构造 Event 需 Window\*——Event.h:50 绑定）……平台层经此接口拿指针，不 include Window.h"*——**与拖入场景逐字对应，7.1 时代即为此设计** |
| `Window.h:228` | `Window* GetWindow() const noexcept override;`——已有实现 |
| `EventTests.cpp:55` | `FakeHost` 已有 override（返回 `nullptr`）⇒ **零改动** |

**影响面修正（相对初设 v1.1）**：

| 项 | 初设 v1.1 | 本文（更正后） |
|---|---|---|
| `PlatformWindowHost.h` | +1 纯虚（§2.10） | **零改动** |
| 修改 Public 头 | 7 个 | **6 个** |
| 测试替身同步 | 3 处 | **2 处**（仅 `TestPlatformWindow` ×2 补 Hide/SetFileDropEnabled） |
| `FakeHost` | 补 1 override | **零改动** |

§3.6 的事件构造据此**更简**：`DropFilesEvent event(m_host.GetWindow(), std::move(paths), pt.x, pt.y);`——`m_host.GetWindow()` 返回的正是 `Window*`，与构造签名（`Window* window, ...`）直接匹配。

> 📌 **记因（防再犯）**：O-6 三方案的提出基于「平台层拿不到 Window*」的未验证前提。**interface 是否已有某能力，必须 grep 接口头本身**（本例 grep `GetWindow` 一发即中），不能只 grep 实现者清单——实现者清点是加纯虚时的动作（条 33），**接口存在性是另一件事**。

### 1.2 初设拍板项兑现（v1.1 §8.1 的 7 项 → 本文落点）

| 拍板 | 本文落点 |
|---|---|
| O-1（sink 清理 = A） | §2.7（`~Application` 显式析构体首行 `SetTrayEventSink(nullptr)`） |
| O-2（R13 保留） | §2.7 / §3.6（单点接线）+ §5 T14-8 + A4 手测两模式 |
| O-4（两方法 API） | §2.5（`SetTrayIcon` / `RemoveTrayIcon`） |
| O-5（v4 坐标） | §3.3 终版翻译规则（含 ContextMenu 兜底）；**坐标空间按屏幕坐标施工**，A5 实测确认 |
| O-6（方案 C） | **修正落地**——复用既有 `GetWindow()`（§1.1），零接口改动 |
| O-7 / O-8（不做） | §7 局限记账 |

### 1.3 与初设的其余差异

- **初设 §2.7 的 `OnTrayEvent override` 从 `Application.h` 移除**——`Application` 对应用级事件**无默认动作**，`EventRouter` 的空默认实现即正确；消费者（用户子类）直接 override。`OnDropFiles` 则**必须**由 `Application` override（HitTest + Bubbling 派发是框架职责，§3.5）。
- 初设 §2.5 的 `#include "ECDI/EventSystem/Application/TrayEvent.h"` 保留（sink 签名需要）。

---

## 2. 逐文件改动清单（最小 diff 规格）

> 阅读方式：每个「修改」小节**只列真正要动的那几行** + 前后对照；**未列出的行一律保持现状**（最小修改面纪律）。行号以当前工作区为准，实施时以锚点文字定位。

### 2.1 新增：`include/ECDI/Application/TrayIcon.h`（Public 头 86 → 87）

以初设 §2.1 的头全文为准施工，无修订。要点重申：`kDefaultIconResourceId = 102`（与 `Win32WindowClass.cpp:21` 的 `kAppIconId` 对齐，**两侧同步修改**纪律）；`TrayMenuItem::id <= 0` 由平台层忽略。

### 2.2 新增：`include/ECDI/EventSystem/Application/TrayEvent.h`（87 → 88）

以初设 §2.2 为准，**含 v1.1 的 ContextMenu 兜底注释**。新目录 `EventSystem/Application/`（与 `EventSystem/Window/` 对称）。

### 2.3 新增：`include/ECDI/EventSystem/Window/DropFilesEvent.h`（88 → 89）

以初设 §2.3 为准。

### 2.4 修改：`include/ECDI/Platform/PlatformWindow.h`

**落点**：`GetWindowState()`（`:120`）之后、类尾 `};` 之前插入：

```cpp
	// ── Phase 14：窗口显示控制 / 文件拖入（运行期——Show() 之后有效）────

	/// @brief 隐藏窗口（R12——Show 的对称；**不销毁 HWND**）
	/// @details 与 Release() 的语义边界：Hide = 资源存活仅不可见（可再 Show）；
	/// Release = 销毁 HWND（不可逆）。**不改 m_shown**（运行期标记不因隐藏回退）。
	/// @pre Show() 之前调用记 Warning 并忽略（与 Minimize 同组）
	virtual void Hide() = 0;

	/// @brief 启用 / 停用本窗口的文件拖入（R8；默认关闭）
	/// @details 平台实现为 DragAcceptFiles 薄封装；幂等、可重复调用。
	/// @pre Show() 之前调用记 Warning 并忽略（O-3 拍板：运行期分组——架构一致性选择）
	virtual void SetFileDropEnabled(bool enabled) = 0;
```

### 2.5 修改：`include/ECDI/Platform/PlatformApplication.h`

**① 头部 include 区**（`#include <functional>` 之后）追加：

```cpp
#include "ECDI/Application/TrayIcon.h"
#include "ECDI/EventSystem/Application/TrayEvent.h"
```

**② 类体**：`RequestExit()`（`:25`）之后、`protected:` 之前插入初设 §2.5 的三个纯虚（`SetTrayIcon` / `RemoveTrayIcon` / `ShowTrayMenu`）与 `SetTrayEventSink` 注入方法；`PerformDeferredCleanup()` 之后追加 `EmitTrayEvent`；private 区追加 `m_trayEventSink`。以初设 §2.5 代码块为准施工。

### 2.6 修改：`include/ECDI/Window/Window.h` + `src/Window/Window.cpp`

**Window.h**：`Show()`（`:57`）之后插入：

```cpp
		/// @brief 隐藏窗口（R12——Show 的对称；不销毁 HWND，可再次 Show）
		/// @pre Show() 之前调用记 Warning 并忽略（运行期 API——同 Minimize）
		void Hide();

		/// @brief 启用 / 停用本窗口的文件拖入（R8；默认关闭）
		/// @pre Show() 之前调用记 Warning 并忽略
		void SetFileDropEnabled(bool enabled);
```

**Window.cpp**：`Show()` 实现之后追加两行转发（`m_platformWindow->Hide()` / `m_platformWindow->SetFileDropEnabled(enabled)`）。

### 2.7 修改：`include/ECDI/Application/Application.h` + `src/Application/Application.cpp`

**Application.h**：
- public 区（`Exit()` 之后）追加：`SetTrayIcon(const TrayIconOptions&)` · `RemoveTrayIcon()` · `ShowTrayMenu(const TrayMenu&)` · `SetQuitOnLastWindowClosed(bool)` 四个声明（doc 注释以初设 §2.7 为准）。
- protected 区追加：`void OnDropFiles(const DropFilesEvent& event) override;`（**不加 `OnTrayEvent` override**——§1.3）。
- private 区追加：`bool m_quitOnLastWindowClosed = true;`
- 顶部追加 `class TrayEvent;` 等前置声明按需（`TrayIconOptions` / `TrayMenu` 为值参数，需 include `ECDI/Application/TrayIcon.h`）。

**Application.cpp**（四处）：

**① 构造体**（`:26-32`）——sink 注册（O-1 契约 A）：

```cpp
Application::Application()
	: m_platformApplication(std::make_unique<Win32PlatformApplication>()){

	// 7.1.5：延迟清理逻辑注册给平台循环（既有，不动）
	m_platformApplication->SetDeferredCleanup([this]{ ProcessDeferredDestroy(); });

	// Phase 14 D3：托盘事件上行通道（契约 A——构造期注册，早于一切上行可能；
	// sink 内只调 OnEvent——经既有 EventRouter 分派到 OnTrayEvent 虚方法）
	m_platformApplication->SetTrayEventSink([this](const TrayEvent& event){
		OnEvent(event);
	});

}
```

**② 析构体**（`:34`）——显式化（O-1 拍板：谁注册谁清理）：

```cpp
Application::~Application(){

	// Phase 14 O-1：先断托盘事件上行，再让 m_platformApplication 析构——
	// 防止宿主窗口销毁过程中的 Shell 回调访问已析构的 Application（§3.4 时序）
	m_platformApplication->SetTrayEventSink(nullptr);

}
```

**③ `OnWindowDestroyed`**（`:115-118`）——**唯一改动行**（R13）：

```cpp
	// 所有窗口都关闭了，退出消息循环（R13：常驻应用可关闭隐式退出——默认 true = 零行为变更）
	if (m_windows.empty() && m_quitOnLastWindowClosed){

		Exit();

	}
```

**④ 文件尾部**追加四个透传实现 + `OnDropFiles` 派发（全文见 §3.5）。

### 2.8 修改：`EventType.h` + `EventRouter.h` + `EventRouter.cpp`

- `EventType.h`：`WindowStateChanged` 之后插 `DropFiles,`；`CharInput` 之后新组 `// ── 应用事件（Phase 14：应用级——无来源窗口）` + `Tray,`。
- `EventRouter.h`：protected 区追加 `OnDropFiles` / `OnTrayEvent` 两个虚方法（默认空实现——初设 §2.9），顶部补 `class DropFilesEvent;` / `class TrayEvent;` 前置声明。
- `EventRouter.cpp`：Dispatch 链**末尾**追加两个分支（模式与既有完全同款）：

```cpp
	// ── Phase 14：拖入 / 托盘 ────────────────────────
	dispatcher.Dispatch<DropFilesEvent>([this](const DropFilesEvent& e){

			OnDropFiles(e);

		});

	dispatcher.Dispatch<TrayEvent>([this](const TrayEvent& e){

			OnTrayEvent(e);

		});
```

### 2.9 修改：`src/Platform/Win32/Win32PlatformWindow.h` + `.cpp`

**Win32.h**：public 区（`GetWindowState()` 之后）追加：

```cpp
	// ── Phase 14：窗口显示控制 / 文件拖入 ──
	void Hide() override;
	void SetFileDropEnabled(bool enabled) override;
```

private 区（`AdjustMaximizedClientRect` 之后）追加：

```cpp
	/// @brief WM_DROPFILES 处理（Phase 14——解析 HDROP → DragFinish → 抛事件，R9/R10/R11）
	void HandleDropFiles(HDROP hDrop);
```

**Win32.cpp**（四处）：

**① `Hide()`**（`Show()` 实现之后；对称但不改 `m_shown`）：

```cpp
void Win32PlatformWindow::Hide(){

	// 与 Show()（:95-107）对称——但 m_shown 是「配置期/运行期」标记，不因隐藏回退
	if (m_hwnd != nullptr){

		ShowWindow(m_hwnd, SW_HIDE);

	}

}
```

**② `SetFileDropEnabled()`**（运行期契约——与 `Minimize` 同判据 `m_shown`）：

```cpp
void Win32PlatformWindow::SetFileDropEnabled(bool enabled){

	if (!m_shown){

		Logger::Log(LogLevel::Warning,
			L"FileDrop: SetFileDropEnabled ignored before Show() - runtime API");

		return;

	}

	if (m_hwnd != nullptr){

		DragAcceptFiles(m_hwnd, enabled ? TRUE : FALSE);   // 幂等（shellapi）

	}

}
```

**③ `HandleMessage` 的 `WM_DROPFILES` case**——**落点在状态同步区**（与 `WM_NCCALCSIZE` 同级、事件翻译器之前）：

```cpp
	// ── Phase 14 R9：文件拖入（窗口级——启用后才收得到）────────
	case WM_DROPFILES:

		HandleDropFiles(reinterpret_cast<HDROP>(wParam));

		return 0;
```

**④ `HandleDropFiles` 全文**（§3.4——`DragFinish` 在事件抛出之前，R10）。

**include 追加**：`#include <shellapi.h>` + `#include "ECDI/EventSystem/Window/DropFilesEvent.h"` + `#include "ECDI/Core/String.h"`（已有则不动）。

### 2.10 修改：`src/Platform/Win32/Win32PlatformApplication.h` + `.cpp`

**Win32.h**（现仅 15 行）扩展为：

```cpp
#pragma once

#include "ECDI/Platform/PlatformApplication.h"

#include <Windows.h>

#include <memory>

namespace ECDI{

class WindowClass;

/// @brief Win32 平台应用（7.1.5 消息泵 + Phase 14 应用级托盘宿主）
class Win32PlatformApplication final : public PlatformApplication{
public:

	int Run() override;

	void RequestExit() override;

	void SetTrayIcon(const TrayIconOptions& options) override;

	void RemoveTrayIcon() override;

	int ShowTrayMenu(const TrayMenu& menu) override;

private:

	/// @brief 托盘宿主窗口与窗口类的懒创建（D2——首次 SetTrayIcon 时）
	void EnsureTrayHost();

	/// @brief 静态窗口过程（GWLP_USERDATA 绑定本实例——与 Win32PlatformWindow::WindowProc 同款）
	static LRESULT CALLBACK TrayHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 实例级宿主消息处理（TaskbarCreated 自愈 / 托盘回调 / 其它走 DefWindowProc）
	LRESULT HandleTrayHostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 托盘回调消息翻译（v4 语义 → TrayEvent，D5 终版规则见 §3.3）
	void HandleTrayCallback(WPARAM wParam, LPARAM lParam);

	/// @brief 以当前 m_trayIcon / tooltip 构造 NOTIFYICONDATAW（ADD / 自愈共用）
	NOTIFYICONDATAW BuildIconData();

	/// @brief Shell 调用统一入口（测试缝——条 51 纪律：不出实现层）
	BOOL NotifyShell(UINT action, NOTIFYICONDATAW& nid);

	HWND m_trayHostHwnd = nullptr;         ///< 隐藏顶层宿主（内部资源——永不进 Application::m_windows）
	std::unique_ptr<WindowClass> m_trayHostClass;   ///< 宿主窗口类（独立实例、懒创建——析构顺序见 §3.4）
	UINT m_taskbarCreatedMsg = 0;          ///< RegisterWindowMessageW(L"TaskbarCreated")
	HICON m_trayIcon = nullptr;            ///< 当前图标（平台持有；失败降级系统默认——§3.2）
	bool m_trayDesired = false;            ///< desired state（D9——应用意图）
	bool m_trayRegistered = false;         ///< shell registration state（D9——Shell 实际）
	std::wstring m_trayTooltip;            ///< 提示文本缓存（UTF-16——MODIFY 重建用）
	int m_lastAnchorX = 0;                 ///< 最近有效锚点（菜单定位）
	int m_lastAnchorY = 0;
};

}
```

**Win32.cpp**：实现全文见 §3.2–§3.4（状态机 / 自愈 / 菜单）。include 追加：`"Platform/Win32/Win32WindowClass.h"`、`"ECDI/Core/String.h"`、`"ECDI/Core/Logger.h"`、`<shellapi.h>`、`<windowsx.h>`（GET_X_LPARAM）。

> ⚠️ **宏防护**：`Win32.h` include `<Windows.h>` 后按惯例跟 `#ifdef DrawText #undef DrawText`（本头无 DrawText 方法名，但保持工程统一——与 `Win32WindowClass.h:4-6` 同款）。

### 2.11 测试替身（2 处）

`AnimationTests.cpp:77` 附近（Phase 12 块末尾）与 `ProgressBarTests.cpp` 同位置各追加：

```cpp
	// ── Phase 14：新增 2 个纯虚（本替身不关心显示/拖入——空实现）──

	void Hide() override{}

	void SetFileDropEnabled(bool) override{}
```

`FakeHost`（`EventTests.cpp`）**零改动**（§1.1）。

---

## 3. 关键实现规格

### 3.1 托盘状态机完整转移表（含失败分支——终版）

状态量：`m_trayDesired`（desired）/ `m_trayRegistered`（registered）。Shell 调用统一走 `NotifyShell()`（测试缝）。

| # | 前置 (desired, registered) | 操作 | Shell 调用 | 结果 | 新 (desired, registered) |
|---|---|---|---|---|---|
| 1 | (false, false) | `SetTrayIcon` | ADD（成功） | Active | (true, true) |
| 2 | (false, false) | `SetTrayIcon` | ADD **失败** | Absent + Warning | (true, **false**) |
| 3 | (true, false) | `SetTrayIcon` | ADD（成功） | Active | (true, true) |
| 4 | (true, true) | `SetTrayIcon` | MODIFY（成功） | Active（tooltip/icon 更新） | (true, true) |
| 5 | (true, true) | `SetTrayIcon` | MODIFY **失败** | Warning | (true, true)——**两态均不改** |
| 6 | (true, true) | `RemoveTrayIcon` | DELETE（成功） | Absent | (false, false) |
| 7 | (true, true) | `RemoveTrayIcon` | DELETE **失败** | Warning | (false, **false**)——向「已移除」收敛 |
| 8 | (false, false) | `RemoveTrayIcon` | **无调用** | no-op（D11） | (false, false) |
| 9 | (true, false) | `TaskbarCreated` 广播 | ADD + SETVERSION（自愈） | Active | (true, true) |
| 10 | (false, *) | `TaskbarCreated` 广播 | **无调用** | 不重加（D9 铁律） | (false, false) |
| 11 | 任意 | `~Win32PlatformApplication` | registered ⇒ DELETE | 幽灵图标防线（R1） | — |

**`SetTrayIcon` 的判定依据**：`m_trayRegistered`（**不是** desired）——ADD 与 MODIFY 的选择必须反映 Shell 实际状态；desired 只在 `TaskbarCreated` 分支（#9/#10）与析构（#11）参与判定。

### 3.2 图标加载（D4 / 失败语义）

```cpp
	// 资源 ID → HICON（平台持有；失败降级——与 Win32WindowClass.cpp:56-58 先例同款）
	HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
		MAKEINTRESOURCEW(options.iconResourceId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

	if (icon == nullptr){

		Logger::Log(LogLevel::Warning, L"Tray: LoadImageW failed - fallback to system default icon");

		icon = LoadIconW(nullptr, IDI_APPLICATION);   // 系统默认（共享 HICON——不 DestroyIcon）

	}
	// LR_SHARED 未指定 ⇒ 返回值需 DestroyIcon（§3.4 步骤 2.2）；LoadIconW 的共享句柄不销毁——
	// 用 m_trayIconNeedsDestroy 标记区分（bool 成员，详设新增）
```

> 成员补记：`bool m_trayIconNeedsDestroy = false;`——`LoadImageW` 成功 ⇒ true；降级 `LoadIconW` ⇒ false（共享句柄）。`~Win32PlatformApplication` 据此决定是否 `DestroyIcon`。

### 3.3 v4 回调翻译规则（终版——O-5 已核实）

| `LOWORD(lParam)` | TrayEventType | 锚点来源 |
|---|---|---|
| `NIN_SELECT` | `Select` | `GET_X/Y_LPARAM(wParam)`（MSDN 有效列表内） |
| `NIN_KEYSELECT` | `KeySelect` | 同上（键盘生成时 = 图标左上角） |
| `WM_LBUTTONDBLCLK` | `DoubleClick` | 同上（∈ WM_MOUSEFIRST..LAST） |
| `WM_CONTEXTMENU` | `ContextMenu` | **wParam undefined ⇒ `GetCursorPos()` 兜底**；失败用最近锚点 |
| 其它（`NIN_POPUPOPEN` / balloon 系） | 忽略 | — |

坐标空间：**按屏幕坐标施工**（`TrackPopupMenu` 直接消费）；A5 手测打印 `GetCursorPos()` 与锚点对比确认（O-5 的最后一步）。

### 3.4 `Win32PlatformApplication` 实现要点

**EnsureTrayHost（懒创建——O-2 级纪律：零消费者零开销）**：

```cpp
void Win32PlatformApplication::EnsureTrayHost(){

	if (m_trayHostHwnd != nullptr) return;

	// ① 独立窗口类（与主窗口类分离——UnregisterClassW 各自独立，K10）
	m_trayHostClass = std::make_unique<WindowClass>("ECDI TrayHost", &TrayHostProc);

	// ② 隐藏顶层窗口（F1：不能是 message-only；F2：必须顶层才能收 TaskbarCreated）
	//    WS_POPUP + 不调用 ShowWindow ⇒ 创建后不可见（ Hide 语义的静态版）
	m_trayHostHwnd = CreateWindowExW(0, m_trayHostClass->GetClassName(), L"",
		WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
		m_trayHostClass->GetInstance(), this);

	if (m_trayHostHwnd == nullptr){

		Logger::Log(LogLevel::Error, L"Tray: CreateWindowExW failed for host window");

		m_trayHostClass.reset();   // 失败回滚窗口类（下次重试）

		return;   // 不抛异常（§6.7 失败语义）

	}

	// ③ shell 广播消息注册（失败 = 返 0 ⇒ 自愈禁用，正常路径不受影响）
	m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

	if (m_taskbarCreatedMsg == 0){

		Logger::Log(LogLevel::Warning, L"Tray: RegisterWindowMessageW(TaskbarCreated) failed");

	}

}
```

**析构序列（四步不变量——R1 / O-1 / K10）**：

```cpp
Win32PlatformApplication::~Win32PlatformApplication(){

	// 2.1 幽灵图标防线（R1 硬要求）：registered 才删——无论 desired
	if (m_trayRegistered && m_trayHostHwnd != nullptr){

		NOTIFYICONDATAW nid = BuildIconData();

		NotifyShell(NIM_DELETE, nid);   // 失败仅 Warning（§6.7）——不阻塞析构

		m_trayRegistered = false;

	}

	// 2.2 图标句柄（owned 才销毁——§3.2 的 m_trayIconNeedsDestroy）
	if (m_trayIcon != nullptr && m_trayIconNeedsDestroy){

		DestroyIcon(m_trayIcon);

		m_trayIcon = nullptr;

	}

	// 2.3 先销窗口（K10：UnregisterClassW 要求该类无存活窗口）
	if (m_trayHostHwnd != nullptr){

		DestroyWindow(m_trayHostHwnd);

		m_trayHostHwnd = nullptr;

	}

	// 2.4 后反注册（m_trayHostClass（unique_ptr）成员析构自动 UnregisterClassW）
	//     ⚠️ 顺序不可颠倒——2.3 必须先于本成员的隐式析构

}
```

**NotifyShell（测试缝——条 51 纪律：不出实现层）**：

```cpp
BOOL Win32PlatformApplication::NotifyShell(UINT action, NOTIFYICONDATAW& nid){
	return Shell_NotifyIconW(action, &nid);
}
```

> 测试替身经「子类 override NotifyShell」或注入函数指针实现（实施期择一——两者都不产生公开抽象；§5.2）。

### 3.5 `Application` 侧新实现（全文）

```cpp
void Application::SetTrayIcon(const TrayIconOptions& options){
	m_platformApplication->SetTrayIcon(options);
}

void Application::RemoveTrayIcon(){
	m_platformApplication->RemoveTrayIcon();
}

int Application::ShowTrayMenu(const TrayMenu& menu){
	return m_platformApplication->ShowTrayMenu(menu);
}

void Application::SetQuitOnLastWindowClosed(bool enabled){
	m_quitOnLastWindowClosed = enabled;
}

// ── Phase 14 R11：拖入派发（HitTest → Dispatch → Bubbling——与鼠标同族）──
void Application::OnDropFiles(const DropFilesEvent& event){

	Window& window = *event.GetWindow();

	// 1. HitTest（落点为客户区坐标——DragQueryPoint 既有语义，与鼠标同系）
	Widget* target = FindTargetWidget(window, event.GetX(), event.GetY());

	// 2. 无目标则忽略（与 OnMouseMove:214 同款语义）
	if (target == nullptr){

		return;

	}

	// 3. Bubbling：沿 Parent 链逐级调用（与 OnMouseMove:220-229 同款——不查 IsHandled）
	Widget* current = target;

	while (current != nullptr){

		current->OnDropFiles(event);

		current = current->GetParent();

	}

}
```

> ⚠️ `Widget::OnDropFiles`——`Widget` 需要新增对应虚方法吗？**需要**（bubbling 调用点）——`Widget.h` 的鼠标事件虚方法区追加 `virtual void OnDropFiles(const DropFilesEvent& event){}`（默认空实现，零破坏；与 `OnTimer` 同为「可选响应」）。**这是初设影响面漏记的一处**（初设 §5 只列了 EventRouter +2）：`Widget.h` +1 虚方法，无替身影响（Widget 派生类不强制 override）。

**接线修正**：`Application.cpp` 的 sink lambda 里 `OnEvent(event)` 经 `EventRouter::OnEvent` 分派——**`TrayEvent` 分支调用 `OnTrayEvent`（默认空）**；`Application` 自身不 override `OnTrayEvent`（§1.3）。

### 3.6 退出开关接线（R13——与既有逻辑的关系）

```
窗口销毁路径（既有，不动）：
  Release() → WM_DESTROY → WindowDestroyedEvent
    → Application::OnWindowDestroyed
        → m_deferredDestroy.emplace_back(std::move(*it))   [既有 :107-109]
        → m_windows.erase(it)                              [既有 :112]
        → if (m_windows.empty() && m_quitOnLastWindowClosed) Exit()   [★ 唯一改动行]

Hide 路径（新增，正交）：
  Hide() → ShowWindow(SW_HIDE) → 无 WM_DESTROY → 不触发 OnWindowDestroyed
    ⇒ m_windows 不变 ⇒ 隐式退出不会发生（§6.6 澄清的代码级落地）
```

---

## 4. 工程注册

| 构建系统 | 动作 |
|---|---|
| **CMake** | `CMakeLists.txt:56`（`dwmapi` 行）之后追加：`target_link_libraries(ECDI PUBLIC shell32)`（注释：Phase 14 托盘 + 拖入——Shell_NotifyIconW / DragAcceptFiles 族）。**源文件 glob 零维护**（新头经 selfcontain 自动覆盖；新测试文件经 TEST_SOURCES glob 自动纳入） |
| **MSVC vcxproj** | `ECDI.vcxproj`：+3 `ClInclude`（`Application\TrayIcon.h` / `EventSystem\Application\TrayEvent.h` / `EventSystem\Window\DropFilesEvent.h`）+ **2 `ClCompile`**（`src\Tests\TrayTests.cpp` / `DropFilesTests.cpp`——Phase 13 D1 教训：vcxproj 也编译框架测试源码，漏登记 = 链接失败）+ `shell32.lib` 追加进链接依赖（若显式列出）。`filters`：参照 D2 结论（测试文件无 filters 条目——既有实情） |
| **新目录** | `include/ECDI/EventSystem/Application/`（新）——install 的 DIRECTORY 拷贝按目录树自动覆盖（`CMakeLists.txt:106-107`），零维护 |

---

## 5. 测试规格

### 5.1 自动测试（新文件 `src/Tests/TrayTests.cpp` + `DropFilesTests.cpp`）

| # | 文件 | 用例 | 断言 |
|---|---|---|---|
| **T14-1** | TrayTests | 状态机四组合（§3.1 #1/#3/#4/#6） | `SetTrayIcon` ×2 ⇒ 第二次走 MODIFY（替身记录）；`Remove` 后 registered 归零 |
| **T14-2** | TrayTests | Shell 调用序列 | 首次 = ADD + SETVERSION；更新 = MODIFY；移除 = DELETE；**移除后再移除 = 零调用**（#8） |
| **T14-3** | TrayTests | 自愈（合成 `m_taskbarCreatedMsg`） | desired=true ⇒ 重加；**desired=false ⇒ 零调用**（D9 回归锚） |
| **T14-4** | TrayTests | 失败语义（替身使 Shell 返 FALSE） | ADD 失败 ⇒ registered 保持 false；MODIFY 失败 ⇒ 两态不变；DELETE 失败 ⇒ registered=false 且析构继续 |
| **T14-5** | TrayTests | 析构防线 | registered=true 时析构 ⇒ 记录到 DELETE（幽灵图标防线） |
| **T14-6** | DropFilesTests | 事件构造与派发 | 构造 `HDROP`（`DROPFILES` 结构：双 `\0` 结尾的宽字符路径 + `pFiles` 偏移 + `pt` + `fNC=FALSE`）→ `SendMessage(WM_DROPFILES)` ⇒ `OnDropFiles` 收到 UTF-8 路径列表 + 客户区落点 |
| **T14-7** | DropFilesTests | HDROP 生命周期 | 测试缝计数 `DragFinish` ⇒ 事件返回后恰好 1 次（R10——不在测试里窥探系统堆） |
| **T14-8** | DropFilesTests | 默认关闭 + 未启用窗口收消息 | 无事件、无异常 |
| **T14-9** | DropFilesTests | 派发路径 | 拖到子控件 / 空白区 ⇒ HitTest 目标不同；bubbling 到达 RootWidget（与鼠标同族断言） |
| **T14-10** | WindowChromeTests 或新 | `Hide()` 契约 | Hide 后 `IsWindowVisible=false` 但 `Release()` 前一切如常；Hide 不产生 `WindowDestroyedEvent` |
| **T14-11** | ApplicationTests 或新 | 退出开关两态 | 默认：销毁最后窗口 ⇒ `RequestExit` 被调（替身平台）；false ⇒ 不调。显式 `Exit()` 两态下均有效 |
| **回归** | 全部既有 | 188 用例 | 零回归（重点：窗口销毁 / `PlatformApplication` 契约 / 事件分派） |

**测试缝设计（Shell seam——不出实现层）**：`Win32PlatformApplication::NotifyShell` 设为 **protected virtual**（本文 §3.4 已按此写）——测试子类 override 记录调用序列；`DragFinish` 同款（`HandleDropFiles` 内经 protected `DoDragFinish(HDROP)`）。两缝均为实现层细节，**不进 Public 头**（条 51）。

> ⚠️ **替身注意**：测试子类 override 虚方法**不破坏**「Win32PlatformApplication final」——测试子类需移除 final 或经函数指针注入。**拍板**：`Win32PlatformApplication` 保留 `final`，`NotifyShell` / `DoDragFinish` 改为**受保护的函数指针成员**（默认指向真实 API，测试注入替身）——零 final 冲突、零公开抽象。实施期二选一，倾向后者。

### 5.2 手测（A 项——真环境）

| # | 项 | 判据 |
|---|---|---|
| A3-托盘 | 图标 / 悬停 / 左键 / 右键 | 通知区可见；tip 显示；四类 TrayEvent 日志齐全 |
| A3-菜单 | 弹出 / 选中 / 取消 | 位置在图标处；返回正确 ID；点外部返回 0 且菜单消失（`SetForegroundWindow` 配套生效） |
| A3-自愈 | **explorer 重启** | 图标自动恢复（与 R10 桌面层 A 路线「被杀」对照） |
| A3-幽灵 | 退出后图标 | **立即消失**（析构防线） |
| A4-拖入 | 拖文件 | UTF-8 路径日志；TextBox vs 空白区派发不同；`DragFinish` 无泄漏（任务管理器句柄数稳定） |
| A4-UIPI | **提权运行拖入** | **平台约束验证**（非「预期失败」）：确认行为与官方约束描述一致 |
| A5-O-5 | 坐标实测 | 手测日志打印事件锚点与 `GetCursorPos()` 对比 ⇒ 确认屏幕坐标语义（O-5 收尾） |
| A4-模式 | R13 两模式 | ModelProbe 演示：模式 A（`SetQuitOnLastWindowClosed(false)` + 真关闭）/ 模式 B（close → `Hide()`） |

---

## 6. 契约汇总（新增公共 API 一览）

| API | 时机契约 | 失败语义 | 事件/返回 |
|---|---|---|---|
| `Application::SetTrayIcon(options)` | 无 @pre | 不抛；状态机 §3.1 #1-5 | — |
| `Application::RemoveTrayIcon()` | 无 @pre | 不抛；幂等 no-op | — |
| `Application::ShowTrayMenu(menu)` | 无 @pre（未设图标/宿主 ⇒ 0） | 不抛；CreatePopupMenu 失败 ⇒ 0 | 返回选中 ID（0 = 取消）；**同步模态**（期间不派发框架事件） |
| `Application::SetQuitOnLastWindowClosed(b)` | 无 @pre（运行期可改） | — | 只影响隐式退出 |
| `Window::Hide()` | Show 前调用 = Warning + 忽略 | — | 不产生 WindowDestroyedEvent |
| `Window::SetFileDropEnabled(b)` | Show 前调用 = Warning + 忽略 | — | 启用后拖入 ⇒ `DropFilesEvent` |
| 事件 `DropFilesEvent` | — | HDROP 已由平台释放（R10） | 路径 UTF-8 + 客户区落点 |
| 事件 `TrayEvent` | — | — | `GetWindow() == nullptr`；锚点屏幕坐标（ContextMenu 兜底） |

---

## 7. 已知局限（记账，非缺陷）

| # | 局限 | 缓解 |
|---|---|---|
| L-1 | 单图标（`uID` 固定）——多图标非目标（O-8） | 二次用例再抽象 |
| L-2 | 菜单一级 / 纯文本 / ID（D6 范围） | YAGNI |
| L-3 | `ShowTrayMenu` 同步模态——菜单期间不派发框架事件 | D10 已记账（系统固有行为） |
| L-4 | O-5 坐标空间未实测（按屏幕坐标施工） | A5 手测确认；若实测为屏幕坐标的反例（极低概率），修正点仅 `HandleTrayCallback` 一处 |
| L-5 | UIPI 跨完整性级别拖入不承诺成功（D7） | 平台约束文档化 |
| L-6 | `TaskbarCreated` 自愈依赖宿主窗口存活——宿主创建失败则自愈连同图标一起不可用 | 失败语义（可重试） |

---

## 8. 验收清单（A 项——由用户在 VS / CLion 执行）

| # | 项 | 判据 |
|---|---|---|
| **A1** | MSVC Debug 构建 + 全量测试 | 失败数 0；报告写明断言是否启用（MSVC Debug 带 `_DEBUG`）；用例数 = 188 + 新增（T14-1..11 预计 +11 ⇒ **199**） |
| **A2** | 四工具链 | MSVC / ClangCL / Clang / MinGW 全绿（MinGW 断言核验按条 35/50） |
| **A3** | 托盘手测 | §5.2 A3 四行全过 |
| **A4** | 拖入 + UIPI + 模式手测 | §5.2 A4 三行全过 |
| **A5** | O-5 坐标实测 | §5.2 A5——**初设遗留的最后一步** |
| **A6** | 静态自查（AI 侧） | 替身补齐（TestPlatformWindow ×2）；`grep "GetWindow"` 接口零改动确认；BOM；`git diff` 逐行 |
| **A7** | ModelProbe 集成手测 | `--native` 模式下接入托盘（现成 `app.ico`）——Phase 13 A4 零回归路径仍可复跑 |

---

## 9. 修订记录

- v1.0（2026-09-16）**详细设计初稿**：
  - **§1.1 实施前核实（重大更正）**：O-6 的载体 `PlatformWindowHost::GetWindow()` **早已存在**（`PlatformWindowHost.h:32-34`，注释明言"翻译器构造 Event 需 Window*"）——初设 v1.0 的 O-6 前提（"平台层拿不到 Window*"）为**未验证的错误断言**，v1.1 据此新增的 §2.10 纯虚**作废**；影响面修正：修改 Public 头 7 → **6**（`PlatformWindowHost.h` 零改动）、测试替身 3 → **2 处**（FakeHost 零改动）。**记因**：接口存在性必须 grep 接口头本身，实现者清点（条 33）不覆盖此项。
  - **§1.2 / §1.3 精化**：O 拍板逐项落点；`Application` 不 override `OnTrayEvent`（初设 §2.7 有误——无默认动作）；**新增初设漏记的一处**：`Widget.h` +1 虚方法 `OnDropFiles`（bubbling 调用点，默认空实现零破坏）。
  - **§2 逐文件最小 diff**：3 新头（86→89）+ 6 头修改 + `Win32PlatformWindow` / `Win32PlatformApplication` / `Application.cpp` 实现规格 + 替身 2 处——每处标注动哪几行、不动哪些行。
  - **§3 关键规格**：状态机 **11 态完整转移表**（含失败分支——NIM_ADD 失败不得置 registered=true / MODIFY 失败两态不动 / DELETE 失败向已移除收敛）；v4 翻译终版（含 ContextMenu 兜底）；`EnsureTrayHost` / 析构四步不变量 / `NotifyShell`+`DoDragFinish` 测试缝（函数指针形态，保 `final`）/ `Application::OnDropFiles` 全文（同鼠标族 bubbling）。
  - **§4 工程注册**：CMake +`shell32`；vcxproj +3 ClInclude + **2 ClCompile**（Phase 13 D1 教训）。
  - **§5 测试规格**：**T14-1..11**（状态机 / Shell 序列 / 自愈 / 失败语义 / 析构防线 / HDROP 生命周期 / 默认关 / 派发 / Hide / 退出开关）+ 手测（O-5 坐标实测 = A5）。
  - **§6 契约汇总 · §7 局限 6 条 · §8 验收 A1–A7**。
  - **阶段边界自律**：本文给逐文件规格与实现全文骨架，但 ModelProbe 集成示例与实施期偏差记录归实施阶段回写。
