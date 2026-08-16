# Phase 7.1.3 输入层抽象 — 职责确认

> 状态：v1.0（2026-08-15，GPT 修订融入）｜已确认，进初步设计
> 相关：phase7-platform-requirements.md（e-2 CaretGeometry 定稿）/ phase7-messagehandler-detailed-design.md（7.1.2 完成）
> 本质（GPT）：**不是"IME 抽象"，是"文本插入点（Insertion Point）模型升级"**——光标不是点，是矩形区域

## 现状（7.1.2 后）

```
TextBox::GetCaretClientPosition() → Point（光标顶部客户区）
  → Window::UpdateTextInputCaret(Point)（薄转发）
  → PlatformWindow::UpdateTextInputCaret(Point)（抽象）
  → Win32PlatformWindow（CreateCaret 硬编码 2x20 + SetCaretPos + Imm）
```

问题：① 传"点"但光标本质是"矩形区域"——平台层被迫硬编码 2x20 ② 未来多行/富文本光标是区域，Point 表达不了 ③ 语义收窄为 IME 专用。

## 决策（D1-D5）

### D1 CaretGeometry 落点 — Widget/ 领域目录（GPT 修订）
`ECDI/Widget/CaretGeometry.h`——只有文本输入领域消费（TextBox→Window→PlatformWindow→IME），不是 Point/Rect/Color 全框架基础类型。**迁 Core 条件（GPT 标准）：被至少 3~4 个独立子系统使用**（TextArea/CodeEditor/RichText 出现后）。

### D2 CaretGeometry 可扩展结构（GPT 修订——非 Rect 空包装）
```cpp
struct CaretGeometry{
    Rect rect;             ///< 插入点矩形（客户区坐标：x/y + 宽高）
    bool visible = true;   ///< 光标可见性（失焦/隐藏——闪烁系统 8.5 消费；false = 平台层跳过更新）
    // 扩展预留（注释记录，非现在实现）：baseline——部分输入法候选框按基线定位（多行/富文本时代）
};
```

### D3 不抽 TextInputInterface — ✅（YAGNI）
dynamic_cast\<TextBox\> 保留；触发条件 = 第二个可编辑控件出现 → EditableTextWidget → 再 TextInputInterface。

### D4 不拆 Win32IME — ✅（三方法建类收益≈0）
CreateCaret/Imm 留 Win32PlatformWindow。

### D5 全链升级 + 验证
- TextBox::GetCaretClientPosition → `CaretGeometry`（rect={光标x,y,2,lineH} + visible）
- Window/PlatformWindow/Win32PlatformWindow 参数全链 CaretGeometry
- Win32PlatformWindow：`CreateCaret(rect.width, rect.height)`（消灭硬编码 2x20）+ SetCaretPos(rect.x, rect.y)；visible=false 跳过更新
- 验证：中文候选窗跟随（caret 尺寸来自 rect——视觉零变化）+ 移动窗口归位 + grep（Point 不再出现在参数链）

## 修订记录

- v1.0（2026-08-15）职责确认定稿：D1-D5。GPT 修订全采纳：D1 CaretGeometry 放 Widget/ 领域目录（非 Core，迁 Core 条件 = 3~4 子系统使用）/ D2 可扩展结构（rect + visible，非 Rect 空包装；baseline 注释预留）/ D3 不抽 TextInputInterface（YAGNI，动态图保留）/ D4 不拆 Win32IME。本质定位：文本插入点模型升级（光标 = 矩形区域，非点）。
