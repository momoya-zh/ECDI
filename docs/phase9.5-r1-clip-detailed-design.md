# Phase 9.5 R1 Clip 栈消费层——详细设计

> 状态：v1.2（2026-08-27）｜五阶段法第 3 步（职责确认 ✅ v1.1 定稿 → 初步设计 ✅ v1.1 → **详细设计** → 实现 → 测试）  
> 升级记录：v1.0 评审前并入横向滚动（用户拍板）；v1.1 GPT 评审**有条件通过，补 3 个设计收口**——① m_preferredColumn 坐标契约彻底冻结（§4.6）② EnsureCaretVisible 统一调用时机（§4.7）③ Resize 后 Caret 可见（§4.8）+ O(n²) 表述修正 + S1/S4 测试精确化  
> 承接：phase9.5-r1-clip-preliminary-design.md v1.1（管线统一 Clip + D1-D4 + §6.5 必审清单 5 项）  
> 相关：phase8-renderer-design.md（PushClip/PopClip 能力 §8.5）/ phase4-renderer-design.md（决策 24/25 开区间契约）/ phase8.5.2-multiline-textbox（垂直滚动 m_scrollOffsetY + EnsureCaretVisible 跟手模式）

## 1. 必审清单落实（初步设计 §6.5 → 本设计冻结）

| #     | 必审项               | 冻结结论（已核实源码）                                                                                                                                                                                                                          |
| ----- | ----------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **1** | 坐标系               | 转换链：`Widget::m_geometry`（父相对）→ `Paint(ctx, offsetX, offsetY)` 累加 → `x/y`（**Window 客户区绝对坐标**，与 OnPaint 的 x/y 同源）→ `PushClip(Rect{x, y, w, h})` → RenderCommand 直传——**后端零感知 Widget Tree**                                              |
| **2** | Clip 与 Paint 包围关系 | **所有 Widget 统一生命周期**：`PushClip(自身边界) → OnPaint → children Paint → PopClip`；唯一例外 = `IsVisible()==false` 提前 return（**不 Push 不 Pop**，天然平衡）                                                                                              |
| **3** | Clip 边界映射         | `GDIBackend::PushClip`（已核实）：`lround(rect.x/y)` → `left/top`、`lround(rect.x+width / y+height)` → `right/bottom`，`IntersectClipRect` = **`[left, top, right, bottom)` 开区间**——与 DrawRect/DrawFocusRect 的 RECT 构造（决策 25）**完全同款**，无边缘像素误裁 |
| **4** | TextBox 三处截断      | ① Selection clamp（959-960 两处 `min`）→ **删**；② 行文本逐码点循环 + substr（972-983）→ **删**；③ 组合下划线（998-999）→ **零改动**（核实：本无 clamp，超宽自然越界，Clip 兜底）                                                                                                 |
| **5** | 既有越界绘制消费者         | **搜索完成（无）**：CheckBox/Radio/Button/Panel/TextWidget 全部绘制在自身边界内；TextBox 文本/Selection/下划线超宽 = **有意几何**（宽度=文本宽度），Clip 正确裁剪非掩盖 bug；TextBox 光标在超宽行末尾可能越界（现存潜在问题）→ Clip + 横向滚动自动修正                                                          |

## 2. 文件变更清单

| 文件                                   | 改动                                               | 说明                            |
| ------------------------------------ | ------------------------------------------------ | ----------------------------- |
| `ECDI/src/Widget/Widget.cpp`         | `Widget::Paint` 加 PushClip/PopClip（核心改动）         | 框架层逻辑变更                         |
| `ECDI/include/ECDI/Widget/Widget.h`   | `SetSize` 改虚（签名不变，实现移 Widget.cpp）              | TextBox override 挂 Resize 后 Caret 可见（§4.8） |
| `ECDI/include/ECDI/Widget/TextBox.h` | 新成员 `m_scrollOffsetX` + 访问器 `GetScrollOffsetX()` | 横向滚动状态                        |
| `ECDI/src/Widget/TextBox.cpp`        | 删 ① Selection clamp ② 行文本截断循环 + 横向滚动 5 处改动（§4.5） | 绘制路径一次改到位                     |
| `ECDI/src/Tests/ClipTests.cpp`（新）    | R1-S1~S10 测试                                     | RecordingBackend 命令断言 + 纯逻辑断言 |
| `ECDI/src/Tests/RunAllTests.h/.cpp`  | 注册 `RegisterClipTests()`                         | —                             |
| `ECDI/ECDI.vcxproj`                  | 添加 ClipTests.cpp                                 | —                             |

**接口变更说明**：无 Renderer/Window/PaintContext 公共接口变更（Phase 8 能力直接消费）；`Widget::SetSize` 改虚（签名不变、行为兼容，仅开放 override 点）；`TextBox` 新增内部状态 `m_scrollOffsetX` 及只读访问器 `GetScrollOffsetX()`（测试用，public 只读 getter）。

## 3. 核心算法（Widget.cpp Paint 完整伪代码）

```cpp
void Widget::Paint(PaintContext& ctx, int offsetX, int offsetY){

    if (!IsVisible())   // 唯一例外路径：不 Push 不 Pop（天然平衡）
        return;

    const int x = offsetX + static_cast<int>(m_geometry.x);
    const int y = offsetY + static_cast<int>(m_geometry.y);

    // 9.5 R1：自身边界入栈（绝对坐标 = Window 客户区；与 OnPaint 的 x/y 同源）
    ctx.PushClip(Rect{ static_cast<float>(x), static_cast<float>(y),
                       m_geometry.width, m_geometry.height });

    OnPaint(ctx, x, y);

    for (auto& child : m_children)
        child->Paint(ctx, x, y);   // 嵌套交集：父边界 ∩ 自身边界（后端 IntersectClipRect 天然语义）

    ctx.PopClip();   // 严格配对（Paint 内无 early return 路径）
}
```

**不变量**：

- **I1**：每次 `Paint` 成功执行 PushClip 后，离开该 scope 前必执行 PopClip（Paint 函数体无其他 return 路径——IsVisible 提前 return 在 Push 之前）
- **I2**：Clip 深度序列必为 `0→1→…→n→…→1→0`（递归返回时终值 0）
- **I3**：PushClip 的 rect 与 OnPaint 的 x/y 同源（同一 `m_geometry` + 同一 offset 累加）——坐标不可能漂移

## 4. TextBox OnPaint 迁移（逐项）

### 4.1 ① Selection 高亮（原 948-963 行）

```cpp
// 删前（959-960）：
const float hlMin = (std::min)(hlMinSize.width, maxTextWidth);
const float hlMax = (std::min)(hlMaxSize.width, maxTextWidth);
// 删后：
const float hlMin = hlMinSize.width;   // 画满——超宽部分被 Clip 裁（视觉等价）
const float hlMax = hlMaxSize.width;
```

### 4.2 ② 行文本（原 965-985 行）

```cpp
// 删前：lineSize.width <= maxTextWidth 分支 + 逐码点循环（972-983）找 visibleCps + substr
// 删后：直接画整行（x 减横向滚动偏移——§4.5）
if (!lineText.empty()){
    ctx.DrawText(Point{ viewX, lineY }, lineText,
                 TextWidget::m_style.foreground.value, TextWidget::m_style.font.value);
}
```

**删除 TextBox 层的 O(n²) 逐码点截断测量**（每码点一次 MeasureText + substr 复制），改为单次完整 DrawText 命令，可视区域裁切由 Clip 完成（绘制路径无截断循环；文本本身仍由后端整行处理）。

### 4.3 ③ 组合下划线（原 987-1001 行）——仅加滚动偏移

- 宽度本无 clamp（核实），超宽自然越界 → Clip 兜底；绘制 x 改 `viewX + ulStartSize.width`（§4.5）

### 4.4 保留不动（交互路径）

- `maxTextWidth` 变量：**删除**（仅绘制路径使用——938 行定义，删 ① ② 后无引用）
- `GetTextAreaWidth()`：**保留**（EnsureCaretVisible 横向右边界 + CaretIndexFromPosition 交互路径使用）
- 光标竖线（1005-1010）：`CalculateCaretPosition` 返回坐标已含滚动偏移（§4.5 第 3 点）——绘制代码不动

### 4.5 横向滚动（v1.1 新增——用户拍板并入 R1）

**现状契约**（已核实）：`CalculateCaretPosition` 352 行 `caretX = min(prefixWidth, 可视宽)`——**光标钉右缘，超长行尾部不可见/不可达**；`CaretIndexFromPosition` 436 行 `innerX` 无滚动偏移。

**滚动模式：跟手（光标驱动）——与垂直滚动同款**，无滚动条、无 `GetMaxScrollOffsetX`（滚动范围由光标边界推导，天然不越界；避免逐行测宽找最大行宽的性能问题）。**不做水平滚动条（v1.0 记账）**。

| # | 改动点                             | 内容                                                                                                                                                                                  |
| - | ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1 | `TextBox.h` 新成员                 | `float m_scrollOffsetX = 0.0f;`（水平滚动偏移，≥ 0）+ `float GetScrollOffsetX() const noexcept`（测试用）                                                                                         |
| 2 | `EnsureCaretVisible()` 加左右边界    | `caretX < scrollOffsetX → scrollOffsetX = caretX`（左边界）；`caretX > scrollOffsetX + 可视宽 → scrollOffsetX = caretX - 可视宽`（右边界）；clamp ≥ 0。caretX = 行内前缀测量宽（与 CalculateCaretPosition 同源测量） |
| 3 | `CalculateCaretPosition()` 删钉右缘 | `caretX = prefixSize.width - m_scrollOffsetX`（逻辑 x − 滚动偏移；不再 min clamp）——光标绘制/IME 候选窗（SyncTextInputCaret 经此）自动跟随滚动                                                                  |
| 4 | `CaretIndexFromPosition()` 加偏移  | `innerX = localPos.x - inset + m_scrollOffsetX`（点击可视位置 → 逻辑 x = 可视 x + 滚动偏移）——点击/拖选跟手                                                                                               |
| 5 | `OnPaint()` 绘制统一减滚动偏移           | 新增 `const float viewX = textX - m_scrollOffsetX;`——文本 DrawText x、Selection 高亮 rect x、组合下划线 x 全部用 `viewX + 测量宽`（光标经 CalculateCaretPosition 自动生效）                                     |

**垂直滚动不动**（Y 逻辑独立正交）；**IME 候选窗**：光标 x 变化 → GetCaretClientGeometry → SyncTextInputCaret 自动联动（无额外改动）。

### 4.6 坐标契约冻结（v1.2——GPT 收口 ①：m_preferredColumn 彻底冻结，不留"实现时核实"）

**TextBox 内容坐标系（逻辑坐标）是唯一真相**，全系统契约如下：

```
                     TextBox 内容坐标系（logical X，文本坐标）
                                    │
                ┌───────────────────┼───────────────────┐
                │                   │                   │
      CaretIndexFromLineX(line, logicalX)   m_preferredColumn（永远存逻辑 X）
                │                   │                   │
                └─────── 码点索引 / Up/Down 跨行 ────────┘
                                    │
                    viewportX = logicalX - scrollOffsetX
                                    │
                                    ↓
                        Window 客户区坐标（绘制 / Clip）
```

- **`CaretIndexFromPosition()`**：输入 **viewport/local X** → `innerX = localPos.x - inset + m_scrollOffsetX` → **logical X** → 交 `CaretIndexFromLineX`
- **`CaretIndexFromLineX()`**：输入 **logical X**（Up/Down 跨行反推码点、点击定位共用——同一入口）
- **`m_preferredColumn`**：**永远存逻辑 X**——`ResetPreferredColumn` 改为 `CalculateCaretPosition(...).x + m_scrollOffsetX`（可视 x + 滚动偏移 = 逻辑 x）
- **绘制**：`viewX = textX - m_scrollOffsetX`（logical → viewport）
- **Up/Down 跨行**：`m_preferredColumn`（逻辑 X）→ `CaretIndexFromLineX` 反推——与点击定位同路径，语义天然一致，无漂移空间

### 4.7 EnsureCaretVisible 统一调用时机（v1.2——GPT 收口 ②）

**原则（冻结）**：**所有导致 Caret 位置变化的既有路径，在提交 Caret 位置后统一调用 `EnsureCaretVisible()`**；新增 Caret 变更路径必须遵守同一契约。

**现状核实**：无统一 `SetCaret` setter（编辑操作直接改 `m_caret` + 尾部调用 EnsureCaretVisible），**全部既有路径已覆盖**（已核实调用点）：点击（109）、键盘 ←→/Home/End/↑↓/Backspace/Delete（128/155/167/182/193/208/216/224）、Enter（564/584）、拖选（618/647/665）、文本修改恢复（726/812/890）。**不引入统一 setter（YAGNI——现状已全覆盖，重构非必需）**，保持"每个变更路径尾部调用"模式。

**测量**：`EnsureCaretVisible` 横向边界需 caretX（行内前缀测量）→ 经 `GetWindow()->GetTextMeasurer()`（既有路径，与 CalculateCaretPosition 同源）；无窗口（测试）时跳过测量，偏移更新规则提取为纯逻辑（测试 S7/S8 覆盖）。

### 4.8 Resize 后 Caret 可见（v1.2——GPT 收口 ③）

**现状核实**：`SetSize` 非虚内联（Widget.h:84），**无尺寸变化通知**；Layout 只 `SetPosition` 不碰尺寸（已核实 Vertical/HorizontalLayout）——**`SetSize` 是尺寸唯一入口**。

**方案（冻结）**：
1. `Widget::SetSize` 改虚（签名不变，实现移 Widget.cpp）——开放 override 点
2. `TextBox::SetSize` override：先调基类，再 `EnsureCaretVisible() + Invalidate()`——缩窗后光标越出可视区立即滚回（垂直/水平同时受益）

**边界**：Layout 不调 SetSize（只 SetPosition），无遗漏路径；`SetPosition` 不改尺寸，不触发。

## 5. 测试用例（ClipTests.cpp，RecordingBackend 命令断言 + 纯逻辑断言）

| 用例                              | 场景                                      | 断言                                                                                                           |
| ------------------------------- | --------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| **R1-S1 深度计数**                  | Panel→Button→Label 3 层树绘制               | RecordingBackend 收到 PushClip/PopClip **命令序列**——测试内部维护 depth 计数逐条断言（Push 前记录、Pop 后校验），嵌套顺序验证（Push(Panel)→Push(Button)→Push(Label)→Pop(Label)→Pop(Button)→Pop(Panel)），**终值必须 0** |
| **R1-S2 裁剪矩形坐标**                | Panel(10,20,100,50) 内 Button(5,5,30,20) | Panel PushClip rect={10,20,100,50}；Button PushClip rect={15,25,30,20}（offset 累加）                             |
| **R1-S3 超宽不截断**                 | TextBox 行文本超宽                           | DrawText 命令文本 = **整行**（含超出可视区部分）；命令位于控件 PushClip 之后                                                          |
| **R1-S4 Selection 不 clamp**     | 超宽行带 Selection                          | 高亮 DrawRect 的 width = **完整逻辑几何**（`EXPECT_EQ(rect.width, measuredSelectionWidth)`——不再被 maxTextWidth 截断）；测试数据保证 `measuredSelectionWidth > viewportWidth`（测"不 clamp"而非"必须超宽"） |
| **R1-S5 越界子控件**                 | 子控件部分超出父边界                              | 命令流含父边界 PushClip（父 Push 在子绘制前）                                                                               |
| **R1-S6 不可见不裁剪**                | IsVisible=false 的控件                     | 无该控件 PushClip/PopClip 命令                                                                                     |
| **R1-S7 横向跟手（纯逻辑）**             | 设 scrollOffsetX=0，caretX 越右边界           | EnsureCaretVisible 偏移更新规则断言（caretX > 可视宽 → scrollOffsetX = caretX - 可视宽）——偏移计算提取纯逻辑（无测量依赖），测量集成由编译验证         |
| **R1-S8 横向左边界回卷（纯逻辑）**          | caretX 移回行首                             | caretX < scrollOffsetX → scrollOffsetX = caretX（回 0）                                                         |
| **R1-S9 点击定位反向**                | 可视区点击 x                                 | CaretIndexFromPosition 用 `x + scrollOffsetX` 定位（innerX 语义）——无测量分支（GetWindow()==nullptr 返回行起始，仅验证偏移语义不破坏既有行为） |
| **R1-S10 preferredColumn 逻辑 x** | 横向滚动后 ResetPreferredColumn              | m_preferredColumn = 可视 x + scrollOffsetX（逻辑 x）——防 Up/Down 跨行漂移                                               |
| **R1-S11 Resize 后 Caret 可见**  | TextBox 宽 500、scrollOffsetX=300、caretX=700 → SetSize(200) | TextBox::SetSize → EnsureCaretVisible 被调用 → scrollOffsetX 重算至光标可见（S7/S8 偏移规则复用） |

## 6. 风险与回归核查

| 风险                          | 处置                                                                                                                                 |
| --------------------------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| **焦点框 1px 边缘**              | 开区间一致性（必审 3 已核实同款 RECT 构造）——边缘像素在 clip 内，无回归；用户编译后视觉核查                                                                             |
| **Radio 圆点 / CheckBox 勾**   | 均内切/内缩于自身边界——Clip 不误伤                                                                                                              |
| **TextBox 光标/Selection/文本** | 有意几何超宽由 Clip 裁（语义正确）；视觉等价迁移                                                                                                        |
| **Up/Down 跨行列漂移**           | 坐标契约冻结（§4.6）——`m_preferredColumn` 永远存逻辑 X，Up/Down 经 `CaretIndexFromLineX(logicalX)` 反推（与点击定位同路径），无漂移空间                                        |
| **横向滚动测量依赖**                | EnsureCaretVisible 的 caretX 需 MeasureText（Widget 非 Paint 期经 Window::GetTextMeasurer 获取——既有路径）；无窗口测试仅覆盖纯逻辑偏移规则（S7/S8），测量集成由用户编译视觉验证 |
| **Resize 漏触发**                | §4.8 冻结：`SetSize` 改虚 + TextBox override（Layout 只 SetPosition 不碰尺寸——已核实无遗漏路径）                                                                      |
| **每控件 SaveDC 性能**           | 记账（初步设计 R3）——v1.0 规模可接受                                                                                                            |

## 7. 修订记录

- v1.0（2026-08-27）初稿：必审清单 5 项落实 + 文件变更清单 + Widget::Paint 核心算法 + 三不变量 + TextBox 三处逐项迁移 + 6 测试用例。
- v1.1（2026-08-27）**用户拍板并入横向滚动**：新增 §4.5（m_scrollOffsetX 成员 + EnsureCaretVisible 左右边界 + CalculateCaretPosition 去钉右缘 + CaretIndexFromPosition 加偏移 + OnPaint viewX 统一）+ `m_preferredColumn` 改存逻辑 x（防跨行漂移）+ 测试 S7-S10 + 风险登记更新；TextBox.h 加入文件变更清单。
- v1.2（2026-08-27）**GPT 评审收口（有条件通过，补 3 项）**：① §4.6 坐标契约彻底冻结（TextBox 内容坐标系 = 逻辑 X 唯一真相；CaretIndexFromPosition 输入 viewport X / CaretIndexFromLineX 输入 logical X / m_preferredColumn 永远存逻辑 X / 绘制 viewX = logicalX - scrollOffsetX）——删"实现时核实"；② §4.7 EnsureCaretVisible 统一调用时机（原则冻结 + 既有路径全覆盖核实清单，不引入 setter）；③ §4.8 Resize 后 Caret 可见（SetSize 改虚 + TextBox override——Layout 只 SetPosition 已核实）；+ O(n²) 表述修正 + S1 深度计数测试描述精确化 + S4 断言稳定化（测"不 clamp"）+ 测试 S11 Resize + 风险登记更新。

