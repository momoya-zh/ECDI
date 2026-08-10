# Phase 4 Renderer 实现设计（phase4-renderer-implementation.md）

> 前置：`docs/phase4-renderer-design.md` v1.7（42 条决策 + §20 Header 规则 + §21 实现顺序）
> 本文档：落地实现蓝图——文件树 / 类定义 / 成员 / 签名 / Commit 修改范围 / 验证方式
> 修订记录：v1.0 初版（2026-08-10）

---

## 1. 目标与原则

承接 design v1.7，按**修订后顺序**（§21 + GPT 评审修正：双缓冲并入 GDIBackend 一次到位）落代码。

原则：
- 小步提交，**每个 commit 可编译**
- 测试随 commit（不单独列测试 commit）
- 平台相关（GDI）最后接，先用 RecordingBackend 验证 命令→Renderer→Backend 链路
- 不再插入大型重构（namespace / 字符串边界已清完）

## 2. 文件树

### 新增

```
ECDI/include/ECDI/
├── Core/
│   ├── Rect.h        （float，纯聚合）
│   ├── Point.h       （float，纯聚合）
│   └── Color.h       （float RGBA + 标准色 + FromRGBA8，全 inline）
└── Render/           （新目录）
    ├── RenderCommand.h    （DrawRectCommand + variant + CommandBuffer）
    ├── PaintContext.h     （class + DrawRect，决策 42）
    ├── Renderer.h         （持 RenderingBackend&，决策 34）
    ├── RenderingBackend.h （纯抽象接口）
    ├── GDIBackend.h       （双缓冲，决策 11/14/15/35/38）
    └── RecordingBackend.h （测试实现）
ECDI/src/Render/
    ├── PaintContext.cpp
    ├── Renderer.cpp
    ├── GDIBackend.cpp
    └── RecordingBackend.cpp
```

Core 三个类型**纯数据、全 inline 头文件实现**（无 .cpp）；Render 层需要 .cpp。

### 修改 / 删除

| 文件 | 动作 | Commit |
|------|------|--------|
| `Widget/Geometry.h` | **删除**（m_geometry 改用 Rect(float)，决策 1） | 4.1 |
| `Widget/Widget.h/.cpp` | Geometry→Rect 迁移 + Paint 签名迁移 + 删 HDC 桥梁 | 4.1 / 4.6 |
| `Widget/Panel.h/.cpp`、`Button.h/.cpp` | OnPaint 改 PaintContext 版本 | 4.6 |
| `Window/Window.h/.cpp` | 加 GDIBackend/Renderer/CommandBuffer 成员 + PaintFrame 编排 | 4.7 |
| `ECDI/ECDI.vcxproj` | 文件条目增删（Geometry 删 / Rect·Point·Color·Render 加） | 随各 commit |
| `CMakeLists.txt` | src 为 GLOB_RECURSE 自动包含（新 .cpp 需 rerun cmake）；include 目录已含 | 随各 commit |

## 3. 类定义与接口（照抄 design §18，供直接编码）

### 3.1 Core 类型（决策 1/22/23，全 inline）

```cpp
// Rect.h —— float 数值类型（决策 1/§4.2）
struct Rect {
    float x = 0.f, y = 0.f;
    float width = 0.f, height = 0.f;
};

// Point.h
struct Point {
    float x = 0.f, y = 0.f;
};

// Color.h —— 决策 21/22/23：语义约定 [0,1]，纯数据零约束，Backend 转换时 Clamp
struct Color {
    float r, g, b, a;   // RGBA float（公共 API 不暴露 Win32 类型）

    static Color White()   { return { 1.0f, 1.0f, 1.0f, 1.0f }; }
    static Color Black()   { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    static Color Red()     { return { 1.0f, 0.0f, 0.0f, 1.0f }; }
    static Color Green()   { return { 0.0f, 1.0f, 0.0f, 1.0f }; }
    static Color Blue()    { return { 0.0f, 0.0f, 1.0f, 1.0f }; }
    static Color Gray()    { return { 0.5f, 0.5f, 0.5f, 1.0f }; }
    static Color FromRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
};
```

### 3.2 RenderCommand（决策 2/3/4/37）

```cpp
// RenderCommand.h
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Color.h"
#include <variant>
#include <vector>

namespace ECDI {

struct DrawRectCommand {          // 死数据（决策 2/37）
    Rect rect;
    Color color;
};

using RenderCommand = std::variant<DrawRectCommand>;   // 决策 3
using CommandBuffer = std::vector<RenderCommand>;      // 决策 4

}
```

### 3.3 PaintContext（决策 5/7/8/42）

```cpp
// PaintContext.h —— 收集门面，完全封装（决策 8：无 GetCommands）
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Render/RenderCommand.h"

namespace ECDI {

class PaintContext {
public:
    explicit PaintContext(CommandBuffer& commands);   // 决策 42：私有成员
    void DrawRect(const Rect& rect, const Color& color);   // 内部 emplace_back(DrawRectCommand{...})，决策 37

private:
    CommandBuffer& m_commands;
};

}
```

### 3.4 RenderingBackend（决策 11/14，操作粒度接口）

```cpp
// RenderingBackend.h —— §20 示例，零依赖
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/Color.h"

namespace ECDI {

class RenderingBackend {
public:
    virtual ~RenderingBackend() = default;
    virtual void BeginFrame() = 0;
    virtual void DrawRect(const Rect& rect, const Color& color) = 0;
    virtual void EndFrame() = 0;
};

}
```

### 3.5 Renderer（决策 9/13/34/36）

```cpp
// Renderer.h —— 不 include RenderingBackend.h，前置声明（§20）
namespace ECDI {

class RenderingBackend;

class Renderer {
public:
    explicit Renderer(RenderingBackend& backend);   // 决策 34，定义在 .cpp
    void BeginFrame();                               // 决策 13：直接转发
    void Execute(const CommandBuffer& commands);     // 决策 36：std::visit
    void EndFrame();                                 // 决策 13：直接转发

private:
    void ExecuteCommand(const DrawRectCommand& cmd); // 决策 9：重载集
    RenderingBackend& m_backend;                     // 决策 34：引用，不拥有
};

}
```

```cpp
// Renderer.cpp
void Renderer::Execute(const CommandBuffer& commands) {
    for (const auto& command : commands) {
        std::visit([this](const auto& cmd) { ExecuteCommand(cmd); }, command);
    }
}
void Renderer::ExecuteCommand(const DrawRectCommand& cmd) {
    m_backend.DrawRect(cmd.rect, cmd.color);   // 决策 21：转换封闭在 GDIBackend
}
```

### 3.6 GDIBackend（决策 11/14/15/16/17/18/19/20/21/24/25/26/27/29/30/31/32/33/35/38）

```cpp
// GDIBackend.h —— Window 值成员（§20：Window.h 必须 include 完整类型）
namespace ECDI {

class GDIBackend : public RenderingBackend {
public:
    GDIBackend();                              // 决策 35：默认构造（hwnd 暂空）
    ~GDIBackend() override;                    // 决策 18/31：ReleaseBackBuffer
    void SetHwnd(HWND hwnd);                   // 决策 35：Window 构造体内调用

    void BeginFrame() override;                // 决策 16 清屏白 + 决策 17 BeginPaint + 决策 15/26 EnsureBackBuffer
    void DrawRect(const Rect&, const Color&) override;   // 决策 21-25
    void EndFrame() override;                  // 决策 17 EndPaint + 决策 27/29 GetClientRect + BitBlt

private:
    void EnsureBackBuffer();                   // 决策 15/19/26/38：懒创建 + 尺寸自检 + 先建后替
    void ReleaseBackBuffer();                  // 决策 20/31：SelectObject(old)→DeleteObject→DeleteDC

    HWND m_hwnd = nullptr;
    PAINTSTRUCT m_ps{};                        // 决策 17：帧状态
    HDC m_windowDC = nullptr;                  // BeginPaint 返回值，EndFrame BitBlt 目标
    HDC m_memoryDC = nullptr;
    HBITMAP m_bitmap = nullptr;
    HBITMAP m_oldBitmap = nullptr;             // 决策 20：SelectObject 返回值
    int m_bitmapWidth = 0, m_bitmapHeight = 0; // 尺寸自检（决策 26）
    bool m_inFrame = false;                    // 决策 32：Begin/End 配对断言
};

}
```

实现要点（全部来自 design §19）：
- BeginFrame：`FRAMEWORK_ASSERT(!m_inFrame)` → BeginPaint → GetClientRect（**不用 rcPaint**，决策 27）→ EnsureBackBuffer → FillRect 白（决策 16）
- EnsureBackBuffer：懒创建 + 尺寸自检重建，**先创建新资源 → 全部成功 → 替换成员 → 释放旧**（决策 38 事务性）
- DrawRect：`ToByte` Clamp（决策 21/23）→ COLORREF → CreateSolidBrush（失败 `if(!brush) return`，决策 30）→ FillRect → DeleteObject（决策 24）
- EndFrame：BitBlt 完整客户区 SRCCOPY（决策 29）→ EndPaint → `m_inFrame = false`
- 析构：直接调 ReleaseBackBuffer（决策 31），空句柄防御

### 3.7 RecordingBackend（决策 12，测试实现）

```cpp
// RecordingBackend.h —— draws 公开是测试实现的刻意选择（决策 12）
namespace ECDI {

class RecordingBackend : public RenderingBackend {
public:
    struct DrawCall { Rect rect; Color color; };
    std::vector<DrawCall> draws;               // 公开：测试直接断言

    void BeginFrame() override;                // 可选：记录清屏（决策 16 白底行为测试）
    void DrawRect(const Rect& rect, const Color& color) override;  // draws.emplace_back({rect, color})
    void EndFrame() override;
};

}
```

## 4. Commit 修改范围与验证（修订后顺序 4.1 - 4.7）

### Commit 4.1 Core 类型 + Geometry 迁移（决策 1）

**新增**：`Core/Rect.h`、`Core/Point.h`、`Core/Color.h`
**修改**：
- `Widget.h`：include `"Geometry.h"` → `"ECDI/Core/Rect.h"`；成员 `Geometry m_geometry` → `Rect m_geometry`（float）；`GetGeometry()` 返回 `const Rect&`；`SetPosition/SetSize` 内部 int→float 存；`GetX/GetY/GetWidth/GetHeight` 内部 float→int 返（公开 API 保持 int 签名）
- `Widget.cpp`：3 处 `m_geometry.x/y/width/height` 的 int 语义适配（float 成员需 cast）：
  - `ContainsPoint`：`x < m_geometry.width`（int vs float，提升比较 OK）
  - `HitTest`：`const int localX = x - static_cast<int>(child->m_geometry.x);`（float→int 显式 cast）
  - `Paint`：`int x = offsetX + static_cast<int>(m_geometry.x);`
**删除**：`Widget/Geometry.h`（vcxproj ClInclude 移除）
**验证**：编译 + main.exe 窗口正常显示（绘制逻辑未动，决策 1"Layout/HitTest/main.cpp 零改动"——Layout 走公共 API 已确认）

### Commit 4.2 RenderingBackend 接口

**新增**：`Render/RenderingBackend.h`（§20 示例，零依赖）
**验证**：编译

### Commit 4.3 RenderCommand

**新增**：`Render/RenderCommand.h`（DrawRectCommand + variant + CommandBuffer）
**验证**：编译

### Commit 4.4 PaintContext

**新增**：`Render/PaintContext.h/.cpp`（决策 42：class + 私有 m_commands + emplace_back）
**验证**：编译（4.5 起随 Renderer 测试覆盖，不在本 commit 单独测）

### Commit 4.5 Renderer + RecordingBackend（核心验证点）

**新增**：`Render/Renderer.h/.cpp`、`Render/RecordingBackend.h/.cpp`
**验证（第二层测试，不碰 GDI）**：main.cpp 临时测试段（或独立测试函数）：
```cpp
ECDI::RecordingBackend backend;
ECDI::Renderer renderer(backend);
ECDI::CommandBuffer commands;
commands.emplace_back(ECDI::DrawRectCommand{ ECDI::Rect{0,0,100,100}, ECDI::Color::Red() });
renderer.Execute(commands);
// 断言 backend.draws.size() == 1 且内容匹配（决策 12：栈对象直接观察）
```
**窗口仍正常**（此阶段未动 Widget 绘制——Widget 还画 HDC）

### Commit 4.6 Widget Paint 迁移（决策 6，⚠️ 窗口暂白是预期中间态）

**修改**：
- `Widget.h/.cpp`：`Paint(HDC, int, int)` → `Paint(PaintContext&, int offsetX, int offsetY)`（决策 6 **必须保留 offset**）；`OnPaint(HDC, int, int)` → `OnPaint(PaintContext&, int x, int y)`；**删除 HDC 临时桥梁**（`struct HDC__; using HDC = HDC__*;`，§13 迁移项）；include PaintContext.h
- `Panel.h/.cpp`：`OnPaint` → `ctx.DrawRect(Rect{float(x), float(y), float(GetWidth()), float(GetHeight())}, Color::Gray())`
- `Button.h/.cpp`：`OnPaint` → `ctx.DrawRect(..., Color::Blue())`（RGB(80,120,220) 的近似）
- `Window.cpp`：WM_PAINT 特判里 `m_rootWidget->Paint(hdc,0,0)` 签名已变——**临时方案**：绘制部分先注释（窗口空白），4.7 接入 Renderer 后恢复。⚠️ 注意 OnPaint 不能用 GetRect()（§19-1，最终坐标 x/y + GetWidth/GetHeight）
**验证**：编译 + 第一层测试（Widget→PaintContext→CommandBuffer 断言：构造 Panel/Button，Paint 到 buffer，检查 DrawRectCommand 的 Rect/Color/顺序）

### Commit 4.7 GDIBackend + Window 接入（窗口恢复）

**新增**：`Render/GDIBackend.h/.cpp`（§3.6 全套）
**修改**：
- `Window.h`：include `GDIBackend.h` + `Renderer.h` + `RenderCommand.h`（§20 值成员必须完整类型）；成员声明顺序 **`GDIBackend m_backend;` 在 `Renderer m_renderer;` 前**（决策 35，初始化按声明序、析构逆序）；加 `CommandBuffer m_commands;`（决策 4 Window 持有复用）；加私有 `void PaintFrame();`（决策 41 改名）
- `Window.cpp`：
  - 构造：初始化列表 `m_renderer(m_backend)`（决策 35）；CreateWindowExW 成功检查后 `m_backend.SetHwnd(m_handle)`（决策 35）
  - `WindowProc`：删除 Phase 3 WM_PAINT 特判（决策 39/§13）→ 统一走 `HandleMessage`
  - `HandleMessage`：加 `case WM_PAINT: PaintFrame(); return 0;`（决策 39，**不进翻译器**，与 WM_DESTROY/WM_SIZE 并列）
  - `PaintFrame()` 实现（决策 10/13/33 严格配对）：
```cpp
void Window::PaintFrame() {
    m_commands.clear();                                  // 决策 4 复用
    PaintContext ctx(m_commands);
    m_rootWidget->Paint(ctx, 0, 0);                      // 根从 (0,0)，决策 6
    m_renderer.BeginFrame();                             // 转发（决策 13）
    m_renderer.Execute(m_commands);
    m_renderer.EndFrame();
}
```
**验证**：窗口显示恢复（Root 白 / Panel 灰 / Button 蓝）+ 无闪烁（双缓冲）+ 三工具链编译

## 5. 关键实现细节（design §19 抄录，实现必看）

1. **OnPaint 不能用 GetRect()**：`GetRect()` 返回局部坐标，必须用基类传入的最终坐标 x/y + `GetWidth()/GetHeight()`（4.6 最易错）
2. **Paint 必须保留 offset 参数**：禁止"PaintContext 存偏移"或"回溯 parent 链"
3. **值成员必须完整类型**：Window.h include GDIBackend.h/Renderer.h，不能前向声明（C2027）
4. **rcPaint 不能当 Buffer 尺寸**：GetClientRect 拿完整客户区（否则每帧重建）
5. **EnsureBackBuffer 懒创建 + 尺寸自检**：首帧/Resize/复用三合一，零通知链路
6. **RECT 开区间**：`right = x + width`（exclusive）
7. **CreateSolidBrush 失败防御**：`if (!brush) return;`
8. **GDI 资源释放**：ReleaseBackBuffer 严格逆序（SelectObject(old)→DeleteObject→DeleteDC）；HBRUSH 每次配对
9. **GDIBackend 成员清单**：见 §3.6（m_hwnd 是 SetHwnd 设置，非构造参数）
10. **OnPaint 撞名已解决**（决策 41）：Window 编排叫 `PaintFrame`，Widget 绘制虚方法叫 `OnPaint`
11. **编码**：新源文件 UTF-8（已配 /utf-8，MSVC 不再报 C4819）

## 6. 验收标准（design §17）

1. main.cpp 两窗口正常显示（灰 Panel + 蓝 Button）
2. 视觉结果与 Phase 3 基本一致（Root 白 / Panel 灰 / Button 蓝）
3. 实际使用无明显闪烁（双缓冲生效）
4. 双层测试通过（4.5 第二层 Recording 断言 + 4.6 第一层命令断言）

---

## 7. 修订记录

- v1.0（2026-08-10）初版：基于 design v1.7 + GPT 评审修正（双缓冲并入 GDIBackend，Commit 4.7 一次到位；顺序 4.1-4.7；测试随 commit）
