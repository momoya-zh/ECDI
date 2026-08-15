# Phase 7.1.2 翻译器契约改造 — 初步设计

> 状态：v1.1（2026-08-15，GPT 二轮修订）｜待用户确认后进详细设计
> 相关：phase7-messagehandler-requirements.md（职责确认 C1-C6）
> 目标（GPT 验收）：**`Platform/` 目录零 `Application*` 依赖**——依赖链 Win32 → PlatformWindowHost → Window → Application

## P1 PlatformWindowHost + OnEvent

```cpp
// PlatformWindowHost.h：+ 前置声明 class Event; + 纯虚
virtual void OnEvent(const Event& event) = 0;   ///< 事件转发（Dispatch 一级：翻译器 → 框架；const 只读——C3 定稿）
```

签名与 `EventRouter::OnEvent(const Event&)` 对齐（const 引用，事件不可变数据模型）。

## P2 翻译器迁移 + 契约改造

**⚠️ 职责澄清（GPT 二轮）**：`WM_IME_*` **不属于事件系统，属于输入法子系统**（独立状态机：TSF/IMM/Candidate Window/System Caret）——暂时借道 WindowMessageHandler 直调 `window->NotifyIMEComposition()`（绕过事件系统）。7.1.2 不处理 IME（边界），但文档明示其定位，防后人误以为 IME = Event。

**物理迁移**：
- `include/ECDI/Window/WindowMessageHandler.h` → `include/ECDI/Platform/Win32/WindowMessageHandler.h`
- `src/Window/WindowMessageHandler.cpp` → `src/Platform/Win32/WindowMessageHandler.cpp`

**契约改造（.h）**：
```cpp
// 构造：Application* → PlatformWindowHost&（C1 定稿）
explicit WindowMessageHandler(PlatformWindowHost& host) noexcept;
// 成员：Application* m_application → PlatformWindowHost& m_host
// Handle 签名不变：Handle(Window* window, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
```

**契约改造（.cpp）**：10 处 `m_application->OnEvent(event)` → `m_host.OnEvent(event)`。

**include 调整**：
- 移除 `"ECDI/Application/Application.h"`（m_application 成员消失——**Application 依赖彻底从平台层剥离**）
- 保留 `"ECDI/Window/Window.h"`（**唯一残留**：WM_IME 直调 `window->NotifyIMEComposition()`——输入法子系统借道；且 Event 构造需 Window*）

## P3 Win32PlatformWindow 去 Application*

```cpp
// 构造签名：去掉 Application* app 参数（C2 + C1 联动）
Win32PlatformWindow(PlatformWindowHost& host, const WindowClass& windowClass,
                    const std::string& title, int width, int height);

// 成员：移除 Application* m_application；m_messageHandler(m_host)（构造列表）
// 头 include：WindowMessageHandler.h 路径更新为 ECDI/Platform/Win32/WindowMessageHandler.h
```

## P4 Window + OnEvent 转发

```cpp
// Window.h：public 或 Host 实现区 + 声明
void OnEvent(const Event& event) override;

// Window.cpp：
void Window::OnEvent(const Event& event){
    // Transitional adapter（GPT 二轮）：平台层经 Window 转发翻译后的事件，
    // 直到 Application 解耦（7.1.5）完成——最终派发目标可能变化（可能直接 EventRouter）。
    // 临时代码标记：非框架最终形态。
    m_application->OnEvent(event);
}
```

`Window.cpp` 构造：`make_unique<Win32PlatformWindow>(*this, windowClass, title, width, height)`（去 app 参数）。
`Window.cpp` include：`ECDI/Platform/Win32/WindowMessageHandler.h` 不再需要（Window.h 已无翻译器成员）——确认现有 include 清理。

## P5 构建 + 验证

- vcxproj：ClCompile `src\Window\WindowMessageHandler.cpp` → `src\Platform\Win32\WindowMessageHandler.cpp`；ClInclude 路径同步
- 验证（V1-V4 + V2.1）：
  | # | 验收项 | 判据 |
  |---|---|---|
  | V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
  | V2 | **grep 实证（GPT 验收）** | `Platform/` 下 `Application*` 零出现（grep -r "Application\*" ECDI/include/ECDI/Platform ECDI/src/Platform） |
  | **V2.1** | **grep 实证（GPT 二轮新增）** | `Platform/` 下 `Window.h` 引用**仅允许一处**（WM_IME 技术债——grep -r "ECDI/Window/Window.h" ECDI/src/Platform 应恰 1 命中；文档明示"Platform 零依赖"说法不成立，实际为"零 Application 依赖 + Window.h 唯一残留"） |
  | V3 | 回归-事件 | 鼠标/键盘/字符/窗口事件经新链（翻译器 → Host::OnEvent → Window::OnEvent → Application）行为不变 |
  | V4 | 回归-IME | 中文候选窗跟随光标 + 移动窗口归位（WM_IME 直调保持） |

## 边界（7.1.2 不做）

- ❌ 不动翻译器翻译逻辑本体（TranslateKeyCode/ConsumeCodeUnit/TranslateModifier）
- ❌ 翻译器 WM_IME 直调 `window->NotifyIMEComposition()` 保持现状（F1；技术债记账——平台层仍 include Window.h 仅为此）
- ❌ 不做 CaretGeometry（7.1.3）/ Backend 注入（7.1.4）/ Application 设计（D5 挂起）

## 技术债记账（7.1.2 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| 翻译器 include Window.h（WM_IME 直调 NotifyIMEComposition——**输入法子系统借道，非事件系统成员**） | Platform/Win32/WindowMessageHandler.cpp | 未来事件模型改造 / EditableTextWidget 抽象时评估（平台层认识框架具体类的唯一残留） |
| Window::OnEvent Transitional adapter（Application 仍是最终入口） | Window.cpp | 7.1.5 Application 解耦评估 |

## 修订记录

- v1.0（2026-08-15）初步设计定稿：P1-P5。核心：翻译器物理迁移 + 构造 Application* → Host& + 10 处派发目标替换；Win32PlatformWindow 去 Application*；Window +OnEvent 过渡转发。**V2 grep 验收 = Platform 零 Application 依赖**（GPT 验收标准）。
- v1.1（2026-08-15，GPT 二轮）：① **V2.1 验收新增**——Platform 下 Window.h 引用仅允许一处（WM_IME 技术债；明示"Platform 零依赖"说法不成立，实际为"零 Application 依赖 + Window.h 唯一残留"）② **WM_IME 定位明示**——不属于事件系统，属于输入法子系统（TSF/IMM/Candidate/System Caret 独立状态机），暂时借道 WindowMessageHandler ③ OnEvent 注释升级为 **Transitional adapter**（临时代码标记）。
