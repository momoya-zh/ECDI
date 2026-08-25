# Phase 5.2 Label 职责确认 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（核心 6 项 → 范围 2 项）+ GPT 评审

## 背景

Label 是 Phase 5 的**第一个文本消费者**——验证 5.1 建好的整条文本链（`Label → OnPaint → PaintContext::DrawText → DrawTextCommand → Renderer → GDIBackend → TextOutW`）。5.1 之前 Label 无 OnPaint override（什么都不画），`m_text` 为 `std::wstring`（字符串边界暂缓项）。

## 决策记录

### L1 文本编码迁移（5.1 D8 落地）

**A：`SetText`/`GetText`/`m_text` 全改 `std::string`（UTF-8 全链路）**

```
Label(std::string) → DrawTextCommand(std::string) → GDIBackend → UTF8ToWide → TextOutW
```

维持 wstring 会出现"Label(wstring) → Command(string) → GDIBackend(wstring)"的无意义双重转换。

### L2 自动尺寸（AutoSize）——第一版不做 ✅（含架构债务记录）

**A：尺寸由 SetSize/布局决定，OnPaint 在给定区域内画文本；`GetPreferredSize()` 暂缓**

理由（不只是 YAGNI，是架构约束）：
- 5.1 路线 X 定案：**TextMeasurer 只通过 PaintContext 注入，控件在非 Paint 时刻拿不到测量能力**
- 为 AutoSize 给 Label 注入 TextMeasurer = 刚建立的"TextMeasurer 不接触 Widget"边界立即被打破
- GUI 框架正常演进顺序：手动 SetSize → GetPreferredSize → 布局系统自动——当前连 GetPreferredSize 都没有，直接 AutoSize 跨度太大

**⚠️ 已知架构债务（5.2 暴露，5.5 TextBox 前必须解决）**：
> **"测量能力的访问路径"**——TextMeasurer 只能在 Paint 阶段访问，导致：AutoSize 做不了、GetPreferredSize() 做不了、未来 TextBox 光标定位（非 Paint 时刻测宽）也会遇到同样问题。5.2 不做是对的（提前暴露是好事），但此问题记入待办，5.5 设计 TextBox 时一并解决（候选方向：布局系统注入测量器 / Window 提供测量服务，届时再定）。

### L3 绘制内容

**A：只画文本（`ctx.DrawText`），背景透明（容器色透出）**

否则 Panel(灰) + Label(白底) 会出现奇怪的背景块。背景是 Panel/Button 的职责。

### L4 文本颜色与字体

**A：提供 `SetTextColor`（默认黑）+ 字体暂缓**——**但内部预留 `Font m_font{}` 成员**（GPT 补充）

```cpp
// 内部永远成立：
ctx.DrawText(pos, m_text, m_textColor, m_font);
```

- `SetTextColor`：成本一行成员，Button 5.3 白字马上要用
- 字体**不提供 `SetFont` API**（第一版无消费者，YAGNI），但**内部保留 `m_font` 成员**（默认 `Font()`）——未来加 `SetFont` 只需 `m_font = font;` 一行，不改绘制流程

### L5 对齐

**A：水平左对齐 + 垂直居中**（5.1 P7 既定落地，OnPaint 里 Measure 算偏移）

水平对齐枚举（Left/Center/Right）未来有需求再加。

### L6 重绘机制——第一版不引入 Invalidate ✅

**A：SetText 只更新内部数据，不自动触发重绘**（框架无 Invalidate 机制，Phase 3 明确"跨系统通知归未来子系统"）

**⚠️ 已知限制必须写注释**（GPT 补充）：

```cpp
/// @brief 设置文本
/// @details 仅更新内部数据，第一版不触发重绘（Invalidate 归未来交互系统）。
void SetText(const std::string& text);
```

动态改文本的真正消费者是 5.5 TextBox（届时 Invalidate 与光标一起做）。main.cpp 演示文本构造时固定，无感知。

### L7 验证

**A：main.cpp 加 Label 断言段 + 窗口视觉显示**

- 断言段：构造 Label → Paint 到 CommandBuffer → 断言 `DrawTextCommand` 的 text/pos/color/font（与 4.5/4.6/5.1 测试同级）
- 视觉：窗口显示 Label 真实文本（第一个可见文本）

### L8 边界（不做清单）

- 不做 AutoSize / GetPreferredSize（L2）
- 不做对齐枚举（L5）
- 不做 Invalidate（L6）
- 不做多行（5.1 D6 已定单行）
- **不提取 TextWidget（5.3 Button 出现第二个文本控件时才做）**
- 不提供 SetFont（L4）

## 待初步设计固化

1. Label 类完整形态（成员/API 签名，含 m_font 预留、m_textColor 默认 Color::Black()）
2. OnPaint 内"垂直居中偏移"的计算公式（MeasureText 宽度 + LineHeight 行高）
3. main.cpp 测试断言段的具体构造（Label 与 Panel 的坐标关系）
4. Widget 是否需要把 GetWidth/GetHeight 转 float 的便利（OnPaint 内 Rect 构造）
