# Phase 7.1.1 PlatformWindow 骨架 — 初步设计

> 状态：v1.1（2026-08-15，GPT 边界守则修订）｜待用户确认后进详细设计
> 相关：phase7-platform-requirements.md（职责确认 D1-D8，v1.0.1 决策点已定稿）
> 原则：**平台负责"窗口存在"，框架负责"窗口里面发生什么"**

## 0. 7.1.1 目标与验收标准（GPT 核心修订——守边界）

> **7.1.1 的目标 = "让 Window 不再认识 Win32"，不是"一次性完成整个平台抽象"。**

**验收标准（GPT 定义）**：`Window.h` 不再出现 Win32 类型——HWND / HDC / UINT / WPARAM / LPARAM / LRESULT / RECT / IME API。

**注意区分**：
- ✅ 目标：**Window 不包含 Win32**
- ❌ 非目标：整个框架完全没有 Win32（Application/WindowClass/MessageLoop 仍含——7.1.5 评估）

**本轮明确不做**（GPT 逐项确认）：
- ❌ 翻译器内部结构不动（Translate+Dispatch 一体保留，7.1.2 再拆）
- ❌ `dynamic_cast<TextBox*>` 技术债不处理（7.1.3 只做平台解耦；EditableTextWidget 以后做）
- ❌ WindowClass / MessageLoop 不动（标记 Phase 7.1.5）
- ❌ Application 不改（D5 挂起，仅 P4 一行 WindowProc 引用适配）

## 0.1 实现前置事实（决定 7.1.1 边界的三个耦合）

| # | 事实 | 对 7.1.1 的影响 |
|---|---|---|
| F1 | `WindowMessageHandler::Handle` 是**翻译 + 派发一体**：构造 Event（绑 `Window*`）→ 直接 `m_application->OnEvent(event)` | **7.1.1 保持现状整体搬入**（结构不动）；7.1.2 拆 Translate/Dispatch（GPT 建议：翻译器只做 Win32→Event，派发交给 Host） |
| F2 | `Window::HandleMessage` 的**状态同步与翻译在同一个 switch**（WM_PAINT/SIZE/DESTROY/EXITSIZEMOVE 自处理 + 其余调翻译器） | 7.1.1 骨架必然**整体搬迁 HandleMessage**（含翻译器调用）——物理不可分 |
| F3 | IME 三方法（`NotifyIMEComposition`/`UpdateTextInputCaret`/`DestroyTextInputCaret`）**直接用 `m_handle`(HWND) + Imm API** | 7.1.1 移除 `Window::m_handle` 后编译必然断裂 → **IME 平台调用（CreateCaret/SetCaretPos/Imm\*）随骨架下沉**；Window 保留框架逻辑（dynamic_cast\<TextBox\> + 坐标计算） |

**结论**：7.1.1 = **骨架 + 消息处理整体搬迁 + IME 平台调用下沉**（F2/F3 物理必要），但**守住 0 节边界**——不拆翻译器结构、不处理 dynamic_cast 债务、不动 WindowClass/Application。7.1.2/7.1.3 为契约层改造（非重复搬运）。

## 1. 新增文件（3 新 + 1 决策点）

### P1 `Platform/PlatformWindow.h` — 抽象基类（纯框架契约，零 Win32）

```cpp
namespace ECDI{
class Point;   // Core 类型（7.1.1 先用 Point；7.1.3 换 CaretGeometry{ Rect }）

/// @brief 平台窗口抽象（7.1）：平台负责"窗口存在"
/// @details Window 组合此接口——Window 不接触 HWND/创建细节；
/// 生命周期 + 平台能力（重绘请求/文本输入插入点）下沉。
class PlatformWindow{
public:
    virtual ~PlatformWindow() = default;

    virtual void Show() = 0;                            ///< 显示窗口
    virtual bool Release() noexcept = 0;                ///< 销毁底层窗口句柄
    virtual void Invalidate() = 0;                      ///< 请求重绘整个客户区
    /// @brief 更新文本输入插入点（7.1.1 平台调用下沉；客户区坐标——语义封装在实现内）
    /// @param clientPos 光标顶部客户区坐标（框架层 Point，非 Win32 类型）
    virtual void UpdateTextInputCaret(const Point& clientPos) = 0;
    virtual void DestroyTextInputCaret() = 0;           ///< 销毁文本输入插入点
};
}
```

### P2 `Platform/PlatformWindowHost.h` — Host 回调接口（框架契约，零 Win32）

```cpp
namespace ECDI{
class Event;

/// @brief 平台窗口宿主（7.1 D2 核心）：Platform 不认识框架具体类，只认识此契约
/// @details Window 实现此接口；Win32PlatformWindow 持 Host& 回调
class PlatformWindowHost{
public:
    virtual ~PlatformWindowHost() = default;

    virtual void OnPaint() = 0;                             ///< WM_PAINT → 帧编排（PaintFrame）
    virtual void OnResized(int width, int height) = 0;      ///< WM_SIZE → RootWidget 尺寸同步
    virtual void OnDestroyed() = 0;                         ///< WM_DESTROY → 句柄失效通知
    virtual void OnIMEComposition() = 0;                    ///< WM_IME_START/COMPOSITION → 候选窗定位
    virtual void OnEvent(Event& event) = 0;                 ///< 翻译后的事件派发（7.1.2 契约改造目标）
};
}
```

### P3 `Platform/Win32/Win32PlatformWindow.h/cpp` — Win32 实现（唯一实现，X11/Wayland 只留接口）

- `WindowProc`（静态，原 `Window::WindowProc` 整体搬来——GWLP_USERDATA 绑定改绑 Win32PlatformWindow 实例）
- `HandleMessage`（原 `Window::HandleMessage` 整体搬来——F2：状态同步 + 翻译器调用都在此）
- `CreateWindowExW`（原 Window 构造的平台部分）、`ShowWindow`/`DestroyWindow`/`InvalidateRect`
- IME 平台调用（原 Window.cpp 342-386 行的 CreateCaret/SetCaretPos/HideCaret/Imm 三件套——F3 下沉）
- 组合 `WindowMessageHandler`（翻译器随 HandleMessage 物理下沉；**7.1.1 内部结构保持现状**——Translate+Dispatch 一体不动，7.1.2 再拆）
- 持 `PlatformWindowHost&`（构造注入）
- **记账（GPT）**：CreateCaret/SetCaretPos/Imm\* 职责上属 `Win32IME`（非 Win32PlatformWindow）——未来 `Win32PlatformWindow → Win32IME` 组合；当前整体搬入为编译必要（F3），不做类拆分

### P4 ✅ WindowClass 归属（GPT 明确支持方案 A——定稿）
- **✅ 方案 A（定稿）**：**不动**——Application 仍持有 `m_windowClass`（D5 挂起），`Window.h` 前置声明 `class WindowClass` → Window 构造签名**保持不变** `(Application*, const WindowClass&, title, width, height)`，内部转给 Win32PlatformWindow。Application.cpp 仅一处适配：`Window::WindowProc` → `Win32PlatformWindow::WindowProc`
- **理由（GPT）**：**不要同时修改两个入口**——Window→PlatformWindow 已是大重构，再改 Application→MessageLoop→WindowClass 调试极难
- **记账**：WindowClass / MessageLoop 明确标记 **Phase 7.1.5**（7.1.1 不提前处理）
- 注：方案 A 下 Window.h 仍含 GDIBackend.h（Win32）——7.1.4 Backend 注入后彻底清干净

## 2. 改动文件

### P5 `Window.h`
- 移除：`m_handle`(HWND)、`WindowProc`、`HandleMessage`、`m_messageHandler`、`m_caretCreated`、`NotifyIMEComposition`/`UpdateTextInputCaret`/`DestroyTextInputCaret` 声明
- 新增：`std::unique_ptr<PlatformWindow> m_platformWindow`（前置声明）
- 保留：`GetTextMeasurer`/`PaintFrame`/`FocusNext`/焦点/捕获/`GetCaretClientPosition` 相关——框架职责全留
- include：前置声明 `PlatformWindow`/`WindowClass`；GDIBackend.h 暂留（7.1.4 清）
- `Window : public PlatformWindowHost`（实现 OnPaint/OnResized/OnDestroyed/OnIMEComposition/OnEvent）
- **技术债明示（GPT）**：`NotifyIMEComposition` 内 `dynamic_cast<TextBox*>` 是 Phase 5 遗留债务——**7.1.1 不处理**（保留现状）；7.1.3 的 CaretGeometry 只负责平台解耦；EditableTextWidget 抽象等第二个可编辑控件出现再做

### P6 `Window.cpp`
- 构造：创建 Win32PlatformWindow（注入 Host + WindowClass + title/width/height）→ 原 CreateWindowExW/SetHwnd/GetClientRect 同步逻辑移入
- `Show`/`Release`/`Invalidate` → 转调 `m_platformWindow->...`
- `NotifyIMEComposition` 保留框架逻辑（dynamic_cast\<TextBox\> + GetCaretClientPosition → `m_platformWindow->UpdateTextInputCaret`）
- 移除 WindowProc/HandleMessage/IME 平台调用（已搬 Win32PlatformWindow）
- 实现 PlatformWindowHost 5 回调（OnPaint → PaintFrame；OnResized → RootWidget SetSize；OnDestroyed → m_platformWindow 置空语义；OnIMEComposition → NotifyIMEComposition；OnEvent → m_application->OnEvent）

### P7 构建
- vcxproj：注册 3 新文件（PlatformWindow.h / PlatformWindowHost.h / Win32PlatformWindow.h+cpp 按工程文件粒度）；CMake GLOB 零改动

### P8 验证（V1-V5）
| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | **Window.h 零 Win32（GPT 验收标准）** | Window.h 不出现 HWND/HDC/UINT/WPARAM/LPARAM/LRESULT/RECT/IME API（grep 验证） |
| V3 | 回归-事件 | 鼠标/键盘/字符/IME 消息翻译与派发行为不变（demo 全交互过一遍） |
| V4 | 回归-渲染/焦点 | 绘制无回归（PaintFrame 经 Host 回调链）；Tab 焦点导航正常 |
| V5 | 回归-IME | 中文候选窗跟随光标（IME 平台调用下沉后行为不变）；移动窗口 EXITSIZEMOVE 归位 |

## 3. 边界（7.1.1 不做）

- ❌ 不改 Application 设计（D5 挂起）——仅 P4 的一行 `WindowProc` 引用适配
- ❌ 不动翻译器内部结构（Translate+Dispatch 一体保留——7.1.2 再拆）
- ❌ 不处理 dynamic_cast\<TextBox\> 债务（7.1.3 只做平台解耦；EditableTextWidget 以后做）
- ❌ 不动 WindowClass / MessageLoop（标记 7.1.5）
- ❌ 不做 CaretGeometry 结构（7.1.3）
- ❌ 不做 Backend 注入（7.1.4：unique_ptr<RenderingBackend> + PlatformRenderContext）
- ❌ 不碰 GDIBackend 内部（4.7 稳定代码）

## 4. 修订记录

- v1.0（2026-08-15）初步设计定稿：P1-P8。基于实现前置事实 F1-F3（翻译器派发一体 / HandleMessage 同 switch / IME 用 m_handle）——7.1.1 实际 = 骨架 + 消息处理整体搬迁 + IME 平台调用下沉，7.1.2/7.1.3 为契约层改造。P4 WindowClass 归属为决策点（方案 A 倾向）。
- v1.1（2026-08-15，GPT 边界守则）核心修订——**守边界：7.1.1 = "让 Window 不再认识 Win32"，非"一次性完成平台抽象"**：① 新增第 0 节目标与验收标准（Window.h 零 Win32 类型：HWND/HDC/UINT/WPARAM/LPARAM/LRESULT/RECT/IME API）② F1 翻译器 7.1.1 保持现状整体搬入（不拆 Translate/Dispatch，7.1.2 再拆）③ P3 记账 Win32IME 未来独立类（CreateCaret/Imm 职责属 Win32IME）④ **P4 方案 A 定稿**（GPT 明确支持：不同时改两个入口；WindowClass/MessageLoop 标记 7.1.5）⑤ P5 明示 dynamic_cast\<TextBox\> 遗留债务 7.1.1 不处理 ⑥ P8 加 V2 验收（Window.h 零 Win32 grep 验证）。
