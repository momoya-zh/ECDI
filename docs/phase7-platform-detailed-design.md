# Phase 7.1.1 PlatformWindow 骨架 — 详细设计

> 状态：v1.1（2026-08-15）｜**已实现并验证通过**（V1-V5：编译零警告 + 事件/渲染/焦点/IME 回归正常）
> 相关：phase7-platform-requirements.md（D1-D8 v1.0.1）/ phase7-platform-preliminary-design.md（v1.1 边界守则）
> 目标（GPT 验收）：**Window.h 不出现 Win32 类型**——HWND/HDC/UINT/WPARAM/LPARAM/LRESULT/RECT/IME API（V2 grep 实证通过）
> 边界（v1.1）：翻译器结构不动 / dynamic_cast\<TextBox\> 债务不处理 / WindowClass+MessageLoop 标记 7.1.5 / Application 仅一行适配

## 1. 文件清单（3 新 + 2 改 + 构建）

| 文件 | 动作 | 内容 |
|---|---|---|
| `include/ECDI/Platform/PlatformWindow.h` | 新增 | 平台窗口抽象（7 纯虚，零 Win32） |
| `include/ECDI/Platform/PlatformWindowHost.h` | 新增 | Host 契约（4 虚方法 + GetWindow，零 Win32） |
| `include/ECDI/Platform/Win32/Win32PlatformWindow.h` + `src/Platform/Win32/Win32PlatformWindow.cpp` | 新增 | Win32 实现（唯一实现） |
| `include/ECDI/Window/Window.h` + `src/Window/Window.cpp` | 改造 | 组合 PlatformWindow + 实现 Host + 移除 Win32 |
| `src/Application/Application.cpp` | 一行 | `Window::WindowProc` → `Win32PlatformWindow::WindowProc` |
| `ECDI.vcxproj` | 注册 | 4 新文件（头×3 + cpp×1）；CMake GLOB 零改动 |
| `src/Window/WindowMessageHandler.cpp` | **不动** | 7.1.1 保持现状（7.1.2 拆 Translate/Dispatch） |

## 2. 新文件详细

### 2.1 `Platform/PlatformWindow.h`（抽象基类）

```cpp
#pragma once
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"

namespace ECDI{

class PlatformWindowHost;   // 前置声明（构造注入 Host&）

/// @brief 平台窗口抽象（7.1）：平台负责"窗口存在"，框架负责"窗口里面发生什么"
/// @details Window 组合此接口——Window 不接触 HWND/创建细节；
/// 生命周期 + 平台能力（重绘请求/客户区查询/文本输入插入点）下沉。
/// 唯一实现：Win32PlatformWindow（X11/Wayland 只留接口，YAGNI）。
class PlatformWindow{
public:
    virtual ~PlatformWindow() = default;

    virtual void Show() = 0;                            ///< 显示窗口
    virtual bool Release() noexcept = 0;                ///< 销毁底层窗口句柄（幂等）
    virtual void Invalidate() = 0;                      ///< 请求重绘整个客户区（异步可合并——契约语义）
    virtual Size GetClientSize() const = 0;             ///< 客户区尺寸（框架层 Size，非 Win32 RECT）

    /// @brief 更新文本输入插入点（5.6 双通道平台调用；客户区坐标——坐标系语义封装在实现内）
    /// @param clientPos 光标顶部客户区坐标（框架层 Point，非 Win32 类型）
    virtual void UpdateTextInputCaret(const Point& clientPos) = 0;
    virtual void DestroyTextInputCaret() = 0;           ///< 销毁文本输入插入点（幂等）
};

}
```

### 2.2 `Platform/PlatformWindowHost.h`（Host 契约）

```cpp
#pragma once

namespace ECDI{

class Window;   // 前置声明——GetWindow 返回框架窗口（平台层不 include Window.h）

/// @brief 平台窗口宿主（7.1 D2 核心）：Platform 不认识框架具体类，只认识此契约
/// @details Window 实现此接口；Win32PlatformWindow 持 Host& 回调。
/// 平台事件 → Host 回调 → 框架响应（契约语言：平台层"发生了窗口事件"，框架层"响应"）。
class PlatformWindowHost{
public:
    virtual ~PlatformWindowHost() = default;

    virtual void OnPaint() = 0;                         ///< WM_PAINT → 帧编排（PaintFrame）
    virtual void OnResized(int width, int height) = 0;  ///< WM_SIZE → RootWidget 尺寸同步
    virtual void OnExitSizeMove() = 0;                  ///< WM_EXITSIZEMOVE → 刷新文本插入点（销毁+重建）
    virtual Window* GetWindow() const noexcept = 0;     ///< 翻译器构造 Event 需来源窗口（Event 绑 Window*——Event.h 50 行）
};

}
```

**设计说明**：
- **无 OnEvent**（YAGNI）——7.1.1 翻译器保持现状直派 Application（F1），7.1.2 拆派发时再加 `OnEvent(Event&)`
- **无 OnIMEComposition**——翻译器 WM_IME_* case 保持现状直调 `window->NotifyIMEComposition()`（翻译器经 Handle 参数持有 Window*）
- **无 OnDestroyed**（YAGNI）——WM_DESTROY 后 Win32PlatformWindow 内部置空句柄，Window 端无需动作
- **GetWindow 必要性**：翻译器构造 Event 需要 `Window*`（Event.h:50 `explicit Event(Window*)`）——平台层经 Host 拿指针，不 include Window.h

### 2.3 `Platform/Win32/Win32PlatformWindow.h/cpp`（Win32 实现）

```cpp
#pragma once

#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Window/WindowMessageHandler.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护（与 GDIBackend.h 同款）
#endif

#include <string>

namespace ECDI{

class Application;   // 前置声明（7.1.1 过渡：翻译器构造需 Application*——7.1.2 拆派发后消除）
class WindowClass;

/// @brief Win32 平台窗口实现（7.1.1 唯一实现）
/// @details 承载全部 Win32：窗口生命周期 / WindowProc / 消息处理 / 翻译器 / IME 平台调用。
/// 平台代码不再出现在框架类（Window）中。
class Win32PlatformWindow final : public PlatformWindow{
public:
    /// @param host     Host 回调（框架契约，平台不认识 Window 具体类）
    /// @param app      Application（过渡：转给翻译器构造；7.1.2 拆派发后消除）
    /// @param windowClass 窗口类（Application 持有，此处仅用类名/实例）
    Win32PlatformWindow(PlatformWindowHost& host, Application* app,
                        const WindowClass& windowClass,
                        const std::string& title, int width, int height);

    ~Win32PlatformWindow() override;    ///< Release（幂等）

    void Show() override;
    bool Release() noexcept override;
    void Invalidate() override;
    Size GetClientSize() const override;
    void UpdateTextInputCaret(const Point& clientPos) override;
    void DestroyTextInputCaret() override;

    /// @brief 静态窗口过程（Application 注册 WindowClass 用；GWLP_USERDATA 绑定本实例）
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    /// @brief 原生句柄（过渡：Window 构造给 GDIBackend::SetHwnd；7.1.4 PlatformRenderContext 替换）
    HWND GetHandle() const noexcept;

private:
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);   ///< 实例级消息处理

    PlatformWindowHost& m_host;       ///< Host 回调（非拥有）
    Application* m_application;       ///< 过渡：转给翻译器（7.1.2 消除）
    WindowMessageHandler m_messageHandler;   ///< 翻译器（随消息处理整体搬入，结构保持现状）
    HWND m_hwnd = nullptr;            ///< 窗口句柄（原 Window::m_handle）
    bool m_caretCreated = false;      ///< 系统 caret 懒创建标记（原 Window::m_caretCreated）
};

}
```

**Win32PlatformWindow.cpp 实现要点**：

1. **构造**（原 Window 构造平台部分整体搬入）：
   - `UTF8ToWide(title)` → `CreateWindowExW`（lpCreateParams = this——WM_NCCREATE 时 GWLP_USERDATA 绑定本实例）
   - 失败抛 `std::system_error`（保持现状行为）
   - 成功后 `GetClientRect` 存初始客户区（GetClientSize 返回）
   - **注意**：不再调 `host.OnResized` 做初始尺寸（构造时 Window::m_rootWidget 尚未创建，顺序问题）——改由 Window 构造 m_rootWidget 后主动 `GetClientSize()`

2. **WindowProc**（原 Window::WindowProc 搬入，绑定目标改 Win32PlatformWindow*）：
   ```cpp
   LRESULT CALLBACK Win32PlatformWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
       Win32PlatformWindow* self = nullptr;
       if (msg == WM_NCCREATE){
           self = static_cast<Win32PlatformWindow*>(
               reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
           self->m_hwnd = hwnd;
           SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
       }
       else{
           self = reinterpret_cast<Win32PlatformWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
       }
       if (self){
           return self->HandleMessage(hwnd, msg, wParam, lParam);
       }
       return DefWindowProcW(hwnd, msg, wParam, lParam);
   }
   ```

3. **HandleMessage**（原 Window::HandleMessage 整体搬入 + 状态同步改 Host 回调）：
   ```cpp
   LRESULT Win32PlatformWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam){
       // ── 状态同步（在翻译前，Host 回调保证 Handler 看到最新状态）──
       switch (msg){
       case WM_PAINT:
           m_host.OnPaint();          // → Window::PaintFrame
           return 0;
       case WM_DESTROY:
           m_hwnd = nullptr;          // 句柄失效（Window 端无需动作）
           break;
       case WM_SIZE:
           m_host.OnResized(LOWORD(lParam), HIWORD(lParam));   // → RootWidget SetSize
           break;
       case WM_EXITSIZEMOVE:
           m_host.OnExitSizeMove();   // → Window 销毁+重建 caret
           return 0;
       }
       // ── 翻译（保持现状：翻译器直派 Application；结构 7.1.2 再拆）──
       auto result = m_messageHandler.Handle(m_host.GetWindow(), hwnd, msg, wParam, lParam);
       if (result){
           return *result;
       }
       return DefWindowProcW(hwnd, msg, wParam, lParam);
   }
   ```
   **注意 WM_SIZE fall-through**：状态同步后 `break` 继续走翻译器（WM_SIZE → WindowResizedEvent，现状行为一致——原代码同步后调翻译器）。

4. **IME 平台调用**（原 Window.cpp 342-386 行整体搬入，m_handle → m_hwnd）：
   - `UpdateTextInputCaret(clientPos)`：懒创建 `CreateCaret(m_hwnd,...)` → `SetCaretPos` → `HideCaret` → `ImmGetContext/ImmSetCompositionWindow/ImmReleaseContext`（**客户区坐标直传，不 ClientToScreen**——5.6 实测语义，注释保留）
   - `DestroyTextInputCaret()`：`if (m_caretCreated) DestroyCaret(); m_caretCreated = false;`

5. **GetHandle()**：返回 `m_hwnd`（过渡用，7.1.4 消除）

6. **Show/Release/Invalidate/GetClientSize**：
   - Show：`ShowWindow(m_hwnd, SW_SHOW); UpdateWindow(m_hwnd);`
   - Release：`if (!m_hwnd) return true; return DestroyWindow(m_hwnd) != FALSE;`
   - Invalidate：`if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);`
   - GetClientSize：`RECT rc; GetClientRect(m_hwnd, &rc); return Size{float(rc.right-rc.left), float(rc.bottom-rc.top)};`（float 直传——C2397 教训）

## 3. 改动文件详细

### 3.1 `Window.h`

```cpp
#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Platform/PlatformWindowHost.h"   // 继承需要完整定义
#include "ECDI/Render/GDIBackend.h"             // 暂留（值成员 m_backend，7.1.4 清）
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"

// ⚠️ 移除：<Windows.h> include、DrawText undef（m_handle/WindowProc 已移除）
// ⚠️ 移除：WindowMessageHandler.h include（翻译器搬走）

#include <string>
#include <memory>

namespace ECDI{

class PlatformWindow;   // 前置声明（unique_ptr 成员）
class WindowClass;      // 前置声明（构造参数引用，P4 定稿）
class Application;
class Widget;
class KeyDownEvent;

class Window : public PlatformWindowHost{   // 新增：实现 Host 契约
public:
    Window(Application* app, const WindowClass& windowClass,   // 签名不变（P4）
           const std::string& title, int width, int height);
    // 拷贝/移动删除、Show/GetRootWidget/Release/焦点/捕获/Invalidate/GetTextMeasurer/
    // HandleKeyDown/NotifyIMEComposition 保留（签名不变，框架职责）

    // ── IME 中介保留（TextBox 零平台依赖——经 Window 调平台）──
    void UpdateTextInputCaret(const Point& clientPos);   // 薄转发 → m_platformWindow
    void DestroyTextInputCaret();                        // 薄转发 → m_platformWindow

    // ── PlatformWindowHost 实现（protected/private override）──
protected:
    void OnPaint() override;                            // PaintFrame
    void OnResized(int width, int height) override;     // m_rootWidget->SetSize
    void OnExitSizeMove() override;                     // DestroyTextInputCaret + NotifyIMEComposition
    Window* GetWindow() const noexcept override { return const_cast<Window*>(this); }

private:
    void PaintFrame();       // 保留（框架编排，被 OnPaint 调用）
    void FocusNext(int direction = 1);   // 保留

    Application* m_application = nullptr;
    std::unique_ptr<PlatformWindow> m_platformWindow;   // 新增（组合，非拥有创建）
    std::unique_ptr<Widget> m_rootWidget;
    Widget* m_focusedWidget = nullptr;
    Widget* m_captureWidget = nullptr;
    GDIBackend m_backend;    // 暂留（7.1.4 unique_ptr<RenderingBackend>）
    Renderer m_renderer;
    CommandBuffer m_commands;
    // ⚠️ 移除：m_handle / m_messageHandler / m_caretCreated
};
}
```

**验收自查（V2）**：Window.h 无 HWND/HDC/UINT/WPARAM/LPARAM/LRESULT/RECT/IME API ✓（唯一 Win32 痕迹是 GDIBackend.h 间接 include——7.1.4 清，V2 只查文件本体）。

### 3.2 `Window.cpp`

```cpp
#include "ECDI/Window/Window.h"
#include "ECDI/Platform/Win32/Win32PlatformWindow.h"   // 新增（工厂创建 + SetHwnd 过渡）
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/WindowClass.h"   // include 保留（构造用 GetClassName）
#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/Render/PaintContext.h"

#include <Windows.h>   // 保留：SetHwnd 过渡需要 HWND（7.1.4 移除）
// ⚠️ 移除：<imm.h>（Imm 调用已下沉 Win32PlatformWindow）
#include <memory>
#include <string>
#include <vector>
```

**构造**（原 CreateWindowExW 部分替换为工厂创建）：
```cpp
Window::Window(Application* app, const WindowClass& windowClass,
               const std::string& title, int width, int height)
    : m_application(app)
    , m_renderer(m_backend){
    // 工厂创建平台窗口（CreateWindowExW 在 Win32PlatformWindow 构造内完成）
    auto platform = std::make_unique<Win32PlatformWindow>(
        *this, app, windowClass, title, width, height);
    // ⚠️ 过渡（7.1.4 由 PlatformRenderContext 替换）：后端注入 HWND
    m_backend.SetHwnd(platform->GetHandle());
    m_platformWindow = std::move(platform);

    m_rootWidget = std::make_unique<Widget>();
    m_rootWidget->SetWindow(this);
    FRAMEWORK_ASSERT(m_rootWidget != nullptr);
    // 初始客户区尺寸（平台窗口构造后查询——GetClientSize 为框架层 Size）
    const Size clientSize = m_platformWindow->GetClientSize();
    m_rootWidget->SetSize(clientSize.width, clientSize.height);
}
```

**Show/Release/Invalidate** → 转调 `m_platformWindow->...`。
**UpdateTextInputCaret/DestroyTextInputCaret** → 薄转发 `m_platformWindow->...`。
**NotifyIMEComposition** 保留（dynamic_cast\<TextBox\> + GetCaretClientPosition → `m_platformWindow->UpdateTextInputCaret(pt)`）。
**PlatformWindowHost 实现**：
- `OnPaint()` → `PaintFrame()`
- `OnResized(w,h)` → `if (m_rootWidget) m_rootWidget->SetSize(w, h);`
- `OnExitSizeMove()` → `DestroyTextInputCaret(); NotifyIMEComposition();`（原 WM_EXITSIZEMOVE case 逻辑）
- `GetWindow()` → `this`

**移除**：WindowProc / HandleMessage / IME 三件套平台调用（已搬 Win32PlatformWindow）。

### 3.3 `Application.cpp`（一行）

```cpp
Application::Application():m_windowClass("ECDI FrameWork", Win32PlatformWindow::WindowProc) {
```
+ include `ECDI/Platform/Win32/Win32PlatformWindow.h`（静态方法声明需完整定义）

### 3.4 `ECDI.vcxproj`
- ClCompile 注册：`src\Platform\Win32\Win32PlatformWindow.cpp`
- ClInclude 注册：`include\ECDI\Platform\PlatformWindow.h`、`PlatformWindowHost.h`、`Win32\Win32PlatformWindow.h`

## 4. 实现顺序（Step 1-5，每条可独立编译验证）

| Step | 内容 | 验证 |
|---|---|---|
| 1 | 新写 PlatformWindow.h + PlatformWindowHost.h（纯契约，零依赖） | 编译通过（未引用） |
| 2 | 新写 Win32PlatformWindow.h/cpp（从 Window.cpp 搬入 WindowProc/HandleMessage/IME 平台调用） | 编译通过（暂未接入） |
| 3 | 改 Window.h/cpp（组合 + Host 实现 + 工厂创建 + 薄转发） | 编译（此时 Application 还引用 Window::WindowProc——报错预期） |
| 4 | 改 Application.cpp 一行 + vcxproj 注册 | 编译通过 → V1 |
| 5 | 验证 V2-V5（grep + 交互回归） | 全过收尾 |

## 5. 验证（V1-V5）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | **Window.h 零 Win32（GPT 验收）** | `grep -E "HWND|HDC|UINT|WPARAM|LPARAM|LRESULT|RECT|Imm|Caret"` Window.h 无命中 |
| V3 | 回归-事件 | 鼠标/键盘/字符/IME 消息翻译与派发行为不变（demo 全交互） |
| V4 | 回归-渲染/焦点 | 绘制无回归（PaintFrame 经 OnPaint 回调链）；Tab 焦点导航正常 |
| V5 | 回归-IME | 中文候选窗跟随光标；移动窗口 EXITSIZEMOVE 归位（经 OnExitSizeMove 链） |

## 6. 技术债记账（7.1.1 过渡项）

| 债务 | 归属 | 消除时机 |
|---|---|---|
| Win32PlatformWindow 构造持 `Application*`（转翻译器） | Win32PlatformWindow | 7.1.2 拆派发（翻译器 → Host::OnEvent） |
| Host::GetWindow() 返回 Window*（Event 构造必需） | PlatformWindowHost | Event 模型不变则长期保留；若 7.1.2 改 Event 绑定再评估 |
| Window.cpp `SetHwnd(platform->GetHandle())` 过渡（HWND 接触） | Window.cpp | 7.1.4 PlatformRenderContext 替换 |
| Window.h 仍含 GDIBackend.h（间接 Win32） | Window.h | 7.1.4 unique_ptr\<RenderingBackend\> |
| 翻译器物理位置仍 src/Window/ | WindowMessageHandler | 7.1.2（契约改造时一并迁 Platform） |

## 7. 修订记录

- v1.0（2026-08-15）详细设计定稿：3 新 + 2 改 + 构建；实现顺序 Step 1-5；V1-V5 验收；6 项过渡债务。关键设计点：Host 4 接口（OnPaint/OnResized/OnExitSizeMove/GetWindow——YAGNI 去 OnEvent/OnIMEComposition/OnDestroyed）；WM_SIZE 状态同步后 fall-through 翻译器（保持现状）；初始客户区用 GetClientSize（避开构造顺序问题）；Win32PlatformWindow 工厂创建 + SetHwnd 过渡。
- v1.1（2026-08-15）**验证通过**：V1 编译（一次 C4244 修复——GetClientSize 返回 float Size → SetSize(int) 显式 static_cast）；V2 Window.h 零 Win32 grep 实证；V3-V5 用户实测"原本功能都正常"（事件/渲染/焦点/IME 回归）。实现落地 3 新 + 2 改 + vcxproj（见工作日志 2026-08-14）。
