#pragma once

#include "ECDI/Widget/TextWidget.h"
#include "ECDI/Widget/CaretGeometry.h"

#include <functional>
#include <optional>

namespace ECDI{

/// @brief 单行文本框（5.5；第三个文本控件，继承 TextWidget）
/// @details 职责：码点级文本编辑（光标/插入/删除/移动）+ 绘制（白底/文本/光标/焦点框）。
/// 编辑操作与事件解耦：OnKeyDown/OnCharInput 只做"事件 → 操作"映射，
/// 逻辑集中在 InsertCodepoint/DeleteBackward 等——可编程、可测试（main.cpp 断言不依赖窗口）。
class TextBox: public TextWidget{

public:

	TextBox() = default;

	explicit TextBox(const std::string& text);

	bool CanFocus() const noexcept override { return true; }

	// ── 回调注册（7.5 新增：表单/数据绑定核心需求）──────────

	using TextChangedCallback = std::function<void(const std::string&)>;   ///< 文本变化回调类型

	/// @brief 注册文本变化回调（覆盖式：传空 = 解除注册）
	/// @details 触发点 = 编辑操作（InsertCodepoint/DeleteBackward/DeleteForward）
	/// 实际改变文本时（D7：SetText 不触发——避免初始化误报）。
	/// 回调在 RaiseTextChanged() 内、OnTextChanged() 虚方法之后调用——
	/// 子类 override OnTextChanged 不影响回调触发（D4 RaiseXxx 分离模式）。
	/// @param callback 参数 = 新文本（UTF-8）
	void SetOnTextChanged(TextChangedCallback callback);

	// ── 光标方向（类型即文档：MoveCaret(-1) 的 -1 是什么？——可读性）──
	enum class CaretDirection{ Left, Right };

	// ── 编辑操作（⚠️ 临时 public：5.5.1 阶段为可测试；Phase 7 API 审查重新定可见性，
	//    届时可能是"公开高层 API（InsertText/Clear/SetCaret）+ protected 底层原语"两层结构）──
	void InsertCodepoint(char32_t codepoint);   // 光标处插入（5.5.2：有 Selection 先删选中区）
	void DeleteBackward();                      // Backspace：删光标前一码点
	void DeleteForward();                       // Delete：删光标后一码点
	void MoveCaret(CaretDirection direction);   // ←→（边界钳制）
	void MoveCaretToStart();                    // Home
	void MoveCaretToEnd();                      // End
	size_t GetCaret() const noexcept;           // 码点索引

	// ── 选择查询（7.2 新增：只读，无副作用——Phase 10 集成测试前置）──────────

	/// @brief 选择区间（start <= end；码点索引，非字节偏移）
	struct SelectionRange {
		size_t start;
		size_t end;
	};

	/// @brief 获取当前选中区（如果存在）
	/// @return 无选中区 → nullopt；有选中区 → SelectionRange{min, anchor}
	/// @details 只读查询——不修改内部状态；供调试/测试/序列化使用。
	std::optional<SelectionRange> GetSelection() const;

	// ── IME 位置（5.6；7.1.3 升级 CaretGeometry）────────────────
	/// @brief 光标客户区几何（文本输入插入点——系统 caret/IME 候选窗锚点）
	/// @details 纯几何查询：GetAbsolutePosition + CalculateCaretPosition（与光标绘制同源）。
	/// 返回值 = 窗口客户区坐标（与 GetAbsolutePosition/事件 GetMouseX 同一坐标系——
	/// 非屏幕坐标、非控件相对坐标；命名保留决议 2026-08-14：项目内 Client == 窗口客户区已统一）。
	/// 7.1.3 改名（GPT 二轮）：返回值已是 CaretGeometry（rect + 逻辑可见性），Position 名不副实。
	/// 平台转换是平台职责（TextBox 零平台依赖，只输出客户区几何）。
	/// 非 const：测量需经 GetWindow()->GetTextMeasurer()（Window 接口非 const，与 OnMouseButtonDown 同性质）。
	CaretGeometry GetCaretClientGeometry();

protected:

	void OnFocusGained() override;              // 显示光标
	void OnFocusLost() override;                // 隐藏光标
	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 点击定位 + 拖选锚点（5.5.1.4/5.5.2）
	void OnMouseMove(const MouseMoveEvent&) override;               // 拖选扩展 active 端（5.5.2）
	void OnMouseButtonUp(const MouseButtonUpEvent&) override;       // 结束拖选（5.5.2）
	void OnKeyDown(const KeyDownEvent&) override;    // 编辑键映射（5.5.1.3）+ Shift+方向键扩展（5.5.2）
	void OnCharInput(const CharInputEvent&) override; // 字符插入（5.5.1.3）
	void OnPaint(PaintContext& ctx, int x, int y) override;

	/// @brief 文本变化虚方法（子类可 override 扩展行为；空实现）
	/// @details 调用链：编辑操作 → RaiseTextChanged → OnTextChanged() + m_onTextChanged()
	/// 保护可见性：仅子类/自身可调（D3 GPT 修订）
	virtual void OnTextChanged(const std::string& text);

private:

	/// @brief 文本变化通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先调 OnTextChanged() 虚方法，再调 m_onTextChanged() 回调——彼此独立
	void RaiseTextChanged();

	size_t m_caret = 0;          ///< 光标位置（码点索引；构造后默认文本起始，不自动跳末尾——标准行为）
	bool m_showCaret = false;    ///< 是否显示光标（焦点时 true；闪烁状态未来另立）

	size_t m_selectionAnchor = 0;   ///< 选择锚点（固定端；无选择时无意义——Selection 扩张/收缩的核心）
	bool m_mouseDown = false;       ///< 鼠标按下（拖选进行中——Capture ≠ 拖选中，两状态分离；未来双击/三击升级 m_dragSelecting 6.x）

	TextChangedCallback m_onTextChanged;   ///< 文本变化回调（7.5 新增：表单/数据绑定核心需求）

	/// @brief 码点总数（私有辅助——消除 DeleteForward/MoveCaret/MoveCaretToEnd 多处重复统计）
	size_t GetCodepointCount() const;

	/// @brief 文本内 x 偏移 → 最近码点索引（点击定位算法——5.5.2 Selection 拖选/双击/Shift+单击复用）
	/// @param innerX 相对文本起点的 x（与绘制同源：OnMouseButtonDown 经 CalculateTextPosition 计算）
	size_t CaretIndexFromX(TextMeasurer& measurer, float innerX) const;

	// ── Selection 辅助（5.5.2；全 private——内部算法不暴露，Phase 7 测试体系补测）──

	bool HasSelection() const noexcept;         // anchor != caret
	size_t GetSelectionMin() const noexcept;    // min(anchor, caret)——绘制/删除用
	size_t GetSelectionMax() const noexcept;    // max(anchor, caret)
	size_t DeleteSelection();                   // 删选中区 + 返回新光标位置（min 处）
	void ClearSelection() noexcept;             // anchor = caret（无效化）

	// ── 光标几何（5.6 提取：消灭三处漂移——点击定位/光标绘制/IME 同源）──

	/// @brief 可视文本宽度（控件宽 − 焦点框内缩 2px×2）——文本裁切/Selection 高亮/光标 共用
	float GetTextAreaWidth() const noexcept;

	/// @brief 光标像素位置（光标**顶部**：textPos.y；相对文本框左上角——不含绝对窗口偏移；含可视钳制）
	/// @param measurer 测量器——调用方决定来源（OnPaint 经 GetWindow()->GetTextMeasurer()，
	/// PaintContext 封装不暴露 measurer；与点击定位 CaretIndexFromX 同源但不合并——方向相反）
	/// @details v1.0.3：统一顶部锚点（系统 caret 语义=caret 左上角；OnPaint 竖线直接用它）
	Point CalculateCaretPosition(TextMeasurer& measurer) const;

	/// @brief 同步文本输入插入点（5.6 v1.0.3：光标变动 → Window::UpdateTextInputCaret 双通道）
	/// @details 11 个调用点统一入口：焦点获/失、点击、拖选、方向键、编辑操作。
	/// 失焦用 DestroyTextInputCaret 不走本方法。
	void SyncTextInputCaret();

};

}
