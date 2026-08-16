# Phase 7.1.5 Application 解耦 — 职责确认

> 状态：v1.0（2026-08-16）｜待用户确认后进初步设计
> 相关：phase7-platform-requirements.md（D5 Application 挂起——7.1.1-7.1.4 后回看）/ phase7-platform-detailed-design.md（WindowClass 标记 7.1.5）
> 目标：**7.1 平台抽象完全闭环**——Framework 层可脱离 Windows 独立存在（Window + Application 双零残留）
> 触发：用户 2026-08-16 决策——"我们既然决定平台解耦，那就不应该在框架里残留"（E2/E4 从记账改为实施）

## 0. 现状盘点（已核实）

| 位置 | Win32 残留 | 性质 |
|---|---|---|
| Application.h:3 | include WindowClass.h → **Windows.h 泄漏进公共头** | 实质 |
| Application.h:107 | `WindowClass m_windowClass` 成员 | 平台类型在框架类 |
| Application.cpp:20 | include `<Windows.h>`（消息泵 + PostQuitMessage） | 平台代码在框架 cpp |
| Application.cpp:26 | 构造直接引用 `Win32PlatformWindow::WindowProc` | 平台符号渗入 |
| Application.cpp:34-46 | 消息泵（GetMessageW/TranslateMessage/DispatchMessageW 循环） | 平台细节 |
| Application.cpp:67-72 | Exit → PostQuitMessage(0) | 平台细节 |
| Window 构造:42 | `const WindowClass&` 参数（传 Win32PlatformWindow） | 平台类型穿过框架接口 |
| Window.h:19 | `class WindowClass;` 前置声明 | 平台类型残留 |

## 1. Application 职责边界（先定义，防返工——GPT 要求）

### 框架编排层（Application 保留）
- 窗口生命周期管理（创建/持有/延迟销毁/空窗退出判定）
- 事件入口（EventRouter 基类：HitTest/Bubbling/焦点分发）
- 应用级语义虚方法（OnWindowCreated/OnWindowResized 等——业务扩展点）
- 消息循环编排（Run()——转调平台泵 + 每帧 ProcessDeferredDestroy 钩子）

### 平台契约层（下沉）
- **事件循环**（PlatformApplication）：消息泵（PumpMessages）+ 退出请求（RequestExit）
- **窗口系统**（Win32PlatformWindow 内部）：窗口类注册（RegisterClassW/WNDPROC——static 共享）+ 窗口过程（WindowProc——平台内闭环）

### 边界澄清（与 Phase 7.5 正交）
- 7.5 事件回调注册（Button::SetOnClick 等）是 **Widget 层业务便利**（virtual OnClick 转发 std::function），与 Application 的事件入口正交——**Application 仍是 EventRouter 入口，回调注册不改变其平台职责**
- 窗口系统（WindowClass）与事件循环（MessageLoop）是**两个独立概念**（GPT）——分别归位，不绑一起

## 2. 决策（拆两个子步骤，GPT 建议）

### 7.1.5.1 WindowClass 下沉（窗口系统归位）
- 文件移 `Platform/Win32/`（git rename，类名 WindowClass 不变——与 7.1.2 翻译器归位同例）
- **注册下沉**：Win32PlatformWindow 内部 `GetSharedWindowClass()` static 局部（注册一次跨窗口共用，进程退出反注册——RAII）；`kWindowClassName` 匿名 namespace 常量；WindowProc 符号平台内闭环
- **Window 构造删 `const WindowClass&` 参数**（平台类型不再穿过框架接口；main.cpp 经 application.Create 调用——零改动）
- **Win32PlatformWindow 构造删 windowClass 参数**（构造内 GetSharedWindowClass()）
- **Application 删 m_windowClass 成员 + GetWindowClass() 公共方法**（无调用者——YAGNI；外部需求时平台层另行暴露）

### 7.1.5.2 PlatformApplication 抽象（事件循环下沉）
```cpp
// Platform/PlatformApplication.h（契约，零 Win32）：
class PlatformApplication{
public:
    virtual ~PlatformApplication() = default;
    /// @param onFrame 每帧回调（框架层 ProcessDeferredDestroy 钩子——消息循环与框架状态同步）
    virtual int PumpMessages(const std::function<void()>& onFrame) = 0;
    virtual void RequestExit() = 0;
};

// Platform/Win32/Win32PlatformApplication.h/cpp（实现）：
//   PumpMessages：GetMessageW/TranslateMessage/DispatchMessageW 循环（含 onFrame）
//   RequestExit：PostQuitMessage(0)

// Application：组合 std::unique_ptr<PlatformApplication>（前置声明成员，cpp 创建 Win32 实现——协调者模式）
//   Run()  → m_platformApplication->PumpMessages([this]{ ProcessDeferredDestroy(); })
//   Exit() → m_platformApplication->RequestExit()
//   Application.cpp 删 <Windows.h> ✓
```
- **接口设计**：PumpMessages(onFrame) 带每帧钩子（比 GPT 的 Run()/Quit() 完整——ProcessDeferredDestroy 需要）；命名区分（不与 Application::Run/Exit 混淆）
- **创建方式**：cpp 层 make_unique<Win32PlatformApplication>（协调者模式——与 Window.cpp make_unique<Win32PlatformWindow> 同构）；**不设注入参数**（消息泵无"可替换"需求——YAGNI，与 RenderServices 注入不同）

### 7.1.5.3 注释转正（小项并入 7.1.5.2）
- Window::OnEvent adapter：Application 是事件最终入口（EventRouter 基类）——注释去"过渡/TODO"语义，标记"框架内转发"；结构零改动

## 3. 验证（V1-V3）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | 回归 | 窗口创建/消息循环/事件分发（鼠标/键盘/IME）/多窗口/退出全正常 |
| V3 | grep 实证 | **Application.h 零 Windows.h 零 Win32 类型；Application.cpp 零 Windows.h**；Window.h 零 WindowClass——Framework 层彻底无平台 |

## 4. 边界（7.1.5 不做）

- ❌ 不引入 PlatformApplication 注入参数（消息泵无可替换需求——YAGNI；与 RenderServices 注入区别对待）
- ❌ 不改事件分发逻辑（HitTest/Bubbling/焦点——框架职责，7.1.5 只动平台边界）
- ❌ 不做 7.2 测试体系（Phase 7.2 专项）
- ❌ 不动 WindowClass 内部实现（只移动 + 改调用方式）

## 5. 技术债记账（7.1.5 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| 窗口类名 "ECDI FrameWork" 硬编码平台层 | Win32PlatformWindow.cpp（匿名 namespace 常量） | 应用层可配置窗口类名需求出现时 |

## 6. 修订记录

- v1.0（2026-08-16）职责确认定稿：职责边界定义（框架编排/事件循环/窗口系统三分）+ 7.1.5.1 WindowClass 下沉 + 7.1.5.2 PlatformApplication 抽象。GPT 深化：E2 从"记账"改"实施"（闭环目标）+ 窗口系统/事件循环分离 + 职责边界先行（防 7.1.5 返工）。用户决策：框架零残留（E4 窗口类注册下沉）。
