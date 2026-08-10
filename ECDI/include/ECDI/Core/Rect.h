#pragma once

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

}
