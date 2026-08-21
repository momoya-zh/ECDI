# Phase 8 渲染增强（能力层）初步设计

> 状态：v1.0（2026-08-20）｜初步设计待审
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅ / Phase 7.5 事件回调 ✅
> 职责确认：phase8-rendering-enhancement-requirements.md v1.1

## 1. 设计目标

在 RenderingBackend 抽象接口上新增 7 类渲染能力，通过 PaintContext 转发给具体后端（GDIBackend）。新接口保持**平台无关**，仅暴露能力契约（"能画什么"），不引用任何 GDI/WIN32 类型。

## 2. 架构概览

```text
Widget::OnPaint(PaintContext& ctx)
        ↓
PaintContext (命令收集门面，值语义)
        ↓
RenderCommand (死数据 variant)
        ↓
Renderer::ExecuteCommand (转发到 Backend)
        ↓
RenderingBackend (抽象接口)
        ↓
GDIBackend (GDI / msimg32 / DIB 实现)
```

新增能力严格遵循现有分层：
- **PaintContext** 新增绘制方法（如 DrawLine）→ 生成对应的 **RenderCommand**
- **Renderer** 新增 ExecuteCommand 重载 → 转发到 Backend
- **RenderingBackend** 新增虚函数 → GDIBackend 实现
- **GDIBackend** 使用平台原生 API（GDI / msimg32 / WIC）完成绘制

## 3. 新增数据结构

### 3.1 Image（图像数据容器）

```cpp
/// @brief 已解码的图像数据（Phase 8 只负责绘制，文件格式加载属于 ImageLoader）
struct Image {
    int width = 0;
    int height = 0;
    int stride = 0;          ///< 每行字节数（像素对齐）
    int channels = 4;        ///< RGBA 4 通道（目前固定 32bpp）
    std::vector<uint8_t> pixels; ///< 像素数据（BGRA 顺序，与 Windows DIB 一致）
};
```

### 3.2 ClipRegion（裁剪区域）

```cpp
/// @brief 裁剪区域描述符（栈式 PushClip/PopClip）
struct ClipRegion {
    Rect rect;               ///< 裁剪矩形（最终坐标）
    // Phase 8 只支持矩形裁剪；未来可扩展路径/圆角裁剪
};
```

### 3.3 渲染坐标浮点化

现有接口坐标参数保持 int（向后兼容），新接口统一使用 float：

```cpp
struct PointFloat { float x; float y; };
struct RectFloat { float x; float y; float w; float h; };
```

## 4. 新增 RenderCommand

每个新能力对应一个 Command 结构体（死数据，值语义）：

```cpp
// 4.1 线条
struct DrawLineCommand {
    PointFloat start;
    PointFloat end;
    float width;
    Color color;
};

// 4.2 圆角矩形
struct DrawRoundedRectCommand {
    RectFloat rect;
    float cornerRadius;
    Color color;
};

// 4.3 图像
struct DrawImageCommand {
    PointFloat dest;          ///< 目标左上角
    Image image;              ///< 已解码图像数据（拷贝进命令，生命周期封闭）
    // Phase 8 不支持源矩形裁剪、缩放、Alpha（简单起见）
};

// 4.4 透明混合（独立 API 暂定，初步设计 D8 待确认）
struct AlphaBlendCommand {
    // 三种模式之一（设计评审时确定）
    // 模式 A：整体透明度（对已绘制内容应用）
    // 模式 B：绘制操作透明度（DrawRect/DawImage 带 alpha 参数）
    // 模式 C：临时缓冲区混合
    // 倾向模式 A：与 GDI AlphaBlend 语义一致
    float opacity;            ///< 0.0 完全透明，1.0 完全不透明
};

// 4.5 裁剪区域（栈操作）
struct PushClipCommand {
    ClipRegion region;
};

struct PopClipCommand {};    // 无数据，仅标记

// 4.6 焦点框（虚线矩形）
struct DrawFocusRectCommand {
    RectFloat rect;
    Color color;
};
```

## 5. RenderingBackend 新增虚函数

```cpp
class RenderingBackend {
public:
    // ... 现有虚函数 ...

    // 5.1 线条（Phase 8 新增）
    virtual void DrawLine(PointFloat start, PointFloat end,
                         float width, Color color) = 0;

    // 5.2 圆角矩形
    virtual void DrawRoundedRect(RectFloat rect, float cornerRadius,
                                Color color) = 0;

    // 5.3 图像绘制（已解码像素数据）
    virtual void DrawImage(PointFloat dest, const Image& image) = 0;

    // 5.4 透明混合（Phase 8 暂提供独立接口，D8 架构待评审）
    virtual void SetOpacity(float opacity) = 0;  // 设置当前帧全局透明度
    virtual void ResetOpacity() = 0;             // 恢复不透明

    // 5.5 裁剪区域（栈式）
    virtual void PushClip(ClipRegion region) = 0;
    virtual void PopClip() = 0;

    // 5.6 焦点框（虚线矩形）
    virtual void DrawFocusRect(RectFloat rect, Color color) = 0;
};
```

**注意**：所有新虚函数的参数均为平台无关类型（float/int/string），不引入 HDC/HBRUSH 等。

## 6. PaintContext 新增绘制方法

```cpp
class PaintContext {
public:
    // ... 现有方法 ...

    // 6.1 线条
    void DrawLine(PointFloat start, PointFloat end,
                  float width, Color color);

    // 6.2 圆角矩形
    void DrawRoundedRect(RectFloat rect, float cornerRadius, Color color);

    // 6.3 图像
    void DrawImage(PointFloat dest, const Image& image);

    // 6.4 透明度（全局）
    void SetOpacity(float opacity);
    void ResetOpacity();

    // 6.5 裁剪（栈式）
    void PushClip(ClipRegion region);
    void PopClip();

    // 6.6 焦点框
    void DrawFocusRect(RectFloat rect, Color color);

private:
    CommandBuffer& m_commands;
    TextMeasurer& m_measurer;
};
```

## 7. Renderer 新增 ExecuteCommand 重载

```cpp
class Renderer {
private:
    // 现有重载
    void ExecuteCommand(const DrawRectCommand& cmd);
    void ExecuteCommand(const DrawTextCommand& cmd);

    // 新增重载（Phase 8）
    void ExecuteCommand(const DrawLineCommand& cmd);
    void ExecuteCommand(const DrawRoundedRectCommand& cmd);
    void ExecuteCommand(const DrawImageCommand& cmd);
    void ExecuteCommand(const AlphaBlendCommand& cmd);
    void ExecuteCommand(const PushClipCommand& cmd);
    void ExecuteCommand(const PopClipCommand& cmd);
    void ExecuteCommand(const DrawFocusRectCommand& cmd);
};
```

## 8. GDIBackend 实现要点

### 8.1 平台 API 选型

| 能力 | GDI / 系统组件 | 备注 |
|---|---|---|
| DrawLine | `MoveToEx` + `LineTo` + `HPEN` | 需创建/选择画笔 |
| DrawRoundedRect | `RoundRect` | GDI 原生支持 |
| DrawImage | `CreateDIBSection` + `BitBlt` + `AlphaBlend`（msimg32） | 需处理 32bpp BGRA |
| AlphaBlend | `AlphaBlend`（msimg32） | 需 32bpp 源 DC |
| PushClip/PopClip | `SaveDC` + `IntersectClipRect` + `RestoreDC` | 栈式保存/恢复 |
| DrawFocusRect | `DrawFocusRect`（User32） | 专用 API，虚线矩形 |

### 8.2 资源管理

- **HPEN**：每次 DrawLine 创建，用完释放（或缓存同参数画笔）
- **HBITMAP**：DrawImage 需要为每个 Image 创建 DIB（或缓存相同 Image）
- **HRGN**：PushClip 临时创建，PopClip 释放
- **GDI+**：**不引入**——所有能力用纯 GDI / msimg32 / User32 实现

### 8.3 AlphaBlend 实现细节

```cpp
void GDIBackend::SetOpacity(float opacity) {
    // 保存当前全局透明度，后续绘制操作使用
    m_globalOpacity = opacity;
}

void GDIBackend::DrawImage(PointFloat dest, const Image& image) {
    // 1. 创建兼容 DC + DIBSection（32bpp BGRA）
    // 2. 复制像素数据到 DIBSection
    // 3. 调用 msimg32::AlphaBlend（AC_SRC_ALPHA + opacity）
    // 4. 释放临时 DC/DIB
}
```

## 9. 测试策略（MockBackend）

延续 Phase 7.2 无窗口测试体系，为每个新能力编写 MockBackend 单元测试：

```cpp
class MockBackend : public RenderingBackend {
public:
    // 记录调用参数
    struct LineCall { PointFloat start; PointFloat end; float width; Color color; };
    std::vector<LineCall> lineCalls;

    void DrawLine(PointFloat start, PointFloat end, float width, Color color) override {
        lineCalls.push_back({start, end, width, color});
    }
    // ... 其他 Mock 方法 ...
};

// 测试用例
void TestPaintContextDrawLine() {
    CommandBuffer commands;
    MockMeasurer measurer;
    PaintContext ctx(commands, measurer);

    ctx.DrawLine({0,0}, {100,100}, 2.0f, Red());
    // 验证命令生成
    ASSERT(commands.size() == 1);
    auto& cmd = std::get<DrawLineCommand>(commands[0]);
    ASSERT(cmd.start.x == 0.0f);
    ASSERT(cmd.end.x == 100.0f);
    ASSERT(cmd.width == 2.0f);
    ASSERT(cmd.color == Red());
}
```

**测试覆盖**：
- 每个 RenderCommand 的生成正确性
- PaintContext 方法参数传递
- 裁剪栈 Push/Pop 配对（MockBackend 验证调用序列）
- 透明度设置/重置

## 10. 文件改动清单

| 文件 | 改动类型 | 内容 |
|---|---|---|
| `ECDI/include/ECDI/Core/Image.h` | **新建** | Image 数据结构 |
| `ECDI/include/ECDI/Core/ClipRegion.h` | **新建** | ClipRegion 数据结构 |
| `ECDI/include/ECDI/Core/PointFloat.h` | **新建** | PointFloat 浮点坐标 |
| `ECDI/include/ECDI/Core/RectFloat.h` | **新建** | RectFloat 浮点矩形 |
| `ECDI/include/ECDI/Render/RenderCommand.h` | **修改** | 新增 7 个 Command 结构体 + variant 扩展 |
| `ECDI/include/ECDI/Render/RenderingBackend.h` | **修改** | 新增 7 个虚函数 |
| `ECDI/include/ECDI/Render/PaintContext.h` | **修改** | 新增 7 个绘制方法 |
| `ECDI/include/ECDI/Render/Renderer.h` | **修改** | 新增 7 个 ExecuteCommand 重载 |
| `ECDI/src/Render/Renderer.cpp` | **修改** | 实现新重载（转发） |
| `ECDI/include/ECDI/Render/GDIBackend.h` | **修改** | 新增 7 个 override 声明 |
| `ECDI/src/Render/GDIBackend.cpp` | **修改** | 实现新能力（GDI/msimg32） |
| `ECDI/src/Tests/RenderingTests.cpp` | **修改** | 新增 MockBackend 单元测试 |

## 11. 实现顺序

1. **新建数据结构**（Image/ClipRegion/PointFloat/RectFloat）
2. **扩展 RenderCommand**（新增 Command + variant 扩展）
3. **扩展 RenderingBackend**（新增虚函数）
4. **扩展 PaintContext**（新增绘制方法，生成命令）
5. **扩展 Renderer**（新增 ExecuteCommand 重载，转发）
6. **实现 GDIBackend**（逐个能力实现，用 GDI/msimg32）
7. **编写 MockBackend 测试**（验证参数传递）
8. **用户编译验证**（skill 第 1 条）

## 12. 风险与待定

### D8 架构决策（需评审确认）

AlphaBlend 是**独立全局操作**（`SetOpacity/ResetOpacity`）还是**绘制操作的附加参数**（`DrawRect(..., alpha)`）？

- **方案 A（倾向）**：独立全局操作——与 GDI AlphaBlend 语义一致，实现简单
- **方案 B**：绘制操作参数——更灵活，但每个 Command 需加 alpha 字段，改动大

**建议**：Phase 8 先实现方案 A（独立全局操作），观察实际需求后再考虑方案 B。

### Image 资源模型

Image 值语义拷贝进 Command（`DrawImageCommand` 持有 Image 副本）。简单但可能低效（大图像多次拷贝）。Phase 8 保持简单，未来可改为 `std::shared_ptr<const Image>`。

### 裁剪区域嵌套

PushClip/PopClip 由 GDIBackend 内部栈管理（`SaveDC/RestoreDC`）。RenderingBackend 不维护状态，每个命令独立执行。需要确保命令顺序正确（Push/Pop 配对）。

## 13. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 8 新文件 BOM | 新建 4 个数据结构文件均带 UTF-8 BOM |
| skill 11 UTF-8 | 所有文本参数保持 `std::string`；Image 像素为 `uint8_t` |
| skill 12 namespace | 新代码在 `namespace ECDI` 内 |
| skill 14 资源类禁复制禁移动 | Image 值语义可复制（不是资源类）；DrawingContext 保持值语义 |
| skill 15 分层 | Widget 通过 PaintContext 调用；GDIBackend 是唯一接触 GDI 的层 |
| skill 18 RenderCommand | 新 Command 是死数据；Renderer 通过 std::visit 转发；Backend 不认识命令体系 |
| skill 19 能力层/决策层正交 | Phase 8 只实现能力（怎么画）；Phase 9 定义主题（画成什么样） |
| skill 21 YAGNI | 不做 JPEG/GIF/高 DPI/全面 float；裁剪区域只支持矩形 |
| skill 22 分层论证 | 接口设计用契约语言，不引用 GDI 实现细节 |
| GDI+ 限制 | GDI+ 不引入；所有实现用纯 GDI/msimg32/User32 |
| 原子授权 | 12 文件全部授权后再改（skill 3） |
| 测试由用户做 | MockBackend 测试写好后，用户编译验证（skill 第 1 条） |
| 五阶段法 | 本文档 = 初步设计；确认后进详细设计 |

## 14. 修订记录

- v1.0（2026-08-20）初步设计初稿（职责确认 v1.1 后的接口设计）

