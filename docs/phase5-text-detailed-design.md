# Phase5 文本系统详细设计（phase5-text-detailed-design.md）

> 前置：`phase5-text-requirements.md` v1.0（职责确认 D1-D9）+ `phase5-text-preliminary-design.md` v1.0（初步设计 P1-P9）
> 本文档：完整接口/签名/实现细节 + D1-D7 详细设计决策记录
> 修订记录：v1.0（2026-08-12）

## 1. 类型与接口（最终形态，可直接编码）

### 1.1 Size（Core，新增，P9）

```cpp
struct Size {
    float width = 0.0f;
    float height = 0.0f;
};
```

几何三元组 Point / Rect / Size 补齐；`MeasureText` 返回值。

### 1.2 Font（Core，新增，纯描述——P1 撤回后的正解）

```cpp
struct Font {
    float size = 14.0f;       // 第一版语义 = GDI 像素高度（D3 约束 3）
    std::string family;       // UTF-8；空串 = 系统默认（内部表达）
};
```

纯数据、零平台资源、无方法——可值拷贝进命令。测量在 TextMeasurer，实例化在平台层。

### 1.3 TextMeasurer（Render，新增，独立能力接口——路线 X 定案）

```cpp
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual Size MeasureText(const Font& font, const std::string& text) = 0;
    virtual float LineHeight(const Font& font) = 0;
};
```

只知道 Font + text → Size/行高；不接触 Widget/PaintContext/RenderCommand；**与 RenderingBackend 无继承关系**（正交能力，D4 约束 1）。

### 1.4 DrawTextCommand（RenderCommand 扩展，死数据）

```cpp
struct DrawTextCommand {
    Point pos;             // 起点（对齐偏移由控件用 Measure 算好，D9/P3）
    std::string text;      // UTF-8（D1 职责确认）
    Color color;           // 前景色（P8）
    Font font;
};

using RenderCommand = std::variant<DrawRectCommand, DrawTextCommand>;
```

### 1.5 PaintContext 扩展（决策 42 构造扩展）

```cpp
class PaintContext {
public:
    PaintContext(CommandBuffer& commands, TextMeasurer& measurer);
    void DrawRect(const Rect& rect, const Color& color);
    void DrawText(const Point& pos, const std::string& text,
                  const Color& color, const Font& font = Font());   // P4：font 可省略
    Size MeasureText(const Font& font, const std::string& text);    // 转发 m_measurer
private:
    CommandBuffer& m_commands;
    TextMeasurer& m_measurer;
};
```

### 1.6 RenderingBackend 扩展（操作粒度不变）

```cpp
class RenderingBackend {
    virtual void DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font) = 0;   // 展开参数，与 DrawRect 风格一致
};
```

### 1.7 RecordingBackend 扩展（测试实现，双接口）

```cpp
class RecordingBackend : public RenderingBackend, public TextMeasurer {
public:
    struct DrawCall { Rect rect; Color color; };
    struct TextDraw { Point pos; std::string text; Color color; Font font; };

    std::vector<DrawCall> draws;      // 既有
    std::vector<TextDraw> textDraws;  // 新增：完整参数记录（D4 约束 2）

    void DrawText(const Point&, const std::string&, const Color&, const Font&) override;  // textDraws.emplace_back
    Size MeasureText(const Font&, const std::string&) override { return { 10.0f, 14.0f }; }  // 固定值
    float LineHeight(const Font&) override { return 14.0f; }
};
```

双接口是**测试便利**，不改变 TextMeasurer 与 RenderingBackend 的独立关系（D4 约束 1）。

### 1.8 GDIBackend 扩展（平台实现）

新增能力：实现 `RenderingBackend::DrawText` + `TextMeasurer`；内部 Font→HFONT 缓存。

## 2. 详细设计决策记录（D1-D7）

### D1 HFONT 缓存 —— ✅ A（GDIBackend 内部缓存 map）

`std::map<std::pair<float, std::string>, HFONT>` 懒创建缓存，析构统一 DeleteObject（与 ReleaseBackBuffer 并列）。
- **约束 1（设计不变量）**：缓存键必须完整覆盖 Font 语义字段——当前 `pair<float,string>`（size+family）；**未来 Font 加 weight/italic 等字段必须同步扩展缓存键**（否则 Normal/Bold 误判同一字体）
- **约束 2**：字号是用户直接输入的描述值，第一版不做浮点规范化；未来字号大量来自计算再引入标准化
- ⚠️ 与 P6 的关系：P6 说"HFONT 缓存策略不上升公共架构决策"——D1 正是"平台实现内部"的缓存，两者不冲突（GPT 总结表曾误把 D1 写成"暂缓"，已纠正）

### D2 测量 HDC 来源 —— ✅ A（临时 GetDC(NULL)，帧无关）

- Paint 阶段测量发生在 BeginFrame **之前**（决策 10 顺序），帧内 DC 不存在 → 测量用临时屏幕 DC：`GetDC(NULL)` → SelectObject(HFONT) → GetTextExtentPoint32W / GetTextMetrics → **恢复原字体** → ReleaseDC（GDI 纪律，D2 补充 3）
- **补充 1（职责边界）**：该 HDC 仅用于文本测量，不承担实际绘制职责
- **补充 2（演进路径）**：临时 DC 是第一版实现；未来性能需要可优化为 GDIBackend 内部复用的 Measure DC——**完全封闭在 GDIBackend 内**，TextMeasurer/Font/PaintContext 公共层零感知
- 不选 B（调 PaintFrame 顺序）：违背决策 10"先收集后建缓冲"语义 + 只解决 Paint 阶段测量 + BeginFrame 提前破坏配对

### D3 HFONT 创建 —— ✅ A（CreateFontIndirectW + LOGFONTW）

- LOGFONTW **零初始化**；`lfHeight = -static_cast<LONG>(std::lround(size))`（**负值 = 字符高度**，语义直白；**lround 而非截断**——`Font(14.9f)` 不该悄悄变 14）
- `lfFaceName`：family UTF-8 → `UTF8ToWide`（封闭在 GDIBackend）；空 family **不设置**（系统默认）；复制遵守 **LF_FACESIZE 长度限制 + 结尾 L'\0'**
- `lfCharSet = DEFAULT_CHARSET`（关键：中文/Unicode 正常）；`lfWeight = FW_NORMAL`；其余默认
- 创建失败返回 NULL → 绘制时跳过该文本（决策 30 风格）
- **单位语义（约束 3，最重要）**：`Font::size` 第一版按 **GDI 像素高度**解释；未来引入 DPI 再改为逻辑单位

### D4 RecordingBackend —— ✅ A（双接口 + 完整参数记录）

- `RecordingBackend : public RenderingBackend, public TextMeasurer`（测试便利，不改变两接口独立关系）
- `textDraws` 记录**完整参数**（pos/text/color/font）——既能测"画了文本"，又能验证 Renderer 原样转发
- MeasureText 固定 `Size{10,14}` / LineHeight 14.0f（测试断言"测量被调用"，不关心精度——精度是 GDIBackend 真实 GDI 的事）

### D5 Renderer 转发 —— ✅ A（ExecuteCommand 重载）

- `ExecuteCommand(const DrawTextCommand&)` → `m_backend.DrawText(cmd.pos, cmd.text, cmd.color, cmd.font)` 展开转发
- **边界**：不向 Backend 泄漏 RenderCommand 类型（不传 DrawTextCommand 本身）
- **原则兑现**：每种 RenderCommand 一个重载；新增命令类型必须同步加重载——std::visit 穷尽性让遗漏在**编译期**暴露（决策 36 承诺，5.1 首次实测）
- **语义**：命令顺序 = 绘制顺序（控件先 DrawRect 背景再 DrawText 文本 → 文本叠背景；Backend 顺序执行，禁止"排序命令"）

### D6 UTF-8 → UTF-16 —— ✅ A（复用 Core/String.h）

- `GDIBackend::DrawText` 内 `const std::wstring wideText = UTF8ToWide(text);` → `TextOutW`
- 细节 1：`TextOutW` 长度参数是 **wchar 数**（`wideText.size()`，别乘 2）
- 细节 2：坐标 `static_cast<LONG>(pos.x)` **截断**（与决策 25 统一；round/DPI 留待未来）
- 细节 3：复用既定工具，不做第二条转换路径

### D7 main.cpp 测试 —— ✅ A（改构造 + 完整文本链路测试）

- 4.6 测试段：加 `RecordingBackend measurer;` + `PaintContext ctx(commands, measurer);`（backend 按 `TextMeasurer&` 参数绑定；原断言不变——variant 扩展不影响 `get<DrawRectCommand>`）
- 新增 5.1 文本链路段（与 4.5 同级）：手动构造 `DrawTextCommand` → CommandBuffer → Renderer → RecordingBackend textDraws 断言（数量 + pos/text/color/font 原样转发）——**5.1 的第一条文本渲染回归测试链**
- 5.1 阶段无 Label 控件（5.2 才有），文本链路测试手动构造命令（与 4.5 哲学一致：先验证链路，控件消费在下一阶段）

## 3. 实现要点汇总（GDIBackend::DrawText 完整实现草图）

```cpp
void GDIBackend::DrawText(const Point& pos, const std::string& text,
                          const Color& color, const Font& font)
{
	// D6：公共层 UTF-8 → Win32 UTF-16（复用 Core/String.h，封闭在平台层）
	const std::wstring wideText = UTF8ToWide(text);
	if (wideText.empty()) return;

	// D1+D3：缓存取/建 HFONT（懒创建，键 = size+family）
	HFONT hfont = GetOrCreateFont(font);          // nullptr 则跳过（决策 30）
	if (!hfont) return;

	SelectObject(m_memoryDC, hfont);
	SetBkMode(m_memoryDC, TRANSPARENT);           // P5
	SetTextColor(m_memoryDC, ToColorRef(color));  // P8：复用 DrawRect 的 ToByte Clamp

	// D6 细节 2：坐标截断（决策 25 统一）
	TextOutW(m_memoryDC,
	         static_cast<LONG>(pos.x), static_cast<LONG>(pos.y),
	         wideText.c_str(),
	         static_cast<int>(wideText.size()));  // wchar 数，非字节
}
```

测量（D2）：`GetDC(NULL)` → SelectObject(hfont) → GetTextExtentPoint32W（MeasureText）/ GetTextMetrics（LineHeight）→ 恢复 SelectObject → ReleaseDC。

## 4. 影响面（已有代码改动清单）

| 文件 | 改动 |
|------|------|
| `Core/Size.h`（新增） | Size 类型 |
| `Core/Font.h`（新增） | Font 描述符 |
| `Render/TextMeasurer.h`（新增） | 抽象能力接口 |
| `Render/RenderCommand.h` | variant 加 DrawTextCommand |
| `Render/PaintContext.h/.cpp` | 构造加 TextMeasurer& + DrawText/MeasureText |
| `Render/Renderer.h/.cpp` | ExecuteCommand 加 DrawTextCommand 重载 |
| `Render/RenderingBackend.h` | 加纯虚 DrawText |
| `Render/GDIBackend.h/.cpp` | 实现 DrawText + TextMeasurer + HFONT 缓存 + 析构清理 |
| `Render/RecordingBackend.h/.cpp` | 实现 DrawText + TextMeasurer + textDraws |
| `Window/Window.cpp` | `PaintFrame` 里 `PaintContext ctx(m_commands, m_backend)`（GDIBackend 兼 TextMeasurer） |
| `main.cpp` | 4.6 段改构造 + 新增 5.1 文本链路断言段 |
| `ECDI.vcxproj` | 新增 3 头 0 cpp（Size/Font/TextMeasurer 全 inline；PaintContext 加方法无需新 cpp 改动注册） |

## 5. 修订记录

- v1.0（2026-08-12）：详细设计 D1-D7 全部落盘（含 GPT 约束；D1 结论纠正——A 缓存，非"暂缓"）
