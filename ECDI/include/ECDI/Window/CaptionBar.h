#pragma once

#include "ECDI/Widget/Panel.h"

#include <string>

namespace ECDI{

class Window;
class Label;
class CaptionButton;

/// @brief 自绘标题栏（Phase 13 R1）——Borderless 窗口的标题文本 + 最小化 / 最大化·还原 / 关闭
/// @details **组合式控件**（D1-A：CaptionBar 是 Widget，不是 Window 的隐式组成部分）：
/// 基类 Panel（继承背景 / 圆角 / 边框能力，以及「自身永不参与命中」语义——Panel::ContainsPoint 恒 false
/// ⇒ 标题栏空白区天然是拖拽区），内部持 1 个标题 Label + 3 个按钮子控件（矢量自绘，零资源依赖）。
///
/// 用法（把「行为区」与「实体区」设为同值——D7 不联动，建议一致）：
///
///   ECDI::Window& w = app.Create("标题", 900, 600);
///   w.SetChromeMode(ECDI::ChromeMode::Borderless);
///   w.SetCaptionHeight(ECDI::CaptionBar::kDefaultHeight);   // 行为区（系统标题栏命中区）
///   auto bar = std::make_unique<ECDI::CaptionBar>(w, "标题");
///   bar->SetSize(900, ECDI::CaptionBar::kDefaultHeight);    // 实体区（控件几何）
///   w.GetRootWidget().AddChild(std::move(bar));
///   w.Show();
///
/// ⚠️ **宽度自适应**：本控件 override SetSize 在内部重排子控件；若父容器带 fillCrossAxis 布局
/// （如 RootWidget 的 VerticalLayout），窗口 resize → Arrange → SetSize → 子控件自动跟随（零额外接线）。
///
/// 生命周期（B 契约）：本控件**非拥有** Window&——只发起窗口命令
/// （Minimize / Maximize / Restore / RequestClose）与状态查询（GetWindowState）。
class CaptionBar : public Panel{
public:

	/// @brief 建议高度（DIP）——与 Window::SetCaptionHeight 建议同值（**框架不联动**，D7）
	static constexpr int kDefaultHeight = 32;

	/// @brief 构造（标题 + 三按钮子控件建好并入树）
	/// @param window 所属窗口（**非拥有**引用——B 契约）
	/// @param title  标题文本（可后改）
	explicit CaptionBar(Window& window, const std::string& title = std::string());

	/// @brief 设置标题文本（转发标题 Label）
	void SetTitle(const std::string& title);

	/// @brief 标题文本（只读）
	const std::string& GetTitle() const noexcept;

	/// @brief 设置几何（override：先记尺寸，再按新宽度重排子控件——标题占左、三按钮靠右）
	/// @details 与 CollapsiblePanel::SetSize 同款先例（复合控件在 SetSize 内同步子控件几何）。
	/// 宽度变化的每次调用（含父布局 fillCrossAxis 的每次 Arrange）都会重排 ⇒ 窗口 resize 零接线。
	/// ⚠️ **可重复调用契约**：本方法必须幂等——RelayoutChildren 只按**当前** w/h 重新计算，
	/// 不读取上一次布局结果、无累积偏移（父布局每次 Arrange 都会调用它，故这是硬性前提）。
	void SetSize(int w, int h) override;

protected:

	/// @brief 绘制前刷新 max 按钮二态 glyph，再走 Panel 背景绘制
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:

	/// @brief 按当前宽高重排子控件
	void RelayoutChildren();

	/// @brief 最大化 ↔ 还原切换（读当前事实决定动作——状态不由本控件缓存，D6）
	void ToggleMaximizeRestore();

	Window& m_window;	///< 非拥有（D1-A / B 契约）

	Label* m_title = nullptr;	///< 标题（树内地址稳定——AddChild 前抓裸指针先例）
	CaptionButton* m_minButton = nullptr;	///< 最小化
	CaptionButton* m_maxButton = nullptr;	///< 最大化·还原（二态——OnPaint 前按窗口状态设 glyph）
	CaptionButton* m_closeButton = nullptr;	///< 关闭（→ m_window.RequestClose()）

};

}
