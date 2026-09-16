# Phase 14 托盘与拖入接缝 初步设计（v1.1）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-16
> 状态：待评审
> 前置：`phase14-tray-and-drop-requirements.md` **v1.1**（外部评审「通过，可进入初步设计」· 2026-09-16）
> 一句话：把需求稿的 **R1–R13 + D0–D11** 落成**可评审的头全文草案 + 平台实现分解 + 生命周期时序**——两条通道（应用级托盘 / 窗口级拖入）各走一条与 R9 同构的惯例分支，公共 API 零 Win32 类型。
> v1.0：初稿（§1 范围映射 · §2 Public 头全文草案 · §3 实现分解 · §4 链接库传播 · §5 影响面 · §6 生命周期与销毁顺序 · §7 测试方向 · §8 开放决策点 · §9 D0–D11 兑现表 · §10 修订记录）
> v1.1（2026-09-16）外部评审「**通过，可进详设**——3 个详设前必须收敛项」全部处理：① **O-5 已核实**（MSDN 原文：v4 锚点 = `GET_X_LPARAM/GET_Y_LPARAM(wParam)`，与本稿逐字一致；**新发现 `WM_CONTEXTMENU` 不在坐标有效列表 ⇒ `GetCursorPos` 兜底**）；② **O-6 拍板 C**（`PlatformWindowHost` 加 `virtual Window& GetWindow()`——§2.10 新小节；影响面 = `FakeHost` 补 1 override）；③ **新增 §6.7 失败语义契约**（平台能力失败不抛异常，通过内部状态 + 日志反映）；④ §3.1 笔误修正（删残留成员 `m_trayHostClass`——评审抓到）；⑤ §8 收敛：7 项拍板 + 唯一遗留 O-3；⑥ §7.2 Shell seam 纪律；⑦ §9 D5 ⚠️→✅

---

## 1. 范围映射（R → 设计域）

| 需求 | 内容 | 本文落点 |
|---|---|---|
| **R1** | 托盘生命周期（添加/更新/移除 + 析构必删 + 幂等） | §2.1 / §2.5 / §3.2 / §6.4 |
| **R2** | 图标来源（资源 ID） | §2.1（`TrayIconOptions::iconResourceId`）/ §3.2 |
| **R3** | 悬停提示文本 | §2.1（`tooltip`）/ §3.2 |
| **R4** | 交互事件 + 锚点坐标 | §2.2（`TrayEvent`）/ §3.1 |
| **R5** | 右键菜单（返回选中 ID） | §2.1（`TrayMenu` / `TrayMenuItem`）/ §3.4 |
| **R6** | explorer 重建自愈 | §3.3 / §6.4 |
| **R7** | 与 `Window` 解耦 | §2.5 硬契约 / §3.1 / §6.2 |
| **R8** | 窗口级拖入开关（默认关） | §2.4 / §2.6 / §3.6 |
| **R9** | 拖入事件形态（UTF-8 路径 + 落点，无 `HDROP`） | §2.3 / §3.6 |
| **R10** | `DragFinish` 在事件抛出前闭合 | §3.6 |
| **R11** | 走既有派发链路，不新开通道 | §3.6 / §6.5 |
| **R12** | `Hide()` | §2.4 / §2.6 / §3.5 |
| **R13** | 退出策略开关 | §2.7 / §3.7 |
| **D0** | 一个 Phase 三组 R（顺序：拖入 → R12/R13 → 托盘） | §9 兑现表（顺序即 §3 的编排） |
| **D1** | 托盘挂 `PlatformApplication` | §2.5 |
| **D2** | 框架自建隐藏顶层窗口 + 硬契约 | §2.5 / §3.1 / §6.2 |
| **D3** | `std::function` 事件 sink 注入 | §2.5 / §3.1 / §6.3 |
| **D4** | exe 资源 ID（默认同 `kAppIconId`） | §2.1 / §3.2 |
| **D5** | `NOTIFYICON_VERSION_4` → 事件翻译 | §2.2 / §3.1 |
| **D6** | 原生一级菜单 + `TPM_RETURNCMD` | §2.1 / §3.4 |
| **D7** | UIPI 不处理（记约束） | §5 记账 / §7 手测 |
| **D8** | R12/R13 纳入本阶段 | §2.4 / §2.6 / §2.7 / §3.5 / §3.7 |
| **D9** | 框架内建自愈 + 双态模型 | §3.3 / §6.4 |
| **D10** | 菜单**同步返回 ID** | §2.1 / §3.4 |
| **D11** | 状态机**宽容语义** | §2.1 契约 / §3.2 |

### 1.1 勘察基线（本文所有「现状」断言的证据）

| # | 事实 | 证据 |
|---|---|---|
| **K1** | `PlatformApplication` 现有形态：**非虚** `SetDeferredCleanup(const std::function<void()>&)`（基类持 `m_deferredCleanup`）+ 纯虚 `Run()` / `RequestExit()` + protected `PerformDeferredCleanup()` | `include/ECDI/Platform/PlatformApplication.h:18-33` |
| **K2** | `PlatformApplication` **实现者仅 1 个**（`Win32PlatformApplication`），**零测试替身** ⇒ 加能力**不影响既有测试** | 全库 grep `public PlatformApplication` |
| **K3** | `PlatformWindow` 实现者 **3 个**：`Win32PlatformWindow`（生产）+ `AnimationTests::TestPlatformWindow` + `ProgressBarTests::TestPlatformWindow`（替身 ×2）⇒ **加纯虚必须同步补 2 处替身**（skill 条 33） | 全库 grep `public PlatformWindow` |
| **K4** | `Application` 构造：`: m_platformApplication(std::make_unique<Win32PlatformApplication>())` 后立即 `SetDeferredCleanup([this]{ ProcessDeferredDestroy(); })`；**无 `GetPlatformApplication()` 公共访问器** | `src/Application/Application.cpp:26-32`；`Application.h:110-121` |
| **K5** | 退出路径：`OnWindowDestroyed` 末尾 `if (m_windows.empty()) Exit();`（当前唯一的隐式退出触发点） | `src/Application/Application.cpp:115-118` |
| **K6** | `OnWindowCloseRequested` → `event.GetWindow()->Release()`（关闭请求 = 销毁 HWND，与「隐藏」是两条路） | `src/Application/Application.cpp:129-135` |
| **K7** | `PlatformWindow` 全纯虚，**无 `Hide()`**；`Show()` / `Release()` 并列 | `include/ECDI/Platform/PlatformWindow.h:33-40` |
| **K8** | `Win32PlatformWindow::Show()`：`m_shown = true; ShowWindow(SW_SHOW); UpdateWindow();` —— `m_shown` 是**配置期/运行期分界线** | `src/Platform/Win32/Win32PlatformWindow.cpp:95-107` |
| **K9** | `Win32PlatformApplication::Run()` 用 `GetMessageW(&msg, nullptr, 0, 0)` ⇒ **收本线程全部窗口的消息**（含未来新增的宿主窗口）**零修改** | `src/Platform/Win32/Win32PlatformApplication.cpp:7-25` |
| **K10** | `WindowClass` **支持多实例**（构造收 `(className, windowProc)`）；`Instance()` 是静态单例（类名 `"ECDI FrameWork"`）；析构 `UnregisterClassW`（**要求该类无存活窗口**） | `src/Platform/Win32/Win32WindowClass.h:24-28` / `.cpp:24-74` |
| **K11** | **HICON 加载先例**：`wc.hIcon = (HICON)LoadImageW(m_instance, MAKEINTRESOURCEW(kAppIconId /*102*/), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE)` | `src/Platform/Win32/Win32WindowClass.cpp:19-21 / 58` |
| **K12** | `Event` 基类**强绑 `Window*`**（`explicit Event(Window*)`）；`EventDispatcher` 只比较 `GetType() == T::StaticType()`，**不读 `GetWindow()`** ⇒ **`nullptr` 窗口的事件在分派层安全** | `include/ECDI/EventSystem/Event.h:47-57` / `EventDispatcher.h:34-49` |
| **K13** | `EventType` 为单一枚举（窗口 / 鼠标 / 键盘三组）；`EventRouter` 为纯虚方法集合（全默认空实现，`Application` 继承） | `include/ECDI/EventSystem/EventType.h:8-30` / `EventRouter.h:27-98` |
| **K14** | 链接库现状：`ECDI PUBLIC user32 imm32 msimg32` + `windowscodecs ole32 shlwapi` + `dwmapi`——**无 `shell32`** | `CMakeLists.txt:51-56` |

### 1.2 两条通道的接缝形态对照（本阶段的结构核心）

| 维度 | 拖入（**窗口级**） | 托盘（**应用级**） |
|---|---|---|
| 语义归属 | 每个窗口独立开关 | 一个应用一个图标位 |
| 接缝载体 | `PlatformWindow`（既有） | `PlatformApplication`（**本阶段首次承载能力**） |
| 事件上行 | `PlatformWindowHost::OnEvent`（**既有**） | `PlatformApplication` 的 **`std::function` sink**（**新增**，D3） |
| 是否需要 HWND | 否（消息到已有窗口） | **是**（自建隐藏顶层窗口，D2） |
| 派发路径 | HitTest + Bubbling（**既有链路**，R11） | 无 HitTest（应用级无落点），直接虚方法 |
| Win32 消息 | `WM_DROPFILES`（1 条） | `WM_TRAYICON`（自定义）+ `TaskbarCreated`（注册消息，2 条） |
| 新增 Public 头 | 1（事件） | 2（配置 + 事件） |

---

## 2. Public 头全文草案

> 头草案按现有风格书写（`#pragma once` / namespace `ECDI` / `///<` 行尾注释 / UTF-8 BOM）。**本阶段不写实现**——实现分解见 §3。

### 2.1 新增：`include/ECDI/Application/TrayIcon.h`（Public 头 **86 → 87**）

> 归域理由：托盘是**应用级**能力，与 `Application.h`（已在 `include/ECDI/Application/`）同域；放 `Window/` 域语义错误。

```cpp
#pragma once

#include <string>
#include <vector>

namespace ECDI{

/// @brief 托盘图标配置（Phase 14 R2/R3）
/// @details 值类型（可拷贝可赋值）；**公共 API 零 Win32 类型**——
/// 图标来源为 exe 资源 ID（int），HICON 的加载/持有/销毁全部收在平台层内部（D4）。
struct TrayIconOptions{

	/// @brief 图标资源 ID（exe 内 ICON 资源；默认与窗口类图标同源）
	/// @details 默认值 = kDefaultIconResourceId（与 Win32WindowClass.cpp 的 kAppIconId 一致）。
	/// ⚠️ 两侧必须同步修改（.rc 宏对 C++ 编译器不可见——cpp 侧本地常量，既有先例）。
	int iconResourceId = kDefaultIconResourceId;

	/// @brief 悬停提示文本（UTF-8；空串 = 不显示提示）
	std::string tooltip;

	/// @brief 图标资源 ID 默认值（与 ECDI.rc 的 IDI_APP 对齐）
	static constexpr int kDefaultIconResourceId = 102;
};

/// @brief 托盘菜单项（Phase 14 R5 / D6）
/// @details 只支持**一级 + 纯文本 + ID**（图标/勾选/子菜单不做——YAGNI，见需求 §5）。
/// 分隔线用 `id == kSeparatorId` 表示（不引入额外枚举，保持值类型简单）。
struct TrayMenuItem{

	/// @brief 命令 ID（> 0；`ShowTrayMenu` 返回它）
	int id = 0;

	/// @brief 菜单项文本（UTF-8）
	std::string text;
};

/// @brief 托盘菜单（一级列表）
struct TrayMenu{

	/// @brief 菜单项（按序显示；空列表 = 不弹菜单并返回 0）
	std::vector<TrayMenuItem> items;
};

}
```

**契约**：
- `TrayIconOptions` / `TrayMenu` / `TrayMenuItem` 均为**值语义**（无虚函数、无 `Window*`、无平台类型）。
- `iconResourceId <= 0` ⇒ 视为**无效**，平台层退回 `kDefaultIconResourceId` 并记 Warning（不抛异常）。
- 菜单项 `id <= 0` 由平台层忽略（保证 `ShowTrayMenu` 的 `0` 能安全表示「未选中」）。

### 2.2 新增：`include/ECDI/EventSystem/Application/TrayEvent.h`（Public 头 **87 → 88**）

> 归域理由：托盘事件是**应用级**事件，`EventSystem/Application/` 与既有 `EventSystem/Window/` 对称。

```cpp
#pragma once

#include "ECDI/EventSystem/Event.h"

namespace ECDI{

/// @brief 托盘交互事件种类（Phase 14 R4 / D5）
/// @details **平台语义在此收口**——`NOTIFYICON_VERSION_4` 的 `NIN_SELECT` / `NIN_KEYSELECT` /
/// `WM_CONTEXTMENU` 等一律翻译为本枚举，**公共 API 不出现 Win32 消息码**。
enum class TrayEventType{

	/// @brief 鼠标左键单击（v4 的 NIN_SELECT）
	Select,

	/// @brief 键盘激活（v4 的 NIN_KEYSELECT——Enter/Space 聚焦图标后激活）
	KeySelect,

	/// @brief 鼠标左键双击
	DoubleClick,

	/// @brief 右键（v4 的 WM_CONTEXTMENU——用于弹出菜单）
	ContextMenu
};

/// @brief 托盘交互事件（Phase 14 R4）
/// @details **应用级事件**：`GetWindow()` 恒为 `nullptr`（无来源窗口——与 `Window` 解耦，R7）。
/// 锚点坐标用于菜单定位与「就地弹窗」（v4 携带；键盘激活时为图标左上角——MSDN 语义）。
/// ⚠️ **ContextMenu 的锚点由框架兜底**（v1.1 核实 O-5：NOTIFYICON_VERSION_4 下
/// WM_CONTEXTMENU 不在坐标有效列表内——MSDN：「For all other messages, wParam is undefined」）；
/// 平台层以 GetCursorPos() 取右键时刻光标位置（Win32 对 WM_CONTEXTMENU 的标准做法）。
class TrayEvent : public Event{

public:

	/// @param type 事件种类
	/// @param x    锚点 X（屏幕坐标）
	/// @param y    锚点 Y（屏幕坐标）
	TrayEvent(TrayEventType type, int x, int y) noexcept
		: Event(nullptr), m_type(type), m_x(x), m_y(y){}

	TrayEventType GetTrayType() const noexcept{ return m_type; }

	/// @brief 锚点 X（**屏幕坐标**——菜单定位直接可用，无需换算）
	int GetX() const noexcept{ return m_x; }

	/// @brief 锚点 Y（屏幕坐标）
	int GetY() const noexcept{ return m_y; }

	static EventType StaticType() noexcept{ return EventType::Tray; }

	EventType GetType() const override{ return EventType::Tray; }

private:

	TrayEventType m_type;
	int m_x = 0;
	int m_y = 0;
};

}
```

### 2.3 新增：`include/ECDI/EventSystem/Window/DropFilesEvent.h`（Public 头 **88 → 89**）

> 归域理由：拖入是**窗口级**事件，与 `WindowStateChangedEvent.h` 等同目录。

```cpp
#pragma once

#include "ECDI/EventSystem/Event.h"

#include <string>
#include <vector>

namespace ECDI{

/// @brief 文件拖入事件（Phase 14 R9/R10）
/// @details **`HDROP` 绝不出现在公共 API**：平台层已解析为 UTF-8 路径列表，
/// 且 `DragFinish` 已在**事件抛出之前**完成（R10）——消费者拿到的是已脱离系统资源的纯数据，
/// **应用侧零释放责任**。
class DropFilesEvent : public Event{

public:

	/// @param window 来源窗口（拖入是窗口级事件——恒非空）
	/// @param paths  UTF-8 路径列表（可空——空列表表示无有效文件，消费者应忽略）
	/// @param x      落点 X（**客户区坐标**——与 MouseButtonDownEvent 同坐标系）
	/// @param y      落点 Y（客户区坐标）
	DropFilesEvent(Window* window,
		std::vector<std::string> paths,
		int x,
		int y) noexcept
		: Event(window)
		, m_paths(std::move(paths))
		, m_x(x)
		, m_y(y){}

	/// @brief 拖入的文件路径列表（UTF-8；每项为一个绝对路径）
	const std::vector<std::string>& GetPaths() const noexcept{ return m_paths; }

	/// @brief 落点 X（客户区坐标——HitTest 直接可用）
	int GetX() const noexcept{ return m_x; }

	/// @brief 落点 Y（客户区坐标）
	int GetY() const noexcept{ return m_y; }

	static EventType StaticType() noexcept{ return EventType::DropFiles; }

	EventType GetType() const override{ return EventType::DropFiles; }

private:

	std::vector<std::string> m_paths;

	int m_x = 0;
	int m_y = 0;
};

}
```

### 2.4 修改：`PlatformWindow.h`（+2 能力）

**新增块（插在「Phase 13：窗口状态查询」之后、类尾之前）——只列新增行，其余不动**：

```cpp
	// ── Phase 14：窗口显示控制（R12——Show 的对称补全）──────────────
	// @pre 运行期契约（Show() 之后有效——与 Minimize/Maximize/Restore 同组）

	/// @brief 隐藏窗口（R12——Show() 的对称；**不销毁 HWND**）
	/// @details 与 Release() 的语义边界是本条的存在理由：
	/// **Hide = 窗口资源存活、仅不可见**（可再次 Show）；**Release = 销毁 HWND**（不可逆）。
	/// ⚠️ **不改动配置期/运行期分界标记**——`Hide()` 后仍处于运行期
	/// （Show() 已调用过这一事实不因隐藏而回退；与既有 m_shown 语义一致）。
	/// @pre Show() 之前调用记 Warning 并忽略
	virtual void Hide() = 0;

	// ── Phase 14：文件拖入（R8——窗口级开关）────────────────────────
	// @pre 运行期契约（Show() 之后有效——与 Minimize/Maximize/Restore 同组）

	/// @brief 启用 / 停用本窗口的文件拖入（R8）
	/// @param enabled true = 接受拖入（系统发 WM_DROPFILES）；false = 拒绝
	/// @details 默认**关闭**（不改变既有窗口行为——零回归底线）。
	/// 平台实现为 DragAcceptFiles 的薄封装；**可重复调用**（幂等）。
	/// @pre Show() 之前调用记 Warning 并忽略
	virtual void SetFileDropEnabled(bool enabled) = 0;
```

**影响面**：`PlatformWindow` 加 **2 个纯虚** ⇒ `AnimationTests::TestPlatformWindow` / `ProgressBarTests::TestPlatformWindow` **各补 2 个空实现 override**（K3）。

### 2.5 修改：`PlatformApplication.h`（+托盘能力 +事件 sink）

**新增块（插在 `RequestExit()` 之后、`protected:` 之前）**：

```cpp
	// ── Phase 14：应用级托盘能力（R1–R7 / D1 / D2）──────────────────
	// ★ 应用级能力惯例（本阶段首次建立——与窗口级 R9 三步同构）：
	//   ① PlatformApplication 加能力 virtual（本文件）；
	//   ② Application 加公共方法透传（Application.h——应用层唯一入口）；
	//   ③ Shell 消息在具体实现内消化（Win32PlatformApplication::TrayHostProc）。

	/// @brief 设置（或更新）托盘图标（R1——**幂等配置语义**，D11）
	/// @param options 图标配置（资源 ID + 提示文本）
	/// @details 宽容语义：**未注册 ⇒ 注册；已注册 ⇒ 更新**（不存在"重复 Add 报错"分支）。
	/// @pre 无（Run() 前后均可调用）
	virtual void SetTrayIcon(const TrayIconOptions& options) = 0;

	/// @brief 移除托盘图标（R1——**幂等**，D11）
	/// @details **未注册 ⇒ no-op**（不抛异常、不记错误）。
	/// ⚠️ 移除后收到 `TaskbarCreated` **不得重加**（D9 双态模型）——
	/// 这是本阶段最容易写错的分支。
	virtual void RemoveTrayIcon() = 0;

	/// @brief 弹出托盘菜单并**同步返回**选中项 ID（R5 / D6 / D10）
	/// @param menu 菜单内容（一级 + 纯文本 + ID）
	/// @return 选中的 `TrayMenuItem::id`；**0 = 未选中 / 取消**（含空菜单）
	/// @details **同步语义**：内部 TrackPopupMenu(TPM_RETURNCMD) 期间消息循环由其接管，
	/// 该期间不派发框架事件（系统模态菜单固有行为——D10 已记账）。
	/// 弹出位置 = **最近一次托盘事件的锚点**（R4 携带）；从未收到事件时用托盘图标位置。
	/// @pre 无
	virtual int ShowTrayMenu(const TrayMenu& menu) = 0;

	/// @brief 注册托盘事件上行通道（D3——复刻 SetDeferredCleanup 的注入模式）
	/// @param sink 事件接收器（**非拥有**；调用时 Application 必然存活——见 §6.3 时序）
	/// @details 与 SetDeferredCleanup 同款：基类持 std::function，实现者负责调用。
	/// ⚠️ 销毁顺序契约：sink 必须在宿主窗口销毁**之前**清空（§6.3/§6.4）。
	void SetTrayEventSink(const std::function<void(const TrayEvent&)>& sink){
		m_trayEventSink = sink;
	}
```

**同时新增私有成员 + protected 调用点**：

```cpp
protected:

	/// @brief 执行托盘事件上行（实现者在收到 Shell 回调时调用——未注册则空操作）
	void EmitTrayEvent(const TrayEvent& event) const{
		if (m_trayEventSink){
			m_trayEventSink(event);
		}
	}

private:

	std::function<void(const TrayEvent&)> m_trayEventSink;   ///< 托盘事件上行（Application 注册）
```

**注意（本头新增 include）**：`#include "ECDI/Application/TrayIcon.h"`（`TrayIconOptions` / `TrayMenu` 为值参数）+ `#include "ECDI/EventSystem/Application/TrayEvent.h"`（sink 签名）。

> ⚠️ **头依赖方向检查**：`Platform/` 域 include `Application/` 与 `EventSystem/` 域——**是否违反分层？**
> 不违反：`PlatformWindow.h` 已有先例（include `Widget/CaretGeometry.h`、`Window/ChromeMode.h` 等框架层值类型）。平台接口用框架层**值类型**作参数是既有惯例（"接口全部用框架层类型"——`PlatformWindow.cpp:20` 契约）；**反向（框架层 include 平台层）才是禁止的**。

### 2.6 修改：`Window.h`（+`Hide` +拖入开关透传）

```cpp
	/// @brief 隐藏窗口（R12——`Show()` 的对称；**不销毁 HWND**，可再次 `Show()`）
	/// @details 与 `Release()` 的区别：**隐藏 ≠ 销毁**。隐藏后窗口对象与 HWND 均存活，
	/// 仅不可见（不进任务栏）。常驻应用「关闭主窗口但保留托盘」的实现路径 = `Hide()` 而非 `Release()`。
	/// @pre `Show()` 之前调用记 Warning 并忽略（运行期 API——同 `Minimize`）
	void Hide();

	/// @brief 启用 / 停用本窗口的文件拖入（R8；默认关闭）
	/// @details 启用后拖入文件到本窗口会派发 `DropFilesEvent`（走既有 HitTest + Bubbling）。
	/// @pre `Show()` 之前调用记 Warning 并忽略
	void SetFileDropEnabled(bool enabled);
```

### 2.7 修改：`Application.h`（+托盘透传 +退出策略）

```cpp
	/// @brief 设置（或更新）托盘图标（R1；转发平台应用——应用层唯一入口，D1）
	/// @details 幂等配置语义（D11）：未注册 ⇒ 注册；已注册 ⇒ 更新。
	void SetTrayIcon(const TrayIconOptions& options);

	/// @brief 移除托盘图标（R1；幂等——未注册为 no-op）
	void RemoveTrayIcon();

	/// @brief 弹出托盘菜单并同步返回选中项 ID（R5/D10；0 = 未选中）
	int ShowTrayMenu(const TrayMenu& menu);

	/// @brief 设置「最后一个窗口关闭时是否退出应用」（R13；默认 `true`）
	/// @param enabled `true` = 保留既有行为（零行为变更）；`false` = 常驻（不自动退出）
	/// @details 命名直述**触发条件**，避免双重否定（`SetStayAlive(false)` 式命名已否决——R13）。
	/// 仅影响 `OnWindowDestroyed` 中的隐式退出；显式 `Exit()` 不受影响。
	void SetQuitOnLastWindowClosed(bool enabled);

protected:

	void OnTrayEvent(const TrayEvent& event) override;   // EventRouter 虚方法（默认空实现）
```

**私有成员**：`bool m_quitOnLastWindowClosed = true;`

### 2.8 修改：`EventType.h`

```cpp
	WindowStateChanged,		///< 窗口状态变化（Phase 12 R7——minimized/maximized/restored）
	DropFiles,				///< 文件拖入（Phase 14 R9——窗口级，携带 UTF-8 路径列表 + 落点）
	Timer,					///< 周期定时器触发（8.5.1；WM_TIMER 翻译，带 timerId）

	// ── 应用事件（Phase 14：应用级——无来源窗口）────────
	Tray,					///< 托盘交互（Phase 14 R4——GetWindow() 恒 nullptr）
```

### 2.9 修改：`EventRouter.h`

```cpp
	/// @brief 文件拖入（Phase 14 R9；窗口级事件——经既有 HitTest + Bubbling 派发）
	/// @details 与鼠标事件同族（有落点坐标）；平台层已完成 DragFinish（R10）。
	virtual void OnDropFiles(const DropFilesEvent& event){}

	/// @brief 托盘交互（Phase 14 R4；**应用级事件——无 HitTest、无来源窗口**）
	/// @details 与 TimerEvent 的差异：Timer 派发给焦点控件；托盘事件**没有窗口上下文**，
	/// 直接到达 Application 子类（消费者在此决定弹菜单 / 恢复窗口等）。
	virtual void OnTrayEvent(const TrayEvent& event){}
```

**影响面**：`EventRouter` 全虚方法**默认空实现** ⇒ 加 2 个虚方法**零破坏**（既有子类无需修改）。

---

### 2.10 修改：`PlatformWindowHost.h`（+`GetWindow`——v1.1 拍板 O-6 方案 C）

```cpp
	/// @brief 获取宿主 Window（v1.1 拍板 O-6 方案 C）
	/// @details 表达**既有事实**——每个 PlatformWindow 必然对应一个宿主 Window
	/// （构造注入 Host& 的对偶关系）。用途：平台层构造窗口级 Framework Event 时
	/// 填充 `Event::m_window`（拖入事件为本阶段唯一消费者）。
	/// @note 只读引用；**平台层不得经此调用 Window 的修改性 API**——平台层对
	/// Window 的合法上行通道只有 OnEvent（访问面纪律）。
	virtual Window& GetWindow() noexcept = 0;
```

**影响面（grep 实证）**：`PlatformWindowHost` 实现者 = **2 个**——`Window`（生产，`Window.h:43`，实现为 `return *this;`）+ `EventTests::FakeHost`（替身，`EventTests.cpp:44`，**补 1 个 override**——skill 条 33）。

**同步**：§3.6 的 `DropFilesEvent` 构造改用 `&m_host.GetWindow()`；**O-6 三路径收敛为 C，B（const_cast）淘汰**；`Window::OnEvent` **零改动**。

**头依赖**：`PlatformWindowHost.h` 需前置声明 `class Window;`（引用返回不需完整类型）。

---

## 3. 实现分解（`src/Platform/Win32/`）

> 实现顺序 = D0 的顺序（**拖入 → R12/R13 → 托盘**）：前者验证 R9 惯例在窗口级仍成立，后者是应用级新惯例。

### 3.1 `Win32PlatformApplication`：托盘宿主窗口（D2 / D3 / D5）

**新增成员**：

```cpp
	HWND m_trayHostHwnd = nullptr;          ///< 隐藏顶层宿主窗口（内部平台资源——永不进 Application::m_windows）
	std::unique_ptr<WindowClass> m_trayHostClass;  ///< 懒创建（避免无托盘应用付出窗口类注册成本）
	UINT m_taskbarCreatedMsg = 0;           ///< RegisterWindowMessageW(L"TaskbarCreated") 的返回值
	HICON m_trayIcon = nullptr;             ///< 当前托盘图标（平台持有——D4）
	bool m_trayRegistered = false;          ///< **shell registration state**（D9 双态之一）
	bool m_trayDesired = false;             ///< **desired state**（D9 双态之二）
	int m_lastAnchorX = 0;                  ///< 最近锚点（ShowTrayMenu 定位用）
	int m_lastAnchorY = 0;
```

**宿主窗口创建（懒创建——首次 `SetTrayIcon` 时）**：

```cpp
void Win32PlatformApplication::EnsureTrayHost(){

	if (m_trayHostHwnd != nullptr) return;

	// ① 注册独立窗口类（类名与主窗口类分离——析构时各自 UnregisterClassW）
	m_trayHostClass = std::make_unique<WindowClass>("ECDI TrayHost", &TrayHostProc);

	// ② 创建**隐藏顶层窗口**（F1：不能是 message-only——否则收不到 TaskbarCreated 广播）
	//    ⚠️ 必须是顶层（parent = nullptr）：F2 的广播只发给顶层窗口
	m_trayHostHwnd = CreateWindowExW(
		0, m_trayHostClass->GetClassName(), L"", WS_POPUP,   // 无样式可见性——不 Show
		0, 0, 0, 0, nullptr, nullptr, m_trayHostClass->GetInstance(), this);
	// 失败 → 记 Error 日志（不抛异常——托盘失败不应杀应用；与图标加载失败同款降级）

	// ③ 注册 shell 广播消息（explorer 重建通知——D9 自愈的触发源）
	m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
}
```

**WndProc（宿主窗口——`Win32PlatformApplication` 的静态成员）**：

```cpp
LRESULT CALLBACK Win32PlatformApplication::TrayHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){

	Win32PlatformApplication* self = nullptr;

	if (msg == WM_NCCREATE){
		auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<Win32PlatformApplication*>(cs->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	} else {
		self = reinterpret_cast<Win32PlatformApplication*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}

	if (self == nullptr) return DefWindowProcW(hwnd, msg, wParam, lParam);

	return self->HandleTrayHostMessage(hwnd, msg, wParam, lParam);
}
```

> 与 `Win32PlatformWindow::WindowProc` 同款（`GWLP_USERDATA` 绑定 + `CREATESTRUCT` 传递——既有先例）。

### 3.2 托盘状态机与 Shell 调用（R1 / D5 / D11）

**状态集**（与 D11 的两态对应）：`Absent`（`m_trayRegistered == false`）/ `Active`（`true`）。**desired state 独立**（D9）。

| 当前（shell） | 操作 | 结果 | 说明 |
|---|---|---|---|
| Absent | `SetTrayIcon` | Active | `NIM_ADD`；`desired = true` |
| Active | `SetTrayIcon` | Active | **`NIM_MODIFY`**（宽容语义——D11 的「Add 重复 = 隐式 Update」） |
| Active | `RemoveTrayIcon` | Absent | `NIM_DELETE`；`desired = false` |
| Absent | `RemoveTrayIcon` | Absent | **no-op**（D11；不抛异常、不记错误） |

**`NOTIFYICONDATAW` 填充与版本设置（D5）**：

```cpp
	NOTIFYICONDATAW nid{};
	nid.cbSize = sizeof(NOTIFYICONDATAW);
	nid.hWnd = m_trayHostHwnd;              // ← 宿主窗口（非 message-only——F1）
	nid.uID = kTrayIconId;                  // 固定 1（单图标——多图标属非目标）
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = kTrayCallbackMessage;   // 自定义（WM_APP + 1）
	nid.hIcon = m_trayIcon;                 // 平台持有（D4）
	// tooltip 转 wchar_t 后 wcscpy_s 到 nid.szTip（UTF-8 → UTF-16 边界转换）

	Shell_NotifyIconW(NIM_ADD, &nid);

	// ★ 版本设置（NOTIFYICON_VERSION_4——D5）
	//   必须在 NIM_ADD **之后**单独设（MSDN：NIM_SETVERSION 不改 cbSize 之外的字段）
	nid.uVersion = NOTIFYICON_VERSION_4;
	Shell_NotifyIconW(NIM_SETVERSION, &nid);
```

**回调消息翻译（D5——Win32 语义在此收口）**：

```cpp
void Win32PlatformApplication::HandleTrayCallback(WPARAM wParam, LPARAM lParam){

	// v4 语义（v1.1 已核实，O-5——MSDN「NOTIFYICONDATAW」原文）：
	//   LOWORD(lParam) = 通知事件；HIWORD(lParam) = 图标 ID（16 位）；
	//   "GET_X_LPARAM(wParam) returns the X anchor coordinate for notification events
	//    NIN_POPUPOPEN, NIN_SELECT, NIN_KEYSELECT, and all mouse messages between
	//    WM_MOUSEFIRST and WM_MOUSELAST"（键盘生成时 = 目标图标左上角）。
	//   ⚠️ 其余消息的 wParam undefined —— 含 WM_CONTEXTMENU（见下方兜底分支）。
	const UINT notifyEvent = LOWORD(lParam);
	const int x = GET_X_LPARAM(wParam);
	const int y = GET_Y_LPARAM(wParam);

	TrayEventType type{};

	switch (notifyEvent){
		case NIN_SELECT:      type = TrayEventType::Select;      break;
		case NIN_KEYSELECT:   type = TrayEventType::KeySelect;   break;
		case WM_LBUTTONDBLCLK: type = TrayEventType::DoubleClick; break;
		case WM_CONTEXTMENU:  type = TrayEventType::ContextMenu; break;
		default: return;   // 其它通知（NIN_POPUPOPEN 等——非目标）静默忽略
	}

	// ★ v1.1（O-5 核实的新发现）：WM_CONTEXTMENU 不在 MSDN 坐标有效列表内
	//   （"For all other messages, wParam is undefined"）⇒ 此处 x/y 无效，
	//   以 GetCursorPos() 兜底（右键上下文菜单时光标必在图标处——Win32 标准做法）。
	if (type == TrayEventType::ContextMenu){

		POINT cursor{};

		if (GetCursorPos(&cursor)){

			m_lastAnchorX = cursor.x;

			m_lastAnchorY = cursor.y;

			EmitTrayEvent(TrayEvent(type, cursor.x, cursor.y));   // ← 上行（D3 的 sink）

			return;

		}

		// GetCursorPos 失败（罕见）→ 落到下方用最近一次有效锚点

	}

	m_lastAnchorX = x;
	m_lastAnchorY = y;

	EmitTrayEvent(TrayEvent(type, x, y));   // ← 上行（D3 的 sink）
}
```

> ⚠️ **待详设核实**：`NOTIFYICON_VERSION_4` 下 `wParam` 携带坐标的**具体打包方式**（MSDN 表述为「`wParam` = 锚点」）——本文按 `GET_X_LPARAM/GET_Y_LPARAM(wParam)` 取（与既有 `WM_NCHITTEST` 同款）。**详设前须用 spike 或 MSDN 精确核对**，见 §8 开放决策点 O-5。

### 3.3 `TaskbarCreated` 自愈（R6 / D9）

```cpp
	if (msg == m_taskbarCreatedMsg && msg != 0){

		// ★ D9 双态模型：只按 **desired state** 恢复（绝不因广播本身重加）
		if (m_trayDesired && m_trayIcon != nullptr){

			NOTIFYICONDATAW nid = BuildIconData();
			Shell_NotifyIconW(NIM_ADD, &nid);
			nid.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW(NIM_SETVERSION, &nid);
			m_trayRegistered = true;

			Logger::Log(LogLevel::Info, L"Tray: re-registered after TaskbarCreated");

		} else {
			// desired = false（应用已主动移除）⇒ **绝不能重加**（D9 表格末行）
			// 只同步内部状态：shell 重建后图标必然已不存在
			m_trayRegistered = false;
		}

		return 0;
	}
```

### 3.4 托盘菜单（R5 / D6 / D10）

```cpp
int Win32PlatformApplication::ShowTrayMenu(const TrayMenu& menu){

	if (m_trayHostHwnd == nullptr) return 0;

	if (menu.items.empty()) return 0;

	HMENU hMenu = CreatePopupMenu();

	// 一级 + 纯文本 + ID（D6 范围边界——不加图标/勾选/子菜单）
	for (const auto& item : menu.items){

		if (item.id <= 0) continue;   // 非法 ID 忽略（保证 0 能安全表示「未选中」）

		AppendMenuW(hMenu, MF_STRING, static_cast<UINT_PTR>(item.id),
			UTF8ToWide(item.text).c_str());

	}

	// ★ TrackPopupMenu 的前置要求（MSDN 明确）：必须先 SetForegroundWindow，
	//   否则菜单不会随点击外部而消失（经典 Win32 陷阱）
	SetForegroundWindow(m_trayHostHwnd);

	const int cmd = TrackPopupMenu(
		hMenu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,   // ← TPM_RETURNCMD：结果不走 WM_COMMAND（D6/D10）
		m_lastAnchorX, m_lastAnchorY, 0, m_trayHostHwnd, nullptr);

	// 菜单关闭后必须补发 WM_NULL（MSDN 配套要求——让系统完成菜单状态清理）
	PostMessageW(m_trayHostHwnd, WM_NULL, 0, 0);

	DestroyMenu(hMenu);

	return cmd;   // 0 = 未选中 / 取消
}
```

### 3.5 `Win32PlatformWindow`：`Hide()`（R12）

```cpp
void Win32PlatformWindow::Hide(){

	// 与 Show 对称（K8）——但**不改 m_shown**（运行期标记不因隐藏而回退）
	if (m_hwnd != nullptr){

		ShowWindow(m_hwnd, SW_HIDE);

	}

}
```

**与既有 API 的语义边界**（本节是初设必须写清的对照，避免消费者混用）：

| API | 时机 | HWND | 可再显示 | 典型用途 |
|---|---|---|---|---|
| `Hide()` | 运行期 | **存活** | ✅ `Show()` | 最小化到托盘 / 临时隐藏 |
| `Release()` | 任意（幂等） | **销毁** | ❌ | 关窗（`OnWindowCloseRequested` 走这条） |
| `Minimize()` | 运行期 | 存活 | ✅ | 最小化到任务栏 |

### 3.6 `Win32PlatformWindow`：拖入（R8 / R9 / R10 / R11）

**开关**：

```cpp
void Win32PlatformWindow::SetFileDropEnabled(bool enabled){

	// 运行期契约（与 Minimize 同组——见 §8 O-3）
	if (!m_shown){ /* Warning + return */ }

	if (m_hwnd != nullptr){

		DragAcceptFiles(m_hwnd, enabled ? TRUE : FALSE);   // 幂等（系统内部就是开关）

	}

}
```

**`WM_DROPFILES` 分支（在 `HandleMessage` 的**前置**处理区，与 `WM_NCCALCSIZE` 同级——不进消息翻译器）**：

```cpp
	case WM_DROPFILES: {

		HDROP hDrop = reinterpret_cast<HDROP>(wParam);

		// ① 路径列表（UTF-8——边界转换在本层，R9）
		const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
		std::vector<std::string> paths;
		paths.reserve(count);

		for (UINT i = 0; i < count; ++i){

			const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);   // 不含终止符的长度
			std::wstring buf(len + 1, L'\0');
			DragQueryFileW(hDrop, i, buf.data(), static_cast<UINT>(buf.size()));
			buf.resize(len);
			paths.push_back(WideToUTF8(buf));

		}

		// ② 落点（**客户区坐标**——DragQueryPoint 的既有语义，与鼠标事件同系，R9）
		POINT pt{};
		DragQueryPoint(hDrop, &pt);

		// ③ ★ DragFinish **必须在事件抛出之前**（R10——资源边界闭合）
		DragFinish(hDrop);

		// ④ 抛 Framework Event（走既有上行链路——R11）
		// v1.1（O-6 拍板 C）：Window* 经宿主引用直接获得——O-6 三路径收敛为 C
		DropFilesEvent event(&m_host.GetWindow(), std::move(paths), pt.x, pt.y);
		m_host.OnEvent(event);   // ← Window::OnEvent → Application::OnEvent

		return 0;

	}
```

> ✅ **v1.1 已解决（O-6 拍板 C）**：`PlatformWindowHost` 新增 `GetWindow()`（§2.10），构造处直接 `&m_host.GetWindow()`——`Window::OnEvent` **零改动**，O-6 三路径收敛为 C（B const_cast 淘汰）。

### 3.7 `Application`：退出策略（R13）

**唯一改动点（`OnWindowDestroyed` 末尾——最小修改面）**：

```cpp
	// 所有窗口都关闭了，退出消息循环
	if (m_windows.empty()){

		// ↓ 新增：常驻应用可关闭隐式退出（R13；默认 true = 零行为变更）
		if (m_quitOnLastWindowClosed){

			Exit();

		}

	}
```

**契约**：
- 默认 `true` ⇒ 既有行为**逐字不变**（零回归）。
- **只影响隐式退出**；显式 `Application::Exit()` 不受该开关影响（应用仍可主动退出）。
- 与 `Hide()` 的正交关系：`Hide()` 不触发 `OnWindowDestroyed`（窗口仍在册）⇒ **「隐藏主窗口 + 常驻托盘」组合不需要 `SetQuitOnLastWindowClosed(false)`**；只有当窗口被**真正销毁**（`Release()`）才需要它。这是初设的重要澄清，见 §6.6。

---

## 4. 链接库传播（shell32）

| 项 | 内容 |
|---|---|
| **需要新增的库** | **`shell32`**——`Shell_NotifyIconW` / `DragAcceptFiles` / `DragQueryFileW` / `DragQueryPoint` / `DragFinish` 全部归属 shell32 |
| **落地位置** | `CMakeLists.txt:56` 之后追加一行：`target_link_libraries(ECDI PUBLIC shell32)` |
| **传播属性** | **`PUBLIC`**——与既有 `user32` / `dwmapi` 同款（静态库模型下的既有惯例） |
| **vs 项目** | 既有 MSVC 工程（`ECDI.vcxproj`）若手工维护了 `AdditionalDependencies`，须同步追加 `shell32.lib`（P2 收尾项，详设确认） |
| **`shlwapi` 冲突检查** | 已在库（Phase 11 引入）——**不冲突**，两者可并存 |

---

## 5. 影响面

| 类别 | 项 | 规模 |
|---|---|---|
| **新增 Public 头** | `Application/TrayIcon.h` · `EventSystem/Application/TrayEvent.h` · `EventSystem/Window/DropFilesEvent.h` | **+3（86 → 89）** |
| **修改 Public 头** | `PlatformWindow.h`（+2 纯虚）· `PlatformApplication.h`（+3 纯虚 + 1 注入 + 1 上行）· `PlatformWindowHost.h`（+1 纯虚 `GetWindow`——v1.1 O-6）· `Window.h`（+2 透传）· `Application.h`（+4 透传 + 1 虚方法 override + 1 成员）· `EventType.h`（+2 枚举）· `EventRouter.h`（+2 虚方法） | **7 个** |
| **⚠️ 测试替身同步** | `PlatformWindow` 加 2 纯虚 ⇒ `AnimationTests::TestPlatformWindow` / `ProgressBarTests::TestPlatformWindow` **各补 2 个 override**（K3）；`PlatformWindowHost` 加 1 纯虚 ⇒ `EventTests::FakeHost` **补 1 个 override**（v1.1，O-6） | **3 处** |
| ✅ **测试替身零影响** | `PlatformApplication` 加纯虚 ⇒ **零替身**（K2——全库仅 1 个生产实现）；`EventRouter` 加虚方法全默认空实现 ⇒ 零影响 | **0 处** |
| **新增测试文件** | `src/Tests/TrayTests.cpp`（托盘状态机 + Shell 替身）· `src/Tests/DropFilesTests.cpp`（拖入 + HDROP 生命周期） | **+2 文件** |
| **修改 Internal** | `Win32PlatformApplication.h/.cpp`（宿主窗口 + 托盘状态 + 回调消化 + 菜单 + 自愈）· `Win32PlatformWindow.h/.cpp`（`Hide()` + 拖入开关 + `WM_DROPFILES`）· `Application.cpp`（托盘透传 + sink 注册 + 退出开关）· `Window.cpp`（透传） | **4 文件** |
| **构建** | `CMakeLists.txt`（+shell32；测试源 glob 自动纳入 2 新测试文件）· `ECDI.vcxproj`（+3 头 + 2 cpp + shell32.lib——若手工维护） | **2 文件** |
| **明确不动** | 渲染四层（Widget → PaintContext → CommandBuffer → Renderer → Backend）· `Window` 所有权契约（B）· Phase 12 四消息拦截骨架 · Phase 13 命中委托与 `CaptionBar` · 布局与尺寸体系 · `Win32WindowClass`（主窗口类不变——宿主用**独立** `WindowClass` 实例，K10） | — |

---

## 6. 生命周期与销毁顺序（本阶段技术含量区）

> GPT 评审点名的 ①②（`PlatformApplication` 生命周期 + 宿主 HWND 创建/销毁 + sink 时序）在此集中定义。**这份时序是详设必须逐字落地的契约，不允许留到实现阶段临时决定**（需求稿 D3 已写明）。

### 6.1 对象与资源的创建顺序

```
Application::Application()                       [Application.cpp:26-32]
  │
  ├─ ① m_platformApplication = make_unique<Win32PlatformApplication>()
  │      └─ 构造体：**无 HWND 创建**（懒创建——见 6.2）
  │
  ├─ ② m_platformApplication->SetDeferredCleanup([this]{ ProcessDeferredDestroy(); })
  │
  └─ ③ 【新增】m_platformApplication->SetTrayEventSink(
  │        [this](const TrayEvent& e){ OnEvent(e); })   ← 经既有 EventRouter 分派
  │
  └─ ④ 【新增】m_platformApplication->SetQuitPolicy(...)  ← 若 R13 采用推模式
```

**契约 A（sink 建立时点）**：`SetTrayEventSink` 必须在 `Application` 构造体内完成（早于任何 `Run()` / `SetTrayIcon`）⇒ **sink 被调用时 `Application` 对象必然存活**。

**契约 B（sink 内容）**：sink 的 lambda **只捕获 `this`**（`Application` 地址在对象生命周期内稳定——`Application` 不可拷贝不可移动）；sink 内**只调用 `OnEvent(event)`**——不直接触碰平台资源。

### 6.2 宿主 HWND 的创建时机（**懒创建**）

| 方案 | 成本 | 判定 |
|---|---|---|
| A 构造即创建 | 不使用托盘的每个应用都要注册窗口类 + 建 HWND | ❌ 违背"零消费者零开销"惯性（动画系统同款原则） |
| **B 首次 `SetTrayIcon` 时创建（懒）** | 用到才有成本 | ✅ **采纳** |
| C `Run()` 时创建 | 与托盘使用无关，时机突兀 | ❌ |

**懒创建的可重入性**：`EnsureTrayHost()` 以 `if (m_trayHostHwnd != nullptr) return;` 守卫 ⇒ 多次 `SetTrayIcon` 只创建一次。

### 6.3 事件 sink 的建立与清理时序（D3）

| 时点 | 动作 | 理由 |
|---|---|---|
| `Application` 构造 | `SetTrayEventSink(...)` | 保证 sink 早于一切上行可能 |
| 运行期 | sink 被 Shell 回调触发 → `EmitTrayEvent` | sink 内只做 `OnEvent` |
| **`~Application` 之前**（`PlatformApplication` 销毁前） | **清空 sink** | 防止宿主窗口销毁过程中回调访问已析构 `Application` |

**实现路径（待详设，§8 O-1）**：`Application` 的析构是 `~Application() = default`（在 .cpp 中，`PlatformApplication` 完整可见）。清空动作有两个落点：
- (a) `~Application` 显式析构体内先 `m_platformApplication->SetTrayEventSink(nullptr)` 再让 `m_platformApplication` 成员析构；
- (b) `~Win32PlatformApplication` 自己在销毁宿主窗口前清空（但它不知道 owner 是否存活——不如 (a) 明确）。

**倾向 (a)**：由 `Application` 显式管理自己的 sink 生命周期——**谁注册谁清理**。这也让 `= default` 变为显式析构体（一行改动）。

### 6.4 托盘图标与宿主 HWND 的销毁顺序（R1 / D9）

```
~Application()                                       [待改：显式析构体]
  │
  ├─ ① m_platformApplication->SetTrayEventSink(nullptr)   ← 先断上行（6.3）
  │
  ├─ ② m_platformApplication 析构 → ~Win32PlatformApplication
  │      │
  │      ├─ 2.1 **若 m_trayRegistered ⇒ Shell_NotifyIconW(NIM_DELETE)**   ← R1 硬要求（幽灵图标）
  │      │      （无论 desired 状态如何——只要 shell 里还注册着就必须删）
  │      │
  │      ├─ 2.2 若 m_trayIcon 为 owned（非 LR_SHARED）⇒ DestroyIcon(m_trayIcon)   ← D4
  │      │
  │      ├─ 2.3 **DestroyWindow(m_trayHostHwnd)**   ← 先于窗口类反注册（K10：UnregisterClassW 要求无存活窗口）
  │      │
  │      └─ 2.4 m_trayHostClass 析构 ⇒ UnregisterClassW   ← 顺序不可颠倒
  │
  └─ ③ m_windows / m_deferredDestroy 析构（既有路径不变）
```

**契约 C（顺序不变量）**：`NIM_DELETE` **先于** `DestroyWindow`；`DestroyWindow` **先于** `UnregisterClassW`。
- 前者理由：托盘回调依赖宿主 HWND 有效（`NOTIFYICONDATAW::hWnd`）；HWND 先销毁会让 shell 侧的删除失去目标（虽不致命，但违反 R1「析构必删」的确定性）。
- 后者理由：K10——`UnregisterClassW` 在有存活窗口时**会失败**，静默留下未反注册的类。

**契约 D（`m_trayRegistered` 与 `m_trayDesired` 的正交性——D9）**：

| desired | registered | 含义 | 收到 `TaskbarCreated` |
|---|---|---|---|
| false | false | 从未设置 / 已移除 | **不重加**（同步 registered=false） |
| true | true | 正常态 | 重加（幂等） |
| true | false | explorer 刚重建 | **重加**（自愈） |
| false | true | （不应出现——移除时同步两态） | 按 desired=false 处理：**不重加**并纠正 registered=false |

### 6.5 拖入事件的链路（R11——与实测链路一致）

```
WM_DROPFILES                                            [系统 → 窗口过程]
  → Win32PlatformWindow::HandleMessage（§3.6）
      ├─ DragQueryFileW × N → UTF-8 路径列表
      ├─ DragQueryPoint → 客户区落点
      └─ DragFinish（★ 先于事件构造——R10）
  → DropFilesEvent
  → PlatformWindowHost::OnEvent（Window 实现）
  → Window::OnEvent → m_application.OnEvent(event)      [Window.cpp:448-452]
  → Application::OnDropFiles（EventRouter 虚方法）
  → HitTest（RootWidget::HitTest，按落点逆序）
  → Dispatch → Bubbling（Application 内 while + GetParent）
```

**与 R11 的关系**：链路**完全复用**既有事件上行通道（`PlatformWindowHost::OnEvent`），**未新增任何通道**。§3.6 的 `Window*` 补填问题是实现细节，不改变链路拓扑。

### 6.6 `Hide()` / `Release()` / 退出策略的三角关系（关键澄清）

| 场景 | 调用 | `OnWindowDestroyed` | `m_windows` | 隐式退出 | 需要 `SetQuitOnLastWindowClosed(false)`？ |
|---|---|---|---|---|---|
| 关窗（正常） | `RequestClose()` → `Release()` | ✅ 触发 | 移除 | 若空 ⇒ 退出 | 是（若要常驻） |
| 隐藏到托盘 | `Hide()` | ❌ **不触发** | **保留** | **不会发生** | **否**（窗口仍在册） |
| 最小化 | `Minimize()` | ❌ | 保留 | 不会 | 否 |

⇒ **重要推论**：「点关闭按钮 → 隐藏到托盘」这一最典型用法，应用侧应把 `WindowCloseRequestedEvent` 处理为 `Hide()`（而非让框架走 `Release()`）——此时**根本不需要 R13 的开关**。R13 真正服务的场景是「窗口确实被销毁、但应用要活到托盘交互结束」。

**这条推论需要在详设中落到文档/示例，否则 R13 会被误用为「常驻必须开关」**（见 §8 O-2 的取舍）。

### 6.7 平台能力失败语义（v1.1 新增——评审 🔴 收敛项 ③）

**统一原则**：**平台能力失败不抛异常，通过内部状态 + 日志反映**——与既有先例同源（`EnsureBackendExtracted` 失败仅记日志、`Win32WindowClass` 的 `LoadImageW` 失败 NULL 自动降级）。

| 失败点 | 处置 | 内部状态 |
|---|---|---|
| `NIM_ADD` 失败 | Warning；不抛 | `desired` 保持 `true`；`registered` **保持 `false`**——下次 `TaskbarCreated` 或重试可恢复（**不得置 `true`**：否则内部状态与 Shell 状态分离，自愈失效） |
| `NIM_MODIFY` 失败 | Warning；不抛 | **两态均不改**——单次失败不足以推断 Shell 状态；`TaskbarCreated` 是权威重置信号 |
| `NIM_DELETE` 失败 | Warning；不抛 | `registered = false`（向「已移除」收敛）；**析构流程继续，不阻塞**（R1 已尽力——剩余风险 = 幽灵图标，记账） |
| 宿主 `CreateWindowExW` 失败 | Error 日志；`SetTrayIcon` 直接返回 | `desired = true`、`registered = false`、**不崩溃**——下次调用重试 |
| `LoadImageW` 失败 | Warning；**降级系统默认图标** `LoadIconW(nullptr, IDI_APPLICATION)` | 与 `Win32WindowClass.cpp` 既有先例同款 |
| `CreatePopupMenu` 失败 | 直接返回 0 | 与「未选中 / 取消」语义天然兼容 |
| `RegisterWindowMessageW` 返回 0 | Warning；自愈禁用 | 正常注册 / 移除路径不受影响 |
| `GetCursorPos` 失败（§3.2 兜底） | 使用最近一次有效锚点 | 不影响事件抛出 |

**与 O-5 的关系**：`TaskbarCreated` 自愈（§3.3）天然承担「失败后重试」的恢复职责——explorer 每次重建都会广播，失败状态（`registered = false`）恰是自愈的触发前提之一。

---

## 7. 测试方向

### 7.1 自动测试（分层——需求 §6 的三层结构）

| 层 | 文件 | 内容 | 判据 |
|---|---|---|---|
| **① 平台状态机** | `TrayTests.cpp` | `Absent/Active` × `SetTrayIcon/RemoveTrayIcon` 四组合 | 状态转移正确；`Remove(未注册)` 不产生 Shell 调用 |
| **② Shell API 调用** | `TrayTests.cpp` | 以**替身**记录 `NIM_ADD` / `NIM_MODIFY` / `NIM_DELETE` / `NIM_SETVERSION` 序列 | 首次 = ADD + SETVERSION；再次 = MODIFY；移除 = DELETE 且随后移除为 no-op |
| **③ `TaskbarCreated` 恢复** | `TrayTests.cpp` | 合成 `m_taskbarCreatedMsg` 消息（`SendMessage` 到宿主 HWND） | desired=true ⇒ 重加；desired=false ⇒ **不重加**（D9 回归锚） |
| **④ 拖入事件** | `DropFilesTests.cpp` | 构造 `HDROP`（`DROPFILES` 结构 + 内存布局）并 `SendMessage(WM_DROPFILES)` | 路径列表 UTF-8 正确、落点正确、**`DragFinish` 已执行**（经测试缝计数——不在测试里窥探系统堆） |
| **⑤ 拖入默认关闭** | `DropFilesTests.cpp` | 未启用的窗口收到 `WM_DROPFILES` | 无事件、无异常 |
| **⑥ 派发路径** | `DropFilesTests.cpp` | 拖到子控件 / 空白区 | 走既有 HitTest + Bubbling（与鼠标事件同族断言） |
| **⑦ `Hide()` 契约** | `WindowChromeTests.cpp` 或新文件 | `Hide()` 后 HWND 仍有效、可再 `Show()`；`Hide()` 不触发 `OnWindowDestroyed` | 与 `Release()` 的语义边界（§3.5 表）逐条断言 |
| **⑧ 退出开关** | 新文件或既有 | `SetQuitOnLastWindowClosed(false)` + 销毁最后窗口 | 不退出；默认 `true` 时退出（两态可断言） |
| **回归** | 全部既有 | 188 用例 | **零回归**（重点：窗口销毁路径 / `PlatformApplication` 契约 / 事件分派） |

### 7.2 测试替身策略

| 接口 | 策略 |
|---|---|
| `PlatformWindow`（+2 纯虚） | 2 个既有替身**必须**补 override（K3）；新增 `DropFilesTests` 若需替身，复用同款 |
| `PlatformApplication`（+3 纯虚） | 加一个**测试替身** `RecordingPlatformApplication`（记录 Shell 调用序列）——**本阶段新增**（K2：此前零替身） |
| Shell 调用 | 经**内部测试缝**（`Win32PlatformApplication` 的虚方法或函数指针注入口，归详设）使 Shell 调用可替身——**不在测试里真的 `Shell_NotifyIconW`**（需求 §6 分层原则） |

> ★ **Shell seam 纪律（v1.1）**：上述内部测试缝**停留在实现层**（`Win32PlatformApplication` 内部的函数指针 / 虚方法注入口），**不得演变为公开抽象**——分层保持 `Public API → PlatformApplication → Win32PlatformApplication → Internal Shell seam → Shell32`。

> ★ **详设待办（v1.1）**：ModelProbe 演示 R13 的两种常驻模式——「模式 A：真关闭窗口但应用活着（`SetQuitOnLastWindowClosed(false)`）」与「模式 B：关闭按钮变成隐藏（`WindowCloseRequestedEvent` 处理为 `Hide()`）」——两者是不同机制，文档须防止混用（§6.6）。

### 7.3 手测（真环境——不经自动测试）

| 项 | 判据 |
|---|---|
| ModelProbe 接入托盘（现成 `app.ico` 资源） | 通知区可见图标；悬停显示 tip；左键/右键有响应 |
| 右键菜单 | 弹出位置在图标处；选中返回 ID；点外部取消（返回 0） |
| **explorer 重启后图标仍在** | 任务管理器重启资源管理器 → 图标自动恢复（**这次预期"自愈成功"**——与 R10 桌面层 A 路线"被杀"形成对照） |
| 拖入文件 | 拖到窗口 → 日志显示 UTF-8 路径 + 落点；拖到 TextBox vs 空白区派发目标不同 |
| **UIPI 提权场景** | **平台约束验证**（非"预期失败"）：确认「不承诺跨完整性级别拖入成功」，结果以系统过滤行为为准 |
| 幽灵图标 | 应用退出后图标**立即**消失（无需鼠标划过） |
| 四工具链 | MSVC / ClangCL / Clang / MinGW 全绿 + 断言启用核验（skill 条 35/50） |

---

## 8. 开放决策点（v1.1 收敛：7 项拍板 + 1 项遗留）

### 8.1 v1.1 拍板（7 项）

| # | 决策 | 拍板 | 依据 |
|---|---|---|---|
| O-1 | sink 清理落点 | **A**（`~Application` 显式析构体先清 sink） | 「谁注册谁清理」——平台层不知道 owner 是否存活；外部评审同判 |
| O-2 | R13 去留 | **保留**（默认 `true`） | 1 bool + 1 if 成本极低；与 `Hide()` 的分工见 §6.6；ModelProbe 演示两模式（§7.2 详设待办） |
| O-4 | 托盘 API 形态 | **A** 两方法（`SetTrayIcon` / `RemoveTrayIcon`） | D11 宽容语义的必然推论（Add 重复 = 隐式 Update ⇒ Add/Update 是同一操作） |
| O-5 | v4 坐标打包 | **已核实**（§3.2 / §2.2 落地） | MSDN 原文证实打包方式与本稿一致；**新发现 `WM_CONTEXTMENU` 无坐标 ⇒ `GetCursorPos` 兜底**；坐标空间按屏幕坐标设计（`TrackPopupMenu` 直接消费），详设实测一次确认 |
| O-6 | `DropFilesEvent::Window*` 补填 | **C**（`PlatformWindowHost::GetWindow()`，§2.10） | 表达既有事实、不为拖入新造机制；影响面 = FakeHost 补 1 override（grep 实证）；B（const_cast）淘汰 |
| O-7 | `WM_ENDSESSION` 清理 | **不做**（YAGNI） | 关机时 shell 自行清理托盘图标 |
| O-8 | 多托盘图标 | **不做**（维持非目标） | 二次用例再抽象（YAGNI） |

### 8.2 唯一遗留

**O-3（拖入开关时机分组）**保留原判 **A（运行期，与 `Minimize` 同组）**——v1.1 注记：这是**架构一致性选择**而非 Win32 技术限制（`DragAcceptFiles` 本身两期皆可调）；第二个平台若发现 Drop 属配置期能力，可重新评估（外部评审同建议）。

（以下为 v1.0 原始 8 项开放决策表，**保留供对照**——其「倾向」列已被 8.1 拍板取代或确认；O-3 仍开放。）

| # | 决策 | 选项 | **倾向** | 理由 |
|---|---|---|---|---|
| **O-1** | sink 清理落点（§6.3） | **A** `~Application` 显式析构体先清 sink / **B** `~Win32PlatformApplication` 自行清 | **A** | 「谁注册谁清理」——`Application` 知道自己的对象是否存活，平台层不知道；且 (a) 把 `= default` 改为显式析构体，一行改动，最小 |
| **O-2** | R13 是否还需要（§6.6） | **A** 保留（默认 true） / **B** 砍掉（`Hide()` 已足够） | **A 保留** | 「关窗 = 销毁」是既有语义，消费者改写成 `Hide()` 需要理解 `WindowCloseRequestedEvent` 的处理技巧；R13 提供**声明式**的常驻开关（成本极低：1 个 bool + 1 个 if）。但**必须在示例/文档里写明它与 `Hide()` 的分工**，避免被误当成"常驻必开" |
| **O-3** | 拖入开关的时机分组（§3.6） | **A** 运行期（Show 后，与 `Minimize` 同组） / **B** 配置期（与 chrome 四件套同组） / **C** 无门控（两期均可） | **A 运行期** | `DragAcceptFiles` 本身两期都可调，但**「启用/停用交互模式」语义上是运行期操作**（类似 `Minimize`——都是对已显示窗口的操控）；选 A 可以让契约三件套（`@pre` / 实现 / 测试）与既有分组完全对齐，**不新造"C 无门控"这一第三分组**（避免破坏 Phase 12 建立的「配置期/运行期对称」纪律） |
| **O-4** | 托盘 API 形态（§2.5） | **A** `SetTrayIcon` + `RemoveTrayIcon`（两方法） / **B** `Add` / `Update` / `Remove`（三方法，照需求 R1 字面） | **A 两方法** | **D11 的宽容语义已经蕴含此结论**——"Add 重复 = 隐式 Update"意味着 Add 与 Update 是同一操作；保留两个名字会让消费者猜"该调哪个"。这不是削减需求，而是把 D11 的语义**在 API 形态上兑现** |
| **O-5** | `NOTIFYICON_VERSION_4` 的坐标打包（§3.2） | 待核实：`wParam` 是否可直接 `GET_X_LPARAM/GET_Y_LPARAM` | **待详设核实** | MSDN 表述为「`wParam` 携带锚点坐标」，但**具体打包方式（LOWORD/HIWORD 顺序、是否屏幕坐标）必须核实后再写实现**——本轮未验证，**不写入契约** |
| **O-6** | `DropFilesEvent::Window*` 的补填路径（§3.6） | **A** `PlatformWindowHost` 增加"事件前置补窗"机会 / **B** `Window::OnEvent` 对 `DropFilesEvent` 做 `const_cast` 补填 / **C** `DropFilesEvent` 构造时经 `PlatformWindowHost` 取 Window（需 Host 暴露 `Window&`） | **待详设（倾向 C 或 A）** | 这是**本阶段唯一触碰既有事件链路的点**。C 最干净但要求 `PlatformWindowHost` 暴露 Window 引用（只读）；B 用 `const_cast` 绕过 `Event` 的 const 语义，风格差。**详设必须给最小修改面 + 说明为何不动 `Event` 基类构造语义** |
| **O-7** | `TaskbarCreated` 与其他消息共存（§3.1） | 宿主窗口是否还需要处理 `WM_ENDSESSION` / `WM_QUERYENDSESSION`（系统关机时删图标） | **归详设评估** | MSDN 建议应用在会话结束时清理托盘图标；但**系统关机时 shell 会自行清理**，实际收益低 ⇒ 倾向不做（YAGNI），详设确认 |
| **O-8** | 是否支持多托盘图标 | 需求 §5 已列**非目标**（单图标） | **维持非目标** | 结构上已预留（`uID` 固定值 + `m_trayRegistered` 单态）；二次用例出现再抽象（YAGNI） |

---

## 9. D0–D11 兑现表

| 决策 | 需求稿倾向 | 本文兑现方式 | 状态 |
|---|---|---|---|
| **D0** | A（一 Phase 三组，顺序 拖入 → R12/R13 → 托盘） | §3 的实现编排即该顺序；§5 影响面按此分组 | ✅ 兑现 |
| **D1** | A（挂 `PlatformApplication`） | §2.5 新增应用级能力惯例块（与窗口级 R9 三步同构） | ✅ 兑现 |
| **D2** | A（自建隐藏顶层窗口） | §3.1 `EnsureTrayHost`（懒创建 + 独立 `WindowClass` 实例）；**硬契约写入 §2.5 注释**：永不进 `Application::m_windows`、不新增平台对象类 | ✅ 兑现 |
| **D3** | A（`std::function` sink） | §2.5 `SetTrayEventSink` + `EmitTrayEvent`（复刻 `SetDeferredCleanup` 模式）；生命周期见 §6.3 | ✅ 兑现 |
| **D4** | A（exe 资源 ID，默认同 `kAppIconId`） | §2.1 `TrayIconOptions::iconResourceId` 默认 102；HICON 生命周期见 §6.4 契约（2.2） | ✅ 兑现 |
| **D5** | A（`NOTIFYICON_VERSION_4`） | §3.2 `NIM_SETVERSION` 调用 + 回调翻译表（`NIN_SELECT`/`NIN_KEYSELECT`/`DBLCLK`/`CONTEXTMENU` → `TrayEventType`）；**坐标打包已核实（v1.1，MSDN 原文）——新发现 `WM_CONTEXTMENU` 无坐标 ⇒ `GetCursorPos` 兜底** | ✅ 兑现 |
| **D6** | A（原生一级菜单 + `TPM_RETURNCMD`） | §3.4 `CreatePopupMenu` + `AppendMenuW` + `TPM_RETURNCMD \| TPM_NONOTIFY`；范围限一级/纯文本/ID | ✅ 兑现 |
| **D7** | C（记录约束 + 文档明示） | §5「明确不动」+ §7.3 手测判据改为「平台约束验证」；**实现零动作** | ✅ 兑现 |
| **D8** | A（R12/R13 纳入本阶段） | §2.4/§2.6 `Hide()`；§2.7 `SetQuitOnLastWindowClosed`；§3.5/§3.7 实现 | ✅ 兑现 |
| **D9** | A（框架内建自愈 + 双态） | §3.3 自愈分支（**只按 desired 恢复**）+ §6.4 契约 D 的四态表 | ✅ 兑现 |
| **D10** | A（同步返回 ID） | §2.5 `ShowTrayMenu` 返回 `int`（0 = 未选中）；§3.4 实现含 `SetForegroundWindow`/`PostMessage(WM_NULL)` 两个 MSDN 必备配套 | ✅ 兑现 |
| **D11** | A（宽容语义） | §2.5/§3.2 状态机四组合表；**API 形态据此收敛为两方法（O-4）** | ✅ 兑现 |

---

## 10. 修订记录

- v1.1（2026-09-16）**外部评审「通过，可进详设」——3 个详设前必须收敛项全部处理**：
  - **O-5 核实（🔴→✅）**：MSDN `NOTIFYICONDATAW` 原文证实 v4 锚点 = `GET_X_LPARAM/GET_Y_LPARAM(wParam)`（与本稿 §3.2 逐字一致）；**核实挖出新问题**——`WM_CONTEXTMENU` 不在坐标有效列表（"For all other messages, wParam is undefined"）⇒ §3.2 补 `GetCursorPos()` 兜底分支、§2.2 头注释同步；坐标空间按屏幕坐标设计（`TrackPopupMenu` 直接消费），详设实测一次确认。
  - **O-6 拍板 C（🔴→✅）**：`PlatformWindowHost` 新增 `virtual Window& GetWindow() noexcept = 0`（§2.10 新小节）——表达「每个 PlatformWindow 必然对应一个宿主 Window」的既有事实；影响面 grep 实证 = 2 实现者（`Window` 实现 `return *this;` + `EventTests::FakeHost` 补 1 override）；§3.6 构造改 `&m_host.GetWindow()`；`Window::OnEvent` 零改动；B（const_cast）淘汰。
  - **失败语义契约（🔴→✅，评审新增点）**：§6.7 统一原则「平台能力失败不抛异常，通过内部状态 + 日志反映」+ 8 行失败处置表（ADD / MODIFY / DELETE / 宿主创建 / `LoadImageW` / `CreatePopupMenu` / `RegisterWindowMessageW` / `GetCursorPos`）。
  - **§3.1 笔误修正**：删除残留成员 `WindowClass m_trayHostClass;`（与 `unique_ptr` 懒创建设计冲突——评审抓到；成员统一命名 `m_trayHostClass`）。
  - **§8 收敛**：O-1 / O-2 / O-4 / O-5 / O-6 / O-7 / O-8 七项**拍板**；唯一遗留 O-3（保留 A 判 + 注记「架构一致性选择而非技术限制」）；v1.0 原表保留供对照。
  - **§5 影响面更新**：修改 Public 头 6 → **7**（+`PlatformWindowHost.h`）；测试替身同步 2 → **3 处**（+FakeHost）。
  - **§7.2** Shell seam 纪律（不得演变为公开抽象）；**详设待办**：R13 模式 A/B ModelProbe 演示。
  - **§9**：D5 兑现状态 ⚠️ → ✅。
- v1.0（2026-09-16）**初步设计初稿**：
  - §1 范围映射（R1–R13 × D0–D11 → 落点）+ **§1.1 勘察基线 K1–K14**（本文所有"现状"断言的代码证据，含行号）+ **§1.2 两条通道接缝形态对照**（本阶段结构核心）。
  - §2 **Public 头全文草案**：3 新增（`Application/TrayIcon.h` 86→87 · `EventSystem/Application/TrayEvent.h` 87→88 · `EventSystem/Window/DropFilesEvent.h` 88→89）+ 6 修改（`PlatformWindow.h` / `PlatformApplication.h` / `Window.h` / `Application.h` / `EventType.h` / `EventRouter.h`）——全部给**可评审的完整头代码**，含契约注释与 `@pre`。
  - §3 实现分解（按 D0 顺序：拖入 → R12/R13 → 托盘）：宿主窗口懒创建 + WndProc（`GWLP_USERDATA` 先例）· 托盘状态机四组合表 + `NOTIFYICONDATAW` 填充 + v4 翻译 · `TaskbarCreated` 自愈（只按 desired）· 菜单（含 `SetForegroundWindow` 与 `WM_NULL` 两个 MSDN 必备配套）· `Hide()` · `WM_DROPFILES`（`DragFinish` 先于事件）· 退出开关（单点最小改动）。
  - §4 链接库传播：**+`shell32`（PUBLIC）**——5 个 API 的唯一归属。
  - §5 影响面：**+3 头 / 6 头修改 / 2 处替身同步 / 0 处替身影响 / 2 新测试文件 / 4 内部文件 / 2 构建文件**（替身影响的两组结论均附 grep 证据 K2/K3）。
  - §6 **生命周期与销毁顺序**（GPT 点名的技术含量区 ①②）：创建顺序 · 宿主 HWND 懒创建取舍 · sink 建立/清理时序 · 销毁四步不变量（`NIM_DELETE` → `DestroyIcon` → `DestroyWindow` → `UnregisterClassW`）· 双态正交表 · 拖入链路（与 §6.5 实测链路一致）· **`Hide()`/`Release()`/退出开关三角关系澄清**。
  - §7 测试方向：**8 层自动测试**（状态机 / Shell 替身 / 自愈 / 拖入 / 默认关 / 派发路径 / `Hide()` / 退出开关）+ 替身策略（含 `PlatformApplication` **首次引入替身**）+ 7 项手测（UIPI 已改「平台约束验证」）。
  - §8 **开放决策点 O-1–O-8**（含 2 项标注「待详设核实」：O-5 v4 坐标打包 · O-6 `DropFilesEvent` 的 `Window*` 补填——**本阶段唯一触碰既有事件链路的点**）。
  - §9 **D0–D11 兑现表**：10 项全额兑现 + D5 部分（待 O-5 核实）。
  - **阶段边界自律**：本文给头全文草案（初步设计职责——skill 条 5/6），但**不给逐文件最小 diff 规格、不给测试用例坐标、不给实现全文**——全部归详细设计。
