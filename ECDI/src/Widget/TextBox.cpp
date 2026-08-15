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

#include <algorithm>

namespace ECDI{

namespace{   // 匿名 namespace：TextBox 内部常量（不暴露）

	/// @brief 选择高亮色（浅蓝——标准文本选择观感；不写死为魔法数字——主题友好，未来 ThemeSystem 替换）
	const Color kSelectionColor = Color::FromRGBA8(173, 216, 230);

}

TextBox::TextBox(const std::string& text): TextWidget(text){
}

// ── 焦点可见性 ──

void TextBox::OnFocusGained(){
	m_showCaret = true;
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：获焦 → 懒创建系统 caret + 初始位置（双通道）
}

void TextBox::OnFocusLost(){
	m_showCaret = false;
	Invalidate();
	// 5.6 v1.0.3：失焦 → 销毁系统 caret（IME 候选窗回默认——fail-safe）
	if (Window* window = GetWindow()){
		window->DestroyTextInputCaret();
	}
}

// ── 编辑操作（5.5.1.3：码点级编辑——UTF-8 变长不切字）──
// Invalidate 内嵌 = 职责契约：修改了可见状态 → 自身负责请求重绘（防职责泄漏；
// 不依赖平台重绘合并；批量编辑解耦归 Phase 7 两层结构）

void TextBox::InsertCodepoint(char32_t codepoint){
	// 5.5.2：编辑操作自包含——有 Selection 先删选中区（IME 上屏/Ctrl+V/程序/脚本 全走这里）
	if (HasSelection())
		m_caret = DeleteSelection();   // DeleteSelection 内部已同步 m_caret + anchor
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, EncodeUTF8(codepoint));
	++m_caret;
	ClearSelection();   // 5.5.2：插入后同步 anchor——否则产生幽灵选择（anchor=插入前caret, caret=+1）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
}

void TextBox::DeleteBackward(){
	// 5.5.2：有 Selection 删整个选中区（一次，非单字符）
	if (HasSelection()){
		m_caret = DeleteSelection();
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		return;
	}
	if (m_caret == 0)
		return;                                   // 头边界
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t prev = CodepointIndexToByteOffset(m_text, m_caret - 1);
	m_text.erase(prev, cur - prev);               // 删前一个码点的字节区间
	--m_caret;
	ClearSelection();   // 5.5.2：删除后同步 anchor（防之前残留幽灵选择）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
}

void TextBox::DeleteForward(){
	// 5.5.2：有 Selection 删整个选中区
	if (HasSelection()){
		m_caret = DeleteSelection();
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		return;
	}
	if (m_caret >= GetCodepointCount())
		return;                                   // 尾边界
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t next = CodepointIndexToByteOffset(m_text, m_caret + 1);
	m_text.erase(cur, next - cur);
	ClearSelection();   // 5.5.2：DeleteForward 光标不动但同步 anchor（防之前残留）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：文本变化（光标可能越界）→ 更新插入点
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
}

void TextBox::MoveCaretToStart(){
	m_caret = 0;
	ClearSelection();   // 5.5.2：同上（先动后清）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标移动 → 更新插入点
}

void TextBox::MoveCaretToEnd(){
	m_caret = GetCodepointCount();
	ClearSelection();   // 5.5.2：同上（先动后清）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标移动 → 更新插入点
}

size_t TextBox::GetCodepointCount() const{
	return ByteOffsetToCodepointIndex(m_text, m_text.size());
}

// ── 光标几何（5.6 提取：与点击定位同源——CalculateTextPosition 单一入口）──

float TextBox::GetTextAreaWidth() const noexcept{
	// 可视宽度 = 控件宽 − 焦点框内缩（2px×2）——与 OnPaint 原 maxTextWidth 同款逻辑
	// （提取为共享辅助：文本裁切/Selection 高亮/光标 三处共用，改一处不漂移）
	const float padding = HasFocus() ? 2.0f : 0.0f;
	return static_cast<float>(GetWidth()) - padding * 2.0f;
}

Point TextBox::CalculateCaretPosition(TextMeasurer& measurer) const{
	// 与 OnPaint 光标绘制完全同源：一次全文测量 → CalculateTextPosition → 前缀宽 → 可视钳制
	const Size textSize = measurer.MeasureText(m_font, m_text);
	// 空串高度兜底（同 OnPaint 296 行）：GDIBackend::MeasureText("") 返回 {0,0}，垂直定位需行高
	const float lineH = (textSize.height > 0.0f) ? textSize.height : measurer.LineHeight(m_font);
	const Point textPos = CalculateTextPosition(0, 0, textSize.width, lineH);   // 相对控件原点
	const size_t byteOffset = CodepointIndexToByteOffset(m_text, m_caret);
	const Size prefixSize = measurer.MeasureText(m_font, m_text.substr(0, byteOffset));
	// 可视钳制：光标超出可视区钉在右缘（示意"后面还有"；自动水平滚动归 Phase 6）
	const float caretX = (std::min)(prefixSize.width, GetTextAreaWidth());
	// v1.0.3：返回光标顶部（textPos.y，无 +lineH）——系统 caret 语义 = caret 左上角；
	// float 直传（Point 成员 float——转 int 列表初始化触发 C2397 narrowing，且丢精度）
	return Point{ textPos.x + caretX, textPos.y };
}

Point TextBox::GetCaretClientPosition(){
	// 客户区绝对坐标 = 控件绝对位置 + 光标相对控件位置（同一 CalculateCaretPosition 变换链）
	const Point abs = GetAbsolutePosition();
	const Point local = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
	return Point{ abs.x + local.x, abs.y + local.y };
}

void TextBox::SyncTextInputCaret(){
	// 5.6 v1.0.3：光标变动 → 更新系统 caret + ImmSetCompositionWindow 双通道
	// （测量经 Window——GetCaretClientPosition 与点击定位同源；失焦走 DestroyTextInputCaret）
	if (Window* window = GetWindow()){
		window->UpdateTextInputCaret(GetCaretClientPosition());
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

// ── 点击定位算法（5.5.1.4；5.5.2 Selection 拖选/双击/Shift+单击复用）──

size_t TextBox::CaretIndexFromX(TextMeasurer& measurer, float innerX) const{
	if (m_text.empty()) return 0;
	const size_t count = GetCodepointCount();
	// MVP：线性扫描 + 前缀测量（O(n²)——单行短文本够用）；
	// 未来长文本/多行/滚动：缓存码点边界或二分查找（性能注释）
	for (size_t i = 0; i < count; ++i){
		const size_t byte = CodepointIndexToByteOffset(m_text, i + 1);
		const Size prefix = measurer.MeasureText(m_font, m_text.substr(0, byte));
		if (innerX <= prefix.width){
			// 落在第 i 个码点区间：中点判断四舍五入（左半 → i，右半 → i+1）
			const Size prevPrefix = (i == 0) ? Size{}
				: measurer.MeasureText(m_font, m_text.substr(0, CodepointIndexToByteOffset(m_text, i)));
			const float charWidth = prefix.width - prevPrefix.width;
			return (innerX >= prevPrefix.width + charWidth / 2.0f) ? i + 1 : i;
		}
	}
	return count;   // 点在末尾之后
}

// ── 纯数据访问 ──

size_t TextBox::GetCaret() const noexcept{
	return m_caret;
}

// ── 事件映射（事件 → 编辑操作，薄薄一层；逻辑集中在上面）──

void TextBox::OnMouseButtonDown(const MouseButtonDownEvent& event){
	// 焦点获取由 Application 前置处理（5.4.2：CanFocus → SetFocusedWidget）
	// 坐标系：事件 GetMouseX = 窗口客户区绝对；GetAbsolutePosition = 客户区绝对
	const Point abs = GetAbsolutePosition();
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();   // T1 首个消费者——非 Paint 时刻测量
	const Size textSize = measurer.MeasureText(m_font, m_text);
	// 与 OnPaint 完全同源：同一个 CalculateTextPosition → 同一个文本起点
	// （点击定位与绘制共享同一坐标系——未来改居中/内边距/滚动不偏）
	const Point textPos = CalculateTextPosition(
		static_cast<int>(abs.x), static_cast<int>(abs.y),
		textSize.width, textSize.height);
	const float innerX = static_cast<float>(event.GetMouseX()) - textPos.x;   // 相对文本起点
	m_caret = CaretIndexFromX(measurer, innerX);
	m_selectionAnchor = m_caret;   // 5.5.2：锚点 = 点击处（准备拖选）
	m_mouseDown = true;            // 5.5.2：拖选进行中（Capture ≠ 拖选中，TextBox 内部状态）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：点击定位 → 更新插入点
}

void TextBox::OnMouseMove(const MouseMoveEvent& event){
	if (!m_mouseDown)
		return;   // 非拖选中：普通移动忽略（避免误扩展选择）
	// 拖选：同源定位（与 OnMouseButtonDown 同款坐标计算）——active 端跟随鼠标
	const Point abs = GetAbsolutePosition();
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();
	const Size textSize = measurer.MeasureText(m_font, m_text);
	const Point textPos = CalculateTextPosition(
		static_cast<int>(abs.x), static_cast<int>(abs.y),
		textSize.width, textSize.height);
	const float innerX = static_cast<float>(event.GetMouseX()) - textPos.x;
	m_caret = CaretIndexFromX(measurer, innerX);
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：拖选中 active 端跟随 → 更新插入点
}

void TextBox::OnMouseButtonUp(const MouseButtonUpEvent&){
	m_mouseDown = false;   // 结束拖选（保留选择）
}

void TextBox::OnKeyDown(const KeyDownEvent& event){
	switch (event.GetKeyCode()){
	case KeyCode::Backspace: DeleteBackward(); break;
	case KeyCode::Delete:    DeleteForward();  break;
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
	default: break;   // 其他键（含 Tab——被 Window::HandleKeyDown 拦截，到不了这里）
	}

	// 5.6 v1.0.3：方向键/Home/End 分支都改过 caret——switch 后统一同步插入点
	SyncTextInputCaret();
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

	// 1. 白底 + 焦点框
	if (HasFocus()){
		// 焦点框：全块深蓝 → 内缩 2px 白底，露出 2px 深蓝环（白底上深蓝框明显，与 Button 白框相反）
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, Color::FromRGBA8(80, 120, 220));
		ctx.DrawRect(Rect{ fx + 2, fy + 2, fw - 4, fh - 4 }, Color::White());
	}
	else{
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, Color::White());
	}

	// 2. 文本 + 光标（同源：一次 MeasureText 宽高 → CalculateTextPosition 起点）
	// ⚠️ 不用 DrawTextContent——TextBox 需要文本起点/尺寸（光标计算），DrawTextContent 不返回中间值
	if (m_text.empty() && !m_showCaret)
		return;   // 空文本且无光标：省测量（光杆背景已画）

	const Size textSize = ctx.MeasureText(m_font, m_text);
	// ⚠️ 空串高度兜底：GDIBackend::MeasureText("") 返回 {0,0}（GetTextExtentPoint32W 空串 cy=0）
	// → CalculateTextPosition 的 offsetY=(fh-0)/2 垂直定位偏下——空文本+焦点时光标竖线用行高
	const float lineH = (textSize.height > 0.0f) ? textSize.height : ctx.LineHeight(m_font);
	const Point textPos = CalculateTextPosition(x, y, textSize.width, lineH);   // 同源（左对齐+垂直居中）

	// 可视宽度（5.6：GetTextAreaWidth 共享——裁切/Selection/光标 改一处不漂移）
	const float maxTextWidth = GetTextAreaWidth();

	// 2.5 Selection 高亮（白底后、文本前——高亮不盖字；与裁切共享 maxTextWidth 钳制不溢出）
	if (HasSelection()){
		const size_t minByte = CodepointIndexToByteOffset(m_text, GetSelectionMin());
		const size_t maxByte = CodepointIndexToByteOffset(m_text, GetSelectionMax());
		const Size minSize = ctx.MeasureText(m_font, m_text.substr(0, minByte));
		const Size maxSize = ctx.MeasureText(m_font, m_text.substr(0, maxByte));
		const float hlMin = (std::min)(minSize.width, maxTextWidth);
		const float hlMax = (std::min)(maxSize.width, maxTextWidth);
		ctx.DrawRect(Rect{ textPos.x + hlMin, textPos.y, hlMax - hlMin, lineH }, kSelectionColor);
	}

	if (textSize.width <= maxTextWidth){
		// 不超宽：全画（零开销）
		ctx.DrawText(textPos, m_text, m_textColor, m_font);
	}
	else{
		// 超宽：逐码点累计找能放下的最大前缀（MVP 临时方案）
		// TODO(Phase 8): 用裁剪区域（PushClip/clipRect，RenderCommand 层）代替字符串截断——
		// 当前截断 O(n²) 且绘制/数据耦合，MVP 短文本可接受
		size_t visibleCps = 0;
		const size_t count = GetCodepointCount();
		for (size_t i = 0; i < count; ++i){
			const size_t byte = CodepointIndexToByteOffset(m_text, i + 1);
			if (ctx.MeasureText(m_font, m_text.substr(0, byte)).width > maxTextWidth)
				break;
			visibleCps = i + 1;
		}
		if (visibleCps > 0)
			ctx.DrawText(textPos, m_text.substr(0, CodepointIndexToByteOffset(m_text, visibleCps)),
			             m_textColor, m_font);
	}

	// 3. 光标竖线（5.6 改 CalculateCaretPosition：点击定位/光标绘制/IME 同一变换链）
	if (m_showCaret){
		// 测量经 Window：PaintContext 封装不暴露 measurer（决策 8）——
		// 与 PaintFrame 注入为同一 TextMeasurer 实例（m_backend），结果一致
		// v1.0.3：CalculateCaretPosition 返回光标顶部——竖线直接用它（系统 caret 同锚点）
		const Point caretLocal = CalculateCaretPosition(GetWindow()->GetTextMeasurer());
		ctx.DrawRect(Rect{ fx + caretLocal.x, fy + caretLocal.y, 2.0f, lineH }, Color::Black());
	}
}

}
