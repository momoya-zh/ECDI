# Phase 7.1 平台抽象 — 职责确认

> 状态：v1.0（2026-08-15）｜待用户确认后进初步设计
> 相关：MEMORY.md「Window 平台抽离（正式决策）」/ roadmap-deferred.md #1-8
> 目标（契约语言）：**让 Window 不再知道 Win32——而不是让 Win32 不再存在**（GPT 概括，已采纳）

## 1. 核心原则

> **平台负责"窗口存在"，框架负责"窗口里面发生什么"。**

| 下沉到 PlatformWindow（平台） | 留在 Window（框架） |
|---|---|
| HWND / CreateWindowExW / ShowWindow / DestroyWindow | WidgetTree（RootWidget 所有权） |
| WindowProc + GWLP_USERDATA 绑定 | Renderer / PaintFrame 编排 |
| HandleMessage 状态同步（WM_PAINT/SIZE/DESTROY/EXITSIZEMOVE） | Focus 状态 + FocusNext 导航 |
| WindowMessageHandler（消息 → Framework Event 翻译） | MouseCapture 状态 |
| IME（CreateCaret/SetCaretPos/Imm*） | EventDispatch（Application 侧） |
| InvalidateRect | Invalidate 语义（请求重绘，异步可合并——契约，非 Win32 细节） |

抽象后 `Window.h` **不再含 HWND / <Windows.h> / Win32 类型**，仅组合 `unique_ptr<PlatformWindow>`（前置声明即可）。

## 2. 决策点

### D1 平台抽象范围 — a
**✅ 全量下沉**（上表左侧全部）。PlatformWindow 是纯平台实现（Win32PlatformWindow 为唯一实现，X11/Wayland 只预留接口不实现——YAGNI）。

### D2 Framework↔Platform 通信 — b（整个 7.1 的核心）
**✅ Host 接口回调**：

```text
PlatformWindow → PlatformWindowHost（抽象接口）← Window（实现）
```

- `PlatformWindowHost`：`OnPaint()` / `OnResized(w,h)` / `OnEvent(Event&)` / IME 回调等——纯框架契约，零 Win32 类型
- `Window : PlatformWindowHost`（组合 + 接口）
- 论证（契约语言）：平台层"发生了窗口事件" → 通过 Host 通知框架层"响应"。**Platform 不认识框架具体类**，只认识契约——Win32/X11/Wayland 实现都不需要认识 Window
- 反例（不采纳）：PlatformWindow 持 `Window*`——平台依赖框架具体类，层次倒挂

### D3 Backend 注入（决策 35 代价解决）— c
**✅ c-1 后端可替换**：Window 持 `unique_ptr<RenderingBackend>`（默认 GDIBackend）+ TextMeasurer 抽象访问（`GetTextMeasurer()` 已返回 `TextMeasurer&`，TextBox 测量路径不受后端替换影响——分层已就位）。

**⚠️ c-2 平台句柄注入方式（GPT 修订，用户 2026-08-15 定稿）：**
- **✅ 方案 Y（定稿）：`PlatformRenderContext` 抽象基类** + `Win32RenderContext{HWND}` 子类——平台句柄经框架层类型传递，非类型擦除
- 方案 X：`Initialize(void*)`——类型擦除是"假抽象"（HWND/X11 Window/Wayland Surface 不是一回事），易成技术债（GPT 反对）——**已否决**
- 注：虽现在只有 GDI，但未来明确 X11/OpenGL，`PlatformRenderContext` 是低成本（空基类）防债

### D4 WindowMessageHandler 去向
**✅ 移入 PlatformWindow 内部**（组合成员）——翻译是平台职责（与 TranslateKeyCode 同层）；其 `Window*` 参数改 `PlatformWindowHost&`。

### D5 Application — 挂起（用户 2026-08-15 决策）
**⏸️ 不做设计、不排入 7.1**。理由（用户）：使用内容未定时设计 Application 易出错——7.1.1-7.1.4 实现完后回看 Application 实际暴露面（还剩多少 Win32：WindowClass 持有 / 消息泵 / GetMessageW-DispatchMessageW），再决定：保持现状 / 下沉 / 并入 7.2。
- 记账：`Application` 平台部分（WindowClass + 消息泵）为 7.1 挂起项，7.1.5 评估

### D6 输入层抽象（TextInputInterface，#2 债务）— e
**✅ e-1 随 7.1 一并做**：IME 平台代码随 PlatformWindow 下沉（UpdateTextInputCaret/DestroyTextInputCaret 变为 PlatformWindow 方法，客户区坐标语义封装在 Win32IME 内）。

**⚠️ e-2 契约结构命名（GPT 修订，用户 2026-08-15 定稿）：**
- **✅ 方案 Y（定稿）：`CaretGeometry{ Rect rect; }`**——语义放大：光标位置是通用能力（单行/多行/富文本/代码编辑器都需要），不只 IME
- 方案 X：`TextInputContext`——语义收窄为 IME——**已否决**

### D7 可测性（衔接 7.2）
**契约**：抽象后 `Window`/`Widget`/`TextBox` 不再含 Win32 类型——TextBox 编辑逻辑（InsertCodepoint/DeleteBackward/Selection/MoveCaret）是**纯逻辑，可脱离窗口单元测试**（编辑逻辑不依赖测量，只有坐标/绘制路径依赖）。7.1 不实现测试，但保证"纯逻辑可测"边界不被打破；7.2 覆盖（Selection 外部行为断言 + 布局契约 + 编辑逻辑）。

### D8 子步骤（GPT 重排，两条线分开改）
**✅ 采纳顺序**（窗口线 3 步 → 渲染线 1 步 → Application 回看，不同时改两条线，调试难度不叠加）：

| 子步骤 | 内容 | 线 |
|---|---|---|
| 7.1.1 | PlatformWindow 骨架：新类 + Host 接口，HWND/WindowProc/HandleMessage 状态同步下沉，Window 组合改造 | 窗口线 |
| 7.1.2 | WindowMessageHandler 下沉（移入 PlatformWindow，Window* → Host&） | 窗口线 |
| 7.1.3 | 输入层抽象：CaretGeometry 契约 + IME adapter 下沉 | 窗口线收尾 |
| 7.1.4 | Backend 注入：unique_ptr<RenderingBackend> + PlatformRenderContext 句柄注入路径 | 渲染线 |
| 7.1.5 | Application 回看评估（挂起项，实现完前四步后决定——不做设计） | 决策点 |

## 3. 边界（本阶段不做）

- ❌ 不做 X11/Wayland 实现（只抽象接口——YAGNI，无第二平台消费者）
- ❌ 不做 Application 改造（D5 挂起）
- ❌ 不做 7.2 测试体系实现（只保证可测边界）
- ❌ 不引入第三方依赖
- ❌ 不碰 GDIBackend 内部实现（4.7 稳定代码，skill 第 23 条）——只改它的接入方式

## 4. 修订记录

- v1.0（2026-08-15）职责确认定稿：D1-D8 + 核心原则。GPT 评审修订全采纳：D2 Host 接口核心化 / D3 PlatformRenderContext 替代 void*（c-2 决策点）/ D6 CaretGeometry 命名（e-2 决策点）/ D8 子步骤重排（窗口线 3 步 + 渲染线 1 步）。**用户决策：Application 挂起（D5，不设计不排期，前四步实现后回看）**。
- v1.0.1（2026-08-15）两个决策点定稿（用户确认）：**c-2 = PlatformRenderContext 抽象基类**（否决 void* 类型擦除）；**e-2 = CaretGeometry{ Rect }**（语义放大，否决 TextInputContext）。全部决策点关闭，进入初步设计。
