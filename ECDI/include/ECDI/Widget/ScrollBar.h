#pragma once

#include "ECDI/Theme/ScrollBarStyle.h"
#include "ECDI/Widget/Widget.h"

#include <functional>

namespace ECDI{

class Theme;
class MouseButtonDownEvent;
class MouseButtonUpEvent;
class MouseMoveEvent;

/// @brief 滚动条（Phase 15 R2——轨道 + 滑块，自绘 + 主题化）
/// @details 纯**展示 + 输入**层（D11）：**不持有**偏移权威——偏移由 `ScrollView` 裁决。
/// 本类只做两件事：把 `(contentExtent, viewportExtent, offset)` 三元组**画出来**，
/// 以及把用户操作（拖拽 / 轨道翻页）**翻译成新 offset 并通知容器**。
/// 形态与 `ProgressBar` 同构（继承 `Widget` + 构造注入 Style + `ApplyTheme`/`SetStyle`）。
///
/// 能力消费：只需 `DrawRect`（轨道）+ `DrawRoundedRect`（滑块）——**全部是 Phase 8 既有能力**，
/// 零新增 `RenderCommand`（Backend 完全不知道"这里是滚动条"）。
///
/// 与 `ScrollView` 的对照（Phase 13 D9 语义）：
/// - `ScrollBar::ConsumesMouseInput()` == `true`（**条是交互控件**——拖拽要阻止窗口拖拽）；
/// - `ScrollView::ConsumesMouseInput()` == `false`（**空白是容器**——`--borderless` 下仍可拖窗）。
class ScrollBar : public Widget{

public:

	/// @brief 朝向（构造确定、无 setter——YAGNI）
	enum class Orientation : unsigned char{
		Vertical   = 0,   ///< 垂直（轨道高 = 可用高；滑块沿 Y 移动）
		Horizontal = 1    ///< 水平（轨道宽 = 可用宽；滑块沿 X 移动）
	};

	/// @brief 构造
	/// @param orientation 朝向（默认垂直）
	explicit ScrollBar(Orientation orientation = Orientation::Vertical);

	~ScrollBar() override;

	// ── 范围模型（三元组——D11：由 ScrollView 单向驱动）────────

	/// @brief 设置范围（内容长度 / 视口长度——本类据此推导滑块长度与位置）
	void SetRange(int contentExtent, int viewportExtent);

	/// @brief 设置偏移（外部驱动；内部 clamp 到 `[0, GetMaxOffset()]`；**不触发回调**）
	void SetOffset(int offset);

	[[nodiscard]] int GetOffset() const noexcept;

	/// @brief 最大偏移（`max(0, contentExtent - viewportExtent)`）
	[[nodiscard]] int GetMaxOffset() const noexcept;

	[[nodiscard]] Orientation GetOrientation() const noexcept;

	/// @brief 条厚度（样式驱动——**参与 `ScrollView` 的 viewport 计算**，详设 §3.3）
	/// @details 单独暴露的动机：厚度不是本控件私有视觉量，`ScrollView` 要用它扣 viewport；
	/// 而厚度归 Style（主题可改）⇒ 必须单点可查，不能在两处各读一份常量。
	[[nodiscard]] int GetThickness() const noexcept { return m_style.thickness.value; }

	// ── 通知（D11：变化时直接通知容器——`std::function` 有 `CaptionButton` 先例）──

	/// @brief 偏移变化回调（**用户操作**触发；`SetOffset` 不触发——防止容器↔条递归）
	void SetOnOffsetChanged(std::function<void(int)> callback);

	// ── Phase 9：主题与样式（D7 契约）────────────────────────

	void ApplyTheme(const Theme& theme);
	void SetStyle(ScrollBarStyleOverride override);

	// ── 接缝 ────────────────────────────────────────────────

	/// @brief **消费鼠标输入**（Phase 13 D9——本类是交互控件，不是容器）
	[[nodiscard]] bool ConsumesMouseInput() const noexcept override { return true; }

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;
	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;
	void OnMouseButtonUp(const MouseButtonUpEvent& event) override;
	void OnMouseMove(const MouseMoveEvent& event) override;

	ScrollBarStyle m_style;   ///< 样式（protected——测试派生类可访问，同 ProgressBar 先例）

private:

	/// @brief 轨道长度（主轴：垂直取高 / 水平取宽）
	[[nodiscard]] int TrackLength() const noexcept;

	/// @brief 滑块长度（比例映射 + 最小长度钳制；无内容/无轨道时占满）
	[[nodiscard]] int ThumbLength() const noexcept;

	/// @brief 滑块起点（主轴，相对本控件原点）
	[[nodiscard]] int ThumbStart() const noexcept;

	/// @brief 滑块矩形（**相对本控件原点**——`Local` 后缀标明坐标系）
	[[nodiscard]] Rect ThumbRectLocal() const noexcept;

	/// @brief 拖拽反推：滑块起点 → 新偏移（`denom <= 0` 时返回 0——除零保护）
	[[nodiscard]] int OffsetFromThumbStart(int thumbStart) const noexcept;

	/// @brief 鼠标主轴坐标（**先把事件坐标换算到自身局部**，再取主轴：垂直取 Y / 水平取 X）
	/// @details ⚠️ **鼠标事件的 `GetMouseX/Y` 是「窗口客户区绝对坐标」，不是控件局部坐标**
	/// （既有约定——`TextBox.cpp:895` 有坐标系记录；`Button`/`CaptionButton` 的 I6 判定与
	/// `TextBox` 的点击定位/拖选**都先减 `GetAbsolutePosition()`**）。
	/// 本控件内部一律用自身局部坐标（与 `ThumbStart`/`TrackLength` 同系）⇒ 换算**必须在此完成**。
	/// 漏掉换算的症状：拖拽/翻页按客户区坐标计算，滑块位置与鼠标无关（点哪都跳到别处）。
	[[nodiscard]] int MainAxisPosFromClient(int clientX, int clientY) const noexcept;

	/// @brief 通知容器偏移变化（回调可空——条可独立使用，D9）
	void NotifyOffset();

	static constexpr int kMinThumbLength = 24;   ///< 最小可抓取滑块长度

	Orientation m_orientation;      ///< 朝向（构造确定）

	int m_contentExtent  = 0;       ///< 内容长度（主轴）
	int m_viewportExtent = 0;       ///< 视口长度（主轴）
	int m_offset         = 0;       ///< 当前偏移（外部驱动）
	int m_dragGrabOffset = 0;       ///< 拖拽时鼠标落在滑块内的相对位置（防跳动）
	bool m_dragging      = false;   ///< 拖拽中（Capture 由 Application 隐式接管）
	bool m_hovered       = false;   ///< 滑块 hover 态（视觉）

	std::function<void(int)> m_onOffsetChanged;   ///< 变化通知（可空）

};

}
