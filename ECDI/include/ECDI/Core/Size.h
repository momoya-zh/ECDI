#pragma once

namespace ECDI
{

/// @brief 二维尺寸（float 数值类型）
/// @details 公共基础类型：几何三元组 Point / Rect / Size 的补齐（Phase 5 新增）。
/// 文本测量返回值、未来 GetPreferredSize() 使用。
struct Size
{
	float width = 0.0f;		///< 宽度
	float height = 0.0f;	///< 高度
};

}
