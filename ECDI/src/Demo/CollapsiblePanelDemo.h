#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Widget/Panel.h"

namespace ECDI{

class Button;

namespace Demo{

/// @brief 展开/折叠面板 demo 容器（9.6 S2——**demo 代码，不入框架**）
/// @details S2 定位（详细设计 §4.2）：验证「动画系统能否成为现有 GUI 状态/布局/渲染
/// 体系的正常消费者」——高度变化 → Geometry 改变 → Layout → Invalidate → 重 Paint 全链路。
/// 经 demo 验证过的实现模式，未来作为 CollapsiblePanel 正式控件的产品化参考
/// （先基础设施 → 首个消费者 → 第二消费者验证 → 再抽象正式控件）。
///
/// 边界原则落地：折叠状态（m_expanded）在 Toggle() 触发点翻转——动画只平滑到达状态；
/// onFinished 仅做收尾表现（更新按钮文案）。
class CollapsiblePanelDemo: public Panel{
public:

	CollapsiblePanelDemo();

	~CollapsiblePanelDemo() override = default;

	/// @brief 展开/折叠切换（d6 值回调式 + d7 token 替换键 + d8 onFinished 全链路消费）
	void Toggle();

	[[nodiscard]] bool IsExpanded() const noexcept{ return m_expanded; }

private:

	static constexpr int kHeaderHeight = 40;	///< 折叠态高度（仅标题按钮）

	static constexpr int kExpandedHeight = 220;	///< 展开态高度

	static constexpr int kContentWidth = 280;	///< 面板/子控件宽度（SetSize 显式给——VerticalLayout 不覆盖尺寸）

	static constexpr int kToggleDurationMs = 200;	///< 高度过渡时长

	Button* m_toggle = nullptr;		///< 标题按钮（树拥有所有权，非拥有指针）

	bool m_expanded = true;			///< 展开状态（触发点翻转——动画不产生状态）

	AnimationToken m_heightToken;	///< 高度动画令牌（每动画属性一个；析构自动标脏）

};

}

}

