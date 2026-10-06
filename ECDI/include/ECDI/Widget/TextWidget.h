#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Widget/Widget.h"

#include <string>
#include <vector>   // ★ Phase 29 批二（D29-C）：TextLayout::lines（布局零 substr——行 = 码点区间）

namespace ECDI{

class PaintContext;
class TextMeasurer;
class Theme;

/// @brief 文本控件基类（B1：第二个文本控件出现时抽取）
/// @details 职责：文本数据/字体/颜色/文本绘制/位置计算；
/// Widget = 几何/可见性/事件；Label/Button/TextBox = 自己的行为。
/// Phase 9：持有 TextStyle（m_style——文字视觉唯一来源）；ApplyTheme/SetStyle 注入默认值/运行时覆盖。
class TextWidget: public Widget{

public:

	TextWidget();   // 空文本控件（Label/Button/TextBox 默认构造依赖——构造期注入 TextStyle）

	explicit TextWidget(const std::string& text);

	explicit TextWidget(std::string&& text);

	/// @brief 设置文本（9.7 修：升格 virtual——TextBox override 置行缓存失效；外部替换文本绕过编辑操作路径）
	virtual void SetText(const std::string& text);

	virtual void SetText(std::string&& text);

	const std::string& GetText() const noexcept;

	/// @brief 设置文本颜色（旧 API 保留——内部转发 SetStyle，单一状态来源；Phase 9）
	void SetTextColor(const Color& color);

	const Color& GetTextColor() const noexcept;

	/// @brief 设置字体（旧 API 保留——内部转发 SetStyle；Font 纯数据值语义）
	void SetFont(const Font& font);

	/// @brief 应用主题（同步默认值到 m_style，只更新未 Override 属性——D7）
	/// @details virtual——Button/TextBox override 扩展（先调基类注入 TextStyle，再注入专属 Style）。
	/// 基类构造期调用时静态派发到 TextWidget::ApplyTheme（派生类构造函数需再次调用）。
	virtual void ApplyTheme(const Theme& theme);

	/// @brief 运行时文本样式覆盖（D7：Set() 标记 Override，后续 ApplyTheme 不覆盖）
	void SetStyle(TextStyleOverride override);

	/// @brief 内容测量 preferred（9.8 override——单行文本宽 + 行高；经 ResolveMeasurer 拿测量器）
	/// @details 有测量器 → 内容测量；无（运行时 fallback——无窗口且未注入）→ Widget 默认当前尺寸
	///          ★ Phase 29 批二：`wrap == true` 且有有限宽 ⇒ **布局驱动** `{GetWidth(), totalHeight}`
	[[nodiscard]] Size GetPreferredSize() const override;

	/// @brief 断行开关（★ Phase 29 批二 △5——公共 API +1 对的前半）
	/// @param wrap true = 文本按控件宽度断行（D29-C 布局驱动 preferred + 逐行绘制）；
	///              false = **100% Phase 28 原路径**（红线①：单条 DrawText，布局引擎零接触——C29-5）
	/// @details ★ **双缓存失效**：本开关影响 preferred（Phase 28 指纹）**和** wrap 布局指纹
	///          （C29-10 五元组）⇒ 两个缓存同时置无效 + `Invalidate()`（重绘）。
	///          ★ 默认 **false**（零回归根基——A1/A2 验收前提）。
	void SetWordWrap(bool wrap);

	/// @brief 断行开关查询（★ Phase 29 批二 △5——公共 API +1 对的后半）
	[[nodiscard]] bool IsWordWrap() const noexcept;

protected:

	/// @brief 文本绘制位置（B3：对齐策略虚方法，不写死）
	/// @param textWidth 文本宽度——供派生类使用（水平居中需要；基类默认左对齐不用）
	/// @param lineHeight 行高——所有文本控件使用（垂直居中）
	/// @return 默认：水平左对齐 + 垂直居中（P7 定案）
	virtual Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const;

	/// @brief 测量器解析接缝（9.8——ProgressBar ResolveAnimationManager 同构；仅服务 preferred 测量，不扩散）
	/// @details 正常运行 = Window 的 TextMeasurer（const 方法内 const_cast——GetLineHeight 先例）；
	/// 测试派生类 override 返回 FakeTextMeasurer；nullptr = 无窗口且未注入 → 调用方走 Widget 默认
	[[nodiscard]] virtual TextMeasurer* ResolveMeasurer() const;

	/// @brief 绘制文本（空文本跳过 → MeasureText 宽高 → CalculateTextPosition → DrawText）
	void DrawTextContent(PaintContext& ctx, int x, int y);

	/// @brief 文本内容变更标记（Phase 28 批二：preferred 指纹的失效唯一入口）
	/// @details `SetText` 双载调用；★ **子类直接改 `m_text` 的路径必须同样调用**
	///          （TextBox 的编辑/组合/Undo 六处 —— 见其 `m_needsLineRecalc` 站点）。
	///          递增 revision ⇒ 下一次 `GetPreferredSize` 指纹失配 ⇒ 重测（C28-5）。
	void MarkTextChanged() noexcept{ ++m_textRevision; }

	std::string m_text;

	/// @brief 文本样式（foreground + font）——所有文本控件的文字视觉唯一来源（Phase 9）
	TextStyle m_style;

private:

	/// @brief 断行后的单行（★ Phase 29 D29-C：**码点区间**而非 string——布局零 substr/零拷贝）
	struct TextLine{
		std::size_t startCp = 0;    ///< 行首码点索引（**绝对**——相对全串）
		std::size_t cpCount = 0;    ///< 本行码点数（**已做尾部空白裁剪**——D29-Ⅰ ⑥）
		float width = 0.0f;         ///< 本行宽（DIP——由扫描状态直接得到，**非** per-line MeasureText）
	};

	/// @brief 断行布局结果（★ D29-C 共享 TextLayout：`GetPreferredSize` 与 `DrawTextContent` 双消费端）
	struct TextLayout{
		std::vector<TextLine> lines;   ///< 逐行区间（空行 = cpCount 0——D29-Ⅴ；绘制跳过）
		float totalHeight = 0.0f;      ///< `lines.size() × 行高`——含空行（占行高零命令）
	};

	/// @brief preferred 内容测量实现（9.8——private：TextWidget 语义组成部分，非 cpp 匿名辅助）
	/// @details Label/Button 0 inset（§3.2 冻结）：{文本测量宽, 行高}——空文本 MeasureText 返回 {0,0} → 宽 0 诚实
	[[nodiscard]] Size DoMeasureText(TextMeasurer& measurer) const;

	/// @brief 断行布局（★ Phase 29 批二 △5——D29-Ⅰ 状态机正式化）
	/// @param measurer  测量器（`FitText`/`LineHeight` 来源——与绘制**同源**）
	/// @param maxWidth  可用宽（DIP = `GetWidth()`；**<= 0 不得传入**——调用方已退化单行）
	/// @return 布局结果；空文本 ⇒ 1 个空行（`lines.size()` == 1——「零行」会让 preferred 高度归零）
	[[nodiscard]] TextLayout BuildTextLayout(TextMeasurer& measurer, float maxWidth) const;

	/// @brief 布局缓存入口（★ C29-10：**五元组指纹显式列键**，入口每次判定）
	/// @details 键 = `(m_textRevision, font.size, font.family, dpi, GetWidth())`——任一变 ⇒ 重建。
	///          ★ 与 Phase 28 的 preferred 指纹（`m_prefValid`…）**正交两套**（盯防③）：
	///          wrap 分支不读不写 preferred 缓存，非 wrap 分支不读不写布局缓存。
	[[nodiscard]] const TextLayout& GetTextLayout(TextMeasurer& measurer) const;

	/// @brief wrap 路径可用性判定（本 cpp 的private helper——★ 三条件同时满足才走布局）
	[[nodiscard]] bool IsWrapLayoutActive() const noexcept;

	/// @brief 区块宽 = 最宽一行（供 `CalculateTextPosition` 的**水平**对齐用）
	/// @details ★ 不是 `GetWidth()`：水平对齐语义 = 「把**段落**当整体居中」（段落宽 = 最宽行）。
	///          ★ 每行自己的对齐（居中/右对齐）是 **O1 开放项**（#51③）——v1 每行左对齐，
	///          这是把段落当整体的自然选择（短末行左对齐而非居中）。
	[[nodiscard]] float BlockWidth(const TextLayout& layout) const noexcept;

	// ── Phase 28 批二：preferred 结果指纹短路（D28-B——TextWidget 层闭环，不做 Widget 通用缓存）──
	std::size_t m_textRevision = 0;      ///< 文本内容版本（`MarkTextChanged` 递增——唯一失效源之一）
	mutable bool m_prefValid = false;    ///< 指纹是否已建立（首次 GetPreferredSize 恒 miss）
	mutable Size m_prefCache{};          ///< 上次测量结果（有效时返回——零 measurer 调用）
	mutable std::size_t m_prefRevision = 0;   ///< 建立缓存时的文本版本
	mutable Font m_prefFont{};           ///< 建立缓存时的字体（值比较——SetFont/SetStyle/ApplyTheme 无需 bump）
	mutable int m_prefDpi = 0;           ///< 建立缓存时的 DPI（`lround(GetDpiScale()*96)`——跨屏失效）

	// ── Phase 29 批二：断行开关 + wrap 布局缓存（D29-C / C29-10）──
	bool m_wordWrap = false;             ///< 断行开关（默认关 = 红线①）

	mutable TextLayout m_wrapLayout;         ///< 布局缓存（`m_wrapValid` 为真时有效）
	mutable bool m_wrapValid = false;        ///< 布局指纹是否已建立
	mutable std::size_t m_wrapRevision = 0;  ///< 建立缓存时的文本版本（C29-10 键维 1）
	mutable Font m_wrapFont{};               ///< 建立缓存时的字体（键维 2/3——size + family）
	mutable int m_wrapDpi = 0;               ///< 建立缓存时的 DPI（键维 4）
	mutable int m_wrapWidth = 0;             ///< 建立缓存时的 `GetWidth()`（键维 5——宽度驱动重排，T29-3）

};

}
