# Phase 48 · SpanStyle::background 与混排选区按 run 对齐（#90）· 初步设计

> 状态：**v1.1（2026-11-18）：吸收初设评审 P1-A..D 与 P2-a..g，放行进详细设计**。
> 上游 = [phase48-span-background-requirements.md](phase48-span-background-requirements.md)（v1.1，已吸收 P1-1..4 / P2-1..5 与用户 P1-2 方向 A 拍板，无未决闸门）。
> ★ 基线：HEAD `697ed9f`；用例 **631**、公共头 **102**、设计文档 **227 → 228**（含本稿）。

---

## §1 改动总览与接口清单

### 1.1 公共接口变更（公共头 `102` 不变，仅结构体字段 +1）

在 `ECDI/include/ECDI/Widget/TextSpan.h` 中，`SpanStyle` 结构体**末尾**追加：

```cpp
std::optional<Color> background = std::nullopt; ///< 背景底色（std::nullopt 表示无底色）
```

- **顺序合规**：`SpanStyle` 现有字段顺序 `family` → `weight` → `foreground` → `italic` → `size` 保持不变，新字段追加在末尾，不破坏仓内 30+ 处 designated initializer（MSVC `C7560`）与聚合/默认初始化。
- **`operator==`**：`bool operator==(const SpanStyle&) const noexcept = default;` 自动覆盖 `background`，使 `TextWidget::SetStyledText` 中的 `m_spans == spans` 早退判断对仅修改 `background` 的调用自然生效（满足 `C48-3`）。
- **`ClampAndValidateSpans`**：`TextSpan s = span;` 整体拷贝保留 `s.style.background`，无需额外改写。

### 1.2 内部数据结构与方法签名变更

在 `ECDI/include/ECDI/Widget/TextWidget.h` 中：

1. **`RunSlice` 末尾追加**：
   ```cpp
   std::optional<Color> background = std::nullopt; ///< 切片背景底色（std::nullopt 表示不绘制底色）
   ```
2. **`BuildRunsForRange`**（`TextWidget.cpp:414-470`）：
   - 前后间隙（gap）`RunSlice`：`background` 置 `std::nullopt`；
   - span 覆盖 `RunSlice`：`background` 填 `span.style.background`。
3. **`LineBoxGuardActive()`**：维持不变（`ResolveRunFont` 不读 `background`，故纯 `background` 覆盖不触发行框字体切换，满足 `K48-4`）。

### 1.3 跨控件公共绘制辅助（`protected` 面 +2，沿 Phase 38 D38-A ⓐ 记账）

在 `TextWidget` 的 `protected` 区直接新增两个只读静态辅助成员（供 `TextWidget::DrawTextContent` 与派生类 `TextBox::OnPaint` 共用；`cpp` 匿名辅助无法跨 TU 复用，故明确落 `protected` 面 `+2`）：

- **`EmitRunBackgrounds`**：给定 `PaintContext&`、`const std::vector<RunSlice>& runs`、行起点 `float startX`、行框 `float lineY`、行框高 `float lineH`：
  ```cpp
  float bgX = startX;
  for (const auto& run : runs) {
      if (run.cpCount == 0) continue;
      if (run.background.has_value() && run.width > 0.0f) {
          ctx.DrawRect(Rect{ bgX, lineY, run.width, lineH }, *run.background);
      }
      bgX += run.width;
  }
  ```
- **`EmitRunTexts`**：给定 `PaintContext&`、`TextMeasurer&`、`const std::string& textSource`、`const std::vector<RunSlice>& runs`、起点 `float startX`、`float lineY`、`const Color& baseColor`、`bool lineBoxActive`、`float boxA`、以及基类 `ByteOffsetAt` 查询：
  - 逐 run 计算 `runY = lineBoxActive ? (lineY + boxA - measurer.Ascent(run.font)) : lineY` 并发射 `ctx.DrawText(...)`。

---

## §2 选区几何（`TextBox` 混排选区按 run 推进）

> 本节独立成章，作为本 Phase 唯一具备独立回退价值的子模块。

### 2.1 问题定义

在 `TextBox::OnPaint` 的单行循环中，设当前行的裁剪区间为 `[startCp, drawEndCp)`（`LineDrawRange`），归属区间为 `[startCp, ownEndCp)`（`LineRange`），已知 `startCp <= drawEndCp <= ownEndCp`。
当 `HasSelection()` 且 `[selMin, selMax)` 与 `[startCp, ownEndCp)` 有交集时，求交区间为：
- `hlStartCp = max(selMin, startCp)`
- `hlEndCp = min(selMax, ownEndCp)`（满足 `startCp <= hlStartCp < hlEndCp <= ownEndCp`）

需要求得 `hlStartCp` 与 `hlEndCp` 相对于行首 `viewX` 的逻辑水平偏移 `hlMin` 与 `hlMax`。

### 2.2 双分支计算策略

- **分支一（`!activeSpans`）**：
  - 保持原有代码：对 `textSource.substr(startByte, hlStartByte - startByte)` 与 `hlEndByte - startByte` 用基字体 `TextWidget::m_style.font.value` 调 `ctx.MeasureText`，保证红线 1 逐位等价。
- **分支二（`activeSpans`）**：
  - 在 `TextBox` 中定义私有成员函数 `MeasureCpOffsetInLine`（`const`），直接复用基类 `protected` 方法 `ByteOffsetAt`（其底层表 `m_cpByteOffsets` 已在 `TextBox.cpp:1512` 基于显示串 `textSource` 建立，非 `m_text`）。输入为：
    - `TextMeasurer& measurer`
    - `const std::string& textSource`
    - `const std::vector<RunSlice>& lineRuns`（覆盖 `[startCp, drawEndCp)`）
    - `std::size_t startCp`、`std::size_t drawEndCp`、`const Font& baseFont`
    - 目标码点 `std::size_t targetCp`（`startCp <= targetCp <= ownEndCp`）
  - **算法步骤**：
    1. 若 `targetCp <= startCp`，直接返回 `0.0f`。
    2. 设 `x = 0.0f`，`const std::size_t cappedCp = std::min(targetCp, drawEndCp)`。
    3. 遍历 `lineRuns`（各 run 连续覆盖 `[startCp, drawEndCp)`）：
       - 若 `run.cpCount == 0`：跳过。
       - 令 `runEnd = run.startCp + run.cpCount`。
       - 若 `cappedCp >= runEnd`：整段 run 位于目标左侧，直接累加 `x += run.width`。
       - 否则若 `cappedCp > run.startCp`（目标落在当前 `run` 内部，`run.startCp < cappedCp < runEnd`）：
         - 取 `b0 = ByteOffsetAt(run.startCp)`、`b1 = ByteOffsetAt(cappedCp)`；
         - 用该 run 自己的字体 `run.font` 测量子串 `textSource.substr(b0, b1 - b0)`，累加 `x += measurer.MeasureText(run.font, ...).width`；
         - 结束遍历（`break`）。
       - 否则（`cappedCp <= run.startCp`）：`break`。
    4. **折行归属尾部补测（P1-1 / P2-d）**：若 `targetCp > drawEndCp`（例如软断点行尾分隔空格被纳入选区，`hlEndCp == ownEndCp > drawEndCp`）：
       - 取 `bDraw = ByteOffsetAt(drawEndCp)`、`bTarget = ByteOffsetAt(targetCp)`；
       - 用基字体 `baseFont` 测量 `textSource.substr(bDraw, bTarget - bDraw)`，累加 `x += measurer.MeasureText(baseFont, ...).width`。
       - **设计依据（防误改）**：该段不发射文本，使用基字体与旧实现整段基字体测量的取值完全一致。
    5. 返回 `x`。

### 2.3 正确性与边界分析

- **整 run 零重复测量**：光标/选区跨过整段 run 时直接累加 `run.width`，与绘制阶段 `runX += run.width` 完全同源，无浮点累积差异。
- **run 内切分精度**：仅当 `targetCp` 落在 run 内部时按 `run.font` 测量前缀，每行最多对 `hlStartCp` 与 `hlEndCp` 各触发 1 次局部 `MeasureText`。
- **尾部空格越界保护**：`drawEndCp == ownEndCp`（非折行或无尾随裁剪空格）时步骤 4 不执行；`ownEndCp > drawEndCp` 时步骤 4 补测 `[drawEndCp, targetCp)`，保证 `hlMax` 覆盖行尾空格。
- **可独立回退性**：若后续需调整选区几何语义，仅影响 `MeasureCpOffsetInLine` 单一函数，不波及 `RunSlice` 与背景绘制。

---

## §3 三条绘制通路与 `TextBox::OnPaint` 重构

> **C48-7（重排等价护栏）**：§3 重排后，既有 `T45-5 ②/③`（`WidgetTests.cpp:2480-2506`，`kGoldenPhase44Spans` 与全 `SpanStyle{}` 黄金字面量）必须**逐位不变**；该既有用例即重排零漂移的回归判据，不新增重复黄金用例。

### 3.1 `TextBox::OnPaint` 循环结构重排（P2-1）

当前 `TextBox::OnPaint`（`TextBox.cpp:1514-1593`）将 `activeSpans` 与 `lineRuns` 放在 ②（文本绘制）内，导致 ①（选区高亮）无法访问 `lineRuns`。重构后在 `for` 循环外先求 `const bool activeSpans = SpansActiveForLayout();`，每行循环头顺序为：

1. **前置准备（每行仅一次）**：
   - 计算 `startCp`、`drawEndCp`、`ownEndCp`、`lineY`。
   - 若 `activeSpans` 为真，构建本行唯一的 `lineRuns = BuildRunsForRange(measurer, textSource, m_cpByteOffsets, startCp, drawEndCp);`（`EnsureCpByteOffsets(textSource)` 已在循环外 `:1512` 调用，无需重复调用）。
2. **Layer 1 — Span 底色块**：
   - 若 `activeSpans`，调用 `EmitRunBackgrounds(ctx, lineRuns, viewX, lineY, lineH)`。
   - **裁剪边界（P2-f）**：超宽文本与横向滚动时的底色不越界由循环外既有的 `ctx.PushClip`（`TextBox.cpp:1502`）保证，无需再额外 `PushClip`。
3. **Layer 2 — Selection 选区高亮**：
   - 若 `HasSelection()` 且有交集：
     - `activeSpans` 为真时用 `MeasureCpOffsetInLine(..., lineRuns, ...)` 计算 `hlMin` / `hlMax`；
     - 否则走原基字体 `ctx.MeasureText` 路径；
     - 发射 `ctx.DrawRect(Rect{ viewX + hlMin, lineY, hlMax - hlMin, lineH }, m_style.selection.value)`。
4. **Layer 3 — 行文本 `DrawText`**：
   - `activeSpans` 为真时调用 `EmitRunTexts`；
   - 否则走原整行 `ctx.DrawText`。
5. **Layer 4 — 组合串下划线**：保持现有 ③ 不变。

### 3.2 `TextWidget::DrawTextContent` 两条通路

- **折行分支**（`TextWidget.cpp:746-763`）：
  - 在 `activeSpans && !line.runs.empty()` 时，先调 `EmitRunBackgrounds(ctx, line.runs, block.x, lineY, lineHeight)`，再调 `EmitRunTexts(...)`。
- **单行分支**（`TextWidget.cpp:806-817`）：
  - 在 `activeSpans` 时，先调 `EmitRunBackgrounds(ctx, m_prefRuns, startPos.x, startPos.y, lineHeight)`，再调 `EmitRunTexts(...)`。
  - 注：垂直坐标使用 `startPos.y`、高度使用 `lineHeight`（与 `TextWidget` 行框高对齐；`overhang` 仅扩外包与居中参考总高，不拉高底色块）。

---

## §4 改动文件清单与影响评估

| 文件 | 改动类型 | 预计改动内容 |
|---|---|---|
| `ECDI/include/ECDI/Widget/TextSpan.h` | 公共头（+1 字段） | `SpanStyle` 末尾追加 `std::optional<Color> background = std::nullopt;` |
| `ECDI/include/ECDI/Widget/TextWidget.h` | 公共头（protected 面 +2） | `RunSlice` 末尾追加 `background`；声明 `EmitRunBackgrounds` / `EmitRunTexts` |
| `ECDI/src/Widget/TextWidget.cpp` | 实现 | `BuildRunsForRange` 透传 `background`；实现并接入 `EmitRunBackgrounds` / `EmitRunTexts` 到单行与折行分支 |
| `ECDI/src/Widget/TextBox.cpp` | 实现 | `OnPaint` 循环头构建 `lineRuns`、先画底色再画选区再画文字；新增 `MeasureCpOffsetInLine` 处理按 run 选区推进与 `ownEndCp > drawEndCp` 补测 |
| `ECDI/src/Tests/WidgetTests.cpp` | 测试（挂载至 `RegisterWidgetTests()`） | 新增 `T48-1`、`T48-2`、`T48-3`、`T48-6`（`RunAllTests.h/.cpp` 注册清单不变） |
| `ECDI/src/Tests/TextBoxTests.cpp` | 测试（挂载至 `RegisterTextBoxTests()`） | 新增 `T48-4`、`T48-5`、`T48-7` |

- **公共头数量**：保持 **102** 不变。
- **渲染后端与 `RenderCommand`**：零改动。

---

## §5 测试矩阵细化

| 用例 | 归属文件 | 断言内容 |
|---|---|---|
| **T48-1** | `WidgetTests.cpp` | 分两相验证：① **同源自洽**：`Label` 单行与折行（软断点将同一带 `background` 的 span 切分到两行）逐 run 断言底色 `DrawRect.x ==` 该 run `DrawText.pos.x`、`DrawRect.width ==` 该 run 测量宽度；② **跨 DPI 响应**：验证 DIP 命令流在不同 DPI 缩放下随 fake measurer 返回的 run 宽变化而同步调整 `DrawRect.width`（无 width 变化时 DIP 坐标不变） |
| **T48-2** | `WidgetTests.cpp` | 同行混字号（如 14px + 22px）与斜体时，所有 span 底色矩形具有相同的 `y` 与 `height`（`lineHeight`），且全行底色 `DrawRect` 索引均小于本行首个 `DrawText` |
| **T48-3** | `WidgetTests.cpp` | 在 `T45-5 ②` 同款 spans（`family`/`weight`/`foreground` 混排，无 `background`）上断言 `DrawRectCommand` 计数为 `0`（命令流逐位等价已由既有 `T45-5 ②/③` 承担，本用例不重复） |
| **T48-4** | `TextBoxTests.cpp` | `TextBox` 设 `background` span 且选中部分文本时，行内索引满足：`span bg DrawRect` < `selection DrawRect` < 该行 `DrawText` |
| **T48-5** | `TextBoxTests.cpp` | 使用固定字宽 fake measurer（如按字符数 × 固定步宽手算期望值），在 `TextBox` 混字号/粗体下分别验证跨 run 选区与 run 内局部码点选区，断言 `selection DrawRect` 的 `x` 与 `width` 精确匹配手算期望 |
| **T48-6** | `WidgetTests.cpp` | 仅修改 `SpanStyle::background` 再次调用 `SetStyledText`，重绘后 `DrawRect` 颜色更新为新值（注：仅验 `Label` `m_prefRuns`；`TextBox` 底色不依赖行表，`m_linesSpanRevision` 已触发重建，不再单设用例） |
| **T48-7** | `TextBoxTests.cpp` | 折行 `TextBox` 选中包含软断点行尾分隔空格的区间（`ownEndCp > drawEndCp`），断言 `selection DrawRect` 右界覆盖空格宽度而不回缩 |

---

## §6 修订记录

- **v1.1（2026-11-18）**：吸收外部评审 `P1-A..D` 与 `P2-a..g`：① 修正测试文件名 `TextWidgetTests.cpp` → `WidgetTests.cpp` 并明确挂载点（`P1-A`、`P2-b`）；② 重写 `T48-3` 为 `DrawRectCommand == 0` 断言，避免与 `T45-5 ③` 重复（`P1-B`）；③ 将 `T48-1` 拆为同源自洽与跨 DPI 响应两相（`P1-C`）；④ 在 `§3` 新增 `C48-7` 重排等价护栏（`P1-D`）；⑤ 明确 `protected` 面 `+2`、步骤 4 基字体依据、`ByteOffsetAt` 显示串口径、既有 `PushClip` 保护、固定字宽手算期望与 `T48-6` 范围（`P2-a..g`）。
- **v1.0（2026-11-18）**：初设稿落盘，细化 `SpanStyle`/`RunSlice` 扩维、`EmitRunBackgrounds`/`EmitRunTexts` 共用辅助、独立「选区几何」算法（含 `ownEndCp > drawEndCp` 补测）、`TextBox::OnPaint` 循环重排与 7 项测试矩阵。
