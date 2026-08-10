#pragma once
namespace ECDI
{

/// @brief Widget 的位置与尺寸（局部坐标系）
/// @details
/// x, y 是相对于父 Widget 的偏移。
/// width, height 是 Widget 自身的宽高。
struct Geometry{

	int x = 0;		///< 相对于父 Widget 的 X 偏移
	int y = 0;		///< 相对于父 Widget 的 Y 偏移

	int width = 0;	///< 宽度
	int height = 0;	///< 高度

};

}
