#pragma once

namespace ECDI
{

/// @brief 二维点（float 数值类型，决策 1/§4.2）
/// @details 公共基础类型：全框架共用。默认成员初始化保证默认构造安全。
struct Point
{
	float x = 0.0f;	///< X 坐标
	float y = 0.0f;	///< Y 坐标
};

}
