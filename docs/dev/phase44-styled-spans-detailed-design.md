# Phase 44 · 段内内联样式（styled spans，#88）· 详细设计稿

> 状态：**v1.5（2026-11-08）：v1.4 复审结论 = 有条件通过、可进批一；本版订正 §3.3 REQ-20 判据原文（逐字对齐需求稿 §1.1 / 初设 §5.3）、补 §4 负对照执行项与 §0.2 算例口径，产品码与测试用例零改动**。★ **条 74：状态由评审结论决定**。
> 上游初步设计稿：`docs/dev/phase44-styled-spans-preliminary-design.md`（v1.4 评审 PASS / 互记勘误同步）。
> 触发来源：消费工程 `ECDI_fake_harness` 能力需求账本 **REQ-20**（`D:\Project\ECDI_fake_harness\docs\ecdi-capability-requests.md:895-912`，v5.23）。
> 全部行号对 HEAD `216aa9f` 亲核（源码对 `c137400` 亲核且此后源码未变：`git log c137400..HEAD -- ECDI/` 为空）；基线：用例 **588**、公共头 **101**、设计文档 **216**（含本稿）。
> ★ 本稿重点：在红线 1–4 严格约束下，给出段内内联样式全部新增公共头、数据结构、布局状态机单调推进算法、绘制命令发射、无侵入串身份护栏（C44-8）与只读 TextBox 接入的逐行实现，以及 11 条全覆盖测试用例的完整实现与注册语句。
> ★★ **门禁机制与阳性对照执行记录（条 76）**：
> `doc_lint.py:101-108` 使用 `git ls-files` 收集跟踪文件，故本稿须 `git add` 入索引（纳入 **766** tracked 文件；工作区 `?? audit/` 为外部审计暂存目录，不属本 Phase 亦不入索引）。
> 执行列举式门禁（逗号分隔，勿用 `..` 范围语法）：
> `python tools/doc_lint.py --select L03,L04,L05,L06,L07,L08,L09,L11,L13` ⇒ **0 defect, 0 style, 28 info**（tests=588 headers=101 docs=216，UTF-8 BOM ✓、LF ✓）。
> 注：L01 为全库编码检查，仓生存量历史文件存在无 BOM 样本，本 Phase 所属 dev 文档均已确保 UTF-8 BOM。
> **阳性对照验证**：篡改 `README.md:22` 的 `216 design documents` 为 `215 design documents`，复跑即刻捕获：
> `DEFECT L13  README.md:22  metric 'docs' anchor disagrees with measured value 216 (numbers on this line: 215)`（退出码 1）；逐字节还原后复跑恢复 0 defect。

---

## §0 详细设计意图与边界收口

### 0.1 核心设计意图与红线落实

1. **红线 1（零 spans 路径逐位等价）**：
   - 当控件未设置 spans、传入空 spans 列表，或经串身份自检发现 spans 失活时，执行逻辑 100% 走既有零样式路径；
   - `TextWidget::SetText` 的两个既有重载（`const std::string&` 与 `std::string&&`，`TextWidget.cpp:37-60`）保持 **100% 零修改**；
   - `BuildTextLayout` 中的非 spans 分支保持 100% 逐字保留原逻辑（`TextWidget.cpp:377-422`），`emitLine` 闭包签名保持四形参不变更；
   - `TextLine` 结构体默认初始化 `runs = {}`，既有代码构造保持逐位兼容。
2. **红线 2（测量与绘制同源 Measurer）**：
   - 布局测量与 `Paint` 绘制严格复用同一 `TextMeasurer` 实例；
   - 布局缓存 `m_wrapLayout` 与单行 runs 缓存 `m_prefRuns` 确保二次重绘时 0 次额外测量（I44-6 机检守护）。
3. **红线 3（执行体与公共头零侵入）**：
   - `DrawTextCommand` 结构体 0 改动；
   - 后端绘制执行体 `GDIBackend::DrawTextImpl`（`:887-1000`）与 `GLRenderer::DrawText`（`:230-303`）保持 **0 代码变更**；
   - 公共头 `Font.h` 0 改动，不引入斜体或字号扩展。
4. **红线 4（编辑器通路延后）**：
   - 可编辑 `TextBox` 的光标移动、IME 组合窗口、复杂选区仍维持基样式单字体度量，编辑器通路演进完整记入 `L44-1`；
   - 本期仅支持只读 `TextBox` 的混排展示、文本全选/部分选区高亮与无格式纯明文复制。

---

### 0.2 关键约束与复审意见深度收口说明（v1.4 闭合）

1. **断行推进与真实 FitText 签名对齐（P0-1 / P0-2 / P0-3 / P0-4）**：
   - `TextWidget.cpp:377-422` 替换块消除变量重声明与未定义符号（`limit` 真实存在，`hardEnd` 单一声明）；
   - spans 分支在需要对单个 run 进行部分折行时，严格调用既有 6 参签名：
     `measurer.FitText(run.font, text, hardEnd, remLimit, cps, byteOffsets)`，返回值取 `rFit.fitCp` 与 `rFit.width`（`TextMeasurer.h:26-30`，严禁虚构 3 参或 `fittedLength`）；
   - 测试夹具 `StyledSpansFakeMeasurer` 覆写真实 6 参 `FitText`；
   - spans 分支经 `BuildRunsForRange` 切片时，采用**显式传入局部 `byteOffsets` 引用与 `OffsetAt` 静态只读查表**，**严禁调用基类 `EnsureCpByteOffsets`**，严格守护 `TextWidget.cpp:314-318` 局部表落款。
2. **测试框架宏规范（P0 宏对齐）**：
   - `TestFramework.h:70-95` 仅支持 `EXPECT_TRUE`、`EXPECT_FALSE`、`EXPECT_EQ`、`EXPECT_NE`、`EXPECT_NEAR` 五种断言；
   - 全文严禁使用 `ASSERT_EQ`、`ASSERT_TRUE`、`ASSERT_GT`、`EXPECT_GT`；
   - 守卫型校验严格使用 `EXPECT_TRUE(cond); if (!(cond)) return;` 范式，失败即记账飘红且安全早退。
3. **真实头文件包含路径（P0 路径对齐）**：
   - `FontEngine.h` 位于 `ECDI/src/Render/FontEngine.h`，测试代码统一采用 `"Render/FontEngine.h"` 包含（与 `WidgetTests.cpp` 其他测试项完全一致，严禁写不存在的 `ECDI/Render/FontEngine.h`）。
4. **`TextBox` 选区与剪贴板真实 API（P0-6 对齐）**：
   - `SelectAll()` 与 `CopySelectionToClipboard()` 位于 `TextBox.h:466/475` 的 private 区；
   - 测试夹具 `TestableTextBox` 新增 `using TextBox::SelectAll;` 与 `using TextBox::CopySelectionToClipboard;` 暴露通道；
   - `T44-10` 调用真实 `box.SelectAll()`、`box.CopySelectionToClipboard()`，并通过差分断言（`selRectCount == baseRectCount + 1`）严密验证选区高亮矩形发射。
5. **T44-2 物理数值算例严密重构（P0-7 修复）**：
   - 算例采用不含行首禁则字符的对称样本 `"AA BBBB CC"`，可用宽设为 `limit = 50.0f`（AA=16, 空格=8, BBBB=40 粗体, 空格=8, CC=16）；
   - Line 0：消费 "AA "（24px <= 50px），因后续 "BBBB"（40px）超出 26px 剩余预算触发软断点；尾部空格裁剪后 Line 0 确定为 "AA"（16px），`startCp=0, cpCount=2, width=16.0f, ownedEnd=3`（单 run 400）；
   - Line 1：从 cp 3 开始，粗体 run "BBBB"（40px）整段装下（`accumW = 40`）；下一 run " CC"（24px）使 `40 + 24 > 50` ⇒ 以 `remLimit = limit - accumW = 10` 调用 6 参 `FitText`，按基字体吃下 1 个分隔空格（8px，`fitCp = 1`）得 `hardEnd = 8`；`FindLastSoftBreak` 返回空格后一位 8，尾部空白裁剪后 Line 1 确定为 "BBBB"（40px），`startCp=3, cpCount=4, width=40.0f, ownedEnd=8`（单 run 700）；
   - Line 2：从 cp 8 开始，消费 "CC"（16px <= 50px），Line 2 确定为 "CC"（16px），`startCp=8, cpCount=2, width=16.0f, ownedEnd=10`（单 run 400）；
   - 总行数恰为 3 行，所有数值在单调折行算法下物理完全自洽；
   - CJK 标点禁则悬挂在单独段落中验证：文本 `"一二。"` 设置 spans 后 limit=24px，`。` 悬挂进入上一行，实测 `lines.size() == 1, cpCount = 3`。
6. **BuildTextLayout 串身份护栏补齐（P1-1 修复）**：
   - `const bool hasSpans = HasActiveSpans() && (text == m_text);` 严正防止 `TextBox` 折行支调用 `BuildTextLayout(measurer, BuildDisplayText(), ...)` 传入掩码/密码串时，spans 错贴到显示串上导致切片越界。
7. **REQ-20 需求验收映射表恢复（P1-3 修复）**：
   - §3 完整恢复 REQ-20 四条判据的双向映射表，明确判据 ① 跨族偏置由 `L44-9` 登记，签署方为 harness（`L44-6`）。

---

## §1 架构与逐行实现修改清单

### 1.1 新增公共头文件：`ECDI/include/ECDI/Widget/TextSpan.h`（全新新增）

```cpp
#pragma once

#include "ECDI/Core/Color.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ECDI {

/// @brief 段内样式描述符（Phase 44，#88）
/// @details 仅允许覆盖字体族、字重与前景色；严禁包含 size（闸门 3）或斜体（闸门 2 / L44-8）
struct SpanStyle {
	std::optional<std::string>   family     = std::nullopt; ///< 字体族（std::nullopt 表示继承基样式）
	std::optional<std::uint16_t> weight     = std::nullopt; ///< 字重（400=Regular, 700=Bold；std::nullopt 表示继承）
	std::optional<Color>         foreground = std::nullopt; ///< 前景色（std::nullopt 表示继承控件当前前景色）

	bool operator==(const SpanStyle&) const noexcept = default;
};

/// @brief 样式区间契约（Phase 44，#88）
/// @details 码点索引空间；区间半开 [startCp, startCp + cpCount)；空 spans 列表等价于普通纯文本
struct TextSpan {
	std::size_t startCp = 0; ///< 起始码点索引（UTF-8 解码后的字符位置，非字节偏移）
	std::size_t cpCount = 0; ///< 覆盖的码点数量
	SpanStyle   style{};     ///< 样式覆盖属性

	bool operator==(const TextSpan&) const noexcept = default;
};

} // namespace ECDI
```

---

### 1.2 `ECDI/include/ECDI/Widget/TextWidget.h` 修改清单

#### Include 头文件段落（`:3-8`，保留 TextStyle.h 原文）
```cpp
// BEFORE:
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Widget/Widget.h"

// AFTER:
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Widget/TextSpan.h"    // ★ Phase 44：段内内联样式契约（TextSpan / SpanStyle）
#include "ECDI/Widget/Widget.h"

#include <optional>                  // ★ Phase 44：RunSlice::color（P1-1 基色解耦）
```

#### Public API 追加（`:60` 既有 `SetText` 之后，闸门 1 确认 ⓐ 原子入口）
```cpp
// BEFORE:
	virtual void SetText(const std::string& text);
	virtual void SetText(std::string&& text);

	/// @brief 文本修改前触发通知（5.5.2 撤销/重做快照点）
	virtual void OnTextChanged();

// AFTER:
	virtual void SetText(const std::string& text);
	virtual void SetText(std::string&& text);

	/// @brief 设置带有内联样式的富文本（Phase 44，闸门 1 原子入口 ⓐ）
	/// @param text 文本明文字符串
	/// @param spans 样式区间列表（内部执行排序、裁剪、去重叠与码点有界化）
	void SetStyledText(const std::string& text, const std::vector<TextSpan>& spans);
	void SetStyledText(std::string&& text, const std::vector<TextSpan>& spans);

	/// @brief 文本修改前触发通知（5.5.2 撤销/重做快照点）
	virtual void OnTextChanged();
```

#### Protected 数据结构扩充（`:118-126`，TextLine 与 RunSlice，P1-1 基色解耦）
```cpp
// BEFORE:
	struct TextLine{
		std::size_t startCp;   ///< 行起始码点索引（`cps` 空间）
		std::size_t cpCount;   ///< 行有效码点数（裁剪口径：尾部空白已被裁掉）
		float       width;     ///< 行有效宽度（尾部空白裁剪后）
		std::size_t ownedEnd;  ///< 归属终点（Phase 38 A-15：含尾部跳过的连续分隔空白）
	};

// AFTER:
	/// @brief 单行内的样式渲染切片（Phase 44，P1-1 基色解耦模型）
	struct RunSlice {
		std::size_t          startCp; ///< 切片起始码点索引
		std::size_t          cpCount; ///< 切片码点数
		float                width;   ///< 切片测量宽度
		Font                 font;    ///< 生效字体（族 + 字重）
		std::optional<Color> color;   ///< 切片前景色（std::nullopt 表示继承基色，绘制期解析）
	};

	struct TextLine{
		std::size_t          startCp;   ///< 行起始码点索引（`cps` 空间）
		std::size_t          cpCount;   ///< 行有效码点数（裁剪口径：尾部空白已被裁掉）
		float                width;     ///< 行有效宽度（尾部空白裁剪后）
		std::size_t          ownedEnd;  ///< 归属终点（Phase 38 A-15：含尾部跳过的连续分隔空白）
		std::vector<RunSlice> runs{};   ///< ★ Phase 44：行内 run 序列（非 spans 态为空，默认初始化）
	};
```

#### Protected 辅助方法追加（`:180-195` 区域，含局部表只读辅助）
```cpp
// BEFORE:
	/// @brief 码点索引对应的字节偏移（Phase 40 D40-K）
	[[nodiscard]] std::size_t ByteOffsetAt(std::size_t cpIndex) const;

// AFTER:
	/// @brief 码点索引对应的字节偏移（Phase 40 D40-K）
	[[nodiscard]] std::size_t ByteOffsetAt(std::size_t cpIndex) const;

	/// @brief 判断当前 styled spans 是否处于激活状态（C44-8 串内容身份自检护栏）
	[[nodiscard]] bool HasActiveSpans() const noexcept {
		return !m_spans.empty() && m_styledTextIdentity == m_text;
	}

	/// @brief 将指定码点区间拆解为连续覆盖的 RunSlice 序列（基于局部表，绝不写基类成员，守护 D40-L-1）
	std::vector<RunSlice> BuildRunsForRange(
	    TextMeasurer& measurer, const std::string& textSource,
	    const std::vector<std::uint32_t>& byteOffsets,
	    std::size_t startCp, std::size_t endCp) const;

	/// @brief 测量指定码点区间的样式总宽
	float MeasureStyledRangeWidth(
	    TextMeasurer& measurer, const std::string& textSource,
	    const std::vector<std::uint32_t>& byteOffsets,
	    std::size_t startCp, std::size_t endCp) const;
```

#### Private 成员字段追加（`:210-233` 区域）
```cpp
// BEFORE:
	mutable TextLayout m_wrapLayout;         ///< 布局缓存（`m_wrapValid` 为真时有效）
	mutable bool m_wrapValid = false;        ///< 布局指纹是否已建立
	mutable std::size_t m_wrapRevision = 0;  ///< 建立缓存时的文本版本（C29-10 键维 1）
	mutable Font m_wrapFont{};               ///< 建立缓存时的字体（键维 2/3/4——size + family + weight；Phase 36 K8-④）
	mutable int m_wrapDpi = 0;               ///< 建立缓存时的 DPI（键维 5）
	mutable int m_wrapWidth = 0;             ///< 建立缓存时的 `GetWidth()`（键维 6——宽度驱动重排，T29-3）

};

// AFTER:
	mutable TextLayout m_wrapLayout;         ///< 布局缓存（`m_wrapValid` 为真时有效）
	mutable bool m_wrapValid = false;        ///< 布局指纹是否已建立
	mutable std::size_t m_wrapRevision = 0;  ///< 建立缓存时的文本版本（C29-10 键维 1）
	mutable Font m_wrapFont{};               ///< 建立缓存时的字体（键维 2/3/4——size + family + weight；Phase 36 K8-④）
	mutable int m_wrapDpi = 0;               ///< 建立缓存时的 DPI（键维 5）
	mutable int m_wrapWidth = 0;             ///< 建立缓存时的 `GetWidth()`（键维 6——宽度驱动重排，T29-3）

	// ── ★ Phase 44：段内内联样式状态与缓存 ──
	std::vector<TextSpan> m_spans;                  ///< 当前样式区间集合（规范化：有序、无重叠、码点有界）
	std::string           m_styledTextIdentity;     ///< 建立样式时的文本副本（C44-8：身份比对护栏）
	std::size_t           m_styledTextRevision = 0; ///< 样式版本号（SetStyledText 递增）

	// 非折行单行 runs 缓存（红线 2：保证 Paint 阶段 0 次额外测量，P1-2 补 DPI 维）
	mutable std::vector<RunSlice> m_prefRuns;
	mutable std::size_t           m_prefRunsRevision = 0;
	mutable std::string           m_prefRunsIdentity;
	mutable int                   m_prefRunsDpi = 0;

	// preferred 高度指纹缓存的 span 版本维
	mutable std::size_t           m_prefSpanRevision = 0;

};
```

---

### 1.3 `ECDI/src/Widget/TextWidget.cpp` 修改清单

#### 1.3.1 跨算法辅助：`ClampAndValidateSpans`（P2-2 stable_sort）
```cpp
namespace {

std::vector<TextSpan> ClampAndValidateSpans(const std::vector<TextSpan>& input, std::size_t textCpCount) {
	std::vector<TextSpan> valid;
	valid.reserve(input.size());

	for (const auto& span : input) {
		if (span.cpCount == 0 || span.startCp >= textCpCount) {
			continue; // 过滤空区间与起始越界
		}
		TextSpan s = span;
		if (s.startCp + s.cpCount > textCpCount) {
			s.cpCount = textCpCount - s.startCp; // 钳制在合法码点上界内
		}
		valid.push_back(std::move(s));
	}

	// 采用 std::stable_sort 保持相同起点 span 的稳定顺序
	std::stable_sort(valid.begin(), valid.end(), [](const TextSpan& a, const TextSpan& b) {
		if (a.startCp != b.startCp) {
			return a.startCp < b.startCp;
		}
		return a.cpCount < b.cpCount;
	});

	// 去重叠：后到区间向后裁剪或剔除（优先采纳在先区间）
	std::vector<TextSpan> nonOverlapping;
	std::size_t currentEnd = 0;
	for (auto& s : valid) {
		if (s.startCp < currentEnd) {
			if (s.startCp + s.cpCount <= currentEnd) {
				continue; // 完全被前一 span 吞没
			}
			const std::size_t diff = currentEnd - s.startCp;
			s.startCp = currentEnd;
			s.cpCount -= diff;
		}
		currentEnd = s.startCp + s.cpCount;
		nonOverlapping.push_back(std::move(s));
	}

	return nonOverlapping;
}

} // namespace
```

---

#### 1.3.2 区间拆解辅助：`BuildRunsForRange` 与 `MeasureStyledRangeWidth`（守护 D40-L-1：绝不写基类表）
```cpp
std::vector<TextWidget::RunSlice> TextWidget::BuildRunsForRange(
    TextMeasurer& measurer, const std::string& textSource,
    const std::vector<std::uint32_t>& byteOffsets,
    std::size_t startCp, std::size_t endCp) const {
	std::vector<RunSlice> runs;
	if (startCp >= endCp) {
		return runs;
	}

	const Font& baseFont = m_style.font.value;

	std::size_t cur = startCp;
	for (const auto& span : m_spans) {
		if (span.startCp >= endCp) {
			break;
		}
		const std::size_t spanEnd = span.startCp + span.cpCount;
		if (spanEnd <= cur) {
			continue;
		}

		// 填充 span 之前的间隙（使用基控件字体，颜色置 nullopt）
		if (span.startCp > cur) {
			const std::size_t gapEnd = (std::min)(span.startCp, endCp);
			const std::size_t b0 = OffsetAt(byteOffsets, cur);
			const std::size_t b1 = OffsetAt(byteOffsets, gapEnd);
			const float w = measurer.MeasureText(baseFont, textSource.substr(b0, b1 - b0)).width;
			runs.push_back(RunSlice{ cur, gapEnd - cur, w, baseFont, std::nullopt });
			cur = gapEnd;
			if (cur >= endCp) {
				break;
			}
		}

		// 填充 span 覆盖部分
		const std::size_t rStart = (std::max)(cur, span.startCp);
		const std::size_t rEnd   = (std::min)(endCp, spanEnd);
		if (rEnd > rStart) {
			Font rFont = baseFont;
			if (span.style.family.has_value()) {
				rFont.family = *span.style.family;
			}
			if (span.style.weight.has_value()) {
				rFont.weight = *span.style.weight;
			}
			const std::size_t b0 = OffsetAt(byteOffsets, rStart);
			const std::size_t b1 = OffsetAt(byteOffsets, rEnd);
			const float w = measurer.MeasureText(rFont, textSource.substr(b0, b1 - b0)).width;
			runs.push_back(RunSlice{ rStart, rEnd - rStart, w, rFont, span.style.foreground });
			cur = rEnd;
		}
	}

	// 填充末尾间隙
	if (cur < endCp) {
		const std::size_t b0 = OffsetAt(byteOffsets, cur);
		const std::size_t b1 = OffsetAt(byteOffsets, endCp);
		const float w = measurer.MeasureText(baseFont, textSource.substr(b0, b1 - b0)).width;
		runs.push_back(RunSlice{ cur, endCp - cur, w, baseFont, std::nullopt });
	}

	return runs;
}

float TextWidget::MeasureStyledRangeWidth(
    TextMeasurer& measurer, const std::string& textSource,
    const std::vector<std::uint32_t>& byteOffsets,
    std::size_t startCp, std::size_t endCp) const {
	const auto runs = BuildRunsForRange(measurer, textSource, byteOffsets, startCp, endCp);
	float totalW = 0.0f;
	for (const auto& r : runs) {
		totalW += r.width;
	}
	return totalW;
}
```

---

#### 1.3.3 `BuildTextLayout` 替换块（`:377-422`，P1-1 串身份护栏 + 真实 6 参 FitText）
```cpp
// BEFORE (:377-422):
			// ① 硬边界：从 scan 起最多放多少码点（C29-1 契约——FitText 不懂空格/CJK/禁则）
			// ★ Phase 40：**5 参**重载——`cps` / `byteOffsets` 复用上面的解码，量具无需再整串预备
			const TextFit fit = measurer.FitText(font, text, scan, limit, cps, byteOffsets);
			const std::size_t hardEnd = scan + fit.fitCp;   // 可能 == scan（一个码点都放不下）

			// ② 整段放得下 ⇒ 一行收尾，本段结束
			if (hardEnd >= segEnd){
				emitLine(lineStart, segEnd, hardEnd, fit.width);
				break;
			}

			// ③ 窗口内最后合法软断点；无 ⇒ 按字硬断（= hardEnd）
			std::size_t b = FindLastSoftBreak(cps, lineStart, hardEnd);
			// ★ 推进保证（「超长词按字硬断」）：一个码点都放不下 ⇒ 按字硬断**一字**。
			//   否则 b == lineStart ⇒ emit 空行 + scan 不推进 ⇒ **死循环**（T29-5 断言锚）。
			if (b == lineStart) { b = lineStart + 1; }

			// ④ 行尾禁则：行尾不得是**开括号**（cps[b−1] ∈ 开括号集 ⇒ −−b；有界 b > lineStart——C29-3）
			//    ※ 名字对着「行尾」看：禁的是**行尾出现开括号**（如「你好（」把（挪到下行）。
			while (b > lineStart && IsLineEndForbidden(cps[b - 1])) { --b; }
			// ④b 守卫：禁则不得把行退成**空行**（空行只允许来自显式 `\n`——D29-Ⅴ）
			//    ⇒ 恢复按字硬断一字（罕见边界：首字符即开括号且只放得下一字）。
			if (b == lineStart) { b = lineStart + 1; }

			// ⑤ 行首禁则：**下行**行首不得是闭标点 ⇒ 悬挂进前行（cps[b] ∈ 闭标点集 ⇒ ++b；
			//   ★ 上界取 **segEnd** 而非 hardEnd（详设 §10.2 E5 勘误：上限若取 hardEnd，
			//   「把闭标点上提」被硬边界吃掉 ⇒ 规则形同虚设；超宽部分 = 悬挂链 advance，
			//   即 D29-Ⅱ 的微超——结构性有界：链遇非禁则成员即停）
			while (b < segEnd && IsLineStartForbidden(cps[b])) { ++b; }

			emitLine(lineStart, b, hardEnd, fit.width);

			// ⑥ ⑦ 终化：nextScan = lineEnd 后**跳过连续空格**（分隔空格不属于任何一行——需求 D1）
			std::size_t nextScan = b;
			while (nextScan < segEnd && cps[nextScan] == U' ') { ++nextScan; }
			if (nextScan <= scan) { nextScan = scan + 1; }   // 防御：绝不零推进（Paint 死循环不可接受）

			// ★★ Phase 38（A-15）：归属终点 = **下一行起点** —— 把上面 ⑥⑦ 跳过的连续分隔空格一并收进本行。
			//    根因：`FindLastSoftBreak` 对空格断点返回「空格后一位」、对 CJK 断点返回「字后一位」，
			//    后者紧跟空格时该空格落进「不属于任何行」的空隙。缺这行则 `"一二三 四"@24` 的
			//    gap==1 而 cps 是空格（A3 第三态）、`"abc   "` 的末行 ownedEnd < count。
			//    ★ 只改新字段 ⇒ 切分（startCp/cpCount/width/totalHeight）逐位不变 = 红线② 结构性成立。
			layout.lines.back().ownedEnd = nextScan;

			scan = nextScan;

// AFTER:
		// ★ P1-1 串身份护栏：防 TextBox 传入显示串/掩码串时错贴明文 spans
		const bool hasSpans = HasActiveSpans() && (text == m_text);
		if (hasSpans) {
			// ── Phase 44（段内样式折行推进）：单调消费 run，复用 run.width（P1-4，复杂度 O(L·R)）──
			const auto candidateRuns = BuildRunsForRange(measurer, text, byteOffsets, scan, segEnd);

			float accumW = 0.0f;
			std::size_t hardEnd = scan;
			for (const auto& run : candidateRuns) {
				if (accumW + run.width <= limit) {
					accumW += run.width;
					hardEnd += run.cpCount;
				} else {
					const float remLimit = (std::max)(0.0f, limit - accumW);
					// ★ 真实 6 参 FitText 调用，获取 rFit.fitCp 与 rFit.width
					const TextFit rFit = measurer.FitText(run.font, text, hardEnd, remLimit, cps, byteOffsets);
					hardEnd += rFit.fitCp;
					accumW += rFit.width;
					break;
				}
			}

			if (hardEnd == scan && scan < segEnd) {
				hardEnd = scan + 1; // 推进保证：至少前进一字
			}

			if (hardEnd >= segEnd) {
				const auto lineRuns = BuildRunsForRange(measurer, text, byteOffsets, lineStart, segEnd);
				float lineWidth = 0.0f;
				for (const auto& r : lineRuns) lineWidth += r.width;
				layout.lines.emplace_back(TextLine{ lineStart, segEnd - lineStart, lineWidth, segEnd, lineRuns });
				break;
			}

			// 内联软断点回退与标点禁则四步链（P0-1 还原）
			std::size_t b = FindLastSoftBreak(cps, lineStart, hardEnd);
			if (b == lineStart) { b = lineStart + 1; }
			while (b > lineStart && IsLineEndForbidden(cps[b - 1])) { --b; }
			if (b == lineStart) { b = lineStart + 1; }
			while (b < segEnd && IsLineStartForbidden(cps[b])) { ++b; }

			// 尾部空白裁剪并生成最终有效 runs
			std::size_t lineEnd = b;
			while (lineEnd > lineStart && cps[lineEnd - 1] == U' ') { --lineEnd; }

			const auto lineRuns = BuildRunsForRange(measurer, text, byteOffsets, lineStart, lineEnd);
			float lineWidth = 0.0f;
			for (const auto& r : lineRuns) {
				lineWidth += r.width;
			}

			layout.lines.emplace_back(TextLine{ lineStart, lineEnd - lineStart, lineWidth, b, lineRuns });

			std::size_t nextScan = b;
			while (nextScan < segEnd && cps[nextScan] == U' ') { ++nextScan; }
			if (nextScan <= scan) { nextScan = scan + 1; }
			layout.lines.back().ownedEnd = nextScan;
			scan = nextScan;
		} else {
			// ── 非 spans 路径：100% 逐字保留原代码（红线 1 绝对保证，emitLine 与 TextLine 初始化 0 扰动）──
			const TextFit fit = measurer.FitText(font, text, scan, limit, cps, byteOffsets);
			const std::size_t hardEnd = scan + fit.fitCp;

			if (hardEnd >= segEnd){
				emitLine(lineStart, segEnd, hardEnd, fit.width);
				break;
			}

			std::size_t b = FindLastSoftBreak(cps, lineStart, hardEnd);
			if (b == lineStart) { b = lineStart + 1; }
			while (b > lineStart && IsLineEndForbidden(cps[b - 1])) { --b; }
			if (b == lineStart) { b = lineStart + 1; }
			while (b < segEnd && IsLineStartForbidden(cps[b])) { ++b; }

			emitLine(lineStart, b, hardEnd, fit.width);

			std::size_t nextScan = b;
			while (nextScan < segEnd && cps[nextScan] == U' ') { ++nextScan; }
			if (nextScan <= scan) { nextScan = scan + 1; }

			layout.lines.back().ownedEnd = nextScan;
			scan = nextScan;
		}
```

---

#### 1.3.4 `DrawTextContent` 替换块（`:485-542`，P1-1 基色解耦 + 红线 2 零额外测量）
```cpp
// BEFORE (:485-542):
	if (IsWrapLayoutActive())
	{
		const TextLayout& layout = GetTextLayout(measurer);
		const float lineHeight = measurer.LineHeight(m_style.font.value);
		const Color& color = m_style.foreground.value;

		const Point block = CalculateTextPosition(x, y, BlockWidth(layout), layout.totalHeight);
		EnsureCpByteOffsets(m_text);

		std::size_t row = 0;
		for (const TextLine& line : layout.lines)
		{
			const float lineY = block.y + static_cast<float>(row) * lineHeight;
			++row;
			if (line.cpCount == 0)
			{
				continue;
			}
			const std::size_t b0 = ByteOffsetAt(line.startCp);
			const std::size_t b1 = ByteOffsetAt(line.startCp + line.cpCount);
			ctx.DrawText(Point{ block.x, lineY }, m_text.substr(b0, b1 - b0), color, m_style.font.value);
		}
		return;
	}

	const Size textSize = ctx.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);

	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, lineHeight),
		m_text, m_style.foreground.value, m_style.font.value
	);

// AFTER:
	const bool activeSpans = HasActiveSpans();
	const Color& baseColor = m_style.foreground.value;

	if (IsWrapLayoutActive())
	{
		const TextLayout& layout = GetTextLayout(measurer);
		const float lineHeight = measurer.LineHeight(m_style.font.value);

		const Point block = CalculateTextPosition(x, y, BlockWidth(layout), layout.totalHeight);
		EnsureCpByteOffsets(m_text);

		std::size_t row = 0;
		for (const TextLine& line : layout.lines)
		{
			const float lineY = block.y + static_cast<float>(row) * lineHeight;
			++row;
			if (line.cpCount == 0)
			{
				continue;
			}

			if (activeSpans && !line.runs.empty())
			{
				float runX = block.x;
				for (const auto& run : line.runs)
				{
					if (run.cpCount == 0) continue;
					const std::size_t b0 = ByteOffsetAt(run.startCp);
					const std::size_t b1 = ByteOffsetAt(run.startCp + run.cpCount);
					const Color runColor = run.color.value_or(baseColor);
					ctx.DrawText(Point{ runX, lineY }, m_text.substr(b0, b1 - b0), runColor, run.font);
					runX += run.width;
				}
			}
			else
			{
				const std::size_t b0 = ByteOffsetAt(line.startCp);
				const std::size_t b1 = ByteOffsetAt(line.startCp + line.cpCount);
				ctx.DrawText(Point{ block.x, lineY }, m_text.substr(b0, b1 - b0), baseColor, m_style.font.value);
			}
		}
		return;
	}

	if (activeSpans)
	{
		int dpi = 0;
		if (const Window* window = GetWindow())
		{
			dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
		}

		// 检查单行 runs 缓存（红线 2：二次重绘零测量）
		if (m_prefRunsRevision != m_styledTextRevision || m_prefRunsIdentity != m_text || m_prefRunsDpi != dpi)
		{
			EnsureCpByteOffsets(m_text);
			m_prefRuns = BuildRunsForRange(measurer, m_text, m_cpByteOffsets, 0, DecodeUtf8(m_text).size());
			m_prefRunsRevision = m_styledTextRevision;
			m_prefRunsIdentity = m_text;
			m_prefRunsDpi = dpi;
		}

		float totalW = 0.0f;
		for (const auto& r : m_prefRuns)
		{
			totalW += r.width;
		}
		const float lineHeight = measurer.LineHeight(m_style.font.value);
		const Point startPos = CalculateTextPosition(x, y, totalW, lineHeight);

		EnsureCpByteOffsets(m_text);
		float curX = startPos.x;
		for (const auto& run : m_prefRuns)
		{
			if (run.cpCount == 0) continue;
			const std::size_t b0 = ByteOffsetAt(run.startCp);
			const std::size_t b1 = ByteOffsetAt(run.startCp + run.cpCount);
			const Color runColor = run.color.value_or(baseColor);
			ctx.DrawText(Point{ curX, startPos.y }, m_text.substr(b0, b1 - b0), runColor, run.font);
			curX += run.width;
		}
		return;
	}

	// 零 spans 纯原路径
	const Size textSize = ctx.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);

	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, lineHeight),
		m_text, m_style.foreground.value, m_style.font.value
	);
```

---

#### 1.3.5 `GetPreferredSize` 替换块（`:544-593`，P1-2 DPI 维与 N1-3 `m_prefSpanRevision` 赋值保存）
```cpp
// BEFORE (:544-593):
	int dpi = 0;
	if (const Window* window = GetWindow())
	{
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}

	if (m_prefValid && dpi != 0 && m_prefRevision == m_textRevision
	    && m_prefDpi == dpi
	    && m_prefFont.size == m_style.font.value.size
	    && m_prefFont.family == m_style.font.value.family
	    && m_prefFont.weight == m_style.font.value.weight)   // Phase 36 批三 K8-④
	{
		return m_prefCache;   // 命中 ⇒ 零 measurer 调用、零键构造（C28-5）
	}

	const Size measured = DoMeasureText(*measurer);

	if (dpi != 0)
	{
		m_prefCache = measured;
		m_prefRevision = m_textRevision;
		m_prefFont = m_style.font.value;
		m_prefDpi = dpi;
		m_prefValid = true;
	}
	return measured;

// AFTER:
	int dpi = 0;
	if (const Window* window = GetWindow())
	{
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}

	const bool activeSpans = HasActiveSpans();
	if (m_prefValid && dpi != 0 && m_prefRevision == m_textRevision
	    && m_prefDpi == dpi
	    && m_prefFont.size == m_style.font.value.size
	    && m_prefFont.family == m_style.font.value.family
	    && m_prefFont.weight == m_style.font.value.weight
	    && (!activeSpans || m_prefSpanRevision == m_styledTextRevision))
	{
		return m_prefCache;
	}

	const Size measured = DoMeasureText(*measurer);

	if (dpi != 0)
	{
		m_prefCache = measured;
		m_prefRevision = m_textRevision;
		m_prefFont = m_style.font.value;
		m_prefDpi = dpi;
		if (activeSpans)
		{
			m_prefSpanRevision = m_styledTextRevision; // ★ N1-3 保存语句，防止有窗口时恒 miss
		}
		m_prefValid = true;
	}
	return measured;
```

---

#### 1.3.6 `DoMeasureText` 替换块（`:604-611`）
```cpp
// BEFORE:
Size TextWidget::DoMeasureText(TextMeasurer& measurer) const{

	// Label/Button 0 inset（§3.2 冻结）：{文本测量宽, 行高}；空文本 MeasureText 返回 {0,0} → 宽 0 诚实
	const Size textSize = measurer.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);
	return Size{ textSize.width, lineHeight };

}

// AFTER:
Size TextWidget::DoMeasureText(TextMeasurer& measurer) const{

	if (HasActiveSpans())
	{
		EnsureCpByteOffsets(m_text);
		const auto runs = BuildRunsForRange(measurer, m_text, m_cpByteOffsets, 0, DecodeUtf8(m_text).size());
		float totalW = 0.0f;
		for (const auto& r : runs)
		{
			totalW += r.width;
		}
		// I44-7 行高单源性：行高必须且仅来自基控件字体
		const float lineHeight = measurer.LineHeight(m_style.font.value);
		return Size{ totalW, lineHeight };
	}

	const Size textSize = measurer.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);
	return Size{ textSize.width, lineHeight };

}
```

---

#### 1.3.7 `SetStyledText` 实现（新增在文件末尾，Step 1-6 顺序敏感纪律）
```cpp
void TextWidget::SetStyledText(const std::string& text, const std::vector<TextSpan>& spans) {
	if (m_text == text && m_spans == spans) {
		return; // 无变更早退
	}

	// ── 严格遵循 Step 1-6 顺序（初设 P1-B 修正）──
	// Step 1: 文本赋值
	m_text = text;

	// Step 2: 触发变更通知与 revision 递增（与 SetText 同序）
	MarkTextChanged();

	// Step 3: 单趟独立 DecodeUtf8 解码码点空间并钳制 spans（绝不复用依赖旧 revision 的表）
	const std::size_t cpCount = DecodeUtf8(m_text).size();
	m_spans = ClampAndValidateSpans(spans, cpCount);

	// Step 4: 记录串身份副本与样式版本
	m_styledTextIdentity = m_text;
	++m_styledTextRevision;

	// Step 5: 使单行 runs 缓存与 preferred 缓存失效
	m_prefRunsRevision = 0;
	m_prefRunsIdentity.clear();
	m_prefRunsDpi = 0;
	m_prefValid = false;
	m_wrapValid = false;

	// Step 6: 触发重排与重绘
	Invalidate();
}

void TextWidget::SetStyledText(std::string&& text, const std::vector<TextSpan>& spans) {
	m_text = std::move(text);
	MarkTextChanged();
	const std::size_t cpCount = DecodeUtf8(m_text).size();
	m_spans = ClampAndValidateSpans(spans, cpCount);
	m_styledTextIdentity = m_text;
	++m_styledTextRevision;
	m_prefRunsRevision = 0;
	m_prefRunsIdentity.clear();
	m_prefRunsDpi = 0;
	m_prefValid = false;
	m_wrapValid = false;
	Invalidate();
}
```

---

### 1.4 `ECDI/include/ECDI/Widget/TextBox.h` 与 `TextBox.cpp` 修改清单

#### 1.4.1 `TextBox.h` 修改清单

##### Private 成员重命名与新增（`:404-407`）
```cpp
// BEFORE (:404-407):
	mutable bool m_linesEcho = false;
	mutable bool m_linesComposing = false;
	mutable size_t m_linesCompStart = 0;
	mutable size_t m_linesCompLen = 0;

// AFTER:
	mutable bool m_linesEcho = false;
	mutable bool m_linesComposing = false;
	mutable size_t m_linesCompStart = 0;
	mutable size_t m_linesCompLen = 0;
	mutable std::size_t m_linesSpanRevision = 0; ///< ★ Phase 44：TextBox 行表缓存的 span 版本维
```

---

#### 1.4.2 `ECDI/src/Widget/TextBox.cpp` 修改清单

##### `RecalculateLines` 缓存键比对（`:457-485`）
```cpp
// BEFORE (:470-478):
	if (m_linesValid
	    && m_linesWidth == availWidth
	    && m_linesTextRev == m_textRevision
	    && m_linesEcho == isEcho
	    && m_linesComposing == m_isComposing
	    && m_linesCompStart == m_compStart
	    && m_linesCompLen == m_compLength)
	{
		return;
	}

// AFTER:
	const bool activeSpans = HasActiveSpans() && m_echoMode == EchoMode::Normal && !m_isComposing;
	if (m_linesValid
	    && m_linesWidth == availWidth
	    && m_linesTextRev == m_textRevision
	    && m_linesEcho == isEcho
	    && m_linesComposing == m_isComposing
	    && m_linesCompStart == m_compStart
	    && m_linesCompLen == m_compLength
	    && (!activeSpans || m_linesSpanRevision == m_styledTextRevision))
	{
		return;
	}
```

##### `RecalculateLines` 非折行单行分支（`:508-542`，P1-3 修正：原代码使用 `m_text`）
```cpp
// BEFORE (:512-520):
		const size_t textSize = m_text.size();
		const char* const data = m_text.data();
		size_t lineStart = 0;
		for (size_t i = 0; i < textSize; ++i)
		{
			if (data[i] == '\n')
			{
				m_lines.push_back(LineRange{ lineStart, i });
				lineStart = i + 1;
			}
		}
		m_lines.push_back(LineRange{ lineStart, textSize });

// AFTER:
		const size_t textSize = m_text.size();
		const char* const data = m_text.data();
		size_t lineStart = 0;
		for (size_t i = 0; i < textSize; ++i)
		{
			if (data[i] == '\n')
			{
				m_lines.push_back(LineRange{ lineStart, i });
				lineStart = i + 1;
			}
		}
		m_lines.push_back(LineRange{ lineStart, textSize });

		if (activeSpans)
		{
			m_linesSpanRevision = m_styledTextRevision;
		}
```

##### `OnPaint` 文本渲染段（`:1508-1532`，P1-1 基色解耦）
```cpp
// BEFORE (:1518-1528):
			ctx.DrawText(
				Point{ viewX, lineY },
				lineText,
				textColor,
				font
			);

// AFTER:
			const bool activeSpans = HasActiveSpans() && m_echoMode == EchoMode::Normal && !m_isComposing;
			if (activeSpans)
			{
				// 只读混排分支：逐 run 渲染并动态回退基色
				EnsureCpByteOffsets(textSource);
				const auto lineRuns = BuildRunsForRange(measurer, textSource, m_cpByteOffsets, startCp, ownEndCp);
				float runX = viewX;
				for (const auto& run : lineRuns)
				{
					if (run.cpCount == 0) continue;
					const std::size_t b0 = ByteOffsetAt(run.startCp);
					const std::size_t b1 = ByteOffsetAt(run.startCp + run.cpCount);
					const Color rColor = run.color.value_or(textColor);
					ctx.DrawText(Point{ runX, lineY }, textSource.substr(b0, b1 - b0), rColor, run.font);
					runX += run.width;
				}
			}
			else
			{
				ctx.DrawText(
					Point{ viewX, lineY },
					lineText,
					textColor,
					font
				);
			}
```

---

## §2 自动化测试用例清单与实现（588 $\to$ 599）

### 2.1 测试夹具与辅助定义（`WidgetTests.cpp`）

```cpp
#include "Render/FontEngine.h" // ★ 内部渲染头文件真实包含路径（消除虚构 ECDI/Render 前缀）
#include "Platform/Win32/Win32FontSource.h" // ★ FreeType 真实字体源（T44-9/11 度量解析必需）

namespace {

// 注：WidgetTests.cpp:36 已定义 constexpr float kEpsilon = 0.001f;

/// @brief 带判别力的假测量器（P0-2 & N1-2：行高有差别，防投毒；实现真实 6 参与 4 参 FitText）
class StyledSpansFakeMeasurer : public TextMeasurer {
public:
	Size MeasureText(const Font& font, const std::string& text) override {
		float charW = 8.0f;
		if (font.weight >= 700) {
			charW = 10.0f; // 粗体字符 10px
		} else if (font.family == "Consolas") {
			charW = 9.0f;  // mono 字符 9px
		}
		std::size_t count = 0;
		for (std::size_t i = 0; i < text.size(); ++i) {
			if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
				++count;
			}
		}
		return Size{ static_cast<float>(count) * charW, LineHeight(font) };
	}

	/// @brief 行高判别力：常规体 16px，粗体 24px（若施工期错误取用 run 的最大行高，即刻飘红）
	float LineHeight(const Font& font) override {
		if (font.weight >= 700) {
			return 24.0f;
		}
		return 16.0f; // 基类/常规行高 16px
	}

	/// @brief 4 参真实重载
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp, float maxWidth) override {
		float charW = 8.0f;
		if (font.weight >= 700) {
			charW = 10.0f;
		} else if (font.family == "Consolas") {
			charW = 9.0f;
		}
		std::size_t totalCp = 0;
		for (std::size_t i = 0; i < text.size(); ++i) {
			if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++totalCp;
		}
		if (startCp >= totalCp || maxWidth <= 0.0f) {
			return TextFit{ 0, 0.0f };
		}
		std::size_t fitted = 0;
		float curW = 0.0f;
		while (startCp + fitted < totalCp && curW + charW <= maxWidth) {
			curW += charW;
			++fitted;
		}
		return TextFit{ fitted, curW };
	}

	/// @brief 6 参真实同源重载（守护 D40-G/J 契约）
	TextFit FitText(const Font& font, const std::string& text, std::size_t startCp,
	                float maxWidth, const std::vector<char32_t>& cps,
	                const std::vector<std::uint32_t>& byteOffsets) override {
		(void)cps;
		(void)byteOffsets;
		return FitText(font, text, startCp, maxWidth);
	}
};

/// @brief 计数探针 Measurer（供 T44-4 / I44-6 判定二次重绘零测量）
class CountingStyledMeasurer : public StyledSpansFakeMeasurer {
public:
	mutable std::size_t measureCalls = 0;

	Size MeasureText(const Font& font, const std::string& text) override {
		++measureCalls;
		return StyledSpansFakeMeasurer::MeasureText(font, text);
	}
};

/// @brief 可测 StyledLabel 夹具（N0-2：mutable 默认 Fake 消除 const 指针转换编译错误）
class TestableStyledLabel : public Label {
public:
	using Label::Label;
	using TextWidget::BuildTextLayout;
	using TextWidget::TextLayout;
	using TextWidget::TextLine;

	void SetMeasurer(TextMeasurer* m) { m_customMeasurer = m; }

protected:
	TextMeasurer* ResolveMeasurer() const override {
		return m_customMeasurer ? m_customMeasurer : &m_defaultFake;
	}

private:
	TextMeasurer* m_customMeasurer = nullptr;
	mutable StyledSpansFakeMeasurer m_defaultFake; // mutable 允许 const 方法返回 TextMeasurer*
};

} // namespace
```

---

### 2.2 用例实现（`WidgetTests.cpp`）

```cpp
// ── T44-1：段内三段混排测量与行高单源性（P0-2 & N1-2：行高有牙防投毒）──
void Test44StyledSpansMeasurement() {
	StyledSpansFakeMeasurer measurer;
	TestableStyledLabel label;
	label.SetMeasurer(&measurer);

	// "Hello World Consolas"
	// [0, 6) "Hello " = 常规体 (6 * 8 = 48px)
	// [6, 12) "World " = 粗体 700 (6 * 10 = 60px)
	// [12, 20) "Consolas" = mono "Consolas" (8 * 9 = 72px)
	std::vector<TextSpan> spans = {
		{ 6, 6, SpanStyle{ .weight = 700 } },
		{ 12, 8, SpanStyle{ .family = "Consolas" } }
	};
	label.SetStyledText("Hello World Consolas", spans);

	const Size pref = label.GetPreferredSize();
	const float expectedW = 6 * 8.0f + 6 * 10.0f + 8 * 9.0f; // 180.0f
	EXPECT_NEAR(pref.width, expectedW, kEpsilon);

	// I44-7 行高单源断言：必须严格等于基控件字体行高（16.0f），绝不可受粗体 24.0f 污染
	EXPECT_NEAR(pref.height, 16.0f, kEpsilon);
	EXPECT_NEAR(pref.height, measurer.LineHeight(Font{}), kEpsilon);
}

// ── T44-2：折行单调性、逐行结构与 CJK 禁则同构（P0-7 物理数值严密对齐）──
void Test44StyledSpansWrap() {
	StyledSpansFakeMeasurer measurer;
	TestableStyledLabel label;
	label.SetMeasurer(&measurer);

	// 文本 = "AA BBBB CC" (AA=16, 空格=8, BBBB=40, 空格=8, CC=16)
	std::vector<TextSpan> spans = {
		{ 3, 4, SpanStyle{ .weight = 700 } } // "BBBB" 为粗体
	};
	label.SetStyledText("AA BBBB CC", spans);

	// 可用宽 limit = 50.0f（严密数值算例：首行放不下 BBBB，次行放得下 BBBB 但放不下 CC）
	const auto layout = label.BuildTextLayout(measurer, "AA BBBB CC", 50.0f);

	EXPECT_EQ(layout.lines.size(), static_cast<std::size_t>(3));
	if (layout.lines.size() < 3) return;

	// Line 0: "AA" (回退掉空格，width=16.0f, ownedEnd=3 含空格)
	EXPECT_EQ(layout.lines[0].startCp, static_cast<std::size_t>(0));
	EXPECT_EQ(layout.lines[0].cpCount, static_cast<std::size_t>(2));
	EXPECT_NEAR(layout.lines[0].width, 16.0f, kEpsilon);
	EXPECT_EQ(layout.lines[0].ownedEnd, static_cast<std::size_t>(3));
	EXPECT_EQ(layout.lines[0].runs.size(), static_cast<std::size_t>(1));
	if (!layout.lines[0].runs.empty()) {
		EXPECT_EQ(layout.lines[0].runs[0].font.weight, static_cast<std::uint16_t>(400));
	}

	// Line 1: "BBBB" (粗体 40px，ownedEnd=8 含空格)
	EXPECT_EQ(layout.lines[1].startCp, static_cast<std::size_t>(3));
	EXPECT_EQ(layout.lines[1].cpCount, static_cast<std::size_t>(4));
	EXPECT_NEAR(layout.lines[1].width, 40.0f, kEpsilon);
	EXPECT_EQ(layout.lines[1].ownedEnd, static_cast<std::size_t>(8));
	EXPECT_EQ(layout.lines[1].runs.size(), static_cast<std::size_t>(1));
	if (!layout.lines[1].runs.empty()) {
		EXPECT_EQ(layout.lines[1].runs[0].font.weight, static_cast<std::uint16_t>(700));
	}

	// Line 2: "CC" (width=16.0f, ownedEnd=10)
	EXPECT_EQ(layout.lines[2].startCp, static_cast<std::size_t>(8));
	EXPECT_EQ(layout.lines[2].cpCount, static_cast<std::size_t>(2));
	EXPECT_NEAR(layout.lines[2].width, 16.0f, kEpsilon);

	// CJK 标点行首禁则悬挂同构断言：文本 "一二。" 设置粗体后 limit=24px 时标点悬挂不单独起行
	label.SetStyledText("一二。", { { 0, 2, SpanStyle{ .weight = 700 } } });
	const auto cjkLayout = label.BuildTextLayout(measurer, "一二。", 24.0f);
	EXPECT_EQ(cjkLayout.lines.size(), static_cast<std::size_t>(1));
	if (!cjkLayout.lines.empty()) {
		EXPECT_EQ(cjkLayout.lines[0].cpCount, static_cast<std::size_t>(3));
	}
}

// ── T44-3：命令流逐 run 原样发射与累积贴合（P0 宏规范对齐）──
void Test44StyledSpansCommandStream() {
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);

	Label label;
	label.SetPosition(10, 20);
	label.SetSize(200, 50);
	std::vector<TextSpan> spans = {
		{ 3, 3, SpanStyle{ .weight = 700, .foreground = Color::Red() } }
	};
	label.SetStyledText("ABCDEF", spans);
	label.Paint(ctx, 0, 0);

	std::vector<DrawTextCommand> textCmds;
	for (const auto& cmd : commands) {
		if (std::holds_alternative<DrawTextCommand>(cmd)) {
			textCmds.push_back(std::get<DrawTextCommand>(cmd));
		}
	}

	EXPECT_EQ(textCmds.size(), static_cast<std::size_t>(2));
	if (textCmds.size() < 2) return;

	EXPECT_EQ(textCmds[0].text, "ABC");
	EXPECT_EQ(textCmds[0].font.weight, static_cast<std::uint16_t>(400));
	EXPECT_EQ(textCmds[1].text, "DEF");
	EXPECT_EQ(textCmds[1].font.weight, static_cast<std::uint16_t>(700));
	EXPECT_EQ(textCmds[1].color, Color::Red());

	// 坐标贴合断言
	EXPECT_NEAR(textCmds[1].pos.y, textCmds[0].pos.y, kEpsilon);
	EXPECT_TRUE(textCmds[1].pos.x > textCmds[0].pos.x);
}

// ── T44-4：#85 三臂、计数探针与 "éab" 单 run 钳制（P0-4 闭合）──
void Test44StyledSpansArmTransitions() {
	CountingStyledMeasurer measurer;
	TestableStyledLabel label;
	label.SetMeasurer(&measurer);

	std::vector<TextSpan> spans = { { 0, 3, SpanStyle{ .weight = 700 } } };
	label.SetStyledText("ABCDEF", spans);

	// 第一次绘制：触发测量并填充缓存
	CommandBuffer cmd1;
	PaintContext ctx1(cmd1, measurer);
	label.Paint(ctx1, 0, 0);
	const std::size_t initialMeasures = measurer.measureCalls;

	// Arm B: 相同文本调用 SetText，样式保持激活，且二次重绘 0 次额外测量
	label.SetText("ABCDEF");
	CommandBuffer cmdB;
	PaintContext ctxB(cmdB, measurer);
	label.Paint(ctxB, 0, 0);
	EXPECT_EQ(measurer.measureCalls, initialMeasures);

	// Arm A: 不同文本调用 SetText，样式失活，回退为单命令纯文本
	label.SetText("XYZUVW");
	CommandBuffer cmdA;
	PaintContext ctxA(cmdA, measurer);
	label.Paint(ctxA, 0, 0);
	int textCmdsA = 0;
	for (const auto& c : cmdA) if (std::holds_alternative<DrawTextCommand>(c)) ++textCmdsA;
	EXPECT_EQ(textCmdsA, 1);

	// Arm C: 换回原文本，样式门控可逆重新激活
	label.SetText("ABCDEF");
	CommandBuffer cmdC;
	PaintContext ctxC(cmdC, measurer);
	label.Paint(ctxC, 0, 0);
	int textCmdsC = 0;
	for (const auto& c : cmdC) if (std::holds_alternative<DrawTextCommand>(c)) ++textCmdsC;
	EXPECT_EQ(textCmdsC, 2);

	// 显式清空：永久清除
	label.SetStyledText("ABCDEF", {});
	CommandBuffer cmdClear;
	PaintContext ctxClear(cmdClear, measurer);
	label.Paint(ctxClear, 0, 0);
	int textCmdsClear = 0;
	for (const auto& c : cmdClear) if (std::holds_alternative<DrawTextCommand>(c)) ++textCmdsClear;
	EXPECT_EQ(textCmdsClear, 1);

	// 同字节长异码点数钳制（"abcd" 4B/4cp -> "éab" 4B/3cp）
	// span 请求 [0, 10) 必须钳制到 [0, 3) 码点空间，产出单一加粗 run
	label.SetStyledText("éab", { { 0, 10, SpanStyle{ .weight = 700 } } });
	CommandBuffer cmdEab;
	PaintContext ctxEab(cmdEab, measurer);
	label.Paint(ctxEab, 0, 0);
	std::vector<DrawTextCommand> eabCmds;
	for (const auto& c : cmdEab) {
		if (const auto* tc = std::get_if<DrawTextCommand>(&c)) {
			eabCmds.push_back(*tc);
		}
	}
	EXPECT_EQ(eabCmds.size(), static_cast<std::size_t>(1));
	if (!eabCmds.empty()) {
		EXPECT_EQ(eabCmds[0].text, "éab");
		EXPECT_EQ(eabCmds[0].font.weight, static_cast<std::uint16_t>(700));
	}
}

// ── T44-5：脏边界、Emoji、Surrogate 与非法字节容错（P0 宏规范对齐）──
void Test44StyledSpansDirtyBoundaries() {
	TestableStyledLabel label;
	// "A😀B" (A=1B, 😀=4B, B=1B)
	std::vector<TextSpan> spans = {
		{ 1, 1, SpanStyle{ .weight = 700 } }, // 刚好覆盖 emoji 单码点
		{ 10, 5, SpanStyle{ .family = "Consolas" } } // 越界 span 自动被剔除
	};
	label.SetStyledText("A\xF0\x9F\x98\x80" "B", spans);

	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	label.Paint(ctx, 0, 0);

	int drawCount = 0;
	for (const auto& cmd : commands) {
		if (std::holds_alternative<DrawTextCommand>(cmd)) {
			++drawCount;
		}
	}
	EXPECT_TRUE(drawCount >= 2);

	// 非法 UTF-8 与 Surrogate 截断字节容错验证（绝不崩溃）
	label.SetStyledText("Bad\xFF\xFE\xED\xA0\x80" "End", { { 0, 20, SpanStyle{ .weight = 700 } } });
	CommandBuffer cmdBad;
	PaintContext ctxBad(cmdBad, backend);
	label.Paint(ctxBad, 0, 0); // 正常返回，无未捕获异常
}

// ── T44-6：零 spans 路径与既有纯文本逐字段位等价（C44-1 守护）──
void Test44StyledSpansZeroSpansBaseline() {
	RecordingBackend backend;

	CommandBuffer baseCmds;
	PaintContext baseCtx(baseCmds, backend);
	Label baseLabel("Bit Identical Text");
	baseLabel.SetPosition(10, 10);
	baseLabel.SetSize(200, 40);
	baseLabel.Paint(baseCtx, 0, 0);

	CommandBuffer styledCmds;
	PaintContext styledCtx(styledCmds, backend);
	Label styledLabel;
	styledLabel.SetPosition(10, 10);
	styledLabel.SetSize(200, 40);
	styledLabel.SetStyledText("Bit Identical Text", {}); // 空 spans
	styledLabel.Paint(styledCtx, 0, 0);

	EXPECT_EQ(styledCmds.size(), baseCmds.size());
	if (styledCmds.size() != baseCmds.size()) return;

	for (std::size_t i = 0; i < baseCmds.size(); ++i) {
		EXPECT_EQ(styledCmds[i].index(), baseCmds[i].index());
		if (const auto* bCmd = std::get_if<DrawTextCommand>(&baseCmds[i])) {
			const auto* sCmd = std::get_if<DrawTextCommand>(&styledCmds[i]);
			EXPECT_TRUE(sCmd != nullptr);
			if (sCmd) {
				EXPECT_NEAR(sCmd->pos.x, bCmd->pos.x, kEpsilon);
				EXPECT_NEAR(sCmd->pos.y, bCmd->pos.y, kEpsilon);
				EXPECT_EQ(sCmd->text, bCmd->text);
				EXPECT_EQ(sCmd->color, bCmd->color);
				EXPECT_NEAR(sCmd->font.size, bCmd->font.size, kEpsilon);
				EXPECT_EQ(sCmd->font.family, bCmd->font.family);
				EXPECT_EQ(sCmd->font.weight, bCmd->font.weight);
			}
		}
	}
}

// ── T44-7：多段 gap 补洞结构切片验证（P0 宏规范对齐）──
void Test44StyledSpansGapFill() {
	StyledSpansFakeMeasurer measurer;
	TestableStyledLabel label;
	label.SetMeasurer(&measurer);

	// "0123456789AB" 总长 12
	// spans: [2, 4) 粗体, [7, 9) mono
	// 期望 runs: [0, 2) 基色, [2, 4) 粗体, [4, 7) 基色, [7, 9) mono, [9, 12) 基色
	std::vector<TextSpan> spans = {
		{ 2, 2, SpanStyle{ .weight = 700 } },
		{ 7, 2, SpanStyle{ .family = "Consolas" } }
	};
	label.SetStyledText("0123456789AB", spans);

	const auto layout = label.BuildTextLayout(measurer, "0123456789AB", 500.0f);
	EXPECT_EQ(layout.lines.size(), static_cast<std::size_t>(1));
	if (layout.lines.empty()) return;

	const auto& runs = layout.lines[0].runs;
	EXPECT_EQ(runs.size(), static_cast<std::size_t>(5));
	if (runs.size() < 5) return;

	// Run 0: [0, 2)
	EXPECT_EQ(runs[0].startCp, static_cast<std::size_t>(0));
	EXPECT_EQ(runs[0].cpCount, static_cast<std::size_t>(2));
	EXPECT_EQ(runs[0].font.weight, static_cast<std::uint16_t>(400));

	// Run 1: [2, 4)
	EXPECT_EQ(runs[1].startCp, static_cast<std::size_t>(2));
	EXPECT_EQ(runs[1].cpCount, static_cast<std::size_t>(2));
	EXPECT_EQ(runs[1].font.weight, static_cast<std::uint16_t>(700));

	// Run 2: [4, 7)
	EXPECT_EQ(runs[2].startCp, static_cast<std::size_t>(4));
	EXPECT_EQ(runs[2].cpCount, static_cast<std::size_t>(3));

	// Run 3: [7, 9)
	EXPECT_EQ(runs[3].startCp, static_cast<std::size_t>(7));
	EXPECT_EQ(runs[3].cpCount, static_cast<std::size_t>(2));
	EXPECT_EQ(runs[3].font.family, "Consolas");

	// Run 4: [9, 12)
	EXPECT_EQ(runs[4].startCp, static_cast<std::size_t>(9));
	EXPECT_EQ(runs[4].cpCount, static_cast<std::size_t>(3));
}

// ── T44-8：跨字体/字重命令发射属性分立（P0 宏规范对齐）──
void Test44StyledSpansDifferentFonts() {
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);

	Label label;
	label.SetStyledText("PlainBold", { { 5, 4, SpanStyle{ .weight = 700 } } });
	label.Paint(ctx, 0, 0);

	std::vector<DrawTextCommand> textCmds;
	for (const auto& c : commands) {
		if (const auto* tc = std::get_if<DrawTextCommand>(&c)) {
			textCmds.push_back(*tc);
		}
	}

	EXPECT_EQ(textCmds.size(), static_cast<std::size_t>(2));
	if (textCmds.size() < 2) return;

	EXPECT_NE(textCmds[0].font.weight, textCmds[1].font.weight);
	EXPECT_EQ(textCmds[0].font.weight, static_cast<std::uint16_t>(400));
	EXPECT_EQ(textCmds[1].font.weight, static_cast<std::uint16_t>(700));
}

// ── T44-9：字体度量健全性校验（从两链一致性载体撤出并更名，P1-1）──
void Test44StyledSpansFontMetricsSanity() {
	auto engine = std::make_unique<FontEngine>();
	engine->SetDpi(96);
	engine->SetFontSource(std::make_unique<Win32FontSource>());
	const Font regFont{ 14.0f, "Segoe UI", 400 };
	const Font boldFont{ 14.0f, "Segoe UI", 700 };

	const float h1 = engine->LineHeight(regFont);
	const float h2 = engine->LineHeight(boldFont);

	EXPECT_TRUE(h1 > 0.0f);
	EXPECT_TRUE(h2 > 0.0f);
	if (h1 <= 0.0f || h2 <= 0.0f) return;

	EXPECT_NEAR(h1, h2, kEpsilon);
}

// ── T44-11：基线数值探针（P0 宏规范对齐，Ascent 有牙守卫）──
void Test44StyledSpansBaselineProbe() {
	auto engine = std::make_unique<FontEngine>();
	engine->SetDpi(96);
	engine->SetFontSource(std::make_unique<Win32FontSource>());
	const Font segoeReg{ 20.0f, "Segoe UI", 400 };
	const Font segoeBold{ 20.0f, "Segoe UI", 700 };
	const Font consolasReg{ 20.0f, "Consolas", 400 };

	const float aReg = engine->Ascent(segoeReg);
	const float aBold = engine->Ascent(segoeBold);
	const float aMono = engine->Ascent(consolasReg);

	// 守卫断言：Ascent 缺失时返回 0.0f，非真即判失败
	EXPECT_TRUE(aReg > 0.0f);
	EXPECT_TRUE(aBold > 0.0f);
	EXPECT_TRUE(aMono > 0.0f);
	if (aReg <= 0.0f || aBold <= 0.0f || aMono <= 0.0f) return;

	// 同族断言：基线 0 偏差
	EXPECT_NEAR(aReg, aBold, kEpsilon);

	// 跨族反证：断言偏差存在并记录（~7px 偏置）
	EXPECT_TRUE(std::abs(aReg - aMono) > 0.0f);
}
```

#### 2.2.1 `WidgetTests.cpp` 注册点（`:1965`，N1-1 闭合）
```cpp
// 在 void ECDI::Test::RegisterWidgetTests() 末尾（:1965 之前）追加注册语句：
    // ── ★ Phase 44：段内内联样式测试（T44-1..9, T44-11）──
    GetTestRegistry().Add("TextWidget.StyledSpansMeasurement",       &Test44StyledSpansMeasurement);
    GetTestRegistry().Add("TextWidget.StyledSpansWrap",              &Test44StyledSpansWrap);
    GetTestRegistry().Add("TextWidget.StyledSpansCommandStream",      &Test44StyledSpansCommandStream);
    GetTestRegistry().Add("TextWidget.StyledSpansArmTransitions",    &Test44StyledSpansArmTransitions);
    GetTestRegistry().Add("TextWidget.StyledSpansDirtyBoundaries",   &Test44StyledSpansDirtyBoundaries);
    GetTestRegistry().Add("TextWidget.StyledSpansZeroSpansBaseline", &Test44StyledSpansZeroSpansBaseline);
    GetTestRegistry().Add("TextWidget.StyledSpansGapFill",           &Test44StyledSpansGapFill);
    GetTestRegistry().Add("TextWidget.StyledSpansDifferentFonts",    &Test44StyledSpansDifferentFonts);
    GetTestRegistry().Add("TextWidget.StyledSpansFontMetricsSanity", &Test44StyledSpansFontMetricsSanity);
    GetTestRegistry().Add("TextWidget.StyledSpansBaselineProbe",     &Test44StyledSpansBaselineProbe);
```

---

### 2.3 T44-10 用例实现与注册（`TextBoxTests.cpp`，P0-6 真实 API 与选区高亮差分断言）

```cpp
// ── TestableTextBox（TextBoxTests.cpp:56-135 扩充 using 暴露通道）──
// BEFORE:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;
    // ...
    using TextBox::CaretIndexFromLineX;

// AFTER:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;
    // ...
    using TextBox::CaretIndexFromLineX;
    using TextBox::SelectAll;                 // ★ Phase 44：暴露全选入口
    using TextBox::CopySelectionToClipboard;  // ★ Phase 44：暴露剪贴板复制入口
public:
    static FakeTextMeasurer& GetFake() { return ms_fake; } // ★ 红线 2：测量绘制同源实例
```

```cpp
// ── T44-10：只读 TextBox 显示混排、选区高亮与明文复制 ──
void Test44TextBoxReadOnlySupport() {
	TestableTextBox box;
	box.SetReadOnly(true);
	box.SetSize(200, 100);

	std::vector<TextSpan> spans = {
		{ 0, 3, SpanStyle{ .weight = 700 } },
		{ 3, 3, SpanStyle{ .family = "Consolas" } }
	};
	box.SetStyledText("AAABBBCCC", spans);

	// 红线 2：测量与绘制严格复用同一 Measurer 实例
	FakeTextMeasurer& fake = TestableTextBox::GetFake();

	// ① 无选区基线绘制：统计背景与边框的 DrawRectCommand 基线数
	CommandBuffer baseCmds;
	PaintContext baseCtx(baseCmds, fake);
	box.Paint(baseCtx, 0, 0);

	int baseRectCount = 0;
	for (const auto& cmd : baseCmds) {
		if (std::holds_alternative<DrawRectCommand>(cmd)) ++baseRectCount;
	}

	// ② 触发全选与剪贴板复制通道
	box.SelectAll();
	box.CopySelectionToClipboard();

	auto sel = box.GetSelection();
	EXPECT_TRUE(sel.has_value());
	if (sel.has_value()) {
		EXPECT_EQ(sel->start, static_cast<std::size_t>(0));
		EXPECT_EQ(sel->end, static_cast<std::size_t>(9));
	}

	// ③ 有选区状态绘制：断言多 run 发射与高亮矩形差分
	CommandBuffer selCmds;
	PaintContext selCtx(selCmds, fake);
	box.Paint(selCtx, 0, 0);

	int drawCount = 0;
	int selRectCount = 0;
	for (const auto& cmd : selCmds) {
		if (std::holds_alternative<DrawTextCommand>(cmd)) {
			++drawCount;
		} else if (std::holds_alternative<DrawRectCommand>(cmd)) {
			++selRectCount;
		}
	}
	EXPECT_TRUE(drawCount >= 2);

	// 差分断言（P1-2 充血化）：选区激活后发射的高亮矩形数严格比基线多 1 个
	EXPECT_EQ(selRectCount, baseRectCount + 1);

	// 断言明文获取不受样式影响且无控制字符污染
	EXPECT_EQ(box.GetText(), "AAABBBCCC");
	if (sel.has_value()) {
		const std::string selectedText = box.GetText().substr(sel->start, sel->end - sel->start);
		EXPECT_EQ(selectedText, "AAABBBCCC");
	}
}
```

#### 2.3.1 `TextBoxTests.cpp` 注册点（`:3626`，N1-1 闭合）
```cpp
// 在 void ECDI::Test::RegisterTextBoxTests() 末尾（:3626 之前）追加注册语句：
    // ── ★ Phase 44：只读 TextBox 混排支持（T44-10）──
    GetTestRegistry().Add("TextBox.StyledReadOnlySupport", &Test44TextBoxReadOnlySupport);
```

---

## §3 契约追溯（C44-1..8）与机械机检表（I44-1..7）

### 3.1 C44 契约与守护矩阵

| 契约编号 | 契约内容 | 机制与实现保证 | 验证用例 |
|---|---|---|---|
| **C44-1** | 零 spans 路径逐位等价 | 未设/空 spans/失活时走原路径，`SetText` 两重载 100% 零改动 | T44-6 + 既有 588 用例 |
| **C44-2** | 测量绘制同源 Measurer | 布局与 Paint 使用同一 Measurer，二次重绘 0 次额外测量 | T44-1 / T44-4 / T44-10 / I44-6 |
| **C44-3** | 行高单源性 | 混排文本行高严格由基控件字体确定，不受 run 字体污染 | T44-1 / I44-7 |
| **C44-4** | 规范化有序不重叠 | `ClampAndValidateSpans` 保证码点有界、正向递增、无重叠 | T44-4 / T44-5 / T44-7 |
| **C44-5** | 折行单调性 | 逐 run 推进测量，候选 run 宽度直接复用，超额严格折行 | T44-2 |
| **C44-6** | 执行体与头文件零侵入 | `DrawTextCommand` / 后端绘制体 / `Font.h` 保持 0 修改 | I44-1 / I44-2 / I44-3 |
| **C44-7** | [已出局] 随 ⓑ 退役 | （初设 v1.0 方案 ⓑ 遗留，现已废弃） | — |
| **C44-8** | 串内容身份自检护栏 | `m_styledTextIdentity == m_text` 串级自检，换串自动失活，换回自动激活 | T44-4 三臂 |

---

### 3.2 机械机检表（I44-1..7，修正真实路径）

| 编号 | 检查目标 | 实施期检验命令 | 预期结果 |
|---|---|---|---|
| **I44-1** | `DrawTextCommand` 零修改 | `git diff HEAD -- ECDI/include/ECDI/Render/RenderCommand.h` | 退出码 0 且无输出 |
| **I44-2** | `Font.h` 零修改 | `git diff HEAD -- ECDI/include/ECDI/Core/Font.h` | 退出码 0 且无输出 |
| **I44-3** | 后端绘制执行体零修改（真实路径） | `git diff HEAD -- ECDI/src/Render/GDIBackend.cpp ECDI/src/Render/GLRenderer.cpp` | 退出码 0 且无输出 |
| **I44-4** | `SetText` 两重载零修改 | `git diff HEAD -- ECDI/src/Widget/TextWidget.cpp`（比对 `:37-60`） | 退出码 0 且 `:37-60` 纯白 |
| **I44-5** | 公共头增量严格受限 | `git status -s -- ECDI/include/` | 仅 `TextSpan.h`（A）与 `TextWidget.h`/`TextBox.h`（M） |
| **I44-6** | 二次重绘零测量 | `CountingStyledMeasurer` 二次重绘断言（T44-4） | `measureCalls == initialMeasures` |
| **I44-7** | 行高单源断言 | `StyledSpansFakeMeasurer` 行高比对（T44-1） | `pref.height == 16.0f == LineHeight(Font{})` |

---

### 3.3 REQ-20 需求验收映射表（P1-3 恢复）

| REQ-20 判据 | 判据原文（逐字引自需求稿 §1.1；与初设 §5.3 同源） | 落地用例 / 机制 | 验收口径与降级说明 |
|---|---|---|---|
| **判据 ①** | ECDI 侧单测：同一 Label 内粗体/常规/mono 三段混排，测量与折行正确、逐段样式生效（GDI/GL 两链一致） | T44-1 / T44-2 / T44-3 / T44-11 | **同族混排（粗体+常规体）基线 0 偏差**；**跨族混排（混 family）显式降级**为「两链统一按单元格顶边对齐，接受 7px 行内基线偏置」（`L44-9`，经 `L44-6` 提交 harness 书面签署确认）。★ T44-9 已更名 `FontMetricsSanity` 并**撤出两链验收载体**（单引擎行高比较不得充当双链验证），本判据的两链部分按上述降级口径验收，偏置锚点由 T44-11 锁定 |
| **判据 ②** | 判别力：回退任一段样式 ⇒ 恰该用例红 | T44-7 / T44-8 + §4 批 1 第 5 项（负对照执行） | 施工期投毒两臂并留痕：① 回退粗体 run 的 `weight` 为 400 ⇒ 恰 T44-1 / T44-3 的粗体断言红、其余绿；② 不拆 run，或拆了 run 却共用基字体 ⇒ 恰 T44-8 红。两臂还原后须复跑全链 599/599 全绿（条 76：量具未校准则判据无效） |
| **判据 ③** | 本仓消费后：转录段内粗体/斜体/行内 code 真渲染，选中/复制仍取块全文**（口径消费时定）** | T44-10（+ K11 明文复制通路） | 闸门 4-① 本期收只读 `TextBox` 多 run 真渲染与选区高亮（T44-10 的 `selRectCount == baseRectCount + 1` 差分断言）；`CopySelectionToClipboard()` 纯取 `m_text` 明文子串（K11），复制与样式正交；**「（口径消费时定）」逐字保留**——选中/复制的具体口径由消费期定，框架本期只提供事实基础；斜体由闸门 2 暂缓记账 `L44-8`，可编辑 `TextBox` 记账 `L44-1` |
| **判据 ④** | 负对照：既有单字体路径（全部现行探针）不回归 | 红线 1 + C44-1 + T44-6 + I44-1 | 既有 588 条用例一行不改全绿；未设 spans / 空 spans / 换串失活与今日单字体路径的命令流、preferred、wrap 布局逐位等价 |

---

## §4 实施任务分批计划（Batch Plan）

* **批 1：公共契约、TextWidget 核心实现与 WidgetTests（588 $\to$ 598）**
  1. 新增 `ECDI/include/ECDI/Widget/TextSpan.h`；
  2. 修改 `ECDI/include/ECDI/Widget/TextWidget.h` 与 `ECDI/src/Widget/TextWidget.cpp`；
  3. 修改 `ECDI/src/Tests/WidgetTests.cpp`，实现并注册 T44-1..9 与 T44-11（用例数 588 $\to$ 598）；
  4. 验证机检项 I44-1..7 并确保构建与测试全绿；
  5. ★ **REQ-20 判据② 负对照执行（条 76：量具须校准）**：施工期投毒两臂并留痕 —— ① 将 T44-1 / T44-3 夹具中的粗体 run `weight` 回退为 400 ⇒ 恰 T44-1 / T44-3 的粗体断言红、其余绿；② 令 `DrawTextContent` 不拆 run（或拆 run 但统一取基字体）⇒ 恰 T44-8 红。两臂还原后须复跑全链 599/599 全绿；未留痕或投毒不红 ⇒ 判据② 不予收口。

* **批 2：TextBox 只读接入、测试 T44-10 与收口回写（598 $\to$ 599）**
  1. 修改 `ECDI/include/ECDI/Widget/TextBox.h` 与 `ECDI/src/Widget/TextBox.cpp`（真实路径）；
  2. 修改 `ECDI/src/Tests/TextBoxTests.cpp`，暴露 using 入口，实现并注册 T44-10（用例数 598 $\to$ 599）；
  3. 待消费方 harness 确认签署后登记延后账本：`L44-1`（编辑器通路）、`L44-6`（harness 确认）、`L44-7`（文档结账）、`L44-8`（斜体扩展）、`L44-9`（跨族基线对齐）；
  4. 同步更新 `README.md`、`docs/dev/README.md` 与 `docs/dev/roadmap-deferred.md` 规模锚点（588 $\to$ 599，headers 101 $\to$ 102）。

---

## §5 修订记录

* **v1.5（2026-11-08）★ v1.4 复审 PASS（有条件通过 / 可进批一）后的纯文档订正；产品码与测试用例零改动**：
  1. **§3.3 REQ-20 映射表逐字订正**：四条判据原文改回需求稿 §1.1:27 逐字（对齐初设 §5.3:321-328），纠正 v1.4 的改写与编号错位；恢复判据②「判别力：回退任一段样式 ⇒ 恰该用例红」及其载体，判据③ 恢复「（口径消费时定）」；判据① 注明 T44-9 已更名 `FontMetricsSanity` 并撤出两链验收载体，两链部分按 `L44-9` 降级口径验收；
  2. **§4 批 1 新增负对照执行项**：把「投毒 → 恰红 → 还原后全绿」写成实施期必留痕步骤，闭合判据② 的验收链；
  3. **§0.2 算例口径订正**：Line 1 的剩余预算由「2px」改为实际 `remLimit = limit - accumW = 10`（tail run " CC" 宽 24，6 参 `FitText` 吃下 1 个空格），与 §1.3.3 算法逐位一致；
  4. ★ 本版**只改本文档**：未触碰 `TextWidget.*` / `TextBox.*` / 测试文件 / README / roadmap，用例数 588、公共头 101、docs 216 三个锚点均不变。
* **v1.4（2026-11-08）★ 复核吸收（P0 算例重构 / 3×P1 全闭合）；状态 = 待复审**：
  1. **P0（T44-2 物理数值自洽重构）**：采用不含行首禁则字符的对称样本 `"AA BBBB CC"`，`limit = 50.0f` 下实测恰为 3 行（Line 0 "AA" 16px、Line 1 粗体 "BBBB" 40px、Line 2 "CC" 16px），彻底消除句号悬挂导致的算例必红缺陷；CJK 禁则悬挂在单独段落断言；
  2. **P1-1（BuildTextLayout 串身份护栏）**：补齐 `const bool hasSpans = HasActiveSpans() && (text == m_text);`，严防 TextBox 传入显示/掩码串时错贴明文 spans；
  3. **P1-2（T44-10 选区高亮差分断言）**：引入无选区 vs 有选区 `DrawRectCommand` 差分断言（`selRectCount == baseRectCount + 1`），彻底根绝恒真假绿；
  4. **P1-3（REQ-20 验收映射表恢复）**：在 §3.3 完整恢复四条判据追溯映射表，明确判据 ① 跨族偏置由 `L44-9` 登记，签署方为 harness（`L44-6`）。
* **v1.3（2026-11-08）★ 复核吸收（6×P0 / 1×P0-7 / P1 / P2）**。
* **v1.2（2026-11-08）★ 复审吸收（3×P0 / 5×P1 / 6×P2）**。
* **v1.1（2026-11-08）★ 吸收初审意见（5×P0 / 5×P1 / 6×P2）**。
* **v1.0（2026-11-08）★ 初始版本**。
