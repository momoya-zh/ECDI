# Phase 7.1.2 翻译器契约改造 — 职责确认

> 状态：v1.0（2026-08-15）｜待用户确认后进初步设计
> 相关：phase7-platform-requirements.md（D1-D8）/ phase7-platform-detailed-design.md（v1.1，7.1.1 已实现）
> 定位：**7.1.1 = 物理迁移（代码搬家）；7.1.2 = 依赖关系重构（真正的平台解耦）**（GPT 概括，采纳）

## 0. 目标（GPT 验收标准）

> **Platform/ 目录不再依赖 Application**——`Application*` 不出现在平台层。

依赖链从：

```text
Win32 → Application（平台直连框架实现，依赖倒挂）
```

变为：

```text
Win32 → PlatformWindowHost（框架契约）→ Window（框架实现）→ Application
```

依赖方向正确：**Platform → Framework Contract → Framework Implementation**。

## 1. 决策点

### C1 派发方式 — a
**✅ 翻译器持 Host& 直派**：

```cpp
// WindowMessageHandler 构造：Application* → PlatformWindowHost&
WindowMessageHandler(PlatformWindowHost& host);

// 内部 10 处 m_application->OnEvent(event) → m_host.OnEvent(event)
```

- **同步事件模型**：Event 栈上构造、同步派发（`MouseEvent event(...); m_host.OnEvent(event);`）——`unique_ptr<Event>` 方案（堆分配/所有权管理）无必要，行为零变化（GPT 明确反对过度设计）
- 翻译器只认识 Host 契约，不认识 Application

### C2 翻译器物理位置 — b
**✅ 随迁 `Platform/Win32/`**：`WindowMessageHandler.h/cpp` → `include/ECDI/Platform/Win32/` + `src/Platform/Win32/`。原 Window/ 目录只剩框架类（Window.cpp/h），平台类归位彻底；未来 X11/Wayland 目录结构清晰。

### C3 Host::OnEvent 签名
**✅ 定稿：`virtual void OnEvent(const Event& event) = 0`**——与 `EventRouter::OnEvent(const Event&)` 对齐；事件只读传递，不可变数据模型（GPT 强调）。`PlatformWindowHost.h` 前置声明 `class Event;`。
（修正 7.1.1 详细设计草案里的 `Event&`——GPT 抓出，采纳）

### C4 Window::OnEvent 实现
**✅ 纯转发 + 过渡层注释**：

```cpp
/// @brief 事件转发节点（非最终派发者）
/// @details 过渡适配层：当前转发到 m_application->OnEvent；
/// 最终派发目标可能随 7.1.5 Application 解耦变化（未来可能直接 EventRouter）。
void Window::OnEvent(const Event& event){
    m_application->OnEvent(event);
}
```

**GPT 命名建议评估（ForwardEvent/HandleTranslatedEvent）**：**不采纳改名**——保留 `OnEvent` 理由：① 与 EventRouter::OnEvent 同名词法族一致（转发链各层同名，可读性好）② 改名成本 > 收益 ③ 语义歧义用注释消灭（"转发节点非拥有者"）。GPT 自认"保持 OnEvent 也没问题"。

### C5 Dispatch 表述修正（GPT）
**✅ 采纳**——职责确认文档措辞修正为**两级 Dispatch**：

```text
Translate（翻译器）→ Dispatch 一级（Host::OnEvent）→ Dispatch 二级（Application::OnEvent）
```

Window 是**事件转发节点**，不是最终派发者（最终派发者是 Application/EventRouter）。

### C6 验证
| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | **grep 实证（GPT 验收标准）** | `Platform/` 目录下 `Application*` 零出现（翻译器构造 + Win32PlatformWindow 构造都已消除） |
| V3 | 回归-事件 | 鼠标/键盘/字符/窗口事件经新链（翻译器 → Host::OnEvent → Window::OnEvent → Application）行为不变 |
| V4 | 回归-IME | 中文候选窗跟随光标 + 移动窗口归位（WM_IME 直调 window->NotifyIMEComposition 保持） |

## 2. 边界（7.1.2 不做）

- ❌ 不动翻译器翻译逻辑本体（TranslateKeyCode/ConsumeCodeUnit/TranslateModifier 原样）
- ❌ 不做 CaretGeometry（7.1.3）
- ❌ 不做 Backend 注入（7.1.4：unique_ptr<RenderingBackend> + PlatformRenderContext）
- ❌ 不改 Application 设计（D5 挂起——Window::OnEvent 仅转发到既有入口，标过渡层）
- ❌ 翻译器 WM_IME_START/COMPOSITION 直调 window->NotifyIMEComposition() 保持现状（F1 精神延续；组合串内嵌等 8.5 再动）

## 3. 改动范围预估（初步设计展开）

| 文件 | 动作 |
|---|---|
| `PlatformWindowHost.h` | + `OnEvent(const Event&)` 纯虚（前置声明 Event） |
| `WindowMessageHandler.h/cpp` | 物理迁移到 Platform/Win32/；构造 `Application*` → `PlatformWindowHost&`；10 处 OnEvent 目标替换 |
| `Win32PlatformWindow.h/cpp` | 构造去掉 `Application*` 参数；`m_messageHandler(m_host)` |
| `Window.h/cpp` | + `OnEvent(const Event&)` override（转发 + 过渡注释） |
| `Application.cpp` | 构造调用 `Win32PlatformWindow` 少传 app 参数（或签名不变——初步设计定） |
| vcxproj | 翻译器文件路径更新（Window/ → Platform/Win32/） |

## 4. 修订记录

- v1.0（2026-08-15）职责确认定稿：C1-C6。GPT 评审全采纳：C1 同步事件模型论证（反对 unique_ptr 过度设计）/ C2 目录归位 / C3 const Event& 修正（抓出 7.1.1 草案的 Event&）/ C4 过渡层注释 / C5 两级 Dispatch 表述 / V2 grep 验收 = Platform 零 Application 依赖。
