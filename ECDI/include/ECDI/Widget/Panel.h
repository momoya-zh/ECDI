#pragma once

#include "Widget.h"

namespace ECDI{

/// @brief 容器面板 Widget
/// @details
/// 纯容器语义：用于组织和分组子 Widget，自身没有额外行为。
/// 所有能力（AddChild/RemoveChild/HitTest/Bubbling）继承自 Widget 基类。
///
/// 使用场景：
/// - 作为其他控件的父容器
/// - 在 Widget 树中形成逻辑分组
/// - 为未来 Layout System 提供容器基础
///
/// @note 当前阶段不实现任何 Layout 能子 Widget 的排列方式由用户手动设置。
class Panel : public Widget{

public:

	Panel() = default;

	~Panel() override = default;

protected:

	void OnPaint(HDC hdc,int x,int y)override;
};

}
