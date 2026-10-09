#include "ECDI/Widget/TextWidget.h"

#include "ECDI/Core/Size.h"
#include "ECDI/Core/UTF8.h"     // ★ Phase 29 批二（D29-C）：CodepointIndexToByteOffset（码点区间 → 字节切片）
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Window/Window.h"
#include "Render/Utf8Decode.h"   // ★ Phase 29 D29-Ⅰ：`DecodeUtf8`（与测量/渲染链**共用同一解码**——
                                 //   两链各自解码会让「测宽」与「断行宽」在非法字节处分裂，S6）

#include <algorithm>
#include <cmath>     // std::lround（Phase 28 批二：DPI 指纹）
#include <utility>

namespace ECDI{

TextWidget::TextWidget(): m_text(){
	// Phase 9：默认构造也注入主题默认样式（生命周期契约——任何 Style 进入绘制前必须 ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

TextWidget::TextWidget(const std::string& text): m_text(text){
	// Phase 9：从主题注入默认样式（虚函数在基类构造期静态派发到 TextWidget::ApplyTheme——
	// Button/TextBox 派生类构造函数需再次调用以注入专属 Style）
	ApplyTheme(GetDefaultTheme());
}

TextWidget::TextWidget(std::string&& text): m_text(std::move(text)){
	ApplyTheme(GetDefaultTheme());
}

void TextWidget::SetText(const std::string& text){

	m_text = text;

	MarkTextChanged();   // Phase 28 批二：preferred 指纹失效（C28-5）

}

void TextWidget::SetText(std::string&& text){

	m_text = std::move(text);

	MarkTextChanged();   // Phase 28 批二：preferred 指纹失效（C28-5）

}

const std::string& TextWidget::GetText() const noexcept{

	return m_text;

}

void TextWidget::SetTextColor(const Color& color){

	// 旧 API 转发（单一状态来源——经 SetStyle 产生 Override 标记，后续 ApplyTheme 不覆盖）
	TextStyleOverride style;
	style.foreground = color;
	SetStyle(style);

}

const Color& TextWidget::GetTextColor() const noexcept{

	return m_style.foreground.value;

}

void TextWidget::SetFont(const Font& font){

	// 旧 API 转发（单一状态来源——经 SetStyle 产生 Override 标记）
	TextStyleOverride style;
	style.font = font;
	SetStyle(style);

}

// ── Phase 9：主题应用与样式覆盖（D7——Apply 只更新未 Override 属性）────────

void TextWidget::ApplyTheme(const Theme& theme){

	TextStyle defaults = theme.GetTextStyle();   // 消费 GetTextStyle（非 GetLabelStyle——v1.1 修正）
	m_style.foreground.Apply(defaults.foreground.value);
	m_style.font.Apply(defaults.font.value);
	Invalidate();

}

void TextWidget::SetStyle(TextStyleOverride override){

	if (override.foreground) m_style.foreground.Set(*override.foreground);
	if (override.font)        m_style.font.Set(*override.font);
	Invalidate();

}

// ── Phase 29 批二：断行开关（△5）────────────────────────────────────────

void TextWidget::SetWordWrap(bool wrap){

	if (m_wordWrap == wrap)
	{
		return;   // 无变化 ⇒ 零失效（重复 SetWordWrap(true) 不打断缓存——T29-WRAP-3 同族）
	}
	m_wordWrap = wrap;

	// ★ **双缓存失效**（C29-10 / 盯防③）：两个缓存正交，本开关两套都影响 ⇒ 同置无效。
	//   （只置一个是经典漏失效：preferred 命中旧单行尺寸 ⇒ 高度不更新、观感 = 文字被裁）
	m_prefValid = false;
	m_wrapValid = false;

	Invalidate();   // 触发重绘（否则须等下一次别的失效才更新）

}

bool TextWidget::IsWordWrap() const noexcept{

	return m_wordWrap;

}

bool TextWidget::IsWrapLayoutActive() const noexcept{

	// ★ wrap 路径三条件同时满足（D29-Ⅰ「width≤0/无窗口 ⇒ 退化单行」）：
	//   ① 开关开；② **有限宽**（GetWidth() > 0——否则 preferred 无宽度来源，必然死循环）；
	//   ③ 有窗口（DPI 指纹来源——无窗口 ⇒ 指纹语义不完整，退回 Phase 28 不短路同族口径）。
	return m_wordWrap && GetWidth() > 0 && GetWindow() != nullptr;

}

// ── Phase 29 批二：断行分类（D29-Ⅱ / C29-7——**字面常量集**，确定性禁 locale/NLS）──

namespace{

/// @brief 行首禁则字符（闭标点族——不得**独占行首**）
bool IsLineStartForbidden(char32_t cp) noexcept{
	switch (cp){
	case 0x300D: return true;   // 」右角引号
	case 0x300F: return true;   // 』右白角引号
	case 0x3011: return true;   // 】右黑角引号
	case 0x3009: return true;   // 〉右单角引号
	case 0x300B: return true;   // 》右双角引号
	case 0x3001: return true;   // 、顿号
	case 0x3002: return true;   // 。句号
	case 0xFF09: return true;   // ）全角右圆括号
	case 0xFF0C: return true;   // ，全角逗号
	case 0xFF01: return true;   // ！全角叹号
	case 0xFF1F: return true;   // ？全角问号
	case 0xFF1B: return true;   // ；全角分号
	case 0xFF1A: return true;   // ：全角冒号
	case 0x2026: return true;   // …省略号
	default:     return false;
	}
}

/// @brief 行尾禁则字符（开括号族——不得**独占行尾**）
bool IsLineEndForbidden(char32_t cp) noexcept{
	switch (cp){
	case 0x300C: return true;   // 「左角引号
	case 0x300E: return true;   // 『左白角引号
	case 0x3010: return true;   // 【左黑角引号
	case 0x3008: return true;   // 〈左单角引号
	case 0x300A: return true;   // 《左双角引号
	case 0xFF08: return true;   // （全角左圆括号
	default:     return false;
	}
}

/// @brief CJK 主体字符（可作软断点的表意/假名文字）
/// @details ★ 不含标点区（U+3000–U+303F / U+FF00–U+FFEF）——标点的断行由**禁则**管（D29-Ⅱ），
///          两者不得互相抢：若把「。」也算主体，禁则就永远没机会生效。
bool IsCjkBody(char32_t cp) noexcept{
	return (cp >= 0x4E00 && cp <= 0x9FFF)      // CJK 统一表意文字
	    || (cp >= 0x3400 && cp <= 0x4DBF)       // 扩展 A
	    || (cp >= 0x20000 && cp <= 0x2FFFF)     // 扩展 B..（辅助平面）
	    || (cp >= 0x3040 && cp <= 0x309F)       // 平假名
	    || (cp >= 0x30A0 && cp <= 0x30FF);      // 片假名
}

/// @brief 「b 之前」是否为合法断点（即 `cps[b−1]` 是否可作断前字符）
/// @details 三类（D29-Ⅰ ③）：空格后（**消费**——分隔空格不进任何一行）· 连字符后（**保留前行**）·
///          CJK 主体后。★ 前两类索引语义相同（字符都留在本行），差别只在**内容裁剪**：
///          空格由尾部空白裁剪去掉、连字符保留。
bool IsSoftBreakBefore(char32_t prev) noexcept{
	return prev == U' ' || prev == U'-' || IsCjkBody(prev);
}

/// @brief 窗口 `[lineStart, hardEnd]` 内**最后一个**合法软断点（无 ⇒ `hardEnd` = 按字硬断）
std::size_t FindLastSoftBreak(const std::vector<char32_t>& cps,
                              std::size_t lineStart, std::size_t hardEnd){
	for (std::size_t b = hardEnd; b > lineStart; --b){
		if (IsSoftBreakBefore(cps[b - 1])){ return b; }
	}
	return hardEnd;
}

}   // namespace

// ── Phase 29 批二：断行状态机（D29-Ⅰ——流水线序固定）──────────────────────

TextWidget::TextLayout TextWidget::BuildTextLayout(TextMeasurer& measurer, float maxWidth) const{

	TextLayout layout;
	const Font& font = m_style.font.value;
	const float lineHeight = measurer.LineHeight(font);

	// ★ 空文本 ⇒ 1 个空行（而非 0 行）：「零行」会让 preferred 高度归零、控件整块消失；
	//   D29-Ⅴ 的空行语义在空串上的自然延伸（T29-5 空串断言锚）。
	const std::vector<char32_t> cps = DecodeUtf8(m_text);
	if (cps.empty()){
		layout.lines.emplace_back(TextLine{ 0, 0, 0.0f });
		layout.totalHeight = lineHeight;
		return layout;
	}

	// ★ emit 一行：区间 [lineStart, lineEnd) + 尾部空白裁剪 + **行宽来源**（D29-Ⅵ / v1.1 必修②）
	//   ——禁无条件对每行调 MeasureText；只在「发生过回退」（end != fitEnd）时补测一次
	//   （FT 侧即逐码点 memo 求和 = O(n)；GDI 侧 = 一次原生 extent，记 O5）。
	const auto emitLine = [&](std::size_t lineStart, std::size_t lineEnd,
	                          std::size_t fitEnd, float fitWidth){
		std::size_t end = lineEnd;
		while (end > lineStart && cps[end - 1] == U' ') { --end; }   // ⑥ 尾部空白裁剪

		const float width = (end == fitEnd)
			? fitWidth                                            // ★ 无回退 ⇒ 直接采用（零二次测量）
			: [&]{
				const std::size_t b0 = CodepointIndexToByteOffset(m_text, lineStart);
				const std::size_t b1 = CodepointIndexToByteOffset(m_text, end);
				return measurer.MeasureText(font, m_text.substr(b0, b1 - b0)).width;
			}();
		layout.lines.emplace_back(TextLine{ lineStart, end - lineStart, width });
	};

	// ── 第一步：显式 `\n` 切硬段（D29-Ⅴ）──
	// ★ `\n` 不属任何一行；段数 = `\n` 数 + 1 ⇒ 「A\n」= 2 段（第 2 段空 ⇒ 空行）——
	//   这正是 D29-Ⅴ「首/末行可为空行」语义的来源（T29-WRAP-1 断言锚）。
	std::vector<std::pair<std::size_t, std::size_t>> segments;   // [start, end) 码点区间
	{
		std::size_t segStart = 0;
		for (std::size_t i = 0; i < cps.size(); ++i){
			if (cps[i] == U'\n'){
				segments.emplace_back(segStart, i);
				segStart = i + 1;
			}
		}
		segments.emplace_back(segStart, cps.size());
	}

	const float limit = (std::max)(0.0f, maxWidth);

	for (const auto& seg : segments){
		const std::size_t segStart = seg.first;
		const std::size_t segEnd = seg.second;

		// ★ 空段 ⇒ 空 TextLine（D29-Ⅴ：占行高、绘制零命令）
		if (segStart >= segEnd){
			layout.lines.emplace_back(TextLine{ segStart, 0, 0.0f });
			continue;
		}

		std::size_t scan = segStart;   // 段内第一个未消费码点
		while (scan < segEnd){
			// ★ 三状态变量语义互斥（v1.1 评审必修①）：lineStart emit 前恒定 / lineEnd 禁则后终态 /
			//   scan 下一行起点——**不得**混用（原稿 scan 双含义 = off-by-one 与跳空格错误根源）。
			const std::size_t lineStart = scan;

			// ① 硬边界：从 scan 起最多放多少码点（C29-1 契约——FitText 不懂空格/CJK/禁则）
			const TextFit fit = measurer.FitText(font, m_text, scan, limit);
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
			scan = nextScan;
		}
	}

	// ★ totalHeight = lines.size() × 行高（含空行——D29-Ⅴ：空行占行高但零 DrawText 命令）
	layout.totalHeight = lineHeight * static_cast<float>(layout.lines.size());
	return layout;
}

const TextWidget::TextLayout& TextWidget::GetTextLayout(TextMeasurer& measurer) const{

	// ★ C29-10：**五元组指纹显式列键**——入口每次判定（不依赖任何 setter「记得清」）。
	//   (revision, font.size, font.family, dpi, GetWidth())——与 Phase 28 preferred 指纹同构。
	int dpi = 0;
	if (const Window* window = GetWindow())
	{
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}
	const int width = GetWidth();

	if (m_wrapValid && m_wrapRevision == m_textRevision
	    && m_wrapDpi == dpi
	    && m_wrapWidth == width
	    && m_wrapFont.size == m_style.font.value.size
	    && m_wrapFont.family == m_style.font.value.family)
	{
		return m_wrapLayout;   // ★ 命中 ⇒ 零 FitText / 零 MeasureText 调用（T29-3 / T29-WRAP-3 锚）
	}

	m_wrapLayout = BuildTextLayout(measurer, static_cast<float>(width));
	m_wrapRevision = m_textRevision;
	m_wrapFont = m_style.font.value;
	m_wrapDpi = dpi;
	m_wrapWidth = width;
	m_wrapValid = true;
	return m_wrapLayout;

}

float TextWidget::BlockWidth(const TextLayout& layout) const noexcept{

	float widest = 0.0f;
	for (const TextLine& line : layout.lines)
	{
		if (line.width > widest) { widest = line.width; }
	}
	return widest;

}

// ── 文本绘制（B3：对齐策略虚方法 + 统一绘制入口）────────────

Point TextWidget::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	// 默认：水平左对齐 + 垂直居中（P7 定案；负 offsetY 合法不修正——控件比文本小是布局问题）
	(void)textWidth;

	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };

}

void TextWidget::DrawTextContent(PaintContext& ctx, int x, int y){

	// 空文本：零命令零绘制
	if (m_text.empty())
		return;

	TextMeasurer& measurer = ctx.GetTextMeasurer();   // ★ 与 MeasureText/LineHeight **同一实例**（同源）

	// ── Phase 29 批二：wrap 分支（C29-5：非 wrap 走下面 100% 原路径）──
	if (IsWrapLayoutActive())
	{
		const TextLayout& layout = GetTextLayout(measurer);
		const float lineHeight = measurer.LineHeight(m_style.font.value);
		const Color& color = m_style.foreground.value;

		// ★ 区块定位：整块按 totalHeight 垂直居中（单行对齐语义的自然推广——
		//   `CalculateTextPosition(x, y, 块宽, 块高)`，水平仍取控件的对齐策略）。
		const Point block = CalculateTextPosition(x, y, BlockWidth(layout), layout.totalHeight);

		std::size_t row = 0;
		for (const TextLine& line : layout.lines)
		{
			const float lineY = block.y + static_cast<float>(row) * lineHeight;
			++row;
			if (line.cpCount == 0)
			{
				continue;   // ★ 空行：占行高但**零 DrawText 命令**（D29-Ⅴ / C29-4）
			}
			// ★ UTF-8 切片**仅绘制时**（D29-C：布局零 substr；每行一次）
			const std::size_t b0 = CodepointIndexToByteOffset(m_text, line.startCp);
			const std::size_t b1 = CodepointIndexToByteOffset(m_text, line.startCp + line.cpCount);
			ctx.DrawText(Point{ block.x, lineY }, m_text.substr(b0, b1 - b0), color, m_style.font.value);
		}
		return;
	}

	// D5：MeasureText 拿宽（水平对齐需要宽度）
	// ★★ Phase 31（与行高口径同源）：**垂直居中改用 `LineHeight()`**
	//   原为 `textSize.height`（= `MeasureText` 的高度）——Phase 31 把 `LineHeight` 改为
	//   hhea 行框口径后，两者在 GDI 侧**不再同源**（实测 SimSun @14 差 2px、@32 差 5px）。
	//   若继续用 `textSize.height`，同一控件在 `SetWordWrap` 开关切换时**垂直位置会跳变**
	//   （wrap 路径用 `LineHeight`、非 wrap 用 `textSize.height`）。
	//   ⇒ 统一为 `LineHeight()`：**行高是行高**（契约 C31-7「三概念分离」的落地）。
	const Size textSize = ctx.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);

	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, lineHeight),
		m_text, m_style.foreground.value, m_style.font.value
	);

}

Size TextWidget::GetPreferredSize() const{

	// 9.8：有测量器（正常运行 Window / 测试注入）→ 内容测量；无 → 运行时 fallback 当前尺寸
	TextMeasurer* measurer = ResolveMeasurer();
	if (measurer == nullptr)
	{
		return Widget::GetPreferredSize();
	}

	// ── Phase 29 批二：wrap 分支（★ 与 Phase 28 指纹**正交**——盯防③，两缓存两分支）──
	// ★ 布局驱动 preferred = {GetWidth(), totalHeight}（D29-B）：宽由控件给（wrap 不自己造宽），
	//   高由行数定。★ 无有限宽 / 无窗口 ⇒ **退化单行**（走下面原路径——否则 preferred 无宽来源，
	//   AutoSize 会死循环：D29-B「wrap=true 无有限宽 ⇒ 退化单行」）。
	if (IsWrapLayoutActive())
	{
		const TextLayout& layout = GetTextLayout(*measurer);
		return Size{ static_cast<float>(GetWidth()), layout.totalHeight };
	}

	// ★ Phase 28 批二（D28-B/C28-5）：指纹短路——(revision, font, dpi) 任一变化 ⇒ miss。
	//   ★ 无窗口（无头测试 / FakeTextMeasurer 接缝）**不短路**：dpi 无来源且指纹语义不完整，
	//   退回现行为（Phase 7.2 无头测试体系零污染——契约 C28-5）。
	int dpi = 0;
	if (const Window* window = GetWindow())
	{
		dpi = static_cast<int>(std::lround(static_cast<double>(window->GetDpiScale()) * 96.0));
	}

	if (m_prefValid && dpi != 0 && m_prefRevision == m_textRevision
	    && m_prefDpi == dpi
	    && m_prefFont.size == m_style.font.value.size
	    && m_prefFont.family == m_style.font.value.family)
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

}

TextMeasurer* TextWidget::ResolveMeasurer() const{

	// const 方法内 GetWindow() 返回 const Window*——GetTextMeasurer 非 const，只读测量经 const_cast（GetLineHeight 同款先例）
	if (Window* window = const_cast<Window*>(GetWindow()))
		return &window->GetTextMeasurer();
	return nullptr;

}

Size TextWidget::DoMeasureText(TextMeasurer& measurer) const{

	// Label/Button 0 inset（§3.2 冻结）：{文本测量宽, 行高}；空文本 MeasureText 返回 {0,0} → 宽 0 诚实
	const Size textSize = measurer.MeasureText(m_style.font.value, m_text);
	const float lineHeight = measurer.LineHeight(m_style.font.value);
	return Size{ textSize.width, lineHeight };

}

}
