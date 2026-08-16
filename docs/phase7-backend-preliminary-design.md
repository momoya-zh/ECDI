# Phase 7.1.4 Backend 注入 — 初步设计

> 状态：v1.0（2026-08-16）｜待用户确认后进详细设计
> 相关：phase7-backend-requirements.md（职责确认 D1-D5 + D1a，v1.2）
> 目标：解决**决策 35 代价**（Window 持 GDIBackend 值成员 → 后端不可替换）
> 终态（GPT）：Window.h 彻底消失 HWND/GDIBackend/HDC/HBITMAP/CreateWindowEx/SetHwnd——只剩纯框架概念

## 0. 实现前置事实（已核实）

| # | 事实 | 对设计的影响 |
|---|---|---|
| F1 | GDIBackend.cpp 测量实现（MeasureText/LineHeight）用 `GetDC(NULL)` 临时屏幕 DC——**零 hwnd 依赖**；唯一共享依赖 = `GetOrCreateFont` + `m_fontCache`（渲染 DrawText 与测量共用） | GDITextMeasurer 独立类可行；fontCache 拆后各持一份 |
| F2 | GDIBackend 渲染部分（BeginFrame/EnsureBackBuffer/ReleaseBackBuffer/DrawRect/ToColorRef/DrawText/EndFrame）全部用 `m_hwnd`/`m_memoryDC` | 拆后 GDIBackend 保留全套 + GetOrCreateFont/fontCache（DrawText 需要） |
| F3 | Window.cpp:46 `m_renderer(m_backend)`——Renderer 持 `RenderingBackend&`（决策 34，初始化列表绑定） | 改 `m_renderer(*m_renderBackend)`——**成员声明顺序 m_renderBackend 在 m_renderer 前** |
| F4 | Window.cpp:55 `m_backend.SetHwnd(platform->GetHandle())`（7.1.1 过渡） | 替换为 `m_renderBackend->Initialize(platform->GetRenderContext())` |
| F5 | Window.cpp:96 `PaintContext ctx(m_commands, m_backend)` + 138 `GetTextMeasurer() → m_backend` | 改 `*m_textMeasurer`（测量路径独立指针） |
| F6 | RecordingBackend 不注入 Window（main.cpp 断言段 `Renderer renderer(backend)` / `PaintContext ctx(commands, backend)` 各取角色） | 测试类不拆——main.cpp 断言段零改动 |
| F7 | Window 构造默认参数 `= CreateDefaultRenderServices()` 需要 RenderServices 完整定义 + 工厂声明可见 | Window.h include RenderServices.h + BackendFactory.h（零 Win32） |
| F8 | Win32PlatformWindow 的 m_hwnd 在构造体内 CreateWindowExW 后才就绪；Win32RenderContext 需持有该 hwnd | Win32RenderContext 可赋值（构造体内 `m_renderContext = Win32RenderContext(m_hwnd)`）或延迟构造 |

## P1 `Render/RenderServices.h` + `Platform/PlatformRenderContext.h`（新契约头）

```cpp
// Render/RenderServices.h（框架层，零 Win32）：
#pragma once
#include <memory>
namespace ECDI{
class RenderingBackend;   // 前置声明（unique_ptr 成员不需要完整定义）
class TextMeasurer;
/// @brief 渲染服务包（7.1.4：绘制 + 测量两个独立能力一次注入）
/// @details 用户决策 2026-08-16：能力接口分离 + 拆类——unique_ptr 各自独立对象，
/// 无 shared_ptr（默认后端 GDIBackend/GDITextMeasurer 是两个独立类）。
struct RenderServices{
    std::unique_ptr<RenderingBackend> renderer;   ///< 绘制能力（GDIBackend）
    std::unique_ptr<TextMeasurer> measurer;       ///< 测量能力（GDITextMeasurer）
};
}

// Platform/PlatformRenderContext.h（平台契约层，零 Win32——GPT 第三处修订：归位 Platform 非 Render）：
#pragma once
namespace ECDI{
/// @brief 平台渲染上下文基类（7.1.1 c-2 定稿：防 void* 类型擦除假抽象）
/// @details 空基类——平台句柄的类型安全容器。归位 Platform/（GPT 论证）：句柄本质是
/// "窗口/平台句柄"不是"渲染句柄"——Win32RenderContext/X11RenderContext/WaylandRenderContext
/// 都是平台实现家族（与 Win32PlatformWindow 同族），语义统一在 Platform 层。
/// RenderingBackend::Initialize 接收它（Render 层仅前置声明——零 include 依赖，见 P2）。
class PlatformRenderContext{
public:
    virtual ~PlatformRenderContext() = default;
};
}
```

## P2 `RenderingBackend` +Initialize；`GDIBackend` 改造 + 拆出 `GDITextMeasurer`

```cpp
// RenderingBackend.h：+（前置声明 class PlatformRenderContext;）
/// @brief 平台句柄注入（7.1.4；默认空实现——不需要平台句柄的后端（如 RecordingBackend）零改动）
/// @param context 平台渲染上下文（Win32 后端 static_cast 取句柄——体系内约定，非跨层 dynamic_cast）
virtual void Initialize(const PlatformRenderContext& context) {}

// GDIBackend.h：
//   - 类声明改为：class GDIBackend : public RenderingBackend{...}（删 TextMeasurer 继承）
//   - SetHwnd → private（仅 Initialize 内部用）或改名 SetHandle；+ Initialize override
//   - 删 MeasureText/LineHeight 声明（搬 GDITextMeasurer）
//   - 保留：m_hwnd/双缓冲成员/GetOrCreateFont/m_fontCache（DrawText 渲染需要）
// GDIBackend.cpp：
//   - + void Initialize(const PlatformRenderContext& context) override{
//         m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
//     }
//     （GDIBackend.cpp include Win32RenderContext.h——同体系识别）

// GDITextMeasurer.h/cpp（新，Render/ 目录）：
//   class GDITextMeasurer : public TextMeasurer{   // 纯测量，零 hwnd
//       Size MeasureText(const Font&, const std::string&) override;
//       float LineHeight(const Font&) override;
//   private:
//       HFONT GetOrCreateFont(const Font&);        // 与 GDIBackend 同逻辑（双份——技术债已记）
//       std::map<std::pair<float, std::string>, HFONT> m_fontCache;
//   };
//   搬移：MeasureText/LineHeight 实现 + GetOrCreateFont + m_fontCache + 析构清理（GDI 对象 10,000 上限纪律）
//   ⚠️ Windows.h + DrawText undef 宏防护（skill 9——含 Windows.h 的头必须紧跟）
```

## P3 `Render/BackendFactory.h/cpp`（工厂）

```cpp
// BackendFactory.h：
#pragma once
#include "ECDI/Render/RenderServices.h"
namespace ECDI{
/// @brief 平台默认渲染服务（7.1.4：默认后端选择从 Window 移出——GPT D3）
/// @details Win32 → GDIBackend + GDITextMeasurer；未来 Linux → OpenGLRenderer + FreeTypeTextMeasurer
RenderServices CreateDefaultRenderServices();
}
// BackendFactory.cpp：
#include "ECDI/Render/GDIBackend.h"
#include "ECDI/Render/GDITextMeasurer.h"
RenderServices CreateDefaultRenderServices(){
    RenderServices services;
    services.renderer = std::make_unique<GDIBackend>();
    services.measurer = std::make_unique<GDITextMeasurer>();
    return services;
}
```

## P4 `PlatformWindow::GetRenderContext` + `Win32RenderContext` + `Win32PlatformWindow` 持成员

```cpp
// PlatformWindow.h：+（前置声明 class PlatformRenderContext;——const& 返回值只需前置声明）
/// @brief 平台渲染上下文（7.1.4：后端经此拿平台句柄——句柄获取从"参数识别"→"平台返回"）
virtual const PlatformRenderContext& GetRenderContext() const = 0;

// Platform/Win32/Win32RenderContext.h（新，极简值类——头文件内联，不建 cpp）：
#pragma once
#include "ECDI/Platform/PlatformRenderContext.h"
#include <Windows.h>
namespace ECDI{
/// @brief Win32 渲染上下文（7.1.4：HWND 的类型安全容器——防 void* 类型擦除；
/// 与 Win32PlatformWindow 同目录同族——GPT 第三处修订）
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

// Win32PlatformWindow.h：+ 成员 Win32RenderContext m_renderContext;（值成员）
// Win32PlatformWindow.cpp 构造：CreateWindowExW 成功后 → m_renderContext.SetHandle(m_hwnd)（F8：构造体内赋值）
// + GetRenderContext() override{ return m_renderContext; }
// ⚠️ 删除：GetHandle()（7.1.1 过渡——7.1.4 消除，D4）
```

## P5 `Window.h/cpp` 改造（核心）

```cpp
// Window.h：
//   include：删 GDIBackend.h；+ RenderingBackend.h + TextMeasurer.h + RenderServices.h + BackendFactory.h
//            （前置声明 class PlatformRenderContext; 不需要——不在 Window.h 暴露）
//   构造签名：Window(Application* app, const WindowClass& windowClass, const std::string& title,
//                   int width, int height, RenderServices services = CreateDefaultRenderServices());
//   成员：
//       std::unique_ptr<RenderingBackend> m_renderBackend;   // 绘制能力（GDIBackend）——声明在 m_renderer 前！
//       std::unique_ptr<TextMeasurer> m_textMeasurer;        // 测量能力（GDITextMeasurer）
//       Renderer m_renderer;                                 // 决策 34：持 RenderingBackend&（初始化列表绑定）
//   删：GDIBackend m_backend;

// ⚠️ 构造传参方式论证（GPT 第五处担心——按值传参无需 && / optional）：
//   - RenderServices 含 unique_ptr → 不可拷贝、可移动（编译器生成移动构造/赋值）
//   - 按值传参 + std::move 到成员 = move-only 类型标准惯用法（unique_ptr 参数同理）
//   - 默认参数 `= CreateDefaultRenderServices()`：prvalue → C++17 guaranteed copy elision
//     直接在参数位置构造（零拷贝零移动），临时生命周期完整覆盖调用——无悬垂
//   - `RenderServices&&` 仅允许右值调用（灵活性低）；`std::optional` 引入额外复杂度——均不采纳

// Window.cpp 构造：
Window::Window(..., RenderServices services)
    : m_application(app)
    , m_renderBackend(std::move(services.renderer))
    , m_textMeasurer(std::move(services.measurer))
    , m_renderer(*m_renderBackend)          // ⚠️ 声明顺序：m_renderBackend 在 m_renderer 前
    , m_platformWindow(/* 原有参数不变 */){
    // 7.1.4：句柄注入经 PlatformRenderContext（不再是 SetHwnd(GetHandle()) 过渡）
    m_renderBackend->Initialize(m_platformWindow->GetRenderContext());
    // ... 其余（RootWidget 创建/尺寸同步）不变
}

// Window.cpp PaintFrame：PaintContext ctx(m_commands, *m_textMeasurer);   // F5
// Window.cpp GetTextMeasurer：return *m_textMeasurer;                     // F5
```

## P6 vcxproj 注册 + 验证

- vcxproj：ClCompile + GDITextMeasurer.cpp + BackendFactory.cpp；ClInclude + RenderServices.h + PlatformRenderContext.h（Platform/）+ GDITextMeasurer.h + Win32RenderContext.h（Platform/Win32/）+ BackendFactory.h
- 验证：
  | # | 验收项 | 判据 |
  |---|---|---|
  | V1 | 编译 | VS Debug x64 零错误零新警告；三工具链惯例 |
  | V2 | 渲染回归 | 绘制/文本/IME 候选窗跟随/移动窗口归位——**行为零变化**（仅持有方式 + 类拆分） |
  | V3 | grep 实证 | Window.h 零 GDIBackend/SetHwnd/GetHandle/shared_ptr；main.cpp 零改动确认 |

## 边界（7.1.4 不做）

- ❌ 不引入第二后端（OpenGL/X11——YAGNI，无消费者）
- ❌ 不拆 RecordingBackend（测试便利保留——TextMeasurer.h:13 原始设计意图，main.cpp 断言段零改动）
- ❌ 不改 Renderer（决策 34 已抽象，零改动）
- ❌ 不做 RecordingBackend 的 Initialize（测试类不注入平台句柄——默认空实现天然满足）

## 技术债记账（7.1.4 遗留）

| 债务 | 位置 | 消除时机 |
|---|---|---|
| GDITextMeasurer 与 GDIBackend 的 GetOrCreateFont/fontCache 逻辑重复（拆类后各持一份） | GDITextMeasurer.h/cpp + GDIBackend.h/cpp | 字体数量显著增长或出现第三消费者时提取 FontCache 共享工具 |
| GDIBackend::Initialize 内 static_cast\<Win32RenderContext\> | GDIBackend.cpp | 第二平台后端出现时评估 |

## 修订记录

- v1.0（2026-08-16）初步设计定稿：P1-P6。基于前置事实 F1-F8（测量零 hwnd 依赖 + fontCache 双份 / Renderer 引用绑定顺序 / SetHwnd 过渡消除 / RecordingBackend 不拆 / 默认参数工厂）。核心：**unique_ptr 双成员 + GDIBackend 拆类 + PlatformRenderContext 句柄注入**——7.1.4 后 Window.h 零具体后端。
- v1.1（2026-08-16，GPT 评审）四处处理：
  - **第三处（采纳）**：`PlatformRenderContext` 归位 **`Platform/` 目录**（非 Render/）——句柄本质是"窗口/平台句柄"不是"渲染句柄"；Win32RenderContext/X11RenderContext 都是平台实现家族（与 Win32PlatformWindow 同族），语义统一。RenderingBackend.h 仅前置声明（Initialize 参数 const&——零 include 依赖）
  - **第五处（不采纳，论证补充）**：构造保持**按值传参** `RenderServices services`——move-only 按值传 + std::move 是惯用法；默认参数 prvalue 经 C++17 guaranteed copy elision 就地构造、生命周期完整覆盖调用（`&&` 限右值调用、`optional` 多余复杂度——均不采纳）
  - 其余四点（拆类 / 工厂 / Initialize 默认空 / V3 验收）一致确认
