# Phase 7.1.2 翻译器契约改造 — 详细设计

> 状态：v1.2（2026-08-15）｜**已实现并验证通过**（V1 编译零警告 + V3/V4 事件/IME 回归正常）
> 相关：phase7-messagehandler-requirements.md（C1-C6）/ phase7-messagehandler-preliminary-design.md（v1.1）
> 目标（GPT 验收）：**`Platform/` 零 `Application` 依赖** + **零 Window.h 依赖**（WM_IME 方案 B 移出后归零）

## 0. 实现前置事实（已核实 + GPT 三轮修订）

| # | 事实 | 处理 |
|---|---|---|
| F1 | 翻译器 cpp 10 处 `m_application->OnEvent(event)` | → `m_host.OnEvent(event)` |
| F2 | 翻译器 cpp include `ECDI/Application/Application.h`（第 4 行） | 移除（Application 依赖剥离） |
| F3 | 翻译器 cpp include `ECDI/Window/Window.h`（第 3 行）——WM_IME 直调（227/236 行） | **GPT 三轮方案 B：WM_IME 移出翻译器 → Window.h include 移除**（平台层彻底零 Window.h） |
| F4 | 翻译器头前置声明 `class Window; class Application;` | Window 保留（Handle 参数 + Event 构造）；Application 移除 |
| F5 | Win32PlatformWindow 构造 `(host, app, windowClass, ...)` + 成员 `m_application` | 去 app；`m_messageHandler(m_host)` |
| F6 | Window.cpp 构造 `make_unique<Win32PlatformWindow>(*this, app, ...)` | 去 app |
| F7 | EventRouter::OnEvent 签名 `const Event&` | Host::OnEvent 对齐 const |
| **F8** | 翻译器 cpp WM_IME 三个 case（225-243 行）直调 window | **移出**——到 Win32PlatformWindow::HandleMessage 状态同步区；翻译器只留 Translate→Event→Host |

## 1. 文件清单（GPT 三轮 + 用户迁移流程修订）

**迁移流程（用户 2026-08-15 指示 + GPT 问题 1）**：写新文件 → 测试使用 → 确认无依赖旧文件 → **旧文件先移到他处（如 `_legacy/`）再测试** → 确认无误后决定删除。**不在同一改动中删除旧文件**（Git 可恢复但没必要冒险）。

| 文件 | 动作 |
|---|---|
| `include/ECDI/Platform/Win32/WindowMessageHandler.h` | 新增（复制自旧 + 契约改造） |
| `src/Platform/Win32/WindowMessageHandler.cpp` | 新增（复制自旧 + 10 处替换 + 删 WM_IME case） |
| `include/ECDI/Window/WindowMessageHandler.h` | **暂留**（迁移流程：确认无依赖后移 `_legacy/`） |
| `src/Window/WindowMessageHandler.cpp` | **暂留**（同上） |
| `include/ECDI/Platform/PlatformWindowHost.h` | 改：+ `OnEvent(const Event&)` + `OnIMEComposition()` 纯虚 + 前置声明 Event |
| `include/ECDI/Platform/Win32/Win32PlatformWindow.h` | 改：去 `Application* app` + include 路径更新 |
| `src/Platform/Win32/Win32PlatformWindow.cpp` | 改：构造列表 + **HandleMessage 加 WM_IME case（方案 B）** |
| `include/ECDI/Window/Window.h` + `src/Window/Window.cpp` | 改：+ `OnEvent` override + `OnIMEComposition` override |
| `ECDI.vcxproj` | 改：ClCompile/ClInclude 路径指向新文件（旧文件从工程移除、文件暂留） |

## 2. 逐文件详细

### 2.1 `Platform/Win32/WindowMessageHandler.h`（新）

```cpp
#pragma once

#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/Platform/PlatformWindowHost.h"   // + 新增（Host& 成员/构造参数）

#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

#include <optional>

namespace ECDI{

class Window;   // 保留（Handle 参数 Window*——仅前置声明，cpp 不再 include Window.h）

/// @brief Win32 消息 → Framework Event 翻译器（7.1.2：纯翻译 + 经 Host 派发）
/// @details
/// 将 Win32 的 HWND/UINT/WPARAM/LPARAM 翻译为类型安全的 Framework Event，
/// 经 m_host.OnEvent() 派发（Dispatch 一级：翻译器 → 框架契约 → Window → Application）。
///
/// 职责边界（GPT 三轮方案 B——职责纯粹化）：
/// - 翻译：Win32 消息 → Framework Event（类型安全）
/// - 派发：m_host.OnEvent()（不再直连 Application——C1）
/// - ⚠️ WM_IME_* 已移出（7.1.2 方案 B）：IME 属输入法子系统（TSF/IMM/候选窗），
///   由 Win32PlatformWindow::HandleMessage 状态同步区处理（host.OnIMEComposition()）
/// - 不负责：Widget HitTest、事件传播、窗口内部状态维护
class WindowMessageHandler
{
public:
	explicit WindowMessageHandler(PlatformWindowHost& host) noexcept;

	/// @brief 处理一条 Win32 消息
	/// @return nullopt = 调用方走 DefWindowProc；有值 = 调用方用该值返回
	std::optional<LRESULT> Handle(
		Window* window,
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
	);

private:
	PlatformWindowHost& m_host;   ///< 框架契约（非拥有；替代 Application*）

	wchar_t m_pendingHighSurrogate = 0;   ///< 等待配对的 UTF-16 高位代理

	static MouseButton TranslateMouseButton(UINT message, WPARAM wParam);
	static KeyCode TranslateKeyCode(WPARAM wParam, LPARAM lParam);
	std::optional<char32_t> ConsumeCodeUnit(wchar_t unit);
};

}
```

### 2.2 `Platform/Win32/WindowMessageHandler.cpp`（新）

- include：自身头（新路径）+ **移除 `"ECDI/Window/Window.h"`（F3 方案 B——不再直调 window）** + **移除 `"ECDI/Application/Application.h"`（F2）** + 各 Event 头 + `<Windows.h>` + `<windowsx.h>`
- 构造：`WindowMessageHandler::WindowMessageHandler(PlatformWindowHost& host) noexcept : m_host(host) {}`
- **10 处替换**：`m_application->OnEvent(event)` → `m_host.OnEvent(event)`（58/68/85/104/122/140/164/185/202/216 行）
- **删除 WM_IME 三个 case（225-243 行）**（F8 方案 B——移出翻译器）
- 其余（TranslateMouseButton/TranslateKeyCode/ConsumeCodeUnit）**零改动**

### 2.3 `Platform/PlatformWindowHost.h`（改）

```cpp
#pragma once

namespace ECDI{

class Window;   // 前置声明——GetWindow 返回框架窗口
class Event;    // + 前置声明——OnEvent 引用参数

class PlatformWindowHost{
public:
	virtual ~PlatformWindowHost() = default;

	virtual void OnPaint() = 0;
	virtual void OnResized(int width, int height) = 0;
	virtual void OnExitSizeMove() = 0;
	virtual Window* GetWindow() const noexcept = 0;

	/// @brief 事件转发（7.1.2 Dispatch 一级；const 只读，与 EventRouter 对齐）
	virtual void OnEvent(const Event& event) = 0;

	/// @brief IME 组合发生（WM_IME_START/COMPOSITION；7.1.2 方案 B——IME 属输入法子系统，
	/// 由平台层状态同步区上报，非事件系统成员）→ Window::NotifyIMEComposition
	virtual void OnIMEComposition() = 0;
};

}
```

### 2.4 `Win32PlatformWindow.h`（改）

- include：`"ECDI/Platform/Win32/WindowMessageHandler.h"`（路径更新，原 ECDI/Window/...）
- 构造：去 `Application* app`（`(PlatformWindowHost& host, const WindowClass& windowClass, const std::string& title, int width, int height)`）
- 成员：移除 `Application* m_application`

### 2.5 `Win32PlatformWindow.cpp`（改）

- 构造列表：`m_host(host), m_messageHandler(m_host)`
- **HandleMessage 加 WM_IME case（方案 B）**：

```cpp
	case WM_IME_STARTCOMPOSITION:
	case WM_IME_COMPOSITION:
		// 7.1.2 方案 B（GPT 三轮）：IME 属输入法子系统——平台层状态同步区上报，
		// 不再经翻译器（翻译器职责纯粹：Translate→Event→Host）
		m_host.OnIMEComposition();   // → Window::NotifyIMEComposition
		return std::nullopt;         // 必须走 DefWindowProc（IME 内部状态机）

	case WM_IME_ENDCOMPOSITION:
		return std::nullopt;         // 预留通道（未来组合串内嵌用）
```

### 2.6 `Window.h`（改）

```cpp
// Host 实现区 + 2 声明：
void OnEvent(const Event& event) override;       ///< 事件转发（Transitional adapter）
void OnIMEComposition() override;                ///< IME 组合 → NotifyIMEComposition
// 前置声明区：+ class Event;
```

### 2.7 `Window.cpp`（改）

```cpp
// 构造：
auto platform = std::make_unique<Win32PlatformWindow>(
    *this, windowClass, title, width, height);   // 去 app

void Window::OnEvent(const Event& event){
    // Transitional adapter（GPT 二轮）：平台层经 Window 转发翻译后的事件，
    // 直到 Application 解耦（7.1.5）完成——最终派发目标可能变化。
    // 临时代码标记：非框架最终形态。
    m_application->OnEvent(event);
}

void Window::OnIMEComposition(){
    NotifyIMEComposition();   // 7.1.2 方案 B：Host 回调 → 既有框架逻辑
}
```

### 2.8 `ECDI.vcxproj`（改）

- ClCompile：`src\Window\WindowMessageHandler.cpp` → `src\Platform\Win32\WindowMessageHandler.cpp`
- ClInclude：`include\ECDI\Window\WindowMessageHandler.h` → `include\ECDI\Platform\Win32\WindowMessageHandler.h`

## 3. 实现顺序（Step 1-5，迁移流程）

| Step | 内容 | 验证 |
|---|---|---|
| 1 | Host + OnEvent + OnIMEComposition 纯虚 + 前置声明 Event | 编译（未引用不报错） |
| 2 | 新建 Platform/Win32/WindowMessageHandler.h/cpp（复制旧 + 改造：Host&/10 处替换/删 WM_IME/去 include）；**vcxproj 指向新文件（旧文件从工程移除、文件暂留）** | 编译（Win32PlatformWindow 还引用旧路径——预期报错） |
| 3 | 改 Win32PlatformWindow h/cpp（新 include 路径 + 去 app + WM_IME case）+ Window h/cpp（OnEvent/OnIMEComposition） | 编译（Application 构造还传 app——预期报错） |
| 4 | Window.cpp 构造去 app | 编译通过 → V1 |
| 5 | **迁移流程（用户指示）**：确认无旧文件依赖（grep）→ 旧文件移 `_legacy/` 目录 → 重测 → 确认后删除 | V2/V2.1 + V3/V4 全过 |

## 4. 验证（V1-V4 + V2.1）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | **grep 实证（GPT 三轮更严格）** | `grep -r "Application" include/ECDI/Platform src/Platform` **零命中**（含 Application&/Application/Application.h 全部形态） |
| V2.1 | **grep 实证（方案 B 强化）** | `grep -r "ECDI/Window/Window.h" src/Platform` **零命中**（原"恰 1 命中"→ 方案 B 归零） |
| V3 | 回归-事件 | 鼠标/键盘/字符/窗口事件经新链（翻译器 → Host::OnEvent → Window::OnEvent → Application）行为不变 |
| V4 | 回归-IME | 中文候选窗跟随光标 + 移动窗口归位（**经新链：Win32PlatformWindow → host.OnIMEComposition → NotifyIMEComposition**） |

## 5. 技术债（7.1.2 遗留——方案 B 后减少为 1 项）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| Window::OnEvent Transitional adapter（Application 仍是最终入口） | Window.cpp | 7.1.5 Application 解耦评估 |
| ~~翻译器 include Window.h~~ | ~~已消除（方案 B）~~ | — |
| ~~WM_IME 借道翻译器~~ | ~~已移出（方案 B）~~ | — |

## 6. 修订记录

- v1.0（2026-08-15）详细设计定稿：8 文件清单 + 逐文件代码蓝图 + Step 1-4 + V1-V4/V2.1。
- v1.1（2026-08-15，GPT 三轮 + 用户迁移流程）三处修订：① **迁移流程**——旧文件不删除，先移 `_legacy/` 再测再删（用户指示 + GPT 问题 1）② **V2 grep 更严格**——`"Application"` 零命中（GPT 问题 2，覆盖 Application&/include 全部形态）③ **WM_IME 方案 B（GPT 问题 3）**——移出翻译器到 Win32PlatformWindow 状态同步区（host.OnIMEComposition），翻译器职责纯粹化（Translate→Event→Host），**技术债从 3 项减至 1 项**，V2.1 从"恰 1 命中"变"零命中"。
- v1.2（2026-08-15）**验证通过**：实现 5 文件 + vcxproj（翻译器迁 Platform/Win32/，旧文件移 `_legacy/`）。实现期修 2 处：① Win32PlatformWindow WM_IME case 的 `return std::nullopt` → `break`（HandleMessage 返回 LRESULT 非 optional；break 走翻译器 → DefWindowProcW——IME 状态机必需）② **CMake GLOB 重复编译**（旧文件仍在 src/ 被 GLOB_RECURSE 扫入 → LNK2005）——迁移流程 Step 5 提前触发：旧文件移 `_legacy/` 解决。V2/V2.1 grep 双零命中（注释 9 处 "Application" 改"应用层"）；用户实测"功能都正常"（V3/V4）。
