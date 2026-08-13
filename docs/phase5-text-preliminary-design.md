# Phase5 文本系统初步设计（phase5-text-preliminary-design.md）

> 前置：`docs/phase5-text-requirements.md` v1.0（职责确认，D1-D9）
> 本文档：接口草稿 + 架构决策（路线 X 定案）+ 影响面
> 修订记录：v1.0（2026-08-12）

## 1. 架构定案：路线 X（2026-08-12 拍板）

**文本测量是 5.1 的基础设施**（不是 TextBox 专属）——Label 自动尺寸、Button 文字居中、TextBox 光标定位都依赖它。

```
Font（纯描述，平台无关）
  ↓
TextMeasurer（抽象能力接口，Render 层）
  ↑
GDIBackend / D2D 实现（平台层：字体实例化 + 测量 + 资源管理）
  ↑
PaintContext 注入 TextMeasurer& → Widget 在 Paint 阶段可测量
```

**分层铁律**：
- Font 保持**纯字体描述**——不持有 HFONT、不自带测量、无 pimpl（P1 撤回）
- TextMeasurer 是**独立能力接口**（不继承 RenderingBackend，实现可以共用平台层）
- TextMeasurer **不接触 Widget**（只知道 Font + text → Size）
- HFONT 创建/缓存/生命周期 = **平台实现内部细节**（P6 暂缓，不上升公共架构决策）

## 2. 类型与接口草稿

### 2.1 Size（✅ P9 已确认：建）

```cpp
struct Size {
    float width = 0.0f;
    float height = 0.0f;
};
```

几何三元组 Point / Rect / Size 的补齐；`MeasureText` 返回值；未来 `GetPreferredSize()` 用。新增类型，不重开 4.1 的既有决策。

### 2.2 Font（纯描述，零平台资源）

```cpp
struct Font {
    float size = 14.0f;       // 字号（像素）
    std::string family;       // 字体族；空串 = 系统默认（内部表达，用户无需理解）
};
```

纯数据、可值拷贝进命令；无 HFONT、无方法（测量在 TextMeasurer）。

### 2.3 TextMeasurer（抽象能力接口）

```cpp
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual Size MeasureText(const Font& font, const std::string& text) = 0;
    virtual float LineHeight(const Font& font) = 0;
};
```

只知道 Font + text → Size/行高；不接触 Widget/PaintContext/RenderCommand。

### 2.4 DrawTextCommand（命令扩展，死数据）

```cpp
struct DrawTextCommand {
    Point pos;             // 起点（对齐偏移由控件用 Measure 算好，D9）
    std::string text;      // UTF-8（D1）
    Color color;           // 前景色（P8，背景透明由控件先画）
    Font font;
};

using RenderCommand = std::variant<DrawRectCommand, DrawTextCommand>;   // 决策 3 扩展
```

### 2.5 PaintContext 扩展（决策 42 的构造扩展）

```cpp
class PaintContext {
public:
    PaintContext(CommandBuffer& commands, TextMeasurer& measurer);   // 构造加第二参数
    void DrawRect(const Rect& rect, const Color& color);
    void DrawText(const Point& pos, const std::string& text,
                  const Color& color, const Font& font = Font());    // P4：font 可省略
    Size MeasureText(const Font& font, const std::string& text);     // 转发 m_measurer
private:
    CommandBuffer& m_commands;
    TextMeasurer& m_measurer;
};
```

### 2.6 RenderingBackend 扩展（操作粒度不变）

```cpp
class RenderingBackend {
    virtual void DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font) = 0;   // 与 DrawRect 风格一致
};
```

### 2.7 GDIBackend（平台实现）

- 实现 `RenderingBackend::DrawText` + `TextMeasurer` 双接口
- 内部：`Font → HFONT` 缓存（键 = family+size）；绘制时 SelectObject + `SetBkMode(TRANSPARENT)` + `SetTextColor` + `TextOutW`（**UTF-8→UTF-16 转换封在此**，P5）
- 测量：`GetTextExtentPoint32W`（宽度）/ `GetTextMetrics`（行高，P7 精确值）
- HFONT 生命周期/缓存策略：平台实现内部解决（P6）

## 3. 决策记录

| # | 决策 | 结论 |
|---|------|------|
| P1 | Font 测量归属 | ✅ **路线 X**：Font 纯描述 + TextMeasurer 独立接口 + GDIBackend 实现 + PaintContext 注入（GPT 边界修正采纳） |
| P2 | Font 默认构造 | ✅ `Font()` = 框架默认；`""` 是内部表达 |
| P3 | DrawTextCommand | ✅ 起点 + 死数据（不带对齐/区域） |
| P4 | PaintContext::DrawText | ✅ font 默认参数（默认 ≠ 只有默认字体） |
| P5 | GDI 绘制 | ✅ TRANSPARENT + SetTextColor + TextOutW；UTF-8→UTF-16 封平台实现 |
| P6 | HFONT 生命周期 | ✅ 暂缓——平台实现内部细节，不做 pimpl/共享/缓存抽象 |
| P7 | 垂直居中 | ✅ 行高来自 TextMeasurer::LineHeight（精确，非字号估算） |
| P8 | 文本颜色 | ✅ 命令只前景色，背景透明 |
| P9 | Size 类型 | ✅ 已确认：建（几何三元组 Point/Rect/Size） |

## 4. 影响面（已有代码改动）

| 文件 | 改动 |
|------|------|
| `Render/PaintContext.h/.cpp` | 构造加 `TextMeasurer&`；加 DrawText/MeasureText |
| `Render/RenderCommand.h` | variant 加 DrawTextCommand |
| `Render/Renderer.h/.cpp` | ExecuteCommand 加 `DrawTextCommand` 重载（决策 36 穷尽性验证点） |
| `Render/RenderingBackend.h` | 加纯虚 DrawText |
| `Render/GDIBackend.h/.cpp` | 实现 DrawText + TextMeasurer（内部 HFONT 缓存） |
| `Render/RecordingBackend.h/.cpp` | 实现 DrawText（记录）+ TextMeasurer（测试返回固定尺寸） |
| `Window/Window.cpp` | `PaintFrame` 里 `PaintContext ctx(m_commands, m_backend)`（GDIBackend 兼 TextMeasurer） |
| `main.cpp` | 4.6 测试段构造 PaintContext 加 measurer 参数 |

## 5. 待详细设计

- GDIBackend 内部 Font→HFONT 缓存的具体形态（map 键、清理时机）
- RecordingBackend 的 TextMeasurer 测试策略（固定尺寸 vs 记录调用）
- TextOutW 的 UTF-16 转换细节（String.cpp 工具复用）
- DrawTextCommand 的 Renderer 转发（ExecuteCommand 重载）
- 5.2 Label 的文本绘制 + 自动尺寸（消费 5.1）

## 6. 修订记录

- v1.0（2026-08-12）：初步设计——路线 X 定案（P1）+ P2-P9 全部确认（P9 Size 类型建，几何三元组）
