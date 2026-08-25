#include "ECDI/Widget/TextBox.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/UTF8.h"
#include "ECDI/Window/Window.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Theme/DefaultTheme.h"

#include <algorithm>
#include <utility>

namespace ECDI{

namespace{   // 匿名 namespace：TextBox 内部常量（不暴露）

	/// @brief 滚轮单行滚动量（8.5.2；WHEEL_DELTA=120 滚一行——16px ≈ 一行行高）
	constexpr float kScrollLinePx = 16.0f;

}

TextBox::TextBox(): TextWidget(){
	// TextWidget 构造已注入 TextStyle；TextBox 再注入 TextBoxStyle
	// （基类构造期虚函数静态派发——必须在此重新调用以覆盖 TextBox::ApplyTheme）
	ApplyTheme(GetDefaultTheme());
}

TextBox::TextBox(const std::string& text): TextWidget(text){
	// TextWidget 构造已注入 TextStyle；TextBox 再注入 TextBoxStyle
	ApplyTheme(GetDefaultTheme());
}

// ── Phase 9：主题应用与样式覆盖（D7——Apply 只更新未 Override 属性）────────

void TextBox::ApplyTheme(const Theme& theme){

	TextWidget::ApplyTheme(theme);   // ① 先注入 TextStyle（foreground/font）
	TextBoxStyle defaults = theme.GetTextBoxStyle();   // ② 再注入 TextBoxStyle
	m_style.background.Apply(defaults.background.value);
	m_style.border.Apply(defaults.border.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.selection.Apply(defaults.selection.value);
	m_style.composition.Apply(defaults.composition.value);
	m_style.caretWidth.Apply(defaults.caretWidth.value);
	m_style.padding.Apply(defaults.padding.value);
	Invalidate();

}

void TextBox::SetStyle(TextBoxStyleOverride override){

	if (override.background)    m_style.background.Set(*override.background);
	if (override.border)        m_style.border.Set(*override.border);
	if (override.borderWidth)   m_style.borderWidth.Set(*override.borderWidth);
	if (override.selection)     m_style.selection.Set(*override.selection);
	if (override.composition)   m_style.composition.Set(*override.composition);
	if (override.caretWidth)    m_style.caretWidth.Set(*override.caretWidth);
	if (override.padding)       m_style.padding.Set(*override.padding);
	Invalidate();

}

// ── 焦点可见性 ──

void TextBox::OnFocusGained(){
	m_showCaret = true;
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：获焦 → 懒创建系统 caret + 初始位置（双通道）
	// 8.5.1：获焦 → 启动光标闪烁定时器（平台层产生——TextBox 只提供 id/周期，不碰平台细节）
	if (Window* window = GetWindow()){
		window->GetPlatformWindow().StartTimer(kCaretBlinkTimer, kCaretBlinkMs);
	}
}

void TextBox::OnFocusLost(){
	m_showCaret = false;
	Invalidate();
	// 5.6 v1.0.3：失焦 → 销毁系统 caret（IME 候选窗回默认——fail-safe）
	if (Window* window = GetWindow()){
		window->DestroyTextInputCaret();
		// 8.5.1：失焦 → 停止光标闪烁定时器（顺序 = 旧 OnFocusLost → 新 OnFocusGained，已核实 Window.cpp）
		window->GetPlatformWindow().StopTimer(kCaretBlinkTimer);
	}
}

// ── 编辑操作（5.5.1.3：码点级编辑——UTF-8 变长不切字）──
// Invalidate 内嵌 = 职责契约：修改了可见状态 → 自身负责请求重绘（防职责泄漏；
// 不依赖平台重绘合并；批量编辑解耦归 Phase 7 两层结构）

void TextBox::InsertCodepoint(char32_t codepoint){
	PushUndoSnapshot();   // 8.5.3（C4）：一次用户可感知编辑 = 一次 Push（操作前完整状态）
	// 5.5.2：编辑操作自包含——有 Selection 先删选中区（IME 上屏/Ctrl+V/程序/脚本 全走这里）
	if (HasSelection())
		m_caret = DeleteSelection();   // DeleteSelection 内部已同步 m_caret + anchor
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, EncodeUTF8(codepoint));
	++m_caret;
	ClearSelection();   // 5.5.2：插入后同步 anchor——否则产生幽灵选择（anchor=插入前caret, caret=+1）
	m_needsLineRecalc = true;   // 8.5.2：文本变化 → 行缓存失效
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();     // 7.5：编辑操作实际改文本 → 通知回调（D7：仅编辑操作触发）
}

// ── 8.5.1：多码点插入（粘贴/IME Commit/程序调用共用——正式编辑语义）──

void TextBox::InsertText(const std::string& text){
	if (text.empty())
		return;   // 空串粘贴 = 空操作（不触发回调——D7 边界语义；也不产生 Undo 快照）
	PushUndoSnapshot();   // 8.5.3（C4）：粘贴/Enter 换行/程序插入——操作前状态
	// 有 Selection 先删选中区（与 InsertCodepoint 同构——粘贴覆盖选中区）
	const size_t insertAt = HasSelection() ? GetSelectionMin() : m_caret;
	if (HasSelection())
		DeleteSelection();   // 内部已同步 m_caret + anchor（min 处）
	m_caret = ReplaceTextRange(insertAt, insertAt, text);   // 纯模型操作（无副作用）
	ClearSelection();
	m_needsLineRecalc = true;   // 8.5.2：文本变化（可能含 \n——粘贴多行）→ 行缓存失效
	Invalidate();
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();
}

// ── 8.5.1：纯文本模型区间替换（C8——InsertText 与 UpdateComposition 共享的底层操作）──

size_t TextBox::ReplaceTextRange(size_t startCp, size_t endCp, const std::string& replacement){
	// 无副作用：不 Invalidate/不 Sync/不 RaiseTextChanged——副作用由调用方按语义添加
	const size_t startByte = CodepointIndexToByteOffset(m_text, startCp);
	const size_t endByte   = CodepointIndexToByteOffset(m_text, endCp);
	m_text.erase(startByte, endByte - startByte);
	if (!replacement.empty())
		m_text.insert(startByte, replacement);
	return startCp + ByteOffsetToCodepointIndex(replacement, replacement.size());
}

void TextBox::DeleteBackward(){
	// 8.5.3 重构（C4/D7）：空操作检查**前置**——无选区且头边界 = no-op（不产生无意义快照）
	if (!HasSelection() && m_caret == 0)
		return;
	PushUndoSnapshot();   // 8.5.3（C4）：删选中区或单字符——操作前状态
	// 5.5.2：有 Selection 删整个选中区（一次，非单字符）
	if (HasSelection()){
		m_caret = DeleteSelection();
		m_needsLineRecalc = true;   // 8.5.2：文本变化 → 行缓存失效
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		EnsureCaretVisible();   // 8.5.2：光标跟随滚动
		RaiseTextChanged();     // 7.5：删选中区 → 文本变化 → 通知
		return;
	}
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t prev = CodepointIndexToByteOffset(m_text, m_caret - 1);
	m_text.erase(prev, cur - prev);               // 删前一个码点的字节区间
	--m_caret;
	ClearSelection();   // 5.5.2：删除后同步 anchor（防之前残留幽灵选择）
	m_needsLineRecalc = true;   // 8.5.2：文本变化（可能删 \n——行合并）→ 行缓存失效
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();     // 7.5：实际删字符 → 通知
}

void TextBox::DeleteForward(){
	// 8.5.3 重构（C4/D7）：空操作检查**前置**——无选区且尾边界 = no-op（不产生无意义快照）
	if (!HasSelection() && m_caret >= GetCodepointCount())
		return;
	PushUndoSnapshot();   // 8.5.3（C4）：删选中区或单字符——操作前状态
	// 5.5.2：有 Selection 删整个选中区
	if (HasSelection()){
		m_caret = DeleteSelection();
		m_needsLineRecalc = true;   // 8.5.2：文本变化 → 行缓存失效
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		EnsureCaretVisible();   // 8.5.2：光标跟随滚动
		RaiseTextChanged();     // 7.5：删选中区 → 通知
		return;
	}
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t next = CodepointIndexToByteOffset(m_text, m_caret + 1);
	m_text.erase(cur, next - cur);
	ClearSelection();   // 5.5.2：DeleteForward 光标不动但同步 anchor（防之前残留）
	m_needsLineRecalc = true;   // 8.5.2：文本变化（可能删 \n——行合并）→ 行缓存失效
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：文本变化（光标可能越界）→ 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();     // 7.5：实际删字符 → 通知
}

void TextBox::MoveCaret(CaretDirection direction){
	const size_t count = GetCodepointCount();
	if (direction == CaretDirection::Left){
		if (m_caret > 0) --m_caret;
	}
	else{
		if (m_caret < count) ++m_caret;
	}
	ClearSelection();   // 5.5.2：先动 caret 再同步 anchor——顺序反了会产生"幽灵选择"（anchor=旧caret）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标移动 → 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动（跨行移动）
}

void TextBox::MoveCaretToStart(){
	m_caret = 0;
	ClearSelection();   // 5.5.2：同上（先动后清）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标移动 → 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
}

void TextBox::MoveCaretToEnd(){
	m_caret = GetCodepointCount();
	ClearSelection();   // 5.5.2：同上（先动后清）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标移动 → 更新插入点
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动（跳到末尾可能超出可视区）
}

size_t TextBox::GetCodepointCount() const{
	return ByteOffsetToCodepointIndex(m_text, m_text.size());
}

// ── 多行与滚动辅助（8.5.2；B4/B5——行缓存 + 滚动偏移 + 坐标定位）──

void TextBox::RecalculateLines(){
	// 码点级扫描（DecodeFirstCodepoint——B10 索引单位契约；不直接 m_text[i] 判断）
	m_lineStarts.clear();
	m_lineStarts.push_back(0);   // 首行恒从 0 开始
	const size_t count = GetCodepointCount();
	for (size_t i = 0; i < count; ++i){
		const size_t byte = CodepointIndexToByteOffset(m_text, i);
		const size_t nextByte = CodepointIndexToByteOffset(m_text, i + 1);
		const char32_t cp = DecodeFirstCodepoint(m_text.substr(byte, nextByte - byte));
		if (cp == U'\n')
			m_lineStarts.push_back(i + 1);   // 换行符归属行尾，下一行起始 = i+1
	}
	m_needsLineRecalc = false;
}

size_t TextBox::LineIndexFromCodepoint(size_t cp) const{
	if (m_lineStarts.empty())
		return 0;
	// 线性/二分：找最后一个 m_lineStarts[i] <= cp（行起始 ≤ 光标）
	size_t lo = 0;
	size_t hi = m_lineStarts.size() - 1;
	while (lo < hi){
		const size_t mid = lo + (hi - lo + 1) / 2;   // 上取整——偏向右侧
		if (m_lineStarts[mid] <= cp)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

std::pair<size_t, size_t> TextBox::LineRange(size_t lineIndex) const{
	// 非最后一行：下一行起始 - 1 = 本行行尾 \n 的位置（lineStarts 只由 \n 产生，-1 恒排除换行符）
	// 最后一行：行尾 = 文本末尾（无 \n 可排）
	const size_t start = m_lineStarts[lineIndex];
	const size_t end = (lineIndex + 1 < m_lineStarts.size())
		? m_lineStarts[lineIndex + 1] - 1
		: GetCodepointCount();
	return { start, end };
}

float TextBox::GetLineHeight() const{
	// 全系统唯一行高来源（8.5.2 固定行高契约——不支持逐行字体，Y/lineH 成立）
	// 空文本兜底：MeasureText("") 返回 {0,0}，LineHeight 提供精确行高
	// const 方法内 GetWindow() 返回 const Window*——GetTextMeasurer 非 const，只读测量经 const_cast
	if (Window* window = const_cast<Window*>(GetWindow()))
		return window->GetTextMeasurer().LineHeight(TextWidget::m_style.font.value);
	return 16.0f;   // 无窗口（测试环境）兜底固定行高
}

float TextBox::GetTextLeftInset() const{
	// 文本实际绘制起点 X（单一来源——OnPaint textX / CaretIndexFromPosition innerX 同源防漂移）
	// 焦点框内缩 2px + 文本起点（水平左对齐 → 内缩即起点）
	const float padding = HasFocus() ? m_style.padding.value : 0.0f;
	return padding;
}

float TextBox::GetTextAreaHeight() const{
	// 垂直可视文本区 = 控件高 − 焦点框内缩（与 GetTextAreaWidth 对称）
	const float padding = HasFocus() ? m_style.padding.value : 0.0f;
	return static_cast<float>(GetHeight()) - padding * 2.0f;
}

float TextBox::GetMaxScrollOffset() const{
	// 内容总高 = 行数 × 固定行高（含最后一行完整行高——最后一行可恰好滚到可视区底部）
	const float contentHeight = static_cast<float>(m_lineStarts.size()) * GetLineHeight();
	const float viewportHeight = GetTextAreaHeight();
	return (std::max)(0.0f, contentHeight - viewportHeight);
}

void TextBox::EnsureCaretVisible(){
	// 惰性重算（行信息消费者——EnsureCaretVisible 经 LineIndexFromCodepoint/GetMaxScrollOffset
	// 依赖 m_lineStarts；编辑操作置 m_needsLineRecalc 后首个消费点在此，须先重算否则 maxScroll=0）
	if (m_needsLineRecalc)
		RecalculateLines();
	// 光标跟随滚动（上/下边界语义——8.5.2 基础设施，编辑/光标移动统一调用）
	const float lineH = GetLineHeight();
	const float caretTop = static_cast<float>(LineIndexFromCodepoint(m_caret)) * lineH;
	const float caretBottom = caretTop + lineH;
	const float viewportTop = m_scrollOffsetY;
	const float viewportBottom = m_scrollOffsetY + GetTextAreaHeight();
	if (caretTop < viewportTop)
		m_scrollOffsetY = caretTop;   // 上边界：滚到光标顶
	else if (caretBottom > viewportBottom)
		m_scrollOffsetY = caretBottom - GetTextAreaHeight();   // 下边界：滚到光标底
	m_scrollOffsetY = (std::clamp)(m_scrollOffsetY, 0.0f, GetMaxScrollOffset());
}

void TextBox::OnMouseWheel(const MouseWheelEvent& event){
	// 惰性重算（GetMaxScrollOffset 依赖 m_lineStarts——首个消费点自保证缓存，同 EnsureCaretVisible 契约）
	if (m_needsLineRecalc)
		RecalculateLines();
	// 滚轮：向上（delta>0）内容上移（scrollOffsetY 减小）——自然方向
	m_scrollOffsetY -= static_cast<float>(event.GetDelta()) / 120.0f * kScrollLinePx;
	m_scrollOffsetY = (std::clamp)(m_scrollOffsetY, 0.0f, GetMaxScrollOffset());
	Invalidate();
	SyncTextInputCaret();   // 5.6 债务兑现：候选窗位置与滚动联动
}

// ── 光标几何（5.6 提取：与点击定位同源——CalculateTextPosition 单一入口）──

float TextBox::GetTextAreaWidth() const noexcept{
	// 可视宽度 = 控件宽 − 焦点框内缩（2px×2）——与 OnPaint 原 maxTextWidth 同款逻辑
	// （提取为共享辅助：文本裁切/Selection 高亮/光标 三处共用，改一处不漂移）
	const float padding = HasFocus() ? m_style.padding.value : 0.0f;
	return static_cast<float>(GetWidth()) - padding * 2.0f;
}

Point TextBox::CalculateCaretPosition(TextMeasurer& measurer) const{
	// 8.5.2 多行版：行号定 y（- scrollOffsetY），行内前缀定 x——与绘制/点击定位同源
	// 全系统统一 GetLineHeight()（不用整段文本 MeasureText 高度——那是多行总高非行高，GPT 修正）
	if (m_needsLineRecalc)
		const_cast<TextBox*>(this)->RecalculateLines();   // 惰性重算（缓存 mutable 语义）
	const size_t lineIndex = LineIndexFromCodepoint(m_caret);
	const size_t startByte = CodepointIndexToByteOffset(m_text, m_lineStarts[lineIndex]);
	const size_t caretByte = CodepointIndexToByteOffset(m_text, m_caret);
	const float lineH = GetLineHeight();
	const Size prefixSize = measurer.MeasureText(TextWidget::m_style.font.value, m_text.substr(startByte, caretByte - startByte));
	// 可视钳制：光标超出可视区钉在右缘（水平溢出契约——无水平滚动，右边界 clamp）
	const float caretX = (std::min)(prefixSize.width, GetTextAreaWidth());
	const float caretY = static_cast<float>(lineIndex) * lineH - m_scrollOffsetY;
	// 返回光标顶部（系统 caret 语义 = caret 左上角）；float 直传（Point 成员 float——narrowing 隐患）
	// Y 与 X 同源加内缩——光标与文本第一行顶部对齐（绘制 textY = fy + inset 同一几何）
	return Point{ GetTextLeftInset() + caretX, GetTextLeftInset() + caretY };
}

CaretGeometry TextBox::GetCaretClientGeometry(){
	// 客户区绝对坐标 = 控件绝对位置 + 光标相对控件位置（同一 CalculateCaretPosition 变换链）
	const Point abs = GetAbsolutePosition();
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();
	const Point local = CalculateCaretPosition(measurer);
	// 光标尺寸：宽 m_style.caretWidth.value（与 OnPaint 竖线同源）+ 高 GetLineHeight()（8.5.2 统一行高来源）
	return CaretGeometry{
		Rect{ abs.x + local.x, abs.y + local.y, m_style.caretWidth.value, GetLineHeight() },
		m_showCaret   // 逻辑可见性：焦点显示 / 失焦隐藏（false → 平台层 HideCaret）
	};
}

void TextBox::SyncTextInputCaret(){
	// 5.6 v1.0.3：光标变动 → 更新系统 caret + ImmSetCompositionWindow 双通道
	// （测量经 Window——GetCaretClientGeometry 与点击定位同源；失焦走 DestroyTextInputCaret）
	if (Window* window = GetWindow()){
		window->UpdateTextInputCaret(GetCaretClientGeometry());
	}
}

// ── Selection 辅助（5.5.2；anchor + caret 模型——Selection 扩张/收缩的核心）──

bool TextBox::HasSelection() const noexcept{
	return m_selectionAnchor != m_caret;
}

size_t TextBox::GetSelectionMin() const noexcept{
	return (std::min)(m_selectionAnchor, m_caret);
}

size_t TextBox::GetSelectionMax() const noexcept{
	return (std::max)(m_selectionAnchor, m_caret);
}

size_t TextBox::DeleteSelection(){
	const size_t minCp = GetSelectionMin();
	const size_t maxCp = GetSelectionMax();
	const size_t minByte = CodepointIndexToByteOffset(m_text, minCp);
	const size_t maxByte = CodepointIndexToByteOffset(m_text, maxCp);
	m_text.erase(minByte, maxByte - minByte);
	// 5.5.2 时序修复：内部直接设好新光标并同步 anchor——返回前 caret 已是最终值，
	// 调用方 `m_caret = DeleteSelection()` 冗余但无害；避免"调用方赋值后 anchor 仍是删前值"产生幽灵选择
	m_caret = minCp;
	m_selectionAnchor = m_caret;
	return minCp;   // 新光标 = 删除起始处
}

void TextBox::ClearSelection() noexcept{
	m_selectionAnchor = m_caret;   // 锚点无效化（anchor == caret = 无选择）
}

// ── 选择查询（7.2 新增）──────────────────────────────

std::optional<TextBox::SelectionRange> TextBox::GetSelection() const {
	if (m_selectionAnchor == m_caret) {
		return std::nullopt;
	}
	return SelectionRange{
		(std::min)(m_selectionAnchor, m_caret),
		(std::max)(m_selectionAnchor, m_caret)
	};
}

// ── 点击定位算法（8.5.2 多行版：替代 5.5.1.4 单行 CaretIndexFromX——B9）──

size_t TextBox::CaretIndexFromPosition(Point localPos){
	if (m_text.empty())
		return 0;
	if (m_needsLineRecalc)
		RecalculateLines();   // 惰性重算（非 const——直接调）
	// 1. Y 定行：可视 Y − 文本区原点（内缩） + scrollOffsetY → 逻辑 Y → 行号
	//（Y 映射契约：任意 Y → 最近有效文本行；与 X 的 innerX = localPos.x − inset 同源对称）
	const float lineH = GetLineHeight();
	const int logicalLine = (std::max)(0,
		static_cast<int>((localPos.y - GetTextLeftInset() + m_scrollOffsetY) / lineH));
	const size_t lineIndex = (std::min)(static_cast<size_t>(logicalLine), m_lineStarts.size() - 1);
	// 2. X 定行内（提取的辅助——CaretIndexFromLineX 与 Up/Down 跨行共用，防漂移）
	const float innerX = localPos.x - GetTextLeftInset();
	return CaretIndexFromLineX(lineIndex, innerX);
}

size_t TextBox::CaretIndexFromLineX(size_t lineIndex, float innerX) const{
	// 行内线性扫描 + 前缀测量（O(行长²) 短行够用——同 8.5.1 单行 CaretIndexFromX 逻辑按行）
	const auto [lineStartCp, lineEndCp] = LineRange(lineIndex);
	const size_t startByte = CodepointIndexToByteOffset(m_text, lineStartCp);
	const size_t endByte   = CodepointIndexToByteOffset(m_text, lineEndCp);
	const std::string lineText = m_text.substr(startByte, endByte - startByte);
	size_t lineInnerCp = 0;
	const size_t lineCpCount = ByteOffsetToCodepointIndex(lineText, lineText.size());
	// 无窗口（测试环境）跳过测量 → 返回行起始（行内 X 定位需最小窗口集成测试——同 F19b/F20b 债务）
	if (!lineText.empty() && GetWindow() != nullptr){
		// const 方法内 GetWindow() 返回 const Window*——GetTextMeasurer 非 const，只读测量经 const_cast（同 GetLineHeight）
		TextMeasurer& measurer = const_cast<Window*>(GetWindow())->GetTextMeasurer();
		for (size_t i = 0; i < lineCpCount; ++i){
			const size_t byte = CodepointIndexToByteOffset(lineText, i + 1);
			const Size prefix = measurer.MeasureText(TextWidget::m_style.font.value, lineText.substr(0, byte));
			if (innerX <= prefix.width){
				// 落在第 i 个码点区间：中点判断四舍五入（左半 → i，右半 → i+1）
				const Size prevPrefix = (i == 0) ? Size{}
					: measurer.MeasureText(TextWidget::m_style.font.value, lineText.substr(0, CodepointIndexToByteOffset(lineText, i)));
				const float charWidth = prefix.width - prevPrefix.width;
				lineInnerCp = (innerX >= prevPrefix.width + charWidth / 2.0f) ? i + 1 : i;
				break;
			}
			lineInnerCp = i + 1;   // 未 break → 点在行尾
		}
	}
	return lineStartCp + lineInnerCp;   // 全局码点索引 = 行起始 + 行内
}

void TextBox::ResetPreferredColumn(){
	// 目标列 = 当前光标 X（Up/Down 跨行保持的基准；非跨行移动后重置）
	if (Window* window = GetWindow())
		m_preferredColumn = CalculateCaretPosition(window->GetTextMeasurer()).x;
	else
		m_preferredColumn = 0.0f;   // 无窗口（测试）兜底
}

// ── 双击选词（8.5.2；B7/C5——code point 级：word 整词 / non-word 单码点）──

std::pair<size_t, size_t> TextBox::GetWordBounds(size_t clickIndex) const{
	const size_t count = GetCodepointCount();
	if (count == 0)
		return { 0, 0 };
	if (clickIndex >= count)
		clickIndex = count - 1;   // 点击末尾 → clamp 到最后一个字符
	const auto IsWordChar = [](char32_t cp){
		// 英文词字符 = ASCII 字母/数字；其余（空格/标点/中文/Emoji/\n）→ non-word
		return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || (cp >= U'0' && cp <= U'9');
	};
	// 点击字符本身是否 word——决定扩展方向（non-word → 单码点，行为锁死 GPT）
	const size_t clickByte = CodepointIndexToByteOffset(m_text, clickIndex);
	const size_t clickNextByte = CodepointIndexToByteOffset(m_text, clickIndex + 1);
	const char32_t clickCp = DecodeFirstCodepoint(m_text.substr(clickByte, clickNextByte - clickByte));
	if (!IsWordChar(clickCp))
		return { clickIndex, clickIndex + 1 };   // non-word → 选中该单个 code point
	// word 字符：向前/向后扩展到 word 边界
	size_t start = clickIndex;
	while (start > 0){
		const size_t prevByte = CodepointIndexToByteOffset(m_text, start - 1);
		const size_t curByte  = CodepointIndexToByteOffset(m_text, start);
		const char32_t prevCp = DecodeFirstCodepoint(m_text.substr(prevByte, curByte - prevByte));
		if (!IsWordChar(prevCp)) break;
		--start;
	}
	size_t end = clickIndex;
	while (end < count){
		const size_t curByte  = CodepointIndexToByteOffset(m_text, end);
		const size_t nextByte = CodepointIndexToByteOffset(m_text, end + 1);
		const char32_t curCp = DecodeFirstCodepoint(m_text.substr(curByte, nextByte - curByte));
		if (!IsWordChar(curCp)) break;
		++end;
	}
	return { start, end };
}

// ── 纯数据访问 ──

size_t TextBox::GetCaret() const noexcept{
	return m_caret;
}

// ── 回调通知（7.5：D4 三段式——RaiseTextChanged 内部先虚方法后回调，彼此独立）──

void TextBox::RaiseTextChanged(){

	OnTextChanged(m_text);            // ① 虚方法（子类可 override 扩展）

	if (m_onTextChanged)              // ② 回调（独立通道，override 无法吞掉）

		m_onTextChanged(m_text);

}

void TextBox::OnTextChanged(const std::string& /*text*/){}

void TextBox::SetOnTextChanged(TextChangedCallback callback){

	m_onTextChanged = std::move(callback);

}

// ── 8.5.1：IME Composition（模型 B——覆盖 m_text 临时区间；C7：Update ≠ Commit）──

void TextBox::UpdateComposition(const std::string& compositionText){

	if (!m_isComposing){
		// 首次：组合开始——标记起点 + Push 一次快照（C3：一次组合 = 一次 Undo 单元）
		m_isComposing = true;
		m_compositionStart = m_caret;
		if (!compositionText.empty()){
			PushUndoSnapshot();   // 组合开始前状态（Ctrl+Z 一次撤销整个组合；Commit 不再 Push）
			m_compositionPushedUndo = true;
		}
	}
	// 替换组合区间（模型 B：m_text 含组合串；ReplaceTextRange 无副作用——不触发 TextChanged）
	m_caret = ReplaceTextRange(
		m_compositionStart, m_compositionStart + m_compositionLength, compositionText);
	m_compositionLength = ByteOffsetToCodepointIndex(compositionText, compositionText.size());
	m_compositionText = compositionText;
	m_compositionCaret = m_compositionLength;   // C9：8.5.1 固定组合末尾
	ClearSelection();
	m_needsLineRecalc = true;   // 8.5.2：组合串替换 m_text（可能含 \n）→ 行缓存失效
	Invalidate();              // 视觉更新（临时编辑也需重绘）
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	// ⚠️ 不 RaiseTextChanged（C8：Composition Update ≠ 正式编辑；空串 = 组合中无内容，组合仍在）
}

void TextBox::CommitComposition(const std::string& resultText){

	if (!m_isComposing)
		return;   // 无组合中 → no-op（fail-safe）
	// 组合区间 → resultText（正式文本）；清组合标记（C12：空串 = 合法空结果提交）
	m_caret = ReplaceTextRange(
		m_compositionStart, m_compositionStart + m_compositionLength, resultText);
	m_isComposing = false;
	m_compositionText.clear();
	m_compositionLength = 0;
	m_compositionStart = m_caret;   // 组合结束光标 = 结果末尾
	m_compositionCaret = 0;
	ClearSelection();
	m_needsLineRecalc = true;   // 8.5.2：组合结果可能含 \n → 行缓存失效
	Invalidate();
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();   // ✅ Commit = 正式编辑（C3：进 Undo 历史——快照在组合开始时已 Push）
	// ⚠️ 不 PushUndoSnapshot——组合开始前已 Push（Ctrl+Z 一次撤销整个组合，C3）
}

void TextBox::CancelComposition(){

	if (!m_isComposing)
		return;
	if (m_compositionPushedUndo && !m_undoStack.empty()){
		// 🔴 v1.1（GPT）：组合串已内嵌 m_text（模型 B）——仅弹栈会残留拼音占位。
		// 正确语义 = 恢复组合开始前快照（正文/光标/选区/滚动整体回滚），且不产生 redo 条目。
		// Cancel vs Undo：恢复✅ / 移除快照✅ / 进 Redo❌（取消不是编辑操作）。
		const UndoSnapshot snapshot = m_undoStack.back();
		m_undoStack.pop_back();
		RestoreSnapshot(snapshot);
		m_compositionPushedUndo = false;
		m_isComposing = false;
		m_compositionText.clear();
		m_compositionLength = 0;
		m_compositionCaret = 0;
		return;   // 状态已完全恢复——跳过原擦除逻辑
	}
	// 未 Push 过的组合（首帧即空串组合——无实际文本变化）：走原擦除占位兜底
	m_caret = m_compositionStart;
	ReplaceTextRange(m_compositionStart, m_compositionStart + m_compositionLength, {});
	m_isComposing = false;
	m_compositionText.clear();
	m_compositionLength = 0;
	m_compositionCaret = 0;
	ClearSelection();
	m_needsLineRecalc = true;   // 8.5.2：组合占位擦除 → 行缓存失效
	Invalidate();
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
}

// ── 8.5.1：剪贴板（C1——Ctrl 组合是 KeyDown 语义动作，剪贴板是 Platform capability）──

void TextBox::CopySelectionToClipboard(){

	if (!HasSelection())
		return;   // 无选区 = 空操作（不写剪贴板——避免误清，C10 语义闭合）
	Window* window = GetWindow();
	if (window == nullptr)
		return;   // 无窗口（测试环境）→ 静默跳过（不崩）
	const size_t minByte = CodepointIndexToByteOffset(m_text, GetSelectionMin());
	const size_t maxByte = CodepointIndexToByteOffset(m_text, GetSelectionMax());
	window->GetPlatformWindow().SetClipboardText(m_text.substr(minByte, maxByte - minByte));
}

void TextBox::CutSelectionToClipboard(){

	if (!HasSelection())
		return;   // 无选区 = 空操作（D7——不 Push）
	PushUndoSnapshot();   // 8.5.3（C4）：剪切 = 复制 + 删选中区（一次编辑，一次快照）
	CopySelectionToClipboard();
	// 删选中区（正式编辑语义——DeleteSelection 内部同步 caret/anchor）
	m_caret = DeleteSelection();
	ClearSelection();
	m_needsLineRecalc = true;   // 8.5.2：剪切可能含 \n → 行缓存失效
	Invalidate();
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动
	RaiseTextChanged();
}

void TextBox::PasteFromClipboard(){

	Window* window = GetWindow();
	if (window == nullptr)
		return;   // 无窗口（测试环境）→ 静默跳过
	InsertText(window->GetPlatformWindow().GetClipboardText());
}

void TextBox::SelectAll(){

	m_caret = GetCodepointCount();
	m_selectionAnchor = 0;
	Invalidate();
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跳到末尾（可能超出可视区）
}

// ── 8.5.3：Undo/Redo（快照模式 B6——编辑前 Push，C4 契约；GPT 评审整合 v1.1）──

void TextBox::Undo(){

	// C3b（GPT 🟡）：组合态忽略——IME 占用输入通道，键盘撤销不中断组合（fail-safe 不崩）
	if (m_isComposing)
		return;
	if (m_undoStack.empty())
		return;   // 空栈 no-op（F38）
	m_redoStack.push_back(CaptureCurrentState());   // C4：当前状态 → redo 栈（Undo 可逆）
	RestoreSnapshot(m_undoStack.back());
	m_undoStack.pop_back();
}

void TextBox::Redo(){

	if (m_isComposing)
		return;   // C3b
	if (m_redoStack.empty())
		return;   // 空栈 no-op
	m_undoStack.push_back(CaptureCurrentState());   // 对称：当前状态 → undo 栈
	RestoreSnapshot(m_redoStack.back());
	m_redoStack.pop_back();
}

void TextBox::PushUndoSnapshot(){

	m_undoStack.push_back(CaptureCurrentState());   // 编辑前状态
	if (m_undoStack.size() > kMaxUndoDepth)
		m_undoStack.erase(m_undoStack.begin());     // 超限丢最旧（深度契约）
	m_redoStack.clear();                            // C4b：新编辑作废旧分支
}

TextBox::UndoSnapshot TextBox::CaptureCurrentState() const{

	UndoSnapshot snapshot;
	snapshot.text = m_text;
	snapshot.caret = m_caret;
	if (HasSelection()){
		// D9：存 {min, max} 固定正向——恢复时 anchor=start、caret=end（方向信息 MVP 不存）
		snapshot.selection = SelectionRange{ GetSelectionMin(), GetSelectionMax() };
	}
	snapshot.scrollOffsetY = m_scrollOffsetY;
	return snapshot;
}

void TextBox::RestoreSnapshot(const UndoSnapshot& snapshot){

	m_text = snapshot.text;
	m_caret = snapshot.caret;
	if (snapshot.selection.has_value()){
		m_selectionAnchor = snapshot.selection->start;   // D9：anchor=min
		m_caret = snapshot.selection->end;               // caret=max
	}
	else{
		ClearSelection();
	}
	m_scrollOffsetY = snapshot.scrollOffsetY;
	m_needsLineRecalc = true;      // 文本恢复 → 行缓存失效（EnsureCaretVisible 内部惰性重算）
	// 顺序（GPT 第 7 点）：恢复文本 → 修正滚动 → 更新视觉 → 更新 IME caret → 通知外部
	EnsureCaretVisible();          // 文本高度变化后的 scroll clamp（依赖新行结构）
	Invalidate();
	SyncTextInputCaret();
	RaiseTextChanged();            // D4：文本实际变化 → 外部观察者感知
}

// ── 8.5.1：光标闪烁（C2——平台产生 Timer，TextBox 消费事实）──

void TextBox::OnTimer(const TimerEvent& event){

	if (event.GetTimerId() == kCaretBlinkTimer){
		// 焦点防御（GPT 检查点 2）：失焦→获焦切换瞬间，旧控件可能收到排队中的
		// 最后一次 TimerEvent（SetFocusedWidget 顺序 = 旧 OnFocusLost → 新 OnFocusGained）。
		// 仅"有窗口且无焦点"时忽略（防排队消息翻转头像）；无窗口（测试环境）允许验证翻转逻辑。
		if (GetWindow() != nullptr && !HasFocus())
			return;   // 已失焦 → 忽略（不闪）
		m_showCaret = !m_showCaret;   // 切换可见性（视觉闪烁）
		Invalidate();
	}
}

// ── 事件映射（事件 → 编辑操作，薄薄一层；逻辑集中在上面）──

void TextBox::OnMouseButtonDown(const MouseButtonDownEvent& event){
	// 焦点获取由 Application 前置处理（5.4.2：CanFocus → SetFocusedWidget）
	// 坐标系：事件 GetMouseX = 窗口客户区绝对；GetAbsolutePosition = 客户区绝对
	const Point abs = GetAbsolutePosition();
	// 8.5.2：双击（平台层系统判定——WM_LBUTTONDBLCLK）→ 选词（GetWordBounds）
	if (event.IsDoubleClick()){
		const size_t clickIndex = CaretIndexFromPosition(
			Point{ static_cast<float>(event.GetMouseX()) - abs.x,
			       static_cast<float>(event.GetMouseY()) - abs.y });
		const auto [startCp, endCp] = GetWordBounds(clickIndex);
		m_selectionAnchor = startCp;
		m_caret = endCp;
		m_mouseDown = false;   // 双击选词后不进入拖选（拖选由后续单 Down 重新触发）
		Invalidate();
		SyncTextInputCaret();
		ResetPreferredColumn();   // 8.5.2 补全：双击定位后重置目标列（点击类操作）
		return;
	}
	// 8.5.2：多行坐标定位（CaretIndexFromPosition——Y 定行 + X 定行内 + 全局索引；替代单行 CaretIndexFromX）
	m_caret = CaretIndexFromPosition(
		Point{ static_cast<float>(event.GetMouseX()) - abs.x,
		       static_cast<float>(event.GetMouseY()) - abs.y });
	m_selectionAnchor = m_caret;   // 5.5.2：锚点 = 点击处（准备拖选）
	m_mouseDown = true;            // 5.5.2：拖选进行中（Capture ≠ 拖选中，TextBox 内部状态）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：点击定位 → 更新插入点
	ResetPreferredColumn();   // 8.5.2 补全：点击定位后重置目标列（Up/Down 的基准 = 点击处）
}

void TextBox::OnMouseMove(const MouseMoveEvent& event){
	if (!m_mouseDown)
		return;   // 非拖选中：普通移动忽略（避免误扩展选择）
	// 拖选：同源定位（与 OnMouseButtonDown 同款——8.5.2 多行坐标）——active 端跟随鼠标
	const Point abs = GetAbsolutePosition();
	m_caret = CaretIndexFromPosition(
		Point{ static_cast<float>(event.GetMouseX()) - abs.x,
		       static_cast<float>(event.GetMouseY()) - abs.y });
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：拖选中 active 端跟随 → 更新插入点
}

void TextBox::OnMouseButtonUp(const MouseButtonUpEvent&){
	m_mouseDown = false;   // 结束拖选（保留选择）
}

void TextBox::OnKeyDown(const KeyDownEvent& event){
	// 8.5.1：Ctrl 组合优先（剪贴板/全选）——KeyDown 是事实，Copy/Paste 是 TextBox 的语义解释（C1）；
	// 无 Ctrl 时继续原有编辑键映射（8.5.3 加 Ctrl+Z/Y）
	if (event.IsCtrlDown()){
		// Ctrl 组合 = KeyDown 语义（C1）——break 走公共尾部（SyncTextInputCaret/EnsureCaretVisible/
		// ResetPreferredColumn 统一执行：Ctrl+V 粘贴多行后光标需滚动可见，Ctrl+X/V 编辑后需重置目标列）
		switch (event.GetKeyCode()){
		case KeyCode::C:    CopySelectionToClipboard(); break;
		case KeyCode::V:    PasteFromClipboard();        break;
		case KeyCode::X:    CutSelectionToClipboard();   break;
		case KeyCode::A:    SelectAll();                 break;
		case KeyCode::Z:    Undo();                      break;   // 8.5.3
		case KeyCode::Y:    Redo();                      break;   // 8.5.3
		default: break;   // 其余 Ctrl 组合交默认（无动作）
		}
		SyncTextInputCaret();
		EnsureCaretVisible();
		if (!(event.GetKeyCode() == KeyCode::Up || event.GetKeyCode() == KeyCode::Down))
			ResetPreferredColumn();
		return;
	}
	switch (event.GetKeyCode()){
	case KeyCode::Backspace: DeleteBackward(); break;
	case KeyCode::Delete:    DeleteForward();  break;
	case KeyCode::Enter:     InsertText("\n"); break;   // 8.5.2：显式换行（多行）
	case KeyCode::Left:
		// 5.5.2：Shift+← 扩展选择（anchor 固定、active=caret 移动）；无 Shift 清选择再移动
		if (event.IsShiftDown()){
			if (m_caret > 0) --m_caret;
		}
		else{
			if (m_caret > 0) --m_caret;
			ClearSelection();   // 先动后清——顺序反了产生幽灵选择（anchor=旧caret）
		}
		Invalidate();
		break;
	case KeyCode::Right:
		if (event.IsShiftDown()){
			if (m_caret < GetCodepointCount()) ++m_caret;
		}
		else{
			if (m_caret < GetCodepointCount()) ++m_caret;
			ClearSelection();
		}
		Invalidate();
		break;
	case KeyCode::Home:
		if (event.IsShiftDown()){
			m_caret = 0;
		}
		else{
			m_caret = 0;
			ClearSelection();
		}
		Invalidate();
		break;
	case KeyCode::End:
		if (event.IsShiftDown()){
			m_caret = GetCodepointCount();
		}
		else{
			m_caret = GetCodepointCount();
			ClearSelection();
		}
		Invalidate();
		break;
	case KeyCode::Up:
	case KeyCode::Down: {
		// 8.5.2 补全：上下跨行（m_preferredColumn 保持目标列——标准编辑器行为）
		if (m_needsLineRecalc)
			RecalculateLines();
		const size_t curLine = LineIndexFromCodepoint(m_caret);
		const bool goingUp = (event.GetKeyCode() == KeyCode::Up);
		// 边界：第一行 Up / 最后一行 Down → 不移动（no-op）
		if ((goingUp && curLine == 0) || (!goingUp && curLine + 1 >= m_lineStarts.size()))
			break;
		const size_t targetLine = goingUp ? curLine - 1 : curLine + 1;
		// 行内 X 定位（preferred column——跨行保持列；目标行较短自动 clamp 行尾）
		m_caret = CaretIndexFromLineX(targetLine, m_preferredColumn);
		// Shift+↑/↓ 扩展选择（anchor 固定、active=caret 移动——同 Left/Right 模式）
		if (!event.IsShiftDown())
			ClearSelection();
		Invalidate();
		break;
	}
	default: break;   // 其他键（含 Tab——被 Window::HandleKeyDown 拦截，到不了这里）
	}

	// 8.5.2 补全：非跨行键 → 重置目标列 = 当前光标 X（Up/Down 不重置——保持列跨行）
	if (!(event.GetKeyCode() == KeyCode::Up || event.GetKeyCode() == KeyCode::Down))
		ResetPreferredColumn();

	// 5.6 v1.0.3：方向键/Home/End 分支都改过 caret——switch 后统一同步插入点
	SyncTextInputCaret();
	EnsureCaretVisible();   // 8.5.2：光标跟随滚动（跨行移动/Home/End 可能超出可视区）
}

void TextBox::OnCharInput(const CharInputEvent& event){
	const char32_t cp = event.GetCodepoint();
	// 过滤控制字符（Event 原则"语义判断推迟给消费者"——TextBox 就是消费者）：
	// Backspace(0x08)/Delete(0x7F) 的 WM_CHAR 通道丢弃（编辑走 OnKeyDown 的 KeyCode 路径）
	if (cp < 0x20 || cp == 0x7F)
		return;
	InsertCodepoint(cp);
}

// ── 绘制（5.5.1.4 完整版：白底 + 焦点框 + 文本 + 光标同源）──

void TextBox::OnPaint(PaintContext& ctx, int x, int y){
	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);
	const float fw = static_cast<float>(GetWidth());
	const float fh = static_cast<float>(GetHeight());

	// 1. 背景 + 焦点框（Phase 9：颜色/内缩全部来自 TextBoxStyle）
	if (HasFocus()){
		// 焦点框：全块 border 色 → 内缩 padding 背景色，露出边框环（与 Button 焦点方案相反配色）
		const float pad = m_style.padding.value;
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, m_style.border.value);
		ctx.DrawRect(Rect{ fx + pad, fy + pad, fw - 2.0f * pad, fh - 2.0f * pad }, m_style.background.value);
	}
	else{
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, m_style.background.value);
	}

	// 2. 文本（8.5.2 多行逐行版：行缓存 → 可视行裁剪 → 每行 Selection 求交 + 绘制 + 组合串下划线）
	if (m_text.empty() && !m_showCaret)
		return;   // 空文本且无光标：省测量（光杆背景已画）

	if (m_needsLineRecalc)
		RecalculateLines();   // 惰性重算（绘制首次触发）
	const float lineH = GetLineHeight();
	// 文本区原点 = 控件客户区绝对位置 + 内缩（焦点态 2px / 非焦点 0）——与 8.5.1 单行版
	// CalculateTextPosition(x, y, ...) 同构（控件位置 + 相对偏移），多行改为顶部对齐
	const float inset = GetTextLeftInset();
	const float textX = fx + inset;   // 客户区绝对 X（绘制/Selection/下划线同源）
	const float textY = fy + inset;   // 客户区绝对 Y（顶部对齐 + 焦点框内缩——与 X 同值内缩）
	const size_t lineCount = m_lineStarts.size();
	// 可视行范围（滚动裁剪——不画屏幕外的行）
	const int firstVisible = (std::max)(0, static_cast<int>(m_scrollOffsetY / lineH));
	const int lastVisible  = (std::min)(static_cast<int>(lineCount) - 1,
		static_cast<int>((m_scrollOffsetY + fh) / lineH) + 1);
	const float maxTextWidth = GetTextAreaWidth();   // 可视宽（裁切 clamp）

	for (int li = firstVisible; li <= lastVisible; ++li){
		const size_t lineIndex = static_cast<size_t>(li);
		const auto [startCp, endCp] = LineRange(lineIndex);
		const size_t startByte = CodepointIndexToByteOffset(m_text, startCp);
		const size_t endByte   = CodepointIndexToByteOffset(m_text, endCp);
		const std::string lineText = m_text.substr(startByte, endByte - startByte);
		const float lineY = textY + static_cast<float>(li) * lineH - m_scrollOffsetY;

		// ① 行内 Selection 高亮（[selMin, selMax] ∩ [startCp, endCp) 求交——逐行）
		if (HasSelection()){
			const size_t selMin = GetSelectionMin();
			const size_t selMax = GetSelectionMax();
			if (selMin < endCp && selMax > startCp){   // 有交集
				const size_t hlStartCp = (std::max)(selMin, startCp);
				const size_t hlEndCp   = (std::min)(selMax, endCp);
				const size_t hlStartByte = CodepointIndexToByteOffset(m_text, hlStartCp);
				const size_t hlEndByte   = CodepointIndexToByteOffset(m_text, hlEndCp);
				const Size hlMinSize = ctx.MeasureText(TextWidget::m_style.font.value, m_text.substr(startByte, hlStartByte - startByte));
				const Size hlMaxSize = ctx.MeasureText(TextWidget::m_style.font.value, m_text.substr(startByte, hlEndByte - startByte));
				const float hlMin = (std::min)(hlMinSize.width, maxTextWidth);
				const float hlMax = (std::min)(hlMaxSize.width, maxTextWidth);
				ctx.DrawRect(Rect{ textX + hlMin, lineY, hlMax - hlMin, lineH }, m_style.selection.value);
			}
		}

		// ② 行文本绘制（超宽逐码点裁切——MVP 同 8.5.1 单行逻辑，按行）
		if (!lineText.empty()){
			const Size lineSize = ctx.MeasureText(TextWidget::m_style.font.value, lineText);
			if (lineSize.width <= maxTextWidth){
				ctx.DrawText(Point{ textX, lineY }, lineText, TextWidget::m_style.foreground.value, TextWidget::m_style.font.value);
			}
			else{
				size_t visibleCps = 0;
				const size_t lineCpCount = ByteOffsetToCodepointIndex(lineText, lineText.size());
				for (size_t i = 0; i < lineCpCount; ++i){
					const size_t byte = CodepointIndexToByteOffset(lineText, i + 1);
					if (ctx.MeasureText(TextWidget::m_style.font.value, lineText.substr(0, byte)).width > maxTextWidth)
						break;
					visibleCps = i + 1;
				}
				if (visibleCps > 0)
					ctx.DrawText(Point{ textX, lineY },
						lineText.substr(0, CodepointIndexToByteOffset(lineText, visibleCps)),
						TextWidget::m_style.foreground.value, TextWidget::m_style.font.value);
			}
		}

		// ③ 组合串下划线（8.5.1 设计承诺 §4.4 补欠账——组合区间在本行时画底线）
		if (m_isComposing && m_compositionLength > 0){
			const size_t compStartCp = m_compositionStart;
			const size_t compEndCp = m_compositionStart + m_compositionLength;
			if (compStartCp < endCp && compEndCp > startCp){   // 组合区间 ∩ 本行
				const size_t ulStartCp = (std::max)(compStartCp, startCp);
				const size_t ulEndCp   = (std::min)(compEndCp, endCp);
				const size_t ulStartByte = CodepointIndexToByteOffset(m_text, ulStartCp);
				const size_t ulEndByte   = CodepointIndexToByteOffset(m_text, ulEndCp);
				const Size ulStartSize = ctx.MeasureText(TextWidget::m_style.font.value, m_text.substr(startByte, ulStartByte - startByte));
				const Size ulEndSize   = ctx.MeasureText(TextWidget::m_style.font.value, m_text.substr(startByte, ulEndByte - startByte));
				ctx.DrawRect(Rect{ textX + ulStartSize.width, lineY + lineH - 1.0f,
					ulEndSize.width - ulStartSize.width, 1.0f }, m_style.composition.value);
			}
		}
	}

	// 3. 光标竖线（8.5.2 多行 CalculateCaretPosition——行号定 y + scrollOffsetY + GetTextLeftInset）
	if (m_showCaret){
		// 测量经 Window：PaintContext 封装不暴露 measurer（决策 8）——结果一致
		// CalculateCaretPosition 返回相对控件原点（含 GetTextLeftInset + scrollOffsetY）
		const Point caretLocal = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
		// 7.1.3：宽度用 m_style.caretWidth.value（与 CaretGeometry 输出同源——F2，不散落魔法数字）
		ctx.DrawRect(Rect{ fx + caretLocal.x, fy + caretLocal.y, m_style.caretWidth.value, lineH }, Color::Black());
	}
}

}
