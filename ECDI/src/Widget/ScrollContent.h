#pragma once

#include "ECDI/Widget/Widget.h"

namespace ECDI{

class ScrollView;

/// @brief 内容坐标空间的根（Phase 15——**唯一被内容偏移作用的子树**）
/// @details 为什么需要本类：`Widget::GetContentOffsetX/Y()` 的语义是「作用于**全部直接子节点**」。
/// 若把偏移 override 在 `ScrollView` 自身，则**滚动条作为直接子也会被一起偏移**
/// ——视觉 / HitTest / GetAbsolutePosition 三个坐标系同时错（详设 §3.1.1 有反例表）。
/// 因此把偏移的消费者**下沉**到本节点：`ScrollView` 自身偏移恒 0（三个直接子都不受影响），
/// 只有本节点的子树进入**内容坐标系**。
///
/// 依赖形态：持所属 `ScrollView&`（**非拥有**）——`CaptionButton` 持 `Window&` 的既有先例。
/// 位置：`src/` 内部头（与 `src/Window/CaptionButton.h` 同款），**不进 Public 头计数**。
///
/// **有意不 override 的两项**：
/// - `ClipsChildren()` 保持默认 `false`——视口约束由 `ScrollView` 一层负责即够
///   （`HitTest` 逐层检查等价于祖先裁剪域交集）；
/// - `ConsumesMouseInput()` 保持默认 `false`——本节点是内容容器而非交互控件。
class ScrollContent : public Widget{

public:

	/// @brief 构造
	/// @param owner 所属滚动容器（**非拥有**——生命周期由 `ScrollView` 的子树拥有）
	explicit ScrollContent(ScrollView& owner);

	~ScrollContent() override = default;

	/// @brief 内容偏移 X（**接缝实现**——转发所属 `ScrollView` 的权威偏移）
	[[nodiscard]] int GetContentOffsetX() const noexcept override;

	/// @brief 内容偏移 Y（转发）
	[[nodiscard]] int GetContentOffsetY() const noexcept override;

private:

	ScrollView& m_owner;   ///< 非拥有（树内节点——父先于子析构）

};

}
