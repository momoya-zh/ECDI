# Phase 7.1.4 Backend 注入 — 详细设计

> 状态：v1.1（2026-08-16）｜✅ 已实现（用户验证：编译零警告 + 渲染回归正常）
> 相关：phase7-backend-requirements.md（职责确认 D1-D5 + D1a，v1.2）/ phase7-backend-preliminary-design.md（初步设计 v1.1）
> 目标：解决决策 35 代价——Window 持 GDIBackend 值成员 → 后端不可替换
> 终态（GPT）：Window.h 彻底消失 HWND/GDIBackend/HDC/HBITMAP/CreateWindowEx/SetHwnd

## 0. 实现前置事实（已核实）

| # | 事实 | 对实现的影响 |
|---|---|---|
| F1 | GDIBackend.cpp 测量（MeasureText/LineHeight）零 hwnd 依赖（GetDC(NULL)）；唯一共享 = GetOrCreateFont/m_fontCache | GDITextMeasurer 独立类（fontCache 双份） |
| F2 | GDIBackend.cpp 渲染部分（BeginFrame/DrawRect/DrawText/EndFrame + Ensure/ReleaseBackBuffer + ToColorRef）用 m_hwnd/m_memoryDC | GDIBackend 保留全套 + fontCache（DrawText 需 HFONT） |
| F3 | GDIBackend.h include Windows.h + DrawText undef（skill 9 宏防护）——渲染用 TextOutW | 拆后两类的头都需要（GDITextMeasurer 用 GetDC/GetTextExtent 等） |
| F4 | Window.h:6 include GDIBackend.h → 引入 Windows.h；7.1.4 删除后 **Window.h 彻底零 Windows.h** | include 换 RenderingBackend.h + TextMeasurer.h + RenderServices.h + BackendFactory.h |
| F5 | Window.h:146 `GDIBackend m_backend` + 148 `Renderer m_renderer`（决策 34 引用成员，初始化列表绑定） | 成员改 unique_ptr 双成员——**m_renderBackend 声明在 m_renderer 前** |
| F6 | Window.cpp:46 `m_renderer(m_backend)`、55 `SetHwnd(GetHandle())`、96 `PaintContext ctx(m_commands, m_backend)`、138 `GetTextMeasurer → m_backend` | 四处改造（P5 蓝图） |
| F7 | Window.cpp include <Windows.h>（7.1.1 后代码零 Win32——疑似残留，需 grep 确认可删） | 清理项 |
| F8 | Win32PlatformWindow.h:48 GetHandle() 声明 + cpp:195-199 实现（7.1.1 过渡） | 删除（D4） |
| F9 | Win32PlatformWindow.cpp 构造：CreateWindowExW 成功（m_hwnd 就绪，45 行）→ 构造体末尾加 SetHandle | m_renderContext.SetHandle(m_hwnd) |
| F10 | PaintContext 构造：`PaintContext(CommandBuffer&, TextMeasurer&)`（路线 X） | `ctx(m_commands, *m_textMeasurer)` 成立 |
| F11 | RenderingBackend.h 无 PlatformRenderContext 依赖（当前接口纯净） | +Initialize 默认空 + 前置声明（const& 参数零 include） |
| F12 | vcxproj：ClCompile 区 153-177（GDIBackend 161）/ ClInclude 区 179+（RenderingBackend 189 / GDIBackend 191） | 注册插入点 |

## 1. 文件清单

**新 6**：`Render/RenderServices.h` / `Platform/PlatformRenderContext.h` / `Platform/Win32/Win32RenderContext.h`（头内联，无 cpp）/ `Render/GDITextMeasurer.h+cpp` / `Render/BackendFactory.h+cpp`
**改 8**：`RenderingBackend.h`（+Initialize）/ `GDIBackend.h+cpp`（拆类）/ `PlatformWindow.h`（+GetRenderContext）/ `Win32PlatformWindow.h+cpp`（+RenderContext+删 GetHandle）/ `Window.h+cpp`（核心）/ `ECDI.vcxproj`（注册）
**零改动**：`RecordingBackend.h/cpp`（Initialize 默认空继承）、`main.cpp`（构造默认参数）、`Renderer`（决策 34）

## 2. 逐文件实现蓝图

### Step 1：契约头 4 个 + RenderingBackend::Initialize（纯新增，零破坏编译）

```cpp
// ① Render/RenderServices.h：
#pragma once
#include <memory>
namespace ECDI{
class RenderingBackend;   // 前置声明
class TextMeasurer;
struct RenderServices{
    std::unique_ptr<RenderingBackend> renderer;   ///< 绘制能力（GDIBackend）
    std::unique_ptr<TextMeasurer> measurer;       ///< 测量能力（GDITextMeasurer）
};
}

// ② Platform/PlatformRenderContext.h（空基类，防 void* 类型擦除——7.1.1 c-2 定稿）：
#pragma once
namespace ECDI{
class PlatformRenderContext{
public:
    virtual ~PlatformRenderContext() = default;
};
}

// ③ Platform/Win32/Win32RenderContext.h（极简值类，头内联）：
#pragma once
#include "ECDI/Platform/PlatformRenderContext.h"
#include <Windows.h>
namespace ECDI{
class Win32RenderContext : public PlatformRenderContext{
public:
    Win32RenderContext() noexcept = default;
    explicit Win32RenderContext(HWND hwnd) noexcept : m_hwnd(hwnd){}
    HWND GetHandle() const noexcept{ return m_hwnd; }
    void SetHandle(HWND hwnd) noexcept{ m_hwnd = hwnd; }
private:
    HWND m_hwnd = nullptr;
};
}

// ④ RenderingBackend.h：+（前置声明 class PlatformRenderContext;）
/// @brief 平台句柄注入（7.1.4；默认空实现——无需句柄的后端（如 RecordingBackend）零改动）
/// @param context 平台渲染上下文（Win32 后端 static_cast 取句柄——体系内约定，非跨层 dynamic_cast）
virtual void Initialize(const PlatformRenderContext& context) {}
```

### Step 2：GDIBackend 拆类 + GDITextMeasurer（预期 Window.cpp 编译报错——m_backend 失测量）

```cpp
// GDIBackend.h 改造：
//   - 类声明：class GDIBackend : public RenderingBackend{...}（删 : , public TextMeasurer）
//   - 删：MeasureText/LineHeight 声明；SetHwnd 声明（→ 删，Initialize 直接赋值）
//   - +：void Initialize(const PlatformRenderContext& context) override;
//   - 保留：Windows.h + DrawText undef / m_hwnd / 双缓冲成员 / GetOrCreateFont / m_fontCache / ToColorRef
// GDIBackend.cpp 改造：
//   - + include "ECDI/Platform/Win32/Win32RenderContext.h"（static_cast 需完整定义——同体系识别）
//   - SetHwnd 实现删 → Initialize override：
//     void GDIBackend::Initialize(const PlatformRenderContext& context){
//         m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
//     }
//   - 删 MeasureText/LineHeight 实现（搬 GDITextMeasurer.cpp）
//   - 析构保留（ReleaseBackBuffer + m_fontCache 清理——渲染 DrawText 的字体缓存）

// GDITextMeasurer.h（新）：
#pragma once
#include <Windows.h>
#ifdef DrawText
#undef DrawText   // skill 9：含 Windows.h 的头必须紧跟宏防护
#endif
#include "ECDI/Render/TextMeasurer.h"
#include <map>
#include <string>
namespace ECDI{
/// @brief GDI 文本测量器（7.1.4 拆类：测量与渲染分离——单一职责，用户决策 A）
/// @details 纯测量零 hwnd：GetDC(NULL) 临时屏幕 DC；fontCache 与 GDIBackend 各持一份
/// （GetOrCreateFont 同逻辑——技术债已记，未来提取 FontCache 共享）。
class GDITextMeasurer : public TextMeasurer{
public:
    GDITextMeasurer() = default;
    ~GDITextMeasurer() override;   ///< fontCache 清理（GDI 对象 10,000 上限纪律）
    Size MeasureText(const Font& font, const std::string& text) override;
    float LineHeight(const Font& font) override;
private:
    HFONT GetOrCreateFont(const Font& font);   ///< 与 GDIBackend 同逻辑（缓存取/建 HFONT）
    std::map<std::pair<float, std::string>, HFONT> m_fontCache;
};
}
// GDITextMeasurer.cpp：搬自 GDIBackend.cpp 209-270 行（MeasureText/LineHeight）+ 173-207（GetOrCreateFont）
//   + 析构（fontCache 清理）——逐字搬移，逻辑零改动（Font.h 的 size/family 键语义不变）
```

### Step 3：PlatformWindow::GetRenderContext + Win32PlatformWindow（预期 Window.cpp:55 报错——GetHandle 消失）

```cpp
// PlatformWindow.h：+（前置声明 class PlatformRenderContext;）
/// @brief 平台渲染上下文（7.1.4：后端经此拿平台句柄——"参数识别"→"平台返回"）
virtual const PlatformRenderContext& GetRenderContext() const = 0;

// Win32PlatformWindow.h：
//   + include "ECDI/Platform/Win32/Win32RenderContext.h"
//   + public：const PlatformRenderContext& GetRenderContext() const override;
//   - 删：HWND GetHandle() const noexcept;（48 行——7.1.1 过渡，D4 消除）
//   + private 成员：Win32RenderContext m_renderContext;（值成员——与 Win32PlatformWindow 同族）
// Win32PlatformWindow.cpp：
//   - 删 GetHandle 实现（195-199 行）
//   + 构造体末尾（CreateWindowExW 成功后）：
//     m_renderContext.SetHandle(m_hwnd);   // F9：hwnd 就绪后绑定
//   + const PlatformRenderContext& Win32PlatformWindow::GetRenderContext() const override{
//         return m_renderContext;
//     }
```

### Step 4：Window.h/cpp 改造（核心，编译恢复）

```cpp
// Window.h：
//   include：删 "ECDI/Render/GDIBackend.h"
//            + "ECDI/Render/RenderingBackend.h" + "ECDI/Render/TextMeasurer.h"
//            + "ECDI/Render/RenderServices.h" + "ECDI/Render/BackendFactory.h"
//   （F4：GDIBackend.h 删除后 Window.h 零 Windows.h——宏防护 include 一并消失）
//   构造签名：Window(Application* app, const WindowClass& windowClass, const std::string& title,
//                    int width, int height, RenderServices services = CreateDefaultRenderServices());
//   成员区（⚠️ 顺序：m_renderBackend 必须声明在 m_renderer 前——F5）：
//       std::unique_ptr<RenderingBackend> m_renderBackend;   // 绘制能力（GDIBackend）
//       std::unique_ptr<TextMeasurer> m_textMeasurer;        // 测量能力（GDITextMeasurer）
//       Renderer m_renderer;                                 // 决策 34：持 RenderingBackend&
//       CommandBuffer m_commands;
//   - 删：GDIBackend m_backend;
//   GetTextMeasurer 注释更新（"GDIBackend 兼 TextMeasurer" → "m_textMeasurer——拆类后独立测量器"）

// Window.cpp：
//   构造：
Window::Window(Application* app, const WindowClass& windowClass, const std::string& title,
               int width, int height, RenderServices services)
    : m_application(app)
    , m_renderBackend(std::move(services.renderer))
    , m_textMeasurer(std::move(services.measurer))
    , m_renderer(*m_renderBackend)          // ⚠️ 声明顺序：m_renderBackend 在 m_renderer 前
    , m_platformWindow(std::make_unique<Win32PlatformWindow>(*this, windowClass, title, width, height)){
    // 7.1.4：句柄注入经 PlatformRenderContext（取代 SetHwnd(GetHandle()) 过渡）
    m_renderBackend->Initialize(m_platformWindow->GetRenderContext());
    m_rootWidget = std::make_unique<Widget>();
    m_rootWidget->SetWindow(this);
    FRAMEWORK_ASSERT(m_rootWidget != nullptr);
    const Size clientSize = m_platformWindow->GetClientSize();
    m_rootWidget->SetSize(static_cast<int>(clientSize.width), static_cast<int>(clientSize.height));
}
//   PaintFrame（96 行）：PaintContext ctx(m_commands, *m_textMeasurer);
//   GetTextMeasurer（138 行）：return *m_textMeasurer;
//   include 清理：grep 确认 <Windows.h> 无使用后删（F7）
```

### Step 5：vcxproj 注册 + 验证

```xml
<!-- ClCompile（157 行 Win32PlatformWindow 后 / 161 行 GDIBackend 旁）：
     + <ClCompile Include="src\Render\GDITextMeasurer.cpp" />
     + <ClCompile Include="src\Render\BackendFactory.cpp" />
     ClInclude（183 行 PlatformWindow.h 旁）：
     + <ClInclude Include="include\ECDI\Platform\PlatformRenderContext.h" />
     + <ClInclude Include="include\ECDI\Platform\Win32\Win32RenderContext.h" />
     + <ClInclude Include="include\ECDI\Render\RenderServices.h" />
     + <ClInclude Include="include\ECDI\Render\GDITextMeasurer.h" />
     + <ClInclude Include="include\ECDI\Render\BackendFactory.h" /> -->
```

## 3. 验证（V1-V3）

| # | 验收项 | 判据 |
|---|---|---|
| V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
| V2 | 渲染回归 | 绘制/文本/IME 候选窗跟随/移动窗口归位——**行为零变化**（仅持有方式 + 类拆分） |
| V3 | grep 实证 | Window.h 零 GDIBackend/SetHwnd/GetHandle/shared_ptr/**Windows.h**；main.cpp 零改动确认（diff） |

## 4. 技术债记账（7.1.4 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| GDITextMeasurer 与 GDIBackend 的 GetOrCreateFont/fontCache 逻辑重复 | GDITextMeasurer + GDIBackend | 字体数量显著增长/第三消费者 → 提取 FontCache 共享工具 |
| GDIBackend::Initialize 内 static_cast\<Win32RenderContext\> | GDIBackend.cpp | 第二平台后端出现时评估 |

## 5. 回退基线

**v1.1 方案确认时的状态**（7.1.3 后：Window 持 GDIBackend 值成员 + SetHwnd 过渡 + CaretGeometry 全链）。实现失败时恢复点：git 工作区在实现前是干净的（7.1.3 已 commit），`git checkout -- .` 可完整回退。

## 6. 修订记录

- v1.0（2026-08-16）详细设计定稿：Step 1-5（契约头 → 拆类 → Platform → Window → 构建验证）。F1-F12 前置事实；每步预期编译报错点已标注（增量重构，类似 7.1.1/7.1.2）。
- v1.1（2026-08-16）验证通过：V1 编译零警告 + V2 渲染回归正常（用户实测）。V3 grep 实证：Window.h 代码层零 GDIBackend/SetHwnd/GetHandle/shared_ptr/Windows.h；main.cpp 零改动。实现补充：Window.cpp 删 Windows.h 残留（F7）；TextBox.cpp:405 过时注释修正（m_backend → *m_textMeasurer）。
