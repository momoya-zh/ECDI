#pragma once

#include <algorithm>   // (std::min)/(std::max)——括号防御 Windows min/max 宏（Phase 24 教训）

namespace ECDI
{

/// @brief 矩形（float 数值类型，决策 1/§4.2）
/// @details 公共基础类型：全框架共用（Widget Geometry / RenderCommand / Layout）。
/// 默认成员初始化保证默认构造安全（仍为聚合类型，可列表初始化）。
struct Rect
{
	float x = 0.0f;			///< 左上角 X（相对父）
	float y = 0.0f;			///< 左上角 Y（相对父）
	float width = 0.0f;		///< 宽度
	float height = 0.0f;	///< 高度
};

/// @brief 矩形相交判定（Phase 27 视口剔除判据——初设 Q5 冻结）
/// @details float 同源（与推入 PushClip 的矩形同一量化规则，不引入第二量化）；
/// 交集宽、高均严格 > 0——边界接触 = 不相交（= 剔除），无边距。
inline bool Intersects(const Rect& a, const Rect& b) noexcept
{
	const float ix = (std::max)(a.x, b.x);
	const float iy = (std::max)(a.y, b.y);
	return (std::min)(a.x + a.width,  b.x + b.width)  - ix > 0.0f
	    && (std::min)(a.y + a.height, b.y + b.height) - iy > 0.0f;
}

}
