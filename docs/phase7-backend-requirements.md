# Phase 7.1.4 Backend 注入 — 职责确认

> 状态：v1.2（2026-08-16，用户决策 A：7.1.4 拆 GDIBackend）｜待用户确认后进初步设计
> 相关：phase7-platform-requirements.md（c-2 PlatformRenderContext 定稿）/ phase7-platform-detailed-design.md（7.1.1 SetHwnd 过渡记账）
> 目标：解决**决策 35 代价**（Window 持 GDIBackend 值成员 → 后端不可替换）——**v1.0 转库前必须完成**（skill 23）
> 终态（GPT）：Window.h 彻底消失 HWND/GDIBackend/HDC/HBITMAP/CreateWindowEx/SetHwnd——只剩纯框架概念

## 现状（7.1.3 后，已核实）

| 位置 | 现状 | 问题 |
|---|---|---|
| Window.h:6 | include GDIBackend.h（具体类） | Window 认识具体后端 |
| Window.h:146 | `GDIBackend m_backend;` 值成员 | 决策 35 代价：不可替换 |
| Window.cpp:54 | `SetHwnd(platform->GetHandle())` | 7.1.1 过渡（7.1.4 消除） |
| Window.cpp:96 | `PaintContext ctx(m_commands, m_backend)` | m_backend 兼 TextMeasurer |
| Window.cpp:138 | `GetTextMeasurer() → m_backend` | 已返回抽象 TextMeasurer& |
| Renderer | 持 `RenderingBackend&` 引用（决策 34） | 已抽象，零改动 |
| RecordingBackend | 测试类，双接口（RenderingBackend + TextMeasurer） | main.cpp 直接构造 |

## 决策（D1-D5）

### D1 Backend 持有方式 — a（用户 2026-08-16 两轮决策：能力接口分离 + 7.1.4 拆实现）
**✅ 接口层分离 + 实现层同步拆类——`unique_ptr` 双成员，无 shared_ptr**：
```cpp
// Window 成员（单一职责：一个能力一个指针，各自独立对象）：
std::unique_ptr<RenderingBackend> m_renderBackend;   // 绘制能力（GDIBackend）
std::unique_ptr<TextMeasurer> m_textMeasurer;        // 测量能力（GDITextMeasurer）
Renderer m_renderer;                                 // 决策 34：持 RenderingBackend&（构造列表绑定）
```
- **用户论证（决策依据）**："实现可能会用同一个，但不代表它们应该合在一起"——绘制/测量是两种独立能力；**GDIBackend 恰好同时实现两者是 GDI 平台事实，不是接口层合并的理由，也不是实现层保持聚合的理由**
- **用户决策 A（2026-08-16）**：7.1.4 **直接拆 GDIBackend**——拆类成本（fontCache 双份 ≈0）+ 7.1.4 本就在触碰 GDIBackend 接入（SetHwnd→Initialize）→ 一次改造到位，避免"先 shared_ptr 共享控制块、未来再拆一次"的两次改造
- **注入形态：`RenderServices` bundle 一次传入（unique_ptr 语义——拆类后无需共享所有权）**：
```cpp
// Render/RenderServices.h（框架层，零 Win32；RenderingBackend/TextMeasurer 前置声明）：
struct RenderServices{
    std::unique_ptr<RenderingBackend> renderer;   ///< 绘制能力
    std::unique_ptr<TextMeasurer> measurer;       ///< 测量能力
};
```
- **RecordingBackend（测试类）保持单类双实现不拆**（已核实：**不注入 Window**——只在 main.cpp 断言段内 `Renderer renderer(backend)` / `PaintContext ctx(commands, backend)` 各取一个角色）——测试段内"一个对象两角色"是便利，正是 TextMeasurer.h:13 注释"测试类可同时实现二者——纯测试便利"的原始设计意图，main.cpp 断言段零改动

### D1a 拆类范围（用户决策 A 落地）
**✅ GDIBackend 拆为两个独立类**：
```cpp
// Render/GDIBackend.h：class GDIBackend : public RenderingBackend（渲染——保留 m_hwnd/双缓冲/m_fontCache）
// Render/GDITextMeasurer.h：class GDITextMeasurer : public TextMeasurer（测量——搬 MeasureText/LineHeight）
//   ⚠️ fontCache 归属：GDITextMeasurer 自带一份 GetOrCreateFont + m_fontCache（测量独立；v0.1 字体个位数，双份代价≈0）
```
- 拆分依据（已核实 GDIBackend.cpp）：测量实现（MeasureText/LineHeight）用 `GetDC(NULL)` 临时屏幕 DC——**零 hwnd 依赖**，技术上完全独立；唯一共享依赖 = `m_fontCache`（绘制 DrawText 与测量共用 GetOrCreateFont）——拆开后各持一份
- RecordingBackend **不拆**（见 D1——测试便利，main.cpp 断言段零改动）

### D2 平台句柄注入 — b（GPT 修订全采纳）
**✅ `PlatformRenderContext` 抽象基类 + `PlatformWindow::GetRenderContext()`（不用 dynamic_cast 于 Window 层）**：
```cpp
// Render/PlatformRenderContext.h（框架层，零 Win32）：
class PlatformRenderContext{   // 空基类——平台句柄的类型安全容器
public:
    virtual ~PlatformRenderContext() = default;
};

// Platform/Win32/Win32RenderContext.h（平台层）：
class Win32RenderContext : public PlatformRenderContext{
public:
    explicit Win32RenderContext(HWND hwnd) noexcept;
    HWND GetHandle() const noexcept;
private:
    HWND m_hwnd;
};

// PlatformWindow.h 抽象 + 方法：
virtual const PlatformRenderContext& GetRenderContext() const = 0;

// Win32PlatformWindow：持 Win32RenderContext 成员（构造时 CreateWindowExW 后绑定 hwnd）
// RenderingBackend 抽象 + 方法（默认空实现）：
virtual void Initialize(const PlatformRenderContext& context) {}
// GDIBackend override：static_cast<const Win32RenderContext&> 取句柄 → SetHwnd
//   （体系内约定转换——GDIBackend 是 Win32 后端，识别 Win32RenderContext 是"同体系内"，
//     非跨层 dynamic_cast；GPT"都是 Win32 体系内部的事情"）
```
- **GPT 修订价值**：句柄获取从"GDIBackend 参数识别"（dynamic_cast 在抽象层）→"PlatformWindow 返回"（GetRenderContext）——Window 层零识别，识别发生在 Win32 体系内部

### D3 Window 构造注入 + 工厂 — c（GPT 修订全采纳 + D1 分离适配）
**✅ 构造参数注入（默认 → 平台默认工厂）**：
```cpp
// Window 构造：+ RenderServices services = CreateDefaultRenderServices()
// （默认参数调用工厂——main.cpp 零改动）

// Render/BackendFactory.h（平台默认后端选择从 Window 移出）：
RenderServices CreateDefaultRenderServices();
// Win32 平台实现：make_unique<GDIBackend>() + make_unique<GDITextMeasurer>() 填 bundle（两个独立对象）
// 未来 Linux：OpenGLRenderer + FreeTypeTextMeasurer 填 bundle（接口分离天然支持，零改动）
```
- **工厂价值（GPT）**：平台默认后端选择从 Window 移出——Windows→GDIBackend+GDITextMeasurer，未来 Linux→OpenGLBackend
- 测试：RecordingBackend 不进 Window（断言段直接使用），无需注入；未来 Window 测试注入时传独立对象或单类双接口对象（RenderServices 按需构造）
- **Renderer 引用绑定注意**：`m_renderer(*m_renderBackend)` 必须在初始化列表绑定（决策 34 引用成员）——**成员声明顺序：m_renderBackend 在 m_renderer 之前**（初始化列表按声明顺序）

### D4 SetHwnd 过渡消除
**✅ 7.1.1 的 `SetHwnd(platform->GetHandle())` + `Win32PlatformWindow::GetHandle()` 移除**——替换为：
```
Window 构造：platform 创建 → m_renderBackend->Initialize(platform->GetRenderContext())
```
- 调用链：Window 构造（RenderServices 注入）→ 创建 PlatformWindow（含 Win32RenderContext）→ backend->Initialize(renderContext) → Renderer(*m_renderBackend)
- GetTextMeasurer() 改为返回 `*m_textMeasurer`（不再 m_backend）——测量路径与渲染路径各自独立指针

### D5 验证
- 编译 + 渲染回归（绘制/文本/IME 候选窗无变化——后端行为零变化，仅持有方式变 + 类拆分）
- grep 实证：Window.h 零 `GDIBackend` 具体类型（只含抽象接口 unique_ptr + RenderServices）；`SetHwnd` 零出现；`GetHandle` 零出现（注入链替换）；`shared_ptr` 零出现于 Window 后端成员

## 边界（7.1.4 不做）
- ❌ 不引入第二后端（OpenGL/X11——YAGNI，无消费者）
- ❌ 拆 GDIBackend 是本轮范围（用户决策 A）；**不拆 RecordingBackend**（测试便利保留——TextMeasurer.h:13 原始设计意图）
- ❌ 不改 Renderer（决策 34 已抽象，零改动）
- ❌ 不做 RecordingBackend 的 Initialize（测试类不注入平台句柄）

## 技术债记账（7.1.4 遗留）
| 债务 | 位置 | 消除时机 |
|---|---|---|
| GDITextMeasurer 与 GDIBackend 的 GetOrCreateFont/fontCache 逻辑重复（拆类后各持一份） | GDITextMeasurer.h/cpp + GDIBackend.h/cpp | 字体数量显著增长或出现第三消费者时提取 FontCache 共享工具 |
| GDIBackend::Initialize 内 static_cast\<Win32RenderContext\> | GDIBackend.cpp | 第二平台后端出现时评估 |

## 修订记录
- v1.0（2026-08-16）职责确认定稿：D1-D5。GPT 评审：D2（GetRenderContext 替代 Window 层 dynamic_cast）/ D3（CreateDefaultRenderBackend 工厂）**全采纳**；D1 **保留组合接口 + 记债**（YAGNI：分离是预测性设计；v1.x 双接口消费者是事实；GPT 妥协认可）。
- v1.1（2026-08-16，**用户决策覆盖 D1**）D1 改为**能力接口分离**："实现可能会用同一个，但不代表它们应该合在一起"——RenderingBackend/TextMeasurer 各自独立持有（shared_ptr 共享控制块解决"一个对象两个角色指针"）；RenderServices bundle 注入；工厂返回 bundle；技术债从"组合接口待拆"改为"GDIBackend 单类双实现待拆"（接口已分离，实现聚合是 GDI 事实）。
- v1.2（2026-08-16，**用户决策 A：7.1.4 拆 GDIBackend**）：
  - D1 从 shared_ptr 共享控制块改为 **unique_ptr 双成员**（拆类后无需共享所有权——两个独立对象）
  - 新增 **D1a 拆类范围**：GDIBackend 拆 → GDIBackend（渲染，保留 fontCache）+ GDITextMeasurer（测量，自带 fontCache 双份——已核实测量零 hwnd 依赖、唯一共享依赖是 fontCache）；**RecordingBackend 不拆**（已核实不注入 Window——main.cpp 断言段 `Renderer`/`PaintContext` 各取角色，测试便利保留，TextMeasurer.h:13 原始设计意图）
  - 技术债：删"单类双实现待拆"（已拆）；新增"GetOrCreateFont/fontCache 双份逻辑重复"（FontCache 提取触发条件）
  - 边界：拆 GDIBackend 入范围；不拆 RecordingBackend
