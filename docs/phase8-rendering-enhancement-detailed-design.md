# Phase 8 渲染增强（能力层）详细设计

> 状态：v1.4（2026-08-21）｜详细设计已确认，进入实现  
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅ / Phase 7.5 事件回调 ✅  
> 职责确认：phase8-rendering-enhancement-requirements.md v1.1  
> 初步设计：phase8-rendering-enhancement-preliminary.md v1.0
>
> **术语映射**：职责确认文档中的 `DrawingContext` 概念由现有代码中的 **`PaintContext`**（`Render/PaintContext.h`）承担；本文档统一使用 `PaintContext`。AlphaBlend 在本设计中定义为 DrawImage 的内部逐像素混合能力（非独立 RenderingBackend API），因此最终新增 Backend 虚函数为 **6 个**。

## 1. 目标

按职责确认 R1-R6，在 RenderingBackend 抽象接口上新增 **5 类渲染能力，共 6 个接口**（线条/圆角矩形/图像/裁剪 2 个/焦点框），通过 PaintContext → RenderCommand → Renderer → GDIBackend 完整链路落地，并用 RecordingBackend（现有测试后端）扩展无窗口单元测试。

## 2. 对初步设计的修正（4 处，需确认）

> ⚠️ 以下修正基于源码事实与职责确认原文，与初步设计 v1.0 有差异，请重点审阅。

### 修正 1：删除 PointFloat / RectFloat（复用现有 Point / Rect）

**事实**：`Core/Point.h` 与 `Core/Rect.h` 的成员**已经是 float**（决策 1/§4.2，全部公共类型 float 化）。初步设计的 `PointFloat`/`RectFloat` 是重复类型。

**修正**：新接口直接使用现有 `Point`（x/y float）与 `Rect`（x/y/width/height float）。不新建 `PointFloat.h`/`RectFloat.h`。

**连带**：职责确认 R4"渲染坐标浮点化"其实**已经达成**——现有接口（DrawRect 的 Rect、DrawText 的 Point）本来就是 float，新接口自然延续，无需渐进式改造。R4 降级为"已满足，仅文档确认"。

### 修正 2：D8 AlphaBlend 改向——去掉 SetOpacity 全局方案，改为 DrawImage 逐像素 Alpha

**事实**：

- 职责确认 R5/D8 原文："AlphaBlend 需 **32bpp+AC_SRC_ALPHA**"——AC_SRC_ALPHA 是**逐像素 Alpha 混合**标志（GDI `AlphaBlend` 的 `BLENDFUNCTION` 参数），语义是"源图像每个像素自带 alpha 通道"。
- 初步设计倾向的 `SetOpacity/ResetOpacity` 全局透明度**不需要 AC_SRC_ALPHA**（整体透明用 `SourceConstantAlpha`），两者语义不一致——初步设计偏离了职责确认原文。
- GDI 无"全局透明度"状态：要实现 SetOpacity 需离屏缓冲合成，侵入现有 DrawRect/DrawText 路径，复杂度高。
- YAGNI（skill 21）：消费方（Phase 9 主题 / CheckBox/Radio）尚未出现，全局半透明语义无法验证。

**修正**：**AlphaBlend 能力 = DrawImage 的逐像素 Alpha 混合**（32bpp premultiplied BGRA + AC_SRC_ALPHA）：

- 删除 `SetOpacity` / `ResetOpacity` 虚函数与 `AlphaBlendCommand`
- DrawImage 语义：源像素自带 premultiplied alpha（premultiplied BGRA），`AlphaBlend(AC_SRC_OVER, 255, AC_SRC_ALPHA)` 逐像素混合到目标
- 全局半透明（"对已绘制内容应用整体透明度"）推迟到 Phase 9 有真实消费需求时再设计
- **连带**：Color.a（alpha 通道）在 Phase 8 仍被 GDIBackend 忽略（ToColorRef 只取 RGB，决策 21/23）——与职责确认"不改变现有控件渲染"一致；Color.a 的消费属 Phase 9

### 修正 3：测试基建复用 RecordingBackend（不新建 MockBackend）

**事实**：项目已有 `RecordingBackend`（`Render/RecordingBackend.h/.cpp`，Phase 4 决策 12 建立）——测试后端，记录调用参数，RendererTests.cpp 已在用。职责确认说的"MockBackend"与既有基建重复。

**修正**：**扩展 RecordingBackend**（新增记录字段 + 实现新虚函数），不新建 MockBackend 类。

**硬约束**：RenderingBackend 新增 6 个**纯虚函数**后，RecordingBackend **必须实现**（否则抽象类无法实例化，编译失败）——RecordingBackend.h/.cpp 是**必改文件**，初步设计的文件清单遗漏了它。

### 修正 4：测试文件用现有 RendererTests.cpp；新头文件需加 vcxproj

**事实**：测试文件是 `src/Tests/RendererTests.cpp`（不是初步设计的 RenderingTests.cpp）；RunAllTests 已有 `RunRendererTests()` 入口——新增测试直接并入，**RunAllTests.h/.cpp 零改动**。vcxproj 列出全部头文件（64 个 ClInclude）——新建的 `Image.h` 必须加入 vcxproj。

### 修正汇总（文件清单变化）

| 初步设计                                                        | 详细设计修正                                                                 |
| ----------------------------------------------------------- | ---------------------------------------------------------------------- |
| 新建 PointFloat.h / RectFloat.h / ClipRegion.h / Image.h（4 个） | 只新建 **Image.h**（1 个）——Point/Rect 复用；ClipRegion 因只含 Rect 删除（见 §3.2）     |
| 新建 RenderingTests.cpp                                       | 用现有 RendererTests.cpp                                                  |
| 修改 8 文件                                                     | 修改 **12 文件**（+PaintContext.cpp、+RecordingBackend.h/.cpp、+ECDI.vcxproj） |
| AlphaBlendCommand / SetOpacity / ResetOpacity               | **删除**（并入 DrawImage）                                                   |

## 3. 数据结构

### 3.1 Image（新建 `Core/Image.h`）

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace ECDI {

/// @brief 已解码图像数据（Phase 8 只负责绘制；文件格式加载属未来 ImageLoader）
/// @details 值语义（可拷贝进 RenderCommand）；像素格式固定 32bpp premultiplied BGRA。
/// - premultiplied BGRA：RGB 通道已预先乘以 Alpha（AC_SRC_ALPHA 要求，否则混合色偏）
/// - width/height：非负值；width == 0 || height == 0 视为空图像，DrawImage 不产生绘制
/// - stride：每行字节数（>= width*4，4 字节对齐）
/// - pixels.size() >= stride*height；逐行读取时按 row*stride 定位（不能整体 memcpy）
/// - 行序：top-to-bottom（row 0 = 图像顶行）
struct Image
{
	int width = 0;                    ///< 像素宽度（>= 0）
	int height = 0;                   ///< 像素高度（>= 0）
	int stride = 0;                   ///< 每行字节数（>= width*4）
	std::vector<std::uint8_t> pixels; ///< premultiplied BGRA 像素数据（>= stride*height）
};

}
```

- 放 `Core/`：Image 是公共数据类型（未来 ImageLoader 产出物，Widget 层可能直接构造）
- 值语义：拷贝进 DrawImageCommand，生命周期随帧封闭
- 不校验像素数据合法性（调用方契约：width/height/stride/pixels 一致；越界是用户错误）
- DrawImage 只读取 Image，不修改其像素数据（const Image& 参数）

### 3.2 删除 ClipRegion

初步设计的 `ClipRegion { Rect rect; }` 只包了一个 Rect——按 YAGNI（skill 21：不做无消费者的抽象），**裁剪接口直接用 `Rect`**（`PushClip(const Rect&)`），不建 ClipRegion.h。未来圆角/路径裁剪出现时再引入 region 类型。

## 4. RenderCommand 新增（`Render/RenderCommand.h`）

```cpp
// 线条（死数据：两点 + 线宽 + 颜色）
struct DrawLineCommand
{
	Point start;        ///< 起点（最终坐标）
	Point end;          ///< 终点（最终坐标）
	float width;        ///< 线宽（px）
	Color color;
};

// 圆角矩形（实心填充，不描边；与 DrawRect 一致）
struct DrawRoundedRectCommand
{
	Rect rect;             ///< 外接矩形（最终坐标）
	float cornerRadius;    ///< 圆角半径（px，0 = 直角退化为矩形）
	Color color;
};

// 图像（逐像素 Alpha：源像素 premultiplied BGRA，AC_SRC_ALPHA 混合）
// 整个 Image 映射到 dest 指定区域（尺寸不同则缩放；DrawImage 只读取 Image）
struct DrawImageCommand
{
	Rect dest;    ///< 目标矩形（最终坐标）；Image 全部内容映射到此区域
	Image image;  ///< 已解码像素数据（值拷贝，生命周期随帧封闭）
};

// 裁剪（栈式：Push 压入裁剪矩形，后续命令受裁剪约束）
struct PushClipCommand
{
	Rect rect;    ///< 裁剪矩形（最终坐标，与既有裁剪区域求交）
};

// 裁剪弹出（无数据，仅标记——命令序列里 Push/Pop 必须配对）
struct PopClipCommand
{
};

// 焦点框（虚线矩形，1px 虚线）
struct DrawFocusRectCommand
{
	Rect rect;
	Color color;
};

// variant 扩展（决策 36：忘加 Renderer 重载 → std::visit 编译报错）
// RenderCommand 分两类：
// - 绘制命令（Draw*）：DrawRect / DrawText / DrawLine / DrawRoundedRect / DrawImage / DrawFocusRect
// - 状态命令（State）：PushClip / PopClip —— 不产生像素绘制，改变后续命令的有效裁剪状态
using RenderCommand = std::variant<DrawRectCommand, DrawTextCommand,
                                   DrawLineCommand, DrawRoundedRectCommand,
                                   DrawImageCommand, PushClipCommand,
                                   PopClipCommand, DrawFocusRectCommand>;
```

## 5. RenderingBackend 新增虚函数（`Render/RenderingBackend.h`）

```cpp
/// @brief 绘制一条线段（最终坐标，线宽 px，不透明色）
/// @details width 表示线宽；GDIBackend 当前将其量化为至少 1 像素的整数线宽
///           （float → GDI integer 是 Backend 实现细节，非 Framework 契约）
virtual void DrawLine(const Point& start, const Point& end,
                      float width, const Color& color) = 0;

/// @brief 绘制实心圆角矩形（最终坐标；cornerRadius=0 退化为直角；不描边）
/// @param rect 外接矩形
/// @param cornerRadius 圆角半径（px；clamp 到 [0, min(rect.width, rect.height) / 2]）
virtual void DrawRoundedRect(const Rect& rect, float cornerRadius,
                             const Color& color) = 0;

/// @brief 绘制图像（最终矩形；源像素 premultiplied BGRA，逐像素 AC_SRC_ALPHA 混合）
/// @details 整个 Image 映射到 dest 指定区域（尺寸不同则缩放）；
///           DrawImage 只读取 Image，不修改其像素数据（const Image&）
/// @param dest 目标矩形（最终坐标）
/// @param image 已解码像素数据（只读，绘制期间有效；值拷贝生命周期随帧封闭）
virtual void DrawImage(const Rect& dest, const Image& image) = 0;

/// @brief 压入裁剪矩形（与当前裁剪区域求交；嵌套安全）
/// @param rect 裁剪矩形（最终坐标）
virtual void PushClip(const Rect& rect) = 0;

/// @brief 弹出裁剪区域（与 PushClip 配对，恢复上一级裁剪）
virtual void PopClip() = 0;

/// @brief 绘制焦点框（1px 虚线矩形，键盘焦点反馈；GDIBackend 用 CreatePen(PS_DOT)+Rectangle 实现）
virtual void DrawFocusRect(const Rect& rect, const Color& color) = 0;
```

- 参数全部平台无关类型（Point/Rect/float/Color/Image）——skill 15 分层，零 GDI 类型泄漏
- `DrawFocusRect` 的 color 参数：契约语义"用指定颜色画虚线矩形"（比系统焦点色更灵活，供 Phase 9 主题注入）
- DrawLine/DrawRoundedRect 是**不透明**绘制（Color.a 忽略，决策 21/23）——能力层的透明度只有 DrawImage 逐像素一种

## 6. PaintContext 新增方法（`Render/PaintContext.h` + `.cpp`）

声明（头）：

```cpp
void DrawLine(const Point& start, const Point& end, float width, const Color& color);
void DrawRoundedRect(const Rect& rect, float cornerRadius, const Color& color);
void DrawImage(const Rect& dest, const Image& image);
void PushClip(const Rect& rect);
void PopClip();
void DrawFocusRect(const Rect& rect, const Color& color);
```

实现（cpp，决策 37 emplace_back 风格，与现有 DrawRect 一致）：

```cpp
void PaintContext::DrawLine(const Point& start, const Point& end,
                            float width, const Color& color)
{
	m_commands.emplace_back(DrawLineCommand{ start, end, width, color });
}

void PaintContext::DrawRoundedRect(const Rect& rect, float cornerRadius,
                                   const Color& color)
{
	m_commands.emplace_back(DrawRoundedRectCommand{ rect, cornerRadius, color });
}

void PaintContext::DrawImage(const Rect& dest, const Image& image)
{
	m_commands.emplace_back(DrawImageCommand{ dest, image });   // Image 值拷贝进命令
}

void PaintContext::PushClip(const Rect& rect)
{
	m_commands.emplace_back(PushClipCommand{ rect });
}

void PaintContext::PopClip()
{
	m_commands.emplace_back(PopClipCommand{});
}

void PaintContext::DrawFocusRect(const Rect& rect, const Color& color)
{
	m_commands.emplace_back(DrawFocusRectCommand{ rect, color });
}
```

## 7. Renderer 新增重载（`Render/Renderer.h` + `.cpp`）

头：私有区新增 6 个重载声明（与现有 DrawRectCommand/DrawTextCommand 并列）。

cpp（展开转发，不向 Backend 泄漏命令类型——决策 36/现有模式）：

```cpp
void Renderer::ExecuteCommand(const DrawLineCommand& cmd)
{
	m_backend.DrawLine(cmd.start, cmd.end, cmd.width, cmd.color);
}

void Renderer::ExecuteCommand(const DrawRoundedRectCommand& cmd)
{
	m_backend.DrawRoundedRect(cmd.rect, cmd.cornerRadius, cmd.color);
}

void Renderer::ExecuteCommand(const DrawImageCommand& cmd)
{
	m_backend.DrawImage(cmd.dest, cmd.image);
}

void Renderer::ExecuteCommand(const PushClipCommand& cmd)
{
	m_backend.PushClip(cmd.rect);
}

void Renderer::ExecuteCommand(const PopClipCommand& cmd)
{
	m_backend.PopClip();
}

void Renderer::ExecuteCommand(const DrawFocusRectCommand& cmd)
{
	m_backend.DrawFocusRect(cmd.rect, cmd.color);
}
```

## 8. GDIBackend 实现（`Render/GDIBackend.h` + `.cpp`）

### 8.1 平台 API 选型（全部原生 GDI / msimg32，不引入 GDI+）

| 能力              | API                                                               | 备注                                                                        |
| --------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------- |
| DrawLine        | `CreatePen` + `MoveToEx` + `LineTo`                               | PS_SOLID；宽 1px 以上                                                         |
| DrawRoundedRect | `CreateSolidBrush` + `CreatePen` + `RoundRect`                    | NULL_PEN 只填充（与 DrawRect 填充语义一致）；椭圆宽高 = cornerRadius*2                     |
| DrawImage       | `CreateDIBSection` + `CreateCompatibleDC` + `AlphaBlend`(msimg32) | 32bpp premultiplied BGRA；BI_RGB top-down；AC_SRC_OVER + 255 + AC_SRC_ALPHA |
| PushClip        | `SaveDC` + `IntersectClipRect`                                    | 在 m_memoryDC 上操作                                                          |
| PopClip         | `RestoreDC`                                                       | 用 Push 时保存的栈 id                                                           |
| DrawFocusRect   | `CreatePen`(PS_DOT) + `Rectangle`                                 | NULL_BRUSH 空心；PS_DOT 仅 1px 有效（GDI 限制，focus rect 即 1px）                    |

### 8.2 新增内部状态（GDIBackend.h）

```cpp
std::vector<int> m_clipStack;   ///< PushClip 保存的 SaveDC 返回值栈（PopClip 取栈顶 RestoreDC）
```

- SaveDC/RestoreDC 天然栈式（GDI 保存/恢复 DC 状态），m_clipStack 记录每级 SaveDC 返回值供精确 RestoreDC
- **配对纪律**：PushClip 未配对的 PopClip = 用户错误（命令序列失衡）——GDIBackend 防御：栈空时 PopClip 直接 return（不崩溃）

### 8.3 实现伪代码

```cpp
void GDIBackend::DrawLine(const Point& start, const Point& end,
                          float width, const Color& color)
{
	// 决策 24 风格：GDI 对象每帧创建/销毁（无缓存，无 10,000 上限风险）
	// 所有浮点坐标/线宽统一 lround 转 GDI 整数（避免截断导致位置偏移）
	// 线宽下限 1px（GDI width=0 实际=1px cosmetic pen；Phase 8 不做亚像素线宽）
	const int penWidth = std::max(1L, std::lround(width));
	HPEN pen = CreatePen(PS_SOLID, penWidth, ToColorRef(color));
	if (!pen) return;
	HGDIOBJ oldPen = SelectObject(m_memoryDC, pen);
	const auto ix1 = static_cast<LONG>(std::lround(start.x));
	const auto iy1 = static_cast<LONG>(std::lround(start.y));
	const auto ix2 = static_cast<LONG>(std::lround(end.x));
	const auto iy2 = static_cast<LONG>(std::lround(end.y));
	MoveToEx(m_memoryDC, ix1, iy1, nullptr);
	LineTo(m_memoryDC, ix2, iy2);
	SelectObject(m_memoryDC, oldPen);   // 严格还原（GDI 铁律）
	DeleteObject(pen);
}

void GDIBackend::DrawRoundedRect(const Rect& rect, float cornerRadius,
                                 const Color& color)
{
	// 空矩形 no-op（引用 Rect 宽高非负契约；负/零尺寸不绘制）
	if (rect.width <= 0.0f || rect.height <= 0.0f) return;

	// 填充语义：NULL_PEN（不画边框）+ 纯色画刷 → RoundRect
	HBRUSH brush = CreateSolidBrush(ToColorRef(color));
	if (!brush) return;
	HGDIOBJ oldBrush = SelectObject(m_memoryDC, brush);
	HGDIOBJ oldPen = SelectObject(m_memoryDC, GetStockObject(NULL_PEN));

	RECT rc{ static_cast<LONG>(std::lround(rect.x)), static_cast<LONG>(std::lround(rect.y)),
	         static_cast<LONG>(std::lround(rect.x + rect.width)), static_cast<LONG>(std::lround(rect.y + rect.height)) };
	// cornerRadius clamp 到 [0, min(width,height)/2]；避免 GDI 未定义行为
	const float maxRadius = std::min(rect.width, rect.height) * 0.5f;
	const float radius = std::clamp(cornerRadius, 0.0f, maxRadius);
	const LONG ellipse = static_cast<LONG>(std::lround(radius * 2.0f));
	RoundRect(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom, ellipse, ellipse);

	SelectObject(m_memoryDC, oldPen);
	SelectObject(m_memoryDC, oldBrush);
	DeleteObject(brush);
}

void GDIBackend::DrawImage(const Rect& dest, const Image& image)
{
	// 空图像 no-op：width == 0 || height == 0 → 不绘制（契约层确定边界）
	if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) return;

	// === 实现链：Image → 临时 32bpp DIB → 临时 DC → AlphaBlend 到目标 DC ===

	// 1. 创建临时 top-down 32bpp DIB（负 biHeight = 行序与 Image 一致，无需翻转）
	BITMAPINFO bi{};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = image.width;
	bi.bmiHeader.biHeight = -image.height;          // top-down（与 Image.pixels 行序一致）
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;

	void* dibBits = nullptr;
	HBITMAP dib = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &dibBits, nullptr, 0);
	if (!dib || !dibBits) { if (dib) DeleteObject(dib); return; }

	// 2. 像素数据 → DIB（逐行拷贝，处理 stride 对齐差异）
	const int dibStride = ((image.width * 4) + 3) & ~3;
	const auto* src = image.pixels.data();
	auto* dst = static_cast<std::uint8_t*>(dibBits);
	for (int row = 0; row < image.height; ++row)
	{
		std::memcpy(dst + row * dibStride, src + row * image.stride,
		            static_cast<size_t>(image.width) * 4u);
	}

	// 3. 临时 DC 选择 DIB → AlphaBlend 到目标 DC（逐像素 premultiplied alpha；拉伸到 dest）
	HDC srcDC = CreateCompatibleDC(m_memoryDC);
	if (srcDC)
	{
		HGDIOBJ oldDib = SelectObject(srcDC, dib);
		BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };   // AC_SRC_ALPHA = 逐像素 premultiplied alpha
		const auto dx = static_cast<LONG>(std::lround(dest.x));
		const auto dy = static_cast<LONG>(std::lround(dest.y));
		const auto dw = static_cast<LONG>(std::lround(dest.width));
		const auto dh = static_cast<LONG>(std::lround(dest.height));
		AlphaBlend(m_memoryDC, dx, dy, dw, dh,
		           srcDC, 0, 0, image.width, image.height, bf);
		SelectObject(srcDC, oldDib);
		DeleteDC(srcDC);
	}
	DeleteObject(dib);
}

void GDIBackend::PushClip(const Rect& rect)
{
	const int savedId = SaveDC(m_memoryDC);
	if (savedId == 0) return;   // SaveDC 失败（理论上不出现）
	m_clipStack.push_back(savedId);
	RECT rc{ static_cast<LONG>(std::lround(rect.x)), static_cast<LONG>(std::lround(rect.y)),
	         static_cast<LONG>(std::lround(rect.x + rect.width)), static_cast<LONG>(std::lround(rect.y + rect.height)) };
	IntersectClipRect(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom);
}

void GDIBackend::PopClip()
{
	if (m_clipStack.empty()) return;   // 防御：命令序列失衡（用户错误）不崩溃
	const int savedId = m_clipStack.back();
	m_clipStack.pop_back();
	if (savedId != 0)   // SaveDC 失败时未压入 0；防御性检查
		RestoreDC(m_memoryDC, savedId);
}

void GDIBackend::DrawFocusRect(const Rect& rect, const Color& color)
{
	// PS_DOT 虚线（仅 1px 有效）+ NULL_BRUSH 空心矩形
	HPEN pen = CreatePen(PS_DOT, 1, ToColorRef(color));
	if (!pen) return;
	HGDIOBJ oldPen = SelectObject(m_memoryDC, pen);
	HGDIOBJ oldBrush = SelectObject(m_memoryDC, GetStockObject(NULL_BRUSH));
	SetBkMode(m_memoryDC, TRANSPARENT);   // 虚线空隙透明（否则白底填充空隙）

	RECT rc{ static_cast<LONG>(std::lround(rect.x)), static_cast<LONG>(std::lround(rect.y)),
	         static_cast<LONG>(std::lround(rect.x + rect.width)), static_cast<LONG>(std::lround(rect.y + rect.height)) };
	Rectangle(m_memoryDC, rc.left, rc.top, rc.right, rc.bottom);

	SelectObject(m_memoryDC, oldBrush);
	SelectObject(m_memoryDC, oldPen);
	DeleteObject(pen);
}
```

### 8.4 资源纪律

- 所有 GDI 对象（HPEN/HBRUSH/HBITMAP/DC）**每次调用创建、用完即删**（决策 24 风格）——无缓存 → 无 10,000 GDI 对象上限风险
- DrawImage 每次创建/销毁 DIB + 临时 DC：每帧少量图像调用可接受；未来需要性能时再引入缓存（YAGNI）
- 严格逆序还原（SelectObject 还原旧对象后再 DeleteObject）——GDI 铁律，与现有 ReleaseBackBuffer 同纪律
- **msimg32 链接**：`AlphaBlend` 在 msimg32.lib——**vcxproj `<AdditionalDependencies>` 显式链接**（平台构建依赖属 Windows Backend；GDIBackend 是唯一接触 GDI 的层，不污染 Framework API）

## 9. RecordingBackend 扩展（`Render/RecordingBackend.h` + `.cpp`）

> ⚠️ 必改（编译硬约束）：RenderingBackend 新增 6 个纯虚函数，RecordingBackend 不实现则抽象类无法实例化。
>
> - 记录字段：5 个（Push/Pop 共享 `clipOps`，通过 `isPush` 区分顺序；其余能力各 1 个）

新增记录字段（头）：

```cpp
struct LineDraw     { Point start; Point end; float width; Color color; };
struct RoundedRect  { Rect rect; float cornerRadius; Color color; };
struct ImageDraw    { Rect dest; Image image; };
struct ClipOp       { Rect rect; bool isPush; };      // 记录 Push/Pop 序列（含顺序）
struct FocusRect    { Rect rect; Color color; };

std::vector<LineDraw>    lines;
std::vector<RoundedRect> roundedRects;
std::vector<ImageDraw>   imageDraws;
std::vector<ClipOp>      clipOps;
std::vector<FocusRect>   focusRects;
```

实现（cpp，与 DrawRect 同模式——emplace_back 记录）：

```cpp
void RecordingBackend::DrawLine(const Point& start, const Point& end,
                                float width, const Color& color)
{
	lines.push_back({ start, end, width, color });
}

void RecordingBackend::DrawRoundedRect(const Rect& rect, float cornerRadius,
                                       const Color& color)
{
	roundedRects.push_back({ rect, cornerRadius, color });
}

void RecordingBackend::DrawImage(const Rect& dest, const Image& image)
{
	imageDraws.push_back({ dest, image });
}

void RecordingBackend::PushClip(const Rect& rect)
{
	clipOps.push_back({ rect, true });
}

void RecordingBackend::PopClip()
{
	clipOps.push_back({ {}, false });
}

void RecordingBackend::DrawFocusRect(const Rect& rect, const Color& color)
{
	focusRects.push_back({ rect, color });
}
```

## 10. 测试用例（`src/Tests/RendererTests.cpp` 扩展）

沿用现有 FloatEq/kEpsilon 辅助，新增三个测试函数，并入 `RunRendererTests()`（**RunAllTests 零改动**）。

### 10.1 TestRendererNewCommands（命令 → Renderer → RecordingBackend 转发）

```cpp
void TestRendererNewCommands()
{
	RecordingBackend backend;
	Renderer renderer(backend);

	CommandBuffer commands;
	commands.emplace_back(DrawLineCommand{ Point{ 1, 2 }, Point{ 30, 40 }, 2.0f, Color::Red() });
	commands.emplace_back(DrawRoundedRectCommand{ Rect{ 0, 0, 100, 50 }, 8.0f, Color::Gray() });
	commands.emplace_back(PushClipCommand{ Rect{ 10, 10, 20, 20 } });
	commands.emplace_back(PopClipCommand{});
	commands.emplace_back(DrawFocusRectCommand{ Rect{ 0, 0, 10, 10 }, Color::Black() });

	Image img;
	img.width = 2; img.height = 1; img.stride = 8;
	img.pixels = { 0, 0, 0, 255, 255, 255, 255, 255 };   // 2x1 premultiplied BGRA
	commands.emplace_back(DrawImageCommand{ Rect{ 5, 5, 10, 10 }, img });

	renderer.Execute(commands);

	FRAMEWORK_ASSERT(backend.lines.size() == 1);
	FRAMEWORK_ASSERT(FloatEq(backend.lines[0].start.x, 1.0f) && FloatEq(backend.lines[0].end.y, 40.0f));
	FRAMEWORK_ASSERT(FloatEq(backend.lines[0].width, 2.0f));
	FRAMEWORK_ASSERT(FloatEq(backend.lines[0].color.r, 1.0f));

	FRAMEWORK_ASSERT(backend.roundedRects.size() == 1);
	FRAMEWORK_ASSERT(FloatEq(backend.roundedRects[0].cornerRadius, 8.0f));
	FRAMEWORK_ASSERT(FloatEq(backend.roundedRects[0].rect.width, 100.0f));

	FRAMEWORK_ASSERT(backend.clipOps.size() == 2);
	FRAMEWORK_ASSERT(backend.clipOps[0].isPush && FloatEq(backend.clipOps[0].rect.x, 10.0f));
	FRAMEWORK_ASSERT(!backend.clipOps[1].isPush);   // Pop 无 rect 语义

	FRAMEWORK_ASSERT(backend.focusRects.size() == 1);
	FRAMEWORK_ASSERT(FloatEq(backend.focusRects[0].rect.height, 10.0f));

	FRAMEWORK_ASSERT(backend.imageDraws.size() == 1);
	FRAMEWORK_ASSERT(backend.imageDraws[0].image.width == 2);
	FRAMEWORK_ASSERT(backend.imageDraws[0].image.pixels.size() == 8);
	FRAMEWORK_ASSERT(FloatEq(backend.imageDraws[0].dest.x, 5.0f));
	FRAMEWORK_ASSERT(FloatEq(backend.imageDraws[0].dest.width, 10.0f));
}
```

### 10.2 TestPaintContextNewCommands（PaintContext → 命令生成）

```cpp
void TestPaintContextNewCommands()
{
	RecordingBackend backend;   // 只借 TextMeasurer 接口（MeasureText 固定值）
	CommandBuffer commands;
	PaintContext ctx(commands, backend);

	ctx.DrawLine(Point{ 0, 0 }, Point{ 100, 100 }, 1.0f, Color::Red());
	ctx.DrawRoundedRect(Rect{ 5, 5, 50, 50 }, 4.0f, Color::Blue());
	ctx.PushClip(Rect{ 1, 1, 2, 2 });
	ctx.PopClip();
	ctx.DrawFocusRect(Rect{ 0, 0, 8, 8 }, Color::Black());

	Image img; img.width = 1; img.height = 1; img.stride = 4;
	img.pixels = { 0, 0, 0, 255 };
	ctx.DrawImage(Rect{ 2, 2, 4, 4 }, img);

	FRAMEWORK_ASSERT(commands.size() == 6);
	FRAMEWORK_ASSERT(std::holds_alternative<DrawLineCommand>(commands[0]));
	FRAMEWORK_ASSERT(std::holds_alternative<DrawRoundedRectCommand>(commands[1]));
	FRAMEWORK_ASSERT(std::holds_alternative<PushClipCommand>(commands[2]));
	FRAMEWORK_ASSERT(std::holds_alternative<PopClipCommand>(commands[3]));
	FRAMEWORK_ASSERT(std::holds_alternative<DrawFocusRectCommand>(commands[4]));
	FRAMEWORK_ASSERT(std::holds_alternative<DrawImageCommand>(commands[5]));

	const auto& line = std::get<DrawLineCommand>(commands[0]);
	FRAMEWORK_ASSERT(FloatEq(line.width, 1.0f) && FloatEq(line.end.x, 100.0f));

	const auto& rounded = std::get<DrawRoundedRectCommand>(commands[1]);
	FRAMEWORK_ASSERT(FloatEq(rounded.cornerRadius, 4.0f));

	const auto& image = std::get<DrawImageCommand>(commands[5]);
	FRAMEWORK_ASSERT(image.image.pixels.size() == 4);
}
```

### 10.3 TestImageValueSemantic（Image 值语义验证：Command 持独立副本）

```cpp
void TestImageValueSemantic()
{
	Image img;
	img.width = 2; img.height = 1; img.stride = 8;
	img.pixels = { 10, 20, 30, 255, 40, 50, 60, 255 };

	DrawImageCommand cmd{ Rect{ 0, 0, 2, 1 }, img };

	// 修改原始 Image
	img.pixels[0] = 255;
	img.pixels[3] = 0;

	// Command 内的 Image 不受影响（值拷贝独立）
	FRAMEWORK_ASSERT(cmd.image.pixels[0] == 10);
	FRAMEWORK_ASSERT(cmd.image.pixels[3] == 255);
	FRAMEWORK_ASSERT(cmd.image.pixels.size() == 8);
}
```

### 10.4 入口

```cpp
void ECDI::Test::RunRendererTests()
{
	TestRendererForwarding();
	TestUTF8Utility();
	TestRendererNewCommands();      // 新增
	TestPaintContextNewCommands();  // 新增
	TestImageValueSemantic();       // 新增
}
```

### 10.5 静态自查对照（AI 侧）

- DrawLine 参数：start/end/width/color 与命令字段一一对应（`FloatEq` 比较 float，避免 == 直比）
- ClipOp 序列：Push 在前 Pop 在后，顺序由 commands 顺序保证
- Image 断言：width/height/stride/pixels 拷贝进命令后原样保留（值语义）
- TestImageValueSemantic：修改原始 img 后 cmd.image 不变（值拷贝独立）
- 测试只验证**参数传递**（7.2 边界），不验证像素/视觉

## 11. 文件改动清单（13 个）

| #  | 文件                                            | 类型     | 内容                                                                                 |
| -- | --------------------------------------------- | ------ | ---------------------------------------------------------------------------------- |
| 1  | `ECDI/include/ECDI/Core/Image.h`              | **新建** | Image 数据结构（32bpp premultiplied BGRA）                                               |
| 2  | `ECDI/include/ECDI/Render/RenderCommand.h`    | 修改     | 新增 6 Command + variant 扩展                                                          |
| 3  | `ECDI/include/ECDI/Render/RenderingBackend.h` | 修改     | 新增 6 虚函数（DrawLine/DrawRoundedRect/DrawImage/PushClip/PopClip/DrawFocusRect）        |
| 4  | `ECDI/include/ECDI/Render/PaintContext.h`     | 修改     | 新增 6 方法声明                                                                          |
| 5  | `ECDI/src/Render/PaintContext.cpp`            | 修改     | 新增 6 方法实现（emplace_back）                                                            |
| 6  | `ECDI/include/ECDI/Render/Renderer.h`         | 修改     | 新增 6 ExecuteCommand 重载声明                                                           |
| 7  | `ECDI/src/Render/Renderer.cpp`                | 修改     | 新增 6 重载实现（转发）                                                                      |
| 8  | `ECDI/include/ECDI/Render/GDIBackend.h`       | 修改     | 新增 6 override + m_clipStack                                                        |
| 9  | `ECDI/src/Render/GDIBackend.cpp`              | 修改     | 实现 6 能力（GDI/msimg32）                                                               |
| 10 | `ECDI/include/ECDI/Render/RecordingBackend.h` | 修改     | 新增 6 override + 5 记录字段                                                             |
| 11 | `ECDI/src/Render/RecordingBackend.cpp`        | 修改     | 新增 6 记录实现                                                                          |
| 12 | `ECDI/src/Tests/RendererTests.cpp`            | 修改     | 新增 TestRendererNewCommands/TestPaintContextNewCommands/TestImageValueSemantic + 入口 |
| 13 | `ECDI/ECDI.vcxproj`                           | 修改     | 加 `Include\ECDI\Core\Image.h` + `msimg32.lib`                                      |

**明确不修改**：RunAllTests.h/.cpp（入口已有 RunRendererTests）、main.cpp（skill 第 2 条）、Widget 层（Phase 8 无控件消费）。

## 12. 实现顺序

1. `Image.h`（新建，BOM 验证）
2. `RenderCommand.h`（6 Command + variant——加完先停，std::visit 编译错即验证）
3. `RenderingBackend.h`（6 虚函数）
4. `PaintContext.h/.cpp`
5. `Renderer.h/.cpp`
6. `RecordingBackend.h/.cpp`（编译硬约束：此时纯虚函数全实现，项目恢复可编译）
7. `GDIBackend.h/.cpp`
8. `RendererTests.cpp`
9. `ECDI.vcxproj`（Image.h + msimg32.lib）
10. 用户 VS 编译 + 运行验证（skill 第 1 条）

## 13. 风险与边界

| 项                            | 说明                                                                                                          |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------- |
| msimg32 链接                   | `AlphaBlend` 需 msimg32.lib——vcxproj `<AdditionalDependencies>` 显式链接                                         |
| Image 值拷贝                    | DrawImageCommand 持 Image 值（vector 拷贝）——大图像每帧拷贝有成本；Phase 8 接受（YAGNI），未来可改 `shared_ptr<const Image>`          |
| 裁剪配对                         | Push/Pop 必须配对（命令序列契约）；GDIBackend 栈空 Pop 防御 return；失衡序列是用户错误                                                 |
| 像素格式                         | Image 固定 32bpp premultiplied BGRA（AC_SRC_ALPHA 要求，否则混合色偏）——其他格式（RGB24/灰度）Phase 8 不支持                        |
| DrawLine 线宽                  | `width` 量化为 `max(1, lround(width))`；float → GDI integer 是 Backend 实现细节，非 Framework 契约                       |
| DrawRoundedRect cornerRadius | clamp 到 `[0, min(rect.width, rect.height)/2]`；避免 GDI 未定义行为                                                  |
| DrawImage 缩放                 | 整个 Image 映射到 dest Rect，尺寸不同则拉伸（AlphaBlend 拉伸）；DrawImage 只读取 Image，不修改像素数据                                   |
| SaveDC 失败                    | `SaveDC() == 0` 时不入栈、不执行对应 RestoreDC；与栈空 Pop 防御同模型                                                          |
| 透明度边界                        | Phase 8 只有 DrawImage 逐像素 Alpha（AC_SRC_ALPHA）；DrawLine/RoundedRect/FocusRect 不透明（Color.a 忽略）——全局半透明归 Phase 9 |
| D9 边界                        | CheckBox/Radio 不实现（职责确认 D9）；能力就绪，控件消费是独立阶段                                                                  |

## 14. 与既有约束的对齐

| 约束                 | 对齐方式                                                                                                    |
| ------------------ | ------------------------------------------------------------------------------------------------------- |
| skill 8 BOM        | 新建 Image.h 带 UTF-8 BOM 并验证；修改文件 Edit 保留原 BOM                                                            |
| skill 10 宏防护       | GDIBackend.h 已有（Windows.h 前 #undef DrawText）；RenderingBackend.h 已有；无新 Windows.h 引入（GDIBackend.cpp 经头传递） |
| skill 11 UTF-8     | 新接口零 wchar_t/文本参数                                                                                       |
| skill 12 namespace | 新代码全在 `namespace ECDI` 内                                                                                |
| skill 14 禁复制禁移动    | Image 值语义可拷贝（非资源类）；GDIBackend 仍 unique_ptr 持有                                                           |
| skill 15 分层        | Widget 经 PaintContext；GDIBackend 唯一接触 GDI；接口零平台类型                                                       |
| skill 18 死数据       | 6 新 Command 纯数据；Backend 不认识命令体系（Renderer 展开转发）                                                          |
| skill 19 能力/决策正交   | Phase 8 只加能力（怎么画）；主题参数（颜色/圆角值）Phase 9 注入                                                                |
| skill 21 YAGNI     | 删 ClipRegion/PointFloat/RectFloat/SetOpacity；不做亚像素/多格式/缓存                                               |
| skill 22 分层论证      | 接口契约用"最终坐标/逐像素 Alpha/栈式裁剪"语言，不引 GDI 细节                                                                  |
| GDI+ 限制            | 不引入 GDI+；全用 GDI/msimg32                                                                                 |
| 原子授权               | 13 文件全部授权后再改（skill 3）                                                                                   |
| 测试由用户做             | 测试写好后用户 VS 编译 + 运行验证（skill 第 1 条）                                                                       |
| 五阶段法               | 本文档 = 详细设计；确认后进实现                                                                                       |

## 15. 修订记录

- **v1.4（2026-08-21）** — GPT 准入审查后最终小修（4 点，架构不变；详细设计已确认，进入实现）
  1. **Image 空图像 no-op**：`width == 0 || height == 0` → DrawImage 不产生绘制（契约层确定边界）
  2. **Image stride 逐行读取明确**：契约补充"逐行读取时按 row*stride 定位（不能整体 memcpy）"
  3. **DrawRoundedRect 空 Rect no-op**：`rect.width <= 0 || rect.height <= 0` → 直接 return
  4. **测试函数数量修正**：文档"新增两个测试函数" → "新增三个测试函数"
  - 状态变更为 **详细设计已确认，进入实现**；文件清单不变：新建 1（Image.h）+ 修改 12（13 个文件）
- **v1.3（2026-08-21）** — GPT 评审后小修正（6 点修正，架构不变）
  1. **术语映射明确**：职责确认中的 `DrawingContext` 由现有代码 `PaintContext` 承担；AlphaBlend 定义为 DrawImage 内部能力（非独立 API），新增 Backend 虚函数为 6 个
  2. **Image width/height 非负契约**：明确 `width >= 0, height >= 0`
  3. **RenderCommand 分类**：明确 PushClip/PopClip 是"状态命令"（不产生像素绘制，改变后续命令的有效裁剪状态）
  4. **DrawLine 契约精确化**：`width` 量化为 `max(1, lround(width))`；float → GDI integer 是 Backend 实现细节，非 Framework 契约
  5. **msimg32 链接定案**：vcxproj `<AdditionalDependencies>` 显式链接（消除待确认项）
  6. **"6 类能力" → "5 类能力 / 6 个接口"**：裁剪 = PushClip + PopClip（2 个接口），其余各 1 个
  - 文件清单不变：新建 1（Image.h）+ 修改 12（13 个文件）
- **v1.2（2026-08-21）** — GPT 评审后小修订（6 点修正，架构不变）
  1. **DrawLine 线宽下限**：`max(1, lround(width))`；width < 1px 时 clamp 到 1px（GDI width=0 实际=1px cosmetic pen）
  2. **SaveDC 失败防御**：`SaveDC() == 0` 时不入栈；PopClip 前检查 `savedId != 0`
  3. **DrawRoundedRect cornerRadius clamp**：`[0, min(rect.width, rect.height)/2]`；避免 GDI 未定义行为
  4. **DrawImage 改为 Rect dest**：整个 Image 映射到 dest Rect（尺寸不同则拉伸）；明确 DrawImage 只读取 Image
  5. **API 依赖描述修正**：`GDI / msimg32 / User32` → `GDI / msimg32`（实际未使用 User32）
  6. **RecordingBackend 字段说明**：明确记录字段为 5 个（Push/Pop 共享 clipOps）
  - 文件清单不变：新建 1（Image.h）+ 修改 12（13 个文件）
- **v1.1（2026-08-21）** — GPT 评审后小修订（9 点修正，架构不变）
  1. **Image 明确 premultiplied BGRA**：RGB 通道已预乘 Alpha（AC_SRC_ALPHA 硬性要求，否则混合色偏）
  2. **stride 契约**：明确 `>= width*4`
  3. **pixels 契约**：明确 `pixels.size() >= stride*height`
  4. **行序明确 top-to-bottom**：row 0 = 图像顶行
  5. **DrawLine 坐标统一 lround**：所有浮点坐标（start/end）+ 线宽统一 `std::lround`，避免截断偏移（原 static_cast 是取整不是四舍五入）
  6. **DrawRoundedRect 明确实心填充**：不描边，语义明确
  7. **DrawFocusRect 实现描述统一**：`CreatePen(PS_DOT) + Rectangle`（非系统 DrawFocusRect API）
  8. **DrawImage 伪代码注释**：强调目标 DC + 源 DIB/DC + AlphaBlend 核心链路
  9. **新增 TestImageValueSemantic**：验证 Command 持 Image 独立副本（修改原始 img 后 cmd.image 不变）
  - 测试函数数量：2 → 3（TestRendererNewCommands / TestPaintContextNewCommands / TestImageValueSemantic）
  - 文件清单不变：新建 1（Image.h）+ 修改 12（13 个文件）
- v1.0（2026-08-21）详细设计初稿
  - 修正初步设计 4 处：① 删 PointFloat/RectFloat（现有 Point/Rect 已是 float）② D8 AlphaBlend 改向（SetOpacity 全局 → DrawImage 逐像素 AC_SRC_ALPHA，与职责确认原文一致）③ 测试基建复用 RecordingBackend（纯虚函数新增后必须实现）④ 测试文件用 RendererTests.cpp + vcxproj 加 Image.h
  - 文件清单：新建 1（Image.h）+ 修改 12（13 个文件）

