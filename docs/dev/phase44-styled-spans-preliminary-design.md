﻿# Phase 44 · 段内内联样式（styled spans，#88）· 初步设计稿

> 状态：**v1.4（2026-11-07）：评审 PASS / 已进详设**。★ **条 74：状态由评审结论决定**——v1.0 评审（2×P0 / 5×P1 / P2）由 v1.1 吸收，v1.1 复审（3×P1 / P2）由 v1.2 吸收，v1.2 复审（1×P1 / 5×P2）由 v1.3 吸收；v1.3 复审结论 = 上轮 6/6 全到位、真门禁通过，仅余 2 个新 P1（P1-A `docs=215` 已含本初设稿勿重复加计；P1-B `SetStyledText` 6 步序须把 `MarkTextChanged()` 提到钳制前并钉死独立单趟 `DecodeUtf8` 码点口径）及 5 项 P2，本版 v1.4 全部修毕并经 v1.4 复审 PASS。上游 = 需求稿 **v1.3**（`phase44-styled-spans-requirements.md`；四道闸门全部裁决：闸门 1 = 确认 ⓐ 原子入口 `SetStyledText`，结构排除 size · 闸门 2 = 暂缓斜体记账 L44-8 · 闸门 3 = 锁同字号 · 闸门 4 = 拆两问：4-① 收只读 TextBox 显示接入，4-② 编辑器通路记账 L44-1；O44-1..4 勘察亲核落地）。
> 触发来源 = 消费工程 `ECDI_fake_harness` 能力需求账本 **REQ-20**（`D:\Project\ECDI_fake_harness\docs\ecdi-capability-requests.md:895-912`，v5.23）。
> 全部行号对 HEAD `216aa9f` 亲核（源码对 `c137400` 亲核且此后源码未变：`git log c137400..HEAD -- ECDI/` 为空）；基线：用例 **588**、公共头 **101**、设计文档 **215**（★ **已含本初设稿**：需求稿 v1.3 时为 214，本稿入库已将三处载体锚点升至 215；后续详设稿立项时再由 215 升至 216）。
> ★ 本稿重点：在红线 1–4 严格约束下，给出「方案 A run 编排层」的公共类型契约、布局状态机单调推进算法、基线定位取舍（同族基线重合 / 跨族顶边对齐降级）、无侵入串内容身份自检护栏（C44-8）与只读 TextBox 接入面；逐行 diff 归详设。
> ★★ **门禁机制与阳性对照执行记录（条 76）**：`doc_lint.py:101-108` 用 `git ls-files` ⇒ untracked 新稿不在扫描集内，故本稿必须先 `git add docs/dev/phase44-styled-spans-preliminary-design.md` 入索引（纳入 **765** tracked 文件；工作区 `?? audit/` 为外部审计暂存目录，不属本 Phase 亦不入索引），再执行列举式门禁：`python tools/doc_lint.py --select L03,L04,L05,L06,L07,L08,L09,L11,L13` ⇒ **0 defect, 0 style**（tests=588 headers=101 docs=215，UTF-8 BOM ✓、LF ✓）。**阳性对照**：篡改载体 `README.md:22` 的 `215 design documents` 为 `214`，重跑 `python tools/doc_lint.py --select L13` ⇒ **恰报 1 条 DEFECT L13（`README.md:22`），退出码 1**；逐字节还原后复跑归零（0 defect 必配对照才算结论）。

**红线（承需求稿四条，此处落成可判定工程形式）**

1. **零 spans 路径逐位等价**：未设 / 清空 spans 或失活时，命令流 / preferred / wrap 布局 / 缓存行为与今日**逐位不变**（结构性保证）；既有文本域用例（Phase 28/29/31/36/38/40/41 全部 588 条）**一行不改仍绿**。
2. **测画同源且二次命中零测量**：每 run 的宽来自同一 `TextMeasurer` 实例的既有求值路径；`emitLine` 回退补测站（`TextWidget.cpp:339`）同改（K18②）；布局缓存持有 run 度量，绘制期零 measurer 调用（满足 C44-6 / T44-4）。
3. **命令流与 DrawText 执行体零改动**：`DrawTextCommand` / `RenderCommand` variant / `RecordingBackend` **零增删**；`GDIBackend` / `GLRenderer` 的 `DrawText` 执行体一行不动——混排靠「每 run 一条既有 DrawText 命令」，不让后端做排版（`RenderCommand.h:25` D9 继承）；`Font` 结构体零新增字段（斜体暂缓）。
4. **不借机扩到编辑器通路**：可编辑 `TextBox` 的光标移动 / 选区编辑 / IME 候选框跟随 / Undo 全通路本期**不做**（记账 L44-1，Phase 29 先例）；★ **只读 TextBox 的显示接入**本期兑现（闸门 4-①，消费者主路径 K17）。

---

## §1 核心机制与勘察落地

### 1.1 方案 A run 模型与编排层

今日 ECDI 文本渲染管线每一层都是「单 font」，但每一层底层接口都已经按 `Font` 参数化：
- `TextMeasurer::MeasureText(const Font&, ...)`
- `TextMeasurer::FitText(const Font&, ...)`
- `PaintContext::DrawText(Point, text, Color, Font)`
- `DrawTextCommand{ Point, text, Color, Font }`

缺的不是测量与绘制能力，而是**「把一串文本按区间拆解并逐段单 font 编排」的协调层**。方案 A 在 `TextWidget` 基类增加 span 数据结构与分段切片逻辑：
1. **数据层**：`TextWidget` 持有一组有序、不重叠的 `TextSpan`（码点区间 + 样式覆盖）。未覆盖区间以基类 `m_style`（基字体与基颜色）自动补齐，形成逻辑上完整覆盖 `[0, totalCp)` 的 run 序列。
2. **布局层**：`BuildTextLayout` 在遇到换行或硬边界计算时，在既有状态机内按 run 切片，逐 run 调用既有单 font 测量接口。
3. **绘制层**：`DrawTextContent` 遍历断行结果与 run 切片，每 run 发射一条**既有** `DrawTextCommand`。

零 span 或 span 失活时，编排层自动退化为单一 run 覆盖全串，完全走既有单 font 路径，红线 1 结构性保证。

### 1.2 O44-1 落地：行高单源与基线垂直定位决策闭合（P0-1 / P1-1）

#### 1.2.1 勘察事实与数值探针实测
* **行高接口**：`TextMeasurer` 纯单 Font，只暴露 `LineHeight(font)`，**接口零增量**（红线 3 / D40-G 约束）。接口中**不存在** `Ascent` / `Descent` 虚方法。
* **GL 基线机制**：`GLRenderer.cpp:254` 执行体计算公式为：
  ```cpp
  const float baseline = std::floor(pos.y) + std::floor(m_fontEngine->Ascent(font));
  ```
  其中 `FontEngine::Ascent` 读的是字面的 `face->size->metrics.ascender`（`FontEngine.cpp:664`），face 由 `FaceIdFor(family, weight)` 选出并缓存（`:650/:657`）。
* **GDI 基线机制**：`GDIBackend.cpp:923` 默认 `TA_LEFT | TA_TOP`，`pos.y` 直接作为 `TextOutW` 的字符单元格（cell）顶边。
* **Windows 系统字体数值探针（实测 @20px DIP, 96 DPI）**：
  * **Segoe UI Regular**（`segoeui.ttf`, upem=2048, hhea asc=2210）⇒ `Ascent` = 21.58px（`floor` = 21px）；
  * **Segoe UI Bold**（`segoeuib.ttf`, upem=2048, hhea asc=2210）⇒ `Ascent` = 21.58px（`floor` = 21px）；
  * **Microsoft YaHei Regular / Bold**（`msyh.ttc` / `msyhbd.ttc`）⇒ `Ascent` 指标完全同度量；
  * **Consolas Regular**（`consola.ttf`, upem=2048, hhea asc=1521）⇒ `Ascent` = 14.85px（`floor` = 14px）；
  * **Consolas Bold**（`consolab.ttf`, upem=2048, hhea asc=1521）⇒ `Ascent` = 14.85px（`floor` = 14px）。
* **探针核心事实剖析（概念厘清）**：
  1. **同族内换 weight（如 Segoe UI 常规 vs 粗体）**：字面间 ascender 严格相等（均为 21.58px），顶边对齐下**基线天然重合（0px 偏差）**，两链绘制基线完全一致；
  2. **跨族混排（如 Segoe UI 混排 Consolas 行内代码）**：ascender 存在 **7px** 巨大差异！
     * 在 GL 链内，Consolas run 的基线计算因其自身 `Ascent(font)` 较小，导致同一行内不同 run 之间出现 7px 的**行内基线偏置（intra-chain inter-run baseline offset）**；
     * 在 GDI 链内，两族以 cell top 顶边对齐，文字视觉基线亦因字体自身度量出现位移；
     * 此 7px 差异的根源是**两字族的固有度量不同**，而非 GDI 与 GL 之间的后端渲染分歧。顶边对齐 ≠ 基线对齐。

#### 1.2.2 基线垂直定位决策闭合（候选 ④ 采纳与取舍明确）
1. **候选 ①：给 `TextMeasurer` 加 `virtual float Ascent(const Font&) = 0;`**
   * *代价*：破坏 `TextMeasurer` 接口 +0 与 D40-G 兼容契约；且 GLRenderer 内部写死了 `pos.y + Ascent(font)`，即使布局层算出基线，GDI 侧因 `TA_TOP` 顶边语义仍无法单源对齐。
2. **候选 ②：后端统一基线渲染（修改 DrawText 执行体契约）**
   * *代价*：直接破坏红线 3（DrawText 执行体一行不动），引发全仓已有文本控件坐标系的破坏性重基线。
3. **候选 ③：限制 `SpanStyle` 不允许覆盖 `family`**
   * *代价*：直接掐死 REQ-20 核心需求（行内 `code` mono 字体），方案直接不可用。
4. **候选 ④（采纳）：行高定为基控件字体，同族基线严格重合；跨族按顶边对齐，显式降级 REQ-20 判据 ① 并记账 L44-9**
   * *闭合论据与取舍声明*：
     1. 行高唯一来源恒等于 `measurer.LineHeight(m_style.font.value)`，保证多行行框等距、`totalHeight` O(1) 乘法成立、`TextBox` 单值 `lineH` 光标系统完全兼容；
     2. REQ-20 最主要用例（粗体/常规体混排）由于同族 ascender 相等，基线绝对自然重合（0px 偏差）；
     3. 跨族行内代码（Consolas）按顶边对齐发射（`pos.y = lineY`），两后端均保持原生单元格顶边对齐排版语义，不破红线 3，不破 `TextMeasurer` 接口 +0；
     4. ★ **契约取舍闭合**：**显式声明 REQ-20 判据 ①「两链一致」在跨族混排（混 family）场景降级**为「两链统一按单元格顶边对齐，接受 7px 行内基线偏置」，并通过记账项 **L44-6** 提交外部消费工程 `ECDI_fake_harness` 书面签署确认；
     5. **基线验收口径纠正（条 76）**：I44-7 只测行高、T44-9 只测宽与断行，不得作为基线一致性证明；基线一致性仅承诺同族内严格对齐，跨族偏差单设探针（T44-11）实测锁定。

### 1.3 O44-4 落地：逐 run 测量形状与单调性守护算法（P0-2）

#### 1.3.1 v1.0 算法漏洞剖析
* `FontEngine.cpp:595` 的 advance 累积循环上界是 `cps.size()`（全串），`TextMeasurer.h:102/:164` 的二分查找上界是 `totalCp - startCp`；
* 若直接在 `curCp` 调用 `FitText(runFont, text, curCp, remWidth, ...)`，当 `remWidth` 超过当前 run 宽度时，`FitText` 会把后续 run 的字符按当前 run 的字体错误地计算 advance；
* 且 `remWidth -= runWidth` 中若 `runWidth` 取自跨 run 的 `fit.width`，则后续宽度扣减直接污染。

#### 1.3.2 采纳修法：先测全段宽，再按需 Fit（单调性守护）

##### 数据结构与 TextLine 扩展落点（P2-5 精确定位）
为了持有断行后的 run 切片并在绘制期实现零测量（红线 2），在 `ECDI/include/ECDI/Widget/TextWidget.h:118-126`（`protected` 区）定义内部轻量切片结构体 `RunSlice`，并将其作为末尾字段追加至 `TextWidget::TextLine`（注：`TextBox.h` 无 `TextLine` 类型，`TextBox` 继承 `TextWidget` 直接复用基类 `protected` 的 `TextLine`）：
```cpp
/// @brief 内部断行切片（纯数据，持有布局期测得的几何度量；落点：TextWidget.h protected 区）
struct RunSlice {
	std::size_t startCp = 0;   ///< 切片起始码点
	std::size_t cpCount = 0;   ///< 切片码点长度
	float       width   = 0.0f;///< 布局期测量所得宽度（严格同源）
	Font        font{};        ///< 本切片实际生效字体
	Color       color{};       ///< 本切片实际生效前景色
};
```
在 `TextWidget::TextLine`（`TextWidget.h:118-126`）结构体中，`std::vector<RunSlice> runs;` 作为末尾成员追加（紧随 `:122` 的 `ownedEnd` 之后），存量聚合初始化（C7560）不受任何干扰。

##### 推进算法状态机
在 `BuildTextLayout` 内部，从未消费码点 `curCp` 开始推进当前行，令当前 run 在本行内的未消费切片为 `[curCp, runEnd)`（码点长度 `runLen = runEnd - curCp`）：

1. **第一步（先测全段）**：
   提取当前 run 切片的 UTF-8 子串 `runSubstr = text.substr(b_cur, b_end - b_cur)`；
   调用 `const float runWidth = measurer.MeasureText(runFont, runSubstr).width;`（每 run 仅调一次）。
2. **第二步（单调性分支）**：
   * **分支 A（整 run 放得下）：`runWidth <= remWidth`**
     * 说明当前 run 剩余切片能完全容纳在本行内！
     * 直接消费整个切片：记录切片宽度为 `runWidth`，码点推进 `curCp = runEnd`，剩余预算 `remWidth -= runWidth`；
     * 切片压入当前行的 `runs`；若 `remWidth > 0` 且本行未完，流转至下一个 run。
   * **分支 B（硬边界落在此 run 内）：`runWidth > remWidth`**
     * 前缀宽度单调性定理（`TextMeasurer.h:99-100`）：前缀越长宽越大；
     * 因为已知长度为 `runLen` 的全切片宽度 `runWidth > remWidth`，故在 `curCp` 处调用 `measurer.FitText(runFont, text, curCp, remWidth, cps, byteOffsets)` 产出的 `fit.fitCp` **在数学上严格满足 `fit.fitCp < runLen`**！
     * **绝不可能跨过 `runEnd`**，测量范围 100% 封闭在当前 run 内部，绝对不污染后续字体！
     * 硬边界锁定为 `hardEnd = curCp + fit.fitCp`，记录该切片宽为 `fit.width`，本行 Fit 推进完成。
3. **第三步（emitLine 补测同源）**：
   * 软断点回退时（`end != fitEnd`），将 `[lineStart, end)` 按 span 拆分，逐 run 调用 `measurer.MeasureText` 求和；
   * 保证 `TextLine::width` 与各 `RunSlice::width` 之和严格同源相等（C44-2）；
   * 零 spans 时退化为单次测宽，红线 1 逐位成立；全程零局部表构造，`TextMeasurer` 接口零增量。

### 1.4 O44-3 落地：只读 TextBox 交互面

#### 1.4.1 事实确认
* `TextBox.h:191` `m_style`（`TextBoxStyle`）为 `protected`（注释明示「测试派生类可访问」），基类文字视觉经 `TextWidget::m_style` 访问。
* `TextBox::m_readOnly` 拦截所有编辑与 IME，保留滚动、选择与明文复制。
* `RecalculateLines()` 折行模式复用基类 `BuildTextLayout`；选区高亮在 `OnPaint` 计算 `hlMin` / `hlMax`。

#### 1.4.2 接入设计
1. **10 维自比键扩展为 11 维**：
   * 增加第 11 维 `m_linesSpanRevision = 0;`；
   * `LineKeyMatches` 与 `LineKeySave` 同步比对基类 `SpanRevision()`（`protected`），保证 span 变更触发行表重建。
2. **行文本绘制与高亮**：
   * `TextBox::OnPaint` 将单条 `ctx.DrawText` 升级为逐 run 发射，坐标以 `viewX + runX` 累加；
   * 选区高亮（`HasSelection()`）的 `hlMin` / `hlMax` 改为按 run 切片累加测宽，消除混排 mono 字体时的高亮块漂移。
3. **明文复制天然支持**：
   * `CopySelectionToClipboard` 纯取 `m_text` 明文（K11），零改动直接兑现 REQ-20 判据 ③。

### 1.5 O44-2 落地：斜体暂缓与推论澄清（闸门 2）

* **【设计推论】澄清**：Windows GDI `lfItalic = TRUE` 在字体缺少 Italic 面时强制由字体映射器进行**合成倾斜（synthetic shear）**，而 GL（FreeType）目前无斜体面时退回 Regular **不合成倾斜**（C36-4 粗体合成同族推论）。此为架构推论，非已测事实；引入斜体不仅面临全库 ≥8 类站点涟漪，更面临合成倾斜的跨链分歧。
* **裁决落地**：**闸门 2 裁定 ⓑ 暂缓斜体，记账 L44-8**。
* `Font` 结构体保持零新增字段，全库聚合初始化点（C7560）零受扰。

---

## §2 公共头与接口设计

### 2.1 新增公共头 `ECDI/include/ECDI/Widget/TextSpan.h`

```cpp
#pragma once

#include "ECDI/Core/Color.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ECDI {

/// @brief 段内样式覆盖（纯数据，值语义）
/// @details 闸门 3 落地：结构上排除 size（仅允许 weight / family / foreground 覆盖）。
///          未设置的字段（nullopt）继承基控件样式（TextWidget::m_style）。
struct SpanStyle {
	std::optional<std::uint16_t> weight;       ///< 字重（0-1000 刻度，如 700 粗体）
	std::optional<std::string>   family;       ///< 字体族（如 "Consolas"）
	std::optional<Color>         foreground;   ///< 前景色
};

/// @brief 段内文本样式区间（码点口径）
/// @details 区间为半开区间 [startCp, startCp + cpCount)。
///          必须满足：startCp 与 cpCount 均为码点索引（UTF-8 字符边界，非字节偏移）。
struct TextSpan {
	std::size_t startCp = 0;   ///< 起始码点索引（绝对坐标，相对全串）
	std::size_t cpCount = 0;   ///< 覆盖码点数
	SpanStyle   style{};       ///< 样式覆盖
};

} // namespace ECDI
```

### 2.2 `TextWidget.h` 公共与保护 API 规范

按照条 91 / 95⑤ 最小化公共接口，严格对齐需求稿主预算（**+1 公共方法（含右值重载）+ 1 新公共头（含 2 类型）**）：

```cpp
public:
	/// @brief 原子设置文本与样式区间表（闸门 1 确认 ⓐ）
	/// @details 流式 delta 整段覆盖与普通静态文本的唯一正规入口。
	///          清空样式语义并入 SetStyledText(text, {})。
	/// @param text  全串文本（UTF-8）
	/// @param spans 样式区间表（要求按 startCp 升序、互不重叠；越界部分内部自动钳制）
	void SetStyledText(const std::string& text, std::vector<TextSpan> spans);

	void SetStyledText(std::string&& text, std::vector<TextSpan> spans);

protected:
	/// @brief span 数据版本号（每次 SetStyledText 生效时递增）
	/// @details 仅供派生类（TextBox 11 维自比键）消费，不暴露为 public
	[[nodiscard]] std::size_t SpanRevision() const noexcept;
```

#### SetText 零改动与串内容身份自检护栏（C44-8，v1.3 P1 + v1.4 P1-B 顺序与码点口径订正）
需求稿 D44-1 明确裁定「SetText 本体零改动（红线 1 结构性最强）」且将「SetText 清 spans」列为已拒方案。为兼顾 D44-1 契约刚性与「防旧 span 误套新串」诉求，**`SetText` 的两个重载（`TextWidget.cpp:37` `const std::string&` 与 `:51` `std::string&&`）保持 100% 零改动**。

* **为何 `TextRevision()` 不是串身份（v1.3 P1）**：
  * `TextRevision()`（`TextWidget.h:146`）返回 `m_textRevision`，注释明示「`m_text` 每次变更递增」——它是**变更调用计数器**，而非串内容身份；
  * 全仓 `MarkTextChanged()` 共 13 处（`TextWidget.cpp:41/:55` 两个 `SetText` 重载 + `TextBox.cpp` 11 处编辑/组合/Undo），且 `SetText` 未做 `m_text == text` 短路，每次调用无条件递增 `m_textRevision`；
  * 若用 `m_styledTextRevision == TextRevision()` 判活，则调用 **`SetText(相同文本)`** 也会递增 `m_textRevision`，导致 `HasActiveSpans()` 误判 `false` 而静默丢掉样式（违反 `#85` 方法论：「宣判单变量因果前必须多测一臂『重复置同一个值』」）。
* **串内容身份自检护栏（String Content Identity Check，C44-8）**：
  1. `TextWidget` 内部维护私有成员 `std::string m_styledTextIdentity;`（与 Phase 40 `EnsureCpByteOffsets` 的 `m_cpByteTextSize` `TextWidget.h:188` / `TextWidget.cpp:271-283` 同族，但 `m_cpByteTextSize` 仅比长度会漏「同长不同串」，此处直接比串内容，`std::string::operator==` 先做 O(1) 长度短路再比字节）。
     * ★ **O(N) 内存成本显式登记（Phase 28 长文本口径，P2-②）**：当 `m_spans` 非空时，`m_styledTextIdentity` 持有一份 `m_text` 全串副本（1× 字符串拷贝与 O(N) 字节常驻成本）；当 `m_spans` 为空（全部 588 条存量用例及普通无样式控件）时，`m_styledTextIdentity` 为空串（0 堆分配）。该 O(N) 副本是在「`SetText` 100% 零改动（D44-1）」刚性约束下防止同长异串/哈希碰撞误贴的**正确性必需成本**，非投机冗余。
  2. **★★ 双重顺序敏感契约与码点数口径（C7560 同族「顺序即语义」，v1.4 P1-B 钉死）**：
     * **顺序陷阱剖析**：`CurrentByteMapKey()`（`TextWidget.cpp:259`）仅含 `m_textRevision`，且 `EnsureCpByteOffsets` 的串身份自检仅比字节长度 `m_cpByteTextSize != text.size()`（`:271`）。若在 `MarkTextChanged()` **之前**执行 spans 钳制且复用缓存表，当同字节长度但不同码点数的串切换时（如 `"abcd"` [4B, 4cp] → `"éab"` [4B, 3cp]），会命中旧串偏移表、按旧码点数错误钳制新 spans！同时，Phase 40 D40-J 存在 `SequenceLength`（`byteOffsets.size()-1`）与 `DecodeUtf8(text).size()` 两套码点数语义（脏字节串上可分歧）。
     * **严格 6 步序与码点口径规定**：
       1. `MarkTextChanged()` **必须紧随 `m_text` 赋值之后（Step 2）**，与 `SetText`（`TextWidget.cpp:39-41`）顺序严格同构，先使 `m_textRevision` 失效；
       2. `ClampAndValidateSpans(m_text, std::move(spans))`（Step 3）**显式规定为对 `m_text` 的独立单趟 `DecodeUtf8` 计数（`totalCp = DecodeUtf8(m_text).size()`），绝不读取任何依赖 `revision` 的缓存表**！这样 `TextSpan` 的 `[startCp, startCp + cpCount)` 与 `BuildTextLayout`（`TextWidget.cpp:307` `cps = DecodeUtf8(text)`）及 `TextLine::startCp / cpCount` **严格同处 `DecodeUtf8` 码点索引空间**，而在转换字节偏移时统一经 `OffsetAt` / `ByteOffsetAt` 钳制（D40-J / D-9），脏字节样本（T44-5）零分歧；
       3. `m_styledTextIdentity` **必须在 Step 1（`m_text` 更新）与 Step 3（`m_spans` 钳制）之后（Step 4）写入**（写反会捕获旧串立刻失活）。
     ```cpp
     void TextWidget::SetStyledText(const std::string& text, std::vector<TextSpan> spans) {
         m_text = text;                          // Step 1: 必须先更新 m_text（右值重载为 std::move(text)）
         MarkTextChanged();                      // Step 2: 立即 ++m_textRevision（与 SetText 同序，防旧 revision 缓存命中的顺序陷阱）
         m_spans = ClampAndValidateSpans(        // Step 3: 独立单趟 DecodeUtf8(m_text).size() 码点口径钳制，绝不读缓存表
                       m_text, std::move(spans));
         m_styledTextIdentity = m_spans.empty()  // Step 4: 必须在 Step 1/3 之后记录新串内容身份！
                                ? std::string{} : m_text;
         ++m_spanRevision;                       // Step 5: ++m_spanRevision（驱动同串仅换 spans 时缓存末维失效）
         Invalidate();                           // Step 6: 请求重绘（与 SetText D41-5 同口径）
     }
     ```
  3. 布局（`BuildTextLayout` / `DoMeasureText`）与绘制（`DrawTextContent` / `TextBox::OnPaint`）执行前，通过自检函数判断 spans 是否处于激活状态：
     ```cpp
     [[nodiscard]] bool HasActiveSpans() const noexcept {
         return !m_spans.empty() && m_styledTextIdentity == m_text;
     }
     ```
  4. **三臂完备闭环与「失活可逆」显式语义（`#85` 三臂方法论 + P2-① 钉死）**：
     * **臂 A（`SetText(异值)` 换串 ⇒ 失活）**：外部或派生类调用 `SetText(B)` 写入不同文本后，`m_styledTextIdentity != m_text` ⇒ `HasActiveSpans()` 严格求值为 `false`，自动退化为零 spans 单字体路径，旧区间绝不误贴新串；
     * **臂 B（`SetText(同值)` 重设同串 ⇒ 不失活）**：调用 `SetText(A)` 重复写入相同文本时，虽 `m_textRevision` 递增，但 `m_styledTextIdentity == m_text` 依然为 `true` ⇒ `HasActiveSpans()` 保持 `true`，样式完好保留；
     * **臂 C（`A → B → A` 换回原串 ⇒ 失活可逆 / 显式清空对比）**：因 `SetText` 零改动不擦除 `m_spans` 与 `m_styledTextIdentity`，**失活是门控级、可逆的（数据保留）**——若 `SetStyledText("A", spans)` 后调 `SetText("B")`（失活），再调 `SetText("A")` 换回原串，因内容重新匹配 `"A"`，原 spans 会重新激活；**若调用方需要永久清除 spans，必须遵循闸门 1 ⓐ 契约显式调用 `SetStyledText(text, {})`**（此时 `m_spans` 与 `m_styledTextIdentity` 均被清空，后续无论如何 `SetText` 均不再激活）。

---

## §3 布局与绘制状态机

### 3.1 缓存指纹与生命周期

```
SetStyledText(text, spans) [严格 6 步序]          SetText(newText) [既有零改动路径]
   │                                                │
   ├─► 1. m_text = text (或 move)                    ├─► 1. m_text = newText
   ├─► 2. MarkTextChanged() [++m_textRevision]      ├─► 2. MarkTextChanged() [++m_textRevision]
   ├─► 3. m_spans = 独立DecodeUtf8口径钳制(spans)    └─► 3. Invalidate()
   ├─► 4. m_styledTextIdentity = m_spans空? "":m_text       │
   ├─► 5. ++m_spanRevision                                  ├─► 臂A (newText != Identity): HasActiveSpans()==false ⇒ 失活退化单run
   └─► 6. Invalidate()                                      ├─► 臂B (newText == Identity): HasActiveSpans()==true  ⇒ 同值保持不丢
                                                            └─► 臂C (A→B→A 换回原串):      失活可逆重新激活（永久清除须调 SetStyledText(t,{})）
```

### 3.2 缓存指纹维扩展对照

| 缓存位置 | 今日键维度 | 改后键维度 |
|---|---|---|
| `TextWidget` Preferred 缓存 | `(revision, weight, size, family, dpi)` | `(revision, weight, size, family, dpi, spanRevision)` |
| `TextWidget` Wrap 缓存 | `(revision, weight, size, family, dpi, width)` | `(revision, weight, size, family, dpi, width, spanRevision)` |
| `TextBox` 10 维自比键 | `(revision, font, dpi, wrapWidth, measurer, wrapReq, echo, comp...)` | 原 10 维 + **第 11 维 `m_linesSpanRevision`** |

---

## §4 契约追溯与机检清单（I44）

### 4.1 C44 契约与测试追溯双向映射表（P1-3 / P1-5 / v1.3 P1 / v1.4 P1-B）

| 不变量 | 契约内容 | 守护用例 / 机制 | 判定依据 |
|---|---|---|---|
| **C44-1** | 零 spans 路径逐位等价 | T44-6 + 既有 588 用例 | 命令流逐字段相等、preferred 尺寸相等、wrap 布局相等 |
| **C44-2** | run 宽「测」与「画」同源，含回退站 | T44-1 / T44-2 / T44-3 | emitLine 补测求和，绘制 x 累积无缝贴合 |
| **C44-3** | span 边界与断行边界恒在合法码点边界 | T44-5（脏字节/Surrogate/越界） | 独立单趟 `DecodeUtf8` 码点口径钳制 + `ByteOffsetAt` 钳制，无非法切割 |
| **C44-4** | 禁则字符集与软断点分类零变化 | T44-2（含 CJK 禁则与闭标点悬挂） | 样式与断点分类正交，行为同构 |
| **C44-5** | 命令流只出现既有 `DrawTextCommand` | T44-3 / I44-2 | RecordingBackend 截获断言，variant 零改动 |
| **C44-6** | 指纹完备：变更 miss，二次命中零测量 | T44-4 / I44-6 | FakeTextMeasurer 计数，重绘零调用 |
| **C44-7** | [已出局] 串身份自检（键命中但串身份不符） | 随方案 ⓑ 降级出局 | 需求稿原 C44-7 绑定制备选 ⓑ，ⓑ 淘汰后本条正式退役 |
| **C44-8** | 串内容身份自检护栏（`m_styledTextIdentity == m_text`） | T44-4（#85 三臂：同值保留 / 异值失活 / 换回可逆与显式清空） | `SetStyledText` 严格 6 步序（先 `MarkTextChanged` 再独立钳制）；`SetText` 零改动下三臂语义闭合 |

### 4.2 机械机检表（I44，v1.4 P2-④ 精确化）

| # | 判据 | 检查命令形式（排除注释行与修改态干扰） | 期望 |
|---|---|---|---|
| **I44-1** | 存量全绿 | 运行既有 588 条测试用例 | **588 passed / 0 failed** |
| **I44-2** | 命令流零增删 | `git diff HEAD -- ECDI/include/ECDI/Render/RenderCommand.h` 过滤 `^\s*//` | **0 代码行变动** |
| **I44-3** | 后端执行体零改动 | `git diff HEAD -- ECDI/src/Render/GDIBackend.cpp ECDI/src/Render/GLRenderer.cpp`（因闸门 2 暂缓斜体，两文件整文件比对；若含注释则对 `GDIBackend::DrawTextImpl` [`:887-1000`] 与 `GLRenderer::DrawText` [`:230-303`] 过滤 `^\s*//`） | **0 代码行变动** |
| **I44-4** | Font 结构零改动 | `git diff HEAD -- ECDI/include/ECDI/Core/Font.h` | **0 行变动** |
| **I44-5** | 公共头净增量 | `git status --porcelain ECDI/include/ECDI` 过滤新增项 `^(\?\?\|A )`（排除 `M` 态的 `TextWidget.h`/`TextBox.h`） | **恰 1 个新文件 `Widget/TextSpan.h`** |
| **I44-6** | 二次重绘零测量 | T44-4 计数 Measurer 探针 | **首绘 >0，二次重绘 == 0** |
| **I44-7** | 行高单源性 | T44-1 高度与 `measurer.LineHeight(m_style.font.value)` 对比 | **误差 0.00 px** |

---

## §5 测试规划与判据矩阵

### 5.1 用例清单（11 条新用例，588 → 599）

| # | 用例名称 | 核心相 / 夹具 | 判据与断言 |
|---|---|---|---|
| **T44-1** | `TextWidget.StyledSpansMeasurement` | 三段混排「常规 + 粗体 + mono」 | `preferred.width == Σ run.width`；`preferred.height == measurer.LineHeight(baseFont)` |
| **T44-2** | `TextWidget.StyledSpansWrap` | 宽恰落在粗体 run 内部触发折行 | 行数、逐行 `startCp`/`cpCount`/`width` 正确；**含软断点回退相**；**禁则样本（CJK 开闭标点 + 闭标点悬挂）与单字体版完全同构** |
| **T44-3** | `TextWidget.StyledSpansCommandStream` | RecordingBackend 收命令 | 每 run 恰一条 `DrawTextCommand`；font/color 逐 run 匹配；x 累积等于前序 run 宽之和（C44-2） |
| **T44-4** | `TextWidget.StyledSpansCacheFingerprint` | 计数 FakeTextMeasurer 探针 + C44-8 三臂（#85） | ① `SetStyledText` 后首绘测宽、**二次绘制 0 次测量**（C44-6）；② **臂 B `SetText(同值)` 重设相**：样式保持激活不丢；③ **臂 A `SetText(异值)` 换串相**：spans 失活退化单字体基样式且 miss 重测；④ **臂 C `A→B→A` 换回相 vs `SetStyledText(A, {})` 清空相**：验证失活可逆与显式永久清空的区别；另覆盖同字节长异码点数（`"abcd"`→`"éab"`）钳制正确性（P1-B） |
| **T44-5** | `TextWidget.StyledSpansDirtyBoundaries` | CJK + surrogate pair + 非法字节 + 越界区间 | 独立 `DecodeUtf8` 码点口径钳制 + `ByteOffsetAt` 钳制（C44-3）；越界 span 自动钳制不崩溃、不越界 |
| **T44-6** | `TextWidget.StyledSpansZeroSpansEquivalent` | 未设 spans 与 `SetStyledText(text, {})` | 命令流逐字段相等、preferred 尺寸相等、wrap 布局结果相等（红线 1） |
| **T44-7** | `TextWidget.StyledSpansNegativeContrastStyle` | **负对照①（判别力）**：回退粗体 run 的 weight 为 400 | 恰 T44-1/3 的粗体断言红、其余绿（REQ-20 判据 ②） |
| **T44-8** | `TextWidget.StyledSpansNegativeContrastRunCount` | **负对照②（入库护栏）**：断言命令数 = run 数 ∧ font 不全相等 | 命令数断言抓未拆 run，font 断言抓拆了 run 但共用基字体的退化臂 |
| **T44-9** | `TextWidget.StyledSpansTwoChainConsistency` | 真窗口 GDI vs GL 同夹具（有真粗体面字体） | 逐 run 宽与断行位置两链一致，行高容差 ≤1px；无头 GL 探针按 `glReadPixels` 在 `SwapBuffers` 前采集；**显式记录 `-D_DEBUG` 启用态与显示器缩放比例** |
| **T44-10** | `TextBox.StyledSpansReadOnlySupport` | 只读 TextBox 设三段混排 spans | 文本真渲染（多 run 命令）；选区高亮矩形与混排字符贴合；`CopySelectionToClipboard()` 正确获取明文子串 |
| **T44-11** | `TextWidget.StyledSpansBaselineProbe` | 同族（Segoe UI Regular/Bold）与跨族（Segoe UI + Consolas） | 实测断言同族 ascender 严格相等；跨族断言指标存在偏置并量化记录 7px，锁定顶边对齐降级语义与跨族行内基线偏置（L44-9） |

### 5.2 T44-11 观测路径与判别力方向（P2-2 / v1.3 P2-3 措辞统一）
* **观测路径**：
  1. 单元测试夹具中直接通过内部测试接口访问 `FontEngine::Ascent(const Font&)`（`ECDI/src/Render/FontEngine.h:167`），读取 FreeType 实际载入的 `face->size->metrics.ascender`；
  2. 无头 GL 测试中通过 `glReadPixels` 获取渲染缓冲区像素分布，核验混排文本行底部渲染基线位置。
* **判据方向与断言逻辑（条 76）**：
  1. **同族断言（正向锁定）**：`EXPECT_NEAR(engine.Ascent(segoeRegular), engine.Ascent(segoeBold), 0.001f);`，严格断言同族不同字重基线 0 偏差；
  2. **跨族断言（反证并记录）**：断言 `std::abs(engine.Ascent(segoeRegular) - engine.Ascent(consolasRegular)) > 0.0f;`，反证并排除了「顶边对齐天然等价于基线对齐」的错误假设；输出并记录实测差值（~7.0px），作为**跨族行内基线偏置（intra-chain inter-run baseline offset）与顶边对齐降级事实（L44-9）**的回归锚点，不采用脆弱的绝对浮点硬编码相等断言。

### 5.3 REQ-20 验收映射表（P1-1 / v1.3 P2-4 逐字对齐需求稿 §1.1 原文）

| REQ-20 判据 | 判据原文（逐字引自需求稿 §1.1） | 落地用例 / 机制 | 验收口径与降级说明 |
|---|---|---|---|
| **判据 ①** | ECDI 侧单测：同一 Label 内粗体/常规/mono 三段混排，测量与折行正确、逐段样式生效（GDI/GL 两链一致） | T44-1 / T44-2 / T44-3 / T44-9 / T44-11 | **同族混排（粗体+常规体）基线 0 偏差、两链完全一致**；**跨族混排（混 family）显式降级**为「两链统一按单元格顶边对齐，接受 7px 行内基线偏置」（L44-9，经 L44-6 提交 harness 书面签署确认） |
| **判据 ②** | 判别力：回退任一段样式 ⇒ 恰该用例红 | T44-7（+ T44-8 入库护栏） | 两组负对照：回退粗体 weight 恰好 T44-1/3 红（T44-7）；未拆 run 或拆了 run 共用基字体退化恰好 T44-8 红 |
| **判据 ③** | 本仓消费后：转录段内粗体/斜体/行内 code 真渲染，选中/复制仍取块全文**（口径消费时定）** | T44-10（+ K11 明文复制通路） | 闸门 4-① 本期收只读 `TextBox` 多 run 真渲染与选区高亮逐 run 测宽贴合（T44-10）；`CopySelectionToClipboard()` 纯取 `m_text` 明文子串（K11），复制与样式正交；**「（口径消费时定）」逐字保留**；斜体由闸门 2 暂缓记账 L44-8，可编辑 `TextBox` 记账 L44-1 |
| **判据 ④** | 负对照：既有单字体路径（全部现行探针）不回归 | 红线 1 + C44-1 + T44-6 + I44-1 | 既有 588 条用例一行不改全绿；未设 spans / 空 spans / 换串失活与今日单字体路径命令流、preferred、wrap 布局逐位等价 |

---

## §6 影响面与分批实施计划

### 6.1 规模预算表（P2-4 口径统一 + v1.4 P1-A docs 计数订正）

| 维度 | 基线 | 变动后 | 差异说明 |
|---|---|---|---|
| 公共头 | 101 | 102 | + `ECDI/include/ECDI/Widget/TextSpan.h`（恰 +1 头） |
| 公共类型 | 基线 | +2 类型 | + `SpanStyle`, `TextSpan`（在 `TextSpan.h`） |
| 公共 API | 基线 | +1 方法（2 重载） | `SetStyledText(const string&, ...)` / `SetStyledText(string&&, ...)`；`SpanRevision()` 为 `protected` |
| 既有用例 | 588 | 588 保持全绿 | 存量零破坏 |
| 新增用例 | 0 | +11（588 → 599） | T44-1..11（含两负对照、只读相与基线探针） |
| 设计文档 | 214（本稿前） | **215（已含本初设稿）** | +1 本初步设计稿（已随本批续订三处锚点 214→215；后续详设稿立项时另 +1 升至 216） |
| `CMakeLists.txt` | 0 改动 | 0 改动 | GLOB_RECURSE 自动入库 |

### 6.2 分批实施计划

本 Phase 采用**两批实施**：

* **批 1：核心数据类型与 TextWidget 编排层**
  1. 新建 `TextSpan.h`（`SpanStyle`, `TextSpan`，含 `<cstddef>`）；
  2. `TextWidget` 增加 spans 成员、原子入口 `SetStyledText`（严格 6 步序：先 `MarkTextChanged()` 再独立 `DecodeUtf8` 钳制）、串内容身份自检 `HasActiveSpans()`（`m_styledTextIdentity == m_text`）、protected `SpanRevision()`（`SetText` 零改动）；
  3. `BuildTextLayout` 单调性守护算法实现；`TextWidget::TextLine`（`TextWidget.h:118-126`）末尾扩展 `runs` 字段；`emitLine` 回退补测站改造；
  4. `TextWidget::DrawTextContent` 逐 run 发射实现；
  5. 落地测试 T44-1..9 及 T44-11。
* **批 2：TextBox 只读显示接入与 REQ-20 验收收口**
  1. `TextBox` 11 维键扩展；
  2. `TextBox::OnPaint` 逐 run 绘制与选区高亮 run 累积改造；
  3. 落地测试 T44-10；
  4. 四链回归验证（MSVC / Clang / ClangCL / MinGW Release 599/599）；
  5. 代码收口锚点续订（续订 README 用例 588→599、公共头 101→102；本初设稿 docs 214→215 已兑现，详设稿立项时单列续订 docs 215→216）。

---

## §7 记账与被拒项

| # | 编号 | 内容 | 状态 / 处置 |
|---|---|---|---|
| 1 | **L44-1** | **编辑器通路**（可编辑 TextBox 光标/选区/IME/Undo × spans） | 闸门 4-② 排除，本期不做；重启条件 = 真实编辑器消费需求（Phase 29 先例） |
| 2 | **L44-2** | **混字号**（行高 max + 基线对齐） | 闸门 3 排除，本期锁同字号；重启条件 = 真实混字号混排需求 |
| 3 | **L44-3** | **行内 code 背景底片** | 既有 `DrawTextCommand` 背景透明，不扩命令原语；记账归 #53 渲染表现力群 |
| 4 | **L44-4** | **下划线 / 删除线 / 链接色** | 装饰原语归 #53 群，本期不做 |
| 5 | **L44-5** | **C36-4 粗体合成两链已知分歧** | GDI 合成 / GL 不合成；测试采用有物理粗体面的字体规避 |
| 6 | **L44-6** | **harness REQ-20 状态回写与降级签署** | 详设完工后回写 harness 账本，注明流式改用 `SetStyledText`，并就跨族基线 7px 偏置降级取得 harness 书面确认 |
| 7 | **L44-7** | **根 README Status 表 Phase 43 缺失** | 存量欠账，收口时一并补齐 |
| 8 | **L44-8** | **斜体样式维度与合成倾斜支持** | 闸门 2 裁决暂缓，记账后补；避免全库 8 类站点扩散与跨链合成歧义 |
| 9 | **L44-9** | **跨族混排（混 family）行内基线偏置与顶边对齐降级** | 实测 Segoe UI vs Consolas 7px；不破红线 3 与接口 +0，两后端均按 cell top 对齐，降级 REQ-20 判据 ① |

---

## §8 修订记录

- **v1.4（2026-11-07）★ v1.3 复审吸收（2×P1 / 5×P2 全收）；状态 = v1.3 复审意见已修，修后进详设（条 74）**：
  ① **P1-A（docs 计数去重）**：`docs=215` 已包含本初步设计稿（由需求稿时 214 升至 215），纠正 §6.1「215→216」与 §6.2「docs 215→216」的重复加计（改为本稿 214→215 已兑现，详设稿立项时再 215→216），防止落地时将锚点误写为 216 触发 L13 红；同步修正总账 #88 行。
  ② **P1-B（6 步序第二顺序陷阱与码点数口径钉死）**：指出 v1.3 将 `MarkTextChanged()` 放在 Step 4 会令 Step 2 钳制在同字节长不同码点数换串（`"abcd"` 4cp → `"éab"` 3cp）时命中旧 revision 偏移表；将 `MarkTextChanged()` 提至 **Step 2（紧随 `m_text` 赋值之后，与 `SetText` 同序）**，并显式规定 Step 3 `ClampAndValidateSpans` 采用**独立单趟 `DecodeUtf8(m_text).size()` 码点口径（绝不复用任何依赖 revision 的缓存表）**，保证与 `BuildTextLayout` 的 `cps` 索引空间严格一致、字节切片由 `OffsetAt`/`ByteOffsetAt` 钳制（D40-J），T44-4 补入 `"abcd"`→`"éab"` 验证。
  ③ **P2-①（`A→B→A` 失活可逆语义与 #85 三臂）**：在 §2.2 与 §3.1 显式声明「`SetText` 换串失活为门控可逆（数据保留，换回原串会重新激活；永久清除须调 `SetStyledText(text, {})`）」，T44-4 升级为 `#85` 三臂（同值保留 / 异值失活 / `A→B→A` 可逆 vs 显式清空）。
  ④ **P2-②（`m_styledTextIdentity` O(N) 内存成本显式登记）**：在 §2.2 登记 `m_spans` 非空时 `m_styledTextIdentity` 的 O(N) 全串副本开销（空 spans 时为 0），注明其为 D44-1「`SetText` 零改动」约束下防同长异串/哈希碰撞的正确性必需成本。
  ⑤ **P2-③（修订记录历史行号消歧）**：将 v1.3 修订记录 ④ 中的「`:297`」明确标注为「v1.2 稿 `:297`」。
  ⑥ **P2-④（I44-3 / I44-5 机检命令精确化）**：I44-5 补 `^(\?\?|A )` 过滤以排除实施期 `M` 态的 `TextWidget.h`/`TextBox.h`；I44-3 明确两后端文件 0 改动及 `DrawTextImpl` [`:887-1000`] / `GLRenderer::DrawText` [`:230-303`] 过滤注释口径。
  ⑦ **P2-⑤（工作区 `?? audit/` 说明）**：在稿首门禁节注明工作区 `?? audit/` 为外部审计暂存目录，不属本稿亦不入索引。
- **v1.3（2026-11-07）★ v1.2 复审吸收（1×P1 / 5×P2 全收）；状态 = v1.2 复审意见已修，待复审**：
  ① **新 P1（`HasActiveSpans()` 身份量从变更计数器订正为真实串内容身份，#85 方法论）**：改为持有 `std::string m_styledTextIdentity`，比对 `!m_spans.empty() && m_styledTextIdentity == m_text`。
  ② **P2-1（`SetStyledText` 内部顺序敏感契约，C7560 同族）**：显式规定写入顺序。
  ③ **P2-2（门禁节入正文与 `git add` 真实入索引）**：稿首补齐门禁与阳性对照专节，执行 `git add` 使扫描集升至 **765 tracked**。
  ④ **P2-3（措辞回扫残留，条 61①）**：清除 §5.1 T44-11 与 §5.2（v1.2 稿 `:297`）残留的「已知两链分歧」旧措辞。
  ⑤ **P2-4（§5.3 REQ-20 判据原文逐字对齐）**：恢复需求稿 §1.1 逐字原文。
  ⑥ **P2-5（`RunSlice` / `TextLine` 落点精确化）**：精确至 `TextWidget.h:118-126` `protected` 区。
- **v1.2（2026-11-07）★ 复审吸收（3×P1 / P2 全收）；状态 = 初设 v1.2 待复审**：
  ① P1-1（候选 ④ 取舍闭合与概念厘清）；② P1-2（SetText 零改动与串自检护栏初版）；③ P1-3（C44-7 编号退役与 C44-8 新立）；④ P2-1..4（门禁说明、T44-11 观测路径、RunSlice 定义、预算口径统一）。
- **v1.1（2026-11-07）★ 初审吸收（2×P0 / 5×P1 / P2 全收）；状态 = 初设 v1.1 待复审**：
  ① P0-1（基线定位重开决策点）；② P0-2（逐 run 推进跨 run 污染，单调性守护算法）；③ P1-1（回写四处载体）；④ P1-2（公共 API 收窄）；⑤ P1-3（SetText 护栏）；⑥ P1-4（推论澄清）；⑦ P1-5（判据补齐与双向映射）；⑧ P2 全项。
- **v1.0（2026-11-07）** 初步设计稿初稿：四闸门落实 · O44-1..4 落地 · 方案 A run 编排层 · 10 维自比键扩展 · 测试规划 10 条。不建议进详设（见 v1.1 评审意见）。
