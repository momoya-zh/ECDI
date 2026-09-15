#include "ECDI/Window/CaptionBar.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Theme/PanelStyle.h"
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/WindowState.h"
#include "Window/CaptionButton.h"

#include <algorithm>
#include <memory>

namespace ECDI{

namespace{

	// 布局常量（逻辑像素 DIP——详设 §3.3：常量集中在实现内）
	constexpr int kButtonWidth   = 46;	///< 按钮宽（Windows 11 caption 按钮量级；高 = 标题栏高）
	constexpr int kTitleLeftPad  = 12;	///< 标题左边距
	constexpr int kTitleRightGap = 8;	///< 标题右留白（标题与最小化按钮之间的空白 = 拖拽区）

}

CaptionBar::CaptionBar(Window& window, const std::string& title)
	: m_window(window){

	// v0.1 默认视觉（D5：样式 API 留待二次用例；主题集成为本阶段非目标）——
	// 底色经**继承的** Panel::SetStyle 注入（Panel 能力直接消费，零新增 API）。
	SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(38, 40, 45, 255) });

	auto label = std::make_unique<Label>(title);

	// ⚠️ 标题前景色必须在此注入：DefaultTheme 的文本色是黑色（不可读于深色标题栏），
	//    而本阶段不提供标题样式 API ⇒ 不注入则消费者无从改变标题颜色（详设 L2 记账）。
	label->SetStyle(TextStyleOverride{ .foreground = Color::FromRGBA8(235, 236, 240, 255) });

	m_title = label.get();

	auto minButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Minimize, [this]{ m_window.Minimize(); });

	m_minButton = minButton.get();

	auto maxButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Maximize, [this]{ ToggleMaximizeRestore(); });

	m_maxButton = maxButton.get();

	auto closeButton = std::make_unique<CaptionButton>(
		CaptionButton::Glyph::Close, [this]{ m_window.RequestClose(); });

	m_closeButton = closeButton.get();

	// 顺序 = 树内顺序（互不重叠 ⇒ 与 Z 序无关）；先标题后按钮
	AddChild(std::move(label));
	AddChild(std::move(minButton));
	AddChild(std::move(maxButton));
	AddChild(std::move(closeButton));

	RelayoutChildren();	// 初始几何（父布局 Arrange 会以新宽度再次 SetSize → 重排）

}

void CaptionBar::SetTitle(const std::string& title){

	m_title->SetText(title);

}

const std::string& CaptionBar::GetTitle() const noexcept{

	return m_title->GetText();

}

void CaptionBar::SetSize(int w, int h){

	Widget::SetSize(w, h);	// 基类几何（Panel 未 override SetSize——直连 Widget）

	RelayoutChildren();		// 复合控件同步子控件几何（CollapsiblePanel::SetSize 同款先例）

}

void CaptionBar::RelayoutChildren(){

	const int w = GetWidth();

	const int h = GetHeight();

	// 三按钮靠右依次排布（左→右：min | max | close——系统惯例 close 在最右）
	const int closeX = w - kButtonWidth;

	const int maxX = w - 2 * kButtonWidth;

	const int minX = w - 3 * kButtonWidth;

	m_minButton->SetPosition(minX, 0);
	m_minButton->SetSize(kButtonWidth, h);

	m_maxButton->SetPosition(maxX, 0);
	m_maxButton->SetSize(kButtonWidth, h);

	m_closeButton->SetPosition(closeX, 0);
	m_closeButton->SetSize(kButtonWidth, h);

	// 标题占左侧剩余（宽度钳 0——窗口极窄时不出现负宽；越界文本由控件自身 PushClip 裁切）
	const int titleW = (std::max)(0, minX - kTitleLeftPad - kTitleRightGap);

	m_title->SetPosition(kTitleLeftPad, 0);
	m_title->SetSize(titleW, h);

}

void CaptionBar::OnPaint(PaintContext& ctx, int x, int y){

	// 二态刷新：绘制前把 max 按钮 glyph 设为当前窗口状态对应的形态。
	// 为什么无需订阅 WindowStateChangedEvent：最大化 / 还原**必产生 WM_SIZE** →
	// Window::OnResized → Invalidate → 重绘，故每次绘制时读「现在是什么」即可
	// （D6：状态是事实、由 Window 提供；也避免引入新的订阅接缝）。
	m_maxButton->SetGlyph(
		m_window.GetWindowState() == WindowState::maximized
			? CaptionButton::Glyph::Restore
			: CaptionButton::Glyph::Maximize);

	Panel::OnPaint(ctx, x, y);	// 背景 / 边框（继承能力）

}

void CaptionBar::ToggleMaximizeRestore(){

	// 读当前事实决定动作（不缓存状态——D6 / O4）
	if (m_window.GetWindowState() == WindowState::maximized){

		m_window.Restore();

	}
	else{

		m_window.Maximize();	// 非 maximized 一律调 Maximize（含 minimized——由平台/系统决定恢复行为，不加特判）

	}

}

}
