# Phase 48 · SpanStyle::background 与混排选区按 run 对齐（#90）· 需求稿

> 状态：**v1.1（2026-11-18）：吸收评审 P1-1..4 与 P2-1..5，并落地用户对 P1-2（方向 A：不加只读门控）的拍板；待清零计费锚点后直接进初步设计**。
> 上游 = `roadmap-deferred.md` **#90 行**（v2.100 登记）＝ 消费工程 `ECDI_fake_harness` 能力需求账本 **REQ-21**。
> ★ **源码行号**已对 HEAD `697ed9f` 亲核（2026-11-18）。基线：用例 **631**、公共头 **102**、设计文档 **226 → 227**（含本稿；进入初步设计时同步续订至 **228**）。

**红线（四条，任一破了本 Phase 即失败）**

1. **零 span 与零 background 覆盖路径逐位等价**：未设 / 清空 spans，或 spans 中 `background` 均为 `std::nullopt` 时，三条文本绘制通路的命令流、首选尺寸与布局行为与今日**逐位不变**；既有 **631** 条用例零回归。
2. **测画同源**：不引入第二测量源；`TextBox` 在 `SpansActiveForLayout()` 激活时的选区高亮 X 偏移，必须与同帧 `lineRuns` 逐 run 绘制推进保持严格同源（超出 `drawEndCp` 的归属尾部按基字体补测）。
3. **渲染后端与命令体零改动**：`RenderCommand` variant、`RecordingBackend`、`GDIBackend` 与 `GLRenderer` 执行体一行不动，仅复用既有 `PaintContext::DrawRect(Rect, Color)`。
4. **不借机扩到可编辑编辑器通路**：可编辑 `TextBox` 的光标 / 编辑 / IME / Undo × spans 仍归 `L44-1`；但按用户对 `P1-2` 的拍板（方向 A），`TextBox::SpansActiveForLayout()` **不额外增加 `IsReadOnly()` 门控**，以使可编辑 `TextBox` 在非组合态下同样受益于选区按 run 对齐（IME 组合期间 `activeSpans` 翻 `false` 导致底色块暂隐、提交后恢复，显式登记为 `L44-1` 已知边界）。

---

## §1 输入与问题陈述

### 1.1 输入与用户拍板（2026-11-18）

- **能力缺口（#90 / REQ-21）**：`SpanStyle` 支持字体族、字重、斜体、字号与前景色，但缺乏 `background` 字段，导致行内 code 片段无法渲染底色块。
- **伴生缺口**：`TextBox::OnPaint` 在 `SpansActiveForLayout()` 激活时，选区高亮仍按基字体整段 `MeasureText`，未按 `lineRuns` 推进，导致混字号 / 粗斜体混排下选区框与实际字形错位。
- **用户拍板（2026-11-18）**：
  1. 绘制叠加顺序：`span background` → `selection` → `DrawText`；
  2. 底色块高度口径：行框全高（`y = lineY`、`height = lineH` / `lineHeight`）；
  3. 同步收掉 `TextBox` 选区高亮按 `lineRuns` 推进；
  4. **P1-2 拍板（方向 A）**：不加 `IsReadOnly()` 门控，维持 `SpansActiveForLayout()` 现行条件。

### 1.2 事实矩阵（源码对 HEAD `697ed9f` 亲核，2026-11-18）

| # | 事实 | 证据（文件:行） |
|---|---|---|
| **K48-1** | `SpanStyle` 含 `family`、`weight`、`foreground`、`italic`、`size` 五个 `std::optional` 字段，无 `background` | `ECDI/include/ECDI/Widget/TextSpan.h:15-23` |
| **K48-2** | `RunSlice` 含 `startCp`、`cpCount`、`width`、`font`、`color`，无底色字段 | `ECDI/include/ECDI/Widget/TextWidget.h:128-134` |
| **K48-3** | `BuildRunsForRange` 为唯一 run 组装点，间隙填 `std::nullopt`，span 填覆盖属性 | `ECDI/src/Widget/TextWidget.cpp:414-470` |
| **K48-4** | `LineBoxGuardActive()` 仅在改变解析 `Font` 时触发，纯 `foreground` 覆盖不触发；`background` 属纯着色属性，同理不触发 | `ECDI/include/ECDI/Widget/TextWidget.h:211-215` |
| **K48-5** | 产品码 `DrawText` 共 5 处：3 条 runs 循环（`TextWidget.cpp:760` / `:815`、`TextBox.cpp:1565`）+ 2 条纯文本（`:768` / `:826`） | `TextWidget.cpp:746-826` · `TextBox.cpp:1547-1568` |
| **K48-6** | `TextBox::OnPaint` 中选区高亮（①）先于文本（②）绘制，且 `hlMin` / `hlMax` 用 `TextWidget::m_style.font.value` 整段 `ctx.MeasureText`；其中选区求交吃归属口径 `ownEndCp`（`LineRange`），而 `lineRuns` 建到裁剪口径 `drawEndCp`（`LineDrawRange`），折行软断点行尾空格满足 `ownEndCp > drawEndCp` | `ECDI/src/Widget/TextBox.cpp:573-586` · `:1519-1568` |
| **K48-7** | `PaintContext::DrawRect(const Rect&, const Color&)` 在非 backdrop 模式走 `BlendAlphaSolid`（GDI `GDIBackend.cpp:742-746`）与 GL `SRC_ALPHA/ONE_MINUS`（`GLRenderer.cpp:285-286`）；TextBox 未设背景走 BackdropMode 预乘直写（`GDIBackend.cpp:718-739`）；默认主题 `DefaultTheme::GetTextBoxStyle()` 的 `selection` 为 `Color::FromRGBA8(173, 216, 230)`（`a = 255` 不透明，`DefaultTheme.cpp:37`） | `PaintContext.h:44` · `DefaultTheme.cpp:37` · `GDIBackend.cpp:718-746` · `GLRenderer.cpp:285-286` |
| **K48-8** | `TextBox::SpansActiveForLayout()` 仅检查 `HasActiveSpans() && m_echoMode == EchoMode::Normal && !m_isComposing`，不含 `IsReadOnly()`；`TextBox` 无 `SetStyledText` override；`ClampAndValidateSpans` 整体拷贝 `TextSpan` | `TextBox.h:211-213` · `TextWidget.cpp:252-292` |

### 1.3 靶子现象

| 编号 | 现象 | 证据链 | 判定 |
|---|---|---|---|
| **R48-A** | 行内 code 无法渲染底色块：`SpanStyle` 与 `RunSlice` 均无 `background` 字段，3 条绘制通路均无底色 `DrawRect` | K48-1 / K48-2 / K48-5 | **能力缺口**（#90 / REQ-21） |
| **R48-B** | `TextBox` 富文本混排时，选区高亮框宽度按基字体估算，与混排文字（如不同字号/族/粗斜体）的真实 X 起止位置不一致 | K48-6 | **伴生缺陷**（#90 同步收口） |

### 1.4 消费者证据矩阵

| 消费者 | 用法 | 本 Phase 之后 |
|---|---|---|
| **harness（REQ-21）** | 行内 code 片段传 `SpanStyle::background`（如浅灰底色） | 三条绘制通路均准确绘制行框等高底色块；`TextBox` 选中时文字位于选区与底色之上保持可读，且选区左右边界与混排文字对齐（跨仓 `--md-probe` 三相回归验证） |
| **既有无 span / 无 background 控件** | 全部现有 `Label` / `Button` / `TextBox` | **逐位不变**（红线 1） |

---

## §2 方案选型

| 方案 | 内容 | 改动面 | 判定 |
|---|---|---|---|
| **A 扩维复用（采纳）** | `SpanStyle` 末尾追加 `background`；`RunSlice` 同步携带 `background`；三条绘制通路在 `DrawText` 前按行框高画 `DrawRect`；`TextBox` 选区计算在 `activeSpans` 时按 `lineRuns` 推进（超 `drawEndCp` 归属尾部用基字体补测） | `TextSpan.h`、`TextWidget.h/.cpp`、`TextBox.cpp`（零后端/命令流改动） | ✅ 符合红线 1–4，实现最小且双后端天然一致 |
| **B 独立绘制或后端扩展** | 新增带底色文本命令或自研渲染路径 | 破坏红线 3 | ❌ 过度设计，无必要 |

---

## §3 核心设计决策（用户已拍板）

### D48-1 · 字段扩展与兼容性

- `SpanStyle`（`TextSpan.h`）**结构体末尾**追加：`std::optional<Color> background = std::nullopt;`。
  - 仓内存在 30+ 处 designated initializer 调用点，受 MSVC `C7560` 声明序约束，追加于结构体末尾是唯一安全位；`operator==` 自动覆盖新字段。
- `RunSlice`（`TextWidget.h`）同步追加 `std::optional<Color> background = std::nullopt;`；`BuildRunsForRange` 在 gap 处填 `std::nullopt`，在 span 处填 `span.style.background`。
- `background` 与 `foreground` 同属纯着色属性，**不触发** `LineBoxGuardActive()`。

### D48-2 · 底色块几何口径

- 水平方向：以该 run 的绘制起点 `runX`（或 `curX`）为左边界，以 `run.width`（推进宽）为宽度。
  - 已知形态边界（P2-3）：斜体末字 ink 可能轻微探出 `run.width` 右边界 1–2px，本 Phase 维持按推进宽绘制底色，不额外按 overhang 扩宽矩形（与 `#92` 记账联动）。
- 垂直方向：统一取**行框高度**（`TextWidget` 中 `y = lineY` / `startPos.y`、高为 `lineHeight`；`TextBox` 中 `y = lineY`、高为 `lineH`），不随单个 run 的字号起伏，保证同行内多个 span 底色块顶底齐平。
- 绘制条件：仅当 `run.background.has_value()` 且 `run.cpCount > 0` 且 `run.width > 0.0f` 时发射 `ctx.DrawRect(...)`。

### D48-3 · 绘制层级顺序与前提

- 在 `TextBox::OnPaint` 每行循环内固定为：
  1. **Span 底色块**（`activeSpans` 下逐 run 绘制 `run.background`）；
  2. **Selection 选区高亮**（`HasSelection()` 时绘制 `m_style.selection.value`）；
  3. **行文本 `DrawText`**（逐 run 或整行绘制，文字位于最上层保证可读）；
  4. **组合串下划线**（`m_isComposing` 时绘制）。
  - 说明（P1-3）：默认主题 `selection` 的 alpha 为 `255`（不透明），默认配置下选区直接覆盖底色；仅当外部使用半透明 `selection` 且处于非 backdrop 混合路径（GDI `BlendAlphaSolid` / GL `SRC_ALPHA`）时才透出底色。因此验收判据以**命令发射顺序**与**文字在选区之上可读**为准，不以混合色像素值做硬判据。
- 在 `TextWidget::DrawTextContent` 中，同样固定**先画整行 `run.background`，再画整行 `DrawText`**，彻底避免相邻斜体字形被后一 run 底色块压住。
- 结构前提（P2-1）：`TextBox::OnPaint` 中 `activeSpans` 判定与 `lineRuns` 构建必须从原先的 ②（文本绘制）上移到行循环头部（① 选区之前），确保每行最多调用一次 `BuildRunsForRange`，严禁选区与文本阶段重复构建。

### D48-4 · `TextBox` 选区高亮按 run 推进（含折行归属尾部）

- 当 `!activeSpans` 时：维持原 `ctx.MeasureText(TextWidget::m_style.font.value, ...)` 逻辑，保证红线 1 逐位等价。
- 当 `activeSpans` 时：复用本行 `lineRuns` 计算 `hlStartCp` 与 `hlEndCp` 对应的逻辑 X 偏移（`hlMin` / `hlMax`），与文本绘制的 `runX` 累加严格同源：
  - `lineRuns` 仅覆盖 `[startCp, drawEndCp)`，若选区界落在 run 内部（`run.startCp < targetCp < run.startCp + run.cpCount`），仅对该 run 内部前缀子串按 `run.font` 调一次 `measurer.MeasureText` 补足局部偏移；整 run 位于目标之前时直接累加 `run.width`。
  - 若折行态软断点行尾分隔空格入选区，出现 `hlEndCp > drawEndCp`（或 `hlStartCp > drawEndCp`），超出 `drawEndCp` 的 `[drawEndCp, targetCp)` 区间（该段不发射文本）用基字体 `TextWidget::m_style.font.value` 补测一次 `MeasureText` 并累加，确保与旧行为归属口径完全一致。

---

## §4 边界与不变量

- **C48-1（零开销与逐位等价）**：`background` 全为 `std::nullopt` 时，不发射任何 `DrawRect` 命令，且命令流与改前逐位一致。
- **C48-2（不改布局尺寸）**：`background` 不改变 `DoMeasureText`、`BuildTextLayout` 的断行位置、行宽或行高。
- **C48-3（缓存失效）**：`SetStyledText` 更新 `m_spans` 时已对比 `m_spans == spans` 并递增 `m_styledTextRevision`，仅修改 `SpanStyle::background` 也能正确触发单行 `m_prefRuns` 与 `TextBox` 行表失效重绘。
- **C48-4（行内层级顺序）**：`TextBox` 每行内所有 `span background` `DrawRect` 均先于 `selection` `DrawRect`，两者均先于该行 `DrawText`。
- **C48-5（选区精确性）**：混排 `TextBox` 选中任意跨 run、run 内子区间或折行含行尾空格归属区间时，选区矩形的左右 X 坐标与对应字符/归属区间的渲染 X 一致。
- **C48-6（可编辑组合边界）**：按 `P1-2` 方向 A，可编辑 `TextBox` 在 `m_isComposing == true` 期间 `SpansActiveForLayout()` 为 `false`，底色块在组合态暂隐、提交后恢复，归 `L44-1`。

---

## §5 验收测试蓝图（初步设计阶段细化）

| 用例 | 验证目标 |
|---|---|
| **T48-1** | `SpanStyle::background` 设值后，`Label` 单行与折行（含软断点将同一 span 切分到两行各画一块底色）均在 `DrawText` 前发射对应 `DrawRect(Rect{x, lineY, run.width, lineHeight}, bg)`，并覆盖跨 DPI（如 1.0x / 1.5x）边界对齐 |
| **T48-2** | 混合字号 / 斜体同行绘制时，所有 span 底色矩形顶底齐平（同 `lineY`、同 `lineHeight`），且全行底色矩形先于 `DrawText` 发射 |
| **T48-3** | 无 `background` 的 `SpanStyle{}` 与零 span / 改前基线录制命令流逐位对拍一致（C48-1，沿 `T45-5` 手法） |
| **T48-4** | 只读 `TextBox` 设 `background` span 且有选区时，本行内所有 `span background` `DrawRect` 均先于 `selection` `DrawRect`，两者均先于该行 `DrawText`（不受控件外框/背景 `DrawRect` 干扰） |
| **T48-5** | 只读 `TextBox` 混字号/粗体下，选中跨 run 区间及 run 内部分码点时，`selection` `DrawRect` 的 X 与 `width` 精确匹配按 `run.font` 累加的偏移 |
| **T48-6** | 仅变更 `SpanStyle::background` 再次调用 `SetStyledText` 时，`m_prefRuns` 缓存正确刷新，新底色生效 |
| **T48-7** | 折行态 `TextBox` 选中含软断点行尾分隔空格（`ownEndCp > drawEndCp`）时，`selection` 右界正确覆盖至空格末尾，不缩回最后一个可见字符 |
| **跨仓回归** | `ECDI_fake_harness --md-probe` 的 `inline-code` / `inline-mixed` / `inline-code-literal` 三相回归通过（REQ-21 判据 ③） |

---

## §6 修订记录

- **v1.1（2026-11-18）**：吸收外部评审 `P1-1..4` 与 `P2-1..5` 及用户对 `P1-2`（方向 A）的拍板：① 补齐折行归属尾部 `hlEndCp > drawEndCp` 基字体补测规约与 `T48-7`（`P1-1`）；② 明确不加 `IsReadOnly()` 门控并登记组合态暂隐边界（`P1-2`）；③ 修正默认主题 `selection` `a=255` 与后端混合口径描述（`P1-3`）；④ 补齐软折行跨行、跨 DPI、逐位对拍与跨仓 `--md-probe` 验收项（`P1-4`、`P2-5`）；⑤ 登记 `lineRuns` 上移、`C7560` 末尾追加理由、斜体右探出不扩矩形及 `T48-4` 行内断言口径（`P2-1..4`）。
- **v1.0（2026-11-18）**：初版落盘（#90 / REQ-21），完成 HEAD `697ed9f` 源码事实核验（`K48-1..7`）及用户三项拍板落地（`D48-1..4`）。
