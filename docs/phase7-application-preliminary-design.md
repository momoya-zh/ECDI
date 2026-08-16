# Phase 7.1.5 Application 解耦 — 初步设计

> 状态：v1.0（2026-08-16）｜待用户确认后进详细设计
> 相关：phase7-application-requirements.md（职责确认，v1.0）
> 目标：**7.1 平台抽象完全闭环**——Framework 层可脱离 Windows 独立存在（Application.h + Application.cpp 零 Win32）

## 0. 实现前置事实（已核实）

| # | 事实 | 影响 |
|---|---|---|
| F1 | WindowClass 引用点：Application.h:3/48/107、Application.cpp:26/30-31/51、Window.h:19/42、Window.cpp:5/42、Win32PlatformWindow.h:17/33、Win32PlatformWindow.cpp:3/15 | 6 文件改造点全列出 |
| F2 | **main.cpp 零 WindowClass 引用**（经 application.Create(title,w,h)） | Window 构造去参零改动 |
| F3 | Win32PlatformWindow.cpp 构造用 windowClass.GetClassName()（26 行）+ GetInstance()（34 行） | 改 GetSharedWindowClass() |
| F4 | Application.cpp:26 构造创建 WindowClass + 51 行 Create 传参 | 删 m_windowClass + Create 去参 |
| F5 | GetWindowClass() 公共方法无调用者（main.cpp 无引用） | 删除（YAGNI） |
| F6 | WindowClass 内部实现（RegisterClassW/WNDPROC/移动语义）稳定 | 只移动不改（边界） |
| F7 | vcxproj：WindowClass.cpp 178 行 / WindowClass.h 241 行 | 路径更新 |
| F8 | Application::Run 循环 + ProcessDeferredDestroy 每帧 + Exit→PostQuitMessage | 转调 PlatformApplication（onFrame 钩子） |

## P1 7.1.5.1 WindowClass 下沉（窗口系统归位）

```cpp
// ① 文件移动（git rename，类名不变）：
//    include/ECDI/Window/WindowClass.h → include/ECDI/Platform/Win32/Win32WindowClass.h
//    src/Window/WindowClass.cpp         → src/Platform/Win32/Win32WindowClass.cpp
//    （include 自身头路径同步：Win32WindowClass.cpp 第一行）

// ② Win32PlatformWindow.h：
//    - 删：class WindowClass; 前置声明 + 构造 windowClass 参数
//    - + include "ECDI/Platform/Win32/Win32WindowClass.h"（GetSharedWindowClass 返回引用——前置声明即可；
//      但 static 局部构造在 cpp——cpp include）
//    - + private static：const WindowClass& GetSharedWindowClass();
//    - 构造签名：Win32PlatformWindow(PlatformWindowHost& host, const std::string& title, int width, int height);

// ③ Win32WindowClass.cpp（窗口类注册归窗口类自身——GPT 修订：Instance() 单例替代挂在 Win32PlatformWindow）：
namespace{   // 匿名 namespace：窗口类内部常量
const char* kWindowClassName = "ECDI FrameWork";   // 窗口类名（原 Application 构造硬编码；UTF-8——WindowClass 构造收 std::string）
}
WindowClass& WindowClass::Instance(){
    // 7.1.5：窗口类注册归"窗口系统"自身——static 局部 RAII（注册一次跨窗口共用，进程退出反注册）
    // WindowProc 符号平台内闭环（cpp include Win32PlatformWindow.h——平台层同族引用）
    static WindowClass windowClass(kWindowClassName, &Win32PlatformWindow::WindowProc);
    return windowClass;
}
// Win32PlatformWindow 构造：CreateWindowExW(0, WindowClass::Instance().GetClassName(), ... WindowClass::Instance().GetInstance() ...)

// ④ Window.h：删 class WindowClass; 前置声明 + 构造 windowClass 参数
//    Window.cpp：删 WindowClass.h include + 构造签名
// ⑤ Application.h：删 WindowClass.h include + GetWindowClass() 声明 + m_windowClass 成员
//    Application.cpp：删构造创建 + GetWindowClass 实现 + Create 传参
```

## P2 7.1.5.2 PlatformApplication 抽象（事件循环下沉）

```cpp
// ① Platform/PlatformApplication.h（新，契约零 Win32）：
#pragma once
#include <functional>
namespace ECDI{
/// @brief 平台应用抽象（7.1.5：事件循环下沉——消息泵 + 退出请求 + 延迟清理时机）
/// @details 平台负责"消息循环"（Event Loop），框架负责"延迟清理逻辑"（资源生命周期管理）。
/// **消息驱动模型（GUI 框架定位），非帧循环（游戏引擎）**——清理调用点语义 =
/// 每条消息处理后的延迟销毁安全窗口（避免 DispatchMessage 栈上销毁窗口悬空指针），
/// 不是 Update/Render。
/// 唯一实现：Win32PlatformApplication（X11/Wayland 只留接口，YAGNI）。
class PlatformApplication{
public:
    virtual ~PlatformApplication() = default;

    /// @brief 注册延迟清理回调（框架层 ProcessDeferredDestroy——资源生命周期管理的清理逻辑）
    /// @details 时机由平台循环控制（每条消息后），逻辑由框架提供——职责分离
    void SetDeferredCleanup(const std::function<void()>& cleanup);

    /// @brief 泵消息循环（阻塞直到退出；循环内每条消息处理后调用 PerformDeferredCleanup）
    /// @return 退出码（框架 Run 的返回值）
    virtual int Run() = 0;

    /// @brief 请求退出消息循环（异步——循环在下一轮判断退出）
    virtual void RequestExit() = 0;

protected:
    /// @brief 执行延迟清理（Run 循环内调用点——实现者职责；未注册则空操作）
    void PerformDeferredCleanup() const;

private:
    std::function<void()> m_deferredCleanup;   ///< 框架清理逻辑（Application::ProcessDeferredDestroy）
};
}

// ② Platform/Win32/Win32PlatformApplication.h/cpp（新）：
class Win32PlatformApplication : public PlatformApplication{
public:
    int Run() override;
    void RequestExit() override;   // PostQuitMessage(0)
};
// cpp：
int Win32PlatformApplication::Run(){
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)){
        TranslateMessage(&message);
        DispatchMessageW(&message);
        PerformDeferredCleanup();   // 每条消息后的延迟清理时机（资源生命周期管理）
    }
    return static_cast<int>(message.wParam);
}
void Win32PlatformApplication::RequestExit(){
    PostQuitMessage(0);
}

// ③ Application.h：+ 前置声明 class PlatformApplication; + 成员
//    std::unique_ptr<PlatformApplication> m_platformApplication;
// ④ Application.cpp：
//    - include Win32PlatformApplication.h（协调者模式——cpp 创建平台实现）
//    - 构造：m_platformApplication(std::make_unique<Win32PlatformApplication>())
//             + m_platformApplication->SetDeferredCleanup([this]{ ProcessDeferredDestroy(); });
//    - Run()：return m_platformApplication->Run();
//    - Exit()：m_platformApplication->RequestExit();
//    - 删 <Windows.h> ✓（消息泵/PostQuitMessage 全平台化）
```

## P3 7.1.5.3 注释转正（Window::OnEvent）

```cpp
// Window.cpp OnEvent 注释：去"过渡/TODO"语义 →
//   "事件转发（框架内）：Application 是事件最终入口（EventRouter 基类）——
//    平台事件 → Host::OnEvent → Window → Application 分发链"
//   结构零改动
```

## P4 vcxproj + 验证

- vcxproj：WindowClass 路径改 Platform/Win32/（178/241 行）+ 注册 Win32PlatformApplication.cpp/h
- 验证：
  | # | 验收项 | 判据 |
  |---|---|---|
  | V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
  | V2 | 回归 | 窗口创建/消息循环/事件分发（鼠标/键盘/IME）/多窗口/退出全正常 |
  | V3 | grep 实证 | Application.h 零 Windows.h/零 Win32；Application.cpp 零 Windows.h；Window.h 零 WindowClass |
  | V4 | grep 实证（GPT 新增） | **框架抽象头零 Windows.h**——`grep -r "Windows.h" include/ECDI` 仅命中 `Platform/Win32/` + 后端实现头（GDIBackend.h/GDITextMeasurer.h——**本质 Win32 GDI 平台实现，物理暂居 Render/，Windows.h 是其本分**；7.1.4 已确认"抽象目标不是后端零 Win32"） |

## 边界（7.1.5 不做）

- ❌ 不引入 PlatformApplication 注入参数（消息泵无可替换需求——YAGNI）
- ❌ 不改事件分发逻辑（框架职责）
- ❌ 不做 7.2 测试体系
- ❌ 不改 WindowClass 内部实现（只移动 + 改调用方式）

## 技术债记账（7.1.5 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| 窗口类名 "ECDI FrameWork" 硬编码平台层 | Win32WindowClass.cpp（匿名 namespace 常量） | 应用层可配置窗口类名需求出现时 |
| 后端实现头（GDIBackend.h/GDITextMeasurer.h）物理在 Render/ 却含 Windows.h | include/ECDI/Render/ | 第二后端出现时评估归位（Render/Backend/Win32/ 或 Platform/Win32/——GPT 目录组织建议；V4 当前豁免） |

## 修订记录

- v1.0（2026-08-16）初步设计定稿：P1-P4。7.1.5.1 WindowClass 下沉（static 共享注册 + Window 构造去参）+ 7.1.5.2 PlatformApplication 抽象（PumpMessages onFrame 钩子）+ 7.1.5.3 注释转正。F1-F8 前置事实。
- v1.1（2026-08-16，GPT 评审）三处修订：
  - **P1（采纳）**：窗口类注册从 `Win32PlatformWindow::GetSharedWindowClass()` 改为 **`WindowClass::Instance()` 单例**——窗口系统资源归窗口类自身（GPT：GetSharedWindowClass 挂平台窗口上，未来多 Application/多窗口类别受限）；kWindowClassName 常量归 Win32WindowClass.cpp；WindowProc 引用平台内闭环（cpp 同族引用）
  - **P2（采纳 + 语义精确化）**：`onFrame` → **`onMessageProcessed`**——GPT 反对"消息循环混帧循环"（GUI 框架 vs 游戏引擎）。论证：ProcessDeferredDestroy 是**既有消息驱动模型内的清理时机**（每条消息后延迟销毁安全窗口，避免 DispatchMessage 栈上悬空指针），非 Update/Render——钩子保留但改名/注释精确化（否则 ProcessDeferredDestroy 无处执行，窗口资源无法及时释放）
  - **V4（采纳 + 修正）**：新增框架抽象头零 Windows.h 验收——**豁免后端实现头**（GDIBackend.h/GDITextMeasurer.h 本质 Win32 GDI 实现，Windows.h 是其本分；抽象目标 = 框架抽象头零平台，非后端零平台——7.1.4 已确认）
- v1.2（2026-08-16，GPT 三轮）两处修订：
  - **P2 接口形态（采纳 GPT"清理语义"）**：`PumpMessages(onMessageProcessed)` → **`SetDeferredCleanup` + `Run()` + `PerformDeferredCleanup`**——清理时机由平台循环控制（Run 内每条消息后调用），清理逻辑由框架注册（SetDeferredCleanup）；命名表达"Deferred Cleanup（资源生命周期管理）"语义，彻底消除事件/帧歧义；Run() 无参（GPT 原提案形态）
  - **技术债新增（GPT 目录组织建议——声明不属 7.1.5 职责）**：后端实现头（GDIBackend/GDITextMeasurer）归位候选（Render/Backend/Win32/ 或 Platform/Win32/）——第二后端出现时评估，V4 当前豁免
  - **7.1 核心洞察记录（GPT）**：整个 7.1 不是"增加抽象"而是"**给每一个 Win32 依赖找到唯一归属**"（HWND→PlatformWindow、WindowProc→PlatformWindow、IME→PlatformWindow、GDI→RenderingBackend、RegisterClassW→Win32WindowClass、GetMessageW→PlatformApplication）——架构原则写入 MEMORY.md
