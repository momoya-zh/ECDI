# Phase 7.1.5 Application 解耦 — 详细设计

> 状态：v1.1（2026-08-16）｜✅ 已实现（用户验证：编译零警告 + 功能正常 + main.cpp 免宏防护）
> 相关：phase7-application-requirements.md（职责确认 v1.0）/ phase7-application-preliminary-design.md（初步设计 v1.2）
> 目标：**7.1 平台抽象完全闭环**——Framework 层可脱离 Windows 独立存在（Application.h + Application.cpp 零 Win32）
> 架构原则（GPT）：给每个 Win32 依赖找到唯一归属

## 0. 实现前置事实（已核实）

| # | 事实 | 影响 |
|---|---|---|
| F1 | WindowClass 引用点 6 文件全列出（Application.h:3/48/107、Application.cpp:26/30-31/51、Window.h:19/42、Window.cpp:5/42、Win32PlatformWindow.h:17/33、Win32PlatformWindow.cpp:3/15） | 逐点改造 |
| F2 | **main.cpp 零 WindowClass 引用**（经 application.Create(title,w,h)） | 构造签名变化零改动 |
| F3 | Win32PlatformWindow.cpp 构造用 windowClass.GetClassName()（26 行）+ GetInstance()（34 行） | 改 WindowClass::Instance() |
| F4 | Application.cpp:26 构造创建 WindowClass + 51 行 Create 传参 + 30-31 GetWindowClass（无调用者） | 删 m_windowClass + GetWindowClass + Create 去参 |
| F5 | WindowClass.cpp include Window.h（第 3 行——**历史遗留，构造/方法未用 Window 类**） | 移动时清理 |
| F6 | WindowClass 移动语义（move ctor/assign）完整实现 | 保留（类能力不动，边界）；Instance 用不到但无害 |
| F7 | Application.h `friend class Window;`（Window.cpp 经此访问？） | 保留（框架内部关系） |
| F8 | vcxproj：WindowClass.cpp 178 / WindowClass.h 241 | 路径改 Platform/Win32/ |
| F9 | Win32PlatformApplication 循环内 ProcessDeferredDestroy 语义 = 每条消息后（既有行为） | SetDeferredCleanup 注册 |
| F10 | Window 构造唯一调用点 = Application.cpp:49（make_unique） | 签名变化只波及 Application.cpp |

## 1. 文件清单

**新 2**：`Platform/PlatformApplication.h` / `Platform/Win32/Win32PlatformApplication.h+cpp`
**移动 2**（git rename）：`Window/WindowClass.h` → `Platform/Win32/Win32WindowClass.h`；`Window/WindowClass.cpp` → `Platform/Win32/Win32WindowClass.cpp`
**改 5**：`Win32PlatformWindow.h/cpp`、`Window.h/cpp`、`Application.h/cpp`、`ECDI.vcxproj`
**零改动**：`main.cpp`（Create 签名不变）、WindowClass 内部实现（仅 +Instance + include 调整）

## 2. 逐文件实现蓝图

### Step 1（7.1.5.1）：窗口系统归位——Win32WindowClass + 注册下沉

```cpp
// ① 文件移动（git mv，类名不变）：
//    include/ECDI/Window/WindowClass.h → include/ECDI/Platform/Win32/Win32WindowClass.h
//    src/Window/WindowClass.cpp         → src/Platform/Win32/Win32WindowClass.cpp

// ② Win32WindowClass.h：
//    - include 链不变（Windows.h + DrawText undef + <string>——skill 9 宏防护）
//    - + 静态单例（窗口系统资源归窗口类自身——GPT 三轮）：
//      /// @brief 共享窗口类实例（7.1.5：注册一次跨窗口共用——窗口系统资源）
//      /// @details static 局部 RAII（进程退出反注册）；WindowProc 平台内闭环
//      static WindowClass& Instance();

// ③ Win32WindowClass.cpp：
//    - include："ECDI/Platform/Win32/Win32WindowClass.h"（自身，第一行）
//    - 删："ECDI/Window/Window.h"（F5：历史遗留，未使用）
//    - 保留："ECDI/Core/String.h"（UTF8ToWide）+ <system_error> + 全部内部实现
//    - + 匿名 namespace 常量 + Instance 实现：
//      namespace{ const char* kWindowClassName = "ECDI FrameWork"; }   // 原 Application 构造硬编码（UTF-8）
//      WindowClass& WindowClass::Instance(){
//          // cpp 层同族引用 Win32PlatformWindow::WindowProc——平台内闭环
//          static WindowClass windowClass(kWindowClassName, &Win32PlatformWindow::WindowProc);
//          return windowClass;
//      }
//      （cpp include "ECDI/Platform/Win32/Win32PlatformWindow.h"——仅 Instance 实现需要）

// ④ Win32PlatformWindow.h：
//    - 删：class WindowClass; 前置声明（17 行）
//    - 构造签名：Win32PlatformWindow(PlatformWindowHost& host, const std::string& title,
//                                     int width, int height);
//    - include "ECDI/Platform/Win32/Win32WindowClass.h"（Instance 返回引用——前置声明即可？
//      构造体内 WindowClass::Instance().GetClassName() 需要完整类型 → cpp include 即可，h 只需
//      返回值引用 → 前置声明。但 h 里构造签名无 WindowClass → h 不需 include；cpp include）
// ⑤ Win32PlatformWindow.cpp：
//    - include 改："ECDI/Window/WindowClass.h" → "ECDI/Platform/Win32/Win32WindowClass.h"
//    - 构造：去 windowClass 参数；CreateWindowExW 改：
//        WindowClass::Instance().GetClassName()   // 原 windowClass.GetClassName()
//        WindowClass::Instance().GetInstance()    // 原 windowClass.GetInstance()
//      （或先取引用 const WindowClass& wc = WindowClass::Instance(); 再复用——更清晰）

// ⑥ Window.h：删 class WindowClass; 前置声明（19 行）+ 构造 windowClass 参数（42 行）
//    Window.cpp：删 "ECDI/Window/WindowClass.h" include（5 行）+ 构造签名
// ⑦ Application.h：删 "ECDI/Window/WindowClass.h" include（3 行）+ GetWindowClass 声明（48 行）+ m_windowClass 成员（107 行）
//    Application.cpp：删构造创建（26 行）+ GetWindowClass 实现（30-32 行）+ Create 传参（51 行）
```

### Step 2（7.1.5.2）：事件循环下沉——PlatformApplication + Win32PlatformApplication

```cpp
// ① Platform/PlatformApplication.h（新，契约零 Win32）：
#pragma once
#include <functional>
namespace ECDI{
/// @brief 平台应用抽象（7.1.5：事件循环下沉——消息泵 + 退出请求 + 延迟清理时机）
/// @details 消息驱动模型（GUI 框架定位，非帧循环）：平台控制消息循环，
/// 框架注册延迟清理逻辑（资源生命周期管理——延迟销毁安全窗口）。
/// 唯一实现：Win32PlatformApplication（X11/Wayland 只留接口，YAGNI）。
class PlatformApplication{
public:
    virtual ~PlatformApplication() = default;
    /// @brief 注册延迟清理回调（框架层 ProcessDeferredDestroy——时机平台控制，逻辑框架提供）
    void SetDeferredCleanup(const std::function<void()>& cleanup);
    /// @brief 泵消息循环（阻塞直到退出；循环内每条消息处理后调用 PerformDeferredCleanup）
    virtual int Run() = 0;
    /// @brief 请求退出消息循环（异步）
    virtual void RequestExit() = 0;
protected:
    /// @brief 执行延迟清理（Run 循环内调用点——实现者职责；未注册则空操作）
    void PerformDeferredCleanup() const;
private:
    std::function<void()> m_deferredCleanup;
};
}

// ② Platform/Win32/Win32PlatformApplication.h（新）：
#pragma once
#include "ECDI/Platform/PlatformApplication.h"
#include <Windows.h>
namespace ECDI{
/// @brief Win32 平台应用（7.1.5：GetMessageW 消息泵 + PostQuitMessage——消息循环唯一归属）
class Win32PlatformApplication final : public PlatformApplication{
public:
    int Run() override;
    void RequestExit() override;
};
}
// ③ Platform/Win32/Win32PlatformApplication.cpp（新）：
#include "ECDI/Platform/Win32/Win32PlatformApplication.h"
namespace ECDI{
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
}

// ④ Application.h：+ 前置声明 class PlatformApplication; + 成员
//    std::unique_ptr<PlatformApplication> m_platformApplication;
//    （include <memory> 已有）
// ⑤ Application.cpp：
//    - include："ECDI/Platform/Win32/Win32PlatformApplication.h"（协调者模式——cpp 创建平台实现）
//    - 构造：: m_platformApplication(std::make_unique<Win32PlatformApplication>()){
//        m_platformApplication->SetDeferredCleanup([this]{ ProcessDeferredDestroy(); });
//      }
//    - Run()：return m_platformApplication->Run();
//    - Exit()：m_platformApplication->RequestExit();
//    - 删 <Windows.h> ✓（消息泵/PostQuitMessage 全平台化）
//    - 删 "ECDI/Platform/Win32/Win32PlatformWindow.h" include？（F4 后不再引用 WindowProc——删）
```

### Step 3（7.1.5.3）：OnEvent 注释转正

```cpp
// Window.cpp OnEvent 注释改（去"过渡/TODO"语义）：
//   "事件转发（框架内）：Application 是事件最终入口（EventRouter 基类）——
//    平台事件 → Host::OnEvent → Window → Application 分发链"
//   结构零改动
```

### Step 4：vcxproj + 验证

```xml
<!-- ClCompile：src\Window\WindowClass.cpp（178）→ src\Platform\Win32\Win32WindowClass.cpp
     + src\Platform\Win32\Win32PlatformApplication.cpp
     ClInclude：Include\ECDI\Window\WindowClass.h（241）→ include\ECDI\Platform\Win32\Win32WindowClass.h
     + include\ECDI\Platform\PlatformApplication.h + include\ECDI\Platform\Win32\Win32PlatformApplication.h -->
```

## 3. 验证（V1-V4）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | 回归 | 窗口创建/消息循环/事件分发（鼠标/键盘/IME）/多窗口/退出全正常（**延迟销毁：关闭窗口后立即释放验证**） |
| V3 | grep 实证 | Application.h 零 Windows.h/零 Win32；Application.cpp 零 Windows.h；Window.h 零 WindowClass |
| V4 | grep 实证（GPT） | 框架抽象头零 Windows.h——`grep -r "Windows.h" include/ECDI` 仅命中 Platform/Win32/ + 后端实现头（GDIBackend/GDITextMeasurer——豁免） |

## 4. 技术债记账（7.1.5 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| 窗口类名 "ECDI FrameWork" 硬编码平台层 | Win32WindowClass.cpp（kWindowClassName） | 应用层可配置窗口类名需求出现时 |
| 后端实现头（GDIBackend/GDITextMeasurer）物理在 Render/ 含 Windows.h | include/ECDI/Render/ | 第二后端出现时评估归位（Render/Backend/Win32/ 或 Platform/Win32/） |

## 5. 回退基线

v1.1 方案确认时的状态（7.1.4 已 commit `24b6bef`，工作区干净）。实现失败恢复：`git checkout -- .` + 已移动文件 `git mv` 反向恢复。

## 6. 修订记录

- v1.0（2026-08-16）详细设计定稿：Step 1-4。F1-F10 前置事实；7.1.5.1 窗口系统归位（Win32WindowClass::Instance + Window 构造去参）+ 7.1.5.2 事件循环下沉（SetDeferredCleanup/Run/PerformDeferredCleanup）+ 7.1.5.3 注释转正 + V4 验收。
- v1.1（2026-08-16）验证通过 + 编译期三连修复：
  - **① main.cpp 缺 Windows.h**（wWinMain 的 WINAPI/HINSTANCE 未定义）——7.1.5 删传递 include 暴露隐式依赖，main.cpp 显式 include（解耦正向副作用）
  - **② unique_ptr 不完整类型析构**——Application 隐式析构在 main.cpp 实例化需 PlatformApplication 完整；显式 `~Application();` + cpp `= default`（pimpl 惯用法）
  - **③ DrawTextW 宏污染**——main.cpp 的 Windows.h 无 undef → 污染 RecordingBackend::DrawText 声明 → LNK2001；main.cpp 加 undef
  - **用户零负担兜底**：RenderingBackend.h/RecordingBackend.h 内置防御性 undef（`#ifdef DrawText #undef DrawText`——#ifdef 包裹，用户 include ECDI 头自动免疫，无需自己加防护）；DrawText 命名冲突记技术债（Phase 10 API 审查改名）
  - V3/V4 实证：Application.h/cpp 代码层零 Win32；框架抽象头零 Windows.h（仅 Platform/Win32/ + 后端实现头豁免）
