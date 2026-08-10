#pragma once
namespace ECDI
{

/// @brief Widget 的基础布尔状态
struct WidgetState{

	bool visible = true;	///< 是否可见（不可见的 Widget 在 HitTest 中被跳过）

	bool enabled = true;	///< 是否启用（禁用的 Widget 在 HitTest 中被跳过）

};

}
