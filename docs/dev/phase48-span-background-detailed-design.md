# Phase 48 · SpanStyle::background 与混排选区按 run 对齐（#90）· 详细设计

> 状态：**v1.1（2026-11-18）：吸收详设评审 P1-1..3 与 P2-1..7，放行进入 Batch 1 → Batch 2 实施**。
> 上游 = [phase48-span-background-requirements.md](phase48-span-background-requirements.md)（v1.1）+ [phase48-span-background-preliminary-design.md](phase48-span-background-preliminary-design.md)（v1.1）。
> ★ 基线：HEAD `697ed9f`；用例 **631 → 638（+7）**、公共头 **102**、设计文档 **228 → 229**（含本稿；`dev/` 228 篇 + `usage.md`）。

---

## §1 逐文件实施规格

### 1.1 `ECDI/include/ECDI/Widget/TextSpan.h`

在 `SpanStyle` 结构体（`:15-23`）末尾追加 `background` 字段，并同步更新类注释（`:12`）：

```cpp
struct SpanStyle {
	std::optional<std::string>   family     = std::nullopt; ///< 字体族（std::nullopt 表示继承基样式）
	std::optional<std::uint16_t> weight     = std::nullopt; ///< 字重（400=Regular, 700=Bold；std::nullopt 表示继承）
	std::optional<Color>         foreground = std::nullopt; ///< 前景色（std::nullopt 表示继承控件当前前景色）
	std::optional<bool>          italic     = std::nullopt; ///< 斜体（std::nullopt 表示继承）
	std::optional<float>         size       = std::nullopt; ///< 字号（DIP 绝对值；std::nullopt 表示继承）
	std::optional<Color>         background = std::nullopt; ///< 背景底色（std::nullopt 表示无底色，追加于末尾以保 C7560 声明序）

	bool operator==(const SpanStyle&) const noexcept = default;
};
```

- **公共头变化**：`102` 不变。
- **`ClampAndValidateSpans`**：`TextWidget.cpp:260` `TextSpan s = span;` 自动拷贝 `background`。

---

### 1.2 `ECDI/include/ECDI/Widget/TextWidget.h`

1. **`RunSlice` 末尾追加字段**（`:128-134`）：
   ```cpp
   struct RunSlice {
   	std::size_t          startCp;    ///< 切片起始码点索引
   	std::size_t          cpCount;    ///< 切片码点数
   	float                width;      ///< 切片测量宽度
   	Font                 font;       ///< 生效字体（族 + 字重 + 斜体 + 字号；P2-4 注释修正，非行为变更）
   	std::optional<Color> color;      ///< 切片前景色（std::nullopt 表示继承基色，绘制期解析）
   	std::optional<Color> background; ///< 切片背景底色（std::nullopt 表示不绘制底色）
   };
   ```

2. **`protected` 区新增两个非静态辅助成员**（紧跟 `BuildRunsForRange` 声明之后，`:239` 附近；`protected` 面 `+2`，沿 Phase 38 D38-A ⓐ 记账）：
   ```cpp
   /// @brief 发射整行 span 底色矩形（Phase 48，先于同帧文本/选区绘制）
   /// @note static 而非 const 成员：不读取 m_cpByteOffsets 偏移表（P2-5）；不加 noexcept（内部调用 DrawRect 可能分配 vector，P1-2）
   static void EmitRunBackgrounds(
       PaintContext& ctx, const std::vector<RunSlice>& runs,
       float startX, float lineY, float lineHeight);

   /// @brief 发射整行 styled runs 文本（Phase 48，三通路复用，消除重复循环）
   /// @note const 成员：需读取本类经 EnsureCpByteOffsets 建立的 ByteOffsetAt（P2-5）
   void EmitRunTexts(
       PaintContext& ctx, TextMeasurer& measurer, const std::string& textSource,
       const std::vector<RunSlice>& runs, float startX, float lineY,
       const Color& baseColor, bool lineBoxActive, float boxAscent) const;
   ```
   - 说明：`EmitRunTexts` 为非静态 `const` 成员，以便内部直接调用 `ByteOffsetAt(run.startCp)`（`TextWidget.h:196`），确保 `TextWidget` 与 `TextBox` 均读取各自经 `EnsureCpByteOffsets(textSource)` 建好的偏移表。

---

### 1.3 `ECDI/src/Widget/TextWidget.cpp`

1. **`BuildRunsForRange`（`:414-470`）**：
   - `:441` 前隙：`runs.push_back(RunSlice{ cur, gapEnd - cur, w, baseFont, std::nullopt, std::nullopt });`
   - `:456` span 覆盖：`runs.push_back(RunSlice{ rStart, rEnd - rStart, w, rFont, span.style.foreground, span.style.background });`
   - `:466` 尾隙：`runs.push_back(RunSlice{ cur, endCp - cur, w, baseFont, std::nullopt, std::nullopt });`

2. **新增 `EmitRunBackgrounds` 与 `EmitRunTexts` 定义**：
   ```cpp
   void TextWidget::EmitRunBackgrounds(
       PaintContext& ctx, const std::vector<RunSlice>& runs,
       float startX, float lineY, float lineHeight) {
   	float bgX = startX;
   	for (const auto& run : runs) {
   		// 前提：零码点 run 恒有 width == 0（BuildRunsForRange 仅对非空切片推入），跳过安全（P2-3）
   		if (run.cpCount == 0) continue;
   		if (run.background.has_value() && run.width > 0.0f) {
   			ctx.DrawRect(Rect{ bgX, lineY, run.width, lineHeight }, *run.background);
   		}
   		bgX += run.width;
   	}
   }

   void TextWidget::EmitRunTexts(
       PaintContext& ctx, TextMeasurer& measurer, const std::string& textSource,
       const std::vector<RunSlice>& runs, float startX, float lineY,
       const Color& baseColor, bool lineBoxActive, float boxAscent) const {
   	float runX = startX;
   	for (const auto& run : runs) {
   		if (run.cpCount == 0) continue;
   		const std::size_t b0 = ByteOffsetAt(run.startCp);
   		const std::size_t b1 = ByteOffsetAt(run.startCp + run.cpCount);
   		const Color runColor = run.color.value_or(baseColor);
   		const float runY = lineBoxActive
   			? (lineY + boxAscent - measurer.Ascent(run.font))
   			: lineY;
   		ctx.DrawText(Point{ runX, runY }, textSource.substr(b0, b1 - b0), runColor, run.font);
   		runX += run.width;
   	}
   }
   ```

3. **改造 `DrawTextContent` 两条通路**：
   - **折行分支**（`:746-763`）：
     ```cpp
     if (activeSpans && !line.runs.empty())
     {
     	const bool lineBoxActive = LineBoxGuardActive();
     	const float boxA = lineBoxActive ? measurer.Ascent(lineBoxFont) : 0.0f;
     	EmitRunBackgrounds(ctx, line.runs, block.x, lineY, lineHeight);
     	EmitRunTexts(ctx, measurer, m_text, line.runs, block.x, lineY, baseColor, lineBoxActive, boxA);
     }
     ```
   - **单行分支**（`:803-818`）：
     ```cpp
     const bool lineBoxActive = LineBoxGuardActive();
     const float boxA = lineBoxActive ? measurer.Ascent(lineBoxFont) : 0.0f;
     EmitRunBackgrounds(ctx, m_prefRuns, startPos.x, startPos.y, lineHeight);
     EmitRunTexts(ctx, measurer, m_text, m_prefRuns, startPos.x, startPos.y, baseColor, lineBoxActive, boxA);
     ```
   - **红线 1 保证**：当 `activeSpans == false` 时完全不走这两段；当 `activeSpans == true` 但 `background` 全为 `std::nullopt` 时，`EmitRunBackgrounds` 不发射任何 `DrawRect`，`EmitRunTexts` 发射的 `DrawText` 序列与改前逐位一致（`C48-7` 由 `T45-5 ②/③` 护栏）。`TextWidget` 侧的 `lineBoxActive`/`boxA` 保持在分支内局部计算，不提升到 `DrawTextContent` 顶部，控制 diff 范围（P2-7）。

---

### 1.4 `ECDI/include/ECDI/Widget/TextBox.h` & `ECDI/src/Widget/TextBox.cpp`

1. **`TextBox.h` `private` 辅助声明**（不暴露公共 API，在 `TextBox.h` 私有成员函数区新增）：
   ```cpp
   /// @brief 计算目标码点在当前行内相对于行首的逻辑水平偏移（Phase 48，与 lineRuns 绘制严格同源）
   [[nodiscard]] float MeasureCpOffsetInLine(
       TextMeasurer& measurer, const std::string& textSource,
       const std::vector<RunSlice>& lineRuns,
       std::size_t startCp, std::size_t drawEndCp,
       const Font& baseFont, std::size_t targetCp) const;
   ```

2. **`TextBox.cpp` 定义 `MeasureCpOffsetInLine`**：
   ```cpp
   float TextBox::MeasureCpOffsetInLine(
       TextMeasurer& measurer, const std::string& textSource,
       const std::vector<RunSlice>& lineRuns,
       std::size_t startCp, std::size_t drawEndCp,
       const Font& baseFont, std::size_t targetCp) const {
   	if (targetCp <= startCp) {
   		return 0.0f;
   	}
   	float x = 0.0f;
   	const std::size_t cappedCp = (std::min)(targetCp, drawEndCp);
   	for (const auto& run : lineRuns) {
   		if (run.cpCount == 0) continue;
   		const std::size_t runEnd = run.startCp + run.cpCount;
   		if (cappedCp >= runEnd) {
   			x += run.width;
   		}
   		else if (cappedCp > run.startCp) {
   			const std::size_t b0 = ByteOffsetAt(run.startCp);
   			const std::size_t b1 = ByteOffsetAt(cappedCp);
   			x += measurer.MeasureText(run.font, textSource.substr(b0, b1 - b0)).width;
   			break;
   		}
   		else {
   			break;
   		}
   	}
   	// 折行归属尾部（如行尾分隔空格，ownEndCp > drawEndCp）：该段不发射文本，
   	// 用基字体补测与旧实现整段基字体测量取值一致，勿改为 span 字体。
   	if (targetCp > drawEndCp) {
   		const std::size_t bDraw   = ByteOffsetAt(drawEndCp);
   		const std::size_t bTarget = ByteOffsetAt(targetCp);
   		x += measurer.MeasureText(baseFont, textSource.substr(bDraw, bTarget - bDraw)).width;
   	}
   	return x;
   }
   ```

3. **`TextBox::OnPaint` 重排**（`TextBox.cpp:1512-1575`）：
   - 在 `for` 循环前一次性求值（**并删除原 `:1547/:1551/:1553/:1554` 的循环内局部定义，防止重复定义，P2-2**）：
     ```cpp
     const bool activeSpans = SpansActiveForLayout();
     TextMeasurer& measurer = ctx.GetTextMeasurer();
     const bool lineBoxActive = activeSpans && LineBoxGuardActive();
     const float boxA = lineBoxActive ? measurer.Ascent(EffectiveLineBoxFont(measurer)) : 0.0f;
     ```
   - 在 `for (int li = firstVisible; li <= lastVisible; ++li)` 循环头：
     - 计算 `startCp`、`drawEndCp`、`ownEndCp`、`startByte`、`drawEndByte`、`lineY`。
     - 仅当 `activeSpans` 为真时构建一次 `lineRuns`：
       ```cpp
       std::vector<RunSlice> lineRuns;
       if (activeSpans) {
       	lineRuns = BuildRunsForRange(measurer, textSource, m_cpByteOffsets, startCp, drawEndCp);
       }
       ```
     - **Layer 1（Span 底色块）**：
       ```cpp
       if (activeSpans) {
       	EmitRunBackgrounds(ctx, lineRuns, viewX, lineY, lineH);
       }
       ```
       （受循环外 `ctx.PushClip` `:1502` 裁剪保护，无需再 `PushClip`。）
     - **Layer 2（Selection 选区高亮）**：
       ```cpp
       if (HasSelection()) {
       	const size_t selMin = GetSelectionMin();
       	const size_t selMax = GetSelectionMax();
       	if (selMin < ownEndCp && selMax > startCp) {
       		const size_t hlStartCp = (std::max)(selMin, startCp);
       		const size_t hlEndCp   = (std::min)(selMax, ownEndCp);
       		float hlMin = 0.0f;
       		float hlMax = 0.0f;
       		if (activeSpans) {
       			hlMin = MeasureCpOffsetInLine(measurer, textSource, lineRuns, startCp, drawEndCp,
       			                              TextWidget::m_style.font.value, hlStartCp);
       			hlMax = MeasureCpOffsetInLine(measurer, textSource, lineRuns, startCp, drawEndCp,
       			                              TextWidget::m_style.font.value, hlEndCp);
       		}
       		else {
       			const size_t hlStartByte = ByteOffsetAt(hlStartCp);
       			const size_t hlEndByte   = ByteOffsetAt(hlEndCp);
       			hlMin = ctx.MeasureText(TextWidget::m_style.font.value, textSource.substr(startByte, hlStartByte - startByte)).width;
       			hlMax = ctx.MeasureText(TextWidget::m_style.font.value, textSource.substr(startByte, hlEndByte - startByte)).width;
       		}
       		ctx.DrawRect(Rect{ viewX + hlMin, lineY, hlMax - hlMin, lineH }, m_style.selection.value);
       	}
       }
       ```
     - **Layer 3（行文本 `DrawText`）**：
       ```cpp
       if (activeSpans) {
       	EmitRunTexts(ctx, measurer, textSource, lineRuns, viewX, lineY,
       	             TextWidget::m_style.foreground.value, lineBoxActive, boxA);
       }
       else {
       	const std::string lineText = textSource.substr(startByte, drawEndByte - startByte);
       	if (!lineText.empty()) {
       		ctx.DrawText(Point{ viewX, lineY }, lineText,
       			TextWidget::m_style.foreground.value, TextWidget::m_style.font.value);
       	}
       }
       ```
     - **Layer 4（组合串下划线）**：保持 `:1578-1592` 不变。

---

## §2 验收测试详细规约（`631 → 638`，+7 用例）

### 2.1 挂载位置
- `T48-1`、`T48-2`、`T48-3`、`T48-6` 写入 `ECDI/src/Tests/WidgetTests.cpp` 末尾并挂载至 `RegisterWidgetTests()`。
- `T48-4`、`T48-5`、`T48-7` 写入 `ECDI/src/Tests/TextBoxTests.cpp` 末尾并挂载至 `RegisterTextBoxTests()`。
- `RunAllTests.h/.cpp` 注册函数清单保持零改动。

### 2.2 各用例精确断言设计

| 用例 | 核心设置与断言 |
|---|---|
| **T48-1** | ① **同源自洽**：使用固定字宽 `FakeTextMeasurer` 构造单行与折行 `Label`（软断点将带 `background` 的 span 切分到两行），遍历录制 `cmds`，逐个匹配底色 `DrawRectCommand` 与对应 `DrawTextCommand`，断言 `bg.rect.x == text.pos.x`、`bg.rect.width` 等于手算期望宽（或与下一 `DrawText.pos.x - text.pos.x` 一致，P2-1）且两行各自发射 1 个底色 `DrawRect`；② **跨 DPI 响应**：用可配置 DPI 缩放系数的 `FakeTextMeasurer` 在 1.0x 与 1.5x 下分别绘制，断言无字宽变化时 DIP `DrawRect` 逐位一致，1.5x 改变测量字宽时 `bg.rect.width` 同步跟随手算新宽 |
| **T48-2** | 构造同行混字号（如 `14.0f` + `22.0f`）与 `italic = true` 且均带 `background` 的 `Label`，断言录制流中所有 `DrawRectCommand` 的 `rect.y` 相等、`rect.height` 相等（均等于行框 `lineHeight`），且所有 `DrawRectCommand` 在命令缓冲中的索引均小于首个 `DrawTextCommand` |
| **T48-3** | 在 `T45-5 ②` 同款混排 spans（`family`/`weight`/`foreground`，全无 `background`）上绘制 `Label`，断言 `cmds` 中 `DrawRectCommand` 数量 `== 0`（命令流逐位等价由既有 `T45-5 ②/③` 承担，不重复） |
| **T48-4** | 构造带 `background` span 的只读 `TextBox`，选中中间部分码点并 `Paint`，断言行内 `span bg DrawRect` 索引 `< selection DrawRect` 索引 `<` 该行 `DrawText` 索引（跳过控件背景与外框 `DrawRect`） |
| **T48-5** | 使用固定字宽 `FakeTextMeasurer`（如基字体每码点 `10.0f`、大号 span 每码点 `16.0f`），设置 `"abcdEFGH"` 中 `"EFGH"` 为大号 span，分别测试跨 run 选区 `[2, 6)`（手算 `hlMin = 20.0f`、`hlMax = 40.0f + 32.0f = 72.0f`、`width = 52.0f`）与 run 内选区 `[5, 7)`（手算 `hlMin = 56.0f`、`width = 32.0f`），断言 `selection DrawRect` 的 `x` 与 `width` 精确命中手算期望 |
| **T48-6** | `Label` 先设 `SpanStyle{ .background = Color::Red() }` 绘制并记录颜色，再仅改 `background = Color::Blue()` 调用 `SetStyledText`，重绘后断言 `DrawRect` 颜色更新为 `Color::Blue()` |
| **T48-7** | 折行 `TextBox`（固定字宽 `10.0f/字`，`SetSize(60, 100)`，两行内容不出滚动条、确保折行宽 = `60.0f`，P2-6），文本 `"abcde fghij"`（行 0 `drawEndCp = 5`，`ownEndCp = 6` 含空格），设 span 覆盖 `"abc"`（`background` 非空），全选或选中 `[3, 6)`，断言 `selection DrawRect` 右界覆盖至 `60.0f`（含行尾空格 `10.0f`），不缩回 `50.0f` |

---

## §3 实施批次与门禁

1. **Batch 1（产品码，5 文件，P1-1）**：`TextSpan.h`、`TextWidget.h`、`TextWidget.cpp`、`TextBox.h`、`TextBox.cpp`。
2. **Batch 2（测试与门禁，2 测试文件 + 账本收口，P1-3）**：`WidgetTests.cpp`（+4）、`TextBoxTests.cpp`（+3）。AI 侧完成 MinGW 过程自检（638/638）与 `doc_lint` 检查；用户侧完成四链（MSVC / Clang / ClangCL / MinGW）Debug 终验（记录 `FRAMEWORK_ASSERT` 启用状态及当前显示器缩放）后，正式更新 `docs/dev/README.md`、`docs/dev/roadmap-deferred.md`（#90 迁入已完成）、`README.md` 与 `README.zh-CN.md` 锚点，并执行跨仓 `ECDI_fake_harness --md-probe` 三相回归。

---

## §4 修订记录

- **v1.1（2026-11-18）**：吸收详设评审 `P1-1..3` 与 `P2-1..7`：① `Batch 1` 清单补齐 `TextBox.h`（`P1-1`）；② 移除 `EmitRunBackgrounds` `noexcept`（`P1-2`）；③ 收口门禁改为「AI 侧 MinGW 自检 → 用户侧四链 Debug 终验」（`P1-3`）；④ 吸收 `P2-1..7`（`T48-1` 手算字宽、`TextBox::OnPaint` 删除原局部定义、`cpCount==0` 注释、注释修正标注、`static/const` 理由、`T48-7` 无滚动条前提、`TextWidget` 不提升 `lineBoxActive`）。
- **v1.0（2026-11-18）**：详细设计初版落盘，吸收 `P2-a..g` 全部 7 项细化要求，给出逐文件精确代码规约与 7 项验收用例详表。
